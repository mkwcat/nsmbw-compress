#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
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
    uint32_t new_bits = src[byte_index + 0] << 24 | src[byte_index + 1] << 16 |
                        src[byte_index + 2] << 8 | src[byte_index + 3];

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
    context->stream_data[tree] = src[byte_index + 0] << 24 |
                                 src[byte_index + 1] << 16 |
                                 src[byte_index + 2] << 8 | src[byte_index + 3];

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
    context->stream_data[tree] = src[byte_index + 0] << 24 |
                                 src[byte_index + 1] << 16 |
                                 src[byte_index + 2] << 8 | src[byte_index + 3];

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
    if (ash_get_bit_1c(context, src, ash_tree_literal) != 0) {
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
  int root12 = ash_read_tree_12(&context, src);

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

    int run_index = dst_index - node12 - 1;
    node9 -= 253;

    for (; node9 > 0; node9--, dst_index++, run_index++) {
      dst[dst_index] = dst[run_index];
    }
  }

  return dst_index;
}
