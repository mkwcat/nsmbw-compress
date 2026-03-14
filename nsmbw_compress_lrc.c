#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct range_coder_info {
  uint32_t *at_0x00;
  uint32_t *at_0x04;
  uint32_t sym_size;
  uint8_t rc_bit_size;
};

struct range_coder_state {
  int32_t at_0x00;
  uint32_t at_0x04;
  int32_t at_0x08;
  uint8_t at_0x0c;
  int32_t at_0x10;
};

static void range_coder_info_init(struct range_coder_info *rc_info,
                                  uint8_t rc_bit_size, uint32_t *work) {
  uint32_t i;
  int32_t rc_sym_size = 1 << rc_bit_size;

  rc_info->rc_bit_size = rc_bit_size;
  rc_info->at_0x00 = work;
  rc_info->at_0x04 = work + rc_sym_size;

  for (i = 0; i < rc_sym_size; ++i) {
    rc_info->at_0x00[i] = 1;
    rc_info->at_0x04[i] = i;
  }

  rc_info->sym_size = rc_sym_size;
}

static void range_coder_state_init(struct range_coder_state *rc_state) {
  rc_state->at_0x00 = 0;
  rc_state->at_0x04 = 0x80000000;
  rc_state->at_0x08 = 0;
  rc_state->at_0x0c = 0;
  rc_state->at_0x10 = 0;
}

static void range_coder_add_count(struct range_coder_info *rc_info,
                                  uint16_t count) {
  uint32_t rc_sym_size = 1 << rc_info->rc_bit_size;

  rc_info->at_0x00[count]++;
  rc_info->sym_size++;

  for (uint32_t i = count + 1; i < rc_sym_size; ++i) {
    rc_info->at_0x04[i]++;
  }

  if (rc_info->sym_size < 0x10000) {
    return;
  }

  if (*rc_info->at_0x00 > 1) {
    *rc_info->at_0x00 >>= 1;
  }

  *rc_info->at_0x04 = 0;
  rc_info->sym_size = *rc_info->at_0x00;

  for (uint32_t i = 1; i < rc_sym_size; ++i) {
    if (rc_info->at_0x00[i] > 1) {
      rc_info->at_0x00[i] >>= 1;
    }

    rc_info->at_0x04[i] = rc_info->at_0x04[i - 1] + rc_info->at_0x00[i - 1];
    rc_info->sym_size += rc_info->at_0x00[i];
  }
}

static uint16_t range_coder_search(struct range_coder_info *rc_info,
                                   int32_t rc_state_0x08,
                                   uint32_t rc_state_0x04,
                                   int32_t rc_state_0x00) {
  int32_t rc_sym_size = 1 << rc_info->rc_bit_size;
  int32_t b = rc_state_0x08 - rc_state_0x00;
  uint32_t c = rc_state_0x04 / rc_info->sym_size;
  uint32_t d = b / c;
  uint32_t e = 0;
  int32_t rc_sym_mask = rc_sym_size - 1;

  int32_t g;
  while (e < rc_sym_mask) {
    g = (e + rc_sym_mask) >> 1;

    if (rc_info->at_0x04[g] > d) {
      rc_sym_mask = g;
    } else {
      e = g + 1;
    }
  }

  g = e;
  while (rc_info->at_0x04[g] > d) {
    --g;
  }

  return g;
}

static int range_coder_get_data(const uint8_t *stream,
                                struct range_coder_info *rc_info,
                                struct range_coder_state *rc_state,
                                int32_t *src_length) {
  uint16_t count = range_coder_search(rc_info, rc_state->at_0x08,
                                      rc_state->at_0x04, rc_state->at_0x00);
  int32_t rc_src_size = 0;

  int32_t c = rc_state->at_0x04 / rc_info->sym_size;

  rc_state->at_0x00 += c * rc_info->at_0x04[count];
  rc_state->at_0x04 = c * rc_info->at_0x00[count];

  range_coder_add_count(rc_info, count);

  while (rc_state->at_0x04 < 0x1000000) {
    if (rc_src_size >= *src_length) {
      nsmbw_compress_print_error(
          "Input file is too small to decode next range coder data");
      return -1;
    }

    rc_state->at_0x08 <<= 8;
    rc_state->at_0x08 += stream[rc_src_size++];
    rc_state->at_0x04 <<= 8;
    rc_state->at_0x00 <<= 8;
  }

  *src_length = rc_src_size;
  return count;
}

