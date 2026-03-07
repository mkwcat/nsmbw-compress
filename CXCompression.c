#include "CXCompression.h"

/*******************************************************************************
 * headers
 */

#include "CXInternal.h"
#include "cx.h"
#include "decomp.h"
#include "macros.h"
#include "types.h"
#include <assert.h>

/*******************************************************************************
 * types
 */

struct LZTable {
  unk2_t unsigned at_0x00; // size 0x02, offset 0x00
  unk2_t unsigned at_0x02; // size 0x02, offset 0x02
  unk2_t signed *at_0x04;  // size 0x04, offset 0x04
  unk2_t signed *at_0x08;  // size 0x04, offset 0x08
  unk2_t signed *at_0x0c;  // size 0x04, offset 0x0c
  u32 windowSize;          // Added
}; // size 0x10

struct at0 {
  unk4_t unsigned count;   // size 0x04, offset 0x00
  unk2_t unsigned at_0x04; // size 0x02, offset 0x04
  unk2_t signed at_0x06;   // size 0x02, offset 0x06
  unk2_t signed at_0x08;   // size 0x02, offset 0x08
  unk2_t signed at_0x0a;   // size 0x02, offset 0x0a
  unk2_t unsigned refSize; // size 0x02, offset 0x0c
  unk2_t unsigned at_0x0e; // size 0x02, offset 0x0e
  unk4_t ref;              // size 0x04, offset 0x10
  unk1_t unsigned at_0x14; // size 0x01, offset 0x14
  /* 1 byte padding */
  u16 at_0x16; // size 0x02, offset 0x16
}; // size 0x18

struct at8 {
  unk1_t unsigned at_0x00; // size 0x01, offset 0x00
  unk1_t unsigned at_0x01; // size 0x01, offset 0x01
  unk2_t unsigned at_0x02; // size 0x02, offset 0x02
  unk2_t unsigned at_0x04; // size 0x02, offset 0x04
}; // size 0x06

struct HuffTable {
  struct at0 *at_0x00; // size 0x04, offset 0x00
  u16 *at_0x04;        // size 0x04, offset 0x04
  struct at8 *at_0x08; // size 0x04, offset 0x08
  u16 treeEntryCount;  // size 0x02, offset 0x0c
                       /* 2 bytes padding */
}; // size 0x10

/*******************************************************************************
 * local function declarations
 */

static unk_t SearchLZ(struct LZTable const *table, byte_t const *data, u32,
                      unk2_t unsigned *, unk4_t);
static void LZInitTable(struct LZTable *table, unk2_t *work, u32 windowSize);
static void SlideByte(struct LZTable *table, byte_t const *data);
static void LZSlide(struct LZTable *table, byte_t const *data, u32 length);

static void HuffInitTable(struct HuffTable *, void *work, u16);

static void HuffCountData(struct at0 *, byte_t const *data, u32 size,
                          u8 huffBitSize);
static u16 HuffConstructTree(struct at0 *, unk_t unsigned);
static void HuffAddParentDepthToTable(struct at0 *, u16, u16);
static void HuffAddCodeToTable(struct at0 *, u16, unk_t);
static u16 HuffAddCountHWordToTable(struct at0 *, u16);
static void HuffMakeHuffTree(struct HuffTable *, unk2_t unsigned,
                             u8 huffBitSize);
static void HuffMakeSubsetHuffTree(struct HuffTable *, u16, u8);
static u8 HuffRemainingNodeCanSetOffset(struct HuffTable *, s16,
                                        int huffBitSize);
static void HuffSetOneNodeOffset(struct HuffTable *, u16, u8);
static u32 HuffConvertData(struct at0 *, byte_t const *srcp, byte_t *dstp,
                           u32 size, u32 maxLength, u8 huffBitSize);

/*******************************************************************************
 * functions
 */

