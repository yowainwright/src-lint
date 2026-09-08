#include "graph.h"

static const char HTML_HEAD[] =
    "<!doctype html>\n"
    "<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>src-lint dependency graph</title>\n<style>\n"
    ":root{color-scheme:dark;font:14px/1.4 ui-monospace,SFMono-Regular,Menlo,monospace;"
    "--bg:#0a0d12;--panel:#111722;--line:#384357;--text:#e7edf6;--muted:#8d9aae;"
    "--ok:#50c878;--bad:#ff667a;--warn:#f0ba55}\n"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);overflow:"
    "hidden}\n"
    "header{height:64px;display:flex;align-items:center;gap:16px;padding:0 20px;"
    "border-bottom:1px solid #222b39;background:#0d121a}\n"
    "h1{font:600 16px/1 sans-serif;margin:0}.stats{color:var(--muted);margin-right:auto}\n"
    "select,button{font:inherit;color:var(--text);background:var(--panel);"
    "border:1px solid #2d3849;border-radius:7px;padding:7px 10px}\n"
    "#viewport{height:calc(100vh - 64px);position:relative;overflow:hidden;cursor:grab;"
    "background-image:radial-gradient(#283142 1px,transparent 1px);background-size:24px 24px}\n"
    "#viewport.dragging{cursor:grabbing}#graph-canvas{position:absolute;transform-origin:0 0;"
    "width:1px;height:1px}svg{position:absolute;overflow:visible;pointer-events:none}\n"
    ".edge{fill:none;stroke:var(--line);stroke-width:2}.edge.violation,.edge.error{stroke:var(-"
    "-bad)}"
    ".edge.advisory{stroke:var(--warn);stroke-dasharray:6 5}\n"
    ".node{position:absolute;width:280px;min-height:88px;text-align:left;padding:12px;"
    "border:1px solid #2a3546;border-left:4px solid var(--ok);border-radius:10px;"
    "background:var(--panel);box-shadow:0 8px 28px #0006;cursor:pointer}\n"
    ".node.violation,.node.error{border-left-color:var(--bad)}.node.advisory{border-left-color:"
    "var(--warn)}"
    ".node.selected{outline:2px solid #78a8ff}.boundary{display:block;color:#78a8ff;"
    "font:600 12px/1 sans-serif;margin-bottom:10px}.path{display:block;overflow-wrap:anywhere}"
    ".rule{display:block;color:var(--bad);font-size:12px;margin-top:8px}\n"
    "</style>\n</head>\n<body>\n"
    "<header><h1>src-lint</h1><span class=\"stats\" id=\"stats\"></span>"
    "<label>Boundary <select id=\"boundary-filter\"><option value=\"\">All</option>"
    "</select></label><button id=\"reset\" type=\"button\">Reset view</button></header>\n"
    "<main id=\"viewport\"><div id=\"graph-canvas\"><svg id=\"edges\"></svg>"
    "<div id=\"nodes\"></div></div></main>\n"
    "<script id=\"graph-data\" type=\"application/json\">\n";

static const char SETUP_SCRIPT[] =
    "</script>\n<script>\n"
    "const graph = JSON.parse(document.querySelector('#graph-data').textContent);\n"
    "const viewport = document.querySelector('#viewport');\n"
    "const canvas = document.querySelector('#graph-canvas');\n"
    "const nodesLayer = document.querySelector('#nodes');\n"
    "const edgesLayer = document.querySelector('#edges');\n"
    "const filter = document.querySelector('#boundary-filter');\n"
    "const stats = document.querySelector('#stats');\n"
    "const state = { x: 48, y: 48, scale: 1, drag: false, startX: 0, startY: 0, selected: '' };\n"
    "const boundaryNames = graph.nodes.map(node => node.boundary || 'unowned');\n"
    "const boundaries = [...new Set(boundaryNames)].sort();\n"
    "const boundaryColumns = new Map();\n"
    "boundaries.forEach((name, index) => boundaryColumns.set(name, index));\n"
    "const statusRank = new Map([['allowed', 0], ['advisory', 1], ['violation', 2], ['error', "
    "3]]);\n"
    "const nodeStatus = new Map(graph.nodes.map(node => [node.id, 'allowed']));\n"
    "const nodeRules = new Map(graph.nodes.map(node => [node.id, new Set()]));\n";

