#ifndef TREE_LEGIBILITY_CHECK_H
#define TREE_LEGIBILITY_CHECK_H

#include <stdbool.h>
#include <stdio.h>

typedef enum { TL_COMMAND_CHECK, TL_COMMAND_DISCOVER, TL_COMMAND_GRAPH } TlCommand;

typedef enum { TL_FORMAT_TEXT, TL_FORMAT_JSON, TL_FORMAT_HTML } TlFormat;

typedef struct {
  const char *root;
  TlFormat format;
  bool strict;
} TlCheckOptions;

typedef struct {
  const char *root;
  TlCommand command;
  TlFormat format;
  bool strict;
} TlRunOptions;

int tl_check(const TlCheckOptions *options, FILE *output, FILE *errors);
int tl_run(const TlRunOptions *options, FILE *output, FILE *errors);

#endif
