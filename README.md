# tree-legibility

Tree Legibility is a fast architecture-conformance linter for imports that cross intended code boundaries.

## Current slice

<!-- implemented languages and inferred boundary conventions from src/check.c -->

The POSIX C CLI currently scans TypeScript and JavaScript static imports, dynamic imports, and `require` calls. With no configuration, it infers sibling boundaries under `services/<name>` and permits target paths under `api/`, `public/`, or `proto/`.

The [implementation issue](docs/issues/0001-build-import-boundary-linter.md) defines configuration inheritance, Python, Go, Proto, caching, discovery, and graph output.

## Build

<!-- build commands matching CMakeLists.txt -->

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

```sh
cmake -S . -B build-asan -DTREE_LEGIBILITY_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Usage

<!-- CLI syntax and exit codes from src/main.c and include/tree_legibility/check.h -->

```sh
./build/tree-legibility check .
./build/tree-legibility check . --format json
```

Exit code `0` is clean, `1` reports policy findings, and `2` reports invalid input or an operational error.

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
