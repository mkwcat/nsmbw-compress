#include "CXCompression.h"

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

struct LZTable {
  // The current offset in the skip table
  uint16_t window_pos;
  // Number of entries populated in the skip table
  uint16_t window_size;
  short *skip_table_next;
  short *skip_table_head;
  short *skip_table_tail;
  uint32_t max_window_size;  // Added
  short *reverse_skip_table; // Added
};

struct at0 {
  int unsigned count;
  uint16_t at_0x04;
  short at_0x06;
  short at_0x08;
  short at_0x0a;
  uint16_t refSize;
  uint16_t at_0x0e;
  int ref;
  uint8_t at_0x14;
  uint16_t at_0x16;
}; // size 0x18

_Static_assert(sizeof(struct at0) == 0x18, "at0 size mismatch");

struct at8 {
  uint8_t at_0x00;
  uint8_t at_0x01;
  uint16_t at_0x02;
  uint16_t at_0x04;
};

_Static_assert(sizeof(struct at8) == 0x06, "at8 size mismatch");

struct HuffTable {
  struct at0 *at_0x00;
  uint16_t *at_0x04;
  struct at8 *at_0x08;
  uint16_t treeEntryCount;
};

/*******************************************************************************
 * local function declarations
 */

static int SearchLZ(struct LZTable const *table, uint8_t const *data, uint32_t,
                    uint16_t *, size_t);
static void LZInitTable(struct LZTable *table, short *work,
                        uint32_t windowSize);
static void SlideByte(struct LZTable *table, uint8_t const *data);
static void LZSlide(struct LZTable *table, uint8_t const *data,
                    uint32_t length);

static void HuffInitTable(struct HuffTable *, void *work, uint16_t);

static void HuffCountData(struct at0 *, uint8_t const *data, uint32_t size,
                          uint8_t huffBitSize);
static uint16_t HuffConstructTree(struct at0 *, unsigned);
static void HuffAddParentDepthToTable(struct at0 *, uint16_t, uint16_t);
static void HuffAddCodeToTable(struct at0 *, uint16_t, int);
static uint16_t HuffAddCountHWordToTable(struct at0 *, uint16_t);
static void HuffMakeHuffTree(struct HuffTable *, uint16_t, uint8_t huffBitSize);
static void HuffMakeSubsetHuffTree(struct HuffTable *, uint16_t, uint8_t);
static uint8_t HuffRemainingNodeCanSetOffset(struct HuffTable *, short,
                                             int huffBitSize);
static void HuffSetOneNodeOffset(struct HuffTable *, uint16_t, uint8_t);
static uint32_t HuffConvertData(struct at0 *, uint8_t const *srcp,
                                uint8_t *dstp, uint32_t size,
                                uint32_t maxLength, uint8_t huffBitSize);

/*******************************************************************************
 * functions
 */

