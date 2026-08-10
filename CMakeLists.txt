cmake_minimum_required(VERSION 3.20)

project(tree_legibility VERSION 0.1.0 LANGUAGES C)

if(NOT UNIX)
  message(FATAL_ERROR "The first import slice requires POSIX fts and realpath")
endif()

option(TREE_LEGIBILITY_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(tree_legibility_core src/cache.c src/check.c src/config.c src/graph.c src/graph_html.c
                                 src/imports.c)
target_include_directories(tree_legibility_core PUBLIC include)
target_compile_definitions(tree_legibility_core PRIVATE _DEFAULT_SOURCE _XOPEN_SOURCE=700)
if(APPLE)
  target_compile_definitions(tree_legibility_core PRIVATE _DARWIN_C_SOURCE)
endif()

add_executable(tree-legibility src/main.c)
target_link_libraries(tree-legibility PRIVATE tree_legibility_core)

target_compile_options(tree_legibility_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_options(tree-legibility PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(TREE_LEGIBILITY_SANITIZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Sanitizers require Clang or GCC")
  endif()
  set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_compile_options(tree_legibility_core PRIVATE ${SANITIZER_FLAGS})
  target_compile_options(tree-legibility PRIVATE ${SANITIZER_FLAGS})
  target_link_options(tree_legibility_core INTERFACE -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
