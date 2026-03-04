#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst,
                                      size_t src_length, size_t dst_length);

extern void nsmbw_compress_szp_decode(const uint8_t *src, uint8_t *dst,
                                      size_t src_length, size_t dst_length);

inline uint32_t nsmbw_compress_util_read_be_u32(const void *data,
                                                size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

inline uint32_t nsmbw_compress_util_read_le_u32(const void *data,
                                                size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[0];
}
