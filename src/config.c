#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum { SL_CONFIG_ROOT, SL_CONFIG_CACHE, SL_CONFIG_BOUNDARY } SlConfigSection;

typedef struct {
  SlConfigSection section;
  SlBoundaryConfig *boundary;
} SlTomlState;

typedef struct {
  char **items;
  size_t count;
  size_t capacity;
} SlPathList;

typedef struct {
  char *start;
  char *cursor;
} SlJsonParser;

typedef enum {
  SL_YAML_ROOT,
  SL_YAML_CACHE,
  SL_YAML_BOUNDARIES,
  SL_YAML_BOUNDARY,
  SL_YAML_PATTERNS
} SlYamlSection;

typedef struct {
  SlYamlSection section;
  SlBoundaryConfig *boundary;
  SlPatternList *patterns;
} SlYamlState;

static char *duplicate_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, value, length);
  return copy;
}

static long file_size(FILE *file) {
  if (fseek(file, 0, SEEK_END) != 0) return -1;
  const long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) return -1;
  return size;
}

static char *read_file_bytes(FILE *file, size_t size) {
  char *content = malloc((size_t)size + 1);
  if (!content) return NULL;
  const bool read = fread(content, 1, (size_t)size, file) == (size_t)size;
  if (!read) {
    free(content);
    return NULL;
  }
  content[size] = '\0';
  return content;
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  const long size = file_size(file);
  char *content = size < 0 ? NULL : read_file_bytes(file, (size_t)size);
  const bool closed = fclose(file) == 0;
  if (!closed) {
    free(content);
    return NULL;
  }
  return content;
}

static void config_error(FILE *errors, const char *path, size_t line, const char *message) {
  fprintf(errors, "src-lint: %s:%zu: %s\n", path, line, message);
}

static void free_patterns(SlPatternList *patterns) {
  for (size_t index = 0; index < patterns->count; index += 1) free(patterns->items[index]);
  free(patterns->items);
  *patterns = (SlPatternList){0};
}

void sl_config_init(SlConfig *config) {
  *config = (SlConfig){0};
  config->version = 1;
  config->cache_max_bytes = SL_DEFAULT_CACHE_BYTES;
}

static void free_boundary(SlBoundaryConfig *boundary) {
  free(boundary->name);
  free(boundary->root);
  free_patterns(&boundary->public_entries);
  free_patterns(&boundary->allow);
}

void sl_config_free(SlConfig *config) {
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    free_boundary(&config->boundaries[index]);
  }
  free(config->boundaries);
  sl_config_init(config);
}

static bool grow_boundaries(SlConfig *config) {
  const size_t capacity = config->boundary_capacity == 0 ? 8 : config->boundary_capacity * 2;
  SlBoundaryConfig *items = realloc(config->boundaries, capacity * sizeof(*items));
  if (!items) return false;
  config->boundaries = items;
  config->boundary_capacity = capacity;
  return true;
}

static SlBoundaryConfig *find_boundary(SlConfig *config, const char *name) {
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    if (strcmp(config->boundaries[index].name, name) == 0) return &config->boundaries[index];
  }
  return NULL;
}

static SlBoundaryConfig *add_boundary(SlConfig *config, const char *name) {
  const bool full = config->boundary_count == config->boundary_capacity;
  if (full && !grow_boundaries(config)) return NULL;
  char *copy = duplicate_string(name);
  if (!copy) return NULL;
  SlBoundaryConfig *boundary = &config->boundaries[config->boundary_count++];
  *boundary = (SlBoundaryConfig){0};
  boundary->name = copy;
  return boundary;
}

static SlBoundaryConfig *get_boundary(SlConfig *config, const char *name) {
  SlBoundaryConfig *boundary = find_boundary(config, name);
  return boundary ? boundary : add_boundary(config, name);
}

static char *trim_left(char *value) {
  while (*value == ' ' || *value == '\t' || *value == '\r') value += 1;
  return value;
}

static void trim_right(char *value) {
  size_t length = strlen(value);
  while (length > 0) {
    const char last = value[length - 1];
    if (last != ' ' && last != '\t' && last != '\r') break;
    value[--length] = '\0';
  }
}

static char *trim(char *value) {
  value = trim_left(value);
  trim_right(value);
  return value;
}

static bool escaped_config_character(const char *cursor, char quote) {
  return quote == '"' && *cursor == '\\' && cursor[1] != '\0';
}

static void strip_config_comment(char *line) {
  char quote = '\0';
  for (char *cursor = line; *cursor; cursor += 1) {
    if (escaped_config_character(cursor, quote)) {
      cursor += 1;
      continue;
    }
    if (quote && *cursor == quote) {
      quote = '\0';
      continue;
    }
    if (quote) continue;
    if (*cursor == '"' || *cursor == '\'') {
      quote = *cursor;
      continue;
    }
    if (*cursor != '#') continue;
    *cursor = '\0';
    return;
  }
}

static bool parse_unsigned(char *value, size_t *result) {
  if (*value < '0' || *value > '9') return false;
  errno = 0;
  char *end;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *trim(end) != '\0' || parsed > SIZE_MAX) return false;
  *result = (size_t)parsed;
  return true;
}

