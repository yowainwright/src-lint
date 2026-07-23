execute_process(
  COMMAND "${CLI}" graph "${FIXTURE}" --format html
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)

if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected graph finding exit 1, received ${result}\n${errors}")
endif()

set(required
  "<!doctype html>"
  "<script id=\"graph-data\" type=\"application/json\">"
  "services/orders/create.ts"
  "TL1001"
  "id=\"boundary-filter\""
  "id=\"graph-canvas\""
)

foreach(fragment IN LISTS required)
  string(FIND "${output}" "${fragment}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Graph HTML is missing ${fragment}")
  endif()
endforeach()
