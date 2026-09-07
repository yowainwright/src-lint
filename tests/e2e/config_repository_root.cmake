file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/repo/.git" "${WORK_ROOT}/repo/services/orders")
file(WRITE "${WORK_ROOT}/.src-lintrc.toml" "version = 1\nstrict = true\n")
file(WRITE "${WORK_ROOT}/repo/services/orders/create.ts" "import \"./missing.ts\";\n")

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}/repo/services/orders/create.ts" --format json
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

file(READ "${EXPECTED}" expected)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Configuration escaped the repository root\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected repository-root diagnostic\n${output}")
endif()
