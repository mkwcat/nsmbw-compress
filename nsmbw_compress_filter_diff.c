#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_filter_diff_decode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  (void)params;

  CXSecureResult result = CXSecureUnfilterDiff(src, src_length, dst);
  if (result != CX_SECURE_ERR_OK) {
    nsmbw_compress_print_cx_error(true, result);
    return false;
  }
  return true;
}

bool nsmbw_compress_filter_diff_encode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  (void)params;

  // Filter-diff doesn't support extended CX file sizes
  if (src_length >= (1 << 24)) {
    nsmbw_compress_print_error(
        "Input size too large for filter-diff compression (max: 2^24-1 bytes)");
    return false;
  }

  uint32_t init_value = CX_COMPRESSION_TYPE_FILTER_DIFF |
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