#include "lz.h"
#include "ncutil.h"
#include "nsmbw_compress.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_lz_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  if (src_length < sizeof(uint32_t)) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed LZ file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const enum nsmbw_compress_cx_type type = header & nsmbw_compress_cx_type_mask;
  if (type != nsmbw_compress_cx_type_lz) {
    nsmbw_compress_print_error("Input data is not a CX-LZ file");
    return false;
  }

  const uint8_t *const src_end = src + src_length;
  uint8_t *const dst_start = dst;

  uint32_t read_size = header >> 8;
  src += sizeof(uint32_t);
  if (read_size == 0) {
    if (src_length < 8) {
      nsmbw_compress_print_error(
          "Input data is too small to be a valid CX-LZ file");
      return false;
    }
    read_size = ncutil_read_le_u32(src, 0);
    src += sizeof(uint32_t);
  }

  assert(read_size <= *dst_length);
  if (read_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in LZ file");
    return false;
  }

  uint8_t option = header & 0x0F;
  if (option != 0 && option != 1) {
    nsmbw_compress_print_error(
        "Input data has unrecognized LZ compression option: %d", option);
    return false;
  }

  const uint32_t size = read_size;
  const uint8_t *const dst_end = dst + size;

  while (dst < dst_end) {
    if (src >= src_end) {
      nsmbw_compress_print_error("Input data ended prematurely");
      return false;
    }
    uint32_t flags = *src++;
    for (int i = 0; i < 8; i++, flags <<= 1) {
      if (dst >= dst_end) {
        break;
      }

      if (!(flags & 0x80)) {
        // Literal byte
        if (src >= src_end) {
          nsmbw_compress_print_error("Input data ended prematurely");
          return false;
        }
        *dst++ = *src++;
        continue;
      }

      // Encoded reference

      if (src + 2 > src_end) {
        nsmbw_compress_print_error("Input data ended prematurely");
        return false;
      }
      uint32_t ref_size = (*src >> 4) + 3;
      if (option) {
        if (ref_size == 0x01 + 3) {
          if (src + 3 > src_end) {
            nsmbw_compress_print_error("Input data ended prematurely");
            return false;
          }
          // Why do we need references this size? This can encode a file
          // repeating the same byte 65808 times very well!!!!!!!
          ref_size = (*src++ & 0x0F) << 12;
          ref_size |= *src++ << 4;
          ref_size |= *src >> 4;
          ref_size += 0x111;
        } else if (ref_size == 0x00 + 3) {
          ref_size = (*src++ & 0x0F) << 4;
          ref_size |= *src >> 4;
          ref_size += 0x11;
        } else {
          ref_size -= 2;
        }
      }

      if (src + 2 > src_end) {
        nsmbw_compress_print_error("Input data ended prematurely");
        return false;
      }

      uint32_t ref_offset = (*src++ & 0x0F) << 8;
      ref_offset = (ref_offset | *src++) + 1;

      if (dst - dst_start < ref_offset) {
        nsmbw_compress_print_error("Out of bounds reference in LZ compressed "
                                   "file (offset %u at output position %zu)",
                                   ref_offset, (size_t)(dst - dst_start));
        return false;
      }
      if (dst + ref_size > dst_end) {
        nsmbw_compress_print_error(
            "Reference overflows output size in LZ compressed file (offset %u, "
            "size %u at output position %zu with output size %zu)",
            ref_offset, ref_size, (size_t)(dst - dst_start), size);
        return false;
      }

      do {
        *dst = *(dst - ref_offset);
        ++dst;
      } while (--ref_size > 0);
    }
  }

  if (ncutil_align_up_ptr(0x20, src) < (const void *)src_end) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        (size_t)(src_end - src));
  }

  *dst_length = size;
  return true;
}

// Must be signed
typedef nsmbw_compress_lz_size_t lz_size_t;
static const lz_size_t lz_invalid_offset = nsmbw_compress_lz_invalid_offset;

