#ifndef SRC_LINT_CHECK_H
#define SRC_LINT_CHECK_H

#include <stdbool.h>
#include <stdio.h>

typedef enum { SL_COMMAND_CHECK, SL_COMMAND_DISCOVER, SL_COMMAND_GRAPH } SlCommand;

typedef enum { SL_FORMAT_TEXT, SL_FORMAT_JSON, SL_FORMAT_HTML } SlFormat;

typedef struct {
  const char *root;
  SlFormat format;
  bool strict;
} SlCheckOptions;

typedef struct {
  const char *root;
  SlCommand command;
  SlFormat format;
  bool strict;
} SlRunOptions;

int sl_check(const SlCheckOptions *options, FILE *output, FILE *errors);
int sl_run(const SlRunOptions *options, FILE *output, FILE *errors);

#endif
