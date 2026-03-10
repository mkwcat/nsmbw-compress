#ifndef RVL_SDK_CX_COMPRESSION_H
#define RVL_SDK_CX_COMPRESSION_H

/*******************************************************************************
 * headers
 */

#include "types.h"

/*******************************************************************************
 * functions
 */

#ifdef __cplusplus
extern "C" {
#endif

u32 CXCompressLZImpl(byte_t const *srcp, u32 size, byte_t *dstp, void *work,
                     BOOL);
u32 CXCompressRL(byte_t const *srcp, u32 size, byte_t *dstp);
u32 CXCompressHuffman(byte_t const *srcp, u32 size, byte_t *dstp,
                      u8 huffBitSize, void *work);

// Added
u32 CXCompressLH(byte_t const *srcp, u32 size, byte_t *dstp, byte_t *tmp_dstp,
                 void *work);

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
  _Alignas(4) byte_t huffLWork[CX_COMPRESS_HUFFMAN_WORK_SIZE(9)];
  _Alignas(4) byte_t huffRWork[CX_COMPRESS_HUFFMAN_WORK_SIZE(5)];

  _Alignas(4) byte_t
      lzWork[CX_COMPRESS_LZ_WORK_SIZE_DETAIL(CX_COMPRESS_LH_LZ_DETAIL_SIZE)];
};

#define CX_COMPRESS_LH_WORK_SIZE sizeof(struct CXiCompressLHWork)

#ifdef __cplusplus
}
#endif

#endif // RVL_SDK_CX_COMPRESSION_H
