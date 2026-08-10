# tree-legibility

Tree Legibility is a fast, standalone architecture-conformance linter for imports that cross intended service, component, package, or Proto boundaries.

## Boundary policy

<!-- implemented languages and inferred boundary conventions from src/check.c and src/imports.c -->

With no rc file, the CLI treats sibling directories under `services/<name>` as boundaries. Cross-service imports may target `api/`, `public/`, or `proto/`; direct traversal into other paths fails with `TL1001`.

| Source | Imports recognized |
| --- | --- |
| TypeScript and JavaScript | Static imports, re-exports, dynamic `import()`, and `require()` |
| Python | `from … import …` and `import …` |
| Go | Single and grouped imports |
| Proto | Regular, `public`, and `weak` imports |

Explicit policy takes precedence over inference. Unresolved local imports are `TL2001` advisories by default and errors under `--strict`.

## Examples

<!-- default public boundary paths from src/check.c and import syntax from src/imports.c -->

Cross-boundary imports should use an entry exposed through `api/`, `public/`, or `proto/`. Direct imports from an owner's internal implementation produce `TL1001`.

### TypeScript and JavaScript

```diff
-import { postEntry } from "../billing/internal/ledger";
+import { postEntry } from "../billing/api";
```

### Python

```diff
-from services.billing.internal import ledger
+from services.billing.api import ledger
```

### Go

```diff
-import "example.com/repo/services/billing/internal/ledger"
+import "example.com/repo/services/billing/api"
```

### Proto

```diff
-import "services/billing/internal/ledger.proto";
+import public "services/billing/proto/public.proto";
```

## Build

<!-- build commands and sanitizer option matching CMakeLists.txt -->

The executable is C11 and has no runtime dependency. POSIX `fts` and `realpath` are currently required.

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

<!-- CLI syntax, formats, and exit codes from src/main.c and include/tree_legibility/check.h -->

```text
tree-legibility check [path] [--strict] [--format text|json]
tree-legibility discover [path] [--format text|json]
tree-legibility graph [path] [--format json|html]
```

```sh
./build/tree-legibility check services/orders --strict --format json
./build/tree-legibility discover . --format json
./build/tree-legibility graph . --format json > dependency-graph.json
./build/tree-legibility graph . --format html > dependency-graph.html
```

`discover` reports configured or inferred boundaries with source-file counts. JSON graph output contains deterministic nodes and edges. Violation edges carry source coordinates, source and target ownership, the rule, and a suggested public entry. HTML output is a self-contained boundary filter and pan-and-zoom dependency canvas.

Exit code `0` means clean, `1` means policy findings exist, and `2` means invalid input, configuration, or operation.

## Configuration

<!-- rc filenames, schema, defaults, and inheritance from src/config.c and src/config.h -->

Place one rc file at the repository root:

- `.tree-legibilityrc.toml`
- `.tree-legibilityrc.json`
- `.tree-legibilityrc.yaml`
- `.tree-legibilityrc.yml`

All formats use the same model. More than one recognized rc file in a directory is invalid.

```toml
version = 1
strict = false

[cache]
max_mib = 8

[boundaries.orders]
root = "services/orders"
allow = ["shared/**"]

[boundaries.billing]
root = "services/billing"
public = ["api/**", "proto/**"]
```

A nested rc file inherits its ancestors within the repository. Boundary maps merge by name. Child scalar and array values replace parent values.

`allow` belongs to the importing boundary and matches repository-relative targets. `public` belongs to the imported boundary and matches paths inside it. Entries are exact paths or prefixes ending in `/**`; other wildcard forms are invalid.

## Cache

<!-- cache location, key inputs, default limit, and eviction from src/cache.c and src/config.h -->

Parsed imports are cached per repository in `.tree-legibility/cache/`. The default hard limit is 8 MiB; `cache.max_mib = 0` disables it.

Keys include tool, parser, and cache versions, effective configuration, file path, and source content. Least-recently-used records are trimmed by stored bytes. The cache is safe to delete and ignored by the supplied [`.gitignore`](.gitignore).

Release tests generate a 10,000-file repository and enforce the 10 ms startup and 50 ms warm one-file budgets.

## Development

<!-- test command and test registration matching CMakeLists.txt and tests/CMakeLists.txt -->

Install the repository's versioned Git hooks for commit and push checks:

```sh
./scripts/install-git-hooks.sh
```

The pre-commit hook checks staged whitespace and C formatting, then runs the Debug test suite. The pre-push hook runs the Release and sanitizer suites. Hook updates take effect from `.githooks` without reinstalling.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CLI-level fixtures cover each adapter, zero-config and configured policy, rc parity and inheritance, strict advisories, cache reuse and limits, discovery, JSON, and HTML.

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
