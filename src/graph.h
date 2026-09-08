#ifndef SRC_LINT_GRAPH_H
#define SRC_LINT_GRAPH_H

#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum { SL_EDGE_ALLOWED, SL_EDGE_ADVISORY, SL_EDGE_VIOLATION, SL_EDGE_ERROR } SlEdgeStatus;

typedef struct {
  char *id;
  char *boundary;
} SlGraphNode;

typedef struct {
  char *from;
  char *to;
  size_t line;
  size_t column;
  SlLanguage language;
  SlEdgeStatus status;
  char *rule;
  char *source_boundary;
  char *target_boundary;
  char *suggested_public_entry;
} SlGraphEdge;

typedef struct {
  const char *from;
  const char *to;
  size_t line;
  size_t column;
  SlLanguage language;
  SlEdgeStatus status;
  const char *rule;
  const char *source_boundary;
  const char *target_boundary;
  const char *suggested_public_entry;
} SlGraphEdgeInput;

typedef struct {
  char *name;
  char *root;
  char *source;
  size_t files;
} SlDiscoveredBoundary;

typedef struct {
  SlGraphNode *nodes;
  size_t node_count;
  size_t node_capacity;
  SlGraphEdge *edges;
  size_t edge_count;
  size_t edge_capacity;
  SlDiscoveredBoundary *boundaries;
  size_t boundary_count;
  size_t boundary_capacity;
} SlGraph;

bool sl_graph_add_node(SlGraph *graph, const char *id, const char *boundary);
bool sl_graph_add_edge(SlGraph *graph, const SlGraphEdgeInput *input);
bool sl_graph_add_boundary(SlGraph *graph, const char *name, const char *root, const char *source);
bool sl_graph_write_json(SlGraph *graph, FILE *output);
bool sl_graph_write_html(SlGraph *graph, FILE *output);
bool sl_graph_write_discovery_json(SlGraph *graph, FILE *output);
bool sl_graph_write_discovery_text(SlGraph *graph, FILE *output);
void sl_graph_free(SlGraph *graph);

#endif
