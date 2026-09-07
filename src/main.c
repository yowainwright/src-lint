#include "src_lint/check.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
  fputs("Usage:\n", stream);
  fputs("  src-lint check [path] [--strict] [--format text|json]\n", stream);
  fputs("  src-lint discover [path] [--format text|json]\n", stream);
  fputs("  src-lint graph [path] [--format json|html]\n", stream);
}

static bool is_help(const char *value) {
  return strcmp(value, "--help") == 0 || strcmp(value, "-h") == 0;
}

static bool set_command(const char *value, SlRunOptions *options) {
  if (strcmp(value, "check") == 0) {
    options->command = SL_COMMAND_CHECK;
    options->format = SL_FORMAT_TEXT;
    return true;
  }
  if (strcmp(value, "discover") == 0) {
    options->command = SL_COMMAND_DISCOVER;
    options->format = SL_FORMAT_TEXT;
    return true;
  }
  if (strcmp(value, "graph") != 0) return false;
  options->command = SL_COMMAND_GRAPH;
  options->format = SL_FORMAT_JSON;
  return true;
}

static bool set_format(const char *value, SlRunOptions *options) {
  if (strcmp(value, "text") == 0) {
    options->format = SL_FORMAT_TEXT;
    return true;
  }
  if (strcmp(value, "json") == 0) {
    options->format = SL_FORMAT_JSON;
    return true;
  }
  if (strcmp(value, "html") != 0) return false;
  options->format = SL_FORMAT_HTML;
  return true;
}

static bool format_supported(const SlRunOptions *options) {
  if (options->command == SL_COMMAND_GRAPH) return options->format != SL_FORMAT_TEXT;
  return options->format != SL_FORMAT_HTML;
}

static bool parse_arguments(int argc, char **argv, SlRunOptions *options) {
  bool has_root = false;
  for (int index = 2; index < argc; index += 1) {
    if (strcmp(argv[index], "--strict") == 0) {
      if (options->command != SL_COMMAND_CHECK) return false;
      options->strict = true;
      continue;
    }
    if (strcmp(argv[index], "--format") == 0) {
      index += 1;
      if (index >= argc || !set_format(argv[index], options)) return false;
      continue;
    }
    if (argv[index][0] == '-' || has_root) return false;
    options->root = argv[index];
    has_root = true;
  }
  return format_supported(options);
}

int main(int argc, char **argv) {
  if (argc == 2 && is_help(argv[1])) {
    print_usage(stdout);
    return 0;
  }
  SlRunOptions options = {".", SL_COMMAND_CHECK, SL_FORMAT_TEXT, false};
  if (argc < 2 || !set_command(argv[1], &options)) {
    print_usage(stderr);
    return 2;
  }
  if (!parse_arguments(argc, argv, &options)) {
    print_usage(stderr);
    return 2;
  }
  return sl_run(&options, stdout, stderr);
}
