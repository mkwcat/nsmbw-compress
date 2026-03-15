#include "huff.h"
#include "lz.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
  lh_sym_huff_bit_size = 9,
  lh_dst_huff_bit_size = 5,
};

static uint32_t lh_import_huff_tree(uint16_t *tree, const uint8_t *src,
                                    uint8_t huff_bit_size,
                                    uint32_t input_size) {
  const uint8_t *const src_start = src;
  uint32_t table_size = *src++;
  if (huff_bit_size > 8) {
    table_size |= *src++ << 8;
  }
  table_size = (table_size + 1) << 2;

  if (table_size > input_size) {
    return table_size;
  }

  const uint8_t *const src_end = src_start + table_size;
  const uint32_t max_table_size = 1 << (huff_bit_size + 1);
  const uint16_t huff_bit_mask = (1 << huff_bit_size) - 1;

  int bit_count = 0;
  uint32_t value = 0;
  uint32_t it = 1;
  while (src < src_end) {
    while (bit_count < huff_bit_size) {
      value <<= 8;
      value |= *src++;
      bit_count += 8;
    }

    if (it < max_table_size) {
      tree[it++] = huff_bit_mask & (value >> (bit_count - huff_bit_size));
    }

    bit_count -= huff_bit_size;
  }
  tree[0] = it - 1;

  return table_size;
}

static inline int lh_read_next_huff_value(struct ncutil_bit_reader *bitReader,
                                          uint16_t *tree,
                                          uint8_t huff_bit_size) {
  const uint16_t *const tree_end = tree + *tree + 1;
  tree += 1;
  const int huff_bit_top = (1 << huff_bit_size) >> 1;
  const int huff_bit_mask = ((1 << huff_bit_size) >> 2) - 1;

  do {
    int bit = ncutil_bit_reader_read_bit(bitReader);
    int index = (((*tree & huff_bit_mask) + 1) << 1) + bit;

    if (bit < 0) {
      nsmbw_compress_print_error(
          "Reached unexpected end of file while decompressing LH input data");
      return -1;
    }

    if (*tree & (huff_bit_top >> bit)) {
      tree = (uint16_t *)ncutil_align_down_ptr(4, tree);
      if (tree + index >= tree_end) {
        nsmbw_compress_print_error("LH Huffman tree index out of bounds");
        return -1;
      }
      return tree[index];
    } else {
      tree = (uint16_t *)ncutil_align_down_ptr(4, tree);
      tree += index;
    }
  } while (tree < tree_end);

  nsmbw_compress_print_error(
      "LH Huffman tree traversal overflowed the end of the tree");
  return -1;
}