static const char STATUS_SCRIPT[] = "const promoteStatus = (id, status) => {\n"
                                    "  const current = nodeStatus.get(id) || 'allowed';\n"
                                    "  const currentRank = statusRank.get(current);\n"
                                    "  const nextRank = statusRank.get(status);\n"
                                    "  const shouldPromote = nextRank > currentRank;\n"
                                    "  if (shouldPromote) nodeStatus.set(id, status);\n"
                                    "};\n"
                                    "const recordRule = (id, rule) => {\n"
                                    "  const hasRule = Boolean(rule);\n"
                                    "  if (hasRule) nodeRules.get(id).add(rule);\n"
                                    "};\n"
                                    "graph.edges.forEach(edge => {\n"
                                    "  promoteStatus(edge.from, edge.status);\n"
                                    "  promoteStatus(edge.to, edge.status);\n"
                                    "  recordRule(edge.from, edge.rule);\n"
                                    "  recordRule(edge.to, edge.rule);\n"
                                    "});\n";

static const char FILTER_SCRIPT[] = "boundaries.forEach(name => {\n"
                                    "  const option = document.createElement('option');\n"
                                    "  option.value = name;\n"
                                    "  option.textContent = name;\n"
                                    "  filter.append(option);\n"
                                    "});\n";

static const char POSITION_SCRIPT[] = "const positionNodes = nodes => {\n"
                                      "  const rows = new Map();\n"
                                      "  const positions = new Map();\n"
                                      "  nodes.forEach(node => {\n"
                                      "    const boundary = node.boundary || 'unowned';\n"
                                      "    const column = boundaryColumns.get(boundary);\n"
                                      "    const row = rows.get(boundary) || 0;\n"
                                      "    const position = { x: column * 360, y: row * 132 };\n"
                                      "    rows.set(boundary, row + 1);\n"
                                      "    positions.set(node.id, position);\n"
                                      "  });\n"
                                      "  return positions;\n"
                                      "};\n";

static const char NODE_SCRIPT[] =
    "const makeLabel = (className, text) => {\n"
    "  const label = document.createElement('span');\n"
    "  label.className = className;\n"
    "  label.textContent = text;\n"
    "  return label;\n"
    "};\n"
    "const appendRules = (card, id) => {\n"
    "  const rules = [...nodeRules.get(id)].join(', ');\n"
    "  const hasRules = rules.length > 0;\n"
    "  if (hasRules) card.append(makeLabel('rule', rules));\n"
    "};\n"
    "const selectNode = (event, id) => {\n"
    "  event.stopPropagation();\n"
    "  const alreadySelected = state.selected === id;\n"
    "  const nextSelection = alreadySelected ? '' : id;\n"
    "  state.selected = nextSelection;\n"
    "  render();\n"
    "};\n"
    "const makeNode = (node, position) => {\n"
    "  const card = document.createElement('button');\n"
    "  const status = nodeStatus.get(node.id);\n"
    "  const owner = makeLabel('boundary', node.boundary || 'unowned');\n"
    "  const path = makeLabel('path', node.id);\n"
    "  card.type = 'button';\n"
    "  card.className = `node ${status}`;\n"
    "  card.style.left = `${position.x}px`;\n"
    "  card.style.top = `${position.y}px`;\n"
    "  card.append(owner, path);\n"
    "  appendRules(card, node.id);\n"
    "  card.addEventListener('click', event => selectNode(event, node.id));\n"
    "  return card;\n"
    "};\n";

static const char EDGE_SCRIPT[] =
    "const makeEdge = (edge, from, to) => {\n"
    "  const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');\n"
    "  const startX = from.x + 280;\n"
    "  const startY = from.y + 44;\n"
    "  const endX = to.x;\n"
    "  const endY = to.y + 44;\n"
    "  const bend = (startX + endX) / 2;\n"
    "  const curve = `M${startX},${startY} C${bend},${startY} ${bend},${endY} ${endX},${endY}`;\n"
    "  const related = !state.selected || edge.from === state.selected || edge.to === "
    "state.selected;\n"
    "  path.setAttribute('d', curve);\n"
    "  path.setAttribute('class', `edge ${edge.status}`);\n"
    "  path.style.opacity = related ? '1' : '0.12';\n"
    "  return path;\n"
    "};\n";

