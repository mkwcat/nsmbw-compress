#ifndef RVL_SDK_CX_STREAMING_UNCOMPRESSION_H
#define RVL_SDK_CX_STREAMING_UNCOMPRESSION_H

/*******************************************************************************
 * headers
 */

#include <stdint.h>

/*******************************************************************************
 * types
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Technically CXStreamingResult is the same as CXSecureResult, but it is
 * deduplicated here to avoid pulling in CXSecureCompression.h
 */
typedef int32_t CXStreamingResult;
enum CXStreamingResult_et {
  CX_STREAMING_ERR_OK = 0,
  CX_STREAMING_ERR_BAD_FILE_TYPE = -1,
  CX_STREAMING_ERR_BUFFER_TOO_SMALL = -2,
  CX_STREAMING_ERR_BUFFER_TOO_LARGE = -3,
  CX_STREAMING_ERR_BAD_FILE_SIZE = -4,
  CX_STREAMING_ERR_BAD_FILE_TABLE = -5,

#define CXSTREAM_ESUCCESS CX_STREAMING_ERR_OK
#define CXSTREAM_EBADTYPE CX_STREAMING_ERR_BAD_FILE_TYPE
#define CXSTREAM_E2SMALL CX_STREAMING_ERR_BUFFER_TOO_SMALL
#define CXSTREAM_E2BIG CX_STREAMING_ERR_BUFFER_TOO_LARGE
#define CXSTREAM_EBADSIZE CX_STREAMING_ERR_BAD_FILE_SIZE
#define CXSTREAM_EBADTABLE CX_STREAMING_ERR_BAD_FILE_TABLE
};

typedef struct CXUncompContextRL {
  uint8_t *at_0x00;  // size 0x04, offset 0x00
  int at_0x04;       // size 0x04, offset 0x04
  int at_0x08;       // size 0x04, offset 0x08
  uint16_t at_0x0c;  // size 0x02, offset 0x0c
  uint8_t at_0x0e;   // size 0x01, offset 0x0e
  uint8_t at_0x0f;   // size 0x01, offset 0x0f
} CXUncompContextRL; // size 0x10?

typedef struct CXUncompContextLZ {
  uint8_t *at_0x00; // size 0x04, offset 0x00
  int at_0x04;      // size 0x04, offset 0x04
  int at_0x08;      // size 0x04, offset 0x08
  int at_0x0c;      // size 0x04, offset 0x0c
  uint8_t at_0x10;  // size 0x01, offset 0x10
  uint8_t at_0x11;  // size 0x01, offset 0x11
  uint8_t at_0x12;  // size 0x01, offset 0x12
  uint8_t at_0x13;  // size 0x01, offset 0x13
  uint8_t at_0x14;  // size 0x01, offset 0x14
  char pad0_[3];
} CXUncompContextLZ; // size 0x18?

typedef struct CXUncompContextHuffman {
  int *at_0x00;         // size 0x04, offset 0x00
  int at_0x04;          // size 0x04, offset 0x04
  int at_0x08;          // size 0x04, offset 0x08
  uint8_t *at_0x0c;     // size 0x04, offset 0x0c
  int unsigned at_0x10; // size 0x04, offset 0x10
  int unsigned at_0x14; // size 0x04, offset 0x14
  int16_t at_0x18;      // size 0x01, offset 0x18
  uint8_t at_0x1a;      // size 0x01, offset 0x1a
  uint8_t at_0x1b;      // size 0x01, offset 0x1b
  uint8_t at_0x1c;      // size 0x01, offset 0x1c
  uint8_t at_0x1d;      // size 0x01, offset 0x1d
  char pad0_[2];
  uint8_t at_0x20[2];     // size 0x??, offset 0x20
} CXUncompContextHuffman; // size 0x??

typedef struct CXUncompContextLH {
  uint8_t *at_0x000;    // size 0x004, offset 0x000
  int at_0x004;         // size 0x004, offset 0x004
  int at_0x008;         // size 0x004, offset 0x008
  uint16_t at_0x00c[2]; // size 0x???, 0ffset 0x00c
  char pad0_[0x80c - (0x00c + 0x004)];
  uint16_t at_0x80c[1]; // size 0x???, 0ffset 0x80c
  char pad1_[0x88c - (0x80c + 0x002)];
  void *at_0x88c;    // size 0x004, offset 0x88c
  int at_0x890;      // size 0x004, offset 0x890
  int at_0x894;      // size 0x004, offset 0x894
  int at_0x898;      // size 0x004, offset 0x898
  int at_0x89c;      // size 0x004, offset 0x89c
  int at_0x8a0;      // size 0x004, offset 0x8a0
  uint16_t at_0x8a4; // size 0x002, offset 0x8a4
  int8_t at_0x8a6;   // size 0x001, offset 0x8a6
  uint8_t at_0x8a7;  // size 0x001, offset 0x8a7
} CXUncompContextLH; // size 0x8a8?

typedef struct CXUncompContextLRC {
  uint8_t *at_0x0000; // size 0x0004, offset 0x0000
  int at_0x0004;      // size 0x0004, offset 0x0004
  int at_0x0008;      // size 0x0004, offset 0x0008
  unsigned at_0x000c; // size 0x0004, offset 0x000c
  char pad0_[0x080c - (0x000c + 0x0004)];
  unsigned at_0x080c; // size 0x0004, offset 0x080c
  char pad1_[0x100c - (0x080c + 0x0004)];
  unsigned at_0x100c; // size 0x0004, offset 0x100c
  char pad2_[0x500c - (0x100c + 0x0004)];
  unsigned at_0x500c; // size 0x0004, offset 0x500c
  char pad3_[0x900c - (0x500c + 0x0004)];
  int at_0x900c;      // size 0x0004, offset 0x900c
  int at_0x9010;      // size 0x0004, offset 0x9010
  int at_0x9014;      // size 0x0004, offset 0x9014
  int at_0x9018;      // size 0x0004, offset 0x9018
  int at_0x901c;      // size 0x0004, offset 0x901c
  int at_0x9020;      // size 0x0004, offset 0x9020
  uint8_t at_0x9024;  // size 0x0001, offset 0x9024
  uint8_t at_0x9025;  // size 0x0001, offset 0x9025
  uint16_t at_0x9026; // size 0x0002, offset 0x9026
  uint8_t at_0x9028;  // size 0x0001, offset 0x9028
} CXUncompContextLRC; // size 0x902c?

/*******************************************************************************
 * functions
 */

void CXInitUncompContextRL(CXUncompContextRL *context, uint8_t *);
void CXInitUncompContextLZ(CXUncompContextLZ *context, uint8_t *);
void CXInitUncompContextHuffman(CXUncompContextHuffman *context, int *);
void CXInitUncompContextLH(CXUncompContextLH *context, uint8_t *);
void CXInitUncompContextLRC(CXUncompContextLRC *context, uint8_t *);

CXStreamingResult CXReadUncompRL(CXUncompContextRL *context,
                                 void const *compressed, uint32_t size);
CXStreamingResult CXReadUncompLZ(CXUncompContextLZ *context,
                                 void const *compressed, uint32_t size);
CXStreamingResult CXReadUncompHuffman(CXUncompContextHuffman *context,
                                      void const *compressed, uint32_t size);
CXStreamingResult CXReadUncompLH(CXUncompContextLH *context,
                                 void const *compressed, uint32_t size);
CXStreamingResult CXReadUncompLRC(CXUncompContextLRC *context,
                                  void const *compressed, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif // RVL_SDK_CX_STREAMING_UNCOMPRESSION_H
