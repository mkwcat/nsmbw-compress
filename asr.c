#include "lz.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Reference: EGG::Decomp::decodeASR from ogws
// https://decomp.me/scratch/4WId8
bool nsmbw_compress_asr_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  uint32_t value, run_length, src_value_sym, src_value_dst;
  uint32_t divide_value;

  uint32_t expand_size = ncutil_read_be_u32(src, 4) & 0x00FFFFFFu;
  assert(expand_size <= *dst_length);
  if (expand_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in ASR file");
    return false;
  }

  uint32_t src_index_dst = ncutil_read_be_u32(src, sizeof(uint32_t) * 2);

  uint32_t dst_table_size = src[4] & 0x80 ? 0x1000 : 0x200;

  uint32_t src_index_sym = sizeof(uint32_t) * 3;
  uint32_t dst_l = 0, sym_r = -1u, sym_l = 0, dst_r = -1u;

  uint32_t sym_table_l[0x200];
  uint32_t sym_table_r[0x200 + 1];
  uint32_t dst_table_l[0x1000];
  uint32_t dst_table_r[0x1000 + 1];

  sym_table_r[0] = dst_table_r[0] = 0;
  for (uint32_t i = 0; i < 0x200; i++) {
    sym_table_l[i] = 1;
    sym_table_r[i + 1] = sym_table_r[i] + 1;
  }
  for (uint32_t i = 0; i < dst_table_size; i++) {
    dst_table_l[i] = 1;
    dst_table_r[i + 1] = dst_table_r[i] + 1;
  }

  src_value_sym = ncutil_read_be_u32(src, src_index_sym);
  src_index_sym += sizeof(uint32_t);
  src_value_dst = ncutil_read_be_u32(src, src_index_dst);
  src_index_dst += sizeof(uint32_t);

  uint32_t dst_index = 0;
  while (dst_index < expand_size) {
    divide_value = (sym_table_r[0x0200] ? (sym_r / sym_table_r[0x0200]) : 0);
    uint32_t point =
        (divide_value ? ((src_value_sym - sym_l) / divide_value) : 0);
    for (uint32_t i = 0, e = 0x200; i < e;) {
      value = (i + e) >> 1;
      if (point < sym_table_r[value]) {
        e = value;
      } else {
        i = value + 1;
      }
    }

    while (value >= 0) {
      if (sym_table_r[value] <= point && point < sym_table_r[value + 1]) {
        break;
      }
      value--;
    }

    run_length = value;

    sym_l += divide_value * sym_table_r[value];
    sym_r = divide_value * sym_table_l[value]++;

    while (++value <= 0x200) {
      sym_table_r[value]++;
    }

    if (sym_table_r[0x200] >= 0x10000) {
      sym_table_r[0] = 0;
      for (uint32_t i = 0; i < 0x200; ++i) {
        sym_table_l[i] = (sym_table_l[i] >> 1) | 1;
        sym_table_r[i + 1] = sym_table_r[i] + sym_table_l[i];
      }
    }

    while ((sym_l & 0xFF000000u) == ((sym_l + sym_r) & 0xFF000000u)) {
      if (src_index_sym >= src_length) {
        nsmbw_compress_print_error(
            "Unexpected end of input in ASR compressed file");
        return false;
      }
      src_value_sym = (src_value_sym << 8) + src[src_index_sym++];
      sym_l <<= 8, sym_r <<= 8;
    }

    while (sym_r < 0x10000) {
      if (src_index_sym >= src_length) {
        nsmbw_compress_print_error(
            "Unexpected end of input in ASR compressed file");
        return false;
      }
      sym_r = sym_l & 0xFFFF;
      sym_l <<= 8;
      sym_r = (0x10000 - sym_r);
      sym_r <<= 8;
      src_value_sym = (src_value_sym << 8) + src[src_index_sym++];
    }

    if (run_length < 0x100) {
      dst[dst_index++] = run_length;
      continue;
    }

    divide_value =
        (dst_table_r[dst_table_size] ? (dst_r / dst_table_r[dst_table_size])
                                     : 0);
    point = (divide_value ? ((src_value_dst - dst_l) / divide_value) : 0);

    for (int i = 0, e = dst_table_size; i < e;) {
      value = (i + e) >> 1;
      if (point < dst_table_r[value]) {
        e = value;
      } else {
        i = value + 1;
      }
    }
    while (value >= 0) {
      if (dst_table_r[value] <= point && point < dst_table_r[value + 1]) {
        break;
      }
      value--;
    }

    int runIdx = dst_index - value - 1;
    if (value + 1 > dst_index) {
      nsmbw_compress_print_error("Out of bounds reference in ASR compressed "
                                 "file (offset %u at output position %zu)",
                                 value + 1, dst_index);
      return false;
    }
    run_length -= 0xFD;
    if (dst_index + run_length > expand_size) {
      nsmbw_compress_print_error(
          "Reference overflows output size in ASR compressed file (offset %u, "
          "size %u at output position %zu with output size %zu)",
          value + 1, run_length - 0xFD, dst_index, expand_size);
      return false;
    }
    for (; run_length-- > 0; dst_index++) {
      dst[dst_index] = dst[runIdx++];
    }

    if (dst_index >= expand_size) {
      break;
    }

    dst_l += divide_value * dst_table_r[value];
    dst_r = divide_value * dst_table_l[value]++;
    while (++value <= dst_table_size) {
      dst_table_r[value]++;
    }
    if (dst_table_r[dst_table_size] >= 0x10000) {
      dst_table_r[0] = 0;
      for (int i = 0; i < dst_table_size; i++) {
        dst_table_l[i] = (dst_table_l[i] >> 1) | 1;
        dst_table_r[i + 1] = dst_table_r[i] + dst_table_l[i];
      }
    }
    while ((dst_l & 0xFF000000u) == ((dst_l + dst_r) & 0xFF000000u)) {
      if (src_index_dst >= src_length) {
        nsmbw_compress_print_error(
            "Unexpected end of input in ASR compressed file");
        return false;
      }
      src_value_dst = (src_value_dst << 8) + src[src_index_dst++];
      dst_l <<= 8, dst_r <<= 8;
    }

    while (dst_r < 0x10000) {
      if (src_index_dst >= src_length) {
        nsmbw_compress_print_error(
            "Unexpected end of input in ASR compressed file");
        return false;
      }
      dst_r = dst_l & 0xFFFF;
      dst_l <<= 8;
      dst_r = (0x10000 - dst_r) << 8;
      src_value_dst = (src_value_dst << 8) + src[src_index_dst++];
    }
  }

  *dst_length = expand_size;
  return true;
}

