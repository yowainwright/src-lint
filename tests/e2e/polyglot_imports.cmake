cmake_minimum_required(VERSION 3.20)

function(run_language command expected)
  execute_process(COMMAND "${CLI}" "${command}" orders --root "${root}"
    --config "${root}/policy.json" --format json ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "${extension} ${command}: expected ${expected}, got ${result}\n${output}\n${errors}")
  endif()
  set(last_output "${output}" PARENT_SCOPE)
endfunction()

function(check_language extension language module private public missing target_extension)
  set(root "${WORK_ROOT}/${extension}")
  file(MAKE_DIRECTORY "${root}/orders" "${root}/billing/internal" "${root}/billing/api")
  file(WRITE "${root}/billing/internal/Ledger.${target_extension}" "")
  file(WRITE "${root}/billing/api/Ledger.${target_extension}" "")
  set(policy "{\"cache\":{\"max_mib\":1},\"boundaries\":{\"Orders\":{\"root\":\"orders\",\"allow\":[\"missing/**\"]},\"${module}\":{\"root\":\"billing\",\"public\":[\"api/**\"]},\"Missing\":{\"root\":\"missing\"}}}")
  file(WRITE "${root}/policy.json" "${policy}")
  file(WRITE "${root}/orders/main.${extension}" "${private}\n")
  foreach(command check discover graph)
    run_language("${command}" 1)
    if(NOT command STREQUAL "discover" AND NOT last_output MATCHES "SL1001")
      message(FATAL_ERROR "${extension} ${command} did not enforce the boundary: ${last_output}")
    endif()
  endforeach()
  string(JSON edge_language GET "${last_output}" edges 0 language)
  string(JSON edge_count LENGTH "${last_output}" edges)
  if(NOT edge_language STREQUAL language OR NOT edge_count EQUAL 1)
    message(FATAL_ERROR "${extension}: incorrect graph edge metadata: ${last_output}")
  endif()
  set(cold_graph "${last_output}")
  run_language(graph 1)
  if(NOT last_output STREQUAL cold_graph)
    message(FATAL_ERROR "${extension}: warm graph changed")
  endif()

  file(WRITE "${root}/orders/main.${extension}" "${public}\n")
  if(extension STREQUAL "swift")
    # Swift imports a module, so allow its root through the source policy.
    string(REPLACE "missing/**" "billing/**\",\"missing/**" policy "${policy}")
    file(WRITE "${root}/policy.json" "${policy}")
  endif()
  foreach(command check discover graph)
    run_language("${command}" 0)
  endforeach()
  string(JSON status GET "${last_output}" edges 0 status)
  if(NOT status STREQUAL "allowed")
    message(FATAL_ERROR "${extension}: public edge was not allowed: ${last_output}")
  endif()

  file(WRITE "${root}/orders/main.${extension}" "${missing}\n")
  run_language(check 0)
  if(NOT last_output MATCHES "SL2001")
    message(FATAL_ERROR "${extension}: missing target was not reported: ${last_output}")
  endif()
  run_language(check 1 --strict)
endfunction()

file(REMOVE_RECURSE "${WORK_ROOT}")
check_language(jl julia Billing
  "include(\"../billing/internal/Ledger.jl\")"
  "include(\"../billing/api/Ledger.jl\")"
  "include(\"../missing/Ledger.jl\")" jl)
check_language(rb ruby billing
  "require_relative '../billing/internal/Ledger'"
  "require_relative '../billing/api/Ledger'"
  "require_relative '../missing/Ledger'" rb)
check_language(swift swift Billing "import Billing" "import Billing" "import Missing" swift)
check_language(kt kotlin com.billing
  "import com.billing.internal.Ledger"
  "import com.billing.api.Ledger as Book"
  "import Missing.Ledger" kt)
check_language(kts kotlin com.billing
  "import com.billing.internal.Ledger"
  "import com.billing.api.Ledger"
  "import Missing.Ledger" kt)
