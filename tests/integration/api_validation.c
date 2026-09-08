#include "src_lint/check.h"

#include <stdio.h>
#include <unistd.h>

static bool rejects(const SlRunOptions *options) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return false;
  const int result = sl_run(options, output, errors);
  fclose(output);
  fclose(errors);
  return result == 2;
}

static bool rejects_invalid_options(const char *root) {
  const SlRunOptions command = {root, (SlCommand)99, SL_FORMAT_TEXT, false};
  const SlRunOptions format = {root, SL_COMMAND_CHECK, (SlFormat)99, false};
  const SlRunOptions graph_text = {root, SL_COMMAND_GRAPH, SL_FORMAT_TEXT, false};
  const SlRunOptions discover_html = {root, SL_COMMAND_DISCOVER, SL_FORMAT_HTML, false};
  const SlRunOptions discover_strict = {root, SL_COMMAND_DISCOVER, SL_FORMAT_TEXT, true};
  return rejects(&command) && rejects(&format) && rejects(&graph_text) && rejects(&discover_html) &&
         rejects(&discover_strict);
}

static bool rejects_output_failure(const char *root) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return false;
  const int descriptor = fileno(output);
  if (descriptor < 0 || close(descriptor) != 0) return false;
  const SlRunOptions options = {root, SL_COMMAND_CHECK, SL_FORMAT_JSON, false};
  const int result = sl_run(&options, output, errors);
  fclose(output);
  fclose(errors);
  return result == 2;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  if (!rejects_invalid_options(argv[1])) return 1;
  return rejects_output_failure(argv[1]) ? 0 : 1;
}
