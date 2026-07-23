#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  char *start;
  size_t length;
} TlToken;

typedef struct {
  char *specifier;
  char *end;
} TlImportEdge;

typedef struct {
  char *source_start;
  char *cursor;
  bool expect_from;
} TlJsScanner;

static char *duplicate_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, value, length);
  return copy;
}

static bool grow_imports(TlImportList *list) {
  const size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
  TlImport *items = realloc(list->items, capacity * sizeof(*items));
  if (!items) return false;
  list->items = items;
  list->capacity = capacity;
  return true;
}

bool tl_import_list_add(TlImportList *list, const char *specifier, size_t line, size_t column,
                        TlLanguage language) {
  if (list->count == list->capacity && !grow_imports(list)) return false;
  char *copy = duplicate_string(specifier);
  if (!copy) return false;
  list->items[list->count++] = (TlImport){copy, line, column, language};
  return true;
}

void tl_import_list_free(TlImportList *list) {
  for (size_t index = 0; index < list->count; index += 1) free(list->items[index].specifier);
  free(list->items);
  *list = (TlImportList){0};
}

static bool quote_character(char character) {
  return character == '\'' || character == '"' || character == '`';
}

static char *quoted_end(char *cursor) {
  const char quote = *cursor;
  cursor += 1;
  while (*cursor) {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 2;
      continue;
    }
    if (*cursor == quote) return cursor;
    cursor += 1;
  }
  return NULL;
}

static char *skip_quoted(char *cursor) {
  char *end = quoted_end(cursor);
  return end ? end + 1 : cursor + strlen(cursor);
}

static bool identifier_start(char character) {
  const bool lowercase = character >= 'a' && character <= 'z';
  const bool uppercase = character >= 'A' && character <= 'Z';
  return lowercase || uppercase || character == '_' || character == '$';
}

static bool identifier_part(char character) {
  return identifier_start(character) || (character >= '0' && character <= '9');
}

static bool token_is(const TlToken *token, const char *value) {
  return strlen(value) == token->length && strncmp(token->start, value, token->length) == 0;
}

static char *skip_space(char *cursor) {
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') cursor += 1;
  return cursor;
}

static bool block_comment_start(const char *cursor) { return cursor[0] == '/' && cursor[1] == '*'; }

static bool line_comment_start(const char *cursor) { return cursor[0] == '/' && cursor[1] == '/'; }

static char *skip_block_comment(char *cursor) {
  char *end = strstr(cursor + 2, "*/");
  return end ? end + 2 : cursor + strlen(cursor);
}

static char *skip_line_comment(char *cursor) {
  char *end = strchr(cursor + 2, '\n');
  return end ? end + 1 : cursor + strlen(cursor);
}

static char *skip_js_trivia(char *cursor) {
  while (*cursor) {
    cursor = skip_space(cursor);
    if (block_comment_start(cursor)) {
      cursor = skip_block_comment(cursor);
      continue;
    }
    if (!line_comment_start(cursor)) return cursor;
    cursor = skip_line_comment(cursor);
  }
  return cursor;
}

static bool read_identifier(TlJsScanner *scanner, TlToken *token) {
  if (!identifier_start(*scanner->cursor)) return false;
  token->start = scanner->cursor;
  scanner->cursor += 1;
  while (identifier_part(*scanner->cursor)) scanner->cursor += 1;
  token->length = (size_t)(scanner->cursor - token->start);
  return true;
}

static bool skip_js_ignored(TlJsScanner *scanner) {
  if (line_comment_start(scanner->cursor)) {
    scanner->cursor = skip_line_comment(scanner->cursor);
    return true;
  }
  if (block_comment_start(scanner->cursor)) {
    scanner->cursor = skip_block_comment(scanner->cursor);
    return true;
  }
  if (!quote_character(*scanner->cursor)) return false;
  scanner->cursor = skip_quoted(scanner->cursor);
  return true;
}

static bool next_js_token(TlJsScanner *scanner, TlToken *token) {
  while (*scanner->cursor) {
    if (skip_js_ignored(scanner)) continue;
    if (read_identifier(scanner, token)) return true;
    if (*scanner->cursor == ';') scanner->expect_from = false;
    scanner->cursor += 1;
  }
  return false;
}

static bool read_quoted_import(char *cursor, TlImportEdge *edge) {
  cursor = skip_js_trivia(cursor);
  if (*cursor != '\'' && *cursor != '"') return false;
  char *end = quoted_end(cursor);
  if (!end) return false;
  edge->specifier = cursor + 1;
  edge->end = end;
  return true;
}

