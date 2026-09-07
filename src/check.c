#include "src_lint/check.h"
#include "cache.h"
#include "config.h"
#include "graph.h"
#include "internal.h"

#include <errno.h>
#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  char root[SL_PATH_CAPACITY];
  SlCommand command;
  SlFormat format;
  bool strict;
  bool graph_failed;
  size_t emitted;
  SlCacheSet caches;
  SlGraph graph;
  FILE *output;
  FILE *errors;
} SlContext;

typedef struct {
  char name[SL_OWNER_CAPACITY];
  const char *inside;
} SlOwner;

typedef struct {
  bool applied;
  bool violation;
  SlOwner source_owner;
  SlOwner target_owner;
  const SlBoundaryConfig *target_boundary;
  size_t boundary_offset;
} SlEdgePolicy;

static bool starts_with(const char *value, const char *prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool has_source_extension(const char *path) {
  const char *extension = strrchr(path, '.');
  if (!extension) return false;
  return strcmp(extension, ".ts") == 0 || strcmp(extension, ".tsx") == 0 ||
         strcmp(extension, ".mts") == 0 || strcmp(extension, ".cts") == 0 ||
         strcmp(extension, ".js") == 0 || strcmp(extension, ".jsx") == 0 ||
         strcmp(extension, ".mjs") == 0 || strcmp(extension, ".cjs") == 0 ||
         strcmp(extension, ".py") == 0 || strcmp(extension, ".go") == 0 ||
         strcmp(extension, ".proto") == 0;
}

static bool ignored_directory(const char *name) {
  return strcmp(name, ".git") == 0 || strcmp(name, "build") == 0 ||
         strcmp(name, "node_modules") == 0 || strcmp(name, ".src-lint") == 0;
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
  fputs("src-lint: cannot ", errors);
  fputs(action, errors);
  fprintf(errors, "%s\n", path);
}

static void report_read_error(FILE *errors, const char *path) {
  report_path_error(errors, "read ", path);
}

static bool set_scan_root(SlContext *context, const char *path) {
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
  if (*output_length + separator + segment_length >= SL_PATH_CAPACITY) return false;
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
  if (total >= SL_PATH_CAPACITY) return false;
  memcpy(joined, source, directory_length);
  if (separator == 1) joined[directory_length] = '/';
  memcpy(joined + directory_length + separator, specifier, specifier_length + 1);
  return true;
}

static bool resolve_import(const char *source, const char *specifier, char *target) {
  char joined[SL_PATH_CAPACITY];
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

static bool read_owner_name(const char *name, SlOwner *owner) {
  const char *slash = strchr(name, '/');
  const size_t length = slash ? (size_t)(slash - name) : strlen(name);
  if (length == 0 || length >= sizeof(owner->name)) return false;
  memcpy(owner->name, name, length);
  owner->name[length] = '\0';
  owner->inside = slash ? slash + 1 : name + length;
  return true;
}

static bool read_owners_at_segment(const char *source, const char *target, const char *segment,
                                   SlOwner *source_owner, SlOwner *target_owner) {
  const size_t prefix_length = (size_t)(segment - source) + strlen("services/");
  if (strncmp(source, target, prefix_length) != 0) return false;
  if (!read_owner_name(source + prefix_length, source_owner)) return false;
  if (!read_owner_name(target + prefix_length, target_owner)) return false;
  return strcmp(source_owner->name, target_owner->name) != 0;
}

static bool read_boundary_owners(const char *source, const char *target, SlOwner *source_owner,
                                 SlOwner *target_owner, size_t *boundary_offset) {
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

static const char *last_services_segment(const char *path) {
  const char *cursor = path;
  const char *selected = NULL;
  const char *segment;
  while ((segment = next_services_segment(path, cursor)) != NULL) {
    SlOwner owner;
    if (read_owner_name(segment + strlen("services/"), &owner) && *owner.inside) {
      selected = segment;
    }
    cursor = segment + strlen("services");
  }
  return selected;
}

static const char *scan_relative_path(const SlContext *context, const char *path) {
  struct stat information;
  if (stat(context->root, &information) != 0 || !S_ISDIR(information.st_mode)) return path;
  const char *root = context->root[0] == '/' ? context->root + 1 : context->root;
  const size_t length = strlen(root);
  if (strncmp(path, root, length) != 0 || path[length] != '/') return path;
  return path + length + 1;
}

static const char *inferred_services_segment(const SlContext *context, const char *path) {
  const char *relative = scan_relative_path(context, path);
  if (relative == path) return last_services_segment(path);
  const char *segment = next_services_segment(relative, relative);
  return segment ? segment : last_services_segment(path);
}

static const char *services_specifier(const char *specifier) {
  if (starts_with(specifier, "services/")) return specifier;
  const char *segment = strstr(specifier, "/services/");
  return segment ? segment + 1 : NULL;
}

typedef enum { SL_PATH_MISSING, SL_PATH_FILE, SL_PATH_DIRECTORY } SlPathKind;

static SlPathKind repository_path_kind(const char *path) {
  char absolute[SL_PATH_CAPACITY];
  const int written = snprintf(absolute, sizeof(absolute), "/%s", path);
  if (written < 0 || (size_t)written >= sizeof(absolute)) return SL_PATH_MISSING;
  struct stat information;
  if (stat(absolute, &information) != 0) return SL_PATH_MISSING;
  return S_ISDIR(information.st_mode) ? SL_PATH_DIRECTORY : SL_PATH_FILE;
}

static bool resolve_suffix(char *path, const char *suffix) {
  char candidate[SL_PATH_CAPACITY];
  const int written = snprintf(candidate, sizeof(candidate), "%s%s", path, suffix);
  if (written < 0 || (size_t)written >= sizeof(candidate)) return false;
  if (repository_path_kind(candidate) != SL_PATH_FILE) return false;
  strcpy(path, candidate);
  return true;
}

static bool resolve_suffixes(char *path, const char *const *suffixes, size_t count) {
  for (size_t index = 0; index < count; index += 1) {
    if (resolve_suffix(path, suffixes[index])) return true;
  }
  return false;
}

static bool replace_javascript_suffix(char *path, const char *from, const char *to) {
  const size_t path_length = strlen(path);
  const size_t from_length = strlen(from);
  const size_t to_length = strlen(to);
  if (path_length < from_length || strcmp(path + path_length - from_length, from) != 0)
    return false;
  if (path_length - from_length + to_length >= SL_PATH_CAPACITY) return false;
  char candidate[SL_PATH_CAPACITY];
  memcpy(candidate, path, path_length - from_length);
  strcpy(candidate + path_length - from_length, to);
  if (repository_path_kind(candidate) != SL_PATH_FILE) return false;
  strcpy(path, candidate);
  return true;
}

static bool resolve_typescript_runtime_path(char *path) {
  if (replace_javascript_suffix(path, ".js", ".ts")) return true;
  if (replace_javascript_suffix(path, ".js", ".tsx")) return true;
  if (replace_javascript_suffix(path, ".mjs", ".mts")) return true;
  return replace_javascript_suffix(path, ".cjs", ".cts");
}

static bool resolve_javascript_path(char *path, SlPathKind kind) {
  static const char *const extensions[] = {".ts", ".tsx", ".mts", ".cts",
                                           ".js", ".jsx", ".mjs", ".cjs"};
  static const char *const indexes[] = {"/index.ts", "/index.tsx", "/index.mts", "/index.cts",
                                        "/index.js", "/index.jsx", "/index.mjs", "/index.cjs"};
  if (kind == SL_PATH_MISSING && resolve_typescript_runtime_path(path)) return true;
  if (kind != SL_PATH_DIRECTORY) {
    return resolve_suffixes(path, extensions, sizeof(extensions) / sizeof(*extensions));
  }
  (void)resolve_suffixes(path, indexes, sizeof(indexes) / sizeof(*indexes));
  return true;
}

static bool resolve_python_path(char *path, SlPathKind kind) {
  if (kind != SL_PATH_DIRECTORY) return resolve_suffix(path, ".py");
  (void)resolve_suffix(path, "/__init__.py");
  return true;
}

static bool resolve_repository_path(char *path, SlLanguage language) {
  const SlPathKind kind = repository_path_kind(path);
  if (kind == SL_PATH_FILE) return true;
  if (language == SL_LANGUAGE_JAVASCRIPT) return resolve_javascript_path(path, kind);
  if (language == SL_LANGUAGE_PYTHON) return resolve_python_path(path, kind);
  return kind == SL_PATH_DIRECTORY;
}

static bool canonicalize_repository_path(char *path) {
  char absolute[SL_PATH_CAPACITY];
  const int written = snprintf(absolute, sizeof(absolute), "/%s", path);
  if (written < 0 || (size_t)written >= sizeof(absolute)) return false;
  char canonical[SL_PATH_CAPACITY];
  if (!realpath(absolute, canonical)) return false;
  const char *relative = canonical[0] == '/' ? canonical + 1 : canonical;
  if (strlen(relative) >= SL_PATH_CAPACITY) return false;
  strcpy(path, relative);
  return true;
}

static bool build_services_target(const char *source, const char *segment, const char *specifier,
                                  char *target) {
  const size_t prefix_length = (size_t)(segment - source);
  const size_t target_length = prefix_length + strlen(specifier);
  if (target_length >= SL_PATH_CAPACITY) return false;
  memcpy(target, source, prefix_length);
  memcpy(target + prefix_length, specifier, strlen(specifier) + 1);
  return true;
}

static bool resolve_services_import(const SlContext *context, const char *source,
                                    const SlImport *import, char *target) {
  const char *specifier = import->specifier;
  const char *services = services_specifier(specifier);
  if (!services) return false;
  const char *segment = inferred_services_segment(context, source);
  char joined[SL_PATH_CAPACITY];
  if (!segment || !build_services_target(source, segment, services, joined)) return false;
  return normalize_path(joined, target);
}

static const char *find_root_match(const char *specifier, const char *root) {
  const size_t length = strlen(root);
  const char *cursor = specifier;
  while ((cursor = strstr(cursor, root)) != NULL) {
    const bool left = cursor == specifier || cursor[-1] == '/';
    const bool right = cursor[length] == '/' || cursor[length] == '\0';
    if (left && right) return cursor;
    cursor += 1;
  }
  return NULL;
}

static const char *specifier_boundary_root(const SlConfig *config, const char *specifier) {
  const char *selected = NULL;
  size_t longest = 0;
  for (size_t index = 0; index < config->boundary_count; index += 1) {
    const char *root = config->boundaries[index].root;
    const char *match = root ? find_root_match(specifier, root) : NULL;
    if (match && strlen(root) > longest) {
      selected = match;
      longest = strlen(root);
    }
  }
  return selected;
}

static bool resolve_configured_import(const SlConfig *config, const char *specifier, char *target) {
  if (!config->present) return false;
  const char *relative = specifier_boundary_root(config, specifier);
  if (!relative) return false;
  char joined[SL_PATH_CAPACITY];
  const int written =
      snprintf(joined, SL_PATH_CAPACITY, "%s/%s", config->repository_root, relative);
  if (written < 0 || written >= SL_PATH_CAPACITY) return false;
  return normalize_path(joined, target);
}

static bool resolve_import_target(const SlContext *context, const SlConfig *config,
                                  const char *source, const SlImport *import, char *target) {
  if (import->specifier[0] == '.') return resolve_import(source, import->specifier, target);
  if (resolve_configured_import(config, import->specifier, target)) return true;
  return resolve_services_import(context, source, import, target);
}

static bool at_or_below(const char *path, const char *entry) {
  if (strcmp(path, entry) == 0) return true;
  const size_t length = strlen(entry);
  return strncmp(path, entry, length) == 0 && path[length] == '/';
}

static bool public_entry(const char *inside) {
  return at_or_below(inside, "api") || at_or_below(inside, "public") ||
         at_or_below(inside, "proto");
}

static bool copy_owner_name(SlOwner *owner, const char *name, const char *inside) {
  const size_t length = strlen(name);
  if (length == 0 || length >= sizeof(owner->name)) return false;
  memcpy(owner->name, name, length + 1);
  owner->inside = inside;
  return true;
}

static bool configured_owner(const SlConfig *config, const char *path, SlOwner *owner,
                             const SlBoundaryConfig **boundary) {
  const char *inside;
  if (!sl_config_boundary_for_path(config, path, boundary, &inside)) return false;
  return copy_owner_name(owner, (*boundary)->name, inside);
}

static const char *config_relative_path(const SlConfig *config, const char *path) {
  const size_t length = strlen(config->repository_root);
  if (length == 0 || strncmp(path, config->repository_root, length) != 0) return path;
  const char *relative = path + length;
  return *relative == '/' ? relative + 1 : relative;
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

static void emit_json_violation(SlContext *context, const char *source, size_t line, size_t column,
                                const SlOwner *source_owner, const SlOwner *target_owner,
                                const char *target) {
  if (context->emitted > 0) fputs(",\n", context->output);
  fputs("    {\n", context->output);
  write_json_field(context->output, "rule", "SL1001");
  write_json_field(context->output, "source", source);
  fprintf(context->output, "      \"line\": %zu,\n", line);
  fprintf(context->output, "      \"column\": %zu,\n", column);
  write_json_field(context->output, "sourceBoundary", source_owner->name);
  write_json_field(context->output, "targetBoundary", target_owner->name);
  fprintf(context->output, "      \"target\": ");
  write_json_string(context->output, target);
  fputs("\n    }", context->output);
}

static void emit_text_violation(const SlContext *context, const char *source, size_t line,
                                size_t column, const SlOwner *source_owner,
                                const SlOwner *target_owner, const char *target) {
  fprintf(context->output, "%s:%zu:%zu", source, line, column);
  fprintf(context->output, " SL1001 %s cannot import %s internals", source_owner->name,
          target_owner->name);
  fprintf(context->output, " -> %s\n", target);
}

static void emit_violation(SlContext *context, const char *source, size_t line, size_t column,
                           const SlOwner *source_owner, const SlOwner *target_owner,
                           const char *target) {
  const bool insight_command = context->command != SL_COMMAND_CHECK;
  if (insight_command) {
    context->emitted += 1;
    return;
  }
  if (context->format == SL_FORMAT_JSON) {
    emit_json_violation(context, source, line, column, source_owner, target_owner, target);
  } else {
    emit_text_violation(context, source, line, column, source_owner, target_owner, target);
  }
  context->emitted += 1;
}

static const char *inferred_relative_path(const SlContext *context, const char *path) {
  const char *segment = inferred_services_segment(context, path);
  return segment ? segment : path;
}

static const char *diagnostic_path(const SlContext *context, const SlConfig *config,
                                   const char *path) {
  if (config->present) return config_relative_path(config, path);
  return inferred_relative_path(context, path);
}

static bool inferred_owner(const SlContext *context, const char *path, SlOwner *owner) {
  const char *segment = inferred_services_segment(context, path);
  if (!segment) return false;
  return read_owner_name(segment + strlen("services/"), owner);
}

static const char *boundary_name(const SlContext *context, const SlConfig *config, const char *path,
                                 SlOwner *owner) {
  const SlBoundaryConfig *boundary;
  if (configured_owner(config, path, owner, &boundary)) return owner->name;
  if (inferred_owner(context, path, owner)) return owner->name;
  return "";
}

static void add_graph_node(SlContext *context, const SlConfig *config, const char *path) {
  if (context->command != SL_COMMAND_GRAPH || context->graph_failed) return;
  SlOwner owner;
  const char *display = diagnostic_path(context, config, path);
  const char *boundary = boundary_name(context, config, path, &owner);
  context->graph_failed = !sl_graph_add_node(&context->graph, display, boundary);
}

static const char *policy_owner(const SlOwner *owner) {
  return owner && owner->name[0] ? owner->name : NULL;
}

static SlGraphEdgeInput graph_edge_input(SlContext *context, const SlConfig *config,
                                         const char *source, const char *target,
                                         const SlImport *import, SlEdgeStatus status,
                                         const char *rule, const SlEdgePolicy *policy,
                                         const char *suggestion) {
  const char *from = diagnostic_path(context, config, source);
  const char *to = diagnostic_path(context, config, target);
  const char *source_boundary = policy ? policy_owner(&policy->source_owner) : NULL;
  const char *target_boundary = policy ? policy_owner(&policy->target_owner) : NULL;
  return (SlGraphEdgeInput){from,   to,   import->line,    import->column,  import->language,
                            status, rule, source_boundary, target_boundary, suggestion};
}

static void add_graph_edge(SlContext *context, const SlConfig *config, const char *source,
                           const char *target, const SlImport *import, SlEdgeStatus status,
                           const char *rule, const SlEdgePolicy *policy, const char *suggestion) {
  if (context->command != SL_COMMAND_GRAPH || context->graph_failed) return;
  add_graph_node(context, config, source);
  add_graph_node(context, config, target);
  if (context->graph_failed) return;
  const SlGraphEdgeInput input =
      graph_edge_input(context, config, source, target, import, status, rule, policy, suggestion);
  context->graph_failed = !sl_graph_add_edge(&context->graph, &input);
}

static bool add_configured_boundary(SlContext *context, const SlConfig *config, const char *path) {
  const SlBoundaryConfig *boundary;
  const char *inside;
  if (!sl_config_boundary_for_path(config, path, &boundary, &inside)) return false;
  context->graph_failed =
      !sl_graph_add_boundary(&context->graph, boundary->name, boundary->root, "configured");
  return true;
}

static bool inferred_boundary(const SlContext *context, const char *path, SlOwner *owner,
                              char *root) {
  if (!inferred_owner(context, path, owner)) return false;
  const int written = snprintf(root, SL_PATH_CAPACITY, "services/%s", owner->name);
  return written >= 0 && written < SL_PATH_CAPACITY;
}

static void add_discovered_boundary(SlContext *context, const SlConfig *config, const char *path) {
  if (context->command != SL_COMMAND_DISCOVER || context->graph_failed) return;
  if (add_configured_boundary(context, config, path)) return;
  SlOwner owner;
  char root[SL_PATH_CAPACITY];
  if (!inferred_boundary(context, path, &owner, root)) return;
  context->graph_failed = !sl_graph_add_boundary(&context->graph, owner.name, root, "inferred");
}

static void emit_json_unresolved(SlContext *context, const char *source, const char *target,
                                 const SlImport *import, bool strict) {
  if (context->emitted > 0) fputs(",\n", context->output);
  fputs("    {\n", context->output);
  write_json_field(context->output, "rule", "SL2001");
  write_json_field(context->output, "source", source);
  fprintf(context->output, "      \"line\": %zu,\n", import->line);
  fprintf(context->output, "      \"column\": %zu,\n", import->column);
  write_json_field(context->output, "severity", strict ? "error" : "advisory");
  fprintf(context->output, "      \"target\": ");
  write_json_string(context->output, target);
  fputs("\n    }", context->output);
}

static void emit_check_unresolved(SlContext *context, const char *source, const char *target,
                                  const SlImport *import, bool strict) {
  if (context->format == SL_FORMAT_JSON) {
    emit_json_unresolved(context, source, target, import, strict);
  } else {
    fprintf(context->output, "%s:%zu:%zu SL2001 unresolved import -> %s\n", source, import->line,
            import->column, target);
  }
}

static int emit_unresolved(SlContext *context, const SlConfig *config, const char *source,
                           const char *target, const SlImport *import) {
  const char *display_source = diagnostic_path(context, config, source);
  const char *display_target = diagnostic_path(context, config, target);
  const bool strict = context->strict || config->strict;
  const SlEdgeStatus status = strict ? SL_EDGE_ERROR : SL_EDGE_ADVISORY;
  add_graph_edge(context, config, source, target, import, status, "SL2001", NULL, NULL);
  const bool insight_command = context->command != SL_COMMAND_CHECK;
  if (insight_command) {
    context->emitted += 1;
    return strict ? 1 : 0;
  }
  emit_check_unresolved(context, display_source, display_target, import, strict);
  context->emitted += 1;
  return strict ? 1 : 0;
}

static void set_unowned(SlOwner *owner) { (void)copy_owner_name(owner, "unowned", ""); }

static bool read_source_owner(SlContext *context, const SlConfig *config, const char *source,
                              SlEdgePolicy *policy, const SlBoundaryConfig **boundary) {
  if (configured_owner(config, source, &policy->source_owner, boundary)) return true;
  if (inferred_owner(context, source, &policy->source_owner)) return false;
  set_unowned(&policy->source_owner);
  return false;
}

static bool classify_configured(SlContext *context, const SlConfig *config, const char *source,
                                const char *target, SlEdgePolicy *policy) {
  const SlBoundaryConfig *source_boundary;
  const bool has_target =
      configured_owner(config, target, &policy->target_owner, &policy->target_boundary);
  if (!has_target) return false;
  const bool has_source = read_source_owner(context, config, source, policy, &source_boundary);
  const bool same_boundary =
      has_source && strcmp(policy->source_owner.name, policy->target_owner.name) == 0;
  const char *relative_target = config_relative_path(config, target);
  const bool public_target =
      sl_config_public_entry(policy->target_boundary, policy->target_owner.inside);
  const bool allowed_target =
      has_source && sl_config_allowed_target(source_boundary, relative_target);
  policy->applied = true;
  policy->violation = !same_boundary && !public_target && !allowed_target;
  return true;
}

static void classify_inferred(const char *source, const char *target, SlEdgePolicy *policy) {
  const bool crosses = read_boundary_owners(source, target, &policy->source_owner,
                                            &policy->target_owner, &policy->boundary_offset);
  policy->applied = crosses;
  policy->violation = crosses && !public_entry(policy->target_owner.inside);
}

static void classify_policy(SlContext *context, const SlConfig *config, const char *source,
                            const char *target, SlEdgePolicy *policy) {
  *policy = (SlEdgePolicy){0};
  if (classify_configured(context, config, source, target, policy)) return;
  classify_inferred(source, target, policy);
}

static size_t public_prefix_length(const char *pattern) {
  size_t length = strcspn(pattern, "*");
  while (length > 0 && pattern[length - 1] == '/') length -= 1;
  return length;
}

static bool configured_suggestion(const SlBoundaryConfig *boundary, char *suggestion) {
  if (!boundary || !boundary->root) return false;
  const char *entry = boundary->public_entries.count ? boundary->public_entries.items[0] : "api";
  const size_t length = public_prefix_length(entry);
  if (length == 0) entry = "api";
  const size_t entry_length = length == 0 ? strlen(entry) : length;
  const int written =
      snprintf(suggestion, SL_PATH_CAPACITY, "%s/%.*s", boundary->root, (int)entry_length, entry);
  return written >= 0 && written < SL_PATH_CAPACITY;
}

static bool inferred_suggestion(const SlEdgePolicy *policy, char *suggestion) {
  const int written =
      snprintf(suggestion, SL_PATH_CAPACITY, "services/%s/api", policy->target_owner.name);
  return written >= 0 && written < SL_PATH_CAPACITY;
}

static const char *public_suggestion(const SlEdgePolicy *policy, char *suggestion) {
  const bool built = policy->target_boundary
                         ? configured_suggestion(policy->target_boundary, suggestion)
                         : inferred_suggestion(policy, suggestion);
  return built ? suggestion : NULL;
}

static int emit_policy_violation(SlContext *context, const SlConfig *config, const char *source,
                                 const char *target, const SlImport *import,
                                 const SlEdgePolicy *policy) {
  char suggestion[SL_PATH_CAPACITY];
  const char *entry = public_suggestion(policy, suggestion);
  add_graph_edge(context, config, source, target, import, SL_EDGE_VIOLATION, "SL1001", policy,
                 entry);
  const char *display_source = diagnostic_path(context, config, source);
  const char *display_target = diagnostic_path(context, config, target);
  emit_violation(context, display_source, import->line, import->column, &policy->source_owner,
                 &policy->target_owner, display_target);
  return 1;
}

static bool classify_canonical_target(SlContext *context, const SlConfig *config,
                                      const char *source, const char *target, bool found,
                                      char *canonical, SlEdgePolicy *policy) {
  strcpy(canonical, target);
  if (!found || !canonicalize_repository_path(canonical)) return false;
  classify_policy(context, config, source, canonical, policy);
  return true;
}

static int evaluate_import(SlContext *context, const SlConfig *config, const char *source,
                           const SlImport *import) {
  char target[SL_PATH_CAPACITY];
  if (!resolve_import_target(context, config, source, import, target)) return 0;
  const bool found = resolve_repository_path(target, import->language);
  SlEdgePolicy lexical_policy;
  classify_policy(context, config, source, target, &lexical_policy);
  char canonical[SL_PATH_CAPACITY];
  SlEdgePolicy canonical_policy = {0};
  const bool resolved = classify_canonical_target(context, config, source, target, found, canonical,
                                                  &canonical_policy);
  if (lexical_policy.violation)
    return emit_policy_violation(context, config, source, target, import, &lexical_policy);
  if (canonical_policy.violation)
    return emit_policy_violation(context, config, source, canonical, import, &canonical_policy);
  if (!resolved) return emit_unresolved(context, config, source, target, import);
  const SlEdgePolicy *metadata = canonical_policy.applied ? &canonical_policy : &lexical_policy;
  if (!metadata->applied) metadata = NULL;
  add_graph_edge(context, config, source, canonical, import, SL_EDGE_ALLOWED, NULL, metadata, NULL);
  return 0;
}

static int evaluate_imports(SlContext *context, const SlConfig *config, const char *source,
                            const SlImportList *imports) {
  int findings = 0;
  for (size_t index = 0; index < imports->count; index += 1) {
    findings += evaluate_import(context, config, source, &imports->items[index]);
  }
  return findings;
}

static bool collect_imports(SlContext *context, const SlConfig *config, const char *source_path,
                            const char *source, char *content, SlImportList *imports) {
  SlCache cache;
  const bool cache_ready = sl_cache_init(&cache, config, source_path, context->root);
  if (cache_ready && !sl_cache_set_add(&context->caches, &cache)) cache.enabled = false;
  if (cache_ready && sl_cache_load(&cache, source_path, content, imports)) return true;
  if (!sl_parse_imports(source, content, imports)) {
    report_path_error(context->errors, "analyze ", source);
    return false;
  }
  const size_t stored_bytes =
      cache_ready ? sl_cache_store(&cache, source_path, content, imports) : 0;
  sl_cache_set_record(&context->caches, &cache, stored_bytes);
  return true;
}

static int scan_content(SlContext *context, const SlConfig *config, const char *source_path,
                        const char *source, char *content) {
  SlImportList imports = {0};
  if (!collect_imports(context, config, source_path, source, content, &imports)) {
    sl_import_list_free(&imports);
    return -1;
  }
  const int findings = evaluate_imports(context, config, source, &imports);
  sl_import_list_free(&imports);
  return findings;
}

static int scan_file(SlContext *context, const char *path) {
  char *content = load_file(path, context->errors);
  if (!content) return -1;
  SlConfig config;
  if (!sl_config_load_for_file(path, &config, context->errors)) {
    free(content);
    return -1;
  }
  const char *source = path[0] == '/' ? path + 1 : path;
  add_graph_node(context, &config, source);
  add_discovered_boundary(context, &config, source);
  const int findings = scan_content(context, &config, path, source, content);
  sl_config_free(&config);
  free(content);
  return findings;
}

static bool entry_error(const FTSENT *entry) {
  return entry->fts_info == FTS_ERR || entry->fts_info == FTS_DNR || entry->fts_info == FTS_NS;
}

static bool config_file_name(const char *name) {
  return strcmp(name, ".src-lintrc.toml") == 0 || strcmp(name, ".src-lintrc.json") == 0 ||
         strcmp(name, ".src-lintrc.yaml") == 0 || strcmp(name, ".src-lintrc.yml") == 0;
}

static bool validate_config_entry(SlContext *context, const char *path) {
  SlConfig config;
  if (!sl_config_load_for_file(path, &config, context->errors)) return false;
  sl_config_free(&config);
  return true;
}

static bool skip_entry(FTS *tree, FTSENT *entry) {
  const bool is_directory = entry->fts_info == FTS_D && entry->fts_level > 0;
  if (!is_directory || !ignored_directory(entry->fts_name)) return false;
  fts_set(tree, entry, FTS_SKIP);
  return true;
}

static int scan_entry(SlContext *context, FTSENT *entry) {
  if (entry_error(entry)) {
    report_path_error(context->errors, "scan ", entry->fts_path);
    return -1;
  }
  if (entry->fts_info != FTS_F) return 0;
  if (config_file_name(entry->fts_name)) {
    return validate_config_entry(context, entry->fts_path) ? 0 : -1;
  }
  if (!has_source_extension(entry->fts_path)) return 0;
  return scan_file(context, entry->fts_path);
}

static int compare_entries(const FTSENT **left, const FTSENT **right) {
  return strcmp((*left)->fts_name, (*right)->fts_name);
}

static int scan_entries(SlContext *context, FTS *tree) {
  int findings = 0;
  FTSENT *entry;
  while (true) {
    errno = 0;
    entry = fts_read(tree);
    if (!entry) break;
    if (skip_entry(tree, entry)) continue;
    const int file_findings = scan_entry(context, entry);
    if (file_findings < 0) {
      findings = -1;
      break;
    }
    findings += file_findings;
  }
  if (findings >= 0 && errno != 0) {
    report_path_error(context->errors, "scan ", context->root);
    return -1;
  }
  return findings;
}

static int close_tree(SlContext *context, FTS *tree, int findings) {
  if (fts_close(tree) != 0) {
    report_path_error(context->errors, "scan ", context->root);
    return -1;
  }
  return findings;
}

static int scan_tree(SlContext *context) {
  char *paths[] = {context->root, NULL};
  FTS *tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, compare_entries);
  if (!tree) {
    report_path_error(context->errors, "scan ", context->root);
    return -1;
  }
  const int findings = scan_entries(context, tree);
  return close_tree(context, tree, findings);
}

static bool writes_check_json(const SlContext *context) {
  return context->command == SL_COMMAND_CHECK && context->format == SL_FORMAT_JSON;
}

static bool write_graph(SlContext *context) {
  if (context->command != SL_COMMAND_GRAPH || context->graph_failed) return !context->graph_failed;
  if (context->format == SL_FORMAT_HTML) {
    return sl_graph_write_html(&context->graph, context->output);
  }
  return sl_graph_write_json(&context->graph, context->output);
}

static bool write_discovery(SlContext *context) {
  if (context->command != SL_COMMAND_DISCOVER || context->graph_failed) {
    return !context->graph_failed;
  }
  if (context->format == SL_FORMAT_JSON) {
    return sl_graph_write_discovery_json(&context->graph, context->output);
  }
  return sl_graph_write_discovery_text(&context->graph, context->output);
}

static bool valid_command(SlCommand command) {
  return command == SL_COMMAND_CHECK || command == SL_COMMAND_DISCOVER ||
         command == SL_COMMAND_GRAPH;
}

static bool valid_format(SlFormat format) {
  return format == SL_FORMAT_TEXT || format == SL_FORMAT_JSON || format == SL_FORMAT_HTML;
}

static bool valid_options(const SlRunOptions *options) {
  if (!options || !options->root || options->root[0] == '\0') return false;
  if (!valid_command(options->command) || !valid_format(options->format)) return false;
  if (options->command == SL_COMMAND_GRAPH)
    return !options->strict && options->format != SL_FORMAT_TEXT;
  if (options->format == SL_FORMAT_HTML) return false;
  return !options->strict || options->command == SL_COMMAND_CHECK;
}

static SlContext make_context(const SlRunOptions *options, FILE *output, FILE *errors) {
  return (SlContext){.command = options->command,
                     .format = options->format,
                     .strict = options->strict,
                     .output = output,
                     .errors = errors};
}

static bool write_scan_output(SlContext *context) {
  if (!write_graph(context) || !write_discovery(context)) return false;
  const bool output_ready = fflush(context->output) == 0 && !ferror(context->output);
  const bool errors_ready = fflush(context->errors) == 0 && !ferror(context->errors);
  return output_ready && errors_ready;
}

static int finish_run(SlContext *context, int findings) {
  const bool cache_ready = sl_cache_set_trim(&context->caches);
  const bool output_ready = write_scan_output(context);
  const bool failed = findings < 0 || context->graph_failed || !cache_ready || !output_ready;
  sl_cache_set_free(&context->caches);
  sl_graph_free(&context->graph);
  if (failed) return 2;
  return findings == 0 ? 0 : 1;
}

int sl_run(const SlRunOptions *options, FILE *output, FILE *errors) {
  if (!valid_options(options) || !output || !errors) return 2;
  SlContext context = make_context(options, output, errors);
  if (!set_scan_root(&context, options->root)) return 2;
  if (writes_check_json(&context)) fputs("{\n  \"findings\": [\n", output);
  const int findings = scan_tree(&context);
  if (writes_check_json(&context)) fputs("\n  ]\n}\n", output);
  return finish_run(&context, findings);
}

int sl_check(const SlCheckOptions *options, FILE *output, FILE *errors) {
  if (!options) return 2;
  const SlRunOptions run = {options->root, SL_COMMAND_CHECK, options->format, options->strict};
  return sl_run(&run, output, errors);
}