u32 CXCompressLZImpl(byte_t const *srcp, u32 size, byte_t *dstp, void *work,
                     BOOL param_5) {
  u32 length;
  u32 a;
  byte_t b;
  u16 c;
  byte_t *d;

  u8 i;

  u32 f;
  unk4_t long g; // or unsigned

  g = param_5 ? 0x10110 : 0x12;

  assert(((size_t)srcp & 0x1) == 0);
  assert(work != NULL);
  assert(size > 4);

  if (size < 0x1000000) {
    *(byte4_t *)dstp = CXiConvertEndian32_(
        size << 8 | CX_COMPRESSION_TYPE_LEMPEL_ZIV | BOOLIFY_TERNARY(param_5));

    dstp += sizeof(byte4_t);

    length = sizeof(byte4_t);
  } else {
    *(byte4_t *)dstp = CXiConvertEndian32_(CX_COMPRESSION_TYPE_LEMPEL_ZIV |
                                           BOOLIFY_TERNARY(param_5));
    dstp += sizeof(byte4_t);

    *(byte4_t *)dstp = CXiConvertEndian32_(size);
    dstp += sizeof(byte4_t);

    length = sizeof(byte4_t) + sizeof(byte4_t);
  }

  f = size;

  struct LZTable table;
  LZInitTable(&table, work, 0x1000);

  while (size) {
    b = 0;
    d = dstp++;
    ++length;

    for (i = 0; i < 8; ++i) {
      b <<= 1;

      if (!size)
        continue;

      a = SearchLZ(&table, srcp, size, &c, g);
      if (a) {
        b |= 1;

        if (length + 2 >= f)
          return 0;

        u32 h;

        if (param_5) {
          if (a >= 0x111) {
            h = a - 0x111;

            *dstp++ = h >> 12 | 0x10;
            *dstp++ = h >> 4;
            length += 2;
          } else if (a >= 0x11) {
            h = a - 0x11;

            *dstp++ = h >> 4;
            length += 1;
          } else {
            h = a - 0x1;
          }
        } else {
          h = a - 3;
        }

        *dstp++ = (h << 4) | (c - 1) >> 8;
        *dstp++ = c - 1;
        length += 2;

        LZSlide(&table, srcp, a);

        srcp += a;
        size -= a;
      } else {
        if (length + 1 >= f)
          return 0;

        LZSlide(&table, srcp, 1);
        *dstp++ = *srcp++;

        --size;
        ++length;
      }
    }

    *d = b;
  }

  for (i = 0; (length + i) % 4 != 0; ++i)
    *dstp++ = 0;

  (void)size;

  return length;
}

static unk_t SearchLZ(struct LZTable const *table, byte_t const *data,
                      u32 param_3, unk2_t unsigned *param_4, unk4_t param_5) {
  byte_t const *a;
  byte_t const *b;
  byte_t const *c;
  unk2_t unsigned d;
  unk2_t unsigned e;
  unk_t unsigned f;
  unk2_t signed *g;
  unk2_t unsigned h;
  unk_t l;
  unk_t unsigned m;

  m = 2;
  g = table->at_0x04;
  e = table->at_0x00;
  h = table->at_0x02;

  if (param_3 < 3)
    return 0;

  l = table->at_0x08[*data];

  while (l != -1) {
    if (l < e)
      b = data - e + l;
    else
      b = data - h - e + l;

    if (b[1] != data[1] || b[2] != data[2]) {
      l = g[l];
      continue;
    }

    if (data - b < 2)
      break;

    f = 3;
    c = b + 3;
    a = data + 3;

    while (a - data < param_3 && *a == *c) {
      ++a;
      ++c;
      ++f;

      if (f == param_5)
        break;
    }

    if (f > m) {
      m = f;
      d = data - b;

      if (m == param_5 || m == param_3)
        break;
    }

    l = g[l];
  }

  if (m < 3)
    return 0;

  *param_4 = d;
  return m;
}

static void LZInitTable(struct LZTable *table, unk2_t *work, u32 windowSize) {
  table->at_0x04 = work;
  table->at_0x08 = work + windowSize;
  table->at_0x0c = work + windowSize + 0x100;

  u16 i;
  for (i = 0; i < 0x100; ++i) {
    table->at_0x08[i] = -1;
    table->at_0x0c[i] = -1;
  }

  table->at_0x00 = 0;
  table->at_0x02 = 0;
  table->windowSize = windowSize;
}

