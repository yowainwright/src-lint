#include "graph.h"
#include "test.h"

static void duplicate_nodes_fill_missing_owner(void) {
  SlGraph graph = {0};
  char id[] = "orders/create.ts";
  CHECK(sl_graph_add_node(&graph, id, ""));
  memset(id, 'x', strlen(id));
  CHECK(sl_graph_add_node(&graph, "orders/create.ts", "orders"));
  CHECK(sl_graph_add_node(&graph, "orders/create.ts", ""));
  CHECK(graph.node_count == 1);
  CHECK(strcmp(graph.nodes[0].id, "orders/create.ts") == 0);
  CHECK(strcmp(graph.nodes[0].boundary, "orders") == 0);
  sl_graph_free(&graph);
}

static void edges_own_their_metadata(void) {
  SlGraph graph = {0};
  char target[] = "billing/internal.ts";
  const SlGraphEdgeInput input = {
      "orders/create.ts", target,   3,        9,         SL_LANGUAGE_JAVASCRIPT,
      SL_EDGE_VIOLATION,  "SL1001", "orders", "billing", "billing/api"};
  CHECK(sl_graph_add_edge(&graph, &input));
  memset(target, 'x', strlen(target));
  CHECK(graph.edge_count == 1);
  const SlGraphEdge *edge = &graph.edges[0];
  CHECK(strcmp(edge->from, "orders/create.ts") == 0);
  CHECK(strcmp(edge->to, "billing/internal.ts") == 0);
  CHECK(edge->line == 3 && edge->column == 9 && edge->language == SL_LANGUAGE_JAVASCRIPT);
  CHECK(edge->status == SL_EDGE_VIOLATION && strcmp(edge->rule, "SL1001") == 0);
  CHECK(strcmp(edge->source_boundary, "orders") == 0);
  CHECK(strcmp(edge->target_boundary, "billing") == 0);
  CHECK(strcmp(edge->suggested_public_entry, "billing/api") == 0);
  sl_graph_free(&graph);
}

static void optional_edge_metadata_stays_absent(void) {
  SlGraph graph = {0};
  const SlGraphEdgeInput input = {.from = "a.ts", .to = "b.ts", .status = SL_EDGE_ALLOWED};
  CHECK(sl_graph_add_edge(&graph, &input));
  const SlGraphEdge *edge = &graph.edges[0];
  CHECK(edge->rule == NULL && edge->source_boundary == NULL && edge->target_boundary == NULL);
  CHECK(edge->suggested_public_entry == NULL);
  sl_graph_free(&graph);
}

static void discovery_counts_by_name_and_root(void) {
  SlGraph graph = {0};
  CHECK(sl_graph_add_boundary(&graph, "billing", "services/billing", "configured"));
  CHECK(sl_graph_add_boundary(&graph, "billing", "services/billing", "configured"));
  CHECK(sl_graph_add_boundary(&graph, "billing", "legacy/billing", "inferred"));
  CHECK(graph.boundary_count == 2);
  CHECK(graph.boundaries[0].files == 2 && graph.boundaries[1].files == 1);
  CHECK(strcmp(graph.boundaries[0].source, "configured") == 0);
  CHECK(strcmp(graph.boundaries[1].root, "legacy/billing") == 0);
  sl_graph_free(&graph);
}

static char *graph_json(SlGraph *graph) {
  char *output = NULL;
  size_t size = 0;
  FILE *stream = open_memstream(&output, &size);
  CHECK(stream != NULL);
  CHECK(sl_graph_write_json(graph, stream));
  CHECK(fclose(stream) == 0);
  CHECK(size > 0);
  return output;
}

static void serialization_sorts_and_escapes_nodes(void) {
  SlGraph graph = {0};
  CHECK(sl_graph_add_node(&graph, "z.ts", "last"));
  CHECK(sl_graph_add_node(&graph, "a\"\\\n\t\x01.ts", "first"));
  char *output = graph_json(&graph);
  const char *first = strstr(output, "a\\\"\\\\\\n\\t\\u0001.ts");
  const char *last = strstr(output, "z.ts");
  CHECK(first != NULL && last != NULL);
  CHECK(first < last);
  char *repeated = graph_json(&graph);
  CHECK(strcmp(output, repeated) == 0);
  free(output);
  free(repeated);
  sl_graph_free(&graph);
}

static void growth_preserves_entries(void) {
  SlGraph graph = {0};
  for (size_t index = 0; index < 70; index += 1) {
    char path[32];
    CHECK(snprintf(path, sizeof(path), "file-%zu.ts", index) > 0);
    CHECK(sl_graph_add_node(&graph, path, "orders"));
    const SlGraphEdgeInput input = {.from = path, .to = "api.ts", .line = index + 1};
    CHECK(sl_graph_add_edge(&graph, &input));
  }
  CHECK(graph.node_count == 70 && graph.edge_count == 70);
  CHECK(strcmp(graph.nodes[0].id, "file-0.ts") == 0);
  CHECK(strcmp(graph.nodes[69].id, "file-69.ts") == 0);
  CHECK(strcmp(graph.edges[69].from, "file-69.ts") == 0 && graph.edges[69].line == 70);
  sl_graph_free(&graph);
  CHECK(graph.nodes == NULL && graph.edges == NULL && graph.boundaries == NULL);
  CHECK(graph.node_count == 0 && graph.edge_count == 0 && graph.boundary_count == 0);
  sl_graph_free(&graph);
}

int main(void) {
  duplicate_nodes_fill_missing_owner();
  edges_own_their_metadata();
  optional_edge_metadata_stays_absent();
  discovery_counts_by_name_and_root();
  serialization_sorts_and_escapes_nodes();
  growth_preserves_entries();
  return 0;
}
