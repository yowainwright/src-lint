file(REMOVE_RECURSE "${WORK_ROOT}")
file(COPY "${SOURCE_FIXTURE}/" DESTINATION "${WORK_ROOT}")

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE first_result
  ERROR_VARIABLE first_errors
)

if(NOT first_result EQUAL 0)
  message(FATAL_ERROR "Initial cache check failed\n${first_errors}")
endif()

file(GLOB first_records "${WORK_ROOT}/.tree-legibility/cache/*.tlc")
list(LENGTH first_records first_count)
if(first_count LESS 1)
  message(FATAL_ERROR "Expected cache records after initial check")
endif()
if(NOT EXISTS "${WORK_ROOT}/.tree-legibility/cache/.size")
  message(FATAL_ERROR "Expected cache size metadata after initial check")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE warm_result
  ERROR_VARIABLE warm_errors
)

file(GLOB warm_records "${WORK_ROOT}/.tree-legibility/cache/*.tlc")
list(LENGTH warm_records warm_count)
if(NOT warm_result EQUAL 0 OR NOT warm_count EQUAL first_count)
  message(FATAL_ERROR "Warm check did not reuse cache\n${warm_errors}")
endif()

file(APPEND "${WORK_ROOT}/services/orders/create.ts" "\n")
execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE changed_result
  ERROR_VARIABLE changed_errors
)

file(GLOB changed_records "${WORK_ROOT}/.tree-legibility/cache/*.tlc")
list(LENGTH changed_records changed_count)
if(NOT changed_result EQUAL 0 OR changed_count LESS_EQUAL warm_count)
  message(FATAL_ERROR "Source change did not produce a new cache key\n${changed_errors}")
endif()

string(REPEAT "x" 700000 oversized_record)
file(WRITE "${WORK_ROOT}/.tree-legibility/cache/oversized-a.tlc" "${oversized_record}")
file(WRITE "${WORK_ROOT}/.tree-legibility/cache/oversized-b.tlc" "${oversized_record}")
file(WRITE "${WORK_ROOT}/.tree-legibility/cache/.dirty" "")

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE trimmed_result
  ERROR_VARIABLE trimmed_errors
)

if(NOT trimmed_result EQUAL 0)
  message(FATAL_ERROR "Cache trim check failed\n${trimmed_errors}")
endif()

file(GLOB changed_records "${WORK_ROOT}/.tree-legibility/cache/*.tlc")
set(total_size 0)
foreach(record IN LISTS changed_records)
  file(SIZE "${record}" record_size)
  math(EXPR total_size "${total_size} + ${record_size}")
endforeach()

if(total_size GREATER 1048576)
  message(FATAL_ERROR "Cache exceeded configured 1 MiB limit: ${total_size}")
endif()

if(EXISTS "${WORK_ROOT}/.tree-legibility/cache/.dirty")
  message(FATAL_ERROR "Cache transaction marker remained after a successful check")
endif()
