#include "nsmbw_compress.h"
#include "ncutil.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *executable_name = "nsmbw-compress";

static const char *input_file_path = NULL;

static const int compress_dst_scale = 4;

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
    [nsmbw_compress_type_diff] = {nsmbw_compress_diff_encode,
                                  nsmbw_compress_diff_decode},
    [nsmbw_compress_type_szs] = {nsmbw_compress_szs_encode,
                                 nsmbw_compress_szs_decode},
    [nsmbw_compress_type_ash] = {nsmbw_compress_ash_encode,
                                 nsmbw_compress_ash_decode},
    [nsmbw_compress_type_asr] = {nsmbw_compress_asr_encode,
                                 nsmbw_compress_asr_decode},
};

static const char *compression_type_names[] = {
    [nsmbw_compress_type_lz] = "lz",   [nsmbw_compress_type_huff] = "huff",
    [nsmbw_compress_type_rl] = "rl",   [nsmbw_compress_type_lh] = "lh",
    [nsmbw_compress_type_lrc] = "lrc", [nsmbw_compress_type_diff] = "diff",
    [nsmbw_compress_type_szs] = "szs", [nsmbw_compress_type_ash] = "ash",
    [nsmbw_compress_type_asr] = "asr",
};

static const char *compression_default_extensions[] = {
    [nsmbw_compress_type_lz] = ".LZ",   [nsmbw_compress_type_huff] = ".HUFF",
    [nsmbw_compress_type_rl] = ".RL",   [nsmbw_compress_type_lh] = ".LH",
    [nsmbw_compress_type_lrc] = ".LRC", [nsmbw_compress_type_diff] = ".DIFF",
    [nsmbw_compress_type_szs] = ".szs", [nsmbw_compress_type_ash] = ".ash",
    [nsmbw_compress_type_asr] = ".asr",
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
        .description = "<path> Specify the output file name",
        .type = nsmbw_compress_argument_type_path,
        .index = 1,
#define argument_index_output 1
    },
    {
        .short_name = 't',
        .long_name = "type",
        .long_name_length = sizeof("type") - 1,
        .description =
            "<type> Specify the compression type (see supported types below)",
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
        .short_name = 'l',
        .long_name = "lz-mode",
        .long_name_length = sizeof("lz-mode") - 1,
        .description = "<0, 1*, auto> Specify the LZ compression mode",
        .type = nsmbw_compress_argument_type_string,
        .index = 4,
#define argument_index_lz_mode 4
    },
    {
        .short_name = 'b',
        .long_name = "huff-size",
        .long_name_length = sizeof("huff-size") - 1,
        .description =
            "<4, 8, auto*> Specify the bit size for Huffman compression",
        .type = nsmbw_compress_argument_type_string,
        .index = 5,
#define argument_index_huff_size 5
    },
    {
        .short_name = 'r',
        .long_name = "asr-mode",
        .long_name_length = sizeof("asr-mode") - 1,
        .description = "<0, 1, auto*> Specify the mode for ASR compression",
        .type = nsmbw_compress_argument_type_string,
        .index = 6,
#define argument_index_asr_mode 6
    },
    {
        .short_name = 'd',
        .long_name = "diff-size",
        .long_name_length = sizeof("diff-size") - 1,
        .description =
            "<8*, 16> Specify the element size for filter-diff encoding",
        .type = nsmbw_compress_argument_type_int,
        .index = 7,
#define argument_index_diff_size 7
    },
    {
        .short_name = '\0',
        .long_name = "test",
        .long_name_length = sizeof("test") - 1,
        .description = "Run internal tests and exit",
        .type = nsmbw_compress_argument_type_bool,
        .index = 8,
#define argument_index_test 8
    },
    {
        .short_name = 'v',
        .long_name = "verbose",
        .long_name_length = sizeof("verbose") - 1,
        .description = "Print verbose output",
        .type = nsmbw_compress_argument_type_bool,
        .index = 9,
#define argument_index_verbose 9
    },
};

static const size_t argument_count = ncutil_array_size(arguments);

static union nsmbw_compress_argument_value
    argument_values[ncutil_array_size(arguments)];
static bool argument_specified[ncutil_array_size(arguments)] = {0};

// Weird way of doing this, anything here will get freed by cleanup_disposable()
// on exit.
static void *disposable_buffers[16] = {};

static void *malloc_disposable(size_t size) {
  for (size_t i = 0; i < ncutil_array_size(disposable_buffers); ++i) {
    if (disposable_buffers[i] == NULL) {
      return disposable_buffers[i] = malloc(size);
    }
  }
  errno = ENOMEM;
  return NULL;
}

