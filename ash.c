#include "huff.h"
#include "lz.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum ash_tree_kind { ash_tree_literal, ash_tree_back_ref, ash_tree_max };

enum ash_node_flag {
  ash_node_left = 1 << 14,
  ash_node_right = 1 << 15,
  ash_node_index_mask = ash_node_left - 1
};

static const int ash_tree_stack_size = 256;
static const int ash_node_9_min = 1 << 9;
static const int ash_node_12_min = 1 << 11;

struct nsmbw_compress_ash_decode_context {
  uint16_t left9[0xFFC / sizeof(uint16_t)];
  uint16_t right9[0xFFC / sizeof(uint16_t)];
  uint16_t left12[0x3FFC / sizeof(uint16_t)];
  uint16_t right12[0x3FFC / sizeof(uint16_t)];
  uint16_t stack[256];

  int stream_bit[ash_tree_max];
  int next_node_id[ash_tree_max];
  int stream_byte[ash_tree_max];
  uint32_t stream_data[ash_tree_max];
};

static int ash_get_bits_code(struct nsmbw_compress_ash_decode_context *context,
                             const uint8_t *src, int size, int tree) {
  int byte_index = context->stream_byte[tree];
  int bit_index = context->stream_bit[tree];
  uint32_t bits = context->stream_data[tree];

  int code = 0;

  // Read past end of bitstream
  if (bit_index + size > 32) {
    // Need to refresh bitstream
    uint32_t new_bits = ncutil_read_be_u32(src, byte_index);

    // Need to read part of old bits and part of new bits
    int old_pos = 32 - size;
    int new_pos = 64 - size - bit_index;
    code = bits >> old_pos | new_bits >> new_pos;

    bit_index += size - 32;

    context->stream_data[tree] = new_bits << bit_index;
    context->stream_bit[tree] = bit_index;
    context->stream_byte[tree] = byte_index + sizeof(uint32_t);
  }
  // Read to end of bitstream
  else if (bit_index + size == 32) {
    code = bits >> (32 - size);

    // Need to refresh bitstream
    context->stream_data[tree] = ncutil_read_be_u32(src, byte_index);

    context->stream_byte[tree] = byte_index + sizeof(uint32_t);
    context->stream_bit[tree] = 0;
  }
  // Read some bits
  else {
    code = bits >> (32 - size);
    context->stream_data[tree] = bits << size;
    context->stream_bit[tree] = bit_index + size;
  }

  return code;
}

static int ash_get_bit_1c(struct nsmbw_compress_ash_decode_context *context,
                          const uint8_t *src, int tree) {
  int byte_index = context->stream_byte[tree];
  int bit_index = context->stream_bit[tree];
  uint32_t bits = context->stream_data[tree];

  int code = bits >> 31;

  // Read to end of bitstream
  if (bit_index == 31) {
    // Need to refresh bitstream
    context->stream_data[tree] = ncutil_read_be_u32(src, byte_index);

    context->stream_byte[tree] = byte_index + sizeof(uint32_t);
    context->stream_bit[tree] = 0;
  }
  // Read next bit
  else {
    context->stream_data[tree] = bits << 1;
    context->stream_bit[tree] = bit_index + 1;
  }

  return code;
}

static int ash_read_tree_9(struct nsmbw_compress_ash_decode_context *context,
                           const uint8_t *src) {
  uint32_t index = context->next_node_id[ash_tree_literal];

  int sp = 0;

  while (true) {
    // Push left/right node to the stack
    if (ash_get_bit_1c(context, src, ash_tree_literal)) {
      context->stack[sp] = ash_node_right | index;
      context->stack[sp + 1] = ash_node_left | index;
      index++;

      sp += 2;
      assert(sp < ash_tree_stack_size);
      continue;
    }

    // Assign value to the previous node
    int code = ash_get_bits_code(context, src, 9, ash_tree_literal);

    while (true) {
      // Pop last node from the stack
      if (sp == 0) {
        nsmbw_compress_print_error("Invalid ASH compressed file: too few nodes "
                                   "in literal tree");
        return -1;
      }
      uint16_t node = context->stack[--sp];

      if (node & ash_node_right) {
        context->right9[node & ash_node_index_mask] = code;
        code = node & ash_node_index_mask;

        if (sp == 0) {
          // Tree root
          return code;
        }
      } else /* ash_node_left */ {
        context->left9[node & ash_node_index_mask] = code;
        break;
      }
    }
  }
}

