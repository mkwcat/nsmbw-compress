#include "nsmbw_compress.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reference: JKRDecomp::decodeSZP from tp
void nsmbw_compress_szp_decode(const uint8_t *src, uint8_t *dst,
                               size_t srcLength, size_t dstLength) {
  int srcChunkOffset;
  int count;
  int dstOffset;
  uint32_t length = srcLength;
  int linkInfo;
  int offset;
  int i;

  int decodedSize = nsmbw_compress_util_read_be_u32(src, 4);
  int linkTableOffset = nsmbw_compress_util_read_be_u32(src, 8);
  int srcDataOffset = nsmbw_compress_util_read_be_u32(src, 12);

  dstOffset = 0;
  uint32_t counter = 0;
  srcChunkOffset = 16;

  uint32_t chunkBits;
  if (srcLength == 0)
    return;
  if (dstLength > decodedSize)
    return;

  do {
    if (counter == 0) {
      chunkBits = nsmbw_compress_util_read_be_u32(src, srcChunkOffset);
      srcChunkOffset += sizeof(uint32_t);
      counter = sizeof(uint32_t) * 8;
    }

    if (chunkBits & 0x80000000) {
      if (dstLength == 0) {
        dst[dstOffset] = src[srcDataOffset];
        length--;
        if (length == 0)
          return;
      } else {
        dstLength--;
      }
      dstOffset++;
      srcDataOffset++;
    } else {
      linkInfo = src[linkTableOffset] << 8 | src[linkTableOffset + 1];
      linkTableOffset += sizeof(uint16_t);

      offset = dstOffset - (linkInfo & 0xFFF);
      count = (linkInfo >> 12);
      if (count == 0) {
        count = (uint32_t)src[srcDataOffset++] + 0x12;
      } else
        count += 2;

      if (count > decodedSize - dstOffset)
        count = decodedSize - dstOffset;

      for (i = 0; i < count; i++, dstOffset++, offset++) {
        if (dstLength == 0) {
          dst[dstOffset] = dst[offset - 1];
          length--;
          if (length == 0)
            return;
        } else
          dstLength--;
      }
    }

    chunkBits <<= 1;
    counter--;
  } while (dstOffset < decodedSize);
}
