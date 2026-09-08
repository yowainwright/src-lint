set(missing "${FIXTURE}/missing")

execute_process(
  COMMAND "${CLI}" check "${missing}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

set(prefix "src-lint: cannot scan ")
set(expected "${prefix}${missing}\n")

if(NOT result EQUAL 2)
  message(FATAL_ERROR "Expected operational exit 2, received ${result}")
endif()

if(NOT output STREQUAL "")
  message(FATAL_ERROR "Unexpected standard output\n${output}")
endif()

if(NOT errors STREQUAL expected)
  message(FATAL_ERROR "Unexpected error output\n${errors}")
endif()
