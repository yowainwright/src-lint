execute_process(COMMAND "${CLI}" --version
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output STREQUAL "src-lint ${VERSION}\n"
    OR NOT error STREQUAL "")
  message(FATAL_ERROR "Unexpected version response: ${result}: ${output}${error}")
endif()

execute_process(COMMAND "${CLI}" --version extra
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_QUIET)
if(NOT result EQUAL 2 OR NOT output STREQUAL "")
  message(FATAL_ERROR "Version accepted unexpected arguments")
endif()