static int ash_read_tree_12(struct nsmbw_compress_ash_decode_context *context,
                            const uint8_t *pData) {
  uint32_t index = context->next_node_id[ash_tree_back_ref];

  int sp = 0;

  while (true) {
    // Push left/right node to the stack
    if (ash_get_bit_1c(context, pData, ash_tree_back_ref) != 0) {
      context->stack[sp] = ash_node_right | index;
      context->stack[sp + 1] = ash_node_left | index;
      index++;

      sp += 2;
      assert(sp < ash_tree_stack_size);
      continue;
    }

    // Assign value to the previous node
    int code = ash_get_bits_code(context, pData, 11, ash_tree_back_ref);

    while (true) {
      // Pop last node from the stack
      if (sp == 0) {
        nsmbw_compress_print_error("Invalid ASH compressed file: too few nodes "
                                   "in back reference tree");
        return -1;
      }
      uint16_t node = context->stack[--sp];

      if (node & ash_node_right) {
        context->right12[node & ash_node_index_mask] = code;
        code = node & ash_node_index_mask;

        if (sp == 0) {
          // Tree root
          return code;
        }
      } else /* ash_node_left */ {
        context->left12[node & ash_node_index_mask] = code;
        break;
      }
    }
  }
}

// Reference: EGG::Decomp::decodeASH from ogws
bool nsmbw_compress_ash_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  (void)params;

  if (src_length < 0x10 || src[0] != 'A' || src[1] != 'S' || src[2] != 'H' ||
      src[3] != '0') {
    nsmbw_compress_print_error("Input data is not a valid ASH file");
    return false;
  }

  const size_t expand_size = ncutil_read_be_u32(src, 4) & 0x00FFFFFF;
  assert(expand_size <= *dst_length);
  if (expand_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in ASH file");
    return false;
  }

  struct nsmbw_compress_ash_decode_context context;

  const uint32_t back_ref_ofs = ncutil_read_be_u32(src, 8);

  context.stream_byte[ash_tree_literal] = 0xC; // Skip header
  context.stream_byte[ash_tree_back_ref] = back_ref_ofs;

  context.stream_bit[ash_tree_literal] = context.stream_bit[ash_tree_back_ref] =
      0;

  context.next_node_id[ash_tree_literal] = ash_node_9_min;
  context.next_node_id[ash_tree_back_ref] = ash_node_12_min;

  ash_get_bits_code(&context, src, 32, ash_tree_literal);
  ash_get_bits_code(&context, src, 32, ash_tree_back_ref);

  int root9 = ash_read_tree_9(&context, src);
  if (root9 < 0) {
    return false;
  }
  int root12 = ash_read_tree_12(&context, src);
  if (root12 < 0) {
    return false;
  }

  int dst_index = 0;
  while (dst_index < expand_size) {
    int node12, node9;
    for (node9 = root9; node9 >= ash_node_9_min;) {
      if (ash_get_bit_1c(&context, src, ash_tree_literal) != 0) {
        node9 = context.right9[node9];
      } else {
        node9 = context.left9[node9];
      }
    }

    if (node9 < 0x100) {
      dst[dst_index++] = node9 & 0xFF;
      continue;
    }

    for (node12 = root12; node12 >= ash_node_12_min;) {
      if (ash_get_bit_1c(&context, src, ash_tree_back_ref) != 0) {
        node12 = context.right12[node12];
      } else {
        node12 = context.left12[node12];
      }
    }

    if (node12 + 1 > dst_index) {
      nsmbw_compress_print_error("Out of bounds reference in ASR compressed "
                                 "file (offset %u at output position %zu)",
                                 node12 + 1, dst_index);
      return false;
    }

    uint32_t run_index = dst_index - node12 - 1;
    node9 -= 253;

    if (dst_index + node9 > expand_size) {
      nsmbw_compress_print_error(
          "Reference overflows output size in ASR compressed file (offset %u, "
          "size %u at output position %zu with output size %zu)",
          node12 + 1, node9, dst_index, expand_size);
      return false;
    }

    for (; node9 > 0; node9--, dst_index++, run_index++) {
      dst[dst_index] = dst[run_index];
    }
  }

  return dst_index;
}

