#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_filter_diff_decode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  (void)params;

  if (src_length < sizeof(uint32_t)) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed filter-diff file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const enum nsmbw_compress_cx_type type = header & nsmbw_compress_cx_type_mask;
  if (type != nsmbw_compress_cx_type_filter_diff) {
    nsmbw_compress_print_error("Input data is not a CX-Filter-Diff file");
    return false;
  }
  const uint8_t option = header & 0x0F;
  const uint32_t size = header >> 8;

  assert(*dst_length >= size);
  if (option != 0 && option != 1) {
    nsmbw_compress_print_error(
        "Input data has unrecognized filter-diff compression option: %d",
        option);
    return false;
  }

  if (size == 0) {
    return true;
  }
  const size_t expected_length =
      ncutil_align_up(option ? 2 : 1, size + sizeof(uint32_t));
  if (src_length < expected_length) {
    nsmbw_compress_print_error(
        "Input data is too small to be a valid compressed filter-diff file");
    return false;
  }

  src += sizeof(uint32_t);

  if (option == 0) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
      sum += src[i];
      dst[i] = sum;
    }
  } else {
    uint16_t sum = 0;
    for (uint32_t i = 0; i < size - 1; i += sizeof(uint16_t)) {
      sum += ncutil_read_le_u16(src, i);
      ncutil_write_le_u16(dst, i, sum);
    }
    if (size % 2 != 0) {
      sum += ncutil_read_le_u16(src, size - 1);
      dst[size - 1] = (uint8_t)sum;
    }
  }

  if (src_length > ncutil_align_up(0x20, expected_length)) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        src_length - expected_length);
  }

  *dst_length = size;
  return true;
}

bool nsmbw_compress_filter_diff_encode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  // Filter-diff doesn't support extended CX file sizes for some reason
  if (src_length >= (1 << 24)) {
    nsmbw_compress_print_error(
        "Input size too large for filter-diff compression (max: 2^24-1 bytes)");
    return false;
  }

  uint32_t init_value = nsmbw_compress_cx_type_filter_diff |
                        (params->filter_diff_size == 8 ? 0x0 : 0x1) |
                        (src_length << 8);
  ncutil_write_le_u32(dst, 0, init_value);

  size_t dst_offset = sizeof(uint32_t);
  uint32_t sum = 0;
  size_t i;
  if (params->filter_diff_size == 8) {
    for (i = 0; i < src_length; i++) {
      uint8_t cur = src[i];
      dst[i + dst_offset] = (uint8_t)(cur - sum);
      sum = cur;
    }
  } else {
    for (i = 0; i < src_length; i += sizeof(uint16_t)) {
      uint16_t cur;
      if (i + sizeof(uint16_t) <= src_length) {
        cur = ncutil_read_le_u16(src, i);
      } else {
        // Handle odd final byte by treating it as a little-endian 16-bit value
        // with the high byte set to 0
        cur = src[i];
      }
      ncutil_write_le_u16(dst, i + dst_offset, cur - sum);
      sum = cur;
    }
  }

  *dst_length = dst_offset + i;
  return true;
}