size_t nsmbw_compress_lz_get_work_size(lz_size_t window_size,
                                       bool reverse_skip_table) {
  static const size_t skip_table_head_size = UCHAR_MAX + 1;
  static const size_t skip_table_tail_size = skip_table_head_size;
  const size_t skip_table_next_size = window_size;
  const size_t skip_table_prev_size =
      reverse_skip_table ? skip_table_next_size : 0;

  return (skip_table_next_size + skip_table_prev_size + skip_table_head_size +
          skip_table_tail_size) *
         sizeof(lz_size_t);
}

void nsmbw_compress_lz_init_context(struct nsmbw_compress_lz_context *context,
                                    void *work, lz_size_t window_size,
                                    bool reverse_skip_table) {
  static const size_t skip_table_head_size = UCHAR_MAX + 1;
  static const size_t skip_table_tail_size = skip_table_head_size;
  const size_t skip_table_next_size = window_size;
  const size_t skip_table_prev_size =
      reverse_skip_table ? skip_table_next_size : 0;

  // Setup work buffers
  lz_size_t *work_ptr = (lz_size_t *)work;
  context->skip_table_next = work_ptr;
  work_ptr += skip_table_next_size;
  context->skip_table_prev = reverse_skip_table ? work_ptr : NULL;
  work_ptr += skip_table_prev_size;
  context->skip_table_head = work_ptr;
  work_ptr += skip_table_head_size;
  context->skip_table_tail = work_ptr;

  // Initialize context
  for (int i = 0; i < skip_table_head_size; ++i) {
    context->skip_table_head[i] = lz_invalid_offset;
    context->skip_table_tail[i] = lz_invalid_offset;
  }

  context->window_pos = 0;
  context->window_size = 0;
  context->max_window_size = window_size;
  context->slide_total = 0;
  context->fake_match_size = 0;
}