static void SlideByte(struct LZTable *table, byte_t const *data) {
  unk1_t unsigned a;
  unk2_t unsigned b;
  unk2_t c;
  unk2_t *d_at8;
  unk2_t *d_at4;
  unk2_t *d_atc;

  a = *data;
  d_at8 = table->at_0x08;
  d_at4 = table->at_0x04;
  d_atc = table->at_0x0c;

  unk2_t unsigned const g = table->at_0x00;
  unk2_t unsigned const h = table->at_0x02;

  s32 const windowSize = table->windowSize;

  if (h == windowSize) {
    unk1_t unsigned i = data[-windowSize];

    if ((d_at8[i] = d_at4[d_at8[i]]) == -1)
      d_atc[i] = -1;

    b = g;
  } else {
    b = h;
  }

  c = d_atc[a];

  if (c == -1)
    d_at8[a] = b;
  else
    d_at4[c] = b;

  d_atc[a] = b;
  d_at4[b] = -1;

  if (h == windowSize)
    table->at_0x00 = (g + 1) % windowSize;
  else
    ++table->at_0x02;
}

static void LZSlide(struct LZTable *table, byte_t const *data, u32 length) {
  int i;
  for (i = 0; i < length; ++i)
    SlideByte(table, data++);
}

u32 CXCompressRL(byte_t const *srcp, u32 size, byte_t *dstp) {
  unk_t unsigned a;
  unk_t unsigned b;
  unk_t c;
  unk1_t unsigned d;
  unk1_t unsigned e;
  unk1_t unsigned f;
  byte_t const *g;

  assert(srcp != NULL);
  assert(dstp != NULL);
  assert(size > 4);

  if (size < 0x1000000) {
    IN_BUFFER_AT(byte4_t, dstp, 0) =
        CXiConvertEndian32_(size << 8 | CX_COMPRESSION_TYPE_RUN_LENGTH);

    b = sizeof(byte4_t);
  } else {
    IN_BUFFER_AT(byte4_t, dstp, 0) =
        CXiConvertEndian32_(CX_COMPRESSION_TYPE_RUN_LENGTH);
    IN_BUFFER_AT(byte4_t, dstp, 4) = CXiConvertEndian32_(size);

    b = sizeof(byte4_t) + sizeof(byte4_t);
  }

  c = 0;
  f = 0;
  d = 0;

  while (c < size) {
    g = srcp + c;

    for (a = 0; a < 128; ++a) {
      if (c + f >= size) {
        f = size - c;
        break;
      }

      if (c + f + 2 < size && g[a] == g[a + 1] && g[a] == g[a + 2]) {
        d = 1;
        break;
      }

      ++f;
    }

    if (f) {
      if (b + f + 1 >= size)
        return 0;

      dstp[b++] = f - 1;

      for (a = 0; a < f; ++a)
        dstp[b++] = srcp[c++];

      f = 0;
    }

    if (d) {
      e = 3;

      // What
      for (a = 0 + 3; a < 127 + 3; ++a) {
        if (c + e >= size) {
          e = size - c;
          break;
        }

        if (srcp[c] != srcp[c + e])
          break;

        ++e;
      }

      if (b + 2 >= size)
        return 0;

      dstp[b++] = (e - 3) | 0x80;
      dstp[b++] = srcp[c];

      c += e;
      d = 0;
    }
  }

  for (a = 0; (b + a) % 4 != 0; ++a)
    dstp[b + a] = 0;

  (void)c;

  return b;
}

u32 CXCompressHuffman(byte_t const *srcp, u32 size, byte_t *dstp,
                      u8 huffBitSize, void *work) {
  int i;

  u16 a;
  unk2_t unsigned b = 1 << huffBitSize;

  assert(srcp != NULL);
  assert(dstp != NULL);
  assert(huffBitSize == 4 || huffBitSize == 8);
  assert(work != NULL);
  assert(((size_t)work & 0x3) == 0);
  assert(size > 4);

  struct HuffTable table;

  HuffInitTable(&table, work, b);
  HuffCountData(table.at_0x00, srcp, size, huffBitSize);
  a = HuffConstructTree(table.at_0x00, b);
  HuffMakeHuffTree(&table, a, huffBitSize);

  *table.at_0x04 = --table.treeEntryCount;

  u32 headerLength;
  if (size < 0x1000000) {
    IN_BUFFER_AT(byte4_t, dstp, 0) = CXiConvertEndian32_(
        size << 8 | CX_COMPRESSION_TYPE_HUFFMAN | huffBitSize);

    headerLength = sizeof(byte4_t);
  } else {
    IN_BUFFER_AT(byte4_t, dstp, 0) =
        CXiConvertEndian32_(CX_COMPRESSION_TYPE_HUFFMAN | huffBitSize);
    IN_BUFFER_AT(byte4_t, dstp, 4) = CXiConvertEndian32_(size);

    headerLength = sizeof(byte4_t) + sizeof(byte4_t);
  }

  u32 length = headerLength;

  if (length + ((table.treeEntryCount + 1) << 1) >= size)
    return 0;

  for (i = 0; i < (u16)((table.treeEntryCount + 1) << 1); ++i) {
    u8 c = (table.at_0x04[i] & 0xC000) >> 8;
    dstp[length++] = table.at_0x04[i] | c;
  }

  while (length % 4 != 0) {
    if (length % 2 != 0) {
      ++table.treeEntryCount;
      ++dstp[headerLength];
    }

    dstp[length++] = 0;
  }

  { // random block
    unk_t c = HuffConvertData(table.at_0x00, srcp, dstp + length, size,
                              size - length, huffBitSize);
    if (!c)
      return 0;

    length += c;
  }

  return length;
}

