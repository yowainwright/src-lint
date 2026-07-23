#include "tree_legibility/check.h"

#include <stdio.h>
#include <unistd.h>

static bool rejects(const TlRunOptions *options) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return false;
  const int result = tl_run(options, output, errors);
  fclose(output);
  fclose(errors);
  return result == 2;
}

static bool rejects_invalid_options(const char *root) {
  const TlRunOptions command = {root, (TlCommand)99, TL_FORMAT_TEXT, false};
  const TlRunOptions format = {root, TL_COMMAND_CHECK, (TlFormat)99, false};
  const TlRunOptions graph_text = {root, TL_COMMAND_GRAPH, TL_FORMAT_TEXT, false};
  const TlRunOptions discover_html = {root, TL_COMMAND_DISCOVER, TL_FORMAT_HTML, false};
  const TlRunOptions discover_strict = {root, TL_COMMAND_DISCOVER, TL_FORMAT_TEXT, true};
  return rejects(&command) && rejects(&format) && rejects(&graph_text) && rejects(&discover_html) &&
         rejects(&discover_strict);
}

static bool rejects_output_failure(const char *root) {
  FILE *output = tmpfile();
  FILE *errors = tmpfile();
  if (!output || !errors) return false;
  const int descriptor = fileno(output);
  if (descriptor < 0 || close(descriptor) != 0) return false;
  const TlRunOptions options = {root, TL_COMMAND_CHECK, TL_FORMAT_JSON, false};
  const int result = tl_run(&options, output, errors);
  fclose(output);
  fclose(errors);
  return result == 2;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  if (!rejects_invalid_options(argv[1])) return 1;
  return rejects_output_failure(argv[1]) ? 0 : 1;
}
