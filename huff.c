#include "huff.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef nsmbw_compress_huff_size_t huff_size_t;
static const huff_size_t huff_invalid_node = nsmbw_compress_huff_invalid_node;

bool nsmbw_compress_huff_verify_table(const uint16_t *table,
                                      uint8_t huff_bit_size,
                                      uint8_t flags_bit_offset) {
  const uint16_t *const table_start = table;
  const uint16_t table_size = *table++;

  if (table_size > 1 << (huff_bit_size + 1)) {
    return false;
  }
  const uint16_t *const end = table + table_size - 1;

  uint8_t leaf_flags[sizeof(uint16_t) * 0x40] = {};

  const uint16_t left_leaf_flag = 1 << (flags_bit_offset - 1);
  const uint16_t right_leaf_flag = 1 << (flags_bit_offset - 2);
  const uint16_t leaf_flags_mask = left_leaf_flag | right_leaf_flag;
  const uint16_t sym_mask = right_leaf_flag - 1;

  for (uint16_t i = 1; table < end; i++, table++) {
    if (leaf_flags[i / 8] & (1 << (i % 8))) {
      continue;
    }

    if (*table == 0 && i >= table_size - 4) {
      continue;
    }

    uint32_t tree_size = ((*table & sym_mask) + 1) << 1;
    const uint16_t *tree_end =
        (table - ((table - table_start) & 1 ? 1 : 0)) + tree_size;

    if (tree_end >= end) {
      return false;
    }

    if (*table & left_leaf_flag) {
      uint32_t value = (i & ~1) + tree_size;
      leaf_flags[value / 8] |= 1 << (value % 8);
    }

    if (*table & right_leaf_flag) {
      uint32_t value = (i & ~1) + tree_size + 1;
      leaf_flags[value / 8] |= 1 << (value % 8);
    }
  }

  return true;
}

static bool huff_verify_table_u8(const uint8_t *table, uint8_t huff_bit_size) {
  uint16_t table_size = *table;
  uint16_t table_u16[1 << 9];
  if (table_size >= 1 << huff_bit_size) {
    nsmbw_compress_print_error(
        "Huffman table exceeds maximum size for bit size %d: (%d > %d)",
        huff_bit_size, table_size, 1 << huff_bit_size);
    return false;
  }
  table_u16[0] = table_size = (table_size + 1) << 1;
  for (uint16_t i = 1; i < table_size; ++i) {
    table_u16[i] = table[i];
  }

  if (!nsmbw_compress_huff_verify_table(table_u16, huff_bit_size, 8)) {
    nsmbw_compress_print_error("Invalid Huffman table in input data");
    return false;
  }
  return true;
}

