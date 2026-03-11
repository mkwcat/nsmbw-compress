#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Reference: EGG::Decomp::decodeSZS from ogws
bool nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  (void)params;

  size_t expand_size = ncutil_read_be_u32(src, 4);
  if (expand_size != *dst_length) {
    return false; // Output size mismatch
  }
  size_t src_index = 0x10u; // Skip header
  uint8_t bit = 0;
  uint8_t chunk;

  for (size_t dst_index = 0; dst_index < expand_size; bit >>= 1) {
    if (src_index >= src_length) {
      return false; // Ran out of input data
    }

    // Refresh code bits
    if (bit == 0) {
      bit = 0b10000000u;
      chunk = src[src_index++];
    }

    // Literal (chunk bit is set)
    if (chunk & bit) {
      dst[dst_index++] = src[src_index++];
      continue;
    }

    // Back-reference (chunk bit is not set)
    // Next bytes contain run offset, length
    if (src_index + 1 >= src_length) {
      return false; // Ran out of input data
    }
    unsigned packed = src[src_index] << 8 | src[src_index + 1];
    src_index += 2u;

    // Short runs (N <= 15 + 2) use two bytes:
    //     NF FF (N=size, F=offset)
    // Minimum run size is 2 (overhead)
    //
    // Long runs (N <= 255 + 3) use three bytes:
    //     0F FF NN (N=size, F=offset)
    // Minimum run size is 0xF (max short run) + 3 (overhead)

    if ((packed & 0x0FFFu) > dst_index) {
      return false; // Run would reference before start of output
    }
    unsigned run_index = dst_index - (packed & 0x0FFFu);
    unsigned run_length = (packed >> 12) == 0
                              ? src[src_index++] + 0xF + 3 // Long run
                              : (packed >> 12) + 2;        // Short run

    if (expand_size - dst_index < run_length) {
      return false; // Run would exceed output buffer
    }

    for (; run_length > 0; run_length--, dst_index++, run_index++) {
      dst[dst_index] = dst[run_index - 1];
    }
  }

  return true;
}

static void nintendo_compute_skip_table(uint16_t *skip_table,
                                        const uint8_t *needle,
                                        int needle_size) {
  for (int i = 0; i < 256; ++i) {
    skip_table[i] = needle_size;
  }
  for (int i = 0; i < needle_size; ++i) {
    skip_table[needle[i]] = needle_size - i - 1;
  }
}

static int nintendo_search_window(const uint8_t *needle, int needle_size,
                                  const uint8_t *haystack, int haystack_size) {
  int it_haystack, it_needle, skip;

  if (needle_size > haystack_size) {
    return haystack_size;
  }
  uint16_t skip_table[256];
  nintendo_compute_skip_table(skip_table, needle, needle_size);

  // Scan forwards for the last character in the needle
  for (it_haystack = needle_size - 1;;) {
    while (true) {
      if (needle[needle_size - 1] == haystack[it_haystack]) {
        break;
      }
      it_haystack += skip_table[haystack[it_haystack]];
    }
    --it_haystack;
    it_needle = needle_size - 2;
    break;
  difference:
    // The entire needle was not found, continue search
    skip = skip_table[haystack[it_haystack]];
    if (needle_size - it_needle > skip)
      skip = needle_size - it_needle;
    it_haystack += skip;
  }

  // Scan backwards for the first difference
  int remaining_bytes = it_needle;
  for (int j = 0; j <= remaining_bytes; ++j) {
    if (haystack[it_haystack] != needle[it_needle]) {
      goto difference;
    }
    --it_haystack;
    --it_needle;
  }
  return it_haystack + 1;
}

