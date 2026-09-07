#ifndef SRC_LINT_INTERNAL_H
#define SRC_LINT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#define SL_PATH_CAPACITY 4096
#define SL_OWNER_CAPACITY 128

typedef enum {
  SL_LANGUAGE_JAVASCRIPT,
  SL_LANGUAGE_PYTHON,
  SL_LANGUAGE_GO,
  SL_LANGUAGE_PROTO
} SlLanguage;

typedef struct {
  char *specifier;
  size_t line;
  size_t column;
  SlLanguage language;
} SlImport;

typedef struct {
  SlImport *items;
  size_t count;
  size_t capacity;
} SlImportList;

bool sl_import_list_add(SlImportList *list, const char *specifier, size_t line, size_t column,
                        SlLanguage language);
void sl_import_list_free(SlImportList *list);
bool sl_parse_imports(const char *path, char *content, SlImportList *list);

#endif
