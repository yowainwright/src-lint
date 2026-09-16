#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
readonly REPO_ROOT

require_command() {
  command -v "${1:?}" >/dev/null 2>&1 && return 0
  printf 'lint: missing %s; run brew bundle --file=scripts/Brewfile\n' "$1" >&2
  exit 1
}

clang_tidy_override() {
  [[ -n "${CLANG_TIDY:-}" ]] || return 1
  printf '%s\n' "$CLANG_TIDY"
}

resolve_clang_tidy() {
  clang_tidy_override && return 0
  command -v clang-tidy && return 0
  local prefix
  require_command brew
  prefix="$(brew --prefix llvm)"
  printf '%s/bin/clang-tidy\n' "$prefix"
}

check_shell() {
  local -a sources=(scripts/*.sh scripts/hooks/*)
  shellcheck "${sources[@]}"
  shellcheck-legibility check scripts/*.sh scripts/hooks/pre-commit scripts/hooks/post-merge
  shellcheck-legibility check scripts/hooks/pre-push --ignore LEG010
}

add_macos_sdk() {
  [[ "$(uname -s)" == Darwin ]] || return 0
  local sdk
  sdk="$(xcrun --sdk macosx --show-sdk-path)"
  args+=(--extra-arg=-isysroot "--extra-arg=$sdk")
}

check_tidy() {
  local build="${1:?}"
  shift
  local -a args=(--config-file=scripts/.clang-tidy -p "$build")
  add_macos_sdk
  "$clang_tidy" "${args[@]}" "$@"
}

check_c() {
  local build="$REPO_ROOT/build-lint"
  local -a sources=(src/*.c tests/unit/*.c tests/integration/*.c)
  "${CLANG_FORMAT:-clang-format}" --style=file:scripts/.clang-format --dry-run --Werror \
    "${sources[@]}" src/*.h include/src_lint/*.h tests/unit/*.h
  cmake -S . -B "$build" -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSRC_LINT_INSTALL_GIT_HOOKS=OFF -DBUILD_TESTING=ON
  cmake --build "$build" --parallel
  check_tidy "$build" "${sources[@]}"
  "$build/src-lint" check tests/fixtures/typescript-runtime-specifiers --strict
  ctest --test-dir "$build" --output-on-failure --no-tests=error -R '^service_boundary_violation$'
}

main() {
  cd "$REPO_ROOT"
  local clang_tidy command
  clang_tidy="$(resolve_clang_tidy)"
  for command in git cmake shellcheck shellcheck-legibility fs-lint \
    "${CLANG_FORMAT:-clang-format}" "$clang_tidy"; do
    require_command "$command"
  done
  git ls-files --cached --others --exclude-standard -z | fs-lint check --stdin0
  check_shell
  check_c
}

main "$@"
