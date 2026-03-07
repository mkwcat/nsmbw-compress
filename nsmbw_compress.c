#include "nsmbw_compress.h"
#include "cx.h"
#include "macros.h"
#include "types.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum nsmbw_compress_type {
  nsmbw_compress_type_lz,
  nsmbw_compress_type_lzex,
  nsmbw_compress_type_huff,
  nsmbw_compress_type_rl,
  nsmbw_compress_type_lh,
  nsmbw_compress_type_lrc,
  nsmbw_compress_type_filter_diff,
  nsmbw_compress_type_szs,
  nsmbw_compress_type_szp,
};

struct nsmbw_compress_argument {
  char short_name;
  const char *long_name;
  const char *description;
  int long_name_length;
  short index;
  enum nsmbw_compress_argument_type {
    nsmbw_compress_argument_type_bool,
    nsmbw_compress_argument_type_int,
    nsmbw_compress_argument_type_string,
    nsmbw_compress_argument_type_path
  } type;
};

union nsmbw_compress_argument_value {
  bool bool_value;
  int int_value;
  const char *string_value;
};

static const struct nsmbw_compress_argument arguments[] = {
    {
        .short_name = 'h',
        .long_name = "help",
        .long_name_length = sizeof("help") - 1,
        .description = "Show this help message and exit",
        .type = nsmbw_compress_argument_type_bool,
        .index = 0,
#define argument_index_help 0
    },
    {
        .short_name = 'o',
        .long_name = "output",
        .long_name_length = sizeof("output") - 1,
        .description = "Specify the output file",
        .type = nsmbw_compress_argument_type_path,
        .index = 1,
#define argument_index_output 1
    },
    {
        .short_name = 't',
        .long_name = "type",
        .long_name_length = sizeof("type") - 1,
        .description = "Specify the compression type (lz, lzex, rl, huff, lh, "
                       "lrc, filter-diff, szs, szp)",
        .type = nsmbw_compress_argument_type_string,
        .index = 2,
#define argument_index_type 2
    },
    {
        .short_name = 'x',
        .long_name = "uncomp",
        .long_name_length = sizeof("uncomp") - 1,
        .description = "Decompress the input file instead of compressing",
        .type = nsmbw_compress_argument_type_bool,
        .index = 3,
#define argument_index_uncomp 3
    },
    {
        .short_name = 'b',
        .long_name = "bitsize",
        .long_name_length = sizeof("bitsize") - 1,
        .description =
            "Specify the bit size for Huffman compression (4 or 8, default: 8)",
        .type = nsmbw_compress_argument_type_int,
        .index = 4,
#define argument_index_bitsize 4
    },
    {
        .short_name = 'v',
        .long_name = "verbose",
        .long_name_length = sizeof("verbose") - 1,
        .description = "Print verbose output",
        .type = nsmbw_compress_argument_type_bool,
        .index = 5,
#define argument_index_verbose 5
    },
};

static const size_t argument_count = ARRAY_LENGTH(arguments);

static union nsmbw_compress_argument_value
    argument_values[ARRAY_LENGTH(arguments)];
static bool argument_specified[ARRAY_LENGTH(arguments)] = {0};

static const char *input_file_path = nullptr;

static const char *executable_name = "nsmbw-compress";

// Weird way of doing this, anything here will get freed by cleanup_disposable()
// on exit.
static void *disposable_buffers[16] = {};

static void *malloc_disposable(size_t size) {
  for (size_t i = 0; i < ARRAY_LENGTH(disposable_buffers); ++i) {
    if (disposable_buffers[i] == nullptr) {
      return disposable_buffers[i] = malloc(size);
    }
  }
  errno = ENOMEM;
  return nullptr;
}

static int cleanup_disposable(int result) {
  for (size_t i = 0; i < ARRAY_LENGTH(disposable_buffers); ++i) {
    free(disposable_buffers[i]);
    disposable_buffers[i] = nullptr;
  }
  return result;
}