static bool asr_encode_range(uint16_t *src, uint32_t src_length,
                             uint32_t table_size, uint8_t *dst,
                             size_t *dst_length) {
  uint32_t table_l[0x1000];
  uint32_t table_r[0x1000 + 1];

  table_r[0] = 0;
  for (uint32_t i = 0; i < table_size; i++) {
    table_l[i] = 1;
    table_r[i + 1] = i + 1;
  }

  uint32_t l = 0, r = -1u;
  uint32_t dst_index = 0;
  for (uint32_t src_index = 0; src_index < src_length; src_index++) {
    uint32_t src_value = src[src_index];
    r /= table_r[table_size];
    l += table_r[src_value] * r;
    r *= table_l[src_value]++;

    while (++src_value <= table_size) {
      table_r[src_value]++;
    }

    if (table_r[table_size] >= 0x10000) {
      table_r[0] = 0;
      for (uint32_t i = 0; i < table_size; i++) {
        table_l[i] = table_l[i] >> 1 | 1;
        table_r[i + 1] = table_r[i] + table_l[i];
      }
    }

    while ((l & 0xFF000000u) == ((l + r) & 0xFF000000u)) {
      if (dst_index >= *dst_length) {
        return false;
      }
      dst[dst_index++] = l >> 24;
      l <<= 8, r <<= 8;
    }
    while (r < 0x10000) {
      if (dst_index >= *dst_length) {
        return false;
      }
      r = (0x10000 - (l & 0xFFFF)) << 8;
      dst[dst_index++] = l >> 24;
      l <<= 8;
    }
  }

  while (r != 0 || l != 0) {
    if (dst_index >= *dst_length) {
      return false;
    }
    dst[dst_index++] = l >> 24;
    l <<= 8, r <<= 8;
  }

  *dst_length = dst_index;
  return true;
}

