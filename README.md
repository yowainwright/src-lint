# tree-legibility

## Competitive landscape

Tree Legibility turns intended dependency boundaries into executable policy. It protects service, component, package, and Proto ownership from accidental changes by humans and AI.

Its focus is a fast, local, polyglot intent graph with advisory discovery, deterministic enforcement, and progressive visualization.

_Landscape reviewed July 2026._

| Reference | Existing strength | Tree Legibility's intended distinction |
| --- | --- | --- |
| [archlint](https://github.com/muhammetsafak/archlint) | Import boundaries for Go, TypeScript, and Python; bounded contexts; public ports; executable ADRs | AST-backed analysis, Proto support, nested provider-owned boundaries, advisory discovery, and an interactive graph |
| [structurelint](https://github.com/Jonathangadeaharder/structurelint) | Polyglot import graphs, architectural layers, cascading configuration, and automatic project detection | A focused architecture-conformance tool with monorepo semantics, explicit public entries, and boundary ownership |
| [Dependency Cruiser](https://github.com/sverweij/dependency-cruiser) | JavaScript and TypeScript rules, cycle checks, caching, and graph exports | One policy and graph spanning TypeScript, JavaScript, Python, Go, and Proto |
| [Nx module boundaries](https://nx.dev/docs/features/enforce-module-boundaries) | Monorepo tags, public APIs, cycle detection, and project graphs | Build-system-independent enforcement for nested boundaries within and across projects |
