execute_process(
  COMMAND "${CLI}" discover "${FIXTURE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

set(expected "billing services/billing inferred 2 files\norders services/orders inferred 1 file\n")

if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected discovery finding exit 1, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL expected)
  message(FATAL_ERROR "Unexpected discovery text\n${output}")
endif()
