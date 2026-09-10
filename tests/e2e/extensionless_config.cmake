cmake_minimum_required(VERSION 3.20)

function(check_findings scope)
  set(SCOPE "${scope}")
  set(EXPECTED_EXIT 1)
  set(COMMAND check)
  include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")
endfunction()

function(check_invalid_config expected_error)
  set(FIXTURE "${WORK_ROOT}")
  set(EXPECTED_ERROR "${expected_error}")
  include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")
endfunction()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}"
  PATTERN ".src-lint" EXCLUDE)
file(REMOVE "${WORK_ROOT}/domains/orders/.src-lintrc.toml")
file(WRITE "${WORK_ROOT}/domains/orders/.src-lintrc"
  [[{"boundaries":{"billing":{"public":["proto/**"]}}}]])
check_findings("${WORK_ROOT}")

file(REMOVE "${WORK_ROOT}/.src-lintrc.toml")
file(WRITE "${WORK_ROOT}/.src-lintrc" [[{
  "version": 1,
  "boundaries": {
    "orders": {"root": "domains/orders"},
    "billing": {"root": "domains/billing", "public": ["api/**"]}
  }
}]])
check_findings("${WORK_ROOT}")

check_findings("${WORK_ROOT}/domains/orders/create.ts")

file(RENAME "${WORK_ROOT}/domains/orders/.src-lintrc"
  "${WORK_ROOT}/domains/billing/.src-lintrc")
check_findings("${WORK_ROOT}")
check_findings("${WORK_ROOT}/domains/orders/create.ts")

foreach(extension toml json yaml yml)
  file(WRITE "${WORK_ROOT}/.src-lintrc.${extension}" "")
  check_invalid_config("multiple rc files")
  file(REMOVE "${WORK_ROOT}/.src-lintrc.${extension}")
endforeach()

file(WRITE "${WORK_ROOT}/.src-lintrc" "{\"strict\":")
check_invalid_config("invalid JSON configuration")
