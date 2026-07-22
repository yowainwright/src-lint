#include "tree_legibility/check.h"

#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TL_PATH_CAPACITY 4096

typedef struct {
  char root[TL_PATH_CAPACITY];
  TlFormat format;
  size_t emitted;
  FILE *output;
  FILE *errors;
} TlContext;

typedef struct {
  char name[128];
  const char *inside;
} TlOwner;

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

static bool starts_with(const char *value, const char *prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool has_source_extension(const char *path) {
  const char *extension = strrchr(path, '.');
  if (!extension) return false;
  return strcmp(extension, ".ts") == 0 || strcmp(extension, ".tsx") == 0 ||
         strcmp(extension, ".mts") == 0 || strcmp(extension, ".cts") == 0 ||
         strcmp(extension, ".js") == 0 || strcmp(extension, ".jsx") == 0 ||
         strcmp(extension, ".mjs") == 0 || strcmp(extension, ".cjs") == 0;
}

static bool ignored_directory(const char *name) {
  return strcmp(name, ".git") == 0 || strcmp(name, "build") == 0 ||
         strcmp(name, "node_modules") == 0 || strcmp(name, ".tree-legibility") == 0;
}

static long file_size(FILE *file) {
  if (fseek(file, 0, SEEK_END) != 0) return -1;
  const long size = ftell(file);
  if (size < 0) return -1;
  if (fseek(file, 0, SEEK_SET) != 0) return -1;
  return size;
}

static char *read_bytes(FILE *file, size_t size) {
  char *content = malloc(size + 1);
  if (!content) return NULL;
  const size_t read_count = fread(content, 1, size, file);
  if (read_count != size) {
    free(content);
    return NULL;
  }
  content[size] = '\0';
  return content;
}

static void report_path_error(FILE *errors, const char *action, const char *path) {
  fputs("tree-legibility: cannot ", errors);
  fputs(action, errors);
  fprintf(errors, "%s\n", path);
}

static void report_read_error(FILE *errors, const char *path) {
  report_path_error(errors, "read ", path);
}

static bool set_scan_root(TlContext *context, const char *path) {
  if (realpath(path, context->root)) return true;
  report_path_error(context->errors, "scan ", path);
  return false;
}

static char *load_file(const char *path, FILE *errors) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    report_read_error(errors, path);
    return NULL;
  }
  const long size = file_size(file);
  char *content = size < 0 ? NULL : read_bytes(file, (size_t)size);
  fclose(file);
  if (!content) report_read_error(errors, path);
  return content;
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