static int cleanup_disposable(int result) {
  for (size_t i = 0; i < ncutil_array_size(disposable_buffers); ++i) {
    free(disposable_buffers[i]);
    disposable_buffers[i] = NULL;
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
  } else if (strcmp(str, "diff") == 0) {
    return nsmbw_compress_type_diff;
  } else if (strcmp(str, "szs") == 0) {
    return nsmbw_compress_type_szs;
  } else if (strcmp(str, "ash") == 0) {
    return nsmbw_compress_type_ash;
  } else if (strcmp(str, "asr") == 0) {
    return nsmbw_compress_type_asr;
  } else {
    return -1; // Invalid type
  }
}

static enum nsmbw_compress_lz_mode get_lz_mode() {
  if (argument_specified[argument_index_lz_mode]) {
    const char *str = argument_values[argument_index_lz_mode].string_value;
    if (strcmp(str, "0") == 0) {
      return nsmbw_compress_lz_mode_0;
    } else if (strcmp(str, "1") == 0) {
      return nsmbw_compress_lz_mode_1;
    } else if (strcmp(str, "auto") == 0) {
      return nsmbw_compress_lz_mode_auto;
    } else {
      nsmbw_compress_print_error("Invalid LZ mode: %s. Use 0, 1, or auto.",
                                 str);
      exit(EXIT_FAILURE);
    }
  } else {
    return nsmbw_compress_lz_mode_1; // Default to mode 1
  }
}

static uint8_t get_huff_bit_size() {
  if (argument_specified[argument_index_huff_size]) {
    const char *str = argument_values[argument_index_huff_size].string_value;
    if (strcmp(str, "4") == 0) {
      return 4;
    } else if (strcmp(str, "8") == 0) {
      return 8;
    } else if (strcmp(str, "auto") == 0) {
      return 0; // Zero indicates auto mode
    } else {
      nsmbw_compress_print_error(
          "Invalid Huffman bit size: %s. Use 4, 8, or auto.", str);
      exit(EXIT_FAILURE);
    }
  } else {
    return 0; // Default to auto
  }
}

