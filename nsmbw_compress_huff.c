#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_huff_decode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  (void)params;

  CXSecureResult result = CXSecureUncompressHuffman(src, src_length, dst);
  if (result != CX_SECURE_ERR_OK) {
    nsmbw_compress_print_cx_error(true, result);
    return false;
  }
  return true;
}

bool nsmbw_compress_huff_encode(
    const uint8_t *src, uint8_t *dst, size_t src_length, size_t *dst_length,
    const struct nsmbw_compress_parameters *params) {
  void *work_buffer =
      malloc(CX_COMPRESS_HUFFMAN_WORK_SIZE(params->huff_bit_size));
  if (work_buffer == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for Huffman compression work buffer: %s",
        strerror(errno));
    return false;
  }

  *dst_length = CXCompressHuffman(src, src_length, dst, params->huff_bit_size,
                                  work_buffer);

  free(work_buffer);

  return *dst_length != 0;
}
