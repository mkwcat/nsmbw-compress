#ifndef RVL_SDK_CX_UNCOMPRESSION_H
#define RVL_SDK_CX_UNCOMPRESSION_H

/*******************************************************************************
 * headers
 */

#include <stdint.h>

/*******************************************************************************
 * functions
 */

#ifdef __cplusplus
extern "C" {
#endif

void CXUncompressAny(void const *compressed, void *uncompressed);

void CXUncompressRL(void const *compressed, void *uncompressed);
void CXUncompressLZ(void const *compressed, void *uncompressed);
void CXUncompressHuffman(void const *compressed, void *uncompressed);
void CXUncompressLH(void const *compressed, uint8_t *uncompressed, uint16_t *);
void CXUncompressLRC(void const *compressed, uint8_t *uncompressed, unsigned *);
void CXUnfilterDiff(void const *compressed, void *uncompressed);

#ifdef __cplusplus
}
#endif

#endif // RVL_SDK_CX_UNCOMPRESSION_H
