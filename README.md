# src-lint

src-lint is a fast, standalone architecture-conformance linter for imports that cross intended service, component, package, or Proto boundaries.

## Boundary policy

With no rc file, the CLI treats sibling directories under `services/<name>` as boundaries. Cross-service imports may target `api/`, `public/`, or `proto/`; direct traversal into other paths fails with `SL1001`.

| Source | Imports recognized |
| --- | --- |
| TypeScript and JavaScript | Static imports, re-exports, dynamic `import()`, and `require()` |
| Python | `from … import …` and `import …` |
| Go | Single and grouped imports |
| Proto | Regular, `public`, and `weak` imports |

Explicit policy takes precedence over inference. Unresolved local imports are `SL2001` advisories by default and errors under `--strict`.

JavaScript and TypeScript directory imports resolve through supported `index` files. An existing directory alone does not count as a resolved module.

## Examples

Cross-boundary imports should use an entry exposed through `api/`, `public/`, or `proto/`. Direct imports from an owner's internal implementation produce `SL1001`.

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

The executable is C11 and has no runtime dependency. POSIX `fts` and `realpath` are currently required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

```sh
cmake -S . -B build-asan -DSRC_LINT_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Usage

```text
src-lint --version
src-lint check [path] [--strict] [--format text|json]
src-lint discover [path] [--format text|json]
src-lint graph [path] [--format json|html]
```

```sh
./build/src-lint check services/orders --strict --format json
./build/src-lint discover . --format json
./build/src-lint graph . --format json > dependency-graph.json
./build/src-lint graph . --format html > dependency-graph.html
```

`discover` reports configured or inferred boundaries with source-file counts. JSON graph output contains deterministic nodes and edges. Violation edges carry source coordinates, source and target ownership, the rule, and a suggested public entry. HTML output is a self-contained boundary filter and pan-and-zoom dependency canvas.

Exit code `0` means clean, `1` means policy findings exist, and `2` means invalid input, configuration, or operation.

## Configuration

Place one rc file at the repository root:

- `.src-lintrc.toml`
- `.src-lintrc.json`
- `.src-lintrc.yaml`
- `.src-lintrc.yml`

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

Public-entry policy also includes nested rc files along the imported path, applied after importer-side overrides. Shared ancestor files are applied once. The importing boundary's `allow` rules remain explicit exceptions; target-side rc files cannot grant those exceptions.

## Cache

Parsed imports are cached per repository in `.src-lint/cache/`. The default hard limit is 8 MiB; `cache.max_mib = 0` disables it.

Keys include tool, parser, and cache versions, effective configuration, file path, and source content. Least-recently-used records are trimmed by stored bytes. The cache is safe to delete and ignored by the supplied [`.gitignore`](.gitignore).

Release tests generate a 10,000-file repository and enforce the 10 ms startup and 50 ms warm one-file budgets.

## Development

Install contributor tools and the repository's versioned Git hooks:

```sh
brew bundle
./scripts/setup.sh
```

The test suite also requires Ruby for release automation checks. A CLI-only build can use `-DBUILD_TESTING=OFF`.

`scripts/setup.sh` generates `.git/hooks/` from the tracked sources in `scripts/hooks/`. It updates only changed hooks and exits without writing when they are current. It migrates the local `.githooks` setting but rejects other `core.hooksPath` settings without changing them.

The pre-commit hook checks staged whitespace and C formatting, then runs the Debug test suite. The post-merge hook refreshes installed hooks and warns when contributor tools are missing. The pre-push hook checks GitHub Actions dependency policy with Codependence, then runs the Release and sanitizer suites.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CLI-level fixtures cover each adapter, zero-config and configured policy, rc parity and inheritance, strict advisories, cache reuse and limits, discovery, JSON, and HTML.

Isolated tests in `tests/unit/` call the import parsers, configuration parser, boundary matchers, and graph functions with in-memory inputs. They cover source coordinates, ignored syntax, rc format parity and inheritance, policy hashes, path segments, and graph ownership and serialization. Assertions run in Release builds and the test code is instrumented in sanitizer builds.

CLI tests live in `tests/e2e/`. API, performance, and hook tests live in `tests/integration/`. Shared inputs and snapshots remain in `tests/fixtures/` and `tests/expected/`.

CTest labels select a group, for example `ctest --test-dir build -L unit --output-on-failure`. Other labels are `e2e`, `integration`, `scripts`, and `performance`; performance tests require a Release build.

## Releases

[`release.yml`](.github/workflows/release.yml) follows the [fs-lint release workflow](https://github.com/yowainwright/fs-lint/blob/main/.github/workflows/release.yml). A pushed `vMAJOR.MINOR.PATCH` tag must point to a commit on `main` and match `project(src_lint VERSION ...)` in `CMakeLists.txt`. That version also supplies `src-lint --version` and the parser cache's tool version.

The workflow tests native macOS and Linux builds for ARM64 and AMD64, runs the sanitizer suite, then publishes the source archive, four binaries named `src-lint-{darwin,linux}-{arm64,amd64}`, SHA256 files, and binary provenance attestations.

Homebrew automation uses `yowainwright/homebrew-tap`'s `brews/src-lint.json`, `scripts/new-formula`, and `scripts/update-formula`. It verifies every binary's checksum and [attestation](https://cli.github.com/manual/gh_attestation_verify), checks the host binary's version, generates the formula, runs Homebrew audit/install/test, and opens a tap PR. The inactive inventory becomes managed only after release verification; the formula's downloaded checksums must still match the verified binaries.

Before the first Homebrew update, the tap's inventory and CI changes must be merged. Configure the src-lint repository secret `HOMEBREW_TAP_TOKEN` with a fine-grained token restricted to `yowainwright/homebrew-tap`, granting Contents and Pull requests write access. Protect `main` and release tags, and require tap CI before merging formula PRs.

If GitHub publication succeeds but Homebrew fails, run the Release workflow manually from `main` with the existing tag. This retries Homebrew using the release scripts on `main`; it does not rebuild or replace published assets. The retry reuses an existing tap branch and open PR.

## Competitive landscape

src-lint turns intended dependency boundaries into executable policy. It protects service, component, package, and Proto ownership from accidental changes by humans and AI.

Its focus is a fast, local, polyglot intent graph with advisory discovery, deterministic enforcement, and progressive visualization.

_Landscape reviewed July 2026._

| Reference | Existing strength | src-lint's intended distinction |
| --- | --- | --- |
| [archlint](https://github.com/muhammetsafak/archlint) | Import boundaries for Go, TypeScript, and Python; bounded contexts; public ports; executable ADRs | AST-backed analysis, Proto support, nested provider-owned boundaries, advisory discovery, and an interactive graph |
| [structurelint](https://github.com/Jonathangadeaharder/structurelint) | Polyglot import graphs, architectural layers, cascading configuration, and automatic project detection | A focused architecture-conformance tool with monorepo semantics, explicit public entries, and boundary ownership |
| [Dependency Cruiser](https://github.com/sverweij/dependency-cruiser) | JavaScript and TypeScript rules, cycle checks, caching, and graph exports | One policy and graph spanning TypeScript, JavaScript, Python, Go, and Proto |
| [Nx module boundaries](https://nx.dev/docs/features/enforce-module-boundaries) | Monorepo tags, public APIs, cycle detection, and project graphs | Build-system-independent enforcement for nested boundaries within and across projects |