static bool parse_bool(const char *value, bool *result) {
  if (strcmp(value, "true") == 0) {
    *result = true;
    return true;
  }
  if (strcmp(value, "false") != 0) return false;
  *result = false;
  return true;
}

static bool decode_toml_escape(char input, char *output) {
  const char *escapes = "btnfr\"\\";
  const char values[] = {'\b', '\t', '\n', '\f', '\r', '"', '\\'};
  const char *match = strchr(escapes, input);
  if (!match) return false;
  *output = values[match - escapes];
  return true;
}

static bool copy_toml_string(const char *cursor, const char *end, char *output) {
  while (cursor < end) {
    if (*cursor == '"') return false;
    if (*cursor != '\\') {
      *output++ = *cursor;
      cursor += 1;
      continue;
    }
    cursor += 1;
    char decoded;
    if (cursor >= end || !decode_toml_escape(*cursor, &decoded)) return false;
    *output++ = decoded;
    cursor += 1;
  }
  *output = '\0';
  return true;
}

static char *parse_string(const char *value) {
  const size_t length = strlen(value);
  if (length < 2 || value[0] != '"' || value[length - 1] != '"') return NULL;
  char *copy = malloc(length - 1);
  if (!copy) return NULL;
  const bool copied = copy_toml_string(value + 1, value + length - 1, copy);
  if (!copied) {
    free(copy);
    return NULL;
  }
  return copy;
}

static bool add_pattern(SlPatternList *patterns, char *pattern) {
  char **items = realloc(patterns->items, (patterns->count + 1) * sizeof(*items));
  if (!items) {
    free(pattern);
    return false;
  }
  patterns->items = items;
  patterns->items[patterns->count++] = pattern;
  return true;
}

static char *toml_string_end(char *value) {
  if (*value != '"') return NULL;
  for (char *cursor = value + 1; *cursor; cursor += 1) {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 1;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
  }
  return NULL;
}

static char *parse_pattern_item(char *cursor, SlPatternList *patterns) {
  char *end = toml_string_end(cursor);
  if (!end) return NULL;
  const char saved = *end;
  *end = '\0';
  char *pattern = parse_string(cursor);
  *end = saved;
  if (!pattern || !add_pattern(patterns, pattern)) return NULL;
  return trim_left(end);
}

static bool parse_pattern_array(char *value, SlPatternList *patterns) {
  value = trim(value);
  const size_t length = strlen(value);
  if (length < 2 || value[0] != '[' || value[length - 1] != ']') return false;
  free_patterns(patterns);
  patterns->set = true;
  value[length - 1] = '\0';
  char *cursor = value + 1;
  while (*(cursor = trim_left(cursor))) {
    cursor = parse_pattern_item(cursor, patterns);
    if (!cursor) return false;
    if (*cursor == '\0') break;
    if (*cursor != ',') return false;
    cursor += 1;
  }
  return true;
}

static bool set_version(SlConfig *config, char *value) {
  size_t version;
  if (!parse_unsigned(value, &version) || version != 1) return false;
  config->version = (unsigned)version;
  config->version_set = true;
  return true;
}

static bool set_strict(SlConfig *config, char *value) {
  if (!parse_bool(value, &config->strict)) return false;
  config->strict_set = true;
  return true;
}

static bool set_root_value(SlConfig *config, const char *key, char *value) {
  if (strcmp(key, "version") == 0) return set_version(config, value);
  if (strcmp(key, "strict") == 0) return set_strict(config, value);
  return false;
}

static bool set_cache_value(SlConfig *config, const char *key, char *value) {
  if (strcmp(key, "max_mib") != 0) return false;
  size_t max_mib;
  if (!parse_unsigned(value, &max_mib) || max_mib > SIZE_MAX / (1024U * 1024U)) return false;
  config->cache_max_bytes = max_mib * 1024U * 1024U;
  config->cache_set = true;
  return true;
}

static bool set_boundary_root(SlBoundaryConfig *boundary, char *value) {
  char *root = parse_string(value);
  if (!root) return false;
  free(boundary->root);
  boundary->root = root;
  boundary->root_set = true;
  return true;
}

static bool set_boundary_value(SlBoundaryConfig *boundary, const char *key, char *value) {
  if (strcmp(key, "root") == 0) return set_boundary_root(boundary, value);
  if (strcmp(key, "public") == 0) return parse_pattern_array(value, &boundary->public_entries);
  if (strcmp(key, "allow") == 0) return parse_pattern_array(value, &boundary->allow);
  return false;
}

static bool parse_boundary_section(char *name, SlConfig *config, SlTomlState *state) {
  const char *prefix = "boundaries.";
  if (strncmp(name, prefix, strlen(prefix)) != 0) return false;
  const char *boundary_name = name + strlen(prefix);
  if (*boundary_name == '\0') return false;
  state->section = SL_CONFIG_BOUNDARY;
  state->boundary = get_boundary(config, boundary_name);
  return state->boundary != NULL;
}

static bool parse_section(char *line, SlConfig *config, SlTomlState *state) {
  const size_t length = strlen(line);
  if (length < 3 || line[0] != '[' || line[length - 1] != ']') return false;
  line[length - 1] = '\0';
  char *name = trim(line + 1);
  if (strcmp(name, "cache") != 0) return parse_boundary_section(name, config, state);
  state->section = SL_CONFIG_CACHE;
  state->boundary = NULL;
  return true;
}

