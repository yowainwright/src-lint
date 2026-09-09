cmake_minimum_required(VERSION 3.20)

project(src_lint VERSION 0.1.0 LANGUAGES C)

if(NOT UNIX)
  message(FATAL_ERROR "The first import slice requires POSIX fts and realpath")
endif()

option(SRC_LINT_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(SRC_LINT_INSTALL_GIT_HOOKS "Install Git hooks during local configuration" ON)

if(SRC_LINT_INSTALL_GIT_HOOKS
    AND CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR
    AND EXISTS "${PROJECT_SOURCE_DIR}/.git"
    AND NOT "$ENV{CI}" AND NOT "$ENV{GITHUB_ACTIONS}")
  execute_process(
    COMMAND "${PROJECT_SOURCE_DIR}/scripts/setup.sh"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE hook_setup_result)
  if(NOT hook_setup_result STREQUAL "0")
    message(FATAL_ERROR
      "Git hook setup failed. Resolve the reported conflict or configure with "
      "-DSRC_LINT_INSTALL_GIT_HOOKS=OFF to use your existing hook setup.")
  endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(src_lint_core
  src/cache.c
  src/check.c
  src/config.c
  src/graph.c
  src/graph_html.c
  src/imports.c
)
target_include_directories(src_lint_core PUBLIC include)
target_compile_definitions(src_lint_core PRIVATE
  _DEFAULT_SOURCE _XOPEN_SOURCE=700 SRC_LINT_VERSION="${PROJECT_VERSION}")
if(APPLE)
  target_compile_definitions(src_lint_core PRIVATE _DARWIN_C_SOURCE)
endif()

add_executable(src-lint src/main.c)
target_link_libraries(src-lint PRIVATE src_lint_core)
target_compile_definitions(src-lint PRIVATE SRC_LINT_VERSION="${PROJECT_VERSION}")

target_compile_options(src_lint_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_options(src-lint PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(SRC_LINT_SANITIZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Sanitizers require Clang or GCC")
  endif()
  set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_compile_options(src_lint_core PRIVATE ${SANITIZER_FLAGS})
  target_compile_options(src-lint PRIVATE ${SANITIZER_FLAGS})
  target_link_options(src_lint_core INTERFACE -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