static bool read_call_import(TlJsScanner *scanner, TlImportEdge *edge) {
  char *cursor = skip_js_trivia(scanner->cursor);
  if (*cursor != '(') return false;
  if (!read_quoted_import(cursor + 1, edge)) return false;
  scanner->cursor = edge->end + 1;
  return true;
}

static bool read_direct_import(TlJsScanner *scanner, TlImportEdge *edge) {
  if (!read_quoted_import(scanner->cursor, edge)) return false;
  scanner->cursor = edge->end + 1;
  return true;
}

static bool read_import_keyword(TlJsScanner *scanner, TlImportEdge *edge) {
  scanner->expect_from = true;
  if (read_call_import(scanner, edge)) {
    scanner->expect_from = false;
    return true;
  }
  if (!read_direct_import(scanner, edge)) return false;
  scanner->expect_from = false;
  return true;
}

static bool bare_token(const TlJsScanner *scanner, const TlToken *token) {
  char *cursor = token->start;
  while (cursor > scanner->source_start) {
    const char previous = cursor[-1];
    const bool whitespace =
        previous == ' ' || previous == '\t' || previous == '\r' || previous == '\n';
    if (!whitespace) break;
    cursor -= 1;
  }
  return cursor == scanner->source_start || cursor[-1] != '.';
}

static bool import_token(TlJsScanner *scanner, const TlToken *token, TlImportEdge *edge) {
  const bool bare = bare_token(scanner, token);
  if (bare && token_is(token, "require")) return read_call_import(scanner, edge);
  if (bare && token_is(token, "export")) {
    scanner->expect_from = true;
    return false;
  }
  if (token_is(token, "from") && scanner->expect_from) {
    scanner->expect_from = false;
    return read_direct_import(scanner, edge);
  }
  if (bare && token_is(token, "import")) return read_import_keyword(scanner, edge);
  return false;
}

static bool next_js_import(TlJsScanner *scanner, TlImportEdge *edge) {
  TlToken token;
  while (next_js_token(scanner, &token)) {
    if (import_token(scanner, &token, edge)) return true;
  }
  return false;
}

static void advance_position(char **cursor, const char *target, size_t *line, size_t *column) {
  while (*cursor < target) {
    if (**cursor == '\n') {
      *line += 1;
      *column = 1;
    } else {
      *column += 1;
    }
    *cursor += 1;
  }
}

static bool add_js_import(TlImportList *list, TlImportEdge *edge, size_t line, size_t column) {
  const char saved = *edge->end;
  *edge->end = '\0';
  const bool added =
      tl_import_list_add(list, edge->specifier, line, column, TL_LANGUAGE_JAVASCRIPT);
  *edge->end = saved;
  return added;
}

static bool parse_javascript(char *content, TlImportList *list) {
  TlJsScanner scanner = {content, content, false};
  char *position = content;
  size_t line = 1;
  size_t column = 1;
  TlImportEdge edge;
  while (next_js_import(&scanner, &edge)) {
    advance_position(&position, edge.specifier, &line, &column);
    if (!add_js_import(list, &edge, line, column)) return false;
  }
  return true;
}

static char *skip_horizontal_space(char *cursor) {
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') cursor += 1;
  return cursor;
}