static void nintendo_find_match(const uint8_t *src, int src_pos, int max_size,
                                int *match_offset, int *match_size) {
  // SZS backreference types:
  // (2 bytes) N >= 2:  NR RR    -> max_match_size=16+2,    window_offset=4096+1
  // (3 bytes) N >= 18: 0R RR NN -> max_match_size=0xFF+18, window_offset=4096+1
  int window = src_pos > 4096 ? src_pos - 4096 : 0;
  int window_size = 3;
  int max_match_size = (max_size - src_pos) <= 273 ? max_size - src_pos : 273;
  if (max_match_size < 3) {
    *match_size = 0;
    *match_offset = 0;
    return;
  }

  int window_offset;
  int found_match_offset;
  while (window < src_pos &&
         (window_offset = nintendo_search_window(
              &src[src_pos], window_size, &src[window],
              src_pos + window_size - window)) < src_pos - window) {
    for (; window_size < max_match_size; ++window_size) {
      if (src[window + window_offset + window_size] !=
          src[src_pos + window_size])
        break;
    }
    if (window_size == max_match_size) {
      *match_offset = window + window_offset;
      *match_size = max_match_size;
      return;
    }
    found_match_offset = window + window_offset;
    ++window_size;
    window += window_offset + 1;
  }
  *match_offset = found_match_offset;
  *match_size = window_size > 3 ? window_size - 1 : 0;
}

static bool nintendo_encode_szs(const uint8_t *src, uint8_t *dst,
                                size_t src_length, size_t *dst_length) {
  dst[16] = 0;
  int src_pos = 0;
  uint8_t group_header_bit_raw = 0x80;
  int group_header_pos = 16;
  int dst_pos = 17;
  while (src_pos < src_length) {
    int match_offset;
    int first_match_len;
    nintendo_find_match(src, src_pos, src_length, &match_offset,
                        &first_match_len);
    if (first_match_len > 2) {
      int second_match_offset;
      int second_match_len;
      nintendo_find_match(src, src_pos + 1, src_length, &second_match_offset,
                          &second_match_len);
      if (first_match_len + 1 < second_match_len) {
        // Put a single byte
        dst[group_header_pos] |= group_header_bit_raw;
        group_header_bit_raw = group_header_bit_raw >> 1;
        dst[dst_pos++] = src[src_pos++];
        if (!group_header_bit_raw) {
          group_header_bit_raw = 0x80;
          group_header_pos = dst_pos;
          dst[dst_pos++] = 0;
        }
        // Use the second match
        first_match_len = second_match_len;
        match_offset = second_match_offset;
      }
      match_offset = src_pos - match_offset - 1;
      if (first_match_len < 18) {
        match_offset |= ((first_match_len - 2) << 12);
        dst[dst_pos] = match_offset >> 8;
        dst[dst_pos + 1] = match_offset;
        dst_pos += 2;
      } else {
        dst[dst_pos] = match_offset >> 8;
        dst[dst_pos + 1] = match_offset;
        dst[dst_pos + 2] = first_match_len - 18;
        dst_pos += 3;
      }
      src_pos += first_match_len;
    } else {
      // Put a single byte
      dst[group_header_pos] |= group_header_bit_raw;
      dst[dst_pos++] = src[src_pos++];
    }

    // Write next group header
    group_header_bit_raw >>= 1;
    if (!group_header_bit_raw) {
      group_header_bit_raw = 0x80;
      group_header_pos = dst_pos;
      dst[dst_pos++] = 0;
    }
  }

  *dst_length = dst_pos;
  return true;
}

// Reference: EGG::encodeSZS from mkw_old
bool nsmbw_compress_szs_encode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  if (*dst_length < 0x10) {
    nsmbw_compress_print_error("Output buffer is too small for Yaz0 header");
    return false;
  }

  dst[0] = 'Y';
  dst[1] = 'a';
  dst[2] = 'z';
  dst[3] = '0';
  dst[4] = src_length >> 24;
  dst[5] = src_length >> 16;
  dst[6] = src_length >> 8;
  dst[7] = src_length;
  memset(dst + 8, 0, 8); // Unused padding bytes

  return nintendo_encode_szs(src, dst, src_length, dst_length);
}