bool nsmbw_compress_lh_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  const uint8_t *const src_end = src + src_length;
  const uint8_t *const dst_start = dst;

  uint16_t sym_tree[1 << (lh_sym_huff_bit_size + 2)];
  uint16_t dst_tree[1 << (lh_dst_huff_bit_size + 2)];

  if (src + sizeof(uint32_t) + sizeof(uint16_t) > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed LH file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const enum nsmbw_compress_cx_type type = header & nsmbw_compress_cx_type_mask;
  if (type != nsmbw_compress_cx_type_lh) {
    nsmbw_compress_print_error("Input data is not a CX-LH file");
    return false;
  }
  const uint8_t option = header & 0xF;
  if (option != 0) {
    nsmbw_compress_print_error(
        "Unknown LH option in input data: %d (expected 0)", option);
    return false;
  }
  uint32_t read_size = header >> 8;

  src += sizeof(uint32_t);

  if (read_size == 0) {
    if (src + sizeof(uint32_t) + sizeof(uint16_t) > src_end) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid compressed LH file");
      return false;
    }
    read_size = ncutil_read_le_u32(src, 0);

    src += sizeof(uint32_t);
  }

  assert(read_size <= *dst_length);
  if (read_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in LH file");
    return false;
  }

  const uint32_t size = read_size;
  const uint8_t *const dst_end = dst + size;

  src +=
      lh_import_huff_tree(sym_tree, src, lh_sym_huff_bit_size, src_end - src);
  if (src >= src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed LH file");
    return false;
  }

  if (!nsmbw_compress_huff_verify_table(sym_tree, lh_sym_huff_bit_size,
                                        lh_sym_huff_bit_size)) {
    nsmbw_compress_print_error("Invalid Huffman table 1 in LH compressed file");
    return false;
  }

  src +=
      lh_import_huff_tree(dst_tree, src, lh_dst_huff_bit_size, src_end - src);

  if (src > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed LH file");
    return false;
  }

  if (!nsmbw_compress_huff_verify_table(dst_tree, lh_dst_huff_bit_size,
                                        lh_dst_huff_bit_size)) {
    nsmbw_compress_print_error("Invalid Huffman table 2 in LH compressed file");
    return false;
  }

  struct ncutil_bit_reader bit_reader = {src, src_end};

  while (dst < dst_end) {
    int ret =
        lh_read_next_huff_value(&bit_reader, sym_tree, lh_sym_huff_bit_size);
    if (ret < 0) {
      return false;
    }

    if (ret < 0x100) {
      // LZ flag not set; just a byte
      *dst++ = ret;
      continue;
    }

    uint16_t ref_size = (ret & 0xFF) + 3;

    ret = lh_read_next_huff_value(&bit_reader, dst_tree, lh_dst_huff_bit_size);
    if (ret < 0) {
      return false;
    }
    uint16_t ref_offset_bits = ret;
    uint16_t ref_offset = 0;
    const uint16_t ref_offset_bits_save = ref_offset_bits;

    if (ref_offset_bits) {
      ref_offset = 1;

      while (--ref_offset_bits) {
        if (ref_offset & 0x8000) {
          nsmbw_compress_print_error(
              "LH data contains reference offset larger than 16 bits");
          return false;
        }
        ref_offset <<= 1;
        ref_offset |= ncutil_bit_reader_read_bit(&bit_reader);
      }
    }

    if (ref_offset == 0xFFFF) {
      nsmbw_compress_print_error(
          "LH data contains reference offset larger than 16 bits");
      return false;
    }

    if (dst - dst_start < ++ref_offset) {
      nsmbw_compress_print_error(
          "Out of bounds reference in LH compressed file (offset %u at output "
          "position %zu)",
          ref_offset, (size_t)(dst - dst_start));
      return false;
    }

    if (dst + ref_size > dst_end) {
      nsmbw_compress_print_error(
          "Reference overflows output size in LH compressed file (offset %u, "
          "size %u at output position %zu with output size %zu)",
          ref_offset, ref_size, (size_t)(dst - dst_start), size);
      return false;
    }

    while (ref_size--) {
      *dst = dst[0 - ref_offset];
      dst++;
    }
  }

  assert(dst == dst_end);

  if (ncutil_align_up_ptr(0x20, bit_reader.data) < (const void *)src_end) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        (size_t)(src_end - bit_reader.data));
  }

  *dst_length = size;
  return true;
}

// Cannot support more than 0xFFFF due to the reference offset being stored in a
// u16 in the official LH decoder
static const uint32_t lh_encode_lz_window_size = 0xFFFFu;