static bool asr_encode(const uint8_t *src, uint32_t src_length, uint8_t *dst,
                       size_t *dst_length,
                       enum nsmbw_compress_asr_mode asr_mode) {
  const size_t max_dst_size = *dst_length;

  if (max_dst_size < 0x10) {
    nsmbw_compress_print_error("Output buffer is too small for ASR0 header");
    return false;
  }
  if (src_length > 0xFFFFFF) {
    nsmbw_compress_print_error("Input data is too large for ASR0 format");
    return false;
  }

  // Encode LZ data with src
  uint16_t *sym_buffer, *off_buffer;
  size_t sym_count, off_count;
  if (!nsmbw_compress_lz_encode_portable(
          src, src_length, UCHAR_MAX + 2, asr_mode ? 0x1000 : 0x200, true,
          0x100, &sym_buffer, &sym_count, &off_buffer, &off_count)) {
    return false;
  }

  dst[0] = 'A';
  dst[1] = 'S';
  dst[2] = 'R';
  dst[3] = '0';
  dst[4] = asr_mode ? 0x80 : 0x00;
  dst[5] = (src_length >> 16) & 0xFF;
  dst[6] = (src_length >> 8) & 0xFF;
  dst[7] = src_length & 0xFF;

  size_t dst_index = 0xC;
  size_t range_sym_size = max_dst_size - dst_index;
  if (!asr_encode_range(sym_buffer, sym_count, 0x200, dst + dst_index,
                        &range_sym_size)) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(sym_buffer);
    free(off_buffer);
    return false;
  }
  free(sym_buffer);

  dst_index += range_sym_size;
  size_t range_off_size = max_dst_size - dst_index;
  if (!asr_encode_range(off_buffer, off_count, asr_mode ? 0x1000 : 0x200,
                        dst + dst_index, &range_off_size)) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(off_buffer);
    return false;
  }
  free(off_buffer);

  dst_index += range_off_size;
  assert(dst_index <= max_dst_size);

  // Write offset to offset range to header
  uint32_t range_off_offset = dst_index - range_off_size;
  ncutil_write_be_u32(dst, 8, range_off_offset);

  *dst_length = dst_index;
  return true;
}

bool nsmbw_compress_asr_encode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  enum nsmbw_compress_asr_mode asr_mode = params->asr_mode;
  if (asr_mode != nsmbw_compress_asr_mode_auto) {
    bool result = asr_encode(src, src_length, dst, dst_length, asr_mode);
    return result;
  }

  // Auto mode: Run both and pick the smaller one
  size_t dst_length_1 = *dst_length;
  if (!asr_encode(src, src_length, dst, &dst_length_1,
                  nsmbw_compress_asr_mode_1)) {
    return false;
  }

  size_t dst_length_0 = dst_length_1;
  void *out_buffer_0 = malloc(dst_length_0);
  if (out_buffer_0 == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for ASR compression output buffer: %s",
        strerror(errno));
    return false;
  }

  bool result = asr_encode(src, src_length, out_buffer_0, &dst_length_0,
                           nsmbw_compress_asr_mode_0);

  if (!result || dst_length_1 <= dst_length_0) {
    free(out_buffer_0);
    nsmbw_compress_print_verbose(
        "Selected ASR mode 1 compression (compressed size: %zu bytes)",
        dst_length_1);
    *dst_length = dst_length_1;
    return true;
  }

  nsmbw_compress_print_verbose(
      "Selected ASR mode 0 compression (compressed size: %zu bytes)",
      dst_length_0);
  *dst_length = dst_length_0;
  memcpy(dst, out_buffer_0, dst_length_0);
  free(out_buffer_0);
  return true;
}
