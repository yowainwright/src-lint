execute_process(
  COMMAND "${CLI}" graph "${FIXTURE}" --format json
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected graph finding exit 1, received ${result}\n${errors}")
endif()

string(FIND "${output}" "\"language\": \"${LANGUAGE}\"" language_position)
string(FIND "${output}" "\"status\": \"violation\"" status_position)
if(language_position EQUAL -1 OR status_position EQUAL -1)
  message(FATAL_ERROR "Graph is missing the ${LANGUAGE} violation edge")
endif()
