#ifndef TREE_LEGIBILITY_CHECK_H
#define TREE_LEGIBILITY_CHECK_H

#include <stdio.h>

typedef enum { TL_FORMAT_TEXT, TL_FORMAT_JSON } TlFormat;

typedef struct {
  const char *root;
  TlFormat format;
} TlCheckOptions;

int tl_check(const TlCheckOptions *options, FILE *output, FILE *errors);

#endif
