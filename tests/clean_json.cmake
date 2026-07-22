execute_process(
  COMMAND "${CLI}" check "${FIXTURE}" --format json
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

file(READ "${EXPECTED}" expected)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Expected clean exit, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected JSON output\n${output}")
endif()