static const char *str_compress_type(enum nsmbw_compress_type type) {
  switch (type) {
  case nsmbw_compress_type_lz:
    return "lz";
  case nsmbw_compress_type_lzex:
    return "lzex";
  case nsmbw_compress_type_huff:
    return "huff";
  case nsmbw_compress_type_rl:
    return "rl";
  case nsmbw_compress_type_lh:
    return "lh";
  case nsmbw_compress_type_lrc:
    return "lrc";
  case nsmbw_compress_type_filter_diff:
    return "filter-diff";
  case nsmbw_compress_type_szs:
    return "szs";
  case nsmbw_compress_type_szp:
    return "szp";

  default:
    return "unknown";
  }
}

static enum nsmbw_compress_type compress_type_from_str(const char *str) {
  if (strcmp(str, "lz") == 0) {
    return nsmbw_compress_type_lz;
  } else if (strcmp(str, "lzex") == 0) {
    return nsmbw_compress_type_lzex;
  } else if (strcmp(str, "huff") == 0) {
    return nsmbw_compress_type_huff;
  } else if (strcmp(str, "rl") == 0) {
    return nsmbw_compress_type_rl;
  } else if (strcmp(str, "lh") == 0) {
    return nsmbw_compress_type_lh;
  } else if (strcmp(str, "lrc") == 0) {
    return nsmbw_compress_type_lrc;
  } else if (strcmp(str, "filter-diff") == 0) {
    return nsmbw_compress_type_filter_diff;
  } else if (strcmp(str, "szs") == 0) {
    return nsmbw_compress_type_szs;
  } else if (strcmp(str, "szp") == 0) {
    return nsmbw_compress_type_szp;
  } else {
    return -1; // Invalid type
  }
}

static const char *str_default_extension(enum nsmbw_compress_type type) {
  switch (type) {
  case nsmbw_compress_type_lz:
    return ".LZ";
  case nsmbw_compress_type_lzex:
    return ".LZ";
  case nsmbw_compress_type_huff:
    return ".HUFF";
  case nsmbw_compress_type_rl:
    return ".RL";
  case nsmbw_compress_type_lh:
    return ".LH";
  case nsmbw_compress_type_lrc:
    return ".LRC";
  case nsmbw_compress_type_filter_diff:
    return ".DIFF";
  case nsmbw_compress_type_szs:
    return ".szs";
  case nsmbw_compress_type_szp:
    return ".szp";
  default:
    return nullptr;
  }
}

static const struct nsmbw_compress_argument *
find_long_argument(const char *long_name) {
  size_t i;
  for (i = 0; i < argument_count; i++) {
    if (strcmp(long_name, arguments[i].long_name) == 0) {
      return &arguments[i];
    }
  }
  return nullptr;
}

static const struct nsmbw_compress_argument *
find_short_argument(char short_name) {
  size_t i;
  for (i = 0; i < argument_count; i++) {
    if (short_name == arguments[i].short_name) {
      return &arguments[i];
    }
  }
  return nullptr;
}