check_language(java java com.billing
  "import static com.billing.internal.Ledger.write;"
  "import com.billing.api.Ledger;"
  "import Missing.Ledger;" java)
check_language(tf terraform billing
  "module \"ledger\" { source = \"../billing/internal\" }"
  "module \"ledger\" { source = \"../billing/api\" }"
  "module \"ledger\" { source = \"../missing\" }" tf)

# Named Julia imports use configured module names, not the policy's location.
set(extension jl)
set(root "${WORK_ROOT}/jl")
file(WRITE "${root}/orders/main.jl" "using Billing\n")
run_language(check 1)
foreach(source "using .Utils, Billing\n" "using Orders, .Utils, Billing\n")
  file(WRITE "${root}/orders/main.jl" "${source}")
  foreach(command check discover graph)
    run_language("${command}" 1)
  endforeach()
endforeach()

# Ruby load-path imports can use the existing configured boundary-root matching.
set(extension rb)
set(root "${WORK_ROOT}/rb")
file(WRITE "${root}/orders/main.rb" "require 'billing/internal/Ledger'\n")
run_language(check 1)
foreach(source "task { require_relative '../billing/internal/Ledger' }\n"
    "require 'billing/internal/Ledger' or abort\n"
    "puts <<A, <<B; require_relative '../billing/internal/Ledger'\nfirst\nA\nsecond\nB\n")
  file(WRITE "${root}/orders/main.rb" "${source}")
  foreach(command check discover graph)
    run_language("${command}" 1)
  endforeach()
endforeach()
file(WRITE "${root}/orders/main.rb"
  "puts <<A, <<B\nfirst\nA\nrequire_relative '../billing/internal/Ledger'\nB\n")
run_language(check 0 --strict)
run_language(graph 0)
string(JSON count LENGTH "${last_output}" edges)
if(NOT count EQUAL 0)
  message(FATAL_ERROR "Ruby heredoc text produced an import: ${last_output}")
endif()

# Ruby require './x' follows the process working directory, including on warm runs.
file(WRITE "${root}/orders/main.rb" "require './billing/api/Ledger'\n")
foreach(working_directory "${root}" "${root}/orders" "${root}")
  execute_process(COMMAND "${CLI}" check orders --root "${root}"
    --config "${root}/policy.json" --format json --strict
    WORKING_DIRECTORY "${working_directory}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
  if(working_directory STREQUAL root)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Ruby cwd import failed: ${output}\n${errors}")
    endif()
  elseif(NOT result EQUAL 1 OR NOT output MATCHES "SL2001")
    message(FATAL_ERROR "Ruby reused a cache entry from a different cwd: ${output}\n${errors}")
  endif()
endforeach()

# Wildcards retain a directory dependency rather than becoming filenames with '*'.
set(extension java)
set(root "${WORK_ROOT}/java")
file(WRITE "${root}/orders/main.java" "import com.billing.api.*;\n")
run_language(graph 0)
string(JSON target GET "${last_output}" edges 0 to)
if(NOT target STREQUAL "billing/api")
  message(FATAL_ERROR "Wildcard target must be the package directory: ${last_output}")
endif()

# Installed Terraform modules are not scanned as the user's source.
set(extension tf)
set(root "${WORK_ROOT}/tf")
file(WRITE "${root}/orders/main.tf" "module \"remote\" { source = \"vendor/module/cloud\" }\n")
file(MAKE_DIRECTORY "${root}/orders/.terraform/modules/fake")
file(WRITE "${root}/orders/.terraform/modules/fake/main.tf"
  "module \"fake\" { source = \"../../../../billing/internal\" }\n")
run_language(graph 0)
string(JSON count LENGTH "${last_output}" edges)
if(NOT count EQUAL 0)
  message(FATAL_ERROR "Remote or installed modules produced local edges: ${last_output}")
endif()