static bool python_identifier_part(char character) {
  const bool letter =
      (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
  return letter || character == '_' || (character >= '0' && character <= '9');
}

static bool python_keyword(const char *cursor, const char *keyword) {
  const size_t length = strlen(keyword);
  return strncmp(cursor, keyword, length) == 0 && !python_identifier_part(cursor[length]);
}

static bool write_python_prefix(char **output, size_t *remaining, size_t dots) {
  const size_t parents = dots == 0 ? 0 : dots - 1;
  const size_t length = dots == 1 ? 2 : parents * 3;
  if (length >= *remaining) return false;
  if (dots == 1) {
    memcpy(*output, "./", 2);
  } else {
    for (size_t index = 0; index < parents; index += 1) memcpy(*output + index * 3, "../", 3);
  }
  *output += length;
  *remaining -= length;
  return true;
}

static bool python_specifier(const char *module, char *output) {
  size_t dots = 0;
  while (module[dots] == '.') dots += 1;
  size_t remaining = TL_PATH_CAPACITY;
  char *cursor = output;
  if (!write_python_prefix(&cursor, &remaining, dots)) return false;
  for (const char *source = module + dots; *source; source += 1) {
    if (remaining <= 1) return false;
    *cursor++ = *source == '.' ? '/' : *source;
    remaining -= 1;
  }
  *cursor = '\0';
  return true;
}

static bool add_python_from(char *line, size_t line_number, TlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (!python_keyword(cursor, "from")) return true;
  cursor = skip_horizontal_space(cursor + strlen("from"));
  char *module = cursor;
  while (python_identifier_part(*cursor) || *cursor == '.') cursor += 1;
  char *after = skip_horizontal_space(cursor);
  if (cursor == module || !python_keyword(after, "import")) return true;
  const char saved = *cursor;
  *cursor = '\0';
  char specifier[TL_PATH_CAPACITY];
  const bool converted = python_specifier(module, specifier);
  *cursor = saved;
  const size_t column = (size_t)(module - line) + 1;
  return converted && tl_import_list_add(list, specifier, line_number, column, TL_LANGUAGE_PYTHON);
}

static bool add_python_import(char *line, size_t line_number, TlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  char *module = cursor;
  while (python_identifier_part(*cursor) || *cursor == '.') cursor += 1;
  if (cursor == module) return true;
  const char saved = *cursor;
  *cursor = '\0';
  char specifier[TL_PATH_CAPACITY];
  const bool converted = python_specifier(module, specifier);
  *cursor = saved;
  const size_t column = (size_t)(module - line) + 1;
  return converted && tl_import_list_add(list, specifier, line_number, column, TL_LANGUAGE_PYTHON);
}

static bool parse_python_line(char *line, size_t line_number, TlImportList *list) {
  if (!add_python_from(line, line_number, list)) return false;
  return add_python_import(line, line_number, list);
}

static bool parse_python(char *content, TlImportList *list) {
  char *line = content;
  size_t line_number = 1;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    const bool added = parse_python_line(line, line_number, list);
    if (next) *next = '\n';
    if (!added || !next) return added;
    line = next + 1;
    line_number += 1;
  }
  return true;
}

static char *find_import_quote(char *cursor) {
  while (*cursor) {
    if (*cursor == '"' || *cursor == '`') return cursor;
    if (line_comment_start(cursor)) return NULL;
    cursor += 1;
  }
  return NULL;
}

static bool add_quoted_specifier(char *line, char *cursor, size_t line_number, TlLanguage language,
                                 TlImportList *list) {
  char *quote = find_import_quote(cursor);
  if (!quote) return true;
  char *end = quoted_end(quote);
  if (!end) return true;
  const char saved = *end;
  *end = '\0';
  const size_t column = (size_t)(quote - line) + 2;
  const bool added = tl_import_list_add(list, quote + 1, line_number, column, language);
  *end = saved;
  return added;
}

static bool parse_go_line(char *line, size_t line_number, bool *block, TlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (*block && *cursor == ')') {
    *block = false;
    return true;
  }
  if (*block) return add_quoted_specifier(line, cursor, line_number, TL_LANGUAGE_GO, list);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  if (*cursor != '(') return add_quoted_specifier(line, cursor, line_number, TL_LANGUAGE_GO, list);
  *block = true;
  return add_quoted_specifier(line, cursor + 1, line_number, TL_LANGUAGE_GO, list);
}

static bool parse_go(char *content, TlImportList *list) {
  char *line = content;
  size_t line_number = 1;
  bool block = false;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    const bool added = parse_go_line(line, line_number, &block, list);
    if (next) *next = '\n';
    if (!added || !next) return added;
    line = next + 1;
    line_number += 1;
  }
  return true;
}

static char *skip_proto_modifier(char *cursor) {
  if (python_keyword(cursor, "public")) cursor += strlen("public");
  if (python_keyword(cursor, "weak")) cursor += strlen("weak");
  return skip_horizontal_space(cursor);
}

static bool parse_proto_line(char *line, size_t line_number, TlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  cursor = skip_proto_modifier(cursor);
  return add_quoted_specifier(line, cursor, line_number, TL_LANGUAGE_PROTO, list);
}

static bool parse_proto(char *content, TlImportList *list) {
  char *line = content;
  size_t line_number = 1;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    const bool added = parse_proto_line(line, line_number, list);
    if (next) *next = '\n';
    if (!added || !next) return added;
    line = next + 1;
    line_number += 1;
  }
  return true;
}

static bool path_has_extension(const char *path, const char *extension) {
  const char *actual = strrchr(path, '.');
  return actual && strcmp(actual, extension) == 0;
}

bool tl_parse_imports(const char *path, char *content, TlImportList *list) {
  if (path_has_extension(path, ".py")) return parse_python(content, list);
  if (path_has_extension(path, ".go")) return parse_go(content, list);
  if (path_has_extension(path, ".proto")) return parse_proto(content, list);
  return parse_javascript(content, list);
}
