#!/usr/bin/env bash

set -euo pipefail

readonly HOOKS_PATH=".githooks"
readonly -a MANAGED_HOOKS=("pre-commit" "pre-push")

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
readonly REPO_ROOT

fail() {
  printf 'install-git-hooks: %s\n' "$1" >&2
  exit 1
}

assert_repository_root() {
  local git_root
  git_root="$(git -C "$REPO_ROOT" rev-parse --show-toplevel 2>/dev/null)" ||
    fail "$REPO_ROOT is not a Git worktree"
  git_root="$(cd -- "$git_root" && pwd -P)"
  [[ "$git_root" == "$REPO_ROOT" ]] || fail "script must be inside the repository root"
}

configured_hooks_path() {
  git -C "$REPO_ROOT" config --local --get core.hooksPath 2>/dev/null || true
}

assert_hook_path_available() {
  local current_path
  current_path="$(configured_hooks_path)"
  [[ -z "$current_path" || "$current_path" == "$HOOKS_PATH" ]] && return
  fail "core.hooksPath is already set to $current_path"
}

assert_legacy_hooks_available() {
  local current_path hooks_dir hook
  current_path="$(configured_hooks_path)"
  [[ -n "$current_path" ]] && return
  hooks_dir="$(git -C "$REPO_ROOT" rev-parse --git-path hooks)"
  [[ "$hooks_dir" == /* ]] || hooks_dir="$REPO_ROOT/$hooks_dir"
  for hook in "${MANAGED_HOOKS[@]}"; do
    [[ ! -e "$hooks_dir/$hook" ]] || fail "existing $hooks_dir/$hook would be bypassed"
  done
}

install_hooks() {
  local hook
  for hook in "${MANAGED_HOOKS[@]}"; do
    [[ -f "$REPO_ROOT/$HOOKS_PATH/$hook" ]] || fail "missing $HOOKS_PATH/$hook"
    chmod +x "$REPO_ROOT/$HOOKS_PATH/$hook"
  done
  git -C "$REPO_ROOT" config --local core.hooksPath "$HOOKS_PATH"
}

main() {
  (( $# == 0 )) || fail "usage: scripts/install-git-hooks.sh"
  assert_repository_root
  assert_hook_path_available
  assert_legacy_hooks_available
  install_hooks
  printf 'Git hooks installed from %s.\n' "$HOOKS_PATH"
}

main "$@"
