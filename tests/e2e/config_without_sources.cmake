cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/.git")
set(FIXTURE "${WORK_ROOT}")
set(EXPECTED_ERROR "invalid .* configuration")

file(WRITE "${WORK_ROOT}/${CONFIG_NAME}" "invalid")
include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")

file(REMOVE "${WORK_ROOT}/${CONFIG_NAME}")
file(WRITE "${WORK_ROOT}/source.ts" "export {};\n")
file(WRITE "${WORK_ROOT}/empty/deep/${CONFIG_NAME}" "invalid")
include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")