static void HuffInitTable(struct HuffTable *param_1, void *work,
                          u16 huffBitMax) {
  u32 i;

  // TODO: clean up types here
  param_1->at_0x00 = (struct at0 *)((size_t)work + 0);
  param_1->at_0x04 = (u16 *)((size_t)work + huffBitMax * 0x30);
  param_1->at_0x08 =
      (struct at8 *)((size_t)param_1->at_0x04 + huffBitMax * (sizeof(u16) * 2));
  param_1->treeEntryCount = 1;

  { // random block
    struct at0 *a = param_1->at_0x00;
    struct at0 initial = {.at_0x08 = -1, .at_0x0a = -1};

    for (i = 0; i < huffBitMax << 1; ++i) {
      a[i] = initial;
      a[i].at_0x04 = i;
    }
  }

  { // another random block
    struct at8 initial = {.at_0x00 = 1, .at_0x01 = 1};

    u16 *b = param_1->at_0x04;
    struct at8 *c = param_1->at_0x08;

    for (i = 0; i < huffBitMax; ++i) {
      b[i * 2 + 0] = 0;
      b[i * 2 + 1] = 0;

      c[i] = initial;
    }
  }
}

static void HuffCountData(struct at0 *param_1, byte_t const *data, u32 size,
                          u8 huffBitSize) {
  u32 i;

  byte_t a;

  if (huffBitSize == 8) {
    for (i = 0; i < size; ++i)
      ++param_1[data[i]].count;
  } else // (huffBitSize == 4)
  {
    for (i = 0; i < size; ++i) {
      a = (data[i] & 0xf0) >> 4;
      ++param_1[a].count;

      a = (data[i] & 0x0f) >> 0;
      ++param_1[a].count;
    }
  }
}

static u16 HuffConstructTree(struct at0 *param_1, unk_t unsigned huffBitMax) {
  int i;

  unk_t signed a;
  unk_t signed b;
  u16 c = huffBitMax;

  a = -1;
  b = -1;

  u16 d;
  u16 e ATTR_UNUSED; // ?

  while (true) {
    for (i = 0; i < c; ++i) {
      if (param_1[i].count && !param_1[i].at_0x06) {
        if (a < 0)
          a = i;
        else if (param_1[i].count < param_1[a].count)
          a = i;
      }
    }

    for (i = 0; i < c; ++i) {
      if (param_1[i].count && !param_1[i].at_0x06 && i != a) {
        if (b < 0)
          b = i;
        else if (param_1[i].count < param_1[b].count)
          b = i;
      }
    }

    if (b < 0) {
      if (c == huffBitMax) {
        param_1[c].count = param_1[a].count;
        param_1[c].at_0x08 = a;
        param_1[c].at_0x0a = a;
        param_1[c].at_0x0e = 1;

        param_1[a].at_0x06 = c;
        param_1[a].at_0x14 = 0;
        param_1[a].refSize = 1;
      } else {
        --c;
      }

      d = c;
      e = (((d - huffBitMax) + 1) << 1) + 1;

      break;
    }

    param_1[c].count = param_1[a].count + param_1[b].count;
    param_1[c].at_0x08 = a;
    param_1[c].at_0x0a = b;

    if (param_1[a].at_0x0e > param_1[b].at_0x0e)
      param_1[c].at_0x0e = param_1[a].at_0x0e + 1;
    else
      param_1[c].at_0x0e = param_1[b].at_0x0e + 1;

    param_1[a].at_0x06 = param_1[b].at_0x06 = c;
    param_1[a].at_0x14 = 0;
    param_1[b].at_0x14 = 1;

    HuffAddParentDepthToTable(param_1, a, b);

    ++c;
    a = b = -1;
  }

  HuffAddCodeToTable(param_1, d, 0);
  HuffAddCountHWordToTable(param_1, d);

  return d;
}

