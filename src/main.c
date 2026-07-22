#include "tree_legibility/check.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream) {
  fputs("Usage: tree-legibility check [path] [--format text|json]\n", stream);
}

static bool is_help(const char *value) {
  return strcmp(value, "--help") == 0 || strcmp(value, "-h") == 0;
}

static bool set_format(const char *value, TlCheckOptions *options) {
  if (strcmp(value, "text") == 0) {
    options->format = TL_FORMAT_TEXT;
    return true;
  }
  if (strcmp(value, "json") == 0) {
    options->format = TL_FORMAT_JSON;
    return true;
  }
  return false;
}

static bool parse_arguments(int argc, char **argv, TlCheckOptions *options) {
  bool has_root = false;
  for (int index = 2; index < argc; index += 1) {
    if (strcmp(argv[index], "--format") == 0) {
      index += 1;
      if (index >= argc || !set_format(argv[index], options)) return false;
      continue;
    }
    if (argv[index][0] == '-' || has_root) return false;
    options->root = argv[index];
    has_root = true;
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc == 2 && is_help(argv[1])) {
    print_usage(stdout);
    return 0;
  }
  if (argc < 2 || strcmp(argv[1], "check") != 0) {
    print_usage(stderr);
    return 2;
  }
  TlCheckOptions options = {".", TL_FORMAT_TEXT};
  if (!parse_arguments(argc, argv, &options)) {
    print_usage(stderr);
    return 2;
  }
  return tl_check(&options, stdout, stderr);
}
