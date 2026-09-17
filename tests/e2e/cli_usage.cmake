execute_process(
  COMMAND "${CLI}" --help
  RESULT_VARIABLE help_result
  OUTPUT_VARIABLE help_output
)

set(commands "check" "discover" "graph")
foreach(command IN LISTS commands)
  string(FIND "${help_output}" "src-lint ${command}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Help is missing the ${command} command")
  endif()
endforeach()

string(FIND "${help_output}" "--config file" config_position)
if(config_position EQUAL -1)
  message(FATAL_ERROR "Help is missing --config")
endif()
foreach(arguments "--config" "--config;--strict" "--config;a.json;--config;b.json")
  execute_process(COMMAND "${CLI}" check ${arguments}
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
  if(NOT result EQUAL 2)
    message(FATAL_ERROR "Invalid config arguments accepted: ${arguments}")
  endif()
endforeach()

execute_process(
  COMMAND "${CLI}" graph . --format text
  RESULT_VARIABLE graph_result
  OUTPUT_QUIET
  ERROR_QUIET
)
execute_process(
  COMMAND "${CLI}" discover . --strict
  RESULT_VARIABLE discover_result
  OUTPUT_QUIET
  ERROR_QUIET
)

if(NOT help_result EQUAL 0 OR NOT graph_result EQUAL 2 OR NOT discover_result EQUAL 2)
  message(FATAL_ERROR "CLI argument contract changed")
endif()
