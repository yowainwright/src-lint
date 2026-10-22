#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Lexical adapters for the static forms documented in README.md.
 * Language references and resolution limits are linked there. */
typedef enum { TOKEN_END, TOKEN_WORD, TOKEN_STRING, TOKEN_SYMBOL } SlTokenKind;

typedef struct {
  const char *start;
  size_t length;
  size_t line;
  size_t column;
  SlTokenKind kind;
  char quote;
  bool literal;
} SlNativeToken;

typedef struct {
  const char *start;
  const char *cursor;
  size_t line;
  size_t column;
  SlLanguage language;
} SlNativeScanner;

static bool begins(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void advance(SlNativeScanner *scanner) {
  if (!*scanner->cursor) return;
  if (*scanner->cursor++ == '\n') {
    scanner->line += 1;
    scanner->column = 1;
    return;
  }
  scanner->column += 1;
}

static void advance_bytes(SlNativeScanner *scanner, size_t count) {
  for (size_t index = 0; index < count; index += 1) advance(scanner);
}

static void skip_line(SlNativeScanner *scanner) {
  while (*scanner->cursor && *scanner->cursor != '\n') advance(scanner);
}

static void skip_block(SlNativeScanner *scanner, const char *open, const char *close) {
  size_t depth = 1;
  advance_bytes(scanner, strlen(open));
  while (*scanner->cursor && depth) {
    if (begins(scanner->cursor, close)) {
      depth -= 1;
      advance_bytes(scanner, strlen(close));
    } else if (scanner->language != SL_LANGUAGE_JAVA && begins(scanner->cursor, open)) {
      depth += 1;
      advance_bytes(scanner, strlen(open));
    } else {
      advance(scanner);
    }
  }
}

static bool word_part(char character) {
  const unsigned char byte = (unsigned char)character;
  return byte >= 0x80 || isalnum(byte) || character == '_' || character == '$';
}

static bool ruby_marker(const SlNativeScanner *scanner, const char *marker) {
  if (scanner->language != SL_LANGUAGE_RUBY || scanner->column != 1) return false;
  return begins(scanner->cursor, marker) && !word_part(scanner->cursor[strlen(marker)]);
}

static bool skip_ruby_document(SlNativeScanner *scanner) {
  if (!ruby_marker(scanner, "=begin")) return false;
  skip_line(scanner);
  while (*scanner->cursor) {
    advance(scanner);
    if (ruby_marker(scanner, "=end")) break;
    skip_line(scanner);
  }
  skip_line(scanner);
  return true;
}

static bool skip_comment(SlNativeScanner *scanner) {
  const bool hash = scanner->language == SL_LANGUAGE_RUBY ||
                    scanner->language == SL_LANGUAGE_JULIA ||
                    scanner->language == SL_LANGUAGE_TERRAFORM;
  if (scanner->language == SL_LANGUAGE_JULIA && begins(scanner->cursor, "#=")) {
    skip_block(scanner, "#=", "=#");
    return true;
  }
  if ((!hash || scanner->language == SL_LANGUAGE_TERRAFORM) && begins(scanner->cursor, "/*")) {
    skip_block(scanner, "/*", "*/");
    return true;
  }
  const bool slash = !hash || scanner->language == SL_LANGUAGE_TERRAFORM;
  if ((hash && *scanner->cursor == '#') || (slash && begins(scanner->cursor, "//"))) {
    skip_line(scanner);
    return true;
  }
  return skip_ruby_document(scanner);
}

static void skip_trivia(SlNativeScanner *scanner) {
  for (;;) {
    while (isspace((unsigned char)*scanner->cursor)) advance(scanner);
    if (!skip_comment(scanner)) return;
  }
}

static bool interpolated(const SlNativeScanner *scanner) {
  if (scanner->language == SL_LANGUAGE_JULIA) return *scanner->cursor == '$';
  if (scanner->language == SL_LANGUAGE_RUBY) return begins(scanner->cursor, "#{");
  return begins(scanner->cursor, "${") || begins(scanner->cursor, "%{");
}

static bool skip_quoted_content(SlNativeScanner *scanner, char quote) {
  advance(scanner);
  while (*scanner->cursor && *scanner->cursor != quote) {
    if (*scanner->cursor == '\\') advance(scanner);
    advance(scanner);
  }
  if (!*scanner->cursor) return false;
  advance(scanner);
  return true;
}

static void skip_interpolation_body(SlNativeScanner *scanner, char open, char close) {
  size_t depth = 1;
  advance_bytes(scanner, 2);
  while (*scanner->cursor && depth) {
    const char character = *scanner->cursor;
    if (character == '\'' || character == '"') {
      skip_quoted_content(scanner, character);
      continue;
    }
    if (character == open) depth += 1;
    if (character == close) depth -= 1;
    advance(scanner);
  }
}

static bool skip_interpolation(SlNativeScanner *scanner) {
  const bool parentheses =
      (scanner->language == SL_LANGUAGE_JULIA && begins(scanner->cursor, "$(")) ||
      (scanner->language == SL_LANGUAGE_SWIFT && begins(scanner->cursor, "\\("));
  const bool braces =
      (scanner->language == SL_LANGUAGE_RUBY && begins(scanner->cursor, "#{")) ||
      ((scanner->language == SL_LANGUAGE_TERRAFORM || scanner->language == SL_LANGUAGE_KOTLIN) &&
       begins(scanner->cursor, "${"));
  if (!parentheses && !braces) return false;
  const char open = parentheses ? '(' : '{';
  const char close = parentheses ? ')' : '}';
  skip_interpolation_body(scanner, open, close);
  return true;
}

static void advance_string_content(SlNativeScanner *scanner, SlNativeToken *token, bool raw) {
  if (token->quote != '\'' && interpolated(scanner)) token->literal = false;
  if (token->quote != '\'' && skip_interpolation(scanner)) {
    token->literal = false;
    return;
  }
  if (!raw && *scanner->cursor == '\\') advance(scanner);
  advance(scanner);
}

static void read_string_body(SlNativeScanner *scanner, SlNativeToken *token, size_t width) {
  const char quote = token->quote;
  char triple[] = {quote, quote, quote, '\0'};
  const bool raw = width == 3 && scanner->language == SL_LANGUAGE_KOTLIN;
  while (*scanner->cursor) {
    const bool end = width == 3 ? begins(scanner->cursor, triple) : *scanner->cursor == quote;
    if (end) {
      advance_bytes(scanner, width);
      return;
    }
    advance_string_content(scanner, token, raw);
  }
  token->literal = false;
}

static void read_string(SlNativeScanner *scanner, SlNativeToken *token) {
  token->quote = *scanner->cursor;
  const bool multiline = begins(scanner->cursor, "\"\"\"");
  const size_t width = multiline ? 3 : 1;
  token->literal = !multiline && token->quote != '`';
  advance_bytes(scanner, width);
  read_string_body(scanner, token, width);
}

static bool raw_string_end(const char *end, size_t width, size_t hashes) {
  size_t quotes = 0;
  while (quotes < width && end[quotes] == '"') quotes += 1;
  size_t count = 0;
  while (quotes == width && count < hashes && end[quotes + count] == '#') count += 1;
  return count == hashes;
}

static bool skip_swift_raw_string(SlNativeScanner *scanner) {
  if (scanner->language != SL_LANGUAGE_SWIFT || *scanner->cursor != '#') return false;
  const char *quote = scanner->cursor;
  while (*quote == '#') quote += 1;
  if (*quote != '"') return false;
  const size_t hashes = (size_t)(quote - scanner->cursor);
  const size_t width = begins(quote, "\"\"\"") ? 3 : 1;
  advance_bytes(scanner, hashes + width);
  while (*scanner->cursor) {
    if (raw_string_end(scanner->cursor, width, hashes)) {
      advance_bytes(scanner, width + hashes);
      return true;
    }
    advance(scanner);
  }
  return true;
}

static void skip_percent_body(SlNativeScanner *scanner, char open, char close) {
  size_t depth = 1;
  while (*scanner->cursor && depth) {
    if (*scanner->cursor == '\\') {
      advance_bytes(scanner, 2);
      continue;
    }
    if (*scanner->cursor == close)
      depth -= 1;
    else if (open != close && *scanner->cursor == open)
      depth += 1;
    advance(scanner);
  }
}

static bool skip_ruby_percent(SlNativeScanner *scanner) {
  if (scanner->language != SL_LANGUAGE_RUBY || *scanner->cursor != '%') return false;
  const char *delimiter = scanner->cursor + 1;
  if (*delimiter && strchr("qQwWiIrxs", *delimiter)) delimiter += 1;
  if (!*delimiter || word_part(*delimiter) || isspace((unsigned char)*delimiter)) return false;
  static const char openings[] = "([{<";
  const char *paired = strchr(openings, *delimiter);
  const char close = paired ? ")]}>"[paired - openings] : *delimiter;
  const char open = *delimiter;
  advance_bytes(scanner, (size_t)(delimiter - scanner->cursor) + 1);
  skip_percent_body(scanner, open, close);
  return true;
}

static bool ruby_literal_prefix(const SlNativeScanner *scanner) {
  const char *before = scanner->cursor;
  while (before > scanner->start && (before[-1] == ' ' || before[-1] == '\t')) before -= 1;
  if (before == scanner->start || strchr("\n=([{,:;!~", before[-1])) return true;
  const bool argument = before < scanner->cursor && !isspace((unsigned char)scanner->cursor[1]);
  if (!argument) return false;
  const char *end = before;
  while (before > scanner->start && word_part(before[-1])) before -= 1;
  return before < end && (islower((unsigned char)*before) || *before == '_');
}

static bool ruby_regex(const SlNativeScanner *scanner) {
  const bool supported = scanner->language == SL_LANGUAGE_RUBY && *scanner->cursor == '/';
  return supported && ruby_literal_prefix(scanner);
}

static bool julia_adjoint(const SlNativeScanner *scanner) {
  if (scanner->language != SL_LANGUAGE_JULIA || *scanner->cursor != '\'') return false;
  if (scanner->cursor == scanner->start) return false;
  const char before = scanner->cursor[-1];
  return word_part(before) || before == ')' || before == ']' || before == '.';
}

static bool heredoc_end(const char *line, const char *label, size_t length) {
  if (strncmp(line, label, length) != 0) return false;
  const char next = line[length];
  return next == '\0' || next == '\r' || next == '\n';
}

static bool skip_heredoc_body(SlNativeScanner *scanner, const char *label, size_t length,
                              bool indented) {
  SlNativeScanner body = *scanner;
  skip_line(&body);
  while (*body.cursor) {
    advance(&body);
    if (indented) {
      while (*body.cursor == ' ' || *body.cursor == '\t') advance(&body);
    }
    if (heredoc_end(body.cursor, label, length)) {
      skip_line(&body);
      *scanner = body;
      return true;
    }
    skip_line(&body);
  }
  return false;
}

static size_t heredoc_label_length(const char *label, char quote) {
  const char *end = label;
  if (!quote) {
    const bool identifier = word_part(*label) && !isdigit((unsigned char)*label) && *label != '$';
    if (!identifier) return 0;
    while (word_part(*end)) end += 1;
    return (size_t)(end - label);
  }
  while (*end && *end != quote && *end != '\r' && *end != '\n') end += 1;
  if (*end != quote) return 0;
  return (size_t)(end - label);
}

static bool skip_heredoc(SlNativeScanner *scanner) {
  const bool ruby = scanner->language == SL_LANGUAGE_RUBY && ruby_literal_prefix(scanner);
  const bool supported = ruby || scanner->language == SL_LANGUAGE_TERRAFORM;
  if (!supported || !begins(scanner->cursor, "<<")) return false;
  const char *label = scanner->cursor + 2;
  const bool modifier = *label == '-' || *label == '~';
  const bool indented = modifier || scanner->language == SL_LANGUAGE_TERRAFORM;
  if (modifier) label += 1;
  const bool quoted = *label == '\'' || *label == '"' || *label == '`';
  const char quote = quoted ? *label : '\0';
  if (quoted) label += 1;
  const size_t length = heredoc_label_length(label, quote);
  if (!length) return false;
  return skip_heredoc_body(scanner, label, length, indented);
}

static bool skip_special_string(SlNativeScanner *scanner) {
  if (skip_heredoc(scanner) || skip_swift_raw_string(scanner) || skip_ruby_percent(scanner))
    return true;
  if (!ruby_regex(scanner)) return false;
  SlNativeScanner regex = *scanner;
  if (!skip_quoted_content(&regex, '/')) return false;
  *scanner = regex;
  return true;
}

static void read_token(SlNativeScanner *scanner, SlNativeToken *token) {
  const char character = *scanner->cursor;
  if (skip_special_string(scanner)) {
    token->kind = TOKEN_STRING;
  } else if (julia_adjoint(scanner)) {
    advance(scanner);
  } else if (character == '"' || character == '\'' || character == '`') {
    token->kind = TOKEN_STRING;
    read_string(scanner, token);
  } else if (word_part(character)) {
    token->kind = TOKEN_WORD;
    while (word_part(*scanner->cursor)) advance(scanner);
  } else {
    advance(scanner);
  }
}

static SlNativeToken next_token(SlNativeScanner *scanner) {
  skip_trivia(scanner);
  SlNativeToken token = {.start = scanner->cursor,
                         .line = scanner->line,
                         .column = scanner->column,
                         .kind = TOKEN_SYMBOL};
  if (!*scanner->cursor || ruby_marker(scanner, "__END__")) {
    while (*scanner->cursor) advance(scanner);
    token.kind = TOKEN_END;
    return token;
  }
  read_token(scanner, &token);
  token.length = (size_t)(scanner->cursor - token.start);
  return token;
}

static bool token_is(SlNativeToken token, const char *value) {
  return token.length == strlen(value) && strncmp(token.start, value, token.length) == 0;
}

static SlNativeToken peek_token(SlNativeScanner scanner) { return next_token(&scanner); }

static bool add_token(SlImportList *list, SlNativeToken token, const char *specifier,
                      SlLanguage language) {
  return sl_import_list_add(list, specifier, token.line, token.column, language);
}

static bool literal_character(SlNativeToken token, size_t *index, char *character) {
  *character = token.start[*index];
  if (*character != '\\') return true;
  *character = token.start[++*index];
  return *character == '\\' || *character == token.quote;
}

static bool decode_literal(SlNativeToken token, char *value) {
  if (!token.literal || token.length < 2 || token.length >= SL_PATH_CAPACITY) return false;
  char *output = value;
  for (size_t index = 1; index + 1 < token.length; index += 1) {
    char character;
    if (!literal_character(token, &index, &character)) return false;
    *output++ = character;
  }
  *output = '\0';
  return output != value;
}

static bool relative_literal(char *value) {
  if (value[0] == '/' || begins(value, "./") || begins(value, "../")) return true;
  const size_t length = strlen(value);
  if (length + 2 >= SL_PATH_CAPACITY) return false;
  memmove(value + 2, value, length + 1);
  memcpy(value, "./", 2);
  return true;
}

static bool ruby_require_path(char *value) {
  if (!begins(value, "./") && !begins(value, "../")) return true;
  char directory[SL_PATH_CAPACITY];
  if (!getcwd(directory, sizeof(directory))) return false;
  char absolute[SL_PATH_CAPACITY];
  const int written = snprintf(absolute, sizeof(absolute), "%s/%s", directory, value);
  if (written < 0 || (size_t)written >= sizeof(absolute)) return false;
  strcpy(value, absolute);
  return true;
}

static bool add_literal(SlImportList *list, SlNativeToken token, SlLanguage language,
                        bool relative) {
  char value[SL_PATH_CAPACITY];
  if (token.length >= sizeof(value)) return false;
  if (!decode_literal(token, value)) return true;
  if (relative && !relative_literal(value)) return false;
  if (!relative && language == SL_LANGUAGE_RUBY && !ruby_require_path(value)) return false;
  token.column += 1;
  return add_token(list, token, value, language);
}

static bool parse_call(SlNativeScanner *scanner, SlNativeToken keyword, SlImportList *list) {
  SlNativeScanner call = *scanner;
  SlNativeToken path = next_token(&call);
  const bool parentheses = token_is(path, "(");
  if (parentheses) path = next_token(&call);
  if (path.kind != TOKEN_STRING || !path.literal) return true;
  const SlNativeToken after = peek_token(call);
  if (parentheses && !token_is(after, ")")) return true;
  if (!parentheses && after.kind != TOKEN_END && after.line == path.line && !token_is(after, ";") &&
      !token_is(after, "if") && !token_is(after, "unless"))
    return true;
  const bool relative = !token_is(keyword, "require");
  *scanner = call;
  return add_literal(list, path, scanner->language, relative);
}

static bool append_name(char *name, size_t *length, SlNativeToken token) {
  const size_t prefix = *length ? 1 : 0;
  if (*length + prefix + token.length >= SL_PATH_CAPACITY) return false;
  if (prefix) name[(*length)++] = '/';
  memcpy(name + *length, token.start, token.length);
  *length += token.length;
  name[*length] = '\0';
  return true;
}

static bool read_name(SlNativeScanner *scanner, SlNativeToken first, char *name) {
  size_t length = 0;
  if (!append_name(name, &length, first)) return false;
  while (token_is(peek_token(*scanner), ".")) {
    (void)next_token(scanner);
    const SlNativeToken part = next_token(scanner);
    if (token_is(part, "*")) return true;
    if (part.kind != TOKEN_WORD) return false;
    if (!append_name(name, &length, part)) return false;
  }
  return true;
}

static bool swift_import_kind(SlNativeToken token) {
  return token_is(token, "typealias") || token_is(token, "struct") || token_is(token, "class") ||
         token_is(token, "enum") || token_is(token, "protocol") || token_is(token, "let") ||
         token_is(token, "var") || token_is(token, "func");
}

static bool add_named_import(SlNativeScanner *scanner, SlNativeToken first, SlImportList *list) {
  char name[SL_PATH_CAPACITY];
  if (!read_name(scanner, first, name)) return false;
  char *member = strchr(name, '/');
  if (scanner->language == SL_LANGUAGE_SWIFT && member) *member = '\0';
  return add_token(list, first, name, scanner->language);
}

static bool next_julia_import(SlNativeScanner *scanner) {
  if (scanner->language != SL_LANGUAGE_JULIA) return false;
  if (token_is(peek_token(*scanner), "as")) {
    (void)next_token(scanner);
    (void)next_token(scanner);
  }
  if (!token_is(peek_token(*scanner), ",")) return false;
  (void)next_token(scanner);
  return true;
}

static bool parse_named_import(SlNativeScanner *scanner, SlImportList *list) {
  SlNativeToken first = next_token(scanner);
  if (scanner->language == SL_LANGUAGE_JAVA && token_is(first, "static"))
    first = next_token(scanner);
  if (scanner->language == SL_LANGUAGE_SWIFT && swift_import_kind(first))
    first = next_token(scanner);
  while (first.kind == TOKEN_WORD) {
    if (!add_named_import(scanner, first, list)) return false;
    if (!next_julia_import(scanner)) return true;
    first = next_token(scanner);
  }
  return true;
}

static bool parse_module_source(SlNativeScanner *scanner, SlImportList *list) {
  SlNativeScanner assignment = *scanner;
  if (!token_is(next_token(&assignment), "=")) return true;
  const SlNativeToken value = next_token(&assignment);
  if (value.kind != TOKEN_STRING || !value.literal) return true;
  char path[SL_PATH_CAPACITY];
  if (value.length >= sizeof(path)) return false;
  if (!decode_literal(value, path)) return true;
  const bool local = begins(path, "./") || begins(path, "../") || path[0] == '/';
  if (!local) return true;
  const SlNativeToken after = peek_token(assignment);
  if (after.line == value.line && after.kind != TOKEN_END && !token_is(after, "}")) return true;
  *scanner = assignment;
  return add_literal(list, value, SL_LANGUAGE_TERRAFORM, false);
}

static bool terraform_module(SlNativeScanner *scanner) {
  SlNativeScanner header = *scanner;
  if (next_token(&header).kind != TOKEN_STRING || !token_is(next_token(&header), "{")) return false;
  *scanner = header;
  return true;
}

static size_t brace_depth(size_t depth, SlNativeToken token) {
  if (token_is(token, "{")) return depth + 1;
  if (depth && token_is(token, "}")) return depth - 1;
  return depth;
}

static bool parse_terraform(SlNativeScanner *scanner, SlImportList *list) {
  size_t depth = 0;
  bool module = false;
  SlNativeToken token;
  while ((token = next_token(scanner)).kind != TOKEN_END) {
    if (!depth && token_is(token, "module") && terraform_module(scanner)) {
      module = true;
      depth = 1;
      continue;
    }
    depth = brace_depth(depth, token);
    if (!depth) module = false;
    const bool source = module && depth == 1 && token_is(token, "source");
    if (source && !parse_module_source(scanner, list)) return false;
  }
  return true;
}

static bool parse_native_token(SlNativeScanner *scanner, SlNativeToken token, SlImportList *list) {
  const bool ruby_call = scanner->language == SL_LANGUAGE_RUBY &&
                         (token_is(token, "require") || token_is(token, "require_relative"));
  if (ruby_call) return parse_call(scanner, token, list);
  if (scanner->language == SL_LANGUAGE_RUBY) return true;
  if (scanner->language == SL_LANGUAGE_JULIA && token_is(token, "include"))
    return parse_call(scanner, token, list);
  if (token_is(token, "import")) return parse_named_import(scanner, list);
  if (scanner->language == SL_LANGUAGE_JULIA && token_is(token, "using"))
    return parse_named_import(scanner, list);
  return true;
}

bool sl_parse_native_imports(const char *content, SlLanguage language, SlImportList *list) {
  SlNativeScanner scanner = {
      .start = content, .cursor = content, .line = 1, .column = 1, .language = language};
  if (language == SL_LANGUAGE_TERRAFORM) return parse_terraform(&scanner, list);
  SlNativeToken previous = {0};
  SlNativeToken token;
  while ((token = next_token(&scanner)).kind != TOKEN_END) {
    const bool member = token_is(previous, ".") || token_is(previous, ":");
    if (token.kind == TOKEN_WORD && !member && !parse_native_token(&scanner, token, list))
      return false;
    previous = token;
  }
  return true;
}