static bool apply_toml_value(SlConfig *config, SlTomlState *state, char *line) {
  char *equals = strchr(line, '=');
  if (!equals) return false;
  *equals = '\0';
  char *key = trim(line);
  char *value = trim(equals + 1);
  if (state->section == SL_CONFIG_ROOT) return set_root_value(config, key, value);
  if (state->section == SL_CONFIG_CACHE) return set_cache_value(config, key, value);
  return state->boundary && set_boundary_value(state->boundary, key, value);
}

static bool parse_toml_line(char *line, SlConfig *config, SlTomlState *state) {
  strip_config_comment(line);
  line = trim(line);
  if (*line == '\0') return true;
  if (*line == '[') return parse_section(line, config, state);
  return apply_toml_value(config, state, line);
}

static bool parse_toml(const char *path, char *content, SlConfig *config, FILE *errors) {
  SlTomlState state = {SL_CONFIG_ROOT, NULL};
  char *line = content;
  size_t line_number = 1;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    const bool parsed = parse_toml_line(line, config, &state);
    if (!parsed) {
      config_error(errors, path, line_number, "invalid TOML configuration");
      return false;
    }
    if (!next) break;
    line = next + 1;
    line_number += 1;
  }
  return true;
}

static void json_skip_space(SlJsonParser *parser) {
  while (*parser->cursor == ' ' || *parser->cursor == '\t' || *parser->cursor == '\r' ||
         *parser->cursor == '\n') {
    parser->cursor += 1;
  }
}

static bool json_take(SlJsonParser *parser, char expected) {
  json_skip_space(parser);
  if (*parser->cursor != expected) return false;
  parser->cursor += 1;
  return true;
}

