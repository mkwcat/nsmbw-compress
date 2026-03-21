#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum nsmbw_compress_type {
  nsmbw_compress_type_lz,
  nsmbw_compress_type_huff,
  nsmbw_compress_type_rl,
  nsmbw_compress_type_lh,
  nsmbw_compress_type_lrc,
  nsmbw_compress_type_diff,
  nsmbw_compress_type_szs,
  nsmbw_compress_type_ash,
  nsmbw_compress_type_asr,
};

enum nsmbw_compress_cx_type {
  nsmbw_compress_cx_type_lz = 0x10,
  nsmbw_compress_cx_type_huff = 0x20,
  nsmbw_compress_cx_type_rl = 0x30,
  nsmbw_compress_cx_type_lh = 0x40,
  nsmbw_compress_cx_type_lrc = 0x50,
  nsmbw_compress_cx_type_diff = 0x80,

  nsmbw_compress_cx_type_mask = 0xF0,
};

enum nsmbw_compress_lz_mode {
  nsmbw_compress_lz_mode_0 = 0,
  nsmbw_compress_lz_mode_1 = 1,
  nsmbw_compress_lz_mode_auto = 2,
};

enum nsmbw_compress_asr_mode {
  nsmbw_compress_asr_mode_0 = 0,
  nsmbw_compress_asr_mode_1 = 1,
  nsmbw_compress_asr_mode_auto = 2,
};

struct nsmbw_compress_parameters {
  enum nsmbw_compress_lz_mode lz_mode;
  uint8_t huff_bit_size;
  uint8_t filter_diff_size;
  enum nsmbw_compress_asr_mode asr_mode;
};

typedef bool (*nsmbw_compress_function)(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_lz_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_lz_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_huff_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                           size_t *dst_length,
                           const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_huff_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                           size_t *dst_length,
                           const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_rl_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_rl_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_lh_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_lh_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                         size_t *dst_length,
                         const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_lrc_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_diff_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                           size_t *dst_length,
                           const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_diff_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                           size_t *dst_length,
                           const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_szs_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_ash_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_asr_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_asr_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

#if defined(__cplusplus)
}
#endif
