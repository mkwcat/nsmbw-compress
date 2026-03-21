#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
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

  uint32_t expand_size = src[5] << 16 | src[6] << 8 | src[7];
  assert(expand_size <= *dst_length);
  if (expand_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in ASR file");
    return false;
  }

  uint32_t src_index_dst = src[8] << 24 | src[9] << 16 | src[10] << 8 | src[11];

  uint32_t dst_table_size = src[4] & 0x80 ? 0x1000 : 0x200;

  uint32_t src_index_sym = 0xC;
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

  src_value_sym = src[src_index_sym] << 24 | src[src_index_sym + 1] << 16 |
                  src[src_index_sym + 2] << 8 | src[src_index_sym + 3];
  src_index_sym += 0x4;
  src_value_dst = src[src_index_dst + 0] << 24 | src[src_index_dst + 1] << 16 |
                  src[src_index_dst + 2] << 8 | src[src_index_dst + 3];
  src_index_dst += 0x4;

  uint32_t dst_index = 0;
  while (dst_index < expand_size) {
    divide_value = (sym_table_r[0x0200] ? (sym_r / sym_table_r[0x0200]) : 0);
    uint32_t point =
        (divide_value ? ((src_value_sym - sym_l) / divide_value) : 0);
    for (uint32_t i = 0, e = 0x200; i < e;) {
      value = (i + e) >> 1;
      if (point < sym_table_r[value]) {
        e = value;
      }
      if (point >= sym_table_r[value]) {
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
            "Input ASR file ended prematurely while decoding compressed data");
        return false;
      }
      src_value_sym = (src_value_sym << 8) + src[src_index_sym++];
      sym_l <<= 8, sym_r <<= 8;
    }

    while (sym_r < 0x10000) {
      if (src_index_sym >= src_length) {
        nsmbw_compress_print_error(
            "Input ASR file ended prematurely while decoding compressed data");
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
      }
      if (point >= dst_table_r[value]) {
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
            "Input ASR file ended prematurely while decoding compressed data");
        return false;
      }
      src_value_dst = (src_value_dst << 8) + src[src_index_dst++];
      dst_l <<= 8, dst_r <<= 8;
    }

    while (dst_r < 0x10000) {
      if (src_index_dst >= src_length) {
        nsmbw_compress_print_error(
            "Input ASR file ended prematurely while decoding compressed data");
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