static int hex_value(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

static bool read_hex_digits(const char *input, size_t count, uint32_t *value) {
  *value = 0;
  for (size_t index = 0; index < count; index += 1) {
    const int digit = hex_value(input[index]);
    if (digit < 0) return false;
    *value = (*value << 4) | (uint32_t)digit;
  }
  return true;
}

static bool read_unicode_escape(const char **cursor, uint32_t *codepoint) {
  uint32_t first;
  if (!read_hex_digits(*cursor, 4, &first)) return false;
  *cursor += 4;
  if (first < 0xd800 || first > 0xdfff) {
    *codepoint = first;
    return true;
  }
  if (first > 0xdbff || (*cursor)[0] != '\\' || (*cursor)[1] != 'u') return false;
  uint32_t second;
  if (!read_hex_digits(*cursor + 2, 4, &second) || second < 0xdc00 || second > 0xdfff) return false;
  *cursor += 6;
  *codepoint = 0x10000 + ((first - 0xd800) << 10) + second - 0xdc00;
  return true;
}

static void write_utf8(char **output, uint32_t codepoint) {
  if (codepoint <= 0x7f) *(*output)++ = (char)codepoint;
  if (codepoint > 0x7f && codepoint <= 0x7ff) {
    *(*output)++ = (char)(0xc0 | codepoint >> 6);
    *(*output)++ = (char)(0x80 | (codepoint & 0x3f));
  }
  if (codepoint > 0x7ff && codepoint <= 0xffff) {
    *(*output)++ = (char)(0xe0 | codepoint >> 12);
    *(*output)++ = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    *(*output)++ = (char)(0x80 | (codepoint & 0x3f));
  }
  if (codepoint <= 0xffff) return;
  *(*output)++ = (char)(0xf0 | codepoint >> 18);
  *(*output)++ = (char)(0x80 | ((codepoint >> 12) & 0x3f));
  *(*output)++ = (char)(0x80 | ((codepoint >> 6) & 0x3f));
  *(*output)++ = (char)(0x80 | (codepoint & 0x3f));
}

static bool decode_json_escape(const char **cursor, char **output) {
  const char *escapes = "\"\\/bfnrt";
  const char values[] = {'"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
  if (**cursor == '\0') return false;
  const char *match = strchr(escapes, **cursor);
  if (match) {
    *(*output)++ = values[match - escapes];
    *cursor += 1;
    return true;
  }
  if (*(*cursor)++ != 'u') return false;
  uint32_t codepoint;
  if (!read_unicode_escape(cursor, &codepoint) || codepoint == 0) return false;
  write_utf8(output, codepoint);
  return true;
}

static char *decode_json_string(SlJsonParser *parser, char *value) {
  const char *cursor = parser->cursor + 1;
  char *output = value;
  while (*cursor && *cursor != '"') {
    const unsigned char character = (unsigned char)*cursor++;
    if (character < 0x20) return NULL;
    const bool escape = character == '\\';
    if (escape && !decode_json_escape(&cursor, &output)) return NULL;
    if (!escape) *output++ = (char)character;
  }
  if (*cursor != '"') return NULL;
  *output = '\0';
  parser->cursor = (char *)cursor + 1;
  return value;
}

static char *json_string(SlJsonParser *parser) {
  json_skip_space(parser);
  if (*parser->cursor != '"') return NULL;
  char *value = malloc(strlen(parser->cursor));
  if (!value) return NULL;
  if (decode_json_string(parser, value)) return value;
  free(value);
  return NULL;
}

static bool json_boolean(SlJsonParser *parser, bool *value) {
  json_skip_space(parser);
  if (strncmp(parser->cursor, "true", 4) == 0) {
    parser->cursor += 4;
    *value = true;
    return true;
  }
  if (strncmp(parser->cursor, "false", 5) != 0) return false;
  parser->cursor += 5;
  *value = false;
  return true;
}

static bool json_unsigned(SlJsonParser *parser, size_t *value) {
  json_skip_space(parser);
  const char first = parser->cursor[0];
  if (first < '0' || first > '9') return false;
  const char second = parser->cursor[1];
  if (first == '0' && second >= '0' && second <= '9') return false;
  errno = 0;
  char *end;
  const unsigned long parsed = strtoul(parser->cursor, &end, 10);
  if (errno != 0 || end == parser->cursor || parsed > SIZE_MAX) return false;
  parser->cursor = end;
  *value = (size_t)parsed;
  return true;
}

static bool json_more(SlJsonParser *parser, bool *done) {
  json_skip_space(parser);
  if (*parser->cursor == '}') {
    parser->cursor += 1;
    *done = true;
    return true;
  }
  if (*parser->cursor != ',') return false;
  parser->cursor += 1;
  *done = false;
  return true;
}

static bool json_array_more(SlJsonParser *parser, bool *done) {
  json_skip_space(parser);
  if (*parser->cursor == ']') {
    parser->cursor += 1;
    *done = true;
    return true;
  }
  if (*parser->cursor != ',') return false;
  parser->cursor += 1;
  *done = false;
  return true;
}

static bool json_pattern_array(SlJsonParser *parser, SlPatternList *patterns) {
  if (!json_take(parser, '[')) return false;
  free_patterns(patterns);
  patterns->set = true;
  json_skip_space(parser);
  if (json_take(parser, ']')) return true;
  while (true) {
    char *pattern = json_string(parser);
    if (!pattern || !add_pattern(patterns, pattern)) return false;
    bool done;
    if (!json_array_more(parser, &done)) return false;
    if (done) return true;
  }
}

static bool replace_boundary_root(SlBoundaryConfig *boundary, char *root) {
  if (!root) return false;
  free(boundary->root);
  boundary->root = root;
  boundary->root_set = true;
  return true;
}

static bool parse_json_boundary_value(SlJsonParser *parser, SlBoundaryConfig *boundary,
                                      const char *key) {
  if (strcmp(key, "root") == 0) return replace_boundary_root(boundary, json_string(parser));
  if (strcmp(key, "public") == 0) return json_pattern_array(parser, &boundary->public_entries);
  if (strcmp(key, "allow") == 0) return json_pattern_array(parser, &boundary->allow);
  return false;
}

static bool parse_json_boundary(SlJsonParser *parser, SlBoundaryConfig *boundary) {
  if (!json_take(parser, '{')) return false;
  json_skip_space(parser);
  if (json_take(parser, '}')) return true;
  while (true) {
    char *key = json_string(parser);
    const bool colon = key && json_take(parser, ':');
    const bool parsed = colon && parse_json_boundary_value(parser, boundary, key);
    free(key);
    if (!parsed) return false;
    bool done;
    if (!json_more(parser, &done)) return false;
    if (done) return true;
  }
}

static bool parse_json_boundaries(SlJsonParser *parser, SlConfig *config) {
  if (!json_take(parser, '{')) return false;
  json_skip_space(parser);
  if (json_take(parser, '}')) return true;
  while (true) {
    char *name = json_string(parser);
    SlBoundaryConfig *boundary = name ? get_boundary(config, name) : NULL;
    const bool colon = boundary && json_take(parser, ':');
    const bool parsed = colon && parse_json_boundary(parser, boundary);
    free(name);
    if (!parsed) return false;
    bool done;
    if (!json_more(parser, &done)) return false;
    if (done) return true;
  }
}

static bool parse_json_cache_value(SlJsonParser *parser, SlConfig *config, const char *key) {
  if (strcmp(key, "max_mib") != 0) return false;
  size_t max_mib;
  if (!json_unsigned(parser, &max_mib) || max_mib > SIZE_MAX / (1024U * 1024U)) return false;
  config->cache_max_bytes = max_mib * 1024U * 1024U;
  config->cache_set = true;
  return true;
}

static bool parse_json_cache(SlJsonParser *parser, SlConfig *config) {
  if (!json_take(parser, '{')) return false;
  json_skip_space(parser);
  if (json_take(parser, '}')) return true;
  while (true) {
    char *key = json_string(parser);
    const bool colon = key && json_take(parser, ':');
    const bool parsed = colon && parse_json_cache_value(parser, config, key);
    free(key);
    if (!parsed) return false;
    bool done;
    if (!json_more(parser, &done)) return false;
    if (done) return true;
  }
}

static bool parse_json_version(SlJsonParser *parser, SlConfig *config) {
  size_t version;
  if (!json_unsigned(parser, &version) || version != 1) return false;
  config->version = (unsigned)version;
  config->version_set = true;
  return true;
}

static bool parse_json_strict(SlJsonParser *parser, SlConfig *config) {
  if (!json_boolean(parser, &config->strict)) return false;
  config->strict_set = true;
  return true;
}

static bool parse_json_root_value(SlJsonParser *parser, SlConfig *config, const char *key) {
  if (strcmp(key, "version") == 0) return parse_json_version(parser, config);
  if (strcmp(key, "strict") == 0) return parse_json_strict(parser, config);
  if (strcmp(key, "cache") == 0) return parse_json_cache(parser, config);
  if (strcmp(key, "boundaries") == 0) return parse_json_boundaries(parser, config);
  return false;
}

static bool parse_json_members(SlJsonParser *parser, SlConfig *config) {
  json_skip_space(parser);
  if (json_take(parser, '}')) return true;
  while (true) {
    char *key = json_string(parser);
    const bool colon = key && json_take(parser, ':');
    const bool parsed = colon && parse_json_root_value(parser, config, key);
    free(key);
    if (!parsed) return false;
    bool done;
    if (!json_more(parser, &done)) return false;
    if (done) return true;
  }
}

static size_t json_line(const SlJsonParser *parser) {
  size_t line = 1;
  for (const char *cursor = parser->start; cursor < parser->cursor; cursor += 1) {
    if (*cursor == '\n') line += 1;
  }
  return line;
}

static bool parse_json(const char *path, char *content, SlConfig *config, FILE *errors) {
  SlJsonParser parser = {content, content};
  const bool object = json_take(&parser, '{') && parse_json_members(&parser, config);
  json_skip_space(&parser);
  const bool complete = object && *parser.cursor == '\0';
  if (!complete) config_error(errors, path, json_line(&parser), "invalid JSON configuration");
  return complete;
}

static size_t yaml_indent(const char *line) {
  size_t indent = 0;
  while (line[indent] == ' ') indent += 1;
  return indent;
}

static bool read_yaml_hex_escape(const char **cursor, char escape, uint32_t *codepoint) {
  size_t count = 0;
  if (escape == 'x') count = 2;
  if (escape == 'u') count = 4;
  if (escape == 'U') count = 8;
  if (!count || !read_hex_digits(*cursor, count, codepoint)) return false;
  *cursor += count;
  const bool surrogate = *codepoint >= 0xd800 && *codepoint <= 0xdfff;
  return *codepoint != 0 && *codepoint <= 0x10ffff && !surrogate;
}

static bool decode_yaml_escape(const char **cursor, char **output) {
  if (**cursor == '\0') return false;
  const char escape = *(*cursor)++;
  const char *escapes = "abtnvfre \t\"/\\N_LP";
  const uint32_t values[] = {'\a', '\b', '\t', '\n', '\v', '\f', '\r',   0x1b,  ' ',
                             '\t', '"',  '/',  '\\', 0x85, 0xa0, 0x2028, 0x2029};
  const char *match = strchr(escapes, escape);
  uint32_t codepoint;
  if (match) {
    codepoint = values[match - escapes];
  } else if (!read_yaml_hex_escape(cursor, escape, &codepoint)) {
    return false;
  }
  write_utf8(output, codepoint);
  return true;
}

static bool copy_yaml_quoted(const char **cursor, char quote, char *output) {
  while (**cursor) {
    const char character = *(*cursor)++;
    const bool doubled_quote = quote == '\'' && character == quote && **cursor == quote;
    if (character == quote && !doubled_quote) {
      *output = '\0';
      return true;
    }
    if (doubled_quote) *cursor += 1;
    const bool escape = quote == '"' && character == '\\';
    if (escape && !decode_yaml_escape(cursor, &output)) return false;
    if (!escape) *output++ = character;
  }
  return false;
}

static char *yaml_quoted_scalar(char **cursor) {
  const size_t length = strlen(*cursor);
  if (length > (SIZE_MAX - 1) / 2) return NULL;
  char *value = malloc(length * 2 + 1);
  if (!value) return NULL;
  const char *input = *cursor + 1;
  if (!copy_yaml_quoted(&input, **cursor, value)) {
    free(value);
    return NULL;
  }
  *cursor = (char *)input;
  return value;
}

static char *yaml_scalar(char *value) {
  value = trim(value);
  if (*value != '"' && *value != '\'') return duplicate_string(value);
  char *decoded = yaml_quoted_scalar(&value);
  if (*trim_left(value) == '\0') return decoded;
  free(decoded);
  return NULL;
}

static char *yaml_pattern_item(char **cursor) {
  if (**cursor == '"' || **cursor == '\'') return yaml_quoted_scalar(cursor);
  char *end = *cursor + strcspn(*cursor, ",]");
  const char saved = *end;
  *end = '\0';
  char *pattern = yaml_scalar(*cursor);
  *end = saved;
  *cursor = end;
  return pattern;
}

static bool yaml_pattern_array(char *value, SlPatternList *patterns) {
  if (*value++ != '[') return false;
  free_patterns(patterns);
  patterns->set = true;
  while (*(value = trim_left(value))) {
    if (*value == ']') return *trim_left(value + 1) == '\0';
    char *pattern = yaml_pattern_item(&value);
    if (!pattern || !add_pattern(patterns, pattern)) return false;
    value = trim_left(value);
    if (*value == ']') return *trim_left(value + 1) == '\0';
    if (*value++ != ',') return false;
  }
  return false;
}

static bool split_yaml(char *line, char **key, char **value) {
  char *colon = strchr(line, ':');
  if (!colon) return false;
  *colon = '\0';
  *key = trim(line);
  *value = trim(colon + 1);
  return **key != '\0';
}

static bool parse_yaml_root(SlConfig *config, SlYamlState *state, char *key, char *value) {
  if (strcmp(key, "version") == 0) return set_version(config, value);
  if (strcmp(key, "strict") == 0) return set_strict(config, value);
  if (strcmp(key, "cache") == 0 && *value == '\0')
    state->section = SL_YAML_CACHE;
  else if (strcmp(key, "boundaries") == 0 && *value == '\0')
    state->section = SL_YAML_BOUNDARIES;
  else
    return false;
  return true;
}

static bool parse_yaml_boundary_name(SlConfig *config, SlYamlState *state, char *key, char *value) {
  if (*value != '\0') return false;
  state->boundary = get_boundary(config, key);
  if (!state->boundary) return false;
  state->section = SL_YAML_BOUNDARY;
  state->patterns = NULL;
  return true;
}

static bool begin_yaml_patterns(SlYamlState *state, SlPatternList *patterns, char *value) {
  if (*value != '\0') return yaml_pattern_array(value, patterns);
  free_patterns(patterns);
  patterns->set = true;
  state->section = SL_YAML_PATTERNS;
  state->patterns = patterns;
  return true;
}

static bool parse_yaml_boundary_value(SlYamlState *state, char *key, char *value) {
  if (!state->boundary) return false;
  if (strcmp(key, "root") == 0) return replace_boundary_root(state->boundary, yaml_scalar(value));
  if (strcmp(key, "public") == 0)
    return begin_yaml_patterns(state, &state->boundary->public_entries, value);
  if (strcmp(key, "allow") == 0) return begin_yaml_patterns(state, &state->boundary->allow, value);
  return false;
}

static bool parse_yaml_pattern(SlYamlState *state, char *line) {
  if (state->section != SL_YAML_PATTERNS || !state->patterns) return false;
  line = trim(line);
  if (line[0] != '-' || (line[1] != ' ' && line[1] != '\t')) return false;
  char *pattern = yaml_scalar(line + 2);
  return pattern && add_pattern(state->patterns, pattern);
}

static bool parse_yaml_mapping(SlConfig *config, SlYamlState *state, size_t indent, char *line) {
  char *key;
  char *value;
  if (!split_yaml(line, &key, &value)) return false;
  if (indent == 0) return parse_yaml_root(config, state, key, value);
  if (indent == 2 && state->section == SL_YAML_CACHE) return set_cache_value(config, key, value);
  if (indent == 2) return parse_yaml_boundary_name(config, state, key, value);
  if (indent == 4) return parse_yaml_boundary_value(state, key, value);
  return false;
}

static bool parse_yaml_line(SlConfig *config, SlYamlState *state, char *line) {
  strip_config_comment(line);
  trim_right(line);
  const size_t indent = yaml_indent(line);
  if (line[indent] == '\0') return true;
  if (indent == 6) return parse_yaml_pattern(state, line + indent);
  return parse_yaml_mapping(config, state, indent, line + indent);
}

static bool parse_yaml(const char *path, char *content, SlConfig *config, FILE *errors) {
  SlYamlState state = {SL_YAML_ROOT, NULL, NULL};
  char *line = content;
  size_t line_number = 1;
  while (*line) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';
    const bool parsed = parse_yaml_line(config, &state, line);
    if (!parsed) {
      config_error(errors, path, line_number, "invalid YAML configuration");
      return false;
    }
    if (!next) break;
    line = next + 1;
    line_number += 1;
  }
  return true;
}

static bool grow_paths(SlPathList *paths) {
  const size_t capacity = paths->capacity == 0 ? 4 : paths->capacity * 2;
  char **items = realloc(paths->items, capacity * sizeof(*items));
  if (!items) return false;
  paths->items = items;
  paths->capacity = capacity;
  return true;
}

static bool path_list_add(SlPathList *paths, const char *path) {
  if (paths->count == paths->capacity && !grow_paths(paths)) return false;
  paths->items[paths->count] = duplicate_string(path);
  if (!paths->items[paths->count]) return false;
  paths->count += 1;
  return true;
}

static void path_list_free(SlPathList *paths) {
  for (size_t index = 0; index < paths->count; index += 1) free(paths->items[index]);
  free(paths->items);
  *paths = (SlPathList){0};
}

static bool regular_file(const char *path) {
  struct stat information;
  return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static int config_in_directory(const char *directory, char *path) {
  const char *names[] = {".src-lintrc.toml", ".src-lintrc.json", ".src-lintrc.yaml",
                         ".src-lintrc.yml"};
  int found = 0;
  path[0] = '\0';
  for (size_t index = 0; index < 4; index += 1) {
    char candidate[SL_PATH_CAPACITY];
    const int written = snprintf(candidate, sizeof(candidate), "%s/%s", directory, names[index]);
    if (written < 0 || (size_t)written >= sizeof(candidate)) return -1;
    if (!regular_file(candidate)) continue;
    found += 1;
    strcpy(path, candidate);
  }
  return found;
}

static bool parent_directory(char *directory) {
  if (strcmp(directory, "/") == 0) return false;
  char *slash = strrchr(directory, '/');
  if (!slash) return false;
  if (slash == directory)
    directory[1] = '\0';
  else
    *slash = '\0';
  return true;
}

static bool file_directory(const char *file_path, char *directory) {
  if (strlen(file_path) >= SL_PATH_CAPACITY) return false;
  strcpy(directory, file_path);
  char *slash = strrchr(directory, '/');
  if (!slash) return false;
  if (slash == directory)
    directory[1] = '\0';
  else
    *slash = '\0';
  return true;
}

static bool git_marker(const char *directory) {
  char path[SL_PATH_CAPACITY];
  const int written = snprintf(path, sizeof(path), "%s/.git", directory);
  if (written < 0 || (size_t)written >= sizeof(path)) return false;
  struct stat information;
  return stat(path, &information) == 0;
}

static bool find_git_root(const char *directory, char *root) {
  if (strlen(directory) >= SL_PATH_CAPACITY) return false;
  strcpy(root, directory);
  do {
    if (git_marker(root)) return true;
  } while (parent_directory(root));
  return false;
}

static bool collect_directory_config(const char *directory, SlPathList *paths, FILE *errors) {
  char config_path[SL_PATH_CAPACITY];
  const int count = config_in_directory(directory, config_path);
  if (count < 0) return false;
  if (count > 1) {
    fprintf(errors, "src-lint: multiple rc files in %s\n", directory);
    return false;
  }
  return count == 0 || path_list_add(paths, config_path);
}

static bool collect_config_paths(const char *file_path, SlPathList *paths, FILE *errors) {
  char directory[SL_PATH_CAPACITY];
  if (!file_directory(file_path, directory)) return false;
  char git_root[SL_PATH_CAPACITY];
  const bool bounded = find_git_root(directory, git_root);
  do {
    if (!collect_directory_config(directory, paths, errors)) return false;
    if (bounded && strcmp(directory, git_root) == 0) return true;
  } while (parent_directory(directory));
  return true;
}

static bool set_repository_root(SlConfig *config, const char *config_path) {
  char directory[SL_PATH_CAPACITY];
  if (!file_directory(config_path, directory)) return false;
  const char *policy_path = directory[0] == '/' ? directory + 1 : directory;
  if (strlen(policy_path) >= sizeof(config->repository_root)) return false;
  strcpy(config->repository_root, policy_path);
  return true;
}

static bool path_ends_with(const char *path, const char *suffix) {
  const size_t path_length = strlen(path);
  const size_t suffix_length = strlen(suffix);
  if (suffix_length > path_length) return false;
  return strcmp(path + path_length - suffix_length, suffix) == 0;
}

static bool parse_config_file(const char *path, SlConfig *config, FILE *errors) {
  char *content = read_file(path);
  if (!content) {
    fprintf(errors, "src-lint: cannot read %s\n", path);
    return false;
  }
  const bool json = path_ends_with(path, ".json");
  const bool yaml = path_ends_with(path, ".yaml") || path_ends_with(path, ".yml");
  bool parsed = json ? parse_json(path, content, config, errors) : false;
  if (yaml) parsed = parse_yaml(path, content, config, errors);
  if (!json && !yaml) parsed = parse_toml(path, content, config, errors);
  free(content);
  return parsed;
}

static bool apply_config_paths(SlPathList *paths, SlConfig *config, FILE *errors) {
  for (size_t index = paths->count; index > 0; index -= 1) {
    if (!parse_config_file(paths->items[index - 1], config, errors)) return false;
  }
  return true;
}

static bool valid_root_segment(const char *segment, size_t length) {
  if (length == 0) return false;
  if (length == 1 && segment[0] == '.') return false;
  return length != 2 || segment[0] != '.' || segment[1] != '.';
}

static bool valid_boundary_root(const char *root) {
  if (!root || root[0] == '/' || root[0] == '\0' || strchr(root, '\\')) return false;
  const char *segment = root;
  while (*segment) {
    const char *slash = strchr(segment, '/');
    const size_t length = slash ? (size_t)(slash - segment) : strlen(segment);
    if (!valid_root_segment(segment, length)) return false;
    if (!slash) return true;
    segment = slash + 1;
  }
  return false;
}

static bool valid_pattern(const char *pattern) {
  const char *star = strchr(pattern, '*');
  if (!star) return *pattern != '\0';
  const bool has_prefix = star >= pattern + 2 && star[-1] == '/';
  return has_prefix && strcmp(star, "**") == 0;
}

static bool valid_patterns(const SlPatternList *patterns) {
  for (size_t index = 0; index < patterns->count; index += 1) {
    if (!valid_pattern(patterns->items[index])) return false;
  }
  return true;
}

static bool duplicate_boundary_root(const SlConfig *config, size_t index) {
  const char *root = config->boundaries[index].root;
  for (size_t other = 0; other < index; other += 1) {
    if (strcmp(config->boundaries[other].root, root) == 0) return true;
  }
  return false;
}

static bool valid_boundaries(const SlConfig *config) {
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    const SlBoundaryConfig *boundary = &config->boundaries[index];
    const bool valid_name = boundary->name[0] && strlen(boundary->name) < SL_OWNER_CAPACITY;
    if (!valid_name || !boundary->root_set) return false;
    if (!valid_boundary_root(boundary->root) || duplicate_boundary_root(config, index))
      return false;
    const bool valid_public = valid_patterns(&boundary->public_entries);
    const bool valid_allow = valid_patterns(&boundary->allow);
    if (!valid_public || !valid_allow) return false;
  }
  return true;
}

static bool validate_config(const SlConfig *config, const char *file_path, FILE *errors) {
  if (valid_boundaries(config)) return true;
  fprintf(errors, "src-lint: %s: invalid merged boundary configuration\n", file_path);
  return false;
}

bool sl_config_load_for_file(const char *file_path, SlConfig *config, FILE *errors) {
  sl_config_init(config);
  SlPathList paths = {0};
  const bool collected = collect_config_paths(file_path, &paths, errors);
  if (!collected) {
    path_list_free(&paths);
    return false;
  }
  const bool has_config = paths.count > 0;
  const bool rooted = !has_config || set_repository_root(config, paths.items[paths.count - 1]);
  const bool applied = rooted && apply_config_paths(&paths, config, errors);
  const bool valid = applied && validate_config(config, file_path, errors);
  config->present = has_config;
  path_list_free(&paths);
  if (!valid) sl_config_free(config);
  return valid;
}

static bool at_or_below(const char *path, const char *root) {
  const size_t length = strlen(root);
  return strcmp(path, root) == 0 || (strncmp(path, root, length) == 0 && path[length] == '/');
}

static bool boundary_root(const SlConfig *config, const SlBoundaryConfig *boundary, char *root) {
  if (!boundary->root_set || !boundary->root) return false;
  const char *separator = config->repository_root[0] == '\0' ? "" : "/";
  const int written = snprintf(root, SL_PATH_CAPACITY, "%s%s%s", config->repository_root, separator,
                               boundary->root);
  return written >= 0 && written < SL_PATH_CAPACITY;
}

bool sl_config_boundary_for_path(const SlConfig *config, const char *path,
                                 const SlBoundaryConfig **boundary, const char **inside) {
  size_t longest = 0;
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    char root[SL_PATH_CAPACITY];
    if (!boundary_root(config, &config->boundaries[index], root)) continue;
    const size_t length = strlen(root);
    if (length <= longest || !at_or_below(path, root)) continue;
    *boundary = &config->boundaries[index];
    *inside = path + length;
    if (**inside == '/') *inside += 1;
    longest = length;
  }
  return longest > 0;
}

