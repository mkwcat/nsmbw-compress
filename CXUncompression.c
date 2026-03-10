#include "CXUncompression.h"

/*******************************************************************************
 * headers
 */

#include "CXInternal.h"
#include "cx.h"
#include "macros.h"
#include <assert.h>
#include <stdint.h>

/*******************************************************************************
 * types
 */

struct BitReader {
  uint8_t const *data;  // size 0x04, offset 0x00
  int byteOffset;       // size 0x04, offset 0x04
  int unsigned at_0x08; // size 0x04, offset 0x08
  int bit;              // size 0x04, offset 0x0c
}; // size 0x10?

struct RCInfo {
  int unsigned *at_0x00; // size 0x04, offset 0x00
  int unsigned *at_0x04; // size 0x04, offset 0x04
  int unsigned at_0x08;  // size 0x04, offset 0x08
  uint8_t at_0x0c;       // size 0x04, offset 0x0c
}; // size 0x10?

struct RCState {
  int at_0x00;          // size 0x04, offset 0x00
  int unsigned at_0x04; // size 0x04, offset 0x04
  int at_0x08;          // size 0x04, offset 0x08
  char at_0x0c;         // size 0x04, offset 0x0c
  uint8_t pad0_[3];     // alignment?
  int at_0x10;          // size 0x04, offset 0x10
}; // size 0x14?

/*******************************************************************************
 * local function declarations
 */

static int CXGetCompressionType(void const *compressed);

static int CXiHuffImportTree(uint16_t *tree, uint8_t const *stream, uint8_t);

// only reasonable way i see an init function come after a read function
static inline void BitReader_Init(struct BitReader *bitReader,
                                  uint8_t const *stream);
static uint8_t BitReader_Read(struct BitReader *bitReader);

static inline void RCInitInfo_(struct RCInfo *rcInfo, uint8_t, int unsigned *);
static inline void RCInitState_(struct RCState *rcState);
static void RCAddCount_(struct RCInfo *rcInfo, uint16_t);
static uint16_t RCSearch_(struct RCInfo *rcInfo, int, unsigned, int);
static uint16_t RCGetData_(uint8_t const *stream, struct RCInfo *rcInfo,
                           struct RCState *rcState, int *);

/*******************************************************************************
 * functions
 */

uint32_t CXGetUncompressedSize(void const *compressed) {
  uint32_t size =
      CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, compressed, 0)) >> 8;

  if (!size)
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, compressed, 4));

  return size;
}

void CXUncompressAny(void const *compressed, void *uncompressed) {
  switch (CXGetCompressionType(compressed)) {
  case CX_COMPRESSION_TYPE_RUN_LENGTH:
    CXUncompressRL(compressed, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_LEMPEL_ZIV:
    CXUncompressLZ(compressed, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_HUFFMAN:
    CXUncompressHuffman(compressed, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_FILTER_DIFF:
    CXUnfilterDiff(compressed, uncompressed);
    break;

  default:
    assert(false && "Unknown compressed format");
  }
}

void CXUncompressRL(void const *compressed, void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint32_t size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;
  src += sizeof(uint32_t);

  if (!size) {
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0));
    src += sizeof(uint32_t);
  }

  while (size) {
    uint8_t byte = *src++;

    uint32_t runLength = byte & 0x7f;

    if (!(byte & 0x80)) {
      runLength += 1;

      if (runLength > size)
        runLength = size;

      size -= runLength;

      do
        *dst++ = *src++;
      while (--runLength);
    } else {
      runLength += 3;

      if (runLength > size)
        runLength = size;

      size -= runLength;

      uint8_t byte = *src++;

      do
        *dst++ = byte;
      while (--runLength);
    }
  }
}

void CXUncompressLZ(void const *compressed, void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint32_t size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;
  bool stat = BOOLIFY_TERNARY(IN_BUFFER_AT(uint8_t, src, 0) & 0x0f);
  src += sizeof(uint32_t);

  if (!size) {
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0));
    src += sizeof(uint32_t);
  }

  while (size) {
    uint32_t i;
    uint32_t flags = *src++;

    for (i = 0; i < 8; ++i) {
      if (!(flags & 0x80)) {
        *dst++ = *src++;
        size--;
      } else {
        int32_t length = *src >> 4;

        if (!stat) {
          length += 3;
        } else {
          if (length == 0x01) {
            length = (*src++ & 0x0f) << 12;
            length |= *src++ << 4;
            length |= *src >> 4;
            length += 0x111;
          } else if (length == 0x00) {
            length = (*src++ & 0x0f) << 4;
            length |= *src >> 4;
            length += 0x11;
          } else {
            length += 0x01;
          }
        }

        int32_t offset = (*src++ & 0x0f) << 8;
        offset = (offset | *src++) + 1;

        if (length > size)
          length = size;

        size -= length;

        // clang-format off
				do
				{
#if defined(__MWERKS__)
					// ERRATUM: Unsequenced modification and access to dst
					/* Here, the compiler has decided to evaluate the left hand
					 * side dst first, then the right hand side dst, then the
					 * side effect of the post-increment on the left hand side
					 * dst.
					 */
					*dst++ = dst[-offset];
#else
					// equivalent defined version
					*dst = dst[-offset];
					++dst;
#endif
				} while (--length > 0);
        // clang-format on
      }

      if (!size)
        break;

      flags <<= 1;
    }
  }
}

