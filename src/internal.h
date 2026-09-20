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
  SL_LANGUAGE_PROTO,
  SL_LANGUAGE_JULIA,
  SL_LANGUAGE_RUBY,
  SL_LANGUAGE_SWIFT,
  SL_LANGUAGE_KOTLIN,
  SL_LANGUAGE_JAVA,
  SL_LANGUAGE_TERRAFORM,
  SL_LANGUAGE_COUNT
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

/* Copies specifier; callers retain it on success and failure.
 * The list owns stored copies until sl_import_list_free(). */
bool sl_import_list_add(SlImportList *list, const char *specifier, size_t line, size_t column,
                        SlLanguage language);
void sl_import_list_free(SlImportList *list);
bool sl_parse_imports(const char *path, char *content, SlImportList *list);
SlLanguage sl_source_language(const char *path);
bool sl_parse_native_imports(const char *content, SlLanguage language, SlImportList *list);

#endif