bool nsmbw_compress_huff_decode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  (void)params;

  const uint8_t *const src_end = src + src_length;
  const uint8_t *const dst_start = dst;

  if (src + sizeof(uint32_t) + sizeof(uint8_t) > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed Huffman file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const enum nsmbw_compress_cx_type type = header & nsmbw_compress_cx_type_mask;
  if (type != nsmbw_compress_cx_type_huff) {
    nsmbw_compress_print_error("Input data is not a CX-Huffman file");
    return false;
  }
  const uint8_t huff_bit_size = header & 0xF;
  uint32_t read_size = header >> 8;

  if (huff_bit_size != 4 && huff_bit_size != 8) {
    nsmbw_compress_print_error(
        "Invalid Huffman bit size in input data: %d (expected 4 or 8)",
        huff_bit_size);
    return false;
  }

  src += sizeof(uint32_t);

  if (read_size == 0) {
    if (src + sizeof(uint32_t) + sizeof(uint8_t) > src_end) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid compressed Huffman file");
      return false;
    }
    read_size = ncutil_read_le_u32(src, 0);

    src += sizeof(uint32_t);
  }

  assert(read_size <= *dst_length);
  if (read_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in Huffman file");
    return false;
  }

  const uint32_t size = read_size;
  const uint8_t *const dst_end = dst + size;
  const uint8_t *const table = src;
  const uint16_t table_size = (*src + 1) << 1;

  if (src + table_size > src_end) {
    nsmbw_compress_print_error(
        "Huffman table is too large to fit inside the input data");
    return false;
  }

  if (!huff_verify_table_u8(table, huff_bit_size)) {
    return false;
  }

  src += table_size;

  const uint8_t *table_cur = table + 1;
  uint32_t decoded = 0;
  int word_it = 0;
  // 32 / huff_bit_size
  const int word_size = (huff_bit_size & 4) + 4;

  while (dst < dst_end) {
    if (src + sizeof(uint32_t) > src_end) {
      nsmbw_compress_print_error(
          "Attempt to read off the end of the input Huffman data");
      return false;
    }

    uint32_t value = ncutil_read_le_u32(src, 0);
    src += sizeof(uint32_t);

    for (int i = 32; i > 0; i--, value <<= 1) {
      const int branch = value >> 31;
      const int sym = *table_cur << branch;

      static const uint16_t left_leaf_flag = 1 << (8 - 1);
      static const uint16_t right_leaf_flag = 1 << (8 - 2);
      static const uint16_t leaf_flags_mask = left_leaf_flag | right_leaf_flag;

      const uint8_t *table_align =
          table_cur - ((table_cur - table) & 1 ? 1 : 0);
      table_cur =
          table_align + (((*table_cur & ~leaf_flags_mask) + 1) << 1) + branch;

      if (!(sym & left_leaf_flag)) {
        continue;
      }
      decoded >>= huff_bit_size;
      decoded |= (uint32_t)*table_cur << (32 - huff_bit_size);
      table_cur = table + 1;
      word_it++;

      if (dst + (word_it * huff_bit_size >> 3) >= dst_end) {
        decoded >>= huff_bit_size * (word_size - word_it);
      } else if (word_it != word_size) {
        continue;
      }

      *dst++ = decoded & 0xFF;
      if (dst < dst_end) {
        *dst++ = (decoded >> 8) & 0xFF;
      }
      if (dst < dst_end) {
        *dst++ = (decoded >> 16) & 0xFF;
      }
      if (dst < dst_end) {
        *dst++ = (decoded >> 24) & 0xFF;
      }
      if (dst >= dst_end) {
        break;
      }
      word_it = 0;
    }
  }

  assert(dst == dst_end);

  if (ncutil_align_up_ptr(0x20, src) < (const void *)src_end) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        (size_t)(src_end - src));
  }

  *dst_length = size;
  return true;
}

size_t nsmbw_compress_huff_get_work_size(uint8_t huff_bit_size) {
  const size_t huff_sym_size = nsmbw_compress_huff_sym_size(huff_bit_size);

  const size_t nodes_size =
      sizeof(struct nsmbw_compress_huff_node) * huff_sym_size * 2;
  const size_t encoded_nodes_size = sizeof(huff_size_t) * huff_sym_size * 2;
  const size_t tree_size =
      sizeof(struct nsmbw_compress_huff_tree) * huff_sym_size;
  return nodes_size + encoded_nodes_size + tree_size;
}

void nsmbw_compress_huff_init_table(struct nsmbw_compress_huff_table *table,
                                    void *work, uint8_t huff_bit_size) {
  const size_t huff_sym_size = nsmbw_compress_huff_sym_size(huff_bit_size);

  const size_t nodes_size =
      sizeof(struct nsmbw_compress_huff_node) * huff_sym_size * 2;
  const size_t encoded_nodes_size = sizeof(huff_size_t) * huff_sym_size * 2;
  const size_t tree_size =
      sizeof(struct nsmbw_compress_huff_tree) * huff_sym_size;

  table->nodes = (struct nsmbw_compress_huff_node *)work;
  table->encoded_nodes = (huff_size_t *)((uint8_t *)work + nodes_size);
  table->tree =
      (struct nsmbw_compress_huff_tree *)((uint8_t *)work + nodes_size +
                                          encoded_nodes_size);
  table->tree_count = 1;

  struct nsmbw_compress_huff_node *const nodes = table->nodes;
  static const struct nsmbw_compress_huff_node initial_node = {
      .left = huff_invalid_node,
      .right = huff_invalid_node,
  };

  for (uint32_t i = 0; i < huff_sym_size * 2; i++) {
    nodes[i] = initial_node;
    nodes[i].index = i;
  }

  static const struct nsmbw_compress_huff_tree initial_tree = {
      .is_left_leaf = true,
      .is_right_leaf = true,
  };

  uint16_t *const encoded_nodes = table->encoded_nodes;
  struct nsmbw_compress_huff_tree *const tree = table->tree;

  for (uint32_t i = 0; i < huff_sym_size; i++) {
    encoded_nodes[i * 2 + 0] = 0;
    encoded_nodes[i * 2 + 1] = 0;

    tree[i] = initial_tree;
  }
}