void CXUncompressHuffman(void const *compressed, void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  int32_t size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;

  uint8_t const *base = size ? src + 4 : src + 8;
  uint8_t const *basep1 = base + 1;

  int a = *src & 0x0f;
  unsigned b = 0;
  unsigned c = 0;
  int d = (a & 0x07) + 4;

  if (!size)
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 4));

  src = base + ((*base + 1) << 1);
  base = basep1;

  while (size > 0) {
    int i = 32;

    uint32_t f;
#if defined(__MWERKS__)
    // NOTE: assignment to lvalue cast is a CW extension
    f = CXiConvertEndian32_(*((uint32_t *)(src))++);
#else
    f = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0));
    src += sizeof(uint32_t);
#endif

    while (--i >= 0) {
      int g = f >> 31;
      int h = *base;
      h <<= g;

      // ok
      base = (uint8_t const *)((((size_t)(base) >> 1) << 1) +
                               (((*base & 0x3f) + 1) << 1) + g);

      if (h & 0x80) {
        b >>= a;
        b |= *base << (32 - a);
        base = basep1;

        if (++c == d) {
#if defined(__MWERKS__)
          // NOTE: assignment to lvalue cast is a CW extension
          *((uint32_t *)(dst))++ = CXiConvertEndian32_(b);
#else
          *(uint32_t *)dst = CXiConvertEndian32_(b);
          dst += sizeof(uint32_t);
#endif
          size -= sizeof(uint32_t);
          c = 0;
        }
      }

      if (size <= 0)
        break;

      f <<= 1;
    }
  }
}

static int CXiHuffImportTree(uint16_t *tree, uint8_t const *stream,
                             uint8_t bit_size) {
  int a;
  int b;
  unsigned c;
  unsigned d;
  int bit_size_mask;
  unsigned f;
  unsigned g;

  a = 1;
  c = 0;

  d = 0;
  bit_size_mask = (1 << bit_size) - 1;

  f = 0;
  g = (1 << bit_size) << 1;

  if (bit_size > 8) {
    b = CXiConvertEndian16_(IN_BUFFER_AT(uint16_t, stream, 0));

    stream += sizeof(uint16_t);
    f += sizeof(uint16_t);
  } else {
    b = IN_BUFFER_AT(uint8_t, stream, 0);

    stream += sizeof(uint8_t);
    f += sizeof(uint8_t);
  }

  b = (b + 1) << 2;

  while (f < b) {
    while (d < bit_size) {
      c <<= 8;
      c |= *stream++;
      ++f;
      d += 8;
    }

    if (a < g)
      tree[a++] = bit_size_mask & (c >> (d - bit_size));

    d -= bit_size;
  }

  (void)stream;

  return b;
}

static void BitReader_Init(struct BitReader *bitReader, uint8_t const *stream) {
  bitReader->data = stream;
  bitReader->byteOffset = 0;
  bitReader->at_0x08 = 0;
  bitReader->bit = 0;
}

