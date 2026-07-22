if(NOT DEFINED SCOPE)
  set(SCOPE "${FIXTURE}")
endif()

execute_process(
  COMMAND "${CLI}" check "${SCOPE}" --format json
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

file(READ "${EXPECTED}" expected)

if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected policy exit 1, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected JSON diagnostic\n${output}")
endif()
