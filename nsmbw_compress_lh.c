#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_lh_decode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  (void)params;

  void *work_buffer = malloc(CX_SECURE_UNCOMPRESS_LH_WORK_SIZE);
  if (work_buffer == nullptr) {
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

bool nsmbw_compress_lh_encode(const uint8_t *src, uint8_t *dst,
                              size_t src_length, size_t *dst_length,
                              const struct nsmbw_compress_parameters *params) {
  void *work_buffer = malloc(CX_COMPRESS_LH_WORK_SIZE + src_length * 3);
  if (work_buffer == nullptr) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LH compression work buffer: %s",
        strerror(errno));
    return false;
  }

  *dst_length = CXCompressLH(
      src, src_length, dst, ((uint8_t *)work_buffer) + CX_COMPRESS_LH_WORK_SIZE,
      work_buffer);

  free(work_buffer);

  return *dst_length != 0;
}
