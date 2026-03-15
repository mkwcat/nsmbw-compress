#include "lz.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Reference: EGG::Decomp::decodeSZS from ogws
bool nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  (void)params;

  if (src_length < 0x10 || src[0] != 'Y' || src[1] != 'a' || src[2] != 'z' ||
      (src[3] != '0' && src[3] != '1')) {
    nsmbw_compress_print_error("Input data is not a valid SZS file");
    return false;
  }

  size_t expand_size = ncutil_read_be_u32(src, 4);
  assert(expand_size <= *dst_length);
  if (expand_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in SZS file");
    return false;
  }
  size_t src_index = 0x10u; // Skip header
  uint8_t bit = 0;
  uint8_t chunk;

  for (size_t dst_index = 0; dst_index < expand_size; bit >>= 1) {
    if (src_index >= src_length) {
      nsmbw_compress_print_error("Input SZS data ended prematurely");
      return false;
    }

    // Refresh code bits
    if (bit == 0) {
      bit = 0b10000000u;
      chunk = src[src_index++];
    }

    // Literal (chunk bit is set)
    if (chunk & bit) {
      dst[dst_index++] = src[src_index++];
      continue;
    }

    // Back-reference (chunk bit is not set)
    // Next bytes contain run offset, length
    if (src_index + 1 >= src_length) {
      nsmbw_compress_print_error("Input SZS data ended prematurely");
      return false;
    }
    unsigned packed = src[src_index] << 8 | src[src_index + 1];
    src_index += 2u;

    // Short runs (N <= 15 + 2) use two bytes:
    //     NF FF (N=size, F=offset)
    // Minimum run size is 2 (overhead)
    //
    // Long runs (N <= 255 + 15 + 3) use three bytes:
    //     0F FF NN (N=size, F=offset)
    // Minimum run size is 0xF (max short run) + 3 (overhead)

    if ((packed & 0x0FFFu) > dst_index) {
      nsmbw_compress_print_error("Out of bounds reference in SZS compressed "
                                 "file (offset %u at output position %zu)",
                                 packed & 0x0FFFu, dst_index);
      return false;
    }
    unsigned run_index = dst_index - (packed & 0x0FFFu);
    unsigned run_length = (packed >> 12) == 0
                              ? src[src_index++] + 0xF + 3 // Long run
                              : (packed >> 12) + 2;        // Short run

    if (expand_size - dst_index < run_length) {
      nsmbw_compress_print_error(
          "Reference overflows output size in SZS compressed file (offset %u, "
          "size %u at output position %zu with output size %zu)",
          packed & 0x0FFFu, run_length, dst_index, expand_size);
      return false;
    }

    for (; run_length > 0; run_length--, dst_index++, run_index++) {
      dst[dst_index] = dst[run_index - 1];
    }
  }

  *dst_length = expand_size;
  return true;
}

bool nsmbw_compress_szs_encode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  if (*dst_length < 0x10) {
    nsmbw_compress_print_error("Output buffer is too small for Yaz0 header");
    return false;
  }

  static const uint32_t window_size = 0x1000;
  const unsigned max_match_size = 255 + 15 + 3;
  const size_t max_dst_size = *dst_length;
  const uint8_t *dst_start = dst;
  const uint8_t *dst_end = dst + max_dst_size;
  const uint8_t *src_end = src + src_length;

  dst[0] = 'Y';
  dst[1] = 'a';
  dst[2] = 'z';
  dst[3] = '0';
  ncutil_write_be_u32(dst, 4, src_length);
  memset(dst + 8, 0, 8); // Unused padding bytes

  dst += 0x10;

  void *work_buffer =
      malloc(nsmbw_compress_lz_get_work_size(window_size, false));
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LZ compression work buffer: %s",
        strerror(errno));
    return false;
  }

  struct nsmbw_compress_lz_context context;
  nsmbw_compress_lz_init_context(&context, work_buffer, window_size, false);

  while (src < src_end) {
    uint8_t flags = 0;
    uint8_t *flags_ptr = dst++;

    for (int i = 0; i < 8; i++) {
      flags <<= 1;
      if (src >= src_end) {
        continue;
      }

      uint16_t match_distance;
      uint32_t match_size = nsmbw_compress_lz_search_window(
          &context, src, src_end - src, &match_distance, max_match_size);
      if (!match_size) {
        // Literal byte
        flags |= 1;

        if (dst + 1 > dst_end) {
          nsmbw_compress_print_error("Output file is too much larger than the "
                                     "input file; aborting compression");
          free(work_buffer);
          return false;
        }

        nsmbw_compress_lz_slide(&context, src, 1);
        *dst++ = *src++;

        continue;
      }

      if (dst + 3 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        free(work_buffer);
        return false;
      }

      int ahead = 0;
      if (match_size != max_match_size) {
        // Check if the next two bytes after the match would allow for a longer
        // match
        ahead++;
        nsmbw_compress_lz_slide(&context, src++, 1);
        uint16_t next_match_distance, next_next_match_distance,
            next_next_match_size = 0;
        uint32_t next_match_size = nsmbw_compress_lz_search_window(
            &context, src, src_end - src, &next_match_distance, max_match_size);
        if (next_match_size > match_size) {
          if (next_match_size != max_match_size) {
            ahead++;
            nsmbw_compress_lz_slide(&context, src++, 1);
            next_next_match_size = nsmbw_compress_lz_search_window(
                &context, src, src_end - src, &next_next_match_distance,
                max_match_size);
          }
          // Write one or two literal bytes now
          for (int j = 0; j < 1 + (next_next_match_size > next_match_size);
               j++) {
            *dst++ = src[0 - ahead + j];
            flags |= 1;
            if (++i >= 8) {
              *flags_ptr = flags;
              flags = i = 0;
              flags_ptr = dst++;
            }
            flags <<= 1;
          }
          if (next_next_match_size > next_match_size) {
            match_size = next_next_match_size;
            match_distance = next_next_match_distance;
            ahead -= 2;
          } else {
            match_size = next_match_size;
            match_distance = next_match_distance;
            ahead--;
          }
        }
      }

      // Encoded reference

      uint32_t match_size_byte = match_size - 2;

      if (match_size >= 15 + 3) {
        match_size_byte = 0;
      }

      if (dst + 3 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        free(work_buffer);
        return false;
      }

      *dst++ = (match_size_byte << 4) | (match_distance - 1) >> 8;
      *dst++ = match_distance - 1;

      if (match_size >= 15 + 3) {
        *dst++ = match_size - 15 - 3;
      }

      nsmbw_compress_lz_slide(&context, src, match_size - ahead);

      src += match_size - ahead;
    }

    *flags_ptr = flags;
  }

  free(work_buffer);

  *dst_length = dst - dst_start;
  return true;
}
