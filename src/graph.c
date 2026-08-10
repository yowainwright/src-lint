#include "graph.h"

#include <stdlib.h>
#include <string.h>

static char *duplicate_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, value, length);
  return copy;
}

static char *duplicate_optional(const char *value) {
  return value ? duplicate_string(value) : NULL;
}

static TlGraphNode *find_node(TlGraph *graph, const char *id) {
  for (size_t index = 0; index < graph->node_count; index += 1) {
    if (strcmp(graph->nodes[index].id, id) == 0) return &graph->nodes[index];
  }
  return NULL;
}

static bool grow_nodes(TlGraph *graph) {
  const size_t capacity = graph->node_capacity == 0 ? 16 : graph->node_capacity * 2;
  TlGraphNode *nodes = realloc(graph->nodes, capacity * sizeof(*nodes));
  if (!nodes) return false;
  graph->nodes = nodes;
  graph->node_capacity = capacity;
  return true;
}

static bool set_boundary(TlGraphNode *node, const char *boundary) {
  if (node->boundary[0] != '\0' || boundary[0] == '\0') return true;
  char *copy = duplicate_string(boundary);
  if (!copy) return false;
  free(node->boundary);
  node->boundary = copy;
  return true;
}

bool tl_graph_add_node(TlGraph *graph, const char *id, const char *boundary) {
  TlGraphNode *existing = find_node(graph, id);
  if (existing) return set_boundary(existing, boundary);
  if (graph->node_count == graph->node_capacity && !grow_nodes(graph)) return false;
  char *id_copy = duplicate_string(id);
  char *boundary_copy = duplicate_string(boundary);
  if (!id_copy || !boundary_copy) {
    free(id_copy);
    free(boundary_copy);
    return false;
  }
  graph->nodes[graph->node_count++] = (TlGraphNode){id_copy, boundary_copy};
  return true;
}

static bool grow_edges(TlGraph *graph) {
  const size_t capacity = graph->edge_capacity == 0 ? 32 : graph->edge_capacity * 2;
  TlGraphEdge *edges = realloc(graph->edges, capacity * sizeof(*edges));
  if (!edges) return false;
  graph->edges = edges;
  graph->edge_capacity = capacity;
  return true;
}

static void free_edge(TlGraphEdge *edge) {
  free(edge->from);
  free(edge->to);
  free(edge->rule);
  free(edge->source_boundary);
  free(edge->target_boundary);
  free(edge->suggested_public_entry);
}

static bool edge_copy_valid(const TlGraphEdgeInput *input, const TlGraphEdge *edge) {
  if (!edge->from || !edge->to) return false;
  if (input->rule && !edge->rule) return false;
  if (input->source_boundary && !edge->source_boundary) return false;
  if (input->target_boundary && !edge->target_boundary) return false;
  return !input->suggested_public_entry || edge->suggested_public_entry;
}

static TlGraphEdge copy_edge(const TlGraphEdgeInput *input) {
  return (TlGraphEdge){duplicate_string(input->from),
                       duplicate_string(input->to),
                       input->line,
                       input->column,
                       input->language,
                       input->status,
                       duplicate_optional(input->rule),
                       duplicate_optional(input->source_boundary),
                       duplicate_optional(input->target_boundary),
                       duplicate_optional(input->suggested_public_entry)};
}

bool tl_graph_add_edge(TlGraph *graph, const TlGraphEdgeInput *input) {
  if (graph->edge_count == graph->edge_capacity && !grow_edges(graph)) return false;
  TlGraphEdge edge = copy_edge(input);
  if (!edge_copy_valid(input, &edge)) {
    free_edge(&edge);
    return false;
  }
  graph->edges[graph->edge_count++] = edge;
  return true;
}

static TlDiscoveredBoundary *find_boundary(TlGraph *graph, const char *name, const char *root) {
  for (size_t index = 0; index < graph->boundary_count; index += 1) {
    TlDiscoveredBoundary *boundary = &graph->boundaries[index];
    if (strcmp(boundary->name, name) == 0 && strcmp(boundary->root, root) == 0) return boundary;
  }
  return NULL;
}

