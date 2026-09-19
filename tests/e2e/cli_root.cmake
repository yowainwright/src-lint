cmake_minimum_required(VERSION 3.20)

function(run_root command expected)
  execute_process(COMMAND "${CLI}" "${command}" --format json ${ARGN}
    WORKING_DIRECTORY "${WORK_ROOT}/tools"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "${command} ${ARGN}: expected ${expected}, got ${result}\n${output}\n${errors}")
  endif()
  if(expected EQUAL 1 AND NOT output MATCHES "orders")
    message(FATAL_ERROR "Root policy was not reflected in ${command}: ${output}")
  endif()
  if(expected EQUAL 2 AND errors STREQUAL "")
    message(FATAL_ERROR "Root failure must report a diagnostic")
  endif()
  set(last_output "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/tools" "${WORK_ROOT}/app/domains/orders"
  "${WORK_ROOT}/app/domains/billing" "${WORK_ROOT}/app/policies" "${WORK_ROOT}/empty"
  "${WORK_ROOT}/app-other")
file(WRITE "${WORK_ROOT}/app/domains/orders/main.ts" "import '../billing/private.ts';\n")
file(WRITE "${WORK_ROOT}/app/domains/billing/private.ts" "export const value = 1;\n")
set(policy [[{"cache":{"max_mib":1},"boundaries":{"orders":{"root":"domains/orders"},"billing":{"root":"domains/billing"}}}]])
file(WRITE "${WORK_ROOT}/tools/policy.json" "${policy}")
file(WRITE "${WORK_ROOT}/app/policies/policy.json" "${policy}")
file(WRITE "${WORK_ROOT}/.src-lintrc" "invalid parent policy")
file(CREATE_LINK "${WORK_ROOT}/app" "${WORK_ROOT}/app-link" SYMBOLIC)

foreach(command check discover graph)
  run_root("${command}" 1 --root ../app --config policy.json)
  run_root("${command}" 1 --config policy.json --root "${WORK_ROOT}/app" domains/orders)
  run_root("${command}" 1 domains/orders/main.ts --root ../app --config policy.json)
  run_root("${command}" 1 --root ../app-link --config policy.json)
  run_root("${command}" 1 "${WORK_ROOT}/app/domains/orders/main.ts"
    --config "${WORK_ROOT}/tools/policy.json" --root ../app)
endforeach()
run_root(check 1 --root ../app --config policy.json domains/orders/main.ts)
string(JSON source GET "${last_output}" findings 0 source)
if(NOT source STREQUAL "domains/orders/main.ts")
  message(FATAL_ERROR "Diagnostics must be relative to --root: ${source}")
endif()
if(NOT IS_DIRECTORY "${WORK_ROOT}/app/.src-lint/cache" OR EXISTS "${WORK_ROOT}/tools/.src-lint")
  message(FATAL_ERROR "Cache must belong to --root, not the external config directory")
endif()

# Source overrides survive loading the target's policy, including an internal config location.
set(public_policy [[{"boundaries":{"billing":{"public":["private.ts"]}}}]])
file(WRITE "${WORK_ROOT}/app/domains/orders/.src-lintrc" "${public_policy}")
run_root(check 0 --root ../app --config policy.json)
run_root(check 0 --root ../app --config ../app/policies/policy.json)
file(REMOVE "${WORK_ROOT}/app/domains/orders/.src-lintrc")
file(WRITE "${WORK_ROOT}/app/domains/billing/.src-lintrc" "${public_policy}")
run_root(check 0 --root ../app --config policy.json domains/orders/main.ts)
file(REMOVE "${WORK_ROOT}/app/domains/billing/.src-lintrc")

# Selecting a policy does not discover unrelated configs beside that policy.
file(WRITE "${WORK_ROOT}/app/policies/.src-lintrc" "invalid unrelated policy")
run_root(check 1 --root ../app --config ../app/policies/policy.json domains/orders/main.ts)
file(REMOVE "${WORK_ROOT}/app/policies/.src-lintrc")

# Automatic discovery stops at --root, even without a Git marker.
file(WRITE "${WORK_ROOT}/app/.src-lintrc" "${policy}")
foreach(command check discover graph)
  run_root("${command}" 1 --root ../app)
  run_root("${command}" 0 --root ../empty)
  run_root("${command}" 2 --root ../missing --config policy.json)
  run_root("${command}" 2 --root policy.json --config policy.json)
  run_root("${command}" 2 ../tools --root ../app --config policy.json)
  run_root("${command}" 2 "${WORK_ROOT}/app-other" --root ../app --config policy.json)
endforeach()

execute_process(COMMAND "${CLI}" check --root ""
  RESULT_VARIABLE empty_result OUTPUT_QUIET ERROR_QUIET)
if(NOT empty_result EQUAL 2)
  message(FATAL_ERROR "An empty --root must be rejected")
endif()
file(WRITE "${WORK_ROOT}/tools/bad.json" "{")
run_root(check 2 --root ../empty --config bad.json)
run_root(check 2 --root ../empty --config missing.json)
