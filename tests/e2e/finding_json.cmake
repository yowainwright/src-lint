if(NOT DEFINED COMMAND)
  set(COMMAND check)
endif()
if(NOT DEFINED EXPECTED_EXIT)
  set(EXPECTED_EXIT 1)
endif()
if(NOT DEFINED SCOPE)
  set(SCOPE "${FIXTURE}")
endif()

set(arguments "${COMMAND}" "${SCOPE}" --format json)
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

if(NOT result STREQUAL "${EXPECTED_EXIT}")
  message(FATAL_ERROR "Expected exit ${EXPECTED_EXIT}, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected ${COMMAND} JSON\nExpected (${EXPECTED}):\n${expected}\nActual:\n${output}\n${errors}")
endif()
