#ifndef TREE_LEGIBILITY_GRAPH_H
#define TREE_LEGIBILITY_GRAPH_H

#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum { TL_EDGE_ALLOWED, TL_EDGE_ADVISORY, TL_EDGE_VIOLATION, TL_EDGE_ERROR } TlEdgeStatus;

typedef struct {
  char *id;
  char *boundary;
} TlGraphNode;

typedef struct {
  char *from;
  char *to;
  size_t line;
  size_t column;
  TlLanguage language;
  TlEdgeStatus status;
  char *rule;
  char *source_boundary;
  char *target_boundary;
  char *suggested_public_entry;
} TlGraphEdge;

typedef struct {
  const char *from;
  const char *to;
  size_t line;
  size_t column;
  TlLanguage language;
  TlEdgeStatus status;
  const char *rule;
  const char *source_boundary;
  const char *target_boundary;
  const char *suggested_public_entry;
} TlGraphEdgeInput;

typedef struct {
  char *name;
  char *root;
  char *source;
  size_t files;
} TlDiscoveredBoundary;

typedef struct {
  TlGraphNode *nodes;
  size_t node_count;
  size_t node_capacity;
  TlGraphEdge *edges;
  size_t edge_count;
  size_t edge_capacity;
  TlDiscoveredBoundary *boundaries;
  size_t boundary_count;
  size_t boundary_capacity;
} TlGraph;

bool tl_graph_add_node(TlGraph *graph, const char *id, const char *boundary);
bool tl_graph_add_edge(TlGraph *graph, const TlGraphEdgeInput *input);
bool tl_graph_add_boundary(TlGraph *graph, const char *name, const char *root, const char *source);
bool tl_graph_write_json(TlGraph *graph, FILE *output);
bool tl_graph_write_html(TlGraph *graph, FILE *output);
bool tl_graph_write_discovery_json(TlGraph *graph, FILE *output);
bool tl_graph_write_discovery_text(TlGraph *graph, FILE *output);
void tl_graph_free(TlGraph *graph);

#endif