static size_t lz_encode(const uint8_t *restrict src, uint8_t *restrict dst,
                        size_t src_length, size_t dst_length,
                        struct nsmbw_compress_huff_table *sym_table,
                        struct nsmbw_compress_huff_table *dst_table,
                        void *work_buffer) {
  const unsigned max_match_size = UCHAR_MAX + 3;
  const size_t max_dst_size = dst_length;
  const uint8_t *dst_start = dst;
  const uint8_t *dst_end = dst + max_dst_size;
  const uint8_t *src_end = src + src_length;

  struct nsmbw_compress_lz_context context;
  nsmbw_compress_lz_init_context(&context, work_buffer,
                                 lh_encode_lz_window_size, true);

  while (src < src_end) {
    uint8_t flags = 0;
    uint8_t *flags_ptr = dst++;

    for (int i = 0; i < 8; i++) {
      flags <<= 1;
      if (src >= src_end) {
        continue;
      }

      uint32_t match_distance;
      uint32_t match_size = nsmbw_compress_lz_search_window(
          &context, src, src_end - src, &match_distance, max_match_size);
      if (!match_size) {
        // Literal byte
        if (dst + 1 > dst_end) {
          nsmbw_compress_print_error("Output file is too much larger than the "
                                     "input file; aborting compression");
          return 0;
        }

        nsmbw_compress_lz_slide(&context, src, 1);

        nsmbw_compress_huff_count_byte(sym_table->nodes, *dst++ = *src++);
        continue;
      }

      if (dst + 8 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        return 0;
      }

      const uint8_t *const literal_ptr = src;
      int literal_count = nsmbw_compress_lz_search_ahead(
          &context, ncutil_restrict_cast(const uint8_t **, &src), src_end,
          max_match_size, &match_size, &match_distance);
      for (int j = 0; j < literal_count; j++) {
        *dst++ = literal_ptr[j];
        nsmbw_compress_huff_count_byte(sym_table->nodes, literal_ptr[j]);
        if (++i >= 8) {
          *flags_ptr = flags;
          flags = i = 0;
          flags_ptr = dst++;
        }
        flags <<= 1;
      }

      // Encoded reference
      flags |= 1;

      // Encoding is only used internally by nsmbw_compress_lh_encode
      uint32_t match_size_byte = match_size - 3;
      *dst++ = match_size_byte;
      nsmbw_compress_huff_count_byte(sym_table->nodes,
                                     0x100u | match_size_byte);

      match_distance -= 1;
      *dst++ = match_distance;
      if (lh_encode_lz_window_size > 0x100u) {
        *dst++ = match_distance >> 8;
        if (lh_encode_lz_window_size > 0x10000u) {
          *dst++ = match_distance >> 16;
          if (lh_encode_lz_window_size > 0x1000000u) {
            *dst++ = match_distance >> 24;
          }
        }
      }
      nsmbw_compress_huff_count_byte(dst_table->nodes,
                                     32 - ncutil_clz_u32(match_distance));
    }

    *flags_ptr = flags;
  }

  // Pad to 4 bytes
  while ((dst - dst_start) % 4 != 0) {
    if (dst + 1 > dst_end) {
      nsmbw_compress_print_error("Output file is too much larger than the "
                                 "input file; aborting compression");
      return 0;
    }
    *dst++ = 0;
  }

  return dst - dst_start;
}