static bool ash_write_tree(struct ncutil_bit_writer *bit_writer,
                           struct nsmbw_compress_huff_table *huff_table,
                           int bit_size) {
  const uint16_t bit_size_mask = (1 << (bit_size + 1)) - 1;
  // Reused after writing the left node
  const nsmbw_compress_huff_size_t visited_right_flag =
      nsmbw_compress_left_leaf_flag;

  nsmbw_compress_huff_size_t stack[256];
  nsmbw_compress_huff_size_t sp = 0;

  bool returned = false;
  nsmbw_compress_huff_size_t *nodes = huff_table->encoded_nodes;
  nsmbw_compress_huff_size_t i =
      ((nodes[1] & ~nsmbw_compress_leaf_flags_mask) + 1) * 2;
  nsmbw_compress_huff_size_t node =
      i | (nodes[1] & nsmbw_compress_leaf_flags_mask);

  // Write root node as left/right
  if (!ncutil_bit_writer_write(bit_writer, 1, 1)) {
    return false;
  }

  while (true) {
    assert(i <= huff_table->tree_count << 1);
    if (!returned) {
      if (node & nsmbw_compress_left_leaf_flag) {
        if (!ncutil_bit_writer_write(bit_writer, nodes[i] & bit_size_mask,
                                     bit_size + 1)) {
          return false;
        }
      } else {
        if (!ncutil_bit_writer_write(bit_writer, 1, 1)) {
          return false;
        }
        // Visit the left subtree
        stack[sp++] = node & ~visited_right_flag;
        node = nodes[i] & nsmbw_compress_leaf_flags_mask;
        i += ((nodes[i] & ~nsmbw_compress_leaf_flags_mask) + 1) * 2;
        node |= i;
        returned = false;
        continue;
      }
    }

    if (!returned || !(node & visited_right_flag)) {
      if (node & nsmbw_compress_right_leaf_flag) {
        if (!ncutil_bit_writer_write(bit_writer, nodes[i + 1] & bit_size_mask,
                                     bit_size + 1)) {
          return false;
        }
      } else {
        if (!ncutil_bit_writer_write(bit_writer, 1, 1)) {
          return false;
        }
        // Visit the right subtree
        stack[sp++] = node | visited_right_flag;
        node = nodes[i + 1] & nsmbw_compress_leaf_flags_mask;
        i += ((nodes[i + 1] & ~nsmbw_compress_leaf_flags_mask) + 1) * 2;
        node |= i;
        returned = false;
        continue;
      }
    }

    if (sp == 0) {
      break;
    }

    // Pop the stack
    node = stack[--sp];
    i = node & ~nsmbw_compress_leaf_flags_mask;
    returned = true;
    continue;
  }

  return true;
}

