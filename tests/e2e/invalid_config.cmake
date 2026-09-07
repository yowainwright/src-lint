execute_process(
  COMMAND "${CLI}" check "${FIXTURE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

if(NOT DEFINED EXPECTED_ERROR)
  set(EXPECTED_ERROR "multiple rc files")
endif()

if(NOT result EQUAL 2)
  message(FATAL_ERROR "Expected invalid-config exit 2, received ${result}\n${output}\n${errors}")
endif()

if(NOT errors MATCHES "${EXPECTED_ERROR}")
  message(FATAL_ERROR "Expected config error '${EXPECTED_ERROR}'\n${errors}")
endif()
