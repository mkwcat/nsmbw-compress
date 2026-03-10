#include "nsmbw_compress.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const nsmbw_compress_function compress_functions[][2] = {
    [nsmbw_compress_type_lz] = {nsmbw_compress_lz_encode,
                                nsmbw_compress_lz_decode},
    [nsmbw_compress_type_huff] = {nsmbw_compress_huff_encode,
                                  nsmbw_compress_huff_decode},
    [nsmbw_compress_type_rl] = {nsmbw_compress_rl_encode,
                                nsmbw_compress_rl_decode},
    [nsmbw_compress_type_lh] = {nsmbw_compress_lh_encode,
                                nsmbw_compress_lh_decode},
    [nsmbw_compress_type_lrc] = {NULL, nsmbw_compress_lrc_decode},
    [nsmbw_compress_type_filter_diff] = {NULL,
                                         nsmbw_compress_filter_diff_decode},
    [nsmbw_compress_type_szs] = {nsmbw_compress_szs_encode,
                                 nsmbw_compress_szs_decode},
};

static const char *compression_type_names[] = {
    [nsmbw_compress_type_lz] = "lz",
    [nsmbw_compress_type_huff] = "huff",
    [nsmbw_compress_type_rl] = "rl",
    [nsmbw_compress_type_lh] = "lh",
    [nsmbw_compress_type_lrc] = "lrc",
    [nsmbw_compress_type_filter_diff] = "filter-diff",
    [nsmbw_compress_type_szs] = "szs",
};

#define BUFFER_SIZE 0x100

static void *generated_uncompressed_data;
static void *compressed_data;
static void *decompressed_data;

static void run_compression_tests() {
  for (size_t i = 0;
       i < sizeof(compress_functions) / sizeof(compress_functions[0]); ++i) {
    const char *type_name = compression_type_names[i];
    const nsmbw_compress_function encode_func = compress_functions[i][0];
    const nsmbw_compress_function decode_func = compress_functions[i][1];

    if (encode_func == NULL || decode_func == NULL) {
      continue;
    }

    printf("Testing compression type: %s\n", type_name);

    size_t compressed_size;
    struct nsmbw_compress_parameters params = {
        .huff_bit_size = 8,
        .lz77_extended = false,
    };
    if (!encode_func(generated_uncompressed_data, compressed_data, BUFFER_SIZE,
                     &compressed_size, &params)) {
      printf("  Compression failed\n");
      exit(EXIT_FAILURE);
    }

    memset(decompressed_data, (*(uint8_t *)generated_uncompressed_data) ^ 0xFF,
           BUFFER_SIZE);

    size_t decompressed_size = BUFFER_SIZE;
    if (!decode_func(compressed_data, decompressed_data, compressed_size,
                     &decompressed_size, &params)) {
      printf("  Decompression failed\n");
      exit(EXIT_FAILURE);
    }

    if (memcmp(generated_uncompressed_data, decompressed_data, BUFFER_SIZE) !=
        0) {
      printf("  Decompressed data does not match original uncompressed data\n");
      exit(EXIT_FAILURE);
    } else {
      printf("  Compression and decompression successful\n");
    }
  }
}

int nsmbw_compress_test() {
  generated_uncompressed_data = malloc(BUFFER_SIZE);
  compressed_data = malloc(0x1000 + BUFFER_SIZE * 4);
  decompressed_data = malloc(BUFFER_SIZE);

  for (size_t i = 0; i < BUFFER_SIZE; ++i) {
    ((uint8_t *)generated_uncompressed_data)[i] = i & 0xFF;
  }
  run_compression_tests();
  for (size_t i = 0; i < BUFFER_SIZE; ++i) {
    ((uint8_t *)generated_uncompressed_data)[i] = (-i) & 0xFF;
  }
  run_compression_tests();
  for (size_t i = 0; i < 16; ++i) {
    memset(generated_uncompressed_data, i, BUFFER_SIZE);
    run_compression_tests();
  }

  free(generated_uncompressed_data);
  free(compressed_data);
  free(decompressed_data);

  return EXIT_SUCCESS;
}