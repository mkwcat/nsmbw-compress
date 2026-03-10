#pragma once

#include <stddef.h>
#include <stdint.h>

static inline uint32_t nsmbw_compress_util_read_be_u32(const void *data,
                                                       size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline uint32_t nsmbw_compress_util_read_le_u32(const void *data,
                                                       size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[0];
}

void nsmbw_compress_print_error(const char *message, ...);

void nsmbw_compress_print_warning(const char *message, ...);

void nsmbw_compress_print_verbose(const char *message, ...);

void nsmbw_compress_print_cx_error(bool decompression, int result);