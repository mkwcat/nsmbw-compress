#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nsmbw_compress_print_error(const char *message, ...);

void nsmbw_compress_print_warning(const char *message, ...);

void nsmbw_compress_print_verbose(const char *message, ...);

void nsmbw_compress_print_cx_error(bool decompression, int result);

#define ncutil_array_size(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline uint32_t ncutil_read_be_u32(const void *data, size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline uint32_t ncutil_read_le_u32(const void *data, size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[0];
}

static inline uint16_t ncutil_read_be_u16(const void *data, size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
}

static inline uint16_t ncutil_read_le_u16(const void *data, size_t offset) {
  const uint8_t *bytes = (const uint8_t *)data + offset;
  return ((uint16_t)bytes[1] << 8) | (uint16_t)bytes[0];
}

static inline size_t ncutil_write_be_u32(void *data, size_t offset,
                                         uint32_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value >> 24;
  bytes[1] = value >> 16;
  bytes[2] = value >> 8;
  bytes[3] = value;
  return sizeof(uint32_t);
}

static inline size_t ncutil_write_le_u32(void *data, size_t offset,
                                         uint32_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value;
  bytes[1] = value >> 8;
  bytes[2] = value >> 16;
  bytes[3] = value >> 24;
  return sizeof(uint32_t);
}

static inline size_t ncutil_write_be_u16(void *data, size_t offset,
                                         uint16_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value >> 8;
  bytes[1] = value;
  return sizeof(uint16_t);
}

static inline size_t ncutil_write_le_u16(void *data, size_t offset,
                                         uint16_t value) {
  uint8_t *bytes = (uint8_t *)data + offset;
  bytes[0] = value;
  bytes[1] = value >> 8;
  return sizeof(uint16_t);
}

static inline int ncutil_popcount_u32(uint32_t value) {
#if defined(__has_builtin) && __has_builtin(__builtin_popcount)
  return __builtin_popcount(value);
#else
  int count = 0;
  while (value) {
    count += value & 1;
    value >>= 1;
  }
  return count;
#endif
}

static inline int ncutil_clz_u32(uint32_t value) {
#if defined(__has_builtin) && __has_builtin(__builtin_clz)
  return value ? __builtin_clz(value) : 32;
#else
  int count = 0;
  uint32_t bit = 1u << 31;
  while (bit && !(value & bit)) {
    count++;
    bit >>= 1;
  }
  return count;
#endif
}

static inline size_t ncutil_align_up(int alignment, size_t value) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static inline size_t ncutil_align_down(int alignment, size_t value) {
  return value & ~(alignment - 1);
}

static inline const void *ncutil_align_up_ptr(int alignment,
                                              const void *value) {
  return (const void *)(((uintptr_t)value + alignment - 1) & ~(alignment - 1));
}

static inline const void *ncutil_align_down_ptr(int alignment,
                                                const void *value) {
  return (const void *)((uintptr_t)value & ~(alignment - 1));
}

struct ncutil_bit_writer {
  uint8_t *data;
  uint32_t offset;
  uint8_t bit;
};

static inline void ncutil_bit_writer_write(struct ncutil_bit_writer *writer,
                                           uint32_t value, int bit_size) {

  value &= (1u << bit_size) - 1u;
  if (writer->bit) {
    value |= writer->data[writer->offset] >> (8 - writer->bit) << bit_size;
  }
  writer->bit = writer->bit + bit_size;
  while (writer->bit >= 8) {
    writer->data[writer->offset++] = value >> (writer->bit -= 8);
  }
  if (writer->bit) {
    writer->data[writer->offset] = value << (8 - writer->bit);
  }
}

static inline void ncutil_bit_writer_flush(struct ncutil_bit_writer *writer) {
  if (writer->bit) {
    writer->offset++;
    writer->bit = 0;
  }
}

static inline void ncutil_bit_writer_pad(struct ncutil_bit_writer *writer,
                                         int byte_alignment) {
  ncutil_bit_writer_flush(writer);
  while (writer->offset % byte_alignment != 0) {
    writer->data[writer->offset++] = 0;
  }
}

struct ncutil_bit_reader {
  const uint8_t *data;
  const uint8_t *data_end;
  uint8_t bit;
};

static inline int ncutil_bit_reader_read_bit(struct ncutil_bit_reader *reader) {
  if (reader->bit == 8) {
    if (++reader->data >= reader->data_end) {
      return -1;
    }
    reader->bit = 0;
  }
  return (*reader->data >> (7 - reader->bit++)) & 1;
}

#ifdef __cplusplus
} // extern "C"
#endif