static void HuffAddParentDepthToTable(struct at0 *param_1, u16 param_2,
                                      u16 param_3) {
  ++param_1[param_2].refSize;
  ++param_1[param_3].refSize;

  if (param_1[param_2].at_0x0e) {
    HuffAddParentDepthToTable(param_1, param_1[param_2].at_0x08,
                              param_1[param_2].at_0x0a);
  }

  if (param_1[param_3].at_0x0e) {
    HuffAddParentDepthToTable(param_1, param_1[param_3].at_0x08,
                              param_1[param_3].at_0x0a);
  }
}

static void HuffAddCodeToTable(struct at0 *param_1, u16 param_2,
                               unk_t param_3) {
  param_1[param_2].ref = param_3 << 1 | param_1[param_2].at_0x14;

  if (param_1[param_2].at_0x0e) {
    HuffAddCodeToTable(param_1, param_1[param_2].at_0x08, param_1[param_2].ref);
    HuffAddCodeToTable(param_1, param_1[param_2].at_0x0a, param_1[param_2].ref);
  }
}

static u16 HuffAddCountHWordToTable(struct at0 *param_1, u16 param_2) {
  u16 a;
  u16 b;

  switch (param_1[param_2].at_0x0e) {
  case 0:
    return 0;

  case 1:
    a = b = 0;
    break;

  default:
    a = HuffAddCountHWordToTable(param_1, param_1[param_2].at_0x08);
    b = HuffAddCountHWordToTable(param_1, param_1[param_2].at_0x0a);
    break;
  }

  param_1[param_2].at_0x16 = a + b + 1;

  return a + b + 1;
}

static void HuffMakeHuffTree(struct HuffTable *param_1, unk2_t unsigned param_2,
                             u8 huffBitSize) {
  s16 i;

  s16 a;
  s16 b;

  unk2_t c;
  unk2_t d;
  s16 e;
  s16 f;
  u16 g;
  unk1_t h ATTR_UNUSED; // ?

  bool l;

  param_1->treeEntryCount = 1;
  d = 0;
  param_1->at_0x08->at_0x00 = 0;
  param_1->at_0x08->at_0x04 = param_2;
  const int maxTreeSize = 1 << (huffBitSize - 2);

loop:
  while (true) {
    u16 m = 0;

    for (i = 0; i < param_1->treeEntryCount; ++i) {
      if (param_1->at_0x08[i].at_0x00)
        ++m;

      if (param_1->at_0x08[i].at_0x01)
        ++m;
    }

    c = -1;
    b = -1;
    h = 0;
    l = 0;

    for (i = 0; i < param_1->treeEntryCount; ++i) {
      e = param_1->treeEntryCount - i;

      if (param_1->at_0x08[i].at_0x00) {
        a = param_1->at_0x00[param_1->at_0x08[i].at_0x02].at_0x16;

        if (a + m <= maxTreeSize &&
            HuffRemainingNodeCanSetOffset(param_1, a, huffBitSize)) {
          if (a > c) {
            b = i;
            f = 0;
          } else if (a == c && e > d) {
            b = i;
            f = 0;
          }
        }
      }

      if (param_1->at_0x08[i].at_0x01) {
        a = param_1->at_0x00[param_1->at_0x08[i].at_0x04].at_0x16;

        if (a + m <= maxTreeSize &&
            HuffRemainingNodeCanSetOffset(param_1, a, huffBitSize)) {
          if (a > c) {
            b = i;
            f = 1;
          } else if (a == c && e > d) {
            b = i;
            f = 1;
          }
        }
      }
    }

    if (b >= 0)
      HuffMakeSubsetHuffTree(param_1, b, f);
    else
      break;
  }

  for (i = 0; i < param_1->treeEntryCount; ++i) {
    g = 0;
    l = 0;

    if (param_1->at_0x08[i].at_0x00)
      g = param_1->at_0x00[param_1->at_0x08[i].at_0x02].at_0x16;

    if (param_1->at_0x08[i].at_0x01 &&
        param_1->at_0x00[param_1->at_0x08[i].at_0x04].at_0x16 > g) {
      l = 1;
    }

    if (g || l) {
      HuffSetOneNodeOffset(param_1, i, l);
      goto loop;
    }
  }
}

