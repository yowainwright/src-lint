set(arguments check "${FIXTURE}" --format json)
if(STRICT)
  list(APPEND arguments --strict)
endif()

execute_process(
  COMMAND "${CLI}" ${arguments}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

file(READ "${EXPECTED}" expected)

if(NOT result EQUAL EXPECTED_EXIT)
  message(FATAL_ERROR "Expected exit ${EXPECTED_EXIT}, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected JSON diagnostic\n${output}")
endif()