extern bool
nsmbw_compress_ash_encode(const uint8_t *src, uint8_t *dst, size_t src_length,
                          size_t *dst_length,
                          const struct nsmbw_compress_parameters *params) {
  (void)params;

  const size_t max_dst_size = *dst_length;

  if (max_dst_size < 0x10) {
    nsmbw_compress_print_error("Output buffer is too small for ASR0 header");
    return false;
  }
  if (src_length > 0xFFFFFF) {
    nsmbw_compress_print_error("Input data is too large for ASR0 format");
    return false;
  }

  const size_t huff_tree_9_table_size =
      ncutil_align_up(sizeof(size_t), nsmbw_compress_huff_get_work_size(9));
  const size_t huff_tree_12_table_size =
      ncutil_align_up(sizeof(size_t), nsmbw_compress_huff_get_work_size(11));

  const size_t total_work_size =
      huff_tree_9_table_size + huff_tree_12_table_size;
  void *work_buffer = malloc(total_work_size);
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for ASH compression work buffer: %s",
        strerror(errno));
    return false;
  }

  uint8_t *const huff_tree_9_table_buffer = (uint8_t *)work_buffer;
  uint8_t *const huff_tree_12_table_buffer =
      huff_tree_9_table_buffer + huff_tree_9_table_size;

  struct nsmbw_compress_huff_table huff_tree_9_table;
  nsmbw_compress_huff_init_table(&huff_tree_9_table, huff_tree_9_table_buffer,
                                 9);
  struct nsmbw_compress_huff_table huff_tree_12_table;
  nsmbw_compress_huff_init_table(&huff_tree_12_table, huff_tree_12_table_buffer,
                                 11);

  // Encode LZ data with src
  uint16_t *sym_buffer, *off_buffer;
  size_t sym_count, off_count;
  if (!nsmbw_compress_lz_encode_portable(src, src_length, UCHAR_MAX + 2, 0x800,
                                         true, 0x100, &sym_buffer, &sym_count,
                                         &off_buffer, &off_count)) {
    free(work_buffer);
    return false;
  }

  // Count symbol frequencies for Huffman trees
  nsmbw_compress_huff_count_data_u16(huff_tree_9_table.nodes, sym_buffer,
                                     sym_count, 9);
  nsmbw_compress_huff_count_data_u16(huff_tree_12_table.nodes, off_buffer,
                                     off_count, 11);

  // Construct Huffman trees
  nsmbw_compress_huff_size_t construct_count =
      nsmbw_compress_huff_construct_tree(huff_tree_9_table.nodes,
                                         nsmbw_compress_huff_sym_size(9));
  nsmbw_compress_huff_make_huff_tree(&huff_tree_9_table, construct_count, 9);
  construct_count = nsmbw_compress_huff_construct_tree(
      huff_tree_12_table.nodes, nsmbw_compress_huff_sym_size(11));
  nsmbw_compress_huff_make_huff_tree(&huff_tree_12_table, construct_count, 11);

  // Write ASH0 header
  dst[0] = 'A';
  dst[1] = 'S';
  dst[2] = 'H';
  dst[3] = '0';
  ncutil_write_be_u32(dst, 4, src_length & 0x00FFFFFF);

  struct ncutil_bit_writer bit_writer = {
      .data = dst + sizeof(uint32_t) * 3, // Skip header
      .max_size = max_dst_size - sizeof(uint32_t) * 3,
  };

  // Write Huffman tree 9
  if (!ash_write_tree(&bit_writer, &huff_tree_9_table, 9)) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(work_buffer);
    free(sym_buffer);
    free(off_buffer);
    return false;
  }

  // Write literal data
  for (size_t i = 0; i < sym_count; i++) {
    uint16_t sym = sym_buffer[i];
    if (!ncutil_bit_writer_write(
            &bit_writer, huff_tree_9_table.nodes[sym].encoded_ref,
            huff_tree_9_table.nodes[sym].encoded_ref_bit_size)) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
      free(work_buffer);
      free(sym_buffer);
      free(off_buffer);
      return false;
    }
  }

  if (!ncutil_bit_writer_flush(&bit_writer)) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(work_buffer);
    free(sym_buffer);
    free(off_buffer);
    return false;
  }

  // Write offset to offset tree to header
  uint32_t tree_12_offset = bit_writer.offset + sizeof(uint32_t) * 3;
  ncutil_write_be_u32(dst, 8, tree_12_offset);

  // Write Huffman tree 12
  if (!ash_write_tree(&bit_writer, &huff_tree_12_table, 11)) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(work_buffer);
    free(sym_buffer);
    free(off_buffer);
    return false;
  }

  // Write offset data
  for (size_t i = 0; i < off_count; i++) {
    uint16_t sym = off_buffer[i];
    if (!ncutil_bit_writer_write(
            &bit_writer, huff_tree_12_table.nodes[sym].encoded_ref,
            huff_tree_12_table.nodes[sym].encoded_ref_bit_size)) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
      free(work_buffer);
      free(sym_buffer);
      free(off_buffer);
      return false;
    }
  }

  if (!ncutil_bit_writer_pad(&bit_writer, sizeof(uint32_t))) {
    nsmbw_compress_print_error("Output file is too much larger than the "
                               "input file; aborting compression");
    free(work_buffer);
    free(sym_buffer);
    free(off_buffer);
    return false;
  }

  free(work_buffer);
  free(sym_buffer);
  free(off_buffer);

  *dst_length = bit_writer.offset + sizeof(uint32_t) * 3;
  return true;
}
