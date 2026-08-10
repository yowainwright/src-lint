#ifndef TREE_LEGIBILITY_INTERNAL_H
#define TREE_LEGIBILITY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#define TL_PATH_CAPACITY 4096
#define TL_OWNER_CAPACITY 128

typedef enum {
  TL_LANGUAGE_JAVASCRIPT,
  TL_LANGUAGE_PYTHON,
  TL_LANGUAGE_GO,
  TL_LANGUAGE_PROTO
} TlLanguage;

typedef struct {
  char *specifier;
  size_t line;
  size_t column;
  TlLanguage language;
} TlImport;

typedef struct {
  TlImport *items;
  size_t count;
  size_t capacity;
} TlImportList;

bool tl_import_list_add(TlImportList *list, const char *specifier, size_t line, size_t column,
                        TlLanguage language);
void tl_import_list_free(TlImportList *list);
bool tl_parse_imports(const char *path, char *content, TlImportList *list);

#endif