static enum nsmbw_compress_asr_mode get_asr_mode() {
  if (argument_specified[argument_index_asr_mode]) {
    const char *str = argument_values[argument_index_asr_mode].string_value;
    if (strcmp(str, "0") == 0) {
      return nsmbw_compress_asr_mode_0;
    } else if (strcmp(str, "1") == 0) {
      return nsmbw_compress_asr_mode_1;
    } else if (strcmp(str, "auto") == 0) {
      return nsmbw_compress_asr_mode_auto;
    } else {
      nsmbw_compress_print_error("Invalid ASR mode: %s. Use 0, 1, or auto.",
                                 str);
      exit(EXIT_FAILURE);
    }
  } else {
    return nsmbw_compress_asr_mode_auto; // Default to auto
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
  return NULL;
}

static const struct nsmbw_compress_argument *
find_short_argument(char short_name) {
  size_t i;
  for (i = 0; i < argument_count; i++) {
    if (short_name == arguments[i].short_name) {
      return &arguments[i];
    }
  }
  return NULL;
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

static int exit_print_help() {
  puts("Usage: nsmbw-compress [options] <input> [-o output]");
  puts("Options:");
  for (size_t i = 0; i < argument_count; i++) {
    const struct nsmbw_compress_argument *arg = &arguments[i];
    if (arg->short_name) {
      printf("  -%c, --%-9s %s\n", arg->short_name, arg->long_name,
             arg->description);
    } else {
      printf("      --%-9s %s\n", arg->long_name, arg->description);
    }
  }
  printf("Supported types for compression:\n"
         "  ");
  for (size_t i = 0; i < ncutil_array_size(compression_type_names); i++) {
    if (compress_functions[i][0] != NULL) {
      printf("%s ", compression_type_names[i]);
    }
  }
  printf("\n"
         "Supported types for decompression:\n"
         "  ");
  for (size_t i = 0; i < ncutil_array_size(compression_type_names); i++) {
    if (compress_functions[i][1] != NULL) {
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
      if (long_arg == NULL) {
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
      if (short_arg == NULL) {
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
      if (input_file_path != NULL) {
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
                                enum nsmbw_compress_type *compression_type) {
  if (input_size < 4) {
    nsmbw_compress_print_error(
        "Input file is too small to be a valid compressed file");
    return false;
  }

  uint32_t header = ncutil_read_le_u32(input_data, 0);

  // Check for "Yaz0" or "Yaz1"
  static const uint32_t szs_magic =
      'Y' | ('a' << 8) | ('z' << 16) | ('0' << 24);
  if ((header & ~(0x1 << 24)) == szs_magic) {
    *compression_type = nsmbw_compress_type_szs;
    if (input_size < 8) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid SZS/Yaz0 file");
      return false;
    }
    *expanded_size = ncutil_read_be_u32(input_data, 4);
    return true;
  }

  // Check for "ASH0"
  static const uint32_t ash_magic =
      'A' | ('S' << 8) | ('H' << 16) | ('0' << 24);
  if (header == ash_magic) {
    *compression_type = nsmbw_compress_type_ash;
    if (input_size < 8) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid ASH file");
      return false;
    }
    *expanded_size = ncutil_read_be_u32(input_data, 4) & 0x00FFFFFFu;
    return true;
  }

  // Check for "ASR0"
  static const uint32_t asr_magic =
      'A' | ('S' << 8) | ('R' << 16) | ('0' << 24);
  if (header == asr_magic) {
    *compression_type = nsmbw_compress_type_asr;
    if (input_size < 8) {
      nsmbw_compress_print_error(
          "Input file is too small to be a valid ASR file");
      return false;
    }
    uint32_t header2 = ncutil_read_be_u32(input_data, 4);
    if (header2 >> 24 != 0x00 && header2 >> 24 != 0x80) {
      nsmbw_compress_print_error("Input file has unrecognized ASR mode: %02x",
                                 header2 >> 24);
      return false;
    }
    *expanded_size = header2 & 0x00FFFFFFu;
    return true;
  }

  enum nsmbw_compress_cx_type cx_type = header & nsmbw_compress_cx_type_mask;
  uint8_t cx_option = header & 0x0F;
  switch (cx_type) {
  case nsmbw_compress_cx_type_lz:
    *compression_type = nsmbw_compress_type_lz;
    if (cx_option != 0 && cx_option != 1) {
      nsmbw_compress_print_error(
          "Input file has unrecognized LZ compression option: %d", cx_option);
      return false;
    }
    break;
  case nsmbw_compress_cx_type_huff:
    *compression_type = nsmbw_compress_type_huff;
    if (cx_option != 4 && cx_option != 8) {
      nsmbw_compress_print_error(
          "Input file has unrecognized Huffman bit size: %d", cx_option);
      return false;
    }
    break;
  case nsmbw_compress_cx_type_rl:
    *compression_type = nsmbw_compress_type_rl;
    if (cx_option != 0) {
      nsmbw_compress_print_error(
          "Input file specifies non-zero option for RL compression: %d",
          cx_option);
      return false;
    }
    break;
  case nsmbw_compress_cx_type_lh:
    *compression_type = nsmbw_compress_type_lh;
    if (cx_option != 0) {
      nsmbw_compress_print_error(
          "Input file specifies non-zero option for LH compression: %d",
          cx_option);
      return false;
    }
    break;
  case nsmbw_compress_cx_type_lrc:
    *compression_type = nsmbw_compress_type_lrc;
    if (cx_option != 0) {
      if (header == 0x2D38AA55) {
        // Explicit check for DARCH (a.k.a. arc or U8) header to provide a more
        // specific error message
        nsmbw_compress_print_error("Input file is not compressed");
        return false;
      }

      nsmbw_compress_print_error(
          "Input file specifies non-zero option for LRC compression: %d",
          cx_option);
      return false;
    }
    break;
  case nsmbw_compress_cx_type_diff:
    *compression_type = nsmbw_compress_type_diff;
    if (cx_option != 0 && cx_option != 1) {
      nsmbw_compress_print_error("Input file has unrecognized filter-diff "
                                 "size flag (%d)",
                                 cx_option);
      return false;
    }
    break;
  default:
    nsmbw_compress_print_error(
        "Couldn't determine compression type of input file");
    return false;
  }

  *expanded_size = header >> 8;
  if (*expanded_size == 0) {
    if (cx_type == nsmbw_compress_cx_type_diff) {
      nsmbw_compress_print_error(
          "Invalid zero size in filter-diff input file header");
      return false;
    }
    // Size is extended to 32 bits
    if (input_size < 8) {
      nsmbw_compress_print_error(
          "Input file ended prematurely while reading extended size");
      return false;
    }
    *expanded_size = ncutil_read_le_u32(input_data, 4);
    if (*expanded_size == 0) {
      nsmbw_compress_print_error("Invalid zero size in input file header");
      return false;
    }
  }
  return true;
}

static int main_uncompress(const void *input_file, size_t input_file_size,
                           void **output_data, size_t *output_size) {
  if (argument_specified[argument_index_type]) {
    nsmbw_compress_print_warning(
        "--type has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_lz_mode]) {
    nsmbw_compress_print_warning(
        "--lz-mode has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_huff_size]) {
    nsmbw_compress_print_warning(
        "--huff-size has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_asr_mode]) {
    nsmbw_compress_print_warning(
        "--asr-mode has no effect when --uncomp is specified");
  }
  if (argument_specified[argument_index_diff_size]) {
    nsmbw_compress_print_warning(
        "--diff-size has no effect when --uncomp is specified");
  }

  size_t expanded_size;
  enum nsmbw_compress_type compression_type;
  if (!get_uncompress_info(input_file, input_file_size, &expanded_size,
                           &compression_type)) {
    return EXIT_FAILURE;
  }

  nsmbw_compress_print_verbose("Type: %s, Expanded size: %zu bytes",
                               compression_type_names[compression_type],
                               expanded_size);

  if (compress_functions[compression_type][1] == NULL) {
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
    char *output_path = NULL;
    if (extension && input_path_len > strlen(extension) &&
        strcasecmp(input_file_path + input_path_len - strlen(extension),
                   extension) == 0) {
      size_t output_path_len = input_path_len - strlen(extension);
      output_path = (char *)malloc_disposable(output_path_len + 1);
      if (output_path == NULL) {
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
      if (output_path == NULL) {
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
    if (test_file != NULL) {
      nsmbw_compress_print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  void *uncompressed_data = malloc_disposable(expanded_size);
  if (uncompressed_data == NULL) {
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

  if (compress_functions[compression_type][0] == NULL) {
    nsmbw_compress_print_error(
        "Compression type %s is not supported for compression",
        compression_type_names[compression_type]);
    return EXIT_FAILURE;
  }

  if (argument_specified[argument_index_lz_mode] &&
      compression_type != nsmbw_compress_type_lz) {
    nsmbw_compress_print_warning(
        "--lz-mode has no effect for compression type %s",
        compression_type_names[compression_type]);
  }
  if (argument_specified[argument_index_huff_size] &&
      compression_type != nsmbw_compress_type_huff) {
    nsmbw_compress_print_warning(
        "--huff-size has no effect for compression type %s",
        compression_type_names[compression_type]);
  }
  if (argument_specified[argument_index_asr_mode] &&
      compression_type != nsmbw_compress_type_asr) {
    nsmbw_compress_print_warning(
        "--asr-mode has no effect for compression type %s",
        compression_type_names[compression_type]);
  }
  if (argument_specified[argument_index_diff_size] &&
      compression_type != nsmbw_compress_type_diff) {
    nsmbw_compress_print_warning(
        "--diff-size has no effect for compression type %s",
        compression_type_names[compression_type]);
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
    if (output_path == NULL) {
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
    if (test_file != NULL) {
      nsmbw_compress_print_error("Output file already exists: %s", output_path);
      fclose(test_file);
      return EXIT_FAILURE;
    }

    argument_values[argument_index_output].string_value = output_path;
  }

  size_t dst_length = 0x1000 + input_file_size * compress_dst_scale;
  void *compressed_data = malloc_disposable(dst_length);
  if (compressed_data == NULL) {
    nsmbw_compress_print_error(
        "Failed to allocate memory for compressed data: %s", strerror(errno));
    return EXIT_FAILURE;
  }

  struct nsmbw_compress_parameters params = {
      .lz_mode = get_lz_mode(),
      .huff_bit_size = get_huff_bit_size(),
      .filter_diff_size =
          argument_specified[argument_index_diff_size]
              ? argument_values[argument_index_diff_size].int_value
              : 8,
      .asr_mode = get_asr_mode(),
  };

  clock_t start_time, end_time;

  if (argument_specified[argument_index_verbose]) {
    start_time = clock();
    nsmbw_compress_print_verbose("Begin compression...");
  }

  bool ok = compress_functions[compression_type][0](
      input_file, compressed_data, input_file_size, &dst_length, &params);
  if (!ok) {
    nsmbw_compress_print_error("Output file not written due to errors");
    return EXIT_FAILURE;
  }

  if (argument_specified[argument_index_verbose]) {
    end_time = clock();
    double elapsed_seconds =
        ncutil_static_cast(double, end_time - start_time) / CLOCKS_PER_SEC;
    nsmbw_compress_print_verbose(
        "Compression completed in %.6f seconds with size %zu bytes (%.2f%%)",
        elapsed_seconds, dst_length,
        ncutil_static_cast(double, dst_length) / input_file_size * 100);
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

  if (input_file_path == NULL) {
    nsmbw_compress_print_error("No input file specified.");
    return cleanup_disposable(exit_print_help());
  }
  nsmbw_compress_print_verbose("Opening input file: %s", input_file_path);
  FILE *input_file = fopen(input_file_path, "rb");
  if (input_file == NULL) {
    nsmbw_compress_print_error("Failed to open input file: %s",
                               strerror(errno));
    return cleanup_disposable(EXIT_FAILURE);
  }
  fseek(input_file, 0, SEEK_END);
  size_t input_file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);
  void *input_file_data = malloc(input_file_size);
  if (input_file_data == NULL) {
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
  if (output_file == NULL) {
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
