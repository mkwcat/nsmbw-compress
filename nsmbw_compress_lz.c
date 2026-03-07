#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_lz_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  CXSecureResult result = CXSecureUncompressLZ(src, src_length, dst);
  if (result != CX_SECURE_ERR_OK) {
    nsmbw_compress_print_cx_error(true, result);
    return false;
  }
  return true;
}

bool nsmbw_compress_lz_encode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  void *work_buffer = malloc(CX_COMPRESS_LZ_WORK_SIZE);
  if (work_buffer == nullptr) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LZ compression work buffer: %s",
        strerror(errno));
    return false;
  }

  *dst_length = CXCompressLZImpl(src, src_length, dst, work_buffer,
                                 params->lz77_extended);
  free(work_buffer);

  return *dst_length != 0;
}
