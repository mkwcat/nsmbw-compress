#include "nsmbw_compress.h"
#include "CXSecureUncompression.h"
#include "cx.h"
#include "macros.h"
#include "nsmbw_compress_internal.h"
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

static const nsmbw_compress_function compress_functions[][2] = {
    [nsmbw_compress_type_lz] = {nsmbw_compress_lz_encode,
                                nsmbw_compress_lz_decode},
    [nsmbw_compress_type_huff] = {nsmbw_compress_huff_encode,
                                  nsmbw_compress_huff_decode},
    [nsmbw_compress_type_rl] = {nsmbw_compress_rl_encode,
                                nsmbw_compress_rl_decode},
    [nsmbw_compress_type_lh] = {nsmbw_compress_lh_encode,
                                nsmbw_compress_lh_decode},
    [nsmbw_compress_type_lrc] = {nullptr, nsmbw_compress_lrc_decode},
    [nsmbw_compress_type_filter_diff] = {nullptr,
                                         nsmbw_compress_filter_diff_decode},
    [nsmbw_compress_type_szs] = {nullptr, nsmbw_compress_szs_decode},
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

static const char *compression_default_extensions[] = {
    [nsmbw_compress_type_lz] = ".LZ",
    [nsmbw_compress_type_huff] = ".HUFF",
    [nsmbw_compress_type_rl] = ".RL",
    [nsmbw_compress_type_lh] = ".LH",
    [nsmbw_compress_type_lrc] = ".LRC",
    [nsmbw_compress_type_filter_diff] = ".DIFF",
    [nsmbw_compress_type_szs] = ".szs",
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
        .description = "Specify the output file name",
        .type = nsmbw_compress_argument_type_path,
        .index = 1,
#define argument_index_output 1
    },
    {
        .short_name = 't',
        .long_name = "type",
        .long_name_length = sizeof("type") - 1,
        .description =
            "Specify the compression type (see supported types below)",
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
        .short_name = 'l',
        .long_name = "old-lz77",
        .long_name_length = sizeof("old-lz77") - 1,
        .description = "Use the older low-efficiency mode of LZ77",
        .type = nsmbw_compress_argument_type_bool,
        .index = 5,
#define argument_index_old_lz77 5
    },
    {
        .short_name = '\0',
        .long_name = "test",
        .long_name_length = sizeof("test") - 1,
        .description = "Run internal tests and exit",
        .type = nsmbw_compress_argument_type_bool,
        .index = 6,
#define argument_index_test 6
    },
    {
        .short_name = 'v',
        .long_name = "verbose",
        .long_name_length = sizeof("verbose") - 1,
        .description = "Print verbose output",
        .type = nsmbw_compress_argument_type_bool,
        .index = 7,
#define argument_index_verbose 7
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

static enum nsmbw_compress_type compress_type_from_str(const char *str) {
  if (strcmp(str, "lz") == 0) {
    return nsmbw_compress_type_lz;
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
  } else {
    return -1; // Invalid type
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

void nsmbw_compress_print_error(const char *message, ...) {
  va_list args;
  va_start(args, message);
  fprintf(stderr, "%s: ", executable_name);
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void nsmbw_compress_print_warning(const char *message, ...) {
  va_list args;
  va_start(args, message);
  fprintf(stderr, "%s: warning: ", executable_name);
  vfprintf(stderr, message, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void nsmbw_compress_print_verbose(const char *message, ...) {
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

void nsmbw_compress_print_cx_error(bool decompression, int result) {
  nsmbw_compress_print_error(
      "%s error: %d", decompression ? "Decompression" : "Compression", result);
}

static int exit_print_help() {
  puts("Usage: nsmbw-compress [options] <input> [-o output]");
  puts("Options:");
  for (size_t i = 0; i < argument_count; i++) {
    const struct nsmbw_compress_argument *arg = &arguments[i];
    if (arg->short_name) {
      printf("  -%c, --%-8s %s\n", arg->short_name, arg->long_name,
             arg->description);
    } else {
      printf("      --%-8s %s\n", arg->long_name, arg->description);
    }
  }
  printf("Supported types for compression:\n"
         "  ");
  for (size_t i = 0; i < ARRAY_LENGTH(compression_type_names); i++) {
    if (compress_functions[i][0] != nullptr) {
      printf("%s ", compression_type_names[i]);
    }
  }
  printf("\n"
         "Supported types for decompression:\n"
         "  ");
  for (size_t i = 0; i < ARRAY_LENGTH(compression_type_names); i++) {
    if (compress_functions[i][1] != nullptr) {
      printf("%s ", compression_type_names[i]);
    }
  }
  printf("\n");
  return EXIT_FAILURE;
}

static bool exit_unknown_argument(const char *arg) {
  nsmbw_compress_print_error("Unknown argument: %s", arg);
  nsmbw_compress_print_error("Use --help to see available options.");
  return false;
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
    nsmbw_compress_print_error("Unknown argument type for argument %s",
                               arg_info->long_name);
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
        nsmbw_compress_print_error("Argument specified multiple times: --%s",
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
          nsmbw_compress_print_error("Missing value for argument: %s", arg);
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
        nsmbw_compress_print_error("Argument specified multiple times: -%c",
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
          nsmbw_compress_print_error("Missing value for argument: %s", arg);
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
        nsmbw_compress_print_error("Multiple input files specified: %s", arg);
        return false;
      }
      input_file_path = arg;
    }
  }
  return true;
}

static bool get_uncompress_info(const void *input_data, size_t input_size,
                                size_t *expanded_size,
                                enum nsmbw_compress_type *compression_type,
                                bool *lz77_extended) {
  if (input_size < 4) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed file");
    return false;
  }

  if (memcmp(input_data, "Yaz0", 4) == 0) {
    *compression_type = nsmbw_compress_type_szs;
    if (input_size < 8) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid SZS/Yaz0 file");
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
    *lz77_extended = false;
    break;
  case CX_COMPRESSION_TYPE_LEMPEL_ZIV | 0x1:
    *compression_type = nsmbw_compress_type_lz;
    *lz77_extended = true;
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
    nsmbw_compress_print_error(
        "Couldn't determine compression type of input file");
    return false;
  }

  *expanded_size = header >> 8;
  return true;
}

static int main_uncompress(const void *input_file, size_t input_file_size,
                           void **output_data, size_t *output_size) {
  if (argument_specified[argument_index_type]) {
    nsmbw_compress_print_warning(
        "--type has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_bitsize]) {
    nsmbw_compress_print_warning(
        "--bitsize has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_old_lz77]) {
    nsmbw_compress_print_warning(
        "--old-lz77 has no effect when --uncomp is specified");
  }

  size_t expanded_size;
  bool lz77_extended;
  enum nsmbw_compress_type compression_type;
  if (!get_uncompress_info(input_file, input_file_size, &expanded_size,
                           &compression_type, &lz77_extended)) {
    return EXIT_FAILURE;
  }

  nsmbw_compress_print_verbose("Type: %s, Expanded size: %zu bytes",
                               compression_type_names[compression_type],
                               expanded_size);

  if (compress_functions[compression_type][1] == nullptr) {
    nsmbw_compress_print_error(
        "Compression type %s is not supported for decompression",
        compression_type_names[compression_type]);
    return EXIT_FAILURE;
  }

  if (!argument_specified[argument_index_output]) {
    nsmbw_compress_print_verbose("No output file specified, automatically "
                                 "determining output file name");
    // Automatically determine output file name by removing known compression
    // extensions
    const char *extension = compression_default_extensions[compression_type];

    assert(input_file_path);
    size_t input_path_len = strlen(input_file_path);
    char *output_path = nullptr;
    if (extension && input_path_len > strlen(extension) &&
        strcasecmp(input_file_path + input_path_len - strlen(extension),
                   extension) == 0) {
      size_t output_path_len = input_path_len - strlen(extension);
      output_path = (char *)malloc_disposable(output_path_len + 1);
      if (output_path == nullptr) {
        nsmbw_compress_print_error(
            "Failed to allocate memory for output file path: %s",
            strerror(errno));
        return EXIT_FAILURE;
      }
      memcpy(output_path, input_file_path, output_path_len);
      output_path[output_path_len] = '\0';
    } else {
      nsmbw_compress_print_warning(
          "Output doesn't have standard extension, using default: *.bin");
      output_path = (char *)malloc_disposable(strlen(input_file_path) + 5);
      if (output_path == nullptr) {
        nsmbw_compress_print_error(
            "Failed to allocate memory for output file path: %s",
            strerror(errno));
        return EXIT_FAILURE;
      }
      strcpy(output_path, input_file_path);
      strcat(output_path, ".bin");
    }
    // We don't want to overwrite a file that already exists
    nsmbw_compress_print_verbose("Determined output path: %s", output_path);
    FILE *test_file = fopen(output_path, "rb");
    if (test_file != nullptr) {
      nsmbw_compress_print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  void *uncompressed_data = malloc_disposable(expanded_size);
  if (uncompressed_data == nullptr) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for decompressed data: %s", strerror(errno));
    return EXIT_FAILURE;
  }

  static const struct nsmbw_compress_parameters default_params = {};

  bool ok = compress_functions[compression_type][1](
      input_file, uncompressed_data, input_file_size, &expanded_size,
      &default_params);
  if (!ok) {
    nsmbw_compress_print_error("Output file not written due to errors");
    return EXIT_FAILURE;
  }

  *output_data = uncompressed_data;
  *output_size = expanded_size;
  return EXIT_SUCCESS;
}

static int main_compress(const void *input_file, size_t input_file_size,
                         void **output_data, size_t *output_size) {
  if (!argument_specified[argument_index_type]) {
    nsmbw_compress_print_warning(
        "No compression --type specified, defaulting to lz");
    argument_specified[argument_index_type] = true;
    argument_values[argument_index_type].string_value = "lz";
  }
  enum nsmbw_compress_type compression_type;
  const char *type_str = argument_values[argument_index_type].string_value;
  compression_type = compress_type_from_str(type_str);
  if (compression_type == -1) {
    nsmbw_compress_print_error("Unknown compression type: %s", type_str);
    return EXIT_FAILURE;
  }

  nsmbw_compress_print_verbose("Compression type: %s",
                               compression_type_names[compression_type]);

  if (compress_functions[compression_type][0] == nullptr) {
    nsmbw_compress_print_error(
        "Compression type %s is not supported for compression",
        compression_type_names[compression_type]);
    return EXIT_FAILURE;
  }

  if (!argument_specified[argument_index_output]) {
    nsmbw_compress_print_verbose("No output file specified, automatically "
                                 "determining output file name");
    // Automatically determine output file name by adding known compression
    // extensions
    const char *extension = compression_default_extensions[compression_type];

    assert(input_file_path);
    size_t input_path_len = strlen(input_file_path);
    char *output_path =
        (char *)malloc_disposable(input_path_len + strlen(extension) + 1);
    if (output_path == nullptr) {
      nsmbw_compress_print_error(
          "Failed to allocate memory for output file path: %s",
          strerror(errno));
      return EXIT_FAILURE;
    }
    strcpy(output_path, input_file_path);
    strcat(output_path, extension);

    // We don't want to overwrite a file that already exists
    nsmbw_compress_print_verbose("Determined output path: %s", output_path);
    FILE *test_file = fopen(output_path, "rb");
    if (test_file != nullptr) {
      nsmbw_compress_print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  void *compressed_data = malloc_disposable(input_file_size * 4);
  if (compressed_data == nullptr) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for compressed data: %s", strerror(errno));
    return EXIT_FAILURE;
  }

  struct nsmbw_compress_parameters params = {
      .lz77_extended =
          argument_specified[argument_index_old_lz77]
              ? !argument_values[argument_index_old_lz77].bool_value
              : true,
      .huff_bit_size = argument_specified[argument_index_bitsize]
                           ? argument_values[argument_index_bitsize].int_value
                           : 8,
  };

  size_t dst_length = 0;
  bool ok = compress_functions[compression_type][0](
      input_file, compressed_data, input_file_size, &dst_length, &params);
  if (!ok) {
    nsmbw_compress_print_error("Output file not written due to errors");
    return EXIT_FAILURE;
  }

  *output_data = compressed_data;
  *output_size = dst_length;
  return EXIT_SUCCESS;
}

extern int nsmbw_compress_test();

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
  if (argument_specified[argument_index_test]) {
    return cleanup_disposable(nsmbw_compress_test());
  }

  if (input_file_path == nullptr) {
    nsmbw_compress_print_error("No input file specified.");
    return cleanup_disposable(exit_print_help());
  }
  nsmbw_compress_print_verbose("Opening input file: %s", input_file_path);
  FILE *input_file = fopen(input_file_path, "rb");
  if (input_file == nullptr) {
    nsmbw_compress_print_error("Failed to open input file: %s",
                               strerror(errno));
    return cleanup_disposable(EXIT_FAILURE);
  }
  fseek(input_file, 0, SEEK_END);
  size_t input_file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);
  void *input_file_data = malloc(input_file_size);
  if (input_file_data == nullptr) {
    nsmbw_compress_print_error("Failed to allocate memory for input file: %s",
                               strerror(errno));
    fclose(input_file);
    return cleanup_disposable(EXIT_FAILURE);
  }
  if (fread(input_file_data, input_file_size, 1, input_file) != 1) {
    nsmbw_compress_print_error("Failed to read input file: %s",
                               strerror(errno));
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
  nsmbw_compress_print_verbose(
      "Opening output file: %s",
      argument_values[argument_index_output].string_value);
  FILE *output_file =
      fopen(argument_values[argument_index_output].string_value, "wb");
  if (output_file == nullptr) {
    nsmbw_compress_print_error("Failed to open output file: %s",
                               strerror(errno));
    return cleanup_disposable(EXIT_FAILURE);
  }
  if (fwrite(output_data, output_size, 1, output_file) != 1) {
    nsmbw_compress_print_error("Failed to write output file: %s",
                               strerror(errno));
    fclose(output_file);
    return cleanup_disposable(EXIT_FAILURE);
  }
  fclose(output_file);

  nsmbw_compress_print_verbose("main(): Exit success");
  return cleanup_disposable(EXIT_SUCCESS);
}