static const char TRANSFORM_SCRIPT[] =
    "const applyTransform = () => {\n"
    "  canvas.style.transform = `translate(${state.x}px,${state.y}px) scale(${state.scale})`;\n"
    "};\n";

static const char RENDER_SCRIPT[] =
    "const nodeIsVisible = (node, wanted) => {\n"
    "  const boundary = node.boundary || 'unowned';\n"
    "  return !wanted || boundary === wanted;\n"
    "};\n"
    "const makeCard = (node, positions) => {\n"
    "  const card = makeNode(node, positions.get(node.id));\n"
    "  const isSelected = state.selected === node.id;\n"
    "  if (isSelected) card.classList.add('selected');\n"
    "  return card;\n"
    "};\n"
    "const edgeIsVisible = (edge, ids) => {\n"
    "  return ids.has(edge.from) && ids.has(edge.to);\n"
    "};\n"
    "const render = () => {\n"
    "  const wanted = filter.value;\n"
    "  const visible = graph.nodes.filter(node => nodeIsVisible(node, wanted));\n"
    "  const ids = new Set(visible.map(node => node.id));\n"
    "  const positions = positionNodes(visible);\n"
    "  const cards = visible.map(node => makeCard(node, positions));\n"
    "  const visibleEdges = graph.edges.filter(edge => edgeIsVisible(edge, ids));\n"
    "  const paths = visibleEdges.map(edge => makeEdge(edge, positions.get(edge.from), "
    "positions.get(edge.to)));\n"
    "  nodesLayer.replaceChildren(...cards);\n"
    "  edgesLayer.replaceChildren(...paths);\n"
    "  stats.textContent = `${visible.length} nodes · ${visibleEdges.length} edges`;\n"
    "  applyTransform();\n"
    "};\n";

static const char RESET_SCRIPT[] = "const reset = () => {\n"
                                   "  state.x = 48;\n"
                                   "  state.y = 48;\n"
                                   "  state.scale = 1;\n"
                                   "  applyTransform();\n"
                                   "};\n";

static const char ZOOM_SCRIPT[] =
    "viewport.addEventListener('wheel', event => {\n"
    "  event.preventDefault();\n"
    "  const factor = event.deltaY < 0 ? 1.1 : 0.9;\n"
    "  state.scale = Math.min(2.5, Math.max(0.25, state.scale * factor));\n"
    "  applyTransform();\n"
    "}, { passive: false });\n";

static const char DRAG_SCRIPT[] =
    "viewport.addEventListener('pointerdown', event => {\n"
    "  const hasNodeTarget = Boolean(event.target.closest('.node'));\n"
    "  if (hasNodeTarget) return;\n"
    "  state.drag = true;\n"
    "  state.startX = event.clientX - state.x;\n"
    "  state.startY = event.clientY - state.y;\n"
    "  viewport.classList.add('dragging');\n"
    "});\n"
    "window.addEventListener('pointermove', event => {\n"
    "  const isIdle = !state.drag;\n"
    "  if (isIdle) return;\n"
    "  state.x = event.clientX - state.startX;\n"
    "  state.y = event.clientY - state.startY;\n"
    "  applyTransform();\n"
    "});\n"
    "window.addEventListener('pointerup', () => {\n"
    "  state.drag = false;\n"
    "  viewport.classList.remove('dragging');\n"
    "});\n";

static const char BOOT_SCRIPT[] =
    "filter.addEventListener('change', render);\n"
    "document.querySelector('#reset').addEventListener('click', reset);\n"
    "render();\n</script>\n</body>\n</html>\n";

bool sl_graph_write_html(SlGraph *graph, FILE *output) {
  fputs(HTML_HEAD, output);
  if (!sl_graph_write_json(graph, output)) return false;
  fputs(SETUP_SCRIPT, output);
  fputs(STATUS_SCRIPT, output);
  fputs(FILTER_SCRIPT, output);
  fputs(POSITION_SCRIPT, output);
  fputs(NODE_SCRIPT, output);
  fputs(EDGE_SCRIPT, output);
  fputs(TRANSFORM_SCRIPT, output);
  fputs(RENDER_SCRIPT, output);
  fputs(RESET_SCRIPT, output);
  fputs(ZOOM_SCRIPT, output);
  fputs(DRAG_SCRIPT, output);
  fputs(BOOT_SCRIPT, output);
  return !ferror(output);
}
