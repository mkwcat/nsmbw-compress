#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t nsmbw_compress_huff_size_t;
static const nsmbw_compress_huff_size_t nsmbw_compress_huff_invalid_node =
    (nsmbw_compress_huff_size_t)-1;

struct nsmbw_compress_huff_table {
  struct nsmbw_compress_huff_node *nodes;
  nsmbw_compress_huff_size_t *encoded_nodes;
  struct nsmbw_compress_huff_tree *tree;
  nsmbw_compress_huff_size_t tree_count;
};

struct nsmbw_compress_huff_node {
  uint32_t count;
  nsmbw_compress_huff_size_t index;
  nsmbw_compress_huff_size_t parent;
  nsmbw_compress_huff_size_t left;
  nsmbw_compress_huff_size_t right;
  uint16_t encoded_ref_bit_size;
  uint16_t depth;
  uint32_t encoded_ref;
  uint8_t direction_from_parent;
  nsmbw_compress_huff_size_t count_hword;
};

struct nsmbw_compress_huff_tree {
  bool is_left_leaf;
  bool is_right_leaf;
  nsmbw_compress_huff_size_t left;
  nsmbw_compress_huff_size_t right;
};