static uint8_t BitReader_Read(struct BitReader *bitReader) {
  if (!bitReader->bit) {
    bitReader->at_0x08 = bitReader->data[bitReader->byteOffset++];
    bitReader->bit = 8;
  }

  uint8_t a = (bitReader->at_0x08 >> (bitReader->bit - 1)) & 0x01;
  --bitReader->bit;

  return a;
}

void CXUncompressLH(void const *compressed, uint8_t *uncompressed,
                    uint16_t *param_3) {
  uint32_t size;
  int a = 0;
  uint8_t const *src = compressed;
  uint16_t *ltree = param_3;
  uint16_t *rtree = param_3 + 0x400;

  // size
  size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;
  src += sizeof(uint32_t);

  if (!size) {
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0));
    src += sizeof(uint32_t);
  }

  src += CXiHuffImportTree(ltree, src, 9);
  src += CXiHuffImportTree(rtree, src, 5);

  struct BitReader bitReader;
  BitReader_Init(&bitReader, src);

  while (a < size) {
    uint16_t d;
    uint16_t *e = ltree + 1;

    while (true) {
      uint8_t f = BitReader_Read(&bitReader);
      int g = (((*e & 0x7f) + 1) << 1) + f;

      if (*e & (0x100 >> f)) {
        e = ROUND_DOWN_PTR(e, 4);
        d = e[g];
        break;
      } else {
        e = ROUND_DOWN_PTR(e, 4);
        e += g;
      }
    }

    if (d < 0x100) {
      uncompressed[a++] = d;
      continue;
    }

    uint16_t h;
    uint16_t j = (d & 0xff) + 3;
    uint16_t *rtree_val = rtree + 1;

    while (true) {
      uint8_t l = BitReader_Read(&bitReader);
      uint32_t m = (((*rtree_val & 0x07) + 1) << 1) + l;

      if (*rtree_val & (0x10 >> l)) {
        rtree_val = ROUND_DOWN_PTR(rtree_val, 4);
        d = rtree_val[m];
        break;
      } else {
        rtree_val = ROUND_DOWN_PTR(rtree_val, 4);
        rtree_val += m;
      }
    }

    h = d;
    d = 0;

    if (h) {
      d = 1;

      while (--h) {
        d <<= 1;
        d |= BitReader_Read(&bitReader);
      }
    }

#if !defined(NDEBUG) // TODO: What
    d = d + 1;
#else
    ++d;
#endif

    if (a + j > size)
      j = size - a;

    while (j--) {
      uncompressed[a] = uncompressed[a - d];
      ++a;
    }
  }
}

static void RCInitInfo_(struct RCInfo *rcInfo, uint8_t param_2,
                        int unsigned *param_3) {
  uint32_t i;
  int a = 1 << param_2;

  rcInfo->at_0x0c = param_2;
  rcInfo->at_0x00 = param_3;
  rcInfo->at_0x04 = param_3 + a;

  for (i = 0; i < a; ++i) {
    rcInfo->at_0x00[i] = 1;
    rcInfo->at_0x04[i] = i;
  }

  rcInfo->at_0x08 = a;
}

static void RCInitState_(struct RCState *rcState) {
  rcState->at_0x00 = 0;
  rcState->at_0x04 = 0x80000000;
  rcState->at_0x08 = 0;
  rcState->at_0x0c = 0;
  rcState->at_0x10 = 0;
}

static void RCAddCount_(struct RCInfo *rcInfo, uint16_t param_2) {
  uint32_t i;
  unsigned a = 1 << rcInfo->at_0x0c;

  ++rcInfo->at_0x00[param_2];
  ++rcInfo->at_0x08;

  for (i = param_2 + 1; i < a; ++i)
    ++rcInfo->at_0x04[i];

  if (rcInfo->at_0x08 < 0x10000)
    return;

  if (*rcInfo->at_0x00 > 1)
    *rcInfo->at_0x00 >>= 1;

  *rcInfo->at_0x04 = 0;
  rcInfo->at_0x08 = *rcInfo->at_0x00;

  for (i = 1; i < a; ++i) {
    if (rcInfo->at_0x00[i] > 1)
      rcInfo->at_0x00[i] >>= 1;

    rcInfo->at_0x04[i] = rcInfo->at_0x04[i - 1] + rcInfo->at_0x00[i - 1];
    rcInfo->at_0x08 += rcInfo->at_0x00[i];
  }
}

