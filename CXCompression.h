#ifndef RVL_SDK_CX_COMPRESSION_H
#define RVL_SDK_CX_COMPRESSION_H

/*******************************************************************************
 * headers
 */

#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
 * functions
 */

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CXCompressLZImpl(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                          void *work, bool);
uint32_t CXCompressRL(uint8_t const *srcp, uint32_t size, uint8_t *dstp);
uint32_t CXCompressHuffman(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                           uint8_t huffBitSize, void *work);

// Added
uint32_t CXCompressLH(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                      uint8_t *tmp_dstp, void *work);

#define CX_COMPRESS_DST_SCALE 4

#define CX_COMPRESS_LZ_WORK_SIZE_DETAIL(LZ_MAX_REFERENCE_SIZE)                 \
  0x400 + (LZ_MAX_REFERENCE_SIZE * 2)
#define CX_COMPRESS_LZ_WORK_SIZE CX_COMPRESS_LZ_WORK_SIZE_DETAIL(0x1000)

#define CX_COMPRESS_HUFFMAN_WORK_SIZE_00(HUFF_BIT_MAX)                         \
  ((HUFF_BIT_MAX) * 2 * 0x18)
#define CX_COMPRESS_HUFFMAN_WORK_SIZE_04(HUFF_BIT_MAX)                         \
  ((HUFF_BIT_MAX) * 2 * 0x2)
#define CX_COMPRESS_HUFFMAN_WORK_SIZE_08(HUFF_BIT_MAX) ((HUFF_BIT_MAX) * 0x6)
#define CX_COMPRESS_HUFFMAN_WORK_SIZE(HUFF_BIT_SIZE)                           \
  (CX_COMPRESS_HUFFMAN_WORK_SIZE_00(1 << HUFF_BIT_SIZE) +                      \
   CX_COMPRESS_HUFFMAN_WORK_SIZE_04(1 << HUFF_BIT_SIZE) +                      \
   CX_COMPRESS_HUFFMAN_WORK_SIZE_08(1 << HUFF_BIT_SIZE))

#define CX_COMPRESS_LH_LZ_DETAIL_SIZE 0x8000

struct CXiCompressLHWork {
  _Alignas(4) uint8_t huffLWork[CX_COMPRESS_HUFFMAN_WORK_SIZE(9)];
  _Alignas(4) uint8_t huffRWork[CX_COMPRESS_HUFFMAN_WORK_SIZE(5)];

  _Alignas(4) uint8_t
      lzWork[CX_COMPRESS_LZ_WORK_SIZE_DETAIL(CX_COMPRESS_LH_LZ_DETAIL_SIZE)];
  short lz_reverse_skip_table[CX_COMPRESS_LH_LZ_DETAIL_SIZE];
};

#define CX_COMPRESS_LH_WORK_SIZE sizeof(struct CXiCompressLHWork)

#ifdef __cplusplus
}
#endif

#endif // RVL_SDK_CX_COMPRESSION_H
