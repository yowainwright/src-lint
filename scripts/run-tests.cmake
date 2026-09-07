cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
set(BUILD_TYPE Debug)
set(SANITIZERS OFF)
if(SUITE STREQUAL "release")
  set(BUILD_TYPE Release)
elseif(SUITE STREQUAL "sanitizers")
  set(SANITIZERS ON)
elseif(NOT SUITE STREQUAL "debug")
  message(FATAL_ERROR "Expected SUITE=debug, release, or sanitizers")
endif()
set(BUILD_DIR "${REPO_ROOT}/build-hooks-${SUITE}")

function(reset_moved_cache)
  if(NOT EXISTS "${BUILD_DIR}/CMakeCache.txt")
    return()
  endif()
  load_cache("${BUILD_DIR}" READ_WITH_PREFIX previous_
    CMAKE_HOME_DIRECTORY CMAKE_CACHEFILE_DIR)
  if(previous_CMAKE_HOME_DIRECTORY STREQUAL REPO_ROOT AND
     previous_CMAKE_CACHEFILE_DIR STREQUAL BUILD_DIR)
    return()
  endif()
  message(STATUS "Refreshing ${SUITE} build metadata after checkout relocation")
  file(REMOVE "${BUILD_DIR}/CMakeCache.txt")
  file(REMOVE_RECURSE "${BUILD_DIR}/CMakeFiles")
endfunction()

reset_moved_cache()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${REPO_ROOT}" -B "${BUILD_DIR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" "-DSRC_LINT_SANITIZERS=${SANITIZERS}"
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --parallel
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD_DIR}" --output-on-failure
  COMMAND_ERROR_IS_FATAL ANY
)
