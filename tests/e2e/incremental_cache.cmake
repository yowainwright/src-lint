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

file(GLOB first_records "${WORK_ROOT}/.src-lint/cache/*.slc")
list(LENGTH first_records first_count)
if(first_count LESS 1)
  message(FATAL_ERROR "Expected cache records after initial check")
endif()
if(NOT EXISTS "${WORK_ROOT}/.src-lint/cache/.size")
  message(FATAL_ERROR "Expected cache size metadata after initial check")
endif()
file(READ "${WORK_ROOT}/.src-lint/cache/.size" first_tracked_size)

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE warm_result
  ERROR_VARIABLE warm_errors
)

file(GLOB warm_records "${WORK_ROOT}/.src-lint/cache/*.slc")
list(LENGTH warm_records warm_count)
file(READ "${WORK_ROOT}/.src-lint/cache/.size" warm_tracked_size)
if(NOT warm_result EQUAL 0 OR NOT warm_count EQUAL first_count)
  message(FATAL_ERROR "Warm check did not reuse cache\n${warm_errors}")
endif()
if(NOT warm_tracked_size STREQUAL first_tracked_size)
  message(FATAL_ERROR "Warm cache changed tracked bytes")
endif()

file(APPEND "${WORK_ROOT}/services/orders/create.ts" "\n")
execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE changed_result
  ERROR_VARIABLE changed_errors
)

file(GLOB changed_records "${WORK_ROOT}/.src-lint/cache/*.slc")
list(LENGTH changed_records changed_count)
if(NOT changed_result EQUAL 0 OR changed_count LESS_EQUAL warm_count)
  message(FATAL_ERROR "Source change did not produce a new cache key\n${changed_errors}")
endif()

string(REPEAT "x" 700000 oversized_record)
file(WRITE "${WORK_ROOT}/.src-lint/cache/oversized-a.slc" "${oversized_record}")
file(WRITE "${WORK_ROOT}/.src-lint/cache/oversized-b.slc" "${oversized_record}")
file(WRITE "${WORK_ROOT}/.src-lint/cache/.dirty" "")

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE trimmed_result
  ERROR_VARIABLE trimmed_errors
)

if(NOT trimmed_result EQUAL 0)
  message(FATAL_ERROR "Cache trim check failed\n${trimmed_errors}")
endif()

file(GLOB changed_records "${WORK_ROOT}/.src-lint/cache/*.slc")
set(total_size 0)
foreach(record IN LISTS changed_records)
  file(SIZE "${record}" record_size)
  math(EXPR total_size "${total_size} + ${record_size}")
endforeach()

if(total_size GREATER 1048576)
  message(FATAL_ERROR "Cache exceeded configured 1 MiB limit: ${total_size}")
endif()

if(EXISTS "${WORK_ROOT}/.src-lint/cache/.dirty")
  message(FATAL_ERROR "Cache transaction marker remained after a successful check")
endif()

set(protected_file "${WORK_ROOT}/protected.txt")
set(dirty_marker "${WORK_ROOT}/.src-lint/cache/.dirty")
file(WRITE "${protected_file}" "preserve me")
file(CREATE_LINK "${protected_file}" "${dirty_marker}" SYMBOLIC RESULT link_result)
if(NOT link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create cache-marker symlink: ${link_result}")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE symlink_result
  ERROR_VARIABLE symlink_errors
)

file(READ "${protected_file}" protected_content)
if(NOT symlink_result EQUAL 0)
  message(FATAL_ERROR "Check failed with a symlinked cache marker\n${symlink_errors}")
endif()
if(NOT protected_content STREQUAL "preserve me")
  message(FATAL_ERROR "Symlinked cache marker modified its target")
endif()

file(REMOVE "${dirty_marker}")
set(missing_target "${WORK_ROOT}/missing-target.txt")
file(CREATE_LINK "${missing_target}" "${dirty_marker}" SYMBOLIC RESULT dangling_link_result)
if(NOT dangling_link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create dangling cache-marker symlink: ${dangling_link_result}")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE dangling_result
  ERROR_VARIABLE dangling_errors
)

if(NOT dangling_result EQUAL 0)
  message(FATAL_ERROR "Check failed with a dangling cache-marker symlink\n${dangling_errors}")
endif()
if(EXISTS "${missing_target}")
  message(FATAL_ERROR "Symlinked cache marker created its target")
endif()

file(REMOVE_RECURSE "${WORK_ROOT}/.src-lint")
set(external_parent "${WORK_ROOT}-external-parent")
file(REMOVE_RECURSE "${external_parent}")
file(MAKE_DIRECTORY "${external_parent}")
file(CREATE_LINK "${external_parent}" "${WORK_ROOT}/.src-lint" SYMBOLIC
     RESULT parent_link_result)
if(NOT parent_link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create cache-parent symlink")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE parent_result
  ERROR_VARIABLE parent_errors
)

if(NOT parent_result EQUAL 0)
  message(FATAL_ERROR "Check failed with symlinked cache parent\n${parent_errors}")
endif()
if(EXISTS "${external_parent}/cache")
  message(FATAL_ERROR "Cache wrote through symlinked parent directory")
endif()

file(REMOVE "${WORK_ROOT}/.src-lint")
file(MAKE_DIRECTORY "${WORK_ROOT}/.src-lint")
set(external_cache "${WORK_ROOT}-external-cache")
file(REMOVE_RECURSE "${external_cache}")
file(MAKE_DIRECTORY "${external_cache}")
file(CREATE_LINK "${external_cache}" "${WORK_ROOT}/.src-lint/cache" SYMBOLIC
     RESULT cache_link_result)
if(NOT cache_link_result STREQUAL "0")
  message(FATAL_ERROR "Could not create cache-directory symlink")
endif()

execute_process(
  COMMAND "${CLI}" check "${WORK_ROOT}" --format json
  RESULT_VARIABLE cache_result
  ERROR_VARIABLE cache_errors
)

if(NOT cache_result EQUAL 0)
  message(FATAL_ERROR "Check failed with symlinked cache directory\n${cache_errors}")
endif()
file(GLOB external_records "${external_cache}/*.slc")
if(EXISTS "${external_cache}/.lock" OR external_records)
  message(FATAL_ERROR "Cache wrote through symlinked cache directory")
endif()