uint32_t
nsmbw_compress_lz_search_window(const struct nsmbw_compress_lz_context *context,
                                const uint8_t *data, uint32_t data_size,
                                uint32_t *restrict match_distance,
                                uint32_t max_match_size) {
  if (context->fake_match_size) {
    assert(context->slide_total == context->fake_match_slide);
    *match_distance = context->fake_match_distance;
    return context->fake_match_size;
  }

  const lz_size_t window_pos = context->window_pos;
  const lz_size_t window_size = context->window_size;

  lz_size_t *st_next = context->skip_table_next;
  if (context->skip_table_prev) {
    // Reverse the skip table for searching from the tail. This is more optimal
    // with LH compression due to the smaller reference bit size. This also
    // appears to be what Nintendo did
    st_next = context->skip_table_prev;
  }

  static const uint32_t minimum_match_size = 3;
  if (data_size < minimum_match_size) {
    return 0;
  }

  lz_size_t it;
  if (context->skip_table_prev) {
    it = context->skip_table_tail[*data];
  } else {
    it = context->skip_table_head[*data];
  }

  max_match_size = max_match_size < data_size ? max_match_size : data_size;

  uint32_t best_match_offset;
  uint32_t best_match_size = 2;
  for (; it != lz_invalid_offset; it = st_next[it]) {
    const uint8_t *match_data;
    if (it < window_pos) {
      match_data = data - window_pos + it;
    } else {
      match_data = data - window_size - window_pos + it;
    }

    if (match_data[1] != data[1] || match_data[2] != data[2]) {
      continue;
    }

    uint32_t match_size;
    for (match_size = 3; match_size < max_match_size &&
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
  *match_distance = best_match_offset;
  return best_match_size;
}

int nsmbw_compress_lz_search_ahead(struct nsmbw_compress_lz_context *context,
                                   const uint8_t **src, const uint8_t *src_end,
                                   uint32_t max_match_size,
                                   uint32_t *restrict match_size,
                                   uint32_t *restrict match_distance) {
  if (context->fake_match_size) {
    assert(context->slide_total == context->fake_match_slide);
    *match_size = context->fake_match_size;
    *match_distance = context->fake_match_distance;
    *src +=
        nsmbw_compress_lz_slide_to(context, *src, context->fake_match_slide_to);
    context->fake_match_size = 0;
    return 0;
  }

  if (*match_size == max_match_size) {
    nsmbw_compress_lz_slide(context, *src, max_match_size);
    *src += max_match_size;
    return 0;
  }

  uint32_t slide_base = context->slide_total;
  uint32_t in_match_size = *match_size, in_match_distance = *match_distance;

  // Check if the first byte after the current match would allow for a longer
  // match
  nsmbw_compress_lz_slide(context, (*src)++, 1);
  uint32_t next_match_distance, next_next_match_distance;
  uint32_t next_next_match_size = 0;
  uint32_t next_match_size = nsmbw_compress_lz_search_window(
      context, *src, src_end - *src, &next_match_distance, max_match_size);
  if (next_match_size <= in_match_size) {
    nsmbw_compress_lz_slide(context, *src, in_match_size - 1);
    *src += in_match_size - 1;
    return 0;
  }

  if (next_match_size < max_match_size) {
    nsmbw_compress_lz_slide(context, (*src)++, 1);
    next_next_match_size = nsmbw_compress_lz_search_window(
        context, *src, src_end - *src, &next_next_match_distance,
        max_match_size);
  }

  int literal_count;
  if (next_next_match_size > next_match_size + 1) {
    *match_size = next_next_match_size;
    *match_distance = next_next_match_distance;
    literal_count = 2;
  } else {
    *match_size = next_match_size;
    *match_distance = next_match_distance;
    literal_count = 1;
  }

  // One last search: It's possible for our match size gains to be completely
  // negated by the next match (leading to worse compression) if it's the same
  // between both selected matches.
  *src += nsmbw_compress_lz_slide_to(context, *src, slide_base + in_match_size);
  uint32_t next_match_distance2;
  uint32_t next_match_size2 = nsmbw_compress_lz_search_window(
      context, *src, src_end - *src, &next_match_distance2, max_match_size);
  *src += nsmbw_compress_lz_slide_to(context, *src,
                                     slide_base + literal_count + *match_size);

  if (in_match_size + next_match_size2 < *match_size) {
    return literal_count;
  }
  // Okay, well, two last searches so we can compare the two
  uint32_t next_match_distance3;
  uint32_t next_match_size3 = nsmbw_compress_lz_search_window(
      context, *src, src_end - *src, &next_match_distance3, max_match_size);
  if (in_match_size + next_match_size2 <
      in_match_size + *match_size + next_match_size3) {
    return literal_count;
  }

  // The change is not worth it, revert back to the original match
  *match_size = in_match_size;
  *match_distance = in_match_distance;
  // This is a HACK!!! We already slid the next match size and we
  // can't slide back!!! But we know we haven't slid past it yet, so we
  // can just store the match we need and rely on the caller to call us in
  // the correct order to return it.
  context->fake_match_size = next_match_size2;
  context->fake_match_distance = next_match_distance2;
  context->fake_match_slide = context->slide_total;
  context->fake_match_slide_to = slide_base + in_match_size + next_match_size2;
  return 0;
}

void nsmbw_compress_lz_slide(struct nsmbw_compress_lz_context *context,
                             const uint8_t *data, uint32_t length) {
  lz_size_t *restrict const st_head = context->skip_table_head;
  lz_size_t *restrict const st_next = context->skip_table_next;
  lz_size_t *restrict const st_tail = context->skip_table_tail;
  lz_size_t *restrict const st_prev = context->skip_table_prev;
  lz_size_t window_pos = context->window_pos;
  lz_size_t window_size = context->window_size;
  const lz_size_t max_window_size = context->max_window_size;

  for (uint32_t i = 0; i < length; i++, data++) {
    const uint8_t value = *data;

    lz_size_t it = window_size;
    if (window_size == max_window_size) {
      const uint8_t remove_value = data[-max_window_size];

      if (st_prev && st_head[remove_value] != lz_invalid_offset &&
          st_next[st_head[remove_value]] != lz_invalid_offset) {
        st_prev[st_next[st_head[remove_value]]] = lz_invalid_offset;
      }

      if ((st_head[remove_value] = st_next[st_head[remove_value]]) ==
          lz_invalid_offset) {
        st_tail[remove_value] = lz_invalid_offset;
      }

      it = window_pos;
    }

    const lz_size_t tail_it = st_tail[value];
    if (tail_it == lz_invalid_offset) {
      st_head[value] = it;
    } else {
      st_next[tail_it] = it;
    }

    st_tail[value] = it;
    st_next[it] = lz_invalid_offset;
    if (st_prev) {
      st_prev[it] = tail_it;
    }

    if (window_size == max_window_size) {
      window_pos = (window_pos + 1) % max_window_size;
    } else {
      window_size++;
    }
  }

  context->window_pos = window_pos;
  context->window_size = window_size;
  context->slide_total += length;
}

size_t nsmbw_compress_lz_slide_to(struct nsmbw_compress_lz_context *context,
                                  const uint8_t *data, size_t target_offset) {
  assert(target_offset >= context->slide_total);
  size_t count = target_offset - context->slide_total;
  nsmbw_compress_lz_slide(context, data, count);
  return count;
}

bool nsmbw_compress_lz_encode(
    const uint8_t *restrict src, uint8_t *restrict dst, size_t src_length,
    size_t *dst_length,
    const struct nsmbw_compress_parameters *restrict params) {
  static const uint32_t window_size = 0x1000;

  void *work_buffer =
      malloc(nsmbw_compress_lz_get_work_size(window_size, false));
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LZ compression work buffer: %s",
        strerror(errno));
    return false;
  }

  const unsigned max_match_size = params->lz_extended ? 0x10110 : 0x12;
  const size_t max_dst_size = *dst_length;
  const uint8_t *dst_start = dst;
  const uint8_t *dst_end = dst + max_dst_size;
  const uint8_t *src_end = src + src_length;

  if (src_length < 0x1000000) {
    ncutil_write_le_u32(dst, 0,
                        src_length << 8 | nsmbw_compress_cx_type_lz |
                            params->lz_extended);
    dst += sizeof(uint32_t);
  } else {
    ncutil_write_le_u32(dst, 0,
                        nsmbw_compress_cx_type_lz | params->lz_extended);
    ncutil_write_le_u32(dst, sizeof(uint32_t), src_length);
    dst += sizeof(uint32_t) + sizeof(uint32_t);
  }

  struct nsmbw_compress_lz_context context;
  nsmbw_compress_lz_init_context(&context, work_buffer, window_size, false);

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
          free(work_buffer);
          return false;
        }

        nsmbw_compress_lz_slide(&context, src, 1);
        *dst++ = *src++;

        continue;
      }

      if (dst + 5 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        free(work_buffer);
        return false;
      }

      const uint8_t *const literal_ptr = src;
      int literal_count = nsmbw_compress_lz_search_ahead(
          &context, ncutil_restrict_cast(const uint8_t **, &src), src_end,
          max_match_size, &match_size, &match_distance);
      for (int j = 0; j < literal_count; j++) {
        *dst++ = literal_ptr[j];
        if (++i >= 8) {
          *flags_ptr = flags;
          flags = i = 0;
          flags_ptr = dst++;
        }
        flags <<= 1;
      }

      // Encoded reference
      flags |= 1;

      uint32_t match_size_byte = match_size - 3;

      if (params->lz_extended) {
        if (match_size >= 0x111) {
          match_size_byte = match_size - 0x111;

          *dst++ = match_size_byte >> 12 | 0x10;
          *dst++ = match_size_byte >> 4;
        } else if (match_size >= 0x11) {
          match_size_byte = match_size - 0x11;

          *dst++ = match_size_byte >> 4;
        } else {
          match_size_byte = match_size - 0x1;
        }
      }

      if (dst + 2 > dst_end) {
        nsmbw_compress_print_error("Output file is too much larger than the "
                                   "input file; aborting compression");
        free(work_buffer);
        return false;
      }

      *dst++ = (match_size_byte << 4) | (match_distance - 1) >> 8;
      *dst++ = match_distance - 1;
    }

    *flags_ptr = flags;
  }

  free(work_buffer);

  *dst_length = dst - dst_start;
  return *dst_length != 0;
}
