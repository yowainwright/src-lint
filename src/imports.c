#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *start;
  size_t length;
} SlToken;

typedef struct {
  char *specifier;
  char *end;
} SlImportEdge;

typedef struct {
  char *source_start;
  char *cursor;
  bool expect_from;
  bool regex_allowed;
} SlJsScanner;

static char *duplicate_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, value, length);
  return copy;
}

static bool grow_imports(SlImportList *list) {
  const size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
  SlImport *items = realloc(list->items, capacity * sizeof(*items));
  if (!items) return false;
  list->items = items;
  list->capacity = capacity;
  return true;
}

bool sl_import_list_add(SlImportList *list, const char *specifier, size_t line, size_t column,
                        SlLanguage language) {
  if (list->count == list->capacity && !grow_imports(list)) return false;
  char *copy = duplicate_string(specifier);
  if (!copy) return false;
  list->items[list->count++] = (SlImport){copy, line, column, language};
  return true;
}

void sl_import_list_free(SlImportList *list) {
  for (size_t index = 0; index < list->count; index += 1) free(list->items[index].specifier);
  free(list->items);
  *list = (SlImportList){0};
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

static bool token_is(const SlToken *token, const char *value) {
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

static char *skip_regex_flags(char *cursor) {
  cursor += 1;
  while (identifier_part(*cursor)) cursor += 1;
  return cursor;
}

static char *regex_literal_end(char *cursor) {
  bool character_class = false;
  cursor += 1;
  while (*cursor && *cursor != '\n' && *cursor != '\r') {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 2;
      continue;
    }
    if (*cursor == '[') character_class = true;
    if (*cursor == ']') character_class = false;
    if (*cursor == '/' && !character_class) return skip_regex_flags(cursor);
    cursor += 1;
  }
  return NULL;
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

static bool read_identifier(SlJsScanner *scanner, SlToken *token) {
  if (!identifier_start(*scanner->cursor)) return false;
  token->start = scanner->cursor;
  scanner->cursor += 1;
  while (identifier_part(*scanner->cursor)) scanner->cursor += 1;
  token->length = (size_t)(scanner->cursor - token->start);
  return true;
}

static bool skip_js_regex(SlJsScanner *scanner) {
  if (*scanner->cursor != '/' || !scanner->regex_allowed) return false;
  char *end = regex_literal_end(scanner->cursor);
  if (!end) return false;
  scanner->cursor = end;
  scanner->regex_allowed = false;
  return true;
}

static bool skip_js_ignored(SlJsScanner *scanner) {
  if (line_comment_start(scanner->cursor)) {
    scanner->cursor = skip_line_comment(scanner->cursor);
    return true;
  }
  if (block_comment_start(scanner->cursor)) {
    scanner->cursor = skip_block_comment(scanner->cursor);
    return true;
  }
  if (skip_js_regex(scanner)) return true;
  if (!quote_character(*scanner->cursor)) return false;
  scanner->cursor = skip_quoted(scanner->cursor);
  scanner->regex_allowed = false;
  return true;
}

static bool regex_prefix_token(const SlToken *token) {
  return token_is(token, "await") || token_is(token, "case") || token_is(token, "delete") ||
         token_is(token, "do") || token_is(token, "else") || token_is(token, "in") ||
         token_is(token, "instanceof") || token_is(token, "new") || token_is(token, "of") ||
         token_is(token, "return") || token_is(token, "throw") || token_is(token, "typeof") ||
         token_is(token, "void") || token_is(token, "yield");
}

static bool read_js_token(SlJsScanner *scanner, SlToken *token) {
  if (!read_identifier(scanner, token)) return false;
  scanner->regex_allowed = regex_prefix_token(token);
  return true;
}

static bool js_whitespace(char character) {
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

static bool closing_js_token(char character) {
  return character == ')' || character == ']' || character == '}' || character == '.';
}

static void advance_js_character(SlJsScanner *scanner) {
  const char character = *scanner->cursor;
  if (js_whitespace(character)) {
    scanner->cursor += 1;
    return;
  }
  if (character >= '0' && character <= '9') {
    while (identifier_part(*scanner->cursor)) scanner->cursor += 1;
    scanner->regex_allowed = false;
    return;
  }
  if (character == ';') scanner->expect_from = false;
  scanner->regex_allowed = !closing_js_token(character);
  scanner->cursor += 1;
}

static bool next_js_token(SlJsScanner *scanner, SlToken *token) {
  while (*scanner->cursor) {
    if (skip_js_ignored(scanner)) continue;
    if (read_js_token(scanner, token)) return true;
    advance_js_character(scanner);
  }
  return false;
}

static bool has_template_substitution(const char *start, const char *end) {
  for (const char *cursor = start; cursor < end; cursor += 1) {
    if (*cursor == '\\' && cursor + 1 < end) {
      cursor += 1;
      continue;
    }
    if (*cursor == '$' && cursor + 1 < end && cursor[1] == '{') return true;
  }
  return false;
}

static bool read_module_literal(char *cursor, bool allow_template, SlImportEdge *edge) {
  cursor = skip_js_trivia(cursor);
  const bool quoted = *cursor == '\'' || *cursor == '"';
  if (!quoted && (!allow_template || *cursor != '`')) return false;
  char *end = quoted_end(cursor);
  if (!end) return false;
  if (*cursor == '`' && has_template_substitution(cursor + 1, end)) return false;
  edge->specifier = cursor + 1;
  edge->end = end;
  return true;
}

static bool read_call_import(SlJsScanner *scanner, SlImportEdge *edge) {
  char *cursor = skip_js_trivia(scanner->cursor);
  if (*cursor != '(') return false;
  if (!read_module_literal(cursor + 1, true, edge)) return false;
  scanner->cursor = edge->end + 1;
  return true;
}

static bool read_direct_import(SlJsScanner *scanner, SlImportEdge *edge) {
  if (!read_module_literal(scanner->cursor, false, edge)) return false;
  scanner->cursor = edge->end + 1;
  return true;
}

static bool read_import_keyword(SlJsScanner *scanner, SlImportEdge *edge) {
  scanner->expect_from = true;
  if (read_call_import(scanner, edge)) {
    scanner->expect_from = false;
    return true;
  }
  if (!read_direct_import(scanner, edge)) return false;
  scanner->expect_from = false;
  return true;
}

static bool bare_token(const SlJsScanner *scanner, const SlToken *token) {
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

static bool import_token(SlJsScanner *scanner, const SlToken *token, SlImportEdge *edge) {
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

static bool next_js_import(SlJsScanner *scanner, SlImportEdge *edge) {
  SlToken token;
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

static int hexadecimal_digit(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

static bool read_hexadecimal(const char **cursor, const char *end, size_t digits, uint32_t *value) {
  if ((size_t)(end - *cursor) < digits) return false;
  *value = 0;
  for (size_t index = 0; index < digits; index += 1) {
    const int digit = hexadecimal_digit((*cursor)[index]);
    if (digit < 0) return false;
    *value = (*value << 4) | (uint32_t)digit;
  }
  *cursor += digits;
  return true;
}

static bool read_braced_unicode(const char **cursor, const char *end, uint32_t *value) {
  *value = 0;
  size_t digits = 0;
  while (*cursor < end && **cursor != '}') {
    const int digit = hexadecimal_digit(**cursor);
    if (digit < 0 || digits == 6) return false;
    *value = (*value << 4) | (uint32_t)digit;
    *cursor += 1;
    digits += 1;
  }
  if (digits == 0 || *cursor == end || **cursor != '}') return false;
  *cursor += 1;
  return *value <= 0x10ffff;
}

static bool read_unicode_escape(const char **cursor, const char *end, uint32_t *value) {
  if (**cursor != '{') return read_hexadecimal(cursor, end, 4, value);
  *cursor += 1;
  return read_braced_unicode(cursor, end, value);
}

static void append_two_byte_utf8(char **output, uint32_t value) {
  *(*output)++ = (char)(0xc0 | (value >> 6));
  *(*output)++ = (char)(0x80 | (value & 0x3f));
}

static void append_three_byte_utf8(char **output, uint32_t value) {
  *(*output)++ = (char)(0xe0 | (value >> 12));
  *(*output)++ = (char)(0x80 | ((value >> 6) & 0x3f));
  *(*output)++ = (char)(0x80 | (value & 0x3f));
}

static void append_four_byte_utf8(char **output, uint32_t value) {
  *(*output)++ = (char)(0xf0 | (value >> 18));
  *(*output)++ = (char)(0x80 | ((value >> 12) & 0x3f));
  *(*output)++ = (char)(0x80 | ((value >> 6) & 0x3f));
  *(*output)++ = (char)(0x80 | (value & 0x3f));
}

static bool append_utf8(char **output, uint32_t value) {
  if (value == 0 || value > 0x10ffff) return false;
  if (value <= 0x7f) {
    *(*output)++ = (char)value;
    return true;
  }
  if (value <= 0x7ff) {
    append_two_byte_utf8(output, value);
    return true;
  }
  if (value >= 0xd800 && value <= 0xdfff) return false;
  if (value <= 0xffff) {
    append_three_byte_utf8(output, value);
    return true;
  }
  append_four_byte_utf8(output, value);
  return true;
}

static char simple_escape(char character) {
  if (character == 'b') return '\b';
  if (character == 'f') return '\f';
  if (character == 'n') return '\n';
  if (character == 'r') return '\r';
  if (character == 't') return '\t';
  if (character == 'v') return '\v';
  return character;
}

static void skip_optional_line_feed(const char **cursor, const char *end) {
  if (*cursor < end && **cursor == '\n') *cursor += 1;
}

static bool decode_js_escape(const char **cursor, const char *end, char **output) {
  if (*cursor == end) return false;
  const char escaped = *(*cursor)++;
  if (escaped == '\n') return true;
  if (escaped == '\r') {
    skip_optional_line_feed(cursor, end);
    return true;
  }
  uint32_t value;
  if (escaped == 'x') return read_hexadecimal(cursor, end, 2, &value) && append_utf8(output, value);
  if (escaped == 'u') return read_unicode_escape(cursor, end, &value) && append_utf8(output, value);
  if (escaped == '0') return false;
  *(*output)++ = simple_escape(escaped);
  return true;
}

static char *decode_js_literal(const SlImportEdge *edge) {
  const size_t length = (size_t)(edge->end - edge->specifier);
  char *decoded = malloc(length + 1);
  if (!decoded) return NULL;
  const char *cursor = edge->specifier;
  char *output = decoded;
  while (cursor < edge->end) {
    if (*cursor != '\\') {
      *output++ = *cursor++;
      continue;
    }
    cursor += 1;
    if (decode_js_escape(&cursor, edge->end, &output)) continue;
    free(decoded);
    return NULL;
  }
  *output = '\0';
  return decoded;
}

static bool add_js_import(SlImportList *list, SlImportEdge *edge, size_t line, size_t column) {
  char *specifier = decode_js_literal(edge);
  if (!specifier) return false;
  const bool added = sl_import_list_add(list, specifier, line, column, SL_LANGUAGE_JAVASCRIPT);
  free(specifier);
  return added;
}

static bool parse_javascript(char *content, SlImportList *list) {
  SlJsScanner scanner = {content, content, false, true};
  char *position = content;
  size_t line = 1;
  size_t column = 1;
  SlImportEdge edge;
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
  size_t remaining = SL_PATH_CAPACITY;
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

static bool add_python_from(char *line, char *statement, size_t line_number, SlImportList *list) {
  char *cursor = skip_horizontal_space(statement);
  if (!python_keyword(cursor, "from")) return true;
  cursor = skip_horizontal_space(cursor + strlen("from"));
  char *module = cursor;
  while (python_identifier_part(*cursor) || *cursor == '.') cursor += 1;
  char *after = skip_horizontal_space(cursor);
  if (cursor == module || !python_keyword(after, "import")) return true;
  const char saved = *cursor;
  *cursor = '\0';
  char specifier[SL_PATH_CAPACITY];
  const bool converted = python_specifier(module, specifier);
  *cursor = saved;
  const size_t column = (size_t)(module - line) + 1;
  return converted && sl_import_list_add(list, specifier, line_number, column, SL_LANGUAGE_PYTHON);
}

static bool add_python_module(char *line, char *module, char *end, size_t line_number,
                              SlImportList *list) {
  const char saved = *end;
  *end = '\0';
  char specifier[SL_PATH_CAPACITY];
  const bool converted = python_specifier(module, specifier);
  *end = saved;
  const size_t column = (size_t)(module - line) + 1;
  return converted && sl_import_list_add(list, specifier, line_number, column, SL_LANGUAGE_PYTHON);
}

static char *skip_python_alias(char *cursor) {
  cursor = skip_horizontal_space(cursor);
  if (!python_keyword(cursor, "as")) return cursor;
  cursor = skip_horizontal_space(cursor + strlen("as"));
  while (python_identifier_part(*cursor)) cursor += 1;
  return skip_horizontal_space(cursor);
}

static bool add_python_import(char *line, char *statement, size_t line_number, SlImportList *list) {
  char *cursor = skip_horizontal_space(statement);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  while (*cursor) {
    char *module = cursor;
    while (python_identifier_part(*cursor) || *cursor == '.') cursor += 1;
    if (cursor == module) return true;
    if (!add_python_module(line, module, cursor, line_number, list)) return false;
    cursor = skip_python_alias(cursor);
    if (*cursor != ',') return true;
    cursor = skip_horizontal_space(cursor + 1);
  }
  return true;
}

static bool parse_python_statement(char *line, char *statement, size_t line_number,
                                   SlImportList *list) {
  if (!add_python_from(line, statement, line_number, list)) return false;
  return add_python_import(line, statement, line_number, list);
}

static bool parse_python_line(char *line, size_t line_number, SlImportList *list) {
  char *statement = line;
  while (*statement) {
    char *separator = strchr(statement, ';');
    if (separator) *separator = '\0';
    const bool parsed = parse_python_statement(line, statement, line_number, list);
    if (separator) *separator = ';';
    if (!parsed || !separator) return parsed;
    statement = separator + 1;
  }
  return true;
}

typedef struct {
  char triple_quote;
} SlPythonState;

static bool python_quote_character(char character) { return character == '\'' || character == '"'; }

static bool python_triple_quote(const char *cursor) {
  return python_quote_character(*cursor) && cursor[1] == *cursor && cursor[2] == *cursor;
}

static char *mask_python_escape(char *cursor) {
  *cursor++ = ' ';
  if (*cursor) *cursor++ = ' ';
  return cursor;
}

static char *mask_python_triple(char *cursor, SlPythonState *state) {
  while (*cursor) {
    if (*cursor == '\\') {
      cursor = mask_python_escape(cursor);
      continue;
    }
    const bool closes = cursor[0] == state->triple_quote && cursor[1] == state->triple_quote &&
                        cursor[2] == state->triple_quote;
    if (closes) {
      memset(cursor, ' ', 3);
      state->triple_quote = '\0';
      return cursor + 3;
    }
    *cursor++ = ' ';
  }
  return cursor;
}

static char *mask_python_quoted(char *cursor) {
  const char quote = *cursor;
  *cursor++ = ' ';
  while (*cursor) {
    if (*cursor == '\\') {
      cursor = mask_python_escape(cursor);
      continue;
    }
    const bool closes = *cursor == quote;
    *cursor++ = ' ';
    if (closes) return cursor;
  }
  return cursor;
}

static void mask_python_line(char *line, SlPythonState *state) {
  char *cursor = line;
  while (*cursor) {
    if (state->triple_quote) {
      cursor = mask_python_triple(cursor, state);
      continue;
    }
    if (*cursor == '#') {
      memset(cursor, ' ', strlen(cursor));
      return;
    }
    if (!python_quote_character(*cursor)) {
      cursor += 1;
      continue;
    }
    if (!python_triple_quote(cursor)) {
      cursor = mask_python_quoted(cursor);
      continue;
    }
    state->triple_quote = *cursor;
    memset(cursor, ' ', 3);
    cursor += 3;
  }
}

static bool parse_python(char *content, SlImportList *list) {
  char *code = duplicate_string(content);
  if (!code) return false;
  char *line = code;
  size_t line_number = 1;
  bool parsed = true;
  SlPythonState state = {0};
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    mask_python_line(line, &state);
    parsed = parse_python_line(line, line_number, list);
    if (!parsed || !next) break;
    line = next + 1;
    line_number += 1;
  }
  free(code);
  return parsed;
}

typedef struct {
  bool block_comment;
  bool raw_string;
} SlCState;

static char *mask_c_block_comment(char *cursor, SlCState *state) {
  while (*cursor) {
    const bool closes = cursor[0] == '*' && cursor[1] == '/';
    if (closes) {
      cursor[0] = ' ';
      cursor[1] = ' ';
      state->block_comment = false;
      return cursor + 2;
    }
    *cursor++ = ' ';
  }
  return cursor;
}

static char *mask_to_line_end(char *cursor) {
  const size_t length = strlen(cursor);
  memset(cursor, ' ', length);
  return cursor + length;
}

static char *mask_go_raw_string(char *cursor, SlCState *state) {
  char *end = strchr(cursor, '`');
  if (!end) return mask_to_line_end(cursor);
  memset(cursor, ' ', (size_t)(end - cursor) + 1);
  state->raw_string = false;
  return end + 1;
}

static bool start_go_raw_string(char *cursor, SlCState *state) {
  if (*cursor != '`' || strchr(cursor + 1, '`')) return false;
  state->raw_string = true;
  return true;
}

static void mask_c_comments(char *line, SlCState *state, bool track_raw_strings) {
  char *cursor = line;
  while (*cursor) {
    if (state->raw_string) {
      cursor = mask_go_raw_string(cursor, state);
      continue;
    }
    if (state->block_comment) {
      cursor = mask_c_block_comment(cursor, state);
      continue;
    }
    if (line_comment_start(cursor)) {
      memset(cursor, ' ', strlen(cursor));
      return;
    }
    if (block_comment_start(cursor)) {
      cursor[0] = ' ';
      cursor[1] = ' ';
      state->block_comment = true;
      cursor += 2;
      continue;
    }
    if (track_raw_strings && start_go_raw_string(cursor, state)) return;
    cursor = quote_character(*cursor) ? skip_quoted(cursor) : cursor + 1;
  }
}

static char *find_import_quote(char *cursor) {
  while (*cursor) {
    if (*cursor == '"' || *cursor == '`') return cursor;
    if (line_comment_start(cursor)) return NULL;
    cursor += 1;
  }
  return NULL;
}

static bool add_quoted_specifier(char *line, char *cursor, size_t line_number, SlLanguage language,
                                 SlImportList *list) {
  char *quote = find_import_quote(cursor);
  if (!quote) return true;
  char *end = quoted_end(quote);
  if (!end) return true;
  const char saved = *end;
  *end = '\0';
  const size_t column = (size_t)(quote - line) + 2;
  const bool added = sl_import_list_add(list, quote + 1, line_number, column, language);
  *end = saved;
  return added;
}

static bool parse_go_line(char *line, size_t line_number, bool *block, SlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (*block && *cursor == ')') {
    *block = false;
    return true;
  }
  if (*block) return add_quoted_specifier(line, cursor, line_number, SL_LANGUAGE_GO, list);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  if (*cursor != '(') return add_quoted_specifier(line, cursor, line_number, SL_LANGUAGE_GO, list);
  *block = true;
  return add_quoted_specifier(line, cursor + 1, line_number, SL_LANGUAGE_GO, list);
}

static bool parse_go(char *content, SlImportList *list) {
  char *code = duplicate_string(content);
  if (!code) return false;
  char *line = code;
  size_t line_number = 1;
  bool block = false;
  SlCState state = {0};
  bool parsed = true;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    mask_c_comments(line, &state, true);
    parsed = parse_go_line(line, line_number, &block, list);
    if (!parsed || !next) break;
    line = next + 1;
    line_number += 1;
  }
  free(code);
  return parsed;
}

static char *skip_proto_modifier(char *cursor) {
  if (python_keyword(cursor, "public")) cursor += strlen("public");
  if (python_keyword(cursor, "weak")) cursor += strlen("weak");
  return skip_horizontal_space(cursor);
}

static bool parse_proto_line(char *line, size_t line_number, SlImportList *list) {
  char *cursor = skip_horizontal_space(line);
  if (!python_keyword(cursor, "import")) return true;
  cursor = skip_horizontal_space(cursor + strlen("import"));
  cursor = skip_proto_modifier(cursor);
  return add_quoted_specifier(line, cursor, line_number, SL_LANGUAGE_PROTO, list);
}

static bool parse_proto(char *content, SlImportList *list) {
  char *code = duplicate_string(content);
  if (!code) return false;
  char *line = code;
  size_t line_number = 1;
  SlCState state = {0};
  bool parsed = true;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    mask_c_comments(line, &state, false);
    parsed = parse_proto_line(line, line_number, list);
    if (!parsed || !next) break;
    line = next + 1;
    line_number += 1;
  }
  free(code);
  return parsed;
}

static bool path_has_extension(const char *path, const char *extension) {
  const char *actual = strrchr(path, '.');
  return actual && strcmp(actual, extension) == 0;
}

bool sl_parse_imports(const char *path, char *content, SlImportList *list) {
  if (path_has_extension(path, ".py")) return parse_python(content, list);
  if (path_has_extension(path, ".go")) return parse_go(content, list);
  if (path_has_extension(path, ".proto")) return parse_proto(content, list);
  return parse_javascript(content, list);
}