static bool pattern_matches(const char *pattern, const char *value) {
  const char *star = strchr(pattern, '*');
  if (!star) return strcmp(pattern, value) == 0;
  const size_t prefix_length = (size_t)(star - pattern) - 1;
  const size_t value_length = strlen(value);
  if (value_length < prefix_length) return false;
  const bool exact = value_length == prefix_length;
  const bool descendant = value_length > prefix_length && value[prefix_length] == '/';
  return strncmp(pattern, value, prefix_length) == 0 && (exact || descendant);
}

static bool pattern_list_matches(const SlPatternList *patterns, const char *value) {
  for (size_t index = 0; index < patterns->count; index += 1) {
    if (pattern_matches(patterns->items[index], value)) return true;
  }
  return false;
}

bool sl_config_public_entry(const SlBoundaryConfig *boundary, const char *inside) {
  return pattern_list_matches(&boundary->public_entries, inside);
}

bool sl_config_allowed_target(const SlBoundaryConfig *boundary, const char *target) {
  return pattern_list_matches(&boundary->allow, target);
}

static void hash_bytes(uint64_t *hash, const void *bytes, size_t length) {
  const unsigned char *cursor = bytes;
  for (size_t index = 0; index < length; index += 1) {
    *hash ^= cursor[index];
    *hash *= UINT64_C(1099511628211);
  }
}