static void HuffMakeSubsetHuffTree(struct HuffTable *param_1, u16 param_2,
                                   u8 param_3) {
  u16 i = param_1->treeEntryCount;

  HuffSetOneNodeOffset(param_1, param_2, param_3);

  if (param_3)
    param_1->at_0x08[param_2].at_0x01 = 0;
  else
    param_1->at_0x08[param_2].at_0x00 = 0;

  for (; i < param_1->treeEntryCount; ++i) {
    if (param_1->at_0x08[i].at_0x00) {
      HuffSetOneNodeOffset(param_1, i, 0);
      param_1->at_0x08[i].at_0x00 = 0;
    }

    if (param_1->at_0x08[i].at_0x01) {
      HuffSetOneNodeOffset(param_1, i, 1);
      param_1->at_0x08[i].at_0x01 = 0;
    }
  }
}

static u8 HuffRemainingNodeCanSetOffset(struct HuffTable *param_1, s16 param_2,
                                        int huffBitSize) {
  u16 i;

  const int maxTreeSize = 1 << (huffBitSize - 2);
  s16 a = maxTreeSize - param_2;

  for (i = 0; i < param_1->treeEntryCount; ++i) {
    if (param_1->at_0x08[i].at_0x00) {
      if (param_1->treeEntryCount - i <= a)
        --a;
      else
        return false;
    }

    if (param_1->at_0x08[i].at_0x01) {
      if (param_1->treeEntryCount - i <= a)
        --a;
      else
        return false;
    }
  }

  return true;
}

static void HuffSetOneNodeOffset(struct HuffTable *param_1, u16 param_2,
                                 u8 param_3) {
  u16 a;
  u16 b = 0;

  struct at0 *table0 = param_1->at_0x00;
  u16 *table4 = param_1->at_0x04;
  struct at8 *table8 = param_1->at_0x08;
  u16 tablec = param_1->treeEntryCount;

  if (param_3 != 0) {
    a = table8[param_2].at_0x04;
    table8[param_2].at_0x01 = 0;
  } else // if (param_3 == 0)
  {
    a = table8[param_2].at_0x02;
    table8[param_2].at_0x00 = 0;
  }

  if (!table0[table0[a].at_0x08].at_0x0e) {
    b |= 0x8000;

    table4[tablec * 2 + 0] = table0[a].at_0x08;
    table8[tablec].at_0x02 = (u8)table0[a].at_0x08;
    table8[tablec].at_0x00 = 0;
  } else {
    table8[tablec].at_0x02 = table0[a].at_0x08;
  }

  if (!table0[table0[a].at_0x0a].at_0x0e) {
    b |= 0x4000;

    table4[tablec * 2 + 1] = table0[a].at_0x0a;
    table8[tablec].at_0x04 = (u8)table0[a].at_0x0a;
    table8[tablec].at_0x01 = 0;
  } else {
    table8[tablec].at_0x04 = table0[a].at_0x0a;
  }

  b |= (u16)(tablec - param_2 - 1);

  table4[param_2 * 2 + param_3] = b;
  ++param_1->treeEntryCount;
}

