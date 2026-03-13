#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_huff.h"
#include "nsmbw_compress_internal.h"
#include "nsmbw_compress_lz.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_lh_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  void *work_buffer = malloc(CX_SECURE_UNCOMPRESS_LH_WORK_SIZE);
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LH decompression work buffer: %s",
        strerror(errno));
    return false;
  }

  CXSecureResult result =
      CXSecureUncompressLH(src, src_length, dst, work_buffer);

  free(work_buffer);

  if (result != CX_SECURE_ERR_OK) {
    nsmbw_compress_print_cx_error(true, result);
    return false;
  }
  return true;
}

static const uint32_t lh_encode_lz_window_size = 0x10000u;

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

      uint16_t match_distance;
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

      // Encoded reference
      flags |= 1;

      if (dst + 5 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        return 0;
      }

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

      nsmbw_compress_lz_slide(&context, src, match_size);
      src += match_size;
    }

    *flags_ptr = flags;
  }

  // Pad to 4 bytes
  for (int i = 0; (dst - dst_start + i) % 4 != 0; i++) {
    *dst++ = 0;
  }

  return dst - dst_start;
}

bool nsmbw_compress_lh_encode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  if (src_length < 0x1000000) {
    ncutil_write_le_u32(dst, 0, src_length << 8 | CX_COMPRESSION_TYPE_LH);
    dst += sizeof(uint32_t);
  } else {
    ncutil_write_le_u32(dst, 0, CX_COMPRESSION_TYPE_LH);
    ncutil_write_le_u32(dst, sizeof(uint32_t), src_length);
    dst += sizeof(uint32_t) + sizeof(uint32_t);
  }

  const size_t lz_work_size = ncutil_align_up(
      sizeof(size_t),
      nsmbw_compress_lz_get_work_size(lh_encode_lz_window_size, true));
  const size_t huff_sym_table_size =
      ncutil_align_up(sizeof(size_t), nsmbw_compress_huff_get_work_size(9));
  const size_t huff_dst_table_size =
      ncutil_align_up(sizeof(size_t), nsmbw_compress_huff_get_work_size(5));
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
  nsmbw_compress_huff_init_table(&sym_table, huff_sym_table_buffer, 9);
  struct nsmbw_compress_huff_table dst_table;
  nsmbw_compress_huff_init_table(&dst_table, huff_dst_table_buffer, 5);

  size_t lz_length = lz_encode(src, lz_data, src_length, tmp_lz_buffer_size,
                               &sym_table, &dst_table, lz_work_buffer);
  if (!lz_length) {
    free(work_buffer);
    return false;
  }

  nsmbw_compress_huff_size_t construct_count =
      nsmbw_compress_huff_construct_tree(sym_table.nodes,
                                         nsmbw_compress_huff_sym_size(9));
  nsmbw_compress_huff_make_huff_tree(&sym_table, construct_count, 9);
  construct_count = nsmbw_compress_huff_construct_tree(
      dst_table.nodes, nsmbw_compress_huff_sym_size(5));
  nsmbw_compress_huff_make_huff_tree(&dst_table, construct_count, 5);

  struct ncutil_bit_writer bit_writer = {
      .data = dst,
  };

  // Write Huffman tables
  struct nsmbw_compress_huff_table *table = &sym_table;
  for (int t = 0; t < 2; t++, table = &dst_table) {
    const int bit_size = t ? 5 : 9;
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

  // Pad to 4 bytes
  ncutil_bit_writer_pad(&bit_writer, sizeof(uint32_t));

  *dst_length = sizeof(uint32_t) + (src_length > 0x1000000) * sizeof(uint32_t) +
                bit_writer.offset;
  free(work_buffer);
  return *dst_length != 0;
}
