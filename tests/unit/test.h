#ifndef SRC_LINT_TEST_H
#define SRC_LINT_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike assert(), checks must execute with NDEBUG in Release builds. */
#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, __func__, #condition);                \
      exit(EXIT_FAILURE);                                                                          \
    }                                                                                              \
  } while (0)

#define COUNT(items) (sizeof(items) / sizeof((items)[0]))

#endif
