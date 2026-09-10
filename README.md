# src-lint

src-lint checks imports between services, packages, and components to catch access to private code.

src-lint focuses on import boundaries, with the aim of staying fast and easy to use with coding agents.

[What it checks](#what-it-checks) · [Install](#install) · [CLI](#cli) · [Configuration](#configuration) · [Development](#development) · [Releases](#releases)

## What it checks

By default, each `services/<name>` directory is a boundary. Other services can import from its `api/`, `public/`, or `proto/` paths.

Importing private code reports `SL1001`. Unresolved local imports report `SL2001` as an advisory; `--strict` makes them errors.

Supports TypeScript, JavaScript, Python, Go, and Proto so far. More languages are welcome and appreciated.

### TypeScript and JavaScript

Given this layout, `orders` can import `billing/api/index.ts` but cannot import `billing/internal/ledger.ts`:

```text
services/
├── orders/
│   └── create.ts
└── billing/
    ├── api/index.ts
    └── internal/ledger.ts
```

The public API file can expose a function from the same service's internals. In `services/billing/api/index.ts`:

```ts
export { postEntry } from "../internal/ledger";
```

Then, in `services/orders/create.ts`:

```diff
-import { postEntry } from "../billing/internal/ledger";
+import { postEntry } from "../billing/api";
```

See the [working fixture](tests/fixtures/service-boundary/services/orders/create.ts) and its [expected finding](tests/expected/service_boundary.json).

JavaScript and TypeScript directory imports need a supported `index` file, such as `index.ts`. An empty directory does not resolve.

### Python

Use the public module instead of the internal one:

```diff
-from services.billing.internal import ledger
+from services.billing.api import ledger
```

See the [Python fixture](tests/fixtures/python-absolute/services/orders/create.py).

### Go

Import the API package instead of its internal implementation:

```diff
-import "example.com/repo/services/billing/internal/ledger"
+import "example.com/repo/services/billing/api"
```

See the [Go fixture](tests/fixtures/go-single/services/orders/create.go).

### Proto

Import a file from the service's `proto/` directory:

```diff
-import "services/billing/internal/ledger.proto";
+import public "services/billing/proto/public.proto";
```

The `proto/` path makes this import allowed. See the [Proto fixture](tests/fixtures/proto-import/services/orders/proto/order.proto).

<details>
<summary>Recognized import syntax</summary>

| Language | Imports recognized |
| --- | --- |
| TypeScript and JavaScript | Static imports, re-exports, dynamic `import()`, and `require()` |
| Python | `from … import …` and `import …` |
| Go | Single and grouped imports |
| Proto | Regular, `public`, and `weak` imports |

</details>

## Install

### Homebrew

The [release workflow](.github/workflows/release.yml) builds macOS and Linux binaries for ARM64 and AMD64, then opens a Homebrew tap PR. The first release and formula are not published yet. Once available:

```sh
brew install yowainwright/tap/src-lint
src-lint --version
```

### Build and install locally

Requires CMake 3.20+ and a C11 compiler on macOS or Linux. This installs the binary and license under `build/install/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSRC_LINT_INSTALL_GIT_HOOKS=OFF
cmake --build build --parallel
cmake --install build --prefix ./build/install
export PATH="$PWD/build/install/bin:$PATH"
src-lint --version
```

Choose another install directory with `--prefix`. The binary uses the system C library; no separate src-lint library or language runtime is needed. See [CMakeLists.txt](CMakeLists.txt).

## CLI

Run from the repository you want to check. These examples use the [included fixture](tests/fixtures/service-boundary), so run them from the src-lint checkout.

| Command | Result |
| --- | --- |
| `src-lint check [path] [--strict] [--format text\|json]` | Report import violations |
| `src-lint discover [path] [--format text\|json]` | List boundaries and source-file counts |
| `src-lint graph [path] [--format json\|html]` | Export dependencies as JSON or an interactive HTML graph |

### `src-lint check`

Check a file or directory. With no path, check the current directory:

```sh
src-lint check tests/fixtures/service-boundary
```

```text
services/orders/create.ts:1:28 SL1001 orders cannot import billing internals -> services/billing/internal/ledger.ts
```

The command exits with `1`. The private import fails; the public API import in the same file passes. The output gives the file, line, column, rule, and rejected target.

Use JSON for scripts and `--strict` to make unresolved imports errors:

```sh
src-lint check tests/fixtures/service-boundary --strict --format json
```

The result contains a `findings` array. Each boundary violation includes `rule`, `source`, `line`, `column`, `sourceBoundary`, `targetBoundary`, and `target`. See the [complete JSON result](tests/expected/service_boundary.json).

| Option | Meaning | Default |
| --- | --- | --- |
| `[path]` | Source file or directory to check | `.` |
| `--strict` | Treat unresolved local imports (`SL2001`) as errors | Off, unless enabled in config |
| `--format text\|json` | Human-readable diagnostics or JSON | `text` |

### `src-lint discover`

List boundaries and the number of source files in each:

```sh
src-lint discover tests/fixtures/service-boundary
```

```text
billing services/billing inferred 2 files
orders services/orders inferred 1 file
```

Each row shows the name, root, whether the boundary was configured or inferred, and file count. Use `--format json` for a [`boundaries` array](tests/expected/discover.json). This fixture exits with `1` because it contains a violation. `--strict` is not accepted by this command.

### `src-lint graph`

Export imports as JSON, or save an interactive HTML graph:

```sh
src-lint graph tests/fixtures/service-boundary --format json > dependency-graph.json
src-lint graph tests/fixtures/service-boundary --format html > dependency-graph.html
```

Open `dependency-graph.html` in a browser to filter boundaries, pan, and zoom. JSON is the default format; `text` and `--strict` are not supported.

| JSON field | Meaning |
| --- | --- |
| `nodes` | Source files, each with an `id` and owning `boundary` |
| `edges` | Imports, with `from`, `to`, source coordinates, and language |
| `status` | Whether an edge is allowed, a violation, an advisory, or an error |
| `suggestedPublicEntry` | Suggested public path for a violation, or `null` |

The [sample graph](tests/expected/graph.json) has one allowed edge and one violation. Both exports exit with `1` for this fixture and still write the graph.

### Help and exit codes

Use `src-lint --help` (or `-h`) for command syntax and `src-lint --version` for the installed version.

| Code | Meaning |
| --- | --- |
| `0` | No errors or boundary violations; unresolved advisories may remain |
| `1` | Boundary violations or unresolved imports treated as errors |
| `2` | Invalid arguments, invalid config, or a failed operation |

## Configuration

Configured rules take precedence over defaults. Use one config file per directory. If a check finds two or more supported rc files in the same directory, it exits with configuration error `2`:

| Filename | Format |
| --- | --- |
| `.src-lintrc` or `.src-lintrc.json` | JSON |
| `.src-lintrc.toml` | TOML |
| `.src-lintrc.yaml` or `.src-lintrc.yml` | YAML |

### Define public paths

Save this as `.src-lintrc` at your repository root:

```json
{
  "version": 1,
  "strict": false,
  "cache": { "max_mib": 8 },
  "boundaries": {
    "orders": {
      "root": "services/orders",
      "allow": ["shared/**"]
    },
    "billing": {
      "root": "services/billing",
      "public": ["api/**", "proto/**"]
    }
  }
}
```

Run `src-lint check .`. Here, `orders` may import from `shared/**`, and `billing` exposes `api/**` and `proto/**`. An import from `orders` into `billing/internal/` still fails with `SL1001`.

| Setting | Meaning | Default |
| --- | --- | --- |
| `version` | Config format version | `1`; no other version is accepted |
| `strict` | Treat unresolved local imports as errors | `false` |
| `cache.max_mib` | Cache limit in MiB; `0` disables caching | `8` |
| `boundaries.<name>.root` | Boundary directory, relative to the repository | Required for each boundary |
| `boundaries.<name>.public` | Paths others may import, relative to this boundary's root | No public paths when configured |
| `boundaries.<name>.allow` | Extra paths this boundary may import, relative to the repository | No exceptions |

Patterns must be exact paths or prefixes ending in `/**`; `*.ts` and `api/*` are invalid. Change `root` to use directories such as `packages/billing` or `components/billing`. The same settings work in [TOML](tests/fixtures/config-toml/.src-lintrc.toml) and [YAML](tests/fixtures/config-yaml/.src-lintrc.yaml).

### Allow a specific private import

In the config above, change `boundaries.orders.allow` to:

```json
["shared/**", "services/billing/internal/ledger.ts"]
```

Run `src-lint check services/orders`. The import of `../billing/internal/ledger` now passes. Other boundaries still cannot import that private file. See the [exception fixture](tests/fixtures/config-allow).

### Override a rule in one directory

Starting with the basic config, add `services/orders/.src-lintrc`:

```json
{
  "boundaries": {
    "billing": { "public": ["proto/**"] }
  }
}
```

Run `src-lint check services/orders`. Imports from `billing/api/` now fail; imports from `billing/proto/` pass:

```diff
-import { listEntries } from "../billing/api/index.ts";
+import { listProtoEntries } from "../billing/proto/index.ts";
```

Child scalar values and arrays replace parent values; omitted settings are inherited. See the [nested config fixture](tests/fixtures/config-child).

<details>
<summary>Configuration inheritance rules</summary>

Boundaries merge by name. Config files along the imported path apply public-entry rules after importer-side overrides. Shared ancestors apply once. Only the importing boundary can grant `allow` exceptions.

</details>

### Cache

Parsed imports are cached in `.src-lint/cache/`, up to 8 MiB by default. Set `cache.max_mib = 0` to disable caching. The cache is safe to delete and covered by [`.gitignore`](.gitignore).

## Development

Install local tools and run the tests:

```sh
brew bundle --file=scripts/Brewfile
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --parallel 4 --output-on-failure
```

Format C with `clang-format --style=file:scripts/.clang-format`; use the same config in your editor. See [contributing](.github/CONTRIBUTING.md) and [tests](tests).

<details>
<summary>Hooks and additional tests</summary>

Run `./scripts/setup.sh` to install hooks before your first build. The [hooks](scripts/hooks) check formatting and Debug tests before commits, refresh after merges, and check dependencies with Codependence before pushes. Pre-push uses `GH_TOKEN`, `GITHUB_TOKEN`, or your `gh auth login` session; anonymous requests are rate-limited.

Run one test group with `ctest --test-dir build -L unit --output-on-failure`. Other labels are `e2e`, `integration`, `scripts`, `release`, and `performance`. Performance tests require a Release build.

Run sanitizer checks:

```sh
cmake -S . -B build-asan -DSRC_LINT_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

[CI](.github/workflows/ci.yml) runs Release and sanitizer tests. [Performance tests](tests/integration/performance_test.c) check 10 ms startup and 50 ms warm one-file budgets on a 10,000-file repository. CI enforces those budgets on Ubuntu 24.04 x64; local Release tests include them too.

</details>

## Releases

The [release workflow](.github/workflows/release.yml) publishes source and macOS/Linux binaries for ARM64 and AMD64, with checksums and provenance attestations.

<details>
<summary>Maintainer setup and retries</summary>

Tags use `vMAJOR.MINOR.PATCH`, must point to a commit on `main`, and must match the version in [CMakeLists.txt](CMakeLists.txt).

Before Homebrew updates, merge the tap's inventory and CI setup. Set `HOMEBREW_TAP_TOKEN` to a fine-grained token limited to `yowainwright/homebrew-tap`, with Contents and Pull requests write access. Protect `main` and release tags, and require tap CI before merging formula PRs.

[Release scripts](scripts/release.sh) verify binaries, test the Homebrew formula, and open a tap PR. If Homebrew fails after publication, rerun the Release workflow from `main` with the existing tag. It reuses the tap PR without replacing published assets.

</details>
