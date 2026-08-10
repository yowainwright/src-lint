file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}")

set(internal_alias "${WORK_ROOT}/services/orders/billing_internal")
set(internal_hop "${WORK_ROOT}/services/orders/billing_internal_hop")
set(chained_alias "${WORK_ROOT}/services/orders/billing_internal_chain")
set(public_alias "${WORK_ROOT}/services/orders/billing_api")
file(CREATE_LINK "../billing/internal" "${internal_alias}" SYMBOLIC RESULT internal_link_result)
file(CREATE_LINK "../billing/internal" "${internal_hop}" SYMBOLIC RESULT hop_link_result)
file(CREATE_LINK "billing_internal_hop" "${chained_alias}" SYMBOLIC RESULT chain_link_result)
file(CREATE_LINK "../billing/api" "${public_alias}" SYMBOLIC RESULT public_link_result)
if(NOT internal_link_result STREQUAL "0" OR NOT hop_link_result STREQUAL "0" OR
   NOT chain_link_result STREQUAL "0" OR NOT public_link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create boundary aliases")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json --strict
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
