cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
file(WRITE "${WORK_ROOT}/LICENSE" "Parent project license\n")
set(parent_project [[
cmake_minimum_required(VERSION 3.20)
project(parent LANGUAGES C)
include(GNUInstallDirs)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("@REPO_ROOT@" src-lint)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
]])
string(CONFIGURE "${parent_project}" parent_project @ONLY)
file(WRITE "${WORK_ROOT}/CMakeLists.txt" "${parent_project}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${WORK_ROOT}" -B "${WORK_ROOT}/build"
    "-DCMAKE_C_COMPILER=${C_COMPILER}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Parent configuration failed\n${output}\n${errors}")
endif()

set(install_root "${WORK_ROOT}/prefix")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${WORK_ROOT}/build" --prefix "${install_root}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Parent installation failed\n${output}\n${errors}")
endif()

file(GLOB_RECURSE installed_files RELATIVE "${install_root}" "${install_root}/*")
if(NOT installed_files STREQUAL "share/doc/parent/LICENSE")
  message(FATAL_ERROR "Unexpected parent installation: ${installed_files}")
endif()
file(READ "${install_root}/share/doc/parent/LICENSE" license)
if(NOT license STREQUAL "Parent project license\n")
  message(FATAL_ERROR "Parent license was overwritten")
endif()