bool nsmbw_compress_lrc_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  (void)params;

  const uint8_t *const src_start = src;
  const uint8_t *const src_end = src + src_length;
  const uint8_t *const dst_start = dst;

  if (src + sizeof(uint32_t) > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed run-length file");
    return false;
  }

  const uint32_t header = ncutil_read_le_u32(src, 0);
  const uint8_t option = header & 0xF;
  uint32_t read_size = header >> 8;

  if (option != 0) {
    nsmbw_compress_print_error(
        "Unknown run-length option in input data: %d (expected 0)", option);
    return false;
  }

  src += sizeof(uint32_t);

  if (read_size == 0) {
    if (src + sizeof(uint32_t) > src_end) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid compressed run-length file");
      return false;
    }
    read_size = ncutil_read_le_u32(src, 0);

    src += sizeof(uint32_t);
  }

  assert(read_size <= *dst_length);
  if (read_size > *dst_length) {
    nsmbw_compress_print_error(
        "Output buffer is too small for decompressed data in run-length file");
    return false;
  }

  const uint32_t size = read_size;
  const uint8_t *const dst_end = dst + size;

  unsigned dst_pos = 0;

  uint32_t *work_buffer =
      (uint32_t *)malloc(CX_SECURE_UNCOMPRESS_LRC_WORK_SIZE);
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LRC decompression work buffer: %s",
        strerror(errno));
    return false;
  }

  struct range_coder_info sym_info;
  range_coder_info_init(&sym_info, 9, work_buffer);

  struct range_coder_info dst_info;
  range_coder_info_init(&dst_info, 12, work_buffer + 0x400);

  struct range_coder_state state;
  range_coder_state_init(&state);

  if (src + sizeof(uint32_t) > src_end) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed run-length file");
    free(work_buffer);
    return false;
  }

  state.at_0x08 = ncutil_read_be_u32(src, 0);
  src += sizeof(uint32_t);

  while (dst_pos < size) {
    int32_t rc_data_length = src_end - src;
    int ret = range_coder_get_data(src, &sym_info, &state, &rc_data_length);
    if (ret < 0) {
      free(work_buffer);
      return false;
    }
    src += rc_data_length;

    if (ret < 0x100) {
      dst[dst_pos++] = ret;
      continue;
    }

    uint16_t ref_size = (ret & 0xff) + 3;

    rc_data_length = src_end - src;
    ret = range_coder_get_data(src, &dst_info, &state, &rc_data_length);
    if (ret < 0) {
      free(work_buffer);
      return false;
    }
    src += rc_data_length;
    uint16_t ref_offset = ret + 1;

    if (dst_pos + ref_size > size) {
      nsmbw_compress_print_error(
          "LRC reference runs past the end of the output file");
      free(work_buffer);
      return false;
    }

    if (dst_pos < ref_offset) {
      nsmbw_compress_print_error(
          "LRC reference points to before the beginning of the output file");
      free(work_buffer);
      return false;
    }

    while (ref_size--) {
      dst[dst_pos] = dst[dst_pos - ref_offset];
      ++dst_pos;
    }
  }

  if (ncutil_align_up(0x20, src - src_start) < src_length) {
    nsmbw_compress_print_warning(
        "Ignored trailing %zu bytes in file after compressed data",
        (size_t)(src_length - (src - src_start)));
  }

  free(work_buffer);
  *dst_length = size;
  return true;
}
