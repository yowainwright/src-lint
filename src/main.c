#include "src_lint/check.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
  fputs("Usage:\n", stream);
  fputs("  src-lint check [path] [--config file] [--strict] [--format text|json]\n", stream);
  fputs("  src-lint discover [path] [--config file] [--format text|json]\n", stream);
  fputs("  src-lint graph [path] [--config file] [--format json|html]\n", stream);
  fputs("  src-lint --version\n", stream);
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

static bool set_option(const char *name, const char *value, SlRunOptions *options) {
  if (strcmp(name, "--format") == 0) return set_format(value, options);
  if (strcmp(name, "--config") != 0 || options->config_path || !*value || *value == '-')
    return false;
  options->config_path = value;
  return true;
}

static bool parse_option(int argc, char **argv, int *index, SlRunOptions *options) {
  const char *name = argv[*index];
  if (strcmp(name, "--strict") == 0) {
    options->strict = true;
    return options->command == SL_COMMAND_CHECK;
  }
  *index += 1;
  return *index < argc && set_option(name, argv[*index], options);
}

static bool parse_arguments(int argc, char **argv, SlRunOptions *options) {
  bool has_root = false;
  for (int index = 2; index < argc; index += 1) {
    const bool is_option = argv[index][0] == '-';
    if (is_option && !parse_option(argc, argv, &index, options)) return false;
    if (is_option) continue;
    if (has_root) return false;
    options->root = argv[index];
    has_root = true;
  }
  return format_supported(options);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    puts("src-lint " SRC_LINT_VERSION);
    return 0;
  }
  if (argc == 2 && is_help(argv[1])) {
    print_usage(stdout);
    return 0;
  }
  SlRunOptions options = {.root = ".", .command = SL_COMMAND_CHECK, .format = SL_FORMAT_TEXT};
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