void nsmbw_compress_huff_count_data(struct nsmbw_compress_huff_node *nodes,
                                    uint8_t const *data, uint32_t size,
                                    uint8_t huff_bit_size) {
  if (huff_bit_size == 8) {
    for (uint32_t i = 0; i < size; i++) {
      nodes[data[i]].count++;
    }
    return;
  }

  // huffBitSize == 4
  for (uint32_t i = 0; i < size; i++) {
    uint8_t c = (data[i] & 0xF0) >> 4;
    nodes[c].count++;

    c = (data[i] & 0x0F) >> 0;
    nodes[c].count++;
  }
}

void nsmbw_compress_huff_count_data_u16(struct nsmbw_compress_huff_node *nodes,
                                        uint16_t const *data, uint32_t size,
                                        uint8_t huff_bit_size) {
  for (uint32_t i = 0; i < size; i++) {
    nodes[data[i]].count++;
  }
}

static void
huff_add_parent_depth_to_table(struct nsmbw_compress_huff_node *nodes,
                               huff_size_t left_index,
                               huff_size_t right_index) {
  nodes[left_index].encoded_ref_bit_size++;
  nodes[right_index].encoded_ref_bit_size++;

  if (nodes[left_index].depth) {
    huff_add_parent_depth_to_table(nodes, nodes[left_index].left,
                                   nodes[left_index].right);
  }

  if (nodes[right_index].depth) {
    huff_add_parent_depth_to_table(nodes, nodes[right_index].left,
                                   nodes[right_index].right);
  }
}

static void huff_add_code_to_table(struct nsmbw_compress_huff_node *nodes,
                                   huff_size_t index,
                                   uint32_t current_encoded_ref) {
  assert(index != huff_invalid_node);
  nodes[index].encoded_ref =
      current_encoded_ref << 1 | nodes[index].is_right_of_parent;

  if (nodes[index].depth) {
    huff_add_code_to_table(nodes, nodes[index].left, nodes[index].encoded_ref);
    huff_add_code_to_table(nodes, nodes[index].right, nodes[index].encoded_ref);
  }
}

static huff_size_t
huff_add_count_hword_to_table(struct nsmbw_compress_huff_node *nodes,
                              huff_size_t index) {
  huff_size_t a;
  huff_size_t b;

  switch (nodes[index].depth) {
  case 0:
    return 0;

  case 1:
    a = b = 0;
    break;

  default:
    a = huff_add_count_hword_to_table(nodes, nodes[index].left);
    b = huff_add_count_hword_to_table(nodes, nodes[index].right);
    break;
  }

  return nodes[index].count_hword = a + b + 1;
}

huff_size_t
nsmbw_compress_huff_construct_tree(struct nsmbw_compress_huff_node *nodes,
                                   const uint32_t huff_sym_size) {
  huff_size_t branch;
  for (branch = huff_sym_size;; branch++) {
    huff_size_t left = huff_invalid_node, right = huff_invalid_node;

    for (huff_size_t i = 0; i < branch; ++i) {
      if (nodes[i].count && !nodes[i].parent) {
        if (left == huff_invalid_node || nodes[i].count < nodes[left].count) {
          left = i;
        }
      }
    }

    for (huff_size_t i = 0; i < branch; ++i) {
      if (nodes[i].count && !nodes[i].parent && i != left) {
        if (right == huff_invalid_node || nodes[i].count < nodes[right].count) {
          right = i;
        }
      }
    }

    if (right == huff_invalid_node) {
      if (left == huff_invalid_node) {
        left = 0;
        nodes[left].count = 1;
      }
      if (branch == huff_sym_size) {
        nodes[branch].count = nodes[left].count;
        nodes[branch].left = left;
        nodes[branch].right = left;
        nodes[branch].depth = 1;

        nodes[left].parent = branch;
        nodes[left].is_right_of_parent = false;
        nodes[left].encoded_ref_bit_size = 1;
      } else {
        branch--;
      }
      break;
    }

    nodes[branch].count = nodes[left].count + nodes[right].count;
    nodes[branch].left = left;
    nodes[branch].right = right;

    if (nodes[left].depth > nodes[right].depth) {
      nodes[branch].depth = nodes[left].depth + 1;
    } else {
      nodes[branch].depth = nodes[right].depth + 1;
    }

    nodes[left].parent = nodes[right].parent = branch;
    nodes[left].is_right_of_parent = false;
    nodes[right].is_right_of_parent = true;

    huff_add_parent_depth_to_table(nodes, left, right);
  }

  huff_add_code_to_table(nodes, branch, 0);
  huff_add_count_hword_to_table(nodes, branch);

  return branch;
}

