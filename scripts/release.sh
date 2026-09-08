#!/usr/bin/env bash
# Follows fs-lint's release-to-tap workflow and the tap's formula generator API.
set -euo pipefail

readonly RELEASE_REPOSITORY=yowainwright/src-lint
readonly TAP_REPOSITORY=yowainwright/homebrew-tap
readonly TARGETS=(darwin-arm64 darwin-amd64 linux-arm64 linux-amd64)

fail() {
  printf 'release: %s\n' "$1" >&2
  exit 1
}

require_tag() {
  [[ "$1" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] ||
    fail 'expected a stable vMAJOR.MINOR.PATCH tag'
}

verify_version() {
  local output
  output="$("$1" --version)"
  [[ "$output" == "src-lint ${2#v}" ]] || fail 'tag does not match binary version'
}

require_tap() {
  local script status
  [[ -n "${GH_TOKEN:-}" ]] || fail 'GH_TOKEN is required'
  for script in new-formula update-formula validate-tap; do
    [[ -x "scripts/$script" ]] || fail "missing tap script: scripts/$script"
  done
  [[ -f brews/src-lint.json ]] || fail 'missing tap inventory: brews/src-lint.json'
  status="$(git status --porcelain)"
  [[ -z "$status" ]] || fail 'tap worktree must be clean'
}

verify_asset() {
  local asset="$1" tag="$2" digest
  [[ -s "$asset" ]] || fail "missing binary: $asset"
  digest="$(shasum -a 256 "$asset")"
  [[ "$(cat "$asset.sha256")" == "$digest" ]] || fail "checksum mismatch: $asset"
  gh attestation verify "$asset" --repo "$RELEASE_REPOSITORY" \
    --bundle "$asset.sigstore.json" --source-ref "refs/tags/$tag" \
    --signer-workflow "$RELEASE_REPOSITORY/.github/workflows/release.yml" \
    --deny-self-hosted-runners
}

verify_release() {
  local tag="$1" target published
  published="$(gh release view "$tag" --repo "$RELEASE_REPOSITORY" \
    --json isDraft,isPrerelease,tagName --jq '[.isDraft, .isPrerelease, .tagName] | @tsv')"
  [[ "$published" == "$(printf 'false\tfalse\t%s' "$tag")" ]] || fail 'release is not published and stable'
  gh release download "$tag" --repo "$RELEASE_REPOSITORY" --dir "$release_dir" \
    --pattern 'src-lint-darwin-*' --pattern 'src-lint-linux-*'
  for target in "${TARGETS[@]}"; do
    (cd "$release_dir" && verify_asset "src-lint-$target" "$tag")
  done
  verify_host_version "$tag"
}

verify_host_version() {
  local target
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=darwin-arm64 ;;
    Darwin-x86_64) target=darwin-amd64 ;;
    Linux-aarch64 | Linux-arm64) target=linux-arm64 ;;
    Linux-x86_64) target=linux-amd64 ;;
    *) fail 'unsupported host for release verification' ;;
  esac
  chmod +x "$release_dir/src-lint-$target"
  verify_version "$release_dir/src-lint-$target" "$1"
}

prepare_branch() {
  local branch="$1" existing
  existing="$(git ls-remote --heads origin "$branch")"
  if [[ -n "$existing" ]]; then
    git fetch origin "$branch"
    git checkout -b "$branch" FETCH_HEAD
    return
  fi
  git checkout -b "$branch"
}

generate_formula() {
  local version="$1" helper="$2" generator=scripts/new-formula
  ruby "$helper" "$PWD" "$version"
  [[ ! -f Formula/src-lint.rb ]] || generator=scripts/update-formula
  "$generator" src-lint "$version"
}

verify_formula_checksums() {
  local target digest
  for target in "${TARGETS[@]}"; do
    digest="$(shasum -a 256 "$release_dir/src-lint-$target")"
    digest="${digest%% *}"
    grep -Fq "sha256 \"$digest\"" Formula/src-lint.rb || fail 'formula artifact changed after verification'
  done
}

validate_formula() {
  local installed_tap
  brew tap yowainwright/tap "$PWD"
  installed_tap="$(brew --repository yowainwright/tap)"
  cp Formula/src-lint.rb "$installed_tap/Formula/src-lint.rb"
  brew audit --strict --online yowainwright/tap/src-lint
  brew install yowainwright/tap/src-lint
  brew test yowainwright/tap/src-lint
}

commit_formula() {
  local tag="$1" status=0
  git add Formula/src-lint.rb brews/src-lint.json README.md
  git diff --cached --quiet || status="$?"
  [[ "$status" != 0 ]] || return 0
  [[ "$status" == 1 ]] || fail 'could not inspect staged formula changes'
  git config user.name 'github-actions[bot]'
  git config user.email '41898282+github-actions[bot]@users.noreply.github.com'
  git commit -m "src-lint $tag"
}

open_pull_request() {
  local branch="$1" tag="$2" existing body
  existing="$(gh pr list --repo "$TAP_REPOSITORY" --head "yowainwright:$branch" \
    --state open --json url --jq '.[0].url // ""')"
  if [[ -n "$existing" ]]; then
    printf '%s\n' "$existing"
    return
  fi
  body="$release_dir/pr-body.md"
  printf 'Updates src-lint to %s from the verified release binaries.\n\nValidation: SHA256, provenance, brew audit, install, and test.\n' "$tag" >"$body"
  gh pr create --repo "$TAP_REPOSITORY" --base main --head "yowainwright:$branch" \
    --title "src-lint $tag" --body-file "$body"
}

run_homebrew_pr() {
  local tap="$1" tag="$2" helper branch="src-lint-$2"
  helper="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/update-tap-package"
  cd "$tap"
  require_tap
  release_dir="$(mktemp -d "$PWD/.src-lint-release.XXXXXX")"
  trap 'rm -rf "$release_dir"' EXIT
  verify_release "$tag"
  prepare_branch "$branch"
  generate_formula "${tag#v}" "$helper"
  verify_formula_checksums
  validate_formula
  commit_formula "$tag"
  git push origin "$branch"
  open_pull_request "$branch" "$tag"
}

main() {
  [[ "$#" -eq 3 ]] || fail 'usage: scripts/release.sh {verify-version <binary>|homebrew-pr <tap-dir>} <tag>'
  require_tag "$3"
  case "$1" in
    verify-version) verify_version "$2" "$3" ;;
    homebrew-pr) run_homebrew_pr "$2" "$3" ;;
    *) fail 'unknown release command' ;;
  esac
}

main "$@"
