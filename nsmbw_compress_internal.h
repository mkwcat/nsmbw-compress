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

static inline uint16_t nsmbw_compress_util_read_be_u16(const void *data,
                                                       size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
}

static inline uint16_t nsmbw_compress_util_read_le_u16(const void *data,
                                                       size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint16_t)bytes[1] << 8) | (uint16_t)bytes[0];
}

static inline size_t nsmbw_compress_util_write_be_u32(void *data, size_t offset,
                                                      uint32_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value >> 24;
  bytes[1] = value >> 16;
  bytes[2] = value >> 8;
  bytes[3] = value;
  return sizeof(uint32_t);
}

static inline size_t nsmbw_compress_util_write_le_u32(void *data, size_t offset,
                                                      uint32_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value;
  bytes[1] = value >> 8;
  bytes[2] = value >> 16;
  bytes[3] = value >> 24;
  return sizeof(uint32_t);
}

static inline size_t nsmbw_compress_util_write_be_u16(void *data, size_t offset,
                                                      uint16_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value >> 8;
  bytes[1] = value;
  return sizeof(uint16_t);
}

static inline size_t nsmbw_compress_util_write_le_u16(void *data, size_t offset,
                                                      uint16_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value;
  bytes[1] = value >> 8;
  return sizeof(uint16_t);
}

void nsmbw_compress_print_error(const char *message, ...);

void nsmbw_compress_print_warning(const char *message, ...);

void nsmbw_compress_print_verbose(const char *message, ...);

void nsmbw_compress_print_cx_error(bool decompression, int result);