static u32 HuffConvertData(struct at0 *param_1, byte_t const *srcp,
                           byte_t *dstp, u32 size, u32 maxLength,
                           u8 huffBitSize) {
  u32 i, j, k;

  unk_t unsigned a = 0;
  unk_t unsigned b = 0;
  u32 length = 0;

  for (i = 0; i < size; ++i) {
    byte_t d;
    byte_t e = srcp[i];

    if (huffBitSize == 8) {
      a = a << param_1[e].refSize | param_1[e].ref;
      b += param_1[e].refSize;

      if (length + b / 8 >= maxLength)
        return 0;

      for (j = 0; j < b / 8; ++j)
        dstp[length++] = a >> (b - ((j + 1) << 3));

      b %= 8;
    } else // if (huffBitSize == 4)
    {
      for (j = 0; j < 8 / 4; ++j) {
        if (j != 0)
          d = e >> 4;
        else
          d = e & 0x0f;

        a = (a << param_1[d].refSize) | param_1[d].ref;
        b += param_1[d].refSize;

        if (length + b / 8 >= maxLength)
          return 0;

        for (k = 0; k < b / 8; ++k)
          dstp[length++] = a >> (b - (k + 1) * 8);

        b %= 8;
      }
    }
  }

  if (b) {
    if (length + 1 >= maxLength)
      return 0;
    else
      dstp[length++] = a << (8 - b);
  }

  while (length % 4 != 0)
    dstp[length++] = 0;

  // I Love Reinventing stwbrx !
  for (i = 0; i < length / sizeof(byte4_t); ++i) {
    byte_t tmp;

    tmp = dstp[i * sizeof(byte4_t) + 0];
    dstp[i * sizeof(byte4_t) + 0] = dstp[i * sizeof(byte4_t) + 3];
    dstp[i * sizeof(byte4_t) + 3] = tmp;

    tmp = dstp[i * sizeof(byte4_t) + 1];
    dstp[i * sizeof(byte4_t) + 1] = dstp[i * sizeof(byte4_t) + 2];
    dstp[i * sizeof(byte4_t) + 2] = tmp;
  }

  return length;
}

static int CountBitsNeededToEncode(u32 value) {
  int bits = 0;
  while (value) {
    value >>= 1;
    ++bits;
  }
  return bits;
}

static u32 LHEncodeLZ(byte_t const *srcp, u32 size, byte_t *dstp, void *work,
                      struct HuffTable *huffTables) {
  u32 length;
  u32 a;
  byte_t b;
  u16 c;
  byte_t *d;

  u8 i;

  u32 f;
  unk4_t long g; // or unsigned

  g = 0x102;

  assert(((size_t)srcp & 0x1) == 0);
  assert(work != NULL);
  assert(size > 4);

  length = 0;

  f = size;

  struct LZTable table;
  LZInitTable(&table, work, CX_COMPRESS_LH_LZ_DETAIL_SIZE);

  while (size) {
    b = 0;
    d = dstp++;
    *d = 0;
    ++length;

    for (i = 0; i < 8; ++i) {
      b <<= 1;

      if (!size)
        continue;

      a = SearchLZ(&table, srcp, size, &c, g);
      if (a) {
        b |= 1;

        if (length + 2 >= f)
          return 0;

        *dstp++ = a - 3;
        c -= 1;
        // How this is stored doesn't matter as it's only used internally
        *dstp++ = c & 0xff;
        *dstp++ = c >> 8;
        length += 3;

        // LH stuff
        huffTables[0].at_0x00[0x100 | (u8)(a - 3)].count++;

        // How many bits are needed to encode the reference offset
        huffTables[1].at_0x00[CountBitsNeededToEncode(c)].count++;

        LZSlide(&table, srcp, a);

        srcp += a;
        size -= a;
      } else {
        if (length + 1 >= f)
          return 0;

        LZSlide(&table, srcp, 1);

        huffTables[0].at_0x00[*srcp].count++;
        *dstp++ = *srcp++;

        --size;
        ++length;
      }
    }

    *d = b;
  }

  for (i = 0; (length + i) % 4 != 0; ++i)
    *dstp++ = 0;

  (void)size;

  return length;
}

struct BitWriter {
  byte_t *data;
  uint32_t offset;
  uint32_t value;
  uint32_t bit;
};

static void BitWriter_Init(struct BitWriter *bitWriter, byte_t *data) {
  bitWriter->data = data;
  bitWriter->offset = 0;
  bitWriter->value = 0;
  bitWriter->bit = 0;
}

static void BitWriter_Write(struct BitWriter *bitWriter, u32 value, u8 size) {
  if (size == 0)
    return;
  assert(size <= 32);

  value = bitWriter->value << size | (value & ((1 << size) - 1));
  uint32_t end = bitWriter->bit + size;
  while (end >= 8) {
    bitWriter->data[bitWriter->offset++] = value >> (end -= 8);
  }
  bitWriter->value = value;
  bitWriter->bit = end;
}

static void BitWriter_Flush(struct BitWriter *bitWriter) {
  if (bitWriter->bit) {
    bitWriter->data[bitWriter->offset++] = bitWriter->value
                                           << (8 - bitWriter->bit);
    bitWriter->value = 0;
    bitWriter->bit = 0;
  }
}

