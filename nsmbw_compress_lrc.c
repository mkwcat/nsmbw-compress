#include "cx.h"
#include "nsmbw_compress.h"
#include "nsmbw_compress_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool nsmbw_compress_lrc_decode(const uint8_t *src, uint8_t *dst,
                               size_t src_length, size_t *dst_length,
                               const struct nsmbw_compress_parameters *params) {
  (void)params;

  void *work_buffer = malloc(CX_SECURE_UNCOMPRESS_LRC_WORK_SIZE);
  if (work_buffer == nullptr) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for LRC decompression work buffer: %s",
        strerror(errno));
    return false;
  }

  CXSecureResult result =
      CXSecureUncompressLRC(src, src_length, dst, work_buffer);

  free(work_buffer);

  if (result != CX_SECURE_ERR_OK) {
    nsmbw_compress_print_cx_error(true, result);
    return false;
  }
  return true;
}
