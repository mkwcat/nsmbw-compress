#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
  bool is_right_of_parent;
  nsmbw_compress_huff_size_t count_hword;
};

struct nsmbw_compress_huff_tree {
  bool is_left_leaf;
  bool is_right_leaf;
  nsmbw_compress_huff_size_t left;
  nsmbw_compress_huff_size_t right;
};

#define nsmbw_compress_huff_sym_size(huff_bit_size) (1 << (huff_bit_size))

size_t nsmbw_compress_huff_get_work_size(uint8_t huff_bit_size);

void nsmbw_compress_huff_init_table(struct nsmbw_compress_huff_table *table,
                                    void *work, uint8_t huff_bit_size);

void nsmbw_compress_huff_count_data(struct nsmbw_compress_huff_node *nodes,
                                    uint8_t const *data, uint32_t size,
                                    uint8_t huff_bit_size);

static inline void
nsmbw_compress_huff_count_byte(struct nsmbw_compress_huff_node *nodes,
                               nsmbw_compress_huff_size_t byte) {
  nodes[byte].count++;
}

nsmbw_compress_huff_size_t
nsmbw_compress_huff_construct_tree(struct nsmbw_compress_huff_node *nodes,
                                   const uint32_t huff_sym_size);

void nsmbw_compress_huff_make_huff_tree(struct nsmbw_compress_huff_table *table,
                                        uint16_t construct_count,
                                        uint8_t huff_bit_size);

uint32_t nsmbw_compress_huff_convert_data(
    struct nsmbw_compress_huff_node *nodes, uint8_t const *src, uint8_t *dst,
    uint32_t size, uint32_t max_length, uint8_t huff_bit_size);

#ifdef __cplusplus
} // extern "C"
#endif