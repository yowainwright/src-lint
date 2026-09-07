#!/usr/bin/env bash

set -euo pipefail

readonly LEGACY_HOOKS_PATH=".githooks"
readonly HOOK_SOURCE_DIR="scripts/hooks"
readonly -a MANAGED_HOOKS=("pre-commit" "pre-push" "post-merge")

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
readonly REPO_ROOT

fail() {
  printf 'setup: %s\n' "$1" >&2
  exit 1
}

assert_repository_root() {
  local git_root
  git_root="$(git -C "$REPO_ROOT" rev-parse --show-toplevel 2>/dev/null)" ||
    fail "$REPO_ROOT is not a Git worktree"
  git_root="$(cd -- "$git_root" && pwd -P)"
  [[ "$git_root" == "$REPO_ROOT" ]] || fail "script must be inside the repository root"
}

hook_path_settings() {
  local settings status=0
  settings="$(git -C "$REPO_ROOT" config --show-scope --get-all core.hooksPath)" || status=$?
  [[ "$status" == 0 || "$status" == 1 ]] || fail "cannot read core.hooksPath"
  printf '%s' "$settings"
}

assert_hook_path_available() {
  local settings setting
  settings="$(hook_path_settings)"
  [[ -z "$settings" ]] && return 0
  # Removing a local override must not expose another configured hooks path.
  while IFS= read -r setting; do
    [[ "$setting" == $'local\t'"$LEGACY_HOOKS_PATH" ]] ||
      fail "unset conflicting core.hooksPath before setup: $setting"
  done <<<"$settings"
}

git_hooks_dir() {
  local git_dir
  # --git-path hooks follows core.hooksPath, including the path being migrated.
  git_dir="$(git -C "$REPO_ROOT" rev-parse --git-common-dir)"
  [[ "$git_dir" == /* ]] || git_dir="$REPO_ROOT/$git_dir"
  printf '%s/hooks\n' "$git_dir"
}

managed_hook() {
  local path="$1"
  [[ -f "$path" ]] && grep -Fq "src-lint managed Git hook" "$path"
}

assert_hook_target_available() {
  local hooks_dir="$1"
  local hook
  for hook in "${MANAGED_HOOKS[@]}"; do
    [[ -f "$REPO_ROOT/$HOOK_SOURCE_DIR/$hook" ]] || fail "missing $HOOK_SOURCE_DIR/$hook"
    local target="$hooks_dir/$hook"
    [[ ! -e "$target" ]] && continue
    managed_hook "$target" ||
      fail "existing $target is not managed by this installer"
  done
}

hook_is_current() {
  local source="$1" target="$2"
  [[ -x "$target" ]] && cmp -s "$source" "$target"
}

hooks_are_current() {
  local hooks_dir="$1" hook
  [[ -z "$(hook_path_settings)" ]] || return 1
  for hook in "${MANAGED_HOOKS[@]}"; do
    hook_is_current "$REPO_ROOT/$HOOK_SOURCE_DIR/$hook" "$hooks_dir/$hook" || return 1
  done
}

install_hooks() {
  local hooks_dir="$1"
  /bin/mkdir -p "$hooks_dir"
  local hook
  for hook in "${MANAGED_HOOKS[@]}"; do
    local source="$REPO_ROOT/$HOOK_SOURCE_DIR/$hook"
    hook_is_current "$source" "$hooks_dir/$hook" && continue
    /bin/cp "$source" "$hooks_dir/$hook"
    /bin/chmod +x "$hooks_dir/$hook"
  done
  if [[ -n "$(hook_path_settings)" ]]; then
    git -C "$REPO_ROOT" config --local --unset-all core.hooksPath
  fi
}

main() {
  (($# == 0)) || fail "usage: scripts/setup.sh"
  assert_repository_root
  assert_hook_path_available
  local hooks_dir
  hooks_dir="$(git_hooks_dir)"
  assert_hook_target_available "$hooks_dir"
  hooks_are_current "$hooks_dir" && return 0
  install_hooks "$hooks_dir"
  printf 'Git hooks updated in .git/hooks.\n'
}

main "$@"
