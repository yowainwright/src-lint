cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}")
set(FIXTURE "${WORK_ROOT}")

function(check_config content expected_exit expected_name)
  file(WRITE "${WORK_ROOT}/.src-lintrc.${FORMAT}" "${content}")
  if(expected_exit EQUAL 2)
    set(EXPECTED_ERROR "invalid ${expected_name} configuration")
    include("${CMAKE_CURRENT_LIST_DIR}/invalid_config.cmake")
    return()
  endif()
  set(EXPECTED_EXIT "${expected_exit}")
  set(EXPECTED "${EXPECTED_ROOT}/${expected_name}.json")
  include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")
endfunction()

if(FORMAT STREQUAL "json")
  foreach(escape IN ITEMS [[\]] [[\u]] [[\u0]] [[\u00]] [[\u002]] [[\u002f]]
      [[\uD800]] [[\uD800\]] [[\uD800\u]] [[\uD800\uD]] [[\uD800\uDC]]
      [[\uD800\uDC0]] [[\uD800\uDC00]] [[\uDC00]] [[\u0000]] [[\uXXXX]])
    check_config("{\"boundaries\":{\"billing\":{\"root\":\"${escape}" 2 JSON)
  endforeach()
  return()
endif()

foreach(slash IN ITEMS [[\/]] [[\x2f]] [[\u002f]] [[\U0000002f]])
  set(root "boundaries:\n  billing:\n    root: \"services${slash}billing\"\n")
  foreach(public IN ITEMS "[\"api${slash}**\"]" "\n      - \"api${slash}**\"")
    check_config("${root}    public: ${public}\n" 1 service_boundary)
  endforeach()
  set(orders "  orders:\n    root: services/orders\n")
  foreach(allow IN ITEMS "[\"services${slash}billing/**\"]"
      "\n      - \"services${slash}billing/**\"")
    check_config("${root}${orders}    allow: ${allow}\n" 0 empty)
  endforeach()
endforeach()

file(WRITE "${WORK_ROOT}/services/bill'ing#1/api/it's,public.ts" "export {};\n")
file(WRITE "${WORK_ROOT}/services/orders/create.ts"
  "import \"../bill'ing#1/api/it's,public.ts\";\n")
foreach(public IN ITEMS "['api/it''s,public.ts']" "\n      - 'api/it''s,public.ts'")
  check_config("boundaries:\n  billing:\n    root: 'services/bill''ing#1'\n    public: ${public}\n"
    0 empty)
endforeach()

function(check_yaml_unicode escape)
  string(ASCII ${ARGN} decoded)
  string(REPEAT "${escape}" 8 escaped_root)
  string(REPEAT "${decoded}" 8 expected_root)
  file(WRITE "${WORK_ROOT}/.src-lintrc.yaml"
    "boundaries:\n  billing:\n    root: \"${escaped_root}\"\n")
  file(WRITE "${WORK_ROOT}/${expected_root}/index.ts" "export {};\n")
  execute_process(COMMAND "${CLI}" discover "${WORK_ROOT}/${expected_root}" --format json
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  string(JSON root ERROR_VARIABLE json_error GET "${output}" boundaries 0 root)
  if(NOT result STREQUAL "0" OR json_error OR NOT root STREQUAL expected_root)
    message(FATAL_ERROR "YAML escape ${escape} did not round-trip\n${output}\n${errors}")
  endif()
endfunction()

check_yaml_unicode([[\N]] 194 133)
check_yaml_unicode([[\_]] 194 160)
check_yaml_unicode([[\L]] 226 128 168)
check_yaml_unicode([[\P]] 226 128 169)
check_yaml_unicode([[\U0001f600]] 240 159 152 128)

foreach(value IN ITEMS [["services/billing]] [['services/billing]]
    [["services/billing"junk]] [['services/bill'ing']]
    [["services\qbilling"]] [["services\x2"]] [["services\u002"]]
    [["services\U0000002"]] [["services\U00110000"]] [["services\uD800"]]
    [["services\uDC00"]] [["services\u0000"]] [["services\0"]])
  check_config("boundaries:\n  billing:\n    root: ${value}\n" 2 YAML)
endforeach()