static void huff_set_one_node_offset(struct nsmbw_compress_huff_table *table,
                                     huff_size_t index, bool is_right) {

  struct nsmbw_compress_huff_node *const nodes = table->nodes;
  huff_size_t *const encoded_nodes = table->encoded_nodes;
  struct nsmbw_compress_huff_tree *const tree = table->tree;
  huff_size_t huff_tree_count = table->tree_count;

  huff_size_t node;
  if (is_right) {
    node = tree[index].right;
    tree[index].is_right_leaf = 0;
  } else {
    node = tree[index].left;
    tree[index].is_left_leaf = 0;
  }

  static const huff_size_t left_leaf_flag = 0x8000;
  static const huff_size_t right_leaf_flag = 0x4000;
  huff_size_t encoded_value = 0;

  if (!nodes[nodes[node].left].depth) {
    encoded_value |= left_leaf_flag;

    encoded_nodes[huff_tree_count * 2 + 0] = nodes[node].left;
    tree[huff_tree_count].left = nodes[node].left;
    tree[huff_tree_count].is_left_leaf = false;
  } else {
    tree[huff_tree_count].left = nodes[node].left;
  }

  if (!nodes[nodes[node].right].depth) {
    encoded_value |= right_leaf_flag;

    encoded_nodes[huff_tree_count * 2 + 1] = nodes[node].right;
    tree[huff_tree_count].right = nodes[node].right;
    tree[huff_tree_count].is_right_leaf = false;
  } else {
    tree[huff_tree_count].right = nodes[node].right;
  }

  encoded_value |= huff_tree_count - index - 1;

  encoded_nodes[index * 2 + is_right] = encoded_value;
  table->tree_count++;
}

static void huff_make_subset_huff_tree(struct nsmbw_compress_huff_table *table,
                                       huff_size_t index, bool is_right) {
  huff_size_t i = table->tree_count;

  huff_set_one_node_offset(table, index, is_right);

  if (is_right) {
    table->tree[index].is_right_leaf = false;
  } else {
    table->tree[index].is_left_leaf = false;
  }

  for (; i < table->tree_count; i++) {
    if (table->tree[i].is_left_leaf) {
      huff_set_one_node_offset(table, i, 0);
      table->tree[i].is_left_leaf = false;
    }

    if (table->tree[i].is_right_leaf) {
      huff_set_one_node_offset(table, i, 1);
      table->tree[i].is_right_leaf = false;
    }
  }
}

static bool
huff_remaining_node_can_set_offset(struct nsmbw_compress_huff_table *table,
                                   huff_size_t index, uint8_t huff_bit_size) {
  const int max_tree_size = 1 << (huff_bit_size - 2);
  short encode_offset = max_tree_size - index;

  for (huff_size_t i = 0; i < table->tree_count; ++i) {
    if (table->tree[i].is_left_leaf) {
      if (table->tree_count - i > encode_offset--) {
        return false;
      }
    }

    if (table->tree[i].is_right_leaf) {
      if (table->tree_count - i > encode_offset--) {
        return false;
      }
    }
  }

  return true;
}