u32 CXCompressLH(byte_t const *srcp, u32 size, byte_t *dstp, byte_t *tmp_dstp,
                 void *work) {
  struct CXiCompressLHWork *lhWork = work;

  u32 length;

  if (size < 0x1000000) {
    *(byte4_t *)dstp = CXiConvertEndian32_(size << 8 | CX_COMPRESSION_TYPE_LH);

    dstp += sizeof(byte4_t);

    length = sizeof(byte4_t);
  } else {
    *(byte4_t *)dstp = CXiConvertEndian32_(CX_COMPRESSION_TYPE_LH);
    dstp += sizeof(byte4_t);

    *(byte4_t *)dstp = CXiConvertEndian32_(size);
    dstp += sizeof(byte4_t);

    length = sizeof(byte4_t) + sizeof(byte4_t);
  }

  struct HuffTable table[2];
  HuffInitTable(&table[0], lhWork->huffLWork, 1 << 9);
  HuffInitTable(&table[1], lhWork->huffRWork, 1 << 5);

  u32 lz_length = LHEncodeLZ(srcp, size, tmp_dstp, lhWork->lzWork, table);
  if (!lz_length)
    return 0;

  u16 a = HuffConstructTree(table[0].at_0x00, 1 << 9);
  HuffMakeHuffTree(&table[0], a, 9);
  u16 b = HuffConstructTree(table[1].at_0x00, 1 << 5);
  HuffMakeHuffTree(&table[1], b, 5);
  table[0].treeEntryCount++;
  table[1].treeEntryCount++;

  struct BitWriter bitWriter;
  BitWriter_Init(&bitWriter, dstp);

  // Write tables
  for (int t = 0; t < 2; ++t) {
    const int bitSize = t ? 5 : 9;
    const u16 tableSize = table[t].treeEntryCount << 1;
    const u16 tableByteSize = (bitSize * (tableSize >> 2)) / 8;
    if (bitSize > 8) {
      BitWriter_Write(&bitWriter, (tableByteSize << 8) | (tableByteSize >> 8),
                      16);
    } else {
      BitWriter_Write(&bitWriter, tableByteSize, 8);
    }
    for (u16 i = 1; i < tableSize; ++i) {
      uint16_t treeFlags = table[t].at_0x04[i] & 0xC000;
      uint16_t encoded = (treeFlags >> (16 - bitSize)) | table[t].at_0x04[i];
      BitWriter_Write(&bitWriter, encoded, bitSize);
    }
    BitWriter_Flush(&bitWriter);
    // Pad to 4 bytes
    while (bitWriter.offset % 4 != 0) {
      BitWriter_Write(&bitWriter, 0, 8);
    }
    dstp += bitWriter.offset;
  }

  struct at0 *refs[2] = {table[0].at_0x00, table[1].at_0x00};

  // Write LZ data with huffman
  for (u32 i = 0; i < lz_length;) {
    byte_t flags = tmp_dstp[i++];
    for (u8 j = 0; j < 8; ++j) {
      if (flags & 0x80) {
        u16 lzref_length = tmp_dstp[i++];
        u16 lzref_offset = tmp_dstp[i] | (tmp_dstp[i + 1] << 8);
        i += 2;

        // This bit in the value is used instead of the flags bit to signify an
        // encoded reference
        lzref_length |= 0x100;

        // Write the length using the first huffman table
        BitWriter_Write(&bitWriter, refs[0][lzref_length].ref,
                        refs[0][lzref_length].refSize);
        // Write number of bits needed to encode the offset with the second
        // huffman table
        u16 offsetBits = CountBitsNeededToEncode(lzref_offset);
        BitWriter_Write(&bitWriter, refs[1][offsetBits].ref,
                        refs[1][offsetBits].refSize);
        // Then write the offset itself with that number of bits
        if (offsetBits > 1)
          BitWriter_Write(&bitWriter,
                          lzref_offset & ((1 << (offsetBits - 1)) - 1),
                          offsetBits - 1);
      } else {
        byte_t a = tmp_dstp[i++];
        BitWriter_Write(&bitWriter, refs[0][a].ref, refs[0][a].refSize);
      }
      flags <<= 1;
    }
  }

  length += bitWriter.offset;

  return length;
}