static uint16_t RCSearch_(struct RCInfo *rcInfo, int param_2, unsigned param_3,
                          int param_4) {
  int a = 1 << rcInfo->at_0x0c;
  int b = param_2 - param_4;
  unsigned c = param_3 / rcInfo->at_0x08;
  unsigned d = b / c;
  unsigned e = 0;
  int f = a - 1;

  int g;
  while (e < f) {
    g = (e + f) >> 1;

    if (rcInfo->at_0x04[g] > d)
      f = g;
    else
      e = g + 1;
  }

  g = e;
  while (rcInfo->at_0x04[g] > d)
    --g;

  return g;
}

static uint16_t RCGetData_(uint8_t const *stream, struct RCInfo *rcInfo,
                           struct RCState *rcState, int *param_4) {
  uint16_t a =
      RCSearch_(rcInfo, rcState->at_0x08, rcState->at_0x04, rcState->at_0x00);
  int b = 0;

  // arbitrary block
  {
    int c = rcState->at_0x04 / rcInfo->at_0x08;

    rcState->at_0x00 += c * rcInfo->at_0x04[a];
    rcState->at_0x04 = c * rcInfo->at_0x00[a];

    RCAddCount_(rcInfo, a);

    while (rcState->at_0x04 < 0x1000000) {
      rcState->at_0x08 <<= 8;
      rcState->at_0x08 += stream[b++];
      rcState->at_0x04 <<= 8;
      rcState->at_0x00 <<= 8;
    }
  }

  *param_4 = b;
  return a;
}

void CXUncompressLRC(void const *compressed, uint8_t *uncompressed,
                     unsigned *param_3) {
  uint8_t const *src = compressed;
  unsigned a = 0;
  uint32_t size = 0;

  struct RCInfo info1;
  RCInitInfo_(&info1, 9, param_3);

  struct RCInfo info2;
#if !defined(NDEBUG) // What
  RCInitInfo_(&info2, 12, param_3 + 0x400);
#else
  RCInitInfo_(&info2, 12, param_3 += 0x400);
#endif

  struct RCState state;
  RCInitState_(&state);

  // size
  size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;
  src += sizeof(uint32_t);

  if (!size) {
    size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0));
    src += sizeof(uint32_t);
  }

  // What
  state.at_0x08 = src[0] << 24 | src[1] << 16 | src[2] << 8 | src[3];
  src += sizeof(uint8_t) * 4;

  while (a < size) {
    int b;

    // why is recast necessary?
    uint16_t c = (uint16_t)RCGetData_(src, &info1, &state, &b);
    src += b;

    if (c < 0x100) {
      uncompressed[a++] = c;
      continue;
    }

    uint16_t d = (c & 0xff) + 3;

    c = RCGetData_(src, &info2, &state, &b) + 1;
    src += b;

    if (a + d > size)
      break;

    if (a < c)
      return;

    while (d--) {
      uncompressed[a] = uncompressed[a - c];
      ++a;
    }
  }
}

void CXUnfilterDiff(void const *compressed, void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint32_t stat = IN_BUFFER_AT(uint8_t, src, 0) & 0x0f;
  int32_t size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, src, 0)) >> 8;
  uint32_t sum = 0;

  src += sizeof(uint32_t);

  if (stat != 0x01) {
    do {
      uint8_t num = *src++;

      size -= sizeof(uint8_t);
      sum += num;

      *dst++ = sum;
    } while (size > 0);
  } else {
    do {
      uint16_t num = CXiConvertEndian16_(*(uint16_t *)src);
      src += sizeof(uint16_t);

      size -= sizeof(uint16_t);
      sum += num;

      *(uint16_t *)dst = CXiConvertEndian16_(sum);
      dst += sizeof(uint16_t);
    } while (size > 0);
  }
}

CXCompressionHeader CXGetCompressionHeader(void const *compressed) {
  CXCompressionHeader header;
  header.type = (IN_BUFFER_AT(uint8_t, compressed, 0) & 0xf0) >> 4;
  header.stat = (IN_BUFFER_AT(uint8_t, compressed, 0) & 0x0f);
  header.size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, compressed, 0)) >> 8;

  if (!header.size) {
    header.size = CXiConvertEndian32_(IN_BUFFER_AT(uint32_t, compressed, 4));
  }

  return header;
}
