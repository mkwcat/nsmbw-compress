#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reference: EGG::Decomp::decodeSZS from ogws
bool nsmbw_compress_szs_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t dst_length) {
  size_t expand_size = src[4] << 24 | src[5] << 16 | src[6] << 8 | src[7];
  if (expand_size != dst_length) {
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