bool nsmbw_compress_lh_encode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  if (src_length < 0x1000000) {
    ncutil_write_le_u32(dst, 0, src_length << 8 | nsmbw_compress_cx_type_lh);
    dst += sizeof(uint32_t);
  } else {
    ncutil_write_le_u32(dst, 0, nsmbw_compress_cx_type_lh);
    ncutil_write_le_u32(dst, sizeof(uint32_t), src_length);
    dst += sizeof(uint32_t) + sizeof(uint32_t);
  }

  const size_t lz_work_size = ncutil_align_up(
      sizeof(size_t),
      nsmbw_compress_lz_get_work_size(lh_encode_lz_window_size, true));
  const size_t huff_sym_table_size = ncutil_align_up(
      sizeof(size_t), nsmbw_compress_huff_get_work_size(lh_sym_huff_bit_size));
  const size_t huff_dst_table_size = ncutil_align_up(
      sizeof(size_t), nsmbw_compress_huff_get_work_size(lh_dst_huff_bit_size));
  const size_t tmp_lz_buffer_size = 0x1000 + src_length * 4;

  const size_t total_work_size = lz_work_size + huff_sym_table_size +
                                 huff_dst_table_size + tmp_lz_buffer_size;
  void *work_buffer = malloc(total_work_size);
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LH compression work buffer: %s",
        strerror(errno));
    return false;
  }

  uint8_t *const lz_work_buffer = (uint8_t *)work_buffer;
  uint8_t *const huff_sym_table_buffer = lz_work_buffer + lz_work_size;
  uint8_t *const huff_dst_table_buffer =
      huff_sym_table_buffer + huff_sym_table_size;
  uint8_t *const lz_data = huff_dst_table_buffer + huff_dst_table_size;

  struct nsmbw_compress_huff_table sym_table;
  nsmbw_compress_huff_init_table(&sym_table, huff_sym_table_buffer,
                                 lh_sym_huff_bit_size);
  struct nsmbw_compress_huff_table dst_table;
  nsmbw_compress_huff_init_table(&dst_table, huff_dst_table_buffer,
                                 lh_dst_huff_bit_size);

  size_t lz_length = lz_encode(src, lz_data, src_length, tmp_lz_buffer_size,
                               &sym_table, &dst_table, lz_work_buffer);
  if (!lz_length) {
    free(work_buffer);
    return false;
  }

  nsmbw_compress_huff_size_t construct_count =
      nsmbw_compress_huff_construct_tree(
          sym_table.nodes, nsmbw_compress_huff_sym_size(lh_sym_huff_bit_size));
  nsmbw_compress_huff_make_huff_tree(&sym_table, construct_count,
                                     lh_sym_huff_bit_size);
  construct_count = nsmbw_compress_huff_construct_tree(
      dst_table.nodes, nsmbw_compress_huff_sym_size(lh_dst_huff_bit_size));
  nsmbw_compress_huff_make_huff_tree(&dst_table, construct_count,
                                     lh_dst_huff_bit_size);

  struct ncutil_bit_writer bit_writer = {
      .data = dst,
  };

  // Write Huffman tables
  struct nsmbw_compress_huff_table *table = &sym_table;
  for (int t = 0; t < 2; t++, table = &dst_table) {
    const int bit_size = t ? lh_dst_huff_bit_size : lh_sym_huff_bit_size;
    struct ncutil_bit_writer header_writer = bit_writer;
    if (bit_size > 8) {
      ncutil_bit_writer_write(&bit_writer, 0, 16);
    } else {
      ncutil_bit_writer_write(&bit_writer, 0, 8);
    }
    const nsmbw_compress_huff_size_t table_size = table->tree_count << 1;
    for (nsmbw_compress_huff_size_t i = 1; i < table_size; ++i) {
      nsmbw_compress_huff_size_t tree_flags = table->encoded_nodes[i] & 0xC000;
      nsmbw_compress_huff_size_t encoded =
          (tree_flags >> (16 - bit_size)) | table->encoded_nodes[i];
      ncutil_bit_writer_write(&bit_writer, encoded, bit_size);
    }
    // Pad to 4 bytes
    ncutil_bit_writer_pad(&bit_writer, sizeof(uint32_t));
    // Write the table size in 32-bit words
    const uint16_t table_byte_count =
        (bit_writer.offset - header_writer.offset) / sizeof(uint32_t) - 1;
    if (bit_size > 8) {
      ncutil_bit_writer_write(&header_writer,
                              (table_byte_count << 8) | (table_byte_count >> 8),
                              16);
    } else {
      ncutil_bit_writer_write(&header_writer, table_byte_count, 8);
    }
  }

  // Write LZ data with huffman
  for (uint32_t i = 0; i < lz_length;) {
    uint8_t flags = lz_data[i++];
    for (uint8_t j = 0; j < 8; j++) {
      if (i >= lz_length) {
        break;
      }
      if (flags & 0x80) {
        uint16_t lzref_length = lz_data[i++];
        uint32_t lzref_offset = lz_data[i++];
        if (lh_encode_lz_window_size > 0x100) {
          lzref_offset |= lz_data[i++] << 8;
          if (lh_encode_lz_window_size > 0x10000) {
            lzref_offset |= lz_data[i++] << 16;
            if (lh_encode_lz_window_size > 0x1000000) {
              lzref_offset |= lz_data[i++] << 24;
            }
          }
        }

        // This bit in the value is used instead of the flags bit to signify an
        // encoded reference
        lzref_length |= 0x100;

        // Write the length using the first huffman table
        ncutil_bit_writer_write(
            &bit_writer, sym_table.nodes[lzref_length].encoded_ref,
            sym_table.nodes[lzref_length].encoded_ref_bit_size);
        // Write number of bits needed to encode the offset with the second
        // huffman table
        uint16_t offset_bits = 32 - ncutil_clz_u32(lzref_offset);
        ncutil_bit_writer_write(
            &bit_writer, dst_table.nodes[offset_bits].encoded_ref,
            dst_table.nodes[offset_bits].encoded_ref_bit_size);
        // Then write the offset itself with that number of bits
        if (offset_bits > 1) {
          ncutil_bit_writer_write(&bit_writer,
                                  lzref_offset & ((1 << (offset_bits - 1)) - 1),
                                  offset_bits - 1);
        }
      } else {
        uint8_t c = lz_data[i++];
        ncutil_bit_writer_write(&bit_writer, sym_table.nodes[c].encoded_ref,
                                sym_table.nodes[c].encoded_ref_bit_size);
      }
      flags <<= 1;
    }
  }

  ncutil_bit_writer_flush(&bit_writer);

  *dst_length = sizeof(uint32_t) + (src_length > 0x1000000) * sizeof(uint32_t) +
                bit_writer.offset;
  free(work_buffer);
  return *dst_length != 0;
}
