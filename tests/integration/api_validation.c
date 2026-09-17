#include "src_lint/check.h"

#include <stdio.h>
#include <unistd.h>

static bool close_streams(FILE *output, FILE *errors, int result) {
  if (output) fclose(output);
  if (errors) fclose(errors);
  return result == 2;
}

static bool rejects(const SlRunOptions *options) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return close_streams(output, errors, 1);
  const int result = sl_run(options, output, errors);
  return close_streams(output, errors, result);
}

static bool rejects_invalid_options(const char *root) {
  const SlRunOptions command = {root, (SlCommand)99, SL_FORMAT_TEXT, false, NULL};
  const SlRunOptions format = {root, SL_COMMAND_CHECK, (SlFormat)99, false, NULL};
  const SlRunOptions graph_text = {root, SL_COMMAND_GRAPH, SL_FORMAT_TEXT, false, NULL};
  const SlRunOptions discover_html = {root, SL_COMMAND_DISCOVER, SL_FORMAT_HTML, false, NULL};
  const SlRunOptions discover_strict = {root, SL_COMMAND_DISCOVER, SL_FORMAT_TEXT, true, NULL};
  const SlRunOptions empty_config = {root, SL_COMMAND_CHECK, SL_FORMAT_TEXT, false, ""};
  return rejects(&command) && rejects(&format) && rejects(&graph_text) && rejects(&discover_html) &&
         rejects(&discover_strict) && rejects(&empty_config);
}

static bool rejects_output_failure(const char *root) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return close_streams(output, errors, 1);
  const int descriptor = fileno(output);
  if (descriptor < 0 || close(descriptor) != 0) return close_streams(output, errors, 1);
  const SlRunOptions options = {root, SL_COMMAND_CHECK, SL_FORMAT_JSON, false, NULL};
  const int result = sl_run(&options, output, errors);
  return close_streams(output, errors, result);
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  if (!rejects_invalid_options(argv[1])) return 1;
  return rejects_output_failure(argv[1]) ? 0 : 1;
}
