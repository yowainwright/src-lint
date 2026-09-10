cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}")
file(REMOVE "${WORK_ROOT}/domains/orders/.src-lintrc.toml")
file(WRITE "${WORK_ROOT}/domains/orders/.src-lintrc"
  [[{"boundaries":{"billing":{"public":["proto/**"]}}}]])
set(FIXTURE "${WORK_ROOT}")
include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")

file(REMOVE "${WORK_ROOT}/.src-lintrc.toml")
file(WRITE "${WORK_ROOT}/.src-lintrc" [[{
  "version": 1,
  "boundaries": {
    "orders": {"root": "domains/orders"},
    "billing": {"root": "domains/billing", "public": ["api/**"]}
  }
}]])
include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")

set(SCOPE "${WORK_ROOT}/domains/orders/create.ts")
include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")

file(RENAME "${WORK_ROOT}/domains/orders/.src-lintrc"
  "${WORK_ROOT}/domains/billing/.src-lintrc")
include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")

foreach(extension toml json yaml yml)
  file(WRITE "${WORK_ROOT}/.src-lintrc.${extension}" "")
  include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")
  file(REMOVE "${WORK_ROOT}/.src-lintrc.${extension}")
endforeach()

file(WRITE "${WORK_ROOT}/.src-lintrc" "{\"strict\":")
set(EXPECTED_ERROR "invalid JSON configuration")
include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")
