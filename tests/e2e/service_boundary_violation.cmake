if(NOT DEFINED SCOPE)
  set(SCOPE "${FIXTURE}")
endif()

if(DEFINED WORKING_DIRECTORY)
  execute_process(
    COMMAND "${CLI}" check "${SCOPE}"
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
  )
else()
  execute_process(
    COMMAND "${CLI}" check "${SCOPE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
  )
endif()

set(location "services/orders/create.ts")
set(position ":1:28")
set(finding " SL1001 orders cannot import billing internals")
set(target " -> services/billing/internal/ledger.ts")
set(expected "${location}${position}${finding}${target}")

if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected policy exit 1, received ${result}\n${errors}")
endif()

if(NOT output STREQUAL "${expected}\n")
  message(FATAL_ERROR "Unexpected diagnostic\n${output}")
endif()