uint32_t CXCompressLZImpl(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                          void *work, bool param_5) {
  uint32_t length;
  uint32_t a;
  uint8_t b;
  uint16_t c;
  uint8_t *d;

  uint8_t i;

  uint32_t f;
  int long g; // or unsigned

  g = param_5 ? 0x10110 : 0x12;

  assert(((size_t)srcp & 0x1) == 0);
  assert(work != NULL);
  assert(size > 4);

  if (size < 0x1000000) {
    *(uint32_t *)dstp = CXiConvertEndian32_(
        size << 8 | CX_COMPRESSION_TYPE_LEMPEL_ZIV | BOOLIFY_TERNARY(param_5));

    dstp += sizeof(uint32_t);

    length = sizeof(uint32_t);
  } else {
    *(uint32_t *)dstp = CXiConvertEndian32_(CX_COMPRESSION_TYPE_LEMPEL_ZIV |
                                            BOOLIFY_TERNARY(param_5));
    dstp += sizeof(uint32_t);

    *(uint32_t *)dstp = CXiConvertEndian32_(size);
    dstp += sizeof(uint32_t);

    length = sizeof(uint32_t) + sizeof(uint32_t);
  }

  f = size * CX_COMPRESS_DST_SCALE;

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

        uint32_t h;

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

static int SearchLZ(struct LZTable const *table, uint8_t const *data,
                    uint32_t data_size, uint16_t *param_4,
                    size_t max_match_size) {
  uint8_t const *data_byte;
  uint8_t const *match_data;
  unsigned best_match_offset;
  unsigned window_pos;
  unsigned match_size;
  unsigned window_size;
  unsigned best_match_size;

  best_match_size = 2;
  short *st_next = table->skip_table_next;
  if (table->reverse_skip_table) {
    // Reverse the skip table for searching from the tail. This is more optimal
    // with LH compression due to the smaller reference bit size. This also
    // appears to be what Nintendo did
    st_next = table->reverse_skip_table;
  }
  window_pos = table->window_pos;
  window_size = table->window_size;

  const unsigned minimum_match_size = 3;

  if (data_size < minimum_match_size) {
    return 0;
  }

  int it;
  if (table->reverse_skip_table) {
    it = table->skip_table_tail[*data];
  } else {
    it = table->skip_table_head[*data];
  }

  max_match_size = MIN(max_match_size, data_size);

  for (; it != -1; it = st_next[it]) {
    if (it < window_pos) {
      match_data = data - window_pos + it;
    } else {
      match_data = data - window_size - window_pos + it;
    }

    if (match_data[1] != data[1] || match_data[2] != data[2]) {
      continue;
    }

    if (data - match_data < 2) {
      if (table->reverse_skip_table) {
        continue;
      }
      break;
    }

    for (match_size = minimum_match_size;
         match_size < max_match_size &&
         data[match_size] == match_data[match_size];
         match_size++) {
    }

    if (match_size > best_match_size) {
      best_match_size = match_size;
      best_match_offset = data - match_data;

      if (best_match_size == max_match_size) {
        break;
      }
    }
  }

  if (best_match_size < minimum_match_size) {
    return 0;
  }

  *param_4 = best_match_offset;
  return best_match_size;
}

static void LZInitTable(struct LZTable *table, short *work,
                        uint32_t windowSize) {
  table->skip_table_next = work;
  table->skip_table_head = work + windowSize;
  table->skip_table_tail = work + windowSize + 0x100;

  uint16_t i;
  for (i = 0; i < 0x100; ++i) {
    table->skip_table_head[i] = -1;
    table->skip_table_tail[i] = -1;
  }

  table->window_pos = 0;
  table->window_size = 0;
  table->max_window_size = windowSize;
  table->reverse_skip_table = NULL;
}

static void SlideByte(struct LZTable *restrict table, uint8_t const *data) {
  uint8_t value;
  uint16_t it;
  short *restrict st_head = table->skip_table_head;
  short *restrict st_next = table->skip_table_next;
  short *restrict st_tail = table->skip_table_tail;
  short *restrict st_reverse = table->reverse_skip_table;

  value = *data;

  uint16_t const window_pos = table->window_pos;
  uint16_t const window_size = table->window_size;

  const int max_window_size = table->max_window_size;

  if (window_size == max_window_size) {
    uint8_t remove_value = data[-max_window_size];
    if (st_reverse && st_head[remove_value] != -1 &&
        st_next[st_head[remove_value]] != -1) {
      st_reverse[st_next[st_head[remove_value]]] = -1;
    }
    if ((st_head[remove_value] = st_next[st_head[remove_value]]) == -1) {
      st_tail[remove_value] = -1;
    }

    it = window_pos;
  } else {
    it = window_size;
  }

  int tail_it = st_tail[value];
  if (tail_it == -1) {
    st_head[value] = it;
  } else {
    st_next[tail_it] = it;
  }

  st_tail[value] = it;
  st_next[it] = -1;
  if (st_reverse) {
    st_reverse[it] = tail_it;
  }

  if (window_size == max_window_size) {
    table->window_pos = (window_pos + 1) % max_window_size;
  } else {
    table->window_size++;
  }
}

static void LZSlide(struct LZTable *table, uint8_t const *data,
                    uint32_t length) {
  for (uint32_t i = 0; i < length; ++i) {
    SlideByte(table, data++);
  }
}

uint32_t CXCompressRL(uint8_t const *srcp, uint32_t size, uint8_t *dstp) {
  unsigned a;
  unsigned b;
  int c;
  uint8_t d;
  uint8_t e;
  uint8_t f;
  uint8_t const *g;

  assert(srcp != NULL);
  assert(dstp != NULL);
  assert(size > 4);

  if (size < 0x1000000) {
    IN_BUFFER_AT(uint32_t, dstp, 0) =
        CXiConvertEndian32_(size << 8 | CX_COMPRESSION_TYPE_RUN_LENGTH);

    b = sizeof(uint32_t);
  } else {
    IN_BUFFER_AT(uint32_t, dstp, 0) =
        CXiConvertEndian32_(CX_COMPRESSION_TYPE_RUN_LENGTH);
    IN_BUFFER_AT(uint32_t, dstp, 4) = CXiConvertEndian32_(size);

    b = sizeof(uint32_t) + sizeof(uint32_t);
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
      if (b + f + 1 >= size * CX_COMPRESS_DST_SCALE)
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

      if (b + 2 >= size * CX_COMPRESS_DST_SCALE)
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

uint32_t CXCompressHuffman(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                           uint8_t huffBitSize, void *work) {
  int i;

  uint16_t a;
  uint16_t b = 1 << huffBitSize;

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

  uint32_t headerLength;
  if (size < 0x1000000) {
    IN_BUFFER_AT(uint32_t, dstp, 0) = CXiConvertEndian32_(
        size << 8 | CX_COMPRESSION_TYPE_HUFFMAN | huffBitSize);

    headerLength = sizeof(uint32_t);
  } else {
    IN_BUFFER_AT(uint32_t, dstp, 0) =
        CXiConvertEndian32_(CX_COMPRESSION_TYPE_HUFFMAN | huffBitSize);
    IN_BUFFER_AT(uint32_t, dstp, 4) = CXiConvertEndian32_(size);

    headerLength = sizeof(uint32_t) + sizeof(uint32_t);
  }

  uint32_t length = headerLength;

  if (length + ((table.treeEntryCount + 1) << 1) >=
      size * CX_COMPRESS_DST_SCALE)
    return 0;

  for (i = 0; i < (uint16_t)((table.treeEntryCount + 1) << 1); ++i) {
    uint8_t c = (table.at_0x04[i] & 0xC000) >> 8;
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
    int c = HuffConvertData(table.at_0x00, srcp, dstp + length, size,
                            size * CX_COMPRESS_DST_SCALE - length, huffBitSize);
    if (!c)
      return 0;

    length += c;
  }

  return length;
}

static void HuffInitTable(struct HuffTable *param_1, void *work,
                          uint16_t huffBitMax) {
  uint32_t i;

  size_t at0Size = sizeof(struct at0) * huffBitMax * 2;
  size_t at4Size = sizeof(uint16_t) * huffBitMax * 2;
  uint8_t *workPtr = (uint8_t *)work;

  param_1->at_0x00 = (struct at0 *)(workPtr + 0);
  param_1->at_0x04 = (uint16_t *)(workPtr + at0Size);
  param_1->at_0x08 = (struct at8 *)(workPtr + at0Size + at4Size);
  param_1->treeEntryCount = 1;

  { // random block
    struct at0 *a = param_1->at_0x00;
    static const struct at0 initial = {.at_0x08 = -1, .at_0x0a = -1};

    for (i = 0; i < huffBitMax * 2; ++i) {
      a[i] = initial;
      a[i].at_0x04 = i;
    }
  }

  { // another random block
    static const struct at8 initial = {.at_0x00 = 1, .at_0x01 = 1};

    uint16_t *b = param_1->at_0x04;
    struct at8 *c = param_1->at_0x08;

    for (i = 0; i < huffBitMax; ++i) {
      b[i * 2 + 0] = 0;
      b[i * 2 + 1] = 0;

      c[i] = initial;
    }
  }
}

static void HuffCountData(struct at0 *param_1, uint8_t const *data,
                          uint32_t size, uint8_t huffBitSize) {
  uint32_t i;

  uint8_t a;

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

static uint16_t HuffConstructTree(struct at0 *param_1, unsigned huffBitMax) {
  int i;

  int a;
  int b;
  uint16_t c = huffBitMax;

  a = -1;
  b = -1;

  uint16_t d;
  uint16_t e ATTR_UNUSED; // ?

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
      if (a < 0) {
        a = 0;
        param_1[a].count = 1;
      }
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

static void HuffAddParentDepthToTable(struct at0 *param_1, uint16_t param_2,
                                      uint16_t param_3) {
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

static void HuffAddCodeToTable(struct at0 *param_1, uint16_t param_2,
                               int param_3) {
  assert(param_2 != 0xFFFF);
  param_1[param_2].ref = param_3 << 1 | param_1[param_2].at_0x14;

  if (param_1[param_2].at_0x0e) {
    HuffAddCodeToTable(param_1, param_1[param_2].at_0x08, param_1[param_2].ref);
    HuffAddCodeToTable(param_1, param_1[param_2].at_0x0a, param_1[param_2].ref);
  }
}

static uint16_t HuffAddCountHWordToTable(struct at0 *param_1,
                                         uint16_t param_2) {
  uint16_t a;
  uint16_t b;

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

static void HuffMakeHuffTree(struct HuffTable *param_1, uint16_t param_2,
                             uint8_t huffBitSize) {
  short i;
  short a;
  short b;
  short c;
  short d;
  short e;
  short f;
  uint16_t g;
  char h ATTR_UNUSED; // ?

  bool l;

  param_1->treeEntryCount = 1;
  d = 0;
  param_1->at_0x08->at_0x00 = 0;
  param_1->at_0x08->at_0x04 = param_2;
  const int maxTreeSize = 1 << (huffBitSize - 2);

loop:
  while (true) {
    uint16_t m = 0;

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

static void HuffMakeSubsetHuffTree(struct HuffTable *param_1, uint16_t param_2,
                                   uint8_t param_3) {
  uint16_t i = param_1->treeEntryCount;

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

static uint8_t HuffRemainingNodeCanSetOffset(struct HuffTable *param_1,
                                             short param_2, int huffBitSize) {
  uint16_t i;

  const int maxTreeSize = 1 << (huffBitSize - 2);
  short a = maxTreeSize - param_2;

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

static void HuffSetOneNodeOffset(struct HuffTable *param_1, uint16_t param_2,
                                 uint8_t param_3) {
  uint16_t a;
  uint16_t b = 0;

  struct at0 *table0 = param_1->at_0x00;
  uint16_t *table4 = param_1->at_0x04;
  struct at8 *table8 = param_1->at_0x08;
  uint16_t tablec = param_1->treeEntryCount;

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
    table8[tablec].at_0x02 = (uint8_t)table0[a].at_0x08;
    table8[tablec].at_0x00 = 0;
  } else {
    table8[tablec].at_0x02 = table0[a].at_0x08;
  }

  if (!table0[table0[a].at_0x0a].at_0x0e) {
    b |= 0x4000;

    table4[tablec * 2 + 1] = table0[a].at_0x0a;
    table8[tablec].at_0x04 = (uint8_t)table0[a].at_0x0a;
    table8[tablec].at_0x01 = 0;
  } else {
    table8[tablec].at_0x04 = table0[a].at_0x0a;
  }

  b |= (uint16_t)(tablec - param_2 - 1);

  table4[param_2 * 2 + param_3] = b;
  ++param_1->treeEntryCount;
}

static uint32_t HuffConvertData(struct at0 *param_1, uint8_t const *srcp,
                                uint8_t *dstp, uint32_t size,
                                uint32_t maxLength, uint8_t huffBitSize) {
  uint32_t i, j, k;

  unsigned a = 0;
  unsigned b = 0;
  uint32_t length = 0;

  for (i = 0; i < size; ++i) {
    uint8_t d;
    uint8_t e = srcp[i];

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
  for (i = 0; i < length / sizeof(uint32_t); ++i) {
    uint8_t tmp;

    tmp = dstp[i * sizeof(uint32_t) + 0];
    dstp[i * sizeof(uint32_t) + 0] = dstp[i * sizeof(uint32_t) + 3];
    dstp[i * sizeof(uint32_t) + 3] = tmp;

    tmp = dstp[i * sizeof(uint32_t) + 1];
    dstp[i * sizeof(uint32_t) + 1] = dstp[i * sizeof(uint32_t) + 2];
    dstp[i * sizeof(uint32_t) + 2] = tmp;
  }

  return length;
}

static int CountBitsNeededToEncode(uint32_t value) {
  int bits = 0;
  while (value) {
    value >>= 1;
    ++bits;
  }
  return bits;
}

static uint32_t LHEncodeLZ(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                           struct CXiCompressLHWork *lhWork,
                           struct HuffTable *huffTables) {
  uint32_t length;
  uint32_t a;
  uint8_t b;
  uint16_t c;
  uint8_t *d;

  uint8_t i;

  uint32_t f;
  int long g; // or unsigned

  g = 0x102;

  length = 0;

  f = size * CX_COMPRESS_DST_SCALE;

  struct LZTable table;
  LZInitTable(&table, (short *)lhWork->lzWork, CX_COMPRESS_LH_LZ_DETAIL_SIZE);
  table.reverse_skip_table = lhWork->lz_reverse_skip_table;

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
        huffTables[0].at_0x00[0x100 | (uint8_t)(a - 3)].count++;

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

  (void)size;

  return length;
}

struct BitWriter {
  uint8_t *data;
  uint32_t offset;
  uint32_t value;
  uint32_t bit;
};

static void BitWriter_Init(struct BitWriter *bitWriter, uint8_t *data) {
  bitWriter->data = data;
  bitWriter->offset = 0;
  bitWriter->value = 0;
  bitWriter->bit = 0;
}

static void BitWriter_Write(struct BitWriter *bitWriter, uint32_t value,
                            uint8_t size) {
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

uint32_t CXCompressLH(uint8_t const *srcp, uint32_t size, uint8_t *dstp,
                      uint8_t *tmp_dstp, void *work) {
  struct CXiCompressLHWork *lh_work = work;

  uint32_t length;

  if (size < 0x1000000) {
    *(uint32_t *)dstp = CXiConvertEndian32_(size << 8 | CX_COMPRESSION_TYPE_LH);

    dstp += sizeof(uint32_t);

    length = sizeof(uint32_t);
  } else {
    *(uint32_t *)dstp = CXiConvertEndian32_(CX_COMPRESSION_TYPE_LH);
    dstp += sizeof(uint32_t);

    *(uint32_t *)dstp = CXiConvertEndian32_(size);
    dstp += sizeof(uint32_t);

    length = sizeof(uint32_t) + sizeof(uint32_t);
  }

  struct HuffTable table[2];
  HuffInitTable(&table[0], lh_work->huffLWork, 1 << 9);
  HuffInitTable(&table[1], lh_work->huffRWork, 1 << 5);

  uint32_t lz_length = LHEncodeLZ(srcp, size, tmp_dstp, lh_work, table);
  if (!lz_length) {
    return 0;
  }

  uint16_t a = HuffConstructTree(table[0].at_0x00, 1 << 9);
  HuffMakeHuffTree(&table[0], a, 9);
  uint16_t b = HuffConstructTree(table[1].at_0x00, 1 << 5);
  HuffMakeHuffTree(&table[1], b, 5);

  struct BitWriter bit_writer;
  BitWriter_Init(&bit_writer, dstp);

  // Write tables
  for (int t = 0; t < 2; ++t) {
    const int bit_size = t ? 5 : 9;
    struct BitWriter header_writer = bit_writer;
    if (bit_size > 8) {
      BitWriter_Write(&bit_writer, 0, 16);
    } else {
      BitWriter_Write(&bit_writer, 0, 8);
    }
    const uint16_t table_size = table[t].treeEntryCount << 1;
    for (uint16_t i = 1; i < table_size; ++i) {
      uint16_t tree_flags = table[t].at_0x04[i] & 0xC000;
      uint16_t encoded = (tree_flags >> (16 - bit_size)) | table[t].at_0x04[i];
      BitWriter_Write(&bit_writer, encoded, bit_size);
    }
    BitWriter_Flush(&bit_writer);
    // Pad to 4 bytes
    while (bit_writer.offset % 4 != 0) {
      BitWriter_Write(&bit_writer, 0, 8);
    }
    // Write the weird way LH wants the byte count encoded
    const uint16_t table_byte_count =
        (bit_writer.offset - header_writer.offset) / 4 - 1;
    if (bit_size > 8) {
      BitWriter_Write(&header_writer,
                      (table_byte_count << 8) | (table_byte_count >> 8), 16);
    } else {
      BitWriter_Write(&header_writer, table_byte_count, 8);
    }
  }

  struct at0 *refs[2] = {table[0].at_0x00, table[1].at_0x00};

  // Write LZ data with huffman
  for (uint32_t i = 0; i < lz_length;) {
    uint8_t flags = tmp_dstp[i++];
    for (uint8_t j = 0; j < 8; ++j) {
      if (i >= lz_length) {
        break;
      }
      if (flags & 0x80) {
        uint16_t lzref_length = tmp_dstp[i++];
        uint16_t lzref_offset = tmp_dstp[i] | (tmp_dstp[i + 1] << 8);
        i += 2;

        // This bit in the value is used instead of the flags bit to signify an
        // encoded reference
        lzref_length |= 0x100;

        // Write the length using the first huffman table
        BitWriter_Write(&bit_writer, refs[0][lzref_length].ref,
                        refs[0][lzref_length].refSize);
        // Write number of bits needed to encode the offset with the second
        // huffman table
        uint16_t offset_bits = CountBitsNeededToEncode(lzref_offset);
        BitWriter_Write(&bit_writer, refs[1][offset_bits].ref,
                        refs[1][offset_bits].refSize);
        // Then write the offset itself with that number of bits
        if (offset_bits > 1) {
          BitWriter_Write(&bit_writer,
                          lzref_offset & ((1 << (offset_bits - 1)) - 1),
                          offset_bits - 1);
        }
      } else {
        uint8_t a = tmp_dstp[i++];
        BitWriter_Write(&bit_writer, refs[0][a].ref, refs[0][a].refSize);
      }
      flags <<= 1;
    }
  }

  BitWriter_Flush(&bit_writer);
  // Pad to 4 bytes
  while (bit_writer.offset % 4 != 0) {
    BitWriter_Write(&bit_writer, 0, 8);
  }

  length += bit_writer.offset;

  return length;
}