static bool next_js_token(TlJsScanner *scanner, TlToken *token) {
  while (*scanner->cursor) {
    if (line_comment_start(scanner->cursor)) {
      scanner->cursor = skip_line_comment(scanner->cursor);
      continue;
    }
    if (block_comment_start(scanner->cursor)) {
      scanner->cursor = skip_block_comment(scanner->cursor);
      continue;
    }
    if (quote_character(*scanner->cursor)) {
      scanner->cursor = skip_quoted(scanner->cursor);
      continue;
    }
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

static bool same_segment(const char *segment, size_t length, const char *value) {
  return strlen(value) == length && strncmp(segment, value, length) == 0;
}

static bool pop_segment(char *output, size_t *length) {
  if (*length == 0) return false;
  char *slash = strrchr(output, '/');
  *length = slash ? (size_t)(slash - output) : 0;
  output[*length] = '\0';
  return true;
}

static bool append_segment(char *output, size_t *output_length, const char *segment,
                           size_t segment_length) {
  const size_t separator = *output_length == 0 ? 0 : 1;
  if (*output_length + separator + segment_length >= TL_PATH_CAPACITY) return false;
  if (separator == 1) output[(*output_length)++] = '/';
  memcpy(output + *output_length, segment, segment_length);
  *output_length += segment_length;
  output[*output_length] = '\0';
  return true;
}

static bool apply_segment(char *output, size_t *output_length, const char *segment,
                          size_t segment_length) {
  if (segment_length == 0 || same_segment(segment, segment_length, ".")) return true;
  if (same_segment(segment, segment_length, "..")) return pop_segment(output, output_length);
  return append_segment(output, output_length, segment, segment_length);
}

static bool normalize_path(const char *input, char *output) {
  const char *cursor = input;
  size_t output_length = 0;
  output[0] = '\0';
  while (*cursor) {
    const char *slash = strchr(cursor, '/');
    const size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
    if (!apply_segment(output, &output_length, cursor, length)) return false;
    if (!slash) break;
    cursor = slash + 1;
  }
  return true;
}

static bool join_import(const char *source, const char *specifier, char *joined) {
  const char *slash = strrchr(source, '/');
  const size_t directory_length = slash ? (size_t)(slash - source) : 0;
  const size_t specifier_length = strlen(specifier);
  const size_t separator = directory_length == 0 ? 0 : 1;
  const size_t total = directory_length + separator + specifier_length;
  if (total >= TL_PATH_CAPACITY) return false;
  memcpy(joined, source, directory_length);
  if (separator == 1) joined[directory_length] = '/';
  memcpy(joined + directory_length + separator, specifier, specifier_length + 1);
  return true;
}

static bool resolve_import(const char *source, const char *specifier, char *target) {
  char joined[TL_PATH_CAPACITY];
  if (!join_import(source, specifier, joined)) return false;
  return normalize_path(joined, target);
}

static const char *next_services_segment(const char *path, const char *cursor) {
  while ((cursor = strstr(cursor, "services")) != NULL) {
    const bool left_edge = cursor == path || cursor[-1] == '/';
    if (left_edge && cursor[8] == '/') return cursor;
    cursor += 8;
  }
  return NULL;
}

static bool read_owner_name(const char *name, TlOwner *owner) {
  const char *slash = strchr(name, '/');
  const size_t length = slash ? (size_t)(slash - name) : strlen(name);
  if (length == 0 || length >= sizeof(owner->name)) return false;
  memcpy(owner->name, name, length);
  owner->name[length] = '\0';
  owner->inside = slash ? slash + 1 : name + length;
  return true;
}

static bool read_owners_at_segment(const char *source, const char *target, const char *segment,
                                   TlOwner *source_owner, TlOwner *target_owner) {
  const size_t prefix_length = (size_t)(segment - source) + strlen("services/");
  if (strncmp(source, target, prefix_length) != 0) return false;
  if (!read_owner_name(source + prefix_length, source_owner)) return false;
  if (!read_owner_name(target + prefix_length, target_owner)) return false;
  return strcmp(source_owner->name, target_owner->name) != 0;
}

static bool read_boundary_owners(const char *source, const char *target, TlOwner *source_owner,
                                 TlOwner *target_owner, size_t *boundary_offset) {
  const char *cursor = source;
  const char *segment;
  while ((segment = next_services_segment(source, cursor)) != NULL) {
    if (read_owners_at_segment(source, target, segment, source_owner, target_owner)) {
      *boundary_offset = (size_t)(segment - source);
      return true;
    }
    cursor = segment + strlen("services");
  }
  return false;
}

static bool public_entry(const char *inside) {
  return starts_with(inside, "api/") || starts_with(inside, "public/") ||
         starts_with(inside, "proto/");
}

static const char *json_escape(unsigned char character) {
  if (character == '"') return "\\\"";
  if (character == '\\') return "\\\\";
  if (character == '\b') return "\\b";
  if (character == '\f') return "\\f";
  if (character == '\n') return "\\n";
  if (character == '\r') return "\\r";
  if (character == '\t') return "\\t";
  return NULL;
}

static void write_json_character(FILE *output, unsigned char character) {
  const char *escape = json_escape(character);
  if (escape) {
    fputs(escape, output);
    return;
  }
  if (character < 0x20) {
    fprintf(output, "\\u%04x", character);
    return;
  }
  fputc(character, output);
}

static void write_json_string(FILE *output, const char *value) {
  fputc('"', output);
  for (const unsigned char *cursor = (const unsigned char *)value; *cursor; cursor += 1) {
    write_json_character(output, *cursor);
  }
  fputc('"', output);
}

static void write_json_field(FILE *output, const char *name, const char *value) {
  fprintf(output, "      \"%s\": ", name);
  write_json_string(output, value);
  fputs(",\n", output);
}

static void emit_json_violation(TlContext *context, const char *source, size_t line, size_t column,
                                const TlOwner *source_owner, const TlOwner *target_owner,
                                const char *target) {
  if (context->emitted > 0) fputs(",\n", context->output);
  fputs("    {\n", context->output);
  write_json_field(context->output, "rule", "TL1001");
  write_json_field(context->output, "source", source);
  fprintf(context->output, "      \"line\": %zu,\n", line);
  fprintf(context->output, "      \"column\": %zu,\n", column);
  write_json_field(context->output, "sourceBoundary", source_owner->name);
  write_json_field(context->output, "targetBoundary", target_owner->name);
  fprintf(context->output, "      \"target\": ");
  write_json_string(context->output, target);
  fputs("\n    }", context->output);
}

static void emit_text_violation(const TlContext *context, const char *source, size_t line,
                                size_t column, const TlOwner *source_owner,
                                const TlOwner *target_owner, const char *target) {
  fprintf(context->output, "%s:%zu:%zu", source, line, column);
  fprintf(context->output, " TL1001 %s cannot import %s internals", source_owner->name,
          target_owner->name);
  fprintf(context->output, " -> %s\n", target);
}

static void emit_violation(TlContext *context, const char *source, size_t line, size_t column,
                           const TlOwner *source_owner, const TlOwner *target_owner,
                           const char *target) {
  if (context->format == TL_FORMAT_JSON) {
    emit_json_violation(context, source, line, column, source_owner, target_owner, target);
  } else {
    emit_text_violation(context, source, line, column, source_owner, target_owner, target);
  }
  context->emitted += 1;
}

static int evaluate_import(TlContext *context, const char *source, const char *specifier,
                           size_t line, size_t column) {
  if (specifier[0] != '.') return 0;
  char target[TL_PATH_CAPACITY];
  if (!resolve_import(source, specifier, target)) return 0;
  TlOwner source_owner;
  TlOwner target_owner;
  size_t boundary_offset;
  if (!read_boundary_owners(source, target, &source_owner, &target_owner, &boundary_offset))
    return 0;
  if (public_entry(target_owner.inside)) return 0;
  emit_violation(context, source + boundary_offset, line, column, &source_owner, &target_owner,
                 target + boundary_offset);
  return 1;
}

static int evaluate_edge(TlContext *context, const char *source, size_t line, size_t column,
                         TlImportEdge *edge) {
  const char saved = *edge->end;
  *edge->end = '\0';
  const int result = evaluate_import(context, source, edge->specifier, line, column);
  *edge->end = saved;
  return result;
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

static int scan_content(TlContext *context, const char *source, char *content) {
  TlJsScanner scanner = {content, content, false};
  char *position = content;
  size_t line = 1;
  size_t column = 1;
  int findings = 0;
  TlImportEdge edge;
  while (next_js_import(&scanner, &edge)) {
    advance_position(&position, edge.specifier, &line, &column);
    findings += evaluate_edge(context, source, line, column, &edge);
  }
  return findings;
}

static int scan_file(TlContext *context, const char *path) {
  char *content = load_file(path, context->errors);
  if (!content) return -1;
  const char *source = path[0] == '/' ? path + 1 : path;
  const int findings = scan_content(context, source, content);
  free(content);
  return findings;
}

static bool entry_error(const FTSENT *entry) {
  return entry->fts_info == FTS_ERR || entry->fts_info == FTS_DNR || entry->fts_info == FTS_NS;
}

static bool skip_entry(FTS *tree, FTSENT *entry) {
  const bool is_directory = entry->fts_info == FTS_D && entry->fts_level > 0;
  if (!is_directory || !ignored_directory(entry->fts_name)) return false;
  fts_set(tree, entry, FTS_SKIP);
  return true;
}

static int scan_entry(TlContext *context, FTSENT *entry) {
  if (entry_error(entry)) {
    report_path_error(context->errors, "scan ", entry->fts_path);
    return -1;
  }
  if (entry->fts_info != FTS_F) return 0;
  if (!has_source_extension(entry->fts_path)) return 0;
  return scan_file(context, entry->fts_path);
}

static int compare_entries(const FTSENT **left, const FTSENT **right) {
  return strcmp((*left)->fts_name, (*right)->fts_name);
}

static int scan_entries(TlContext *context, FTS *tree) {
  int findings = 0;
  FTSENT *entry;
  while ((entry = fts_read(tree)) != NULL) {
    if (skip_entry(tree, entry)) continue;
    const int file_findings = scan_entry(context, entry);
    if (file_findings < 0) {
      findings = -1;
      break;
    }
    findings += file_findings;
  }
  return findings;
}

static int close_tree(TlContext *context, FTS *tree, int findings) {
  if (fts_close(tree) != 0) {
    report_path_error(context->errors, "scan ", context->root);
    return -1;
  }
  return findings;
}

static int scan_tree(TlContext *context) {
  char *paths[] = {context->root, NULL};
  FTS *tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, compare_entries);
  if (!tree) {
    report_path_error(context->errors, "scan ", context->root);
    return -1;
  }
  const int findings = scan_entries(context, tree);
  return close_tree(context, tree, findings);
}

int tl_check(const TlCheckOptions *options, FILE *output, FILE *errors) {
  if (!options || !options->root || !output || !errors) return 2;
  TlContext context = {{0}, options->format, 0, output, errors};
  if (!set_scan_root(&context, options->root)) return 2;
  if (context.format == TL_FORMAT_JSON) fputs("{\n  \"findings\": [\n", output);
  const int findings = scan_tree(&context);
  if (context.format == TL_FORMAT_JSON) fputs("\n  ]\n}\n", output);
  if (findings < 0) return 2;
  return findings == 0 ? 0 : 1;
}