static void print_error(const char *message, ...) {
  va_list args;
  va_start(args, message);
  fprintf(stderr, "%s: ", executable_name);
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void print_warning(const char *message, ...) {
  va_list args;
  va_start(args, message);
  fprintf(stderr, "%s: warning: ", executable_name);
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void print_verbose(const char *message, ...) {
  if (!argument_specified[argument_index_verbose]) {
    return;
  }
  va_list args;
  va_start(args, message);
  fprintf(stdout, "%s: debug: ", executable_name);
  vfprintf(stdout, message, args);
  fprintf(stdout, "\n");
  va_end(args);
}

static int exit_print_help() {
  puts("Usage: nsmbw-compress [options] <input> [-o output]");
  puts("Options:");
  for (size_t i = 0; i < argument_count; i++) {
    const struct nsmbw_compress_argument *arg = &arguments[i];
    printf("  -%c, --%-8s %s\n", arg->short_name, arg->long_name,
           arg->description);
  }
  return EXIT_FAILURE;
}

static int exit_unknown_argument(const char *arg) {
  print_error("Unknown argument: %s", arg);
  print_error("Use --help to see available options.");
  return EXIT_FAILURE;
}

static bool parse_argument_value(const struct nsmbw_compress_argument *arg_info,
                                 const char *value_str) {
  argument_specified[arg_info->index] = true;
  union nsmbw_compress_argument_value *value_ptr =
      &argument_values[arg_info->index];
  switch (arg_info->type) {
  case nsmbw_compress_argument_type_bool:
    assert(false && "Boolean arguments should not have a value");
  case nsmbw_compress_argument_type_int:
    value_ptr->int_value = atoi(value_str);
    return true;
  case nsmbw_compress_argument_type_string:
  case nsmbw_compress_argument_type_path:
    value_ptr->string_value = value_str;
    return true;
  default:
    print_error("Unknown argument type for argument %s", arg_info->long_name);
    return false;
  }
}

static bool parse_arguments(int argc, const char *const *argv) {
  for (int arg_num = 1; arg_num < argc; arg_num++) {
    const char *arg = argv[arg_num];
    if (arg[0] == '-' && arg[1] == '-') {
      // Long argument name
      const struct nsmbw_compress_argument *long_arg =
          find_long_argument(arg + 2);
      if (long_arg == nullptr) {
        return exit_unknown_argument(arg);
      }
      if (long_arg->type == nsmbw_compress_argument_type_bool) {
        // Boolean flags don't take a value, just set to true
        argument_specified[long_arg->index] = true;
        argument_values[long_arg->index].bool_value = true;
        continue;
      }
      if (argument_specified[long_arg->index]) {
        print_error("Argument specified multiple times: --%s",
                    long_arg->long_name);
        return false;
      }
      const char *value_str;
      if (arg[2 + long_arg->long_name_length] == '=') {
        // --arg=value
        value_str = arg + 2 + long_arg->long_name_length + 1;
      } else {
        // --arg value
        if (arg_num + 1 >= argc) {
          print_error("Missing value for argument: %s", arg);
          return false;
        }
        value_str = argv[++arg_num];
      }
      if (!parse_argument_value(long_arg, value_str)) {
        return false;
      }
    } else if (arg[0] == '-' && arg[1]) {
      // Short argument name
      const struct nsmbw_compress_argument *short_arg =
          find_short_argument(arg[1]);
      if (short_arg == nullptr) {
        return exit_unknown_argument(arg);
      }
      if (short_arg->type == nsmbw_compress_argument_type_bool) {
        // Boolean flags don't take a value, just set to true
        argument_specified[short_arg->index] = true;
        argument_values[short_arg->index].bool_value = true;
        continue;
      }
      if (argument_specified[short_arg->index]) {
        print_error("Argument specified multiple times: -%c",
                    short_arg->short_name);
        return false;
      }
      const char *value_str;
      if (arg[2]) {
        // -avalue
        value_str = arg + 2;
      } else {
        // -a value
        if (arg_num + 1 >= argc) {
          print_error("Missing value for argument: %s", arg);
          return false;
        }
        value_str = argv[++arg_num];
      }
      if (!parse_argument_value(short_arg, value_str)) {
        return false;
      }
    } else {
      // Positional argument (input file)
      if (input_file_path != nullptr) {
        print_error("Multiple input files specified: %s", arg);
        return false;
      }
      input_file_path = arg;
    }
  }
  return true;
}

static bool get_uncompress_info(const void *input_data, size_t input_size,
                                size_t *expanded_size,
                                enum nsmbw_compress_type *compression_type) {
  if (input_size < 4) {
    print_error("Input file is too small to be a valid compressed file");
    return false;
  }

  if (memcmp(input_data, "Yaz0", 4) == 0) {
    *compression_type = nsmbw_compress_type_szs;
    if (input_size < 8) {
      print_error("Input file is too small to be a valid SZS/Yaz0 file");
      return false;
    }
    *expanded_size = nsmbw_compress_util_read_be_u32(input_data, 4);
    return true;
  }
  if (memcmp(input_data, "Yay0", 4) == 0) {
    *compression_type = nsmbw_compress_type_szp;
    if (input_size < 8) {
      print_error("Input file is too small to be a valid SZP/Yay0 file");
      return false;
    }
    *expanded_size = nsmbw_compress_util_read_be_u32(input_data, 4);
    return true;
  }

  uint32_t header = nsmbw_compress_util_read_le_u32(input_data, 0);
  CXCompressionType cx_type = header & CX_COMPRESSION_TYPE_MASK;
  switch (cx_type) {
  case CX_COMPRESSION_TYPE_LEMPEL_ZIV:
    *compression_type = nsmbw_compress_type_lz;
    break;
  case CX_COMPRESSION_TYPE_LEMPEL_ZIV | 0x1:
    *compression_type = nsmbw_compress_type_lzex;
    break;
  case CX_COMPRESSION_TYPE_HUFFMAN:
    *compression_type = nsmbw_compress_type_huff;
    break;
  case CX_COMPRESSION_TYPE_RUN_LENGTH:
    *compression_type = nsmbw_compress_type_rl;
    break;
  case CX_COMPRESSION_TYPE_LH:
    *compression_type = nsmbw_compress_type_lh;
    break;
  case CX_COMPRESSION_TYPE_LRC:
    *compression_type = nsmbw_compress_type_lrc;
    break;
  case CX_COMPRESSION_TYPE_FILTER_DIFF:
    *compression_type = nsmbw_compress_type_filter_diff;
    break;
  default:
    print_error("Couldn't determine compression type of input file");
    return false;
  }

  *expanded_size = header >> 8;
  return true;
}

static int main_uncompress(const void *input_file, size_t input_file_size,
                           void **output_data, size_t *output_size) {
  if (argument_specified[argument_index_type]) {
    print_warning("--type has no effect when --uncomp is specified");
  }

  size_t expanded_size;
  enum nsmbw_compress_type compression_type;
  if (!get_uncompress_info(input_file, input_file_size, &expanded_size,
                           &compression_type)) {
    return EXIT_FAILURE;
  }

  print_verbose("Type: %s, Expanded size: %zu bytes",
                str_compress_type(compression_type), expanded_size);

  if (!argument_specified[argument_index_output]) {
    print_verbose(
        "No output file specified, automatically determining output file name");
    // Automatically determine output file name by removing known compression
    // extensions
    const char *extension = str_default_extension(compression_type);

    assert(input_file_path);
    size_t input_path_len = strlen(input_file_path);
    char *output_path = nullptr;
    if (extension && input_path_len > strlen(extension) &&
        strcasecmp(input_file_path + input_path_len - strlen(extension),
                   extension) == 0) {
      size_t output_path_len = input_path_len - strlen(extension);
      output_path = (char *)malloc_disposable(output_path_len + 1);
      if (output_path == nullptr) {
        print_error("Failed to allocate memory for output file path: %s",
                    strerror(errno));
        return EXIT_FAILURE;
      }
      memcpy(output_path, input_file_path, output_path_len);
      output_path[output_path_len] = '\0';
    } else {
      print_warning(
          "Output doesn't have standard extension, using default: *.bin");
      output_path = (char *)malloc_disposable(strlen(input_file_path) + 5);
      if (output_path == nullptr) {
        print_error("Failed to allocate memory for output file path: %s",
                    strerror(errno));
        return EXIT_FAILURE;
      }
      strcpy(output_path, input_file_path);
      strcat(output_path, ".bin");
    }
    // We don't want to overwrite a file that already exists
    print_verbose("Determined output path: %s", output_path);
    FILE *test_file = fopen(output_path, "rb");
    if (test_file != nullptr) {
      print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  void *uncompressed_data = malloc_disposable(expanded_size);
  if (uncompressed_data == nullptr) {
    print_error("Failed to allocate memory for decompressed data: %s",
                strerror(errno));
    return EXIT_FAILURE;
  }

  void *work_buffer = nullptr;
  CXSecureResult result;
  switch (compression_type) {
  case nsmbw_compress_type_lz:
  case nsmbw_compress_type_lzex:
    print_verbose("CXSecureUncompressLZ()...");
    result =
        CXSecureUncompressLZ(input_file, input_file_size, uncompressed_data);
    break;
  case nsmbw_compress_type_huff:
    print_verbose("CXSecureUncompressHuffman()...");
    result = CXSecureUncompressHuffman(input_file, input_file_size,
                                       uncompressed_data);
    break;
  case nsmbw_compress_type_rl:
    print_verbose("CXSecureUncompressRL()...");
    result =
        CXSecureUncompressRL(input_file, input_file_size, uncompressed_data);
    break;
  case nsmbw_compress_type_lh:
    print_verbose("CXSecureUncompressLH()...");
    work_buffer = malloc(CX_SECURE_UNCOMPRESS_LH_WORK_SIZE);
    if (work_buffer == nullptr) {
      print_error(
          "Failed to allocate memory for LH decompression work buffer: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    result = CXSecureUncompressLH(input_file, input_file_size,
                                  uncompressed_data, work_buffer);
    break;
  case nsmbw_compress_type_lrc:
    print_verbose("CXSecureUncompressLRC()...");
    work_buffer = malloc(CX_SECURE_UNCOMPRESS_LRC_WORK_SIZE);
    if (work_buffer == nullptr) {
      print_error(
          "Failed to allocate memory for LRC decompression work buffer: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    result = CXSecureUncompressLRC(input_file, input_file_size,
                                   uncompressed_data, work_buffer);
    break;
  case nsmbw_compress_type_filter_diff:
    print_verbose("CXSecureUnfilterDiff()...");
    result =
        CXSecureUnfilterDiff(input_file, input_file_size, uncompressed_data);
    break;
  case nsmbw_compress_type_szs:
    result = nsmbw_compress_szs_decode(input_file, uncompressed_data,
                                       input_file_size, expanded_size)
                 ? CXSECURE_ESUCCESS
                 : CXSECURE_EBADTYPE;
    break;
  case nsmbw_compress_type_szp:
    (void)nsmbw_compress_szp_decode(input_file, uncompressed_data,
                                    input_file_size, expanded_size);
    result = CXSECURE_ESUCCESS; // nsmbw_compress_szp_decode doesn't have error
                                // handling, assume success
    break;
  default:
    assert(false && "Unknown compressed format");
  }

  print_verbose("CXSecureResult: %d", result);

  if (work_buffer) {
    free(work_buffer);
  }

  if (result != CXSECURE_ESUCCESS) {
    print_error("Decompression failed with error code: %d", result);
    return EXIT_FAILURE;
  }

  *output_data = uncompressed_data;
  *output_size = expanded_size;
  return EXIT_SUCCESS;
}

static int main_compress(const void *input_file, size_t input_file_size,
                         void **output_data, size_t *output_size) {
  if (!argument_specified[argument_index_type]) {
    print_warning("No compression --type specified, defaulting to lz");
    argument_specified[argument_index_type] = true;
    argument_values[argument_index_type].string_value = "lz";
  }
  enum nsmbw_compress_type compression_type;
  const char *type_str = argument_values[argument_index_type].string_value;
  compression_type = compress_type_from_str(type_str);
  if (compression_type == -1) {
    print_error("Unknown compression type: %s", type_str);
    return EXIT_FAILURE;
  }

  print_verbose("Compression type: %s", str_compress_type(compression_type));

  if (!argument_specified[argument_index_output]) {
    print_verbose(
        "No output file specified, automatically determining output file name");
    // Automatically determine output file name by adding known compression
    // extensions
    const char *extension = str_default_extension(compression_type);

    assert(input_file_path);
    size_t input_path_len = strlen(input_file_path);
    char *output_path =
        (char *)malloc_disposable(input_path_len + strlen(extension) + 1);
    if (output_path == nullptr) {
      print_error("Failed to allocate memory for output file path: %s",
                  strerror(errno));
      return EXIT_FAILURE;
    }
    strcpy(output_path, input_file_path);
    strcat(output_path, extension);

    // We don't want to overwrite a file that already exists
    print_verbose("Determined output path: %s", output_path);
    FILE *test_file = fopen(output_path, "rb");
    if (test_file != nullptr) {
      print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  void *compressed_data = malloc_disposable(input_file_size * 4);
  if (compressed_data == nullptr) {
    print_error("Failed to allocate memory for compressed data: %s",
                strerror(errno));
    return EXIT_FAILURE;
  }

  u32 length = 0;
  void *work_buffer = nullptr;
  switch (compression_type) {
  case nsmbw_compress_type_lz:
  case nsmbw_compress_type_lzex:
    print_verbose("CXCompressLZ()...");
    work_buffer = malloc(CX_COMPRESS_LZ_WORK_SIZE);
    if (work_buffer == nullptr) {
      print_error(
          "Failed to allocate memory for LZ compression work buffer: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    length = CXCompressLZImpl(input_file, input_file_size, compressed_data,
                              work_buffer,
                              compression_type == nsmbw_compress_type_lzex);
    break;
  case nsmbw_compress_type_lh:
    print_verbose("CXCompressLH()...");
    work_buffer = malloc(CX_COMPRESS_LH_WORK_SIZE + input_file_size * 3);
    if (work_buffer == nullptr) {
      print_error(
          "Failed to allocate memory for LH compression work buffer: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    length = CXCompressLH(input_file, input_file_size, compressed_data,
                          work_buffer + CX_COMPRESS_LH_WORK_SIZE, work_buffer);
    break;
  case nsmbw_compress_type_rl:
    print_verbose("CXCompressRL()...");
    length = CXCompressRL(input_file, input_file_size, compressed_data);
    break;
  case nsmbw_compress_type_huff:
    print_verbose("CXCompressHuff()...");
    work_buffer = malloc(CX_COMPRESS_HUFFMAN_WORK_SIZE(
        argument_values[argument_index_bitsize].int_value));
    if (work_buffer == nullptr) {
      print_error(
          "Failed to allocate memory for Huffman compression work buffer: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    length = CXCompressHuffman(
        input_file, input_file_size, compressed_data,
        argument_values[argument_index_bitsize].int_value, work_buffer);
    break;
  default:
    print_error("Compression type not supported for compression: %s",
                str_compress_type(compression_type));
    return EXIT_FAILURE;
  }

  if (work_buffer) {
    free(work_buffer);
  }

  if (length == 0) {
    print_error("Compression failed");
    return EXIT_FAILURE;
  }

  *output_data = compressed_data;
  *output_size = length;
  return EXIT_SUCCESS;
}

int main(int argc, const char *const *argv) {
  if (argc > 0 && argv[0]) {
    executable_name = strrchr(argv[0], '/');
    if (executable_name) {
      executable_name++;
    } else {
      executable_name = argv[0];
    }
  }

  if (!parse_arguments(argc, argv)) {
    return cleanup_disposable(EXIT_FAILURE);
  }

  if (argument_specified[argument_index_help]) {
    return cleanup_disposable(exit_print_help());
  }
  if (!argument_specified[argument_index_bitsize]) {
    argument_values[argument_index_bitsize].int_value = 8;
  }

  if (input_file_path == nullptr) {
    (void)exit_print_help();
    print_error("No input file specified.");
    return cleanup_disposable(EXIT_FAILURE);
  }
  print_verbose("Opening input file: %s", input_file_path);
  FILE *input_file = fopen(input_file_path, "rb");
  if (input_file == nullptr) {
    print_error("Failed to open input file: %s", strerror(errno));
    return cleanup_disposable(EXIT_FAILURE);
  }
  fseek(input_file, 0, SEEK_END);
  size_t input_file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);
  void *input_file_data = malloc(input_file_size);
  if (input_file_data == nullptr) {
    print_error("Failed to allocate memory for input file: %s",
                strerror(errno));
    fclose(input_file);
    return cleanup_disposable(EXIT_FAILURE);
  }
  if (fread(input_file_data, input_file_size, 1, input_file) != 1) {
    print_error("Failed to read input file: %s", strerror(errno));
    free(input_file_data);
    fclose(input_file);
    return cleanup_disposable(EXIT_FAILURE);
  }
  fclose(input_file);

  int result;
  void *output_data;
  size_t output_size;
  if (argument_specified[argument_index_uncomp]) {
    result = main_uncompress(input_file_data, input_file_size, &output_data,
                             &output_size);
  } else {
    result = main_compress(input_file_data, input_file_size, &output_data,
                           &output_size);
  }
  free(input_file_data);

  if (result != EXIT_SUCCESS) {
    return result;
  }

  // Output file name must be written (if not provided by the user) by
  // main_compress() or main_uncompress() if they succeed
  print_verbose("Opening output file: %s",
                argument_values[argument_index_output].string_value);
  FILE *output_file =
      fopen(argument_values[argument_index_output].string_value, "wb");
  if (output_file == nullptr) {
    print_error("Failed to open output file: %s", strerror(errno));
    return cleanup_disposable(EXIT_FAILURE);
  }
  if (fwrite(output_data, output_size, 1, output_file) != 1) {
    print_error("Failed to write output file: %s", strerror(errno));
    fclose(output_file);
    return cleanup_disposable(EXIT_FAILURE);
  }
  fclose(output_file);

  print_verbose("main(): Exit success");
  return cleanup_disposable(EXIT_SUCCESS);
}