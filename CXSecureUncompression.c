#include "CXSecureUncompression.h"

/*******************************************************************************
 * headers
 */

#include "CXInternal.h"
#include "cx.h"
#include "macros.h"
#include <stdint.h>

/*******************************************************************************
 * types
 */

struct BitReader {
  uint8_t const *data; // size 0x04, offset 0x00
  int byteOffset;      // size 0x04, offset 0x04
  int unsigned value;  // size 0x04, offset 0x08
  int bit;             // size 0x04, offset 0x0c
  int fullSize;        // size 0x04, offset 0x0c
}; // size 0x14?

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

static int CXiHuffImportTree(uint16_t *tree, uint8_t const *stream, uint8_t,
                             unsigned);

// only reasonable way i see an init function come after a read function
static inline void BitReader_Init(struct BitReader *bitReader,
                                  uint8_t const *stream, int);
// why is it signed char now
static signed char BitReader_Read(struct BitReader *bitReader);

static inline void RCInitInfo_(struct RCInfo *rcInfo, uint8_t, int unsigned *);
static inline void RCInitState_(struct RCState *rcState);
static void RCAddCount_(struct RCInfo *rcInfo, uint16_t);
static uint16_t RCSearch_(struct RCInfo *rcInfo, int, unsigned, int);
static uint16_t RCGetData_(uint8_t const *stream, struct RCInfo *rcInfo,
                           struct RCState *rcState, int *);

/*******************************************************************************
 * functions
 */

static uint32_t ReadU32LE(const void *data, size_t offset) {
  const uint8_t *dataBytes = (const uint8_t *)data;
  return (uint32_t)dataBytes[offset] | (uint32_t)dataBytes[offset + 1] << 8 |
         (uint32_t)dataBytes[offset + 2] << 16 |
         (uint32_t)dataBytes[offset + 3] << 24;
}

static uint16_t ReadU16LE(const void *data, size_t offset) {
  const uint8_t *dataBytes = (const uint8_t *)data;
  return (uint16_t)dataBytes[offset] | (uint16_t)dataBytes[offset + 1] << 8;
}

CXSecureResult CXSecureUncompressAny(void const *compressed, uint32_t length,
                                     void *uncompressed) {
  switch (CXGetCompressionType(compressed)) {
  case CX_COMPRESSION_TYPE_RUN_LENGTH:
    return CXSecureUncompressRL(compressed, length, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_LEMPEL_ZIV:
    return CXSecureUncompressLZ(compressed, length, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_HUFFMAN:
    return CXSecureUncompressHuffman(compressed, length, uncompressed);
    break;

  case CX_COMPRESSION_TYPE_FILTER_DIFF:
    return CXSecureUnfilterDiff(compressed, length, uncompressed);
    break;

  default:
    return CXSECURE_EBADTYPE;
  }
}

CXSecureResult CXSecureUncompressRL(void const *compressed, uint32_t length,
                                    void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint8_t secstat = ReadU32LE(src, 0);

  uint32_t size = ReadU32LE(src, 0) >> 8;
  int32_t remainingLength = length;

  if ((secstat & CX_COMPRESSION_TYPE_MASK) != CX_COMPRESSION_TYPE_RUN_LENGTH)
    return CXSECURE_EBADTYPE;

  if ((secstat & 0x0f) != 0)
    return CXSECURE_EBADTYPE;

  if (length <= 4)
    return CXSECURE_E2SMALL;

  src += sizeof(uint32_t);
  remainingLength -= sizeof(uint32_t);

  if (!size) {
    if (remainingLength < 4)
      return CXSECURE_E2SMALL;

    size = ReadU32LE(src, 0);
    src += sizeof(uint32_t);
    remainingLength -= sizeof(uint32_t);
  }

  while (size) {
    uint8_t byte = *src++;

    int32_t runLength = byte & 0x7f;

    --remainingLength;
    if (remainingLength < 0)
      return CXSECURE_E2SMALL;

    if (!(byte & 0x80)) {
      runLength += 1;

      if (runLength > size)
        return CXSECURE_EBADSIZE;

      remainingLength -= runLength;
      if (remainingLength < 0)
        return CXSECURE_E2SMALL;

      size -= runLength;

      do
        *dst++ = *src++;
      while (--runLength > 0);
    } else {
      runLength += 3;

      if (runLength > size)
        return CXSECURE_EBADSIZE;

      size -= runLength;
      uint8_t byte = *src++;

      remainingLength -= 1;
      if (remainingLength < 0)
        return CXSECURE_E2SMALL;

      do
        *dst++ = byte;
      while (--runLength > 0);
    }
  }

  if (remainingLength > 0x20)
    return CXSECURE_E2BIG;

  (void)src;
  (void)src;

  return CXSECURE_ESUCCESS;
}

CXSecureResult CXSecureUncompressLZ(void const *compressed, uint32_t length,
                                    void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint8_t secstat = ReadU32LE(src, 0) & 0xff;
  uint32_t size = ReadU32LE(src, 0) >> 8;
  int32_t remainingLength = length;

  bool stat = BOOLIFY_TERNARY(IN_BUFFER_AT(uint8_t, src, 0) & 0x0f);

  if ((secstat & CX_COMPRESSION_TYPE_MASK) != CX_COMPRESSION_TYPE_LEMPEL_ZIV)
    return CXSECURE_EBADTYPE;

  if ((secstat & 0x0f) != 0 && (secstat & 0x0f) != 1)
    return CXSECURE_EBADTYPE;

  if (length <= 4)
    return CXSECURE_E2SMALL;

  src += sizeof(uint32_t);
  remainingLength -= sizeof(uint32_t);

  if (!size) {
    if (remainingLength < 4)
      return CXSECURE_E2SMALL;

    size = ReadU32LE(src, 0);
    src += sizeof(uint32_t);
    remainingLength -= sizeof(uint32_t);
  }

  while (size) {
    uint32_t i;
    uint32_t flags = *src++;

    --remainingLength;
    if (remainingLength < 0)
      return CXSECURE_E2SMALL;
    for (i = 0; i < 8; ++i) {

      if (!(flags & 0x80)) {
        *dst++ = *src++;

        --remainingLength;
        if (remainingLength < 0)
          return CXSECURE_E2SMALL;

        --size;
      } else {
        int32_t length2 = *src >> 4;

        if (!stat) {
          length2 += 3;
        } else {
          if (length2 == 0x01) {
            length2 = (*src++ & 0x0f) << 12;
            length2 |= *src++ << 4;
            length2 |= *src >> 4;
            length2 += 0x111;

            remainingLength -= 2;
          } else if (length2 == 0x00) {
            length2 = (*src++ & 0x0f) << 4;
            length2 |= *src >> 4;
            length2 += 0x11;

            remainingLength -= 1;
          } else {
            length2 += 0x01;
          }
        }

        int32_t offset = (*src++ & 0x0f) << 8;
        offset = (offset | *src++) + 1;

        remainingLength -= 2;
        if (remainingLength < 0)
          return CXSECURE_E2SMALL;

        if (length2 > size)
          return CXSECURE_EBADSIZE;

        if ((size_t)&dst[-offset] < (size_t)uncompressed)
          return CXSECURE_EBADSIZE;

        size -= length2;

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
				} while (--length2 > 0);
        // clang-format on
      }

      if (!size)
        break;

      flags <<= 1;
    }
  }

  if (remainingLength > 0x20)
    return CXSECURE_E2BIG;

  return CXSECURE_ESUCCESS;
}

_Bool CXiVerifyHuffmanTable_(void const *param_1, uint8_t param_2) {
  // What would this be for
  static uint8_t const FLAGS_ARRAY_NUM[4] ATTR_UNUSED = {0, 0, 0, 0x40};

  uint8_t const *a = param_1;
  uint8_t const *b = a + 1;
  unsigned c = *a;
  uint8_t const *d = (uint8_t *)param_1 + ((c + 1) << 1);

  uint8_t e[sizeof(uint8_t) * 0x40];
  for (uint32_t i = 0; i < ARRAY_LENGTH(e); ++i)
    e[i] = 0;

  if (param_2 == 4 && c >= 16)
    return false;

  unsigned f = 1;

  for (a = b; a < d; ++f, (void)++a) {
    if (e[f / 8] & (1 << (f % 8)))
      continue;

    int g = ((*a & 0x3f) + 1) << 1;
    size_t h = ((size_t)a >> 1 << 1) + g;

    if (*a == 0x00 && f >= c << 1)
      continue;

    if ((size_t)h >= (size_t)d)
      return false;

    if (*a & 0x80) {
      unsigned j = (f & ~1) + g;
      e[j / 8] |= (uint8_t)(1 << (j % 8));
    }

    if (*a & 0x40) {
      unsigned k = (f & ~1) + g + 1;
      e[k / 8] |= (uint8_t)(1 << (k % 8));
    }
  }

  return true;
}

CXSecureResult CXSecureUncompressHuffman(void const *compressed,
                                         uint32_t length, void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint8_t secstat = ReadU32LE(src, 0) & 0xff;
  int32_t size = ReadU32LE(src, 0) >> 8;

  uint8_t const *base = size ? src + 4 : src + 8;
  uint8_t const *basep1 = base + 1;

  uint32_t stat = IN_BUFFER_AT(uint8_t, src, 0) & 0x0f;
  unsigned b = 0;
  unsigned c = 0;
  int d = (stat & 7) + 4;
  int32_t remainingLength = length;
  unsigned e = (*base + 1) << 1;

  if ((secstat & CX_COMPRESSION_TYPE_MASK) != CX_COMPRESSION_TYPE_HUFFMAN)
    return CXSECURE_EBADTYPE;

  if (stat != 4 && stat != 8)
    return CXSECURE_EBADTYPE;

  if (!size) {
    if (length < e + 8)
      return CXSECURE_E2SMALL;

    size = ReadU32LE(src, 4);
  } else {
    if (length < e + 4)
      return CXSECURE_E2SMALL;
  }

  if (!CXiVerifyHuffmanTable_(base, stat))
    return CXSECURE_EBADTABLE;

  src = base + e;
  remainingLength -= (size_t)src - (size_t)compressed;

  if (remainingLength < 0)
    return CXSECURE_E2SMALL;

  base = basep1;

  while (size > 0) {
    int i = 32;

    uint32_t f;
#if defined(__MWERKS__)
    // NOTE: assignment to lvalue cast is a CW extension
    f = CXiConvertEndian32_(*((uint32_t *)(src))++);
#else
    f = ReadU32LE(src, 0);
    src += sizeof(uint32_t);
#endif

    remainingLength -= sizeof(uint32_t);
    if (remainingLength < 0)
      return CXSECURE_E2SMALL;

    while (--i >= 0) {
      int g = f >> 31;
      int h = *base;
      h <<= g;

      // ok
      uint8_t const *baseAligned =
          (uint8_t const *)(((size_t)(base) >> 1) << 1);
      base = baseAligned + (((*base & 0x3f) + 1) << 1) + g;

      if (h & 0x80) {
        b >>= stat;
        b |= *base << (32 - stat);
        base = basep1;

        ++c;

        if (size <= c * stat >> 3) {
          b >>= stat * (d - c);
          c = d;
        }

        if (c == d) {
#if defined(__MWERKS__)
          // NOTE: assignment to lvalue cast is a CW extension
          *((uint32_t *)(dst))++ = CXiConvertEndian32_(b);
#else
          dst[0] = b & 0xFF;
          dst[1] = b >> 8;
          dst[2] = b >> 16;
          dst[3] = b >> 24;
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

  if (remainingLength > 0x20)
    return CXSECURE_E2BIG;

  return CXSECURE_ESUCCESS;
}

static int CXiHuffImportTree(uint16_t *tree, uint8_t const *param_2,
                             uint8_t huffBitSize, unsigned param_4) {
  int a;
  int b;
  unsigned c;
  unsigned d;
  int e;
  unsigned f;
  unsigned g;

  a = 1;
  c = 0;

  d = 0;
  e = (1 << huffBitSize) - 1;

  f = 0;
  g = (1 << huffBitSize) << 1;

  if (huffBitSize > 8) {
    b = ReadU16LE(param_2, 0);

    param_2 += sizeof(uint16_t);
    f += sizeof(uint16_t);
  } else {
    b = IN_BUFFER_AT(uint8_t, param_2, 0);

    param_2 += sizeof(uint8_t);
    f += sizeof(uint8_t);
  }

  b = (b + 1) << 2;

  if (param_4 < b)
    return b;

  while (f < b) {
    while (d < huffBitSize) {
      c <<= 8;
      c |= *param_2++;
      ++f;
      d += 8;
    }

    if (a < g)
      tree[a++] = e & (c >> (d - huffBitSize));

    d -= huffBitSize;
  }

  (void)param_2;

  tree[0] = a - 1;

  return b;
}

CXSecureResult CXSecureUnfilterDiff(void const *compressed, uint32_t length,
                                    void *uncompressed) {
  uint8_t const *src = compressed;
  uint8_t *dst = uncompressed;

  uint32_t stat = IN_BUFFER_AT(uint8_t, src, 0) & 0x0f;
  uint8_t stat2 = ReadU32LE(src, 0) & 0xff;
  int32_t size = ReadU32LE(src, 0) >> 8;
  uint32_t sum = 0;

  int32_t remainingLength = length;

  if ((stat2 & CX_COMPRESSION_TYPE_MASK) != CX_COMPRESSION_TYPE_FILTER_DIFF)
    return CXSECURE_EBADTYPE;

  if (stat != 0 && stat != 1)
    return CXSECURE_EBADTYPE;

  if (length <= 4)
    return CXSECURE_E2SMALL;

  src += sizeof(uint32_t);
  remainingLength -= sizeof(uint32_t);

  if (stat != 0x01) {
    do {
      uint8_t num = *src++;

      remainingLength -= sizeof(uint8_t);
      if (remainingLength < 0)
        return CXSECURE_E2SMALL;

      size -= sizeof(uint8_t);

      sum += num;

      *dst++ = sum;
    } while (size > 0);
  } else {
    do {
      uint16_t num = ReadU16LE(src, 0);
      src += sizeof(uint16_t);

      remainingLength -= sizeof(uint16_t);
      if (remainingLength < 0)
        return CXSECURE_E2SMALL;

      size -= sizeof(uint16_t);

      sum += num;

      dst[0] = sum & 0xFF;
      dst[1] = sum >> 8;
      dst += sizeof(uint16_t);
    } while (size > 0);
  }

  if (remainingLength > 0x20)
    return CXSECURE_E2BIG;

  return CXSECURE_ESUCCESS;
}

static void BitReader_Init(struct BitReader *bitReader, uint8_t const *param_2,
                           int param_3) {
  bitReader->data = param_2;
  bitReader->byteOffset = 0;
  bitReader->value = 0;
  bitReader->bit = 0;
  bitReader->fullSize = param_3;
}

static signed char BitReader_Read(struct BitReader *bitReader) {
  if (!bitReader->bit) {
    if ((uint32_t)bitReader->byteOffset >= bitReader->fullSize)
      return CXSECURE_EBADTYPE;

    bitReader->value = bitReader->data[bitReader->byteOffset++];
    bitReader->bit = 8;
  }

  signed char a = (bitReader->value >> (bitReader->bit - 1)) & 0x01;
  --bitReader->bit;

  return a;
}

_Bool CXiLHVerifyTable(void const *tree, uint8_t huffBitSize) {
  uint16_t const *treeCast = tree;
  uint16_t const *treeData = treeCast + 1;
  unsigned c = *treeCast;
  uint16_t const *d = (uint16_t *)tree + c;
  uint16_t e = (1 << (huffBitSize - 2)) - 1;
  uint16_t f = 1 << (huffBitSize - 1);
  uint16_t g = 1 << (huffBitSize - 2);

  uint8_t h[sizeof(uint16_t) * 0x40];
  for (uint32_t i = 0; i < ARRAY_LENGTH(h); ++i)
    h[i] = 0;

  if (c > 1 << (huffBitSize + 1))
    return false;

  unsigned j = 1;
  for (treeCast = treeData; treeCast < d; ++j, (void)++treeCast) {
    if (h[j / 8] & (1 << (j % 8)))
      continue;

    int k = ((*treeCast & e) + 1) << 1;
    size_t l = ((size_t)treeCast & ~3) + (k << 1);

    if (*treeCast == 0x00 && j >= c - 4)
      continue;

    if (l >= (size_t)d)
      return false;

    if (*treeCast & f) {
      unsigned m = (j & ~1) + k;
      h[m / 8] |= (uint8_t)(1 << (m % 8));
    }

    if (*treeCast & g) {
      unsigned n = (j & ~1) + k + 1;
      h[n / 8] |= (uint8_t)(1 << (n % 8));
    }
  }

  return true;
}

static inline int CXiReadNextHuffValue(struct BitReader *bitReader,
                                       uint16_t *tree, uint8_t huffBitSize) {
  const uint16_t *const treeEnd = tree + *tree + 1;
  tree += 1;
  const int huffBitTop = (1 << huffBitSize) >> 1;
  const int huffBitMask = ((1 << huffBitSize) >> 2) - 1;

  do {
    signed char bit = BitReader_Read(bitReader);
    int index = (((*tree & huffBitMask) + 1) << 1) + bit;

    if (bit < 0)
      return CXSECURE_E2SMALL;

    if (*tree & (huffBitTop >> bit)) {
      tree = ROUND_DOWN_PTR(tree, 4);
      if (tree + index >= treeEnd)
        return CXSECURE_EBADTABLE;
      return tree[index];
    } else {
      tree = ROUND_DOWN_PTR(tree, 4);
      tree += index;
    }
  } while (tree < treeEnd);
  return CXSECURE_EBADTABLE;
}

CXSecureResult CXSecureUncompressLH(void const *compressed, uint32_t length,
                                    uint8_t *uncompressed, uint16_t *param_4) {
  uint32_t size;
  unsigned outpos = 0;
  uint8_t const *src = compressed;

  if ((IN_BUFFER_AT(uint8_t, compressed, 0) & CX_COMPRESSION_TYPE_MASK) !=
      CX_COMPRESSION_TYPE_LH) {
    return CXSECURE_EBADTYPE;
  }

  if (length <= 4)
    return CXSECURE_E2SMALL;

  uint16_t *tree0 = param_4;
  uint16_t *tree1 = param_4 + 0x400;
  uint16_t *work_end ATTR_UNUSED = param_4 + 0x440;

  // size
  size = ReadU32LE(src, 0) >> 8;
  src += sizeof(uint32_t);

  if (!size) {
    size = ReadU32LE(src, 0);
    src += sizeof(uint32_t);

    if (length < 8)
      return CXSECURE_E2SMALL;
  }

  src += CXiHuffImportTree(tree0, src, 9,
                           length - ((size_t)src - (size_t)compressed));
  if ((size_t)src > (size_t)compressed + length)
    return CXSECURE_E2SMALL;

  if (!CXiLHVerifyTable(tree0, 9))
    return CXSECURE_EBADTABLE;

  src += CXiHuffImportTree(tree1, src, 5,
                           length - ((size_t)src - (size_t)compressed));

  if ((size_t)src > (size_t)compressed + length)
    return CXSECURE_E2SMALL;

  if (!CXiLHVerifyTable(tree1, 5))
    return CXSECURE_EBADTABLE;

  struct BitReader bitReader;
  BitReader_Init(&bitReader, src, length - ((size_t)src - (size_t)compressed));

  while (outpos < size) {
    int ret = CXiReadNextHuffValue(&bitReader, tree0, 9);
    if (ret < 0)
      return ret;

    if (ret < 0x100) {
      // LZ flag not set, just a byte
      uncompressed[outpos++] = ret;
      continue;
    }

    uint16_t ref_length = (ret & 0xff) + 3;

    ret = CXiReadNextHuffValue(&bitReader, tree1, 5);
    if (ret < 0)
      return ret;
    uint16_t ref_offset_bits = ret;
    uint16_t ref_offset = 0;
    const uint16_t ref_offset_bits_save = ref_offset_bits;

    if (ref_offset_bits) {
      ref_offset = 1;

      while (--ref_offset_bits) {
        ref_offset <<= 1;
        ref_offset |= BitReader_Read(&bitReader);
      }
    }

    if (outpos < ++ref_offset)
      return CXSECURE_EBADSIZE;

    if (outpos + ref_length > size)
      return CXSECURE_EBADSIZE;

    while (ref_length--) {
      uncompressed[outpos] = uncompressed[outpos - ref_offset];
      ++outpos;
    }
  }

  if ((uint32_t)bitReader.fullSize - bitReader.byteOffset > 0x20)
    return CXSECURE_E2BIG;

  (void)outpos;
  (void)compressed;

  return CXSECURE_ESUCCESS;
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

CXSecureResult CXSecureUncompressLRC(void const *compressed, uint32_t length,
                                     uint8_t *uncompressed, unsigned *param_3) {
  uint8_t const *src = compressed;
  unsigned a = 0;
  uint32_t size = 0;

  if ((IN_BUFFER_AT(uint8_t, compressed, 0) & CX_COMPRESSION_TYPE_MASK) !=
      CX_COMPRESSION_TYPE_LRC) {
    return CXSECURE_EBADTYPE;
  }

  if (length <= 4)
    return CXSECURE_E2SMALL;

  struct RCInfo info1;
  RCInitInfo_(&info1, 9, param_3);

  struct RCInfo info2;
#if !defined(NDEBUG) // TODO (What)
  RCInitInfo_(&info2, 12, param_3 + 0x400);
#else
  RCInitInfo_(&info2, 12, param_3 += 0x400);
#endif

  struct RCState state;
  RCInitState_(&state);

  // size
  size = ReadU32LE(src, 0) >> 8;
  src += sizeof(uint32_t);

  if (!size) {
    size = ReadU32LE(src, 0);
    src += sizeof(uint32_t);

    if (length < 8)
      return CXSECURE_E2SMALL;
  }

  if (length - ((size_t)src - (size_t)compressed) < 4)
    return CXSECURE_E2SMALL;

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
      return CXSECURE_EBADSIZE;

    if (a < c)
      return CXSECURE_EBADSIZE;

    if ((size_t)src - (size_t)compressed > length)
      return CXSECURE_E2SMALL;

    while (d--) {
      uncompressed[a] = uncompressed[a - c];
      ++a;
    }
  }

  (void)compressed;

  return CXSECURE_ESUCCESS;
}
