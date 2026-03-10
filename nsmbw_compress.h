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
  nsmbw_compress_type_filter_diff,
  nsmbw_compress_type_szs,
};

struct nsmbw_compress_parameters {
  uint8_t huff_bit_size;
  uint8_t filter_diff_size;
  bool lz77_extended;
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

extern bool nsmbw_compress_filter_diff_encode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params);

extern bool nsmbw_compress_filter_diff_decode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_szs_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

extern bool
nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params);

#if defined(__cplusplus)
}
#endif