static bool grow_boundaries(TlGraph *graph) {
  const size_t capacity = graph->boundary_capacity == 0 ? 8 : graph->boundary_capacity * 2;
  TlDiscoveredBoundary *items = realloc(graph->boundaries, capacity * sizeof(*items));
  if (!items) return false;
  graph->boundaries = items;
  graph->boundary_capacity = capacity;
  return true;
}

static bool append_boundary(TlGraph *graph, const char *name, const char *root,
                            const char *source) {
  char *name_copy = duplicate_string(name);
  char *root_copy = duplicate_string(root);
  char *source_copy = duplicate_string(source);
  if (!name_copy || !root_copy || !source_copy) {
    free(name_copy);
    free(root_copy);
    free(source_copy);
    return false;
  }
  TlDiscoveredBoundary item = {name_copy, root_copy, source_copy, 1};
  graph->boundaries[graph->boundary_count++] = item;
  return true;
}

bool tl_graph_add_boundary(TlGraph *graph, const char *name, const char *root, const char *source) {
  TlDiscoveredBoundary *existing = find_boundary(graph, name, root);
  if (existing) {
    existing->files += 1;
    return true;
  }
  if (graph->boundary_count == graph->boundary_capacity && !grow_boundaries(graph)) return false;
  return append_boundary(graph, name, root, source);
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
  const bool html_sensitive = character == '<' || character == '>' || character == '&';
  if (html_sensitive) {
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

static int compare_nodes(const void *left, const void *right) {
  const TlGraphNode *left_node = left;
  const TlGraphNode *right_node = right;
  return strcmp(left_node->id, right_node->id);
}

static int compare_edges(const void *left, const void *right) {
  const TlGraphEdge *left_edge = left;
  const TlGraphEdge *right_edge = right;
  const int from = strcmp(left_edge->from, right_edge->from);
  if (from != 0) return from;
  if (left_edge->line != right_edge->line) return left_edge->line < right_edge->line ? -1 : 1;
  if (left_edge->column != right_edge->column)
    return left_edge->column < right_edge->column ? -1 : 1;
  return strcmp(left_edge->to, right_edge->to);
}

static int compare_boundaries(const void *left, const void *right) {
  const TlDiscoveredBoundary *left_boundary = left;
  const TlDiscoveredBoundary *right_boundary = right;
  const int name = strcmp(left_boundary->name, right_boundary->name);
  if (name != 0) return name;
  return strcmp(left_boundary->root, right_boundary->root);
}

static const char *language_name(TlLanguage language) {
  if (language == TL_LANGUAGE_PYTHON) return "python";
  if (language == TL_LANGUAGE_GO) return "go";
  if (language == TL_LANGUAGE_PROTO) return "proto";
  return "javascript";
}

static const char *status_name(TlEdgeStatus status) {
  if (status == TL_EDGE_ADVISORY) return "advisory";
  if (status == TL_EDGE_VIOLATION) return "violation";
  if (status == TL_EDGE_ERROR) return "error";
  return "allowed";
}

static void write_node(FILE *output, const TlGraphNode *node, bool comma) {
  fputs("    {\n      \"id\": ", output);
  write_json_string(output, node->id);
  fputs(",\n      \"boundary\": ", output);
  write_json_string(output, node->boundary);
  fputs(comma ? "\n    },\n" : "\n    }\n", output);
}

static void write_nullable_string(FILE *output, const char *value) {
  if (value) {
    write_json_string(output, value);
    return;
  }
  fputs("null", output);
}

static void write_edge_field(FILE *output, const char *name, const char *value, bool comma) {
  fprintf(output, "      \"%s\": ", name);
  write_nullable_string(output, value);
  fputs(comma ? ",\n" : "\n", output);
}

static void write_edge(FILE *output, const TlGraphEdge *edge, bool comma) {
  fputs("    {\n      \"from\": ", output);
  write_json_string(output, edge->from);
  fputs(",\n      \"to\": ", output);
  write_json_string(output, edge->to);
  fprintf(output, ",\n      \"line\": %zu,\n      \"column\": %zu,\n", edge->line, edge->column);
  fputs("      \"language\": ", output);
  write_json_string(output, language_name(edge->language));
  fputs(",\n      \"status\": ", output);
  write_json_string(output, status_name(edge->status));
  fputs(",\n", output);
  write_edge_field(output, "rule", edge->rule, true);
  write_edge_field(output, "sourceBoundary", edge->source_boundary, true);
  write_edge_field(output, "targetBoundary", edge->target_boundary, true);
  write_edge_field(output, "owner", edge->target_boundary, true);
  write_edge_field(output, "suggestedPublicEntry", edge->suggested_public_entry, false);
  fputs(comma ? "    },\n" : "    }\n", output);
}

bool tl_graph_write_json(TlGraph *graph, FILE *output) {
  if (graph->node_count > 1)
    qsort(graph->nodes, graph->node_count, sizeof(*graph->nodes), compare_nodes);
  if (graph->edge_count > 1)
    qsort(graph->edges, graph->edge_count, sizeof(*graph->edges), compare_edges);
  fputs("{\n  \"nodes\": [\n", output);
  for (size_t index = 0; index < graph->node_count; index += 1) {
    write_node(output, &graph->nodes[index], index + 1 < graph->node_count);
  }
  fputs("  ],\n  \"edges\": [\n", output);
  for (size_t index = 0; index < graph->edge_count; index += 1) {
    write_edge(output, &graph->edges[index], index + 1 < graph->edge_count);
  }
  fputs("  ]\n}\n", output);
  return !ferror(output);
}

static void write_boundary(FILE *output, const TlDiscoveredBoundary *boundary, bool comma) {
  fputs("    {\n      \"name\": ", output);
  write_json_string(output, boundary->name);
  fputs(",\n      \"root\": ", output);
  write_json_string(output, boundary->root);
  fputs(",\n      \"source\": ", output);
  write_json_string(output, boundary->source);
  fprintf(output, ",\n      \"files\": %zu\n", boundary->files);
  fputs(comma ? "    },\n" : "    }\n", output);
}

static void sort_boundaries(TlGraph *graph) {
  if (graph->boundary_count > 1)
    qsort(graph->boundaries, graph->boundary_count, sizeof(*graph->boundaries), compare_boundaries);
}

bool tl_graph_write_discovery_json(TlGraph *graph, FILE *output) {
  sort_boundaries(graph);
  fputs("{\n  \"boundaries\": [\n", output);
  for (size_t index = 0; index < graph->boundary_count; index += 1) {
    write_boundary(output, &graph->boundaries[index], index + 1 < graph->boundary_count);
  }
  fputs("  ]\n}\n", output);
  return !ferror(output);
}

bool tl_graph_write_discovery_text(TlGraph *graph, FILE *output) {
  sort_boundaries(graph);
  for (size_t index = 0; index < graph->boundary_count; index += 1) {
    const TlDiscoveredBoundary *boundary = &graph->boundaries[index];
    const char *unit = boundary->files == 1 ? "file" : "files";
    fprintf(output, "%s %s %s %zu %s\n", boundary->name, boundary->root, boundary->source,
            boundary->files, unit);
  }
  return !ferror(output);
}

static void free_nodes(TlGraph *graph) {
  for (size_t index = 0; index < graph->node_count; index += 1) {
    free(graph->nodes[index].id);
    free(graph->nodes[index].boundary);
  }
}

static void free_edges(TlGraph *graph) {
  for (size_t index = 0; index < graph->edge_count; index += 1) {
    free_edge(&graph->edges[index]);
  }
}

static void free_boundaries(TlGraph *graph) {
  for (size_t index = 0; index < graph->boundary_count; index += 1) {
    free(graph->boundaries[index].name);
    free(graph->boundaries[index].root);
    free(graph->boundaries[index].source);
  }
}

void tl_graph_free(TlGraph *graph) {
  free_nodes(graph);
  free_edges(graph);
  free_boundaries(graph);
  free(graph->nodes);
  free(graph->edges);
  free(graph->boundaries);
  *graph = (TlGraph){0};
}
