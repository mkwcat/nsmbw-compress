#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Must be signed
typedef int nsmbw_compress_lz_size_t;
static const nsmbw_compress_lz_size_t nsmbw_compress_lz_invalid_offset = -1;

struct nsmbw_compress_lz_context {
  // The current offset in the skip table
  nsmbw_compress_lz_size_t window_pos;
  // Number of entries populated in the skip table
  nsmbw_compress_lz_size_t window_size;
  // The maximum window size
  nsmbw_compress_lz_size_t max_window_size;
  // Total number of bytes that have been slid so far
  size_t slide_total;
  // The next window offset for the byte value, indexed by the current window
  // offset
  nsmbw_compress_lz_size_t *skip_table_next;
  // The previous window offset for the byte value, indexed by the current
  // window offset. If this is not NULL, the skip table is reversed for
  // searching from the tail instead of the head. This is useful if nearer
  // references are preferred, such as with LH compression due to the smaller
  // bit size.
  nsmbw_compress_lz_size_t *skip_table_prev;
  // The head of the skip table, indexed by the byte value
  nsmbw_compress_lz_size_t *skip_table_head;
  // The tail of the skip table, indexed by the byte value
  nsmbw_compress_lz_size_t *skip_table_tail;
};

size_t nsmbw_compress_lz_get_work_size(nsmbw_compress_lz_size_t window_size,
                                       bool reverse_skip_table);

void nsmbw_compress_lz_init_context(struct nsmbw_compress_lz_context *context,
                                    void *work,
                                    nsmbw_compress_lz_size_t window_size,
                                    bool reverse_skip_table);

uint32_t nsmbw_compress_lz_search_window(
    const struct nsmbw_compress_lz_context *context, const uint8_t *data,
    uint32_t data_size, uint32_t *match_distance, uint32_t max_match_size);

int nsmbw_compress_lz_search_ahead(struct nsmbw_compress_lz_context *context,
                                   const uint8_t **src, const uint8_t *src_end,
                                   uint32_t max_match_size,
                                   uint32_t *match_size,
                                   uint32_t *match_distance);

void nsmbw_compress_lz_slide(struct nsmbw_compress_lz_context *context,
                             const uint8_t *data, uint32_t length);

size_t nsmbw_compress_lz_slide_to(struct nsmbw_compress_lz_context *context,
                                  const uint8_t *data, size_t target_offset);

#ifdef __cplusplus
} // extern "C"
#endif
