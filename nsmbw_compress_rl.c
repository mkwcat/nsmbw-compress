#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_rl_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  const uint8_t *const src_end = src + src_length;
  const uint8_t *const dst_start = dst;

  if (src + sizeof(uint32_t) > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed run-length file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const uint8_t option = header & 0xF;
  uint32_t read_size = header >> 8;

  if (option != 0) {
    nsmbw_compress_print_error(
        "Unknown run-length option in input data: %d (expected 0)", option);
    return false;
  }

  src += sizeof(uint32_t);

  if (read_size == 0) {
    if (src + sizeof(uint32_t) > src_end) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid compressed run-length file");
      return false;
    }
    read_size = ncutil_read_le_u32(src, 4);

    src += sizeof(uint32_t);
  }

  assert(read_size <= *dst_length);
  if (read_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in run-length file");
    return false;
  }

  const uint32_t size = read_size;
  const uint8_t *const dst_end = dst + size;

  while (dst < dst_end) {
    if (src + 1 >= src_end) {
      nsmbw_compress_print_error(
          "Input file ended prematurely while decoding run-length data");
      return false;
    }

    uint8_t byte = *src++;
    int32_t run_length = byte & 0x7F;
    if (byte & 0x80) {
      // Run of repeated byte
      run_length += 3;
      if (dst + run_length > dst_end) {
        nsmbw_compress_print_error(
            "Run-length run exceeds the end of the output file");
        return false;
      }

      memset(dst, *src++, run_length);
      dst += run_length;
    } else {
      // Run of literal bytes
      run_length += 1;
      if (src + run_length > src_end) {
        nsmbw_compress_print_error(
            "Input file ended prematurely while reading literal run in "
            "run-length data");
        return false;
      }
      if (dst + run_length > dst_end) {
        nsmbw_compress_print_error(
            "Literal run exceeds the end of the output file");
        return false;
      }

      memcpy(dst, src, run_length);
      dst += run_length;
      src += run_length;
    }
  }

  if (ncutil_align_up_ptr(0x20, src) < (const void *)src_end) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        (size_t)(src_end - src));
  }

  *dst_length = size;
  return true;
}

bool nsmbw_compress_rl_encode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  const size_t max_dst_size = *dst_length;
  const uint8_t *dst_start = dst;
  const uint8_t *dst_end = dst + max_dst_size;
  const uint8_t *src_end = src + src_length;

  if (src_length < 0x1000000) {
    ncutil_write_le_u32(dst, 0,
                        src_length << 8 | CX_COMPRESSION_TYPE_RUN_LENGTH);
    dst += sizeof(uint32_t);
  } else {
    ncutil_write_le_u32(dst, 0, CX_COMPRESSION_TYPE_RUN_LENGTH);
    ncutil_write_le_u32(dst, sizeof(uint32_t), src_length);
    dst += sizeof(uint32_t) + sizeof(uint32_t);
  }

  while (src < src_end) {
    uint8_t byte = *src++;
    int32_t run_length = 1;
    while (src < src_end && run_length < 0x7F + 3) {
      if (*src != byte) {
        break;
      }
      run_length++;
      src++;
    }
    if (run_length > 2) {
      // Run of repeated byte
      run_length -= 3;
      if (dst + 2 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        return false;
      }

      *dst++ = (run_length & 0x7F) | 0x80;
      *dst++ = byte;
    } else {
      // Run of literal bytes
      run_length = 1;
      while (src < src_end && run_length < 0x7F + 1) {
        if (src + 1 < src_end && *src == src[1] && *src == byte) {
          break;
        }
        run_length++;
        src++;
      }
      if (dst + 1 + run_length > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        return false;
      }

      *dst++ = (run_length - 1) & 0x7F;
      memcpy(dst, src - run_length, run_length);
      dst += run_length;
    }
  }

  // Pad to 4 bytes
  while ((dst - dst_start) % 4 != 0) {
    if (dst + 1 > dst_end) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
      return false;
    }
    *dst++ = 0;
  }

  *dst_length = dst - dst_start;
  return true;
}