void nsmbw_compress_huff_make_huff_tree(struct nsmbw_compress_huff_table *table,
                                        uint16_t construct_count,
                                        uint8_t huff_bit_size) {
  static const huff_size_t first_index = 0;

  table->tree_count = 1;
  table->tree->is_left_leaf = false;
  table->tree->right = construct_count;
  const uint32_t max_tree_size = 1 << (huff_bit_size - 2);

loop:
  while (true) {
    huff_size_t leaf_count = 0;
    for (huff_size_t i = 0; i < table->tree_count; i++) {
      if (table->tree[i].is_left_leaf) {
        leaf_count++;
      }

      if (table->tree[i].is_right_leaf) {
        leaf_count++;
      }
    }

    huff_size_t branch_node = huff_invalid_node;
    bool is_right;

    for (huff_size_t i = 0; i < table->tree_count; i++) {
      huff_size_t encoded_offset = table->tree_count - i;

      if (table->tree[i].is_left_leaf) {
        huff_size_t count_hword = table->nodes[table->tree[i].left].count_hword;

        if (count_hword + leaf_count <= max_tree_size &&
            huff_remaining_node_can_set_offset(table, count_hword,
                                               huff_bit_size)) {
          if (count_hword != huff_invalid_node ||
              encoded_offset > first_index) {
            branch_node = i;
            is_right = false;
          }
        }
      }

      if (table->tree[i].is_right_leaf) {
        huff_size_t count_hword =
            table->nodes[table->tree[i].right].count_hword;

        if (count_hword + leaf_count <= max_tree_size &&
            huff_remaining_node_can_set_offset(table, count_hword,
                                               huff_bit_size)) {
          if (count_hword != huff_invalid_node ||
              encoded_offset > first_index) {
            branch_node = i;
            is_right = true;
          }
        }
      }
    }

    if (branch_node == huff_invalid_node) {
      break;
    }
    huff_make_subset_huff_tree(table, branch_node, is_right);
  }

  for (huff_size_t i = 0; i < table->tree_count; i++) {
    bool is_right = false;

    huff_size_t left_count = 0;
    if (table->tree[i].is_left_leaf) {
      left_count = table->nodes[table->tree[i].left].count_hword;
    }

    if (table->tree[i].is_right_leaf &&
        table->nodes[table->tree[i].right].count_hword > left_count) {
      is_right = true;
    }

    if (left_count > 0 || is_right) {
      huff_set_one_node_offset(table, i, is_right);
      goto loop;
    }
  }
}

uint32_t nsmbw_compress_huff_convert_data(
    struct nsmbw_compress_huff_node *nodes, uint8_t const *src, uint8_t *dst,
    uint32_t size, uint32_t max_length, uint8_t huff_bit_size) {
  uint32_t write_value = 0;
  uint32_t bit_offset = 0;
  uint32_t length = 0;

  for (uint32_t i = 0; i < size; ++i) {
    uint8_t small_value;
    uint8_t value = src[i];

    if (huff_bit_size == 8) {
      write_value = write_value << nodes[value].encoded_ref_bit_size |
                    nodes[value].encoded_ref;
      bit_offset += nodes[value].encoded_ref_bit_size;

      if (length + bit_offset / 8 >= max_length) {
        return 0;
      }

      for (uint32_t j = 0; j < bit_offset / 8; j++) {
        dst[length++] = write_value >> (bit_offset - ((j + 1) << 3));
      }

      bit_offset %= 8;
      continue;
    }

    for (uint32_t j = 0; j < 8 / 4; ++j) {
      if (j != 0) {
        small_value = value >> 4;
      } else {
        small_value = value & 0x0F;
      }

      write_value = (write_value << nodes[small_value].encoded_ref_bit_size) |
                    nodes[small_value].encoded_ref;
      bit_offset += nodes[small_value].encoded_ref_bit_size;

      if (length + bit_offset / 8 >= max_length) {
        return 0;
      }

      for (uint32_t k = 0; k < bit_offset / 8; ++k) {
        dst[length++] = write_value >> (bit_offset - (k + 1) * 8);
      }

      bit_offset %= 8;
    }
  }

  if (bit_offset) {
    if (length + 1 >= max_length) {
      return 0;
    } else {
      dst[length++] = write_value << (8 - bit_offset);
    }
  }

  while (length % 4 != 0) {
    if (length + 1 >= max_length) {
      return 0;
    }
    dst[length++] = 0;
  }

  for (uint32_t i = 0; i < length; i += sizeof(uint32_t)) {
    ncutil_write_le_u32(dst, i, ncutil_read_be_u32(dst, i));
  }

  return length;
}

