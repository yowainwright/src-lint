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

file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}")
file(WRITE "${WORK_ROOT}/services/orders/create.ts"
     "import { ledger } from \"../billing/internal/ledger.ts\";\nvoid ledger;\n")
file(REMOVE_RECURSE "${WORK_ROOT}/services/billing/internal")
file(MAKE_DIRECTORY "${WORK_ROOT}/vendor/billing-internal")
file(WRITE "${WORK_ROOT}/vendor/billing-internal/ledger.ts" "export const ledger = 1;\n")
file(CREATE_LINK "../../vendor/billing-internal"
     "${WORK_ROOT}/services/billing/internal" SYMBOLIC RESULT subtree_link_result)
if(NOT subtree_link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create symlinked boundary subtree")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json --strict
  RESULT_VARIABLE subtree_result
  OUTPUT_VARIABLE subtree_output
  ERROR_VARIABLE subtree_errors
)

if(NOT subtree_result EQUAL 1 OR NOT subtree_output MATCHES "TL1001")
  message(FATAL_ERROR "Symlinked boundary subtree bypassed policy\n${subtree_output}\n${subtree_errors}")
endif()