static void hash_string(uint64_t *hash, const char *value) {
  if (value) hash_bytes(hash, value, strlen(value));
  const unsigned char separator = 0xff;
  hash_bytes(hash, &separator, 1);
}

static void hash_patterns(uint64_t *hash, const SlPatternList *patterns) {
  hash_bytes(hash, &patterns->set, sizeof(patterns->set));
  for (size_t index = 0; index < patterns->count; index += 1) {
    hash_string(hash, patterns->items[index]);
  }
}

static void hash_boundary(uint64_t *hash, const SlBoundaryConfig *boundary) {
  hash_string(hash, boundary->name);
  hash_string(hash, boundary->root);
  hash_bytes(hash, &boundary->root_set, sizeof(boundary->root_set));
  hash_patterns(hash, &boundary->public_entries);
  hash_patterns(hash, &boundary->allow);
}

uint64_t sl_config_hash(const SlConfig *config) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash_bytes(&hash, &config->version, sizeof(config->version));
  hash_bytes(&hash, &config->strict, sizeof(config->strict));
  hash_bytes(&hash, &config->cache_max_bytes, sizeof(config->cache_max_bytes));
  hash_string(&hash, config->repository_root);
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    hash_boundary(&hash, &config->boundaries[index]);
  }
  return hash;
}
