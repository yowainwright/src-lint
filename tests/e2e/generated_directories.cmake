cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/.git" "${WORK_ROOT}/src")
file(WRITE "${WORK_ROOT}/src/index.ts" "export const source = true;\n")

set(ignored_directories .git build node_modules .src-lint dist .next coverage out .turbo
  build-debug cmake-build-debug)
foreach(directory IN LISTS ignored_directories)
  foreach(parent "${WORK_ROOT}" "${WORK_ROOT}/src")
    file(WRITE "${parent}/${directory}/generated.ts" "import \"./missing.ts\";\n")
  endforeach()
endforeach()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --strict --format json
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Generated directories were scanned\n${output}\n${errors}")
endif()

string(JSON count LENGTH "${output}" findings)
if(NOT count EQUAL 0)
  message(FATAL_ERROR "Unexpected findings from generated directories\n${output}")
endif()

foreach(scope src dist src/.next)
  file(WRITE "${WORK_ROOT}/${scope}/generated.ts" "import \"./missing.ts\";\n")
  execute_process(COMMAND "${CLI}" check "${WORK_ROOT}/${scope}" --strict --format json
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 1)
    message(FATAL_ERROR "Explicit scan of ${scope} missed a source finding\n${output}\n${errors}")
  endif()
  string(JSON count LENGTH "${output}" findings)
  string(JSON rule GET "${output}" findings 0 rule)
  if(NOT count EQUAL 1 OR NOT rule STREQUAL "SL2001")
    message(FATAL_ERROR "Unexpected findings for ${scope}\n${output}")
  endif()
endforeach()