static bool huff_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                        size_t *dst_length, uint8_t huff_bit_size,
                        bool print_error) {
  void *work_buffer = malloc(nsmbw_compress_huff_get_work_size(huff_bit_size));
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for Huffman compression work buffer: %s",
        strerror(errno));
    return false;
  }

  const uint16_t huff_sym_size = nsmbw_compress_huff_sym_size(huff_bit_size);
  const size_t max_dst_size = *dst_length;

  struct nsmbw_compress_huff_table table;

  nsmbw_compress_huff_init_table(&table, work_buffer, huff_bit_size);
  nsmbw_compress_huff_count_data(table.nodes, src, src_length, huff_bit_size);
  uint16_t construct_size =
      nsmbw_compress_huff_construct_tree(table.nodes, huff_sym_size);
  nsmbw_compress_huff_make_huff_tree(&table, construct_size, huff_bit_size);

  table.encoded_nodes[0] = --table.tree_count;

  uint32_t length = 0;
  if (src_length < 0x1000000) {
    ncutil_write_le_u32(
        dst, 0, src_length << 8 | nsmbw_compress_cx_type_huff | huff_bit_size);
    length += sizeof(uint32_t);
  } else {
    ncutil_write_le_u32(dst, 0, nsmbw_compress_cx_type_huff | huff_bit_size);
    ncutil_write_le_u32(dst, sizeof(uint32_t), src_length);
    length += sizeof(uint32_t) + sizeof(uint32_t);
  }

  uint32_t tree_length_pos = length;

  if (length + ((table.tree_count + 1) << 1) >= max_dst_size) {
    if (print_error) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
    }
    free(work_buffer);
    return false;
  }

  for (uint32_t i = 0; i < (table.tree_count + 1) << 1; ++i) {
    uint8_t c = (table.encoded_nodes[i] & 0xC000) >> 8;
    dst[length++] = table.encoded_nodes[i] | c;
  }

  while (length % 4 != 0) {
    if (length % 2 != 0) {
      table.tree_count++;
      dst[tree_length_pos]++;
    }

    dst[length++] = 0;
  }

  uint32_t converted_size = nsmbw_compress_huff_convert_data(
      table.nodes, src, dst + length, src_length, max_dst_size - length,
      huff_bit_size);
  if (!converted_size) {
    if (print_error) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
    }
    free(work_buffer);
    return false;
  }

  length += converted_size;

  free(work_buffer);

  *dst_length = length;
  return *dst_length != 0;
}

bool nsmbw_compress_huff_encode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  const uint8_t huff_bit_size = params->huff_bit_size;
  if (huff_bit_size != 4 && huff_bit_size != 8 && huff_bit_size != 0) {
    nsmbw_compress_print_error("Invalid Huffman bit size for compression: %d "
                               "(expected 4, 8, or 0 (for auto))",
                               huff_bit_size);
    return false;
  }

  if (huff_bit_size != 0) {
    return huff_encode(src, dst, src_length, dst_length, huff_bit_size, true);
  }

  // Auto mode: Run both and pick the smaller one
  size_t dst_length_8 = *dst_length;
  if (!huff_encode(src, dst, src_length, &dst_length_8, 8, true)) {
    return false;
  }

  size_t dst_length_4 = *dst_length;
  void *dst_buffer_4 = malloc(dst_length_4);
  if (dst_buffer_4 == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for Huffman compression output buffer: %s",
        strerror(errno));
    return false;
  }

  bool result =
      huff_encode(src, dst_buffer_4, src_length, &dst_length_4, 4, true);

  if (!result || dst_length_8 <= dst_length_4) {
    free(dst_buffer_4);
    nsmbw_compress_print_verbose(
        "Selected Huffman bit size 8 (compressed size: %zu bytes)",
        dst_length_8);
    *dst_length = dst_length_8;
    return true;
  }

  nsmbw_compress_print_verbose(
      "Selected Huffman bit size 4 (compressed size: %zu bytes)", dst_length_4);
  *dst_length = dst_length_4;
  memcpy(dst, dst_buffer_4, dst_length_4);
  free(dst_buffer_4);
  return true;
}
