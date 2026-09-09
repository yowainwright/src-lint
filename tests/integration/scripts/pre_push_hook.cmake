cmake_minimum_required(VERSION 3.20)

find_program(BASH bash REQUIRED)
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/bin")
file(WRITE "${WORK_ROOT}/bin/git" [=[#!/bin/sh
set -eu
test "$*" = 'rev-parse --show-toplevel'
printf '%s\n' "$PWD"
]=])
file(WRITE "${WORK_ROOT}/bin/gh" [=[#!/bin/sh
set -eu
test "$*" = 'auth token --hostname github.com'
printf 'gh\n' >> "$HOOK_LOG"
test "$MOCK_GH_STATUS" = 0
printf 'fixture-gh-token\n'
]=])
file(WRITE "${WORK_ROOT}/bin/codependence" [=[#!/bin/sh
set -eu
test "$*" = '--noCache --format table'
test "${GH_TOKEN:-}" = "${EXPECTED_TOKEN:-}"
printf 'codependence\n' >> "$HOOK_LOG"
exit "$MOCK_DEPENDENCY_STATUS"
]=])
file(WRITE "${WORK_ROOT}/bin/cmake" [=[#!/bin/sh
set -eu
printf 'cmake\n' >> "$HOOK_LOG"
printf 'pre-push unexpectedly ran a build or test suite\n' >&2
exit 99
]=])
foreach(command git gh codependence cmake)
  file(CHMOD "${WORK_ROOT}/bin/${command}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endforeach()
set(ENV{PATH} "${WORK_ROOT}/bin")
set(ENV{HOOK_LOG} "${WORK_ROOT}/calls.txt")

foreach(mode gh_login gh_token github_token both_tokens logged_out no_gh custom_api dependency_failure no_codependence)
  set(ENV{GH_TOKEN} "")
  set(ENV{GITHUB_TOKEN} "")
  unset(ENV{GITHUB_API_URL})
  set(ENV{MOCK_GH_STATUS} 0)
  set(ENV{MOCK_DEPENDENCY_STATUS} 0)
  set(ENV{EXPECTED_TOKEN} fixture-gh-token)
  set(expected_calls "gh\ncodependence\n")
  set(expected_exit 0)
  if(mode STREQUAL "gh_token" OR mode STREQUAL "both_tokens")
    set(ENV{GH_TOKEN} fixture-env-gh-token)
    set(ENV{EXPECTED_TOKEN} fixture-env-gh-token)
  endif()
  if(mode STREQUAL "github_token" OR mode STREQUAL "both_tokens")
    set(ENV{GITHUB_TOKEN} fixture-env-github-token)
    if(mode STREQUAL "github_token")
      set(ENV{EXPECTED_TOKEN} fixture-env-github-token)
    endif()
  endif()
  if(mode STREQUAL "custom_api")
    set(ENV{GITHUB_API_URL} https://example.invalid/api)
  endif()
  if(mode STREQUAL "logged_out" OR mode STREQUAL "no_gh" OR mode STREQUAL "custom_api")
    set(ENV{EXPECTED_TOKEN} "")
    set(ENV{MOCK_GH_STATUS} 1)
  endif()
  if(mode STREQUAL "no_gh")
    file(RENAME "${WORK_ROOT}/bin/gh" "${WORK_ROOT}/gh-disabled")
  endif()
  if(mode MATCHES "^(gh_token|github_token|both_tokens|no_gh|custom_api)$")
    set(expected_calls "codependence\n")
  elseif(mode STREQUAL "dependency_failure")
    set(ENV{MOCK_DEPENDENCY_STATUS} 2)
    set(expected_calls "gh\ncodependence\n")
    set(expected_exit 2)
  elseif(mode STREQUAL "no_codependence")
    file(RENAME "${WORK_ROOT}/bin/codependence" "${WORK_ROOT}/codependence-disabled")
    set(expected_calls "")
    set(expected_exit 1)
  endif()
  file(WRITE "$ENV{HOOK_LOG}" "")
  execute_process(COMMAND "${BASH}" "${REPO_ROOT}/scripts/hooks/pre-push"
    WORKING_DIRECTORY "${WORK_ROOT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(mode STREQUAL "no_gh")
    file(RENAME "${WORK_ROOT}/gh-disabled" "${WORK_ROOT}/bin/gh")
  elseif(mode STREQUAL "no_codependence")
    file(RENAME "${WORK_ROOT}/codependence-disabled" "${WORK_ROOT}/bin/codependence")
    if(NOT errors MATCHES "install codependence")
      message(FATAL_ERROR "Missing dependency did not report installation instructions\n${errors}")
    endif()
  endif()
  file(READ "$ENV{HOOK_LOG}" calls)
  if(NOT result STREQUAL "${expected_exit}" OR NOT calls STREQUAL expected_calls)
    message(FATAL_ERROR "${mode}: exit ${result}, expected ${expected_exit}\n${calls}\n${output}\n${errors}")
  endif()
  if(output MATCHES "fixture-.*token" OR errors MATCHES "fixture-.*token")
    message(FATAL_ERROR "${mode}: hook exposed a token")
  endif()
endforeach()
