cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK_ROOT}")
set(source "${WORK_ROOT}/source")
file(MAKE_DIRECTORY "${source}")
file(COPY "${REPO_ROOT}/CMakeLists.txt" "${REPO_ROOT}/LICENSE"
  "${REPO_ROOT}/src" "${REPO_ROOT}/include" "${REPO_ROOT}/cmake"
  DESTINATION "${source}")

function(configure_version build)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
      "-DCMAKE_C_COMPILER=${C_COMPILER}" -DCMAKE_BUILD_TYPE=Release
      -DBUILD_TESTING=OFF -DSRC_LINT_INSTALL_GIT_HOOKS=OFF ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Version configuration failed\n${output}\n${errors}")
  endif()
endfunction()

function(check_binary_version build version)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release --target src-lint
      --clean-first --parallel 2
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Version build failed\n${output}\n${errors}")
  endif()
  execute_process(
    COMMAND bash "${REPO_ROOT}/scripts/release.sh" verify-version "${build}/src-lint" "v${version}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Release version gate failed\n${output}\n${errors}")
  endif()
endfunction()

set(build "${WORK_ROOT}/release-build")
configure_version("${build}" -DSRC_LINT_VERSION=0.1.1)
check_binary_version("${build}" 0.1.1)
configure_version("${build}" -DSRC_LINT_VERSION=0.1.2)
check_binary_version("${build}" 0.1.2)

file(WRITE "${source}/VERSION" "0.1.1\n")
set(archive_build "${WORK_ROOT}/archive-build")
configure_version("${archive_build}")
check_binary_version("${archive_build}" 0.1.1)
file(WRITE "${source}/VERSION" "0.1.2\n")
configure_version("${archive_build}")
check_binary_version("${archive_build}" 0.1.2)

configure_version("${archive_build}" -DSRC_LINT_VERSION=0.2.0)
check_binary_version("${archive_build}" 0.2.0)
file(REMOVE "${source}/VERSION")
configure_version("${archive_build}" -USRC_LINT_VERSION)
check_binary_version("${archive_build}" 0.0.0)

foreach(version "" v0.1.1 01.1.1 0.01.1 0.1.01 0.1 0.1.1.1 0.1.1-rc1 "0.1.1\ninvalid")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DSRC_LINT_VERSION=${version}"
      -P "${source}/cmake/version.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(result EQUAL 0 OR NOT errors MATCHES "SRC_LINT_VERSION must be")
    message(FATAL_ERROR "Invalid version was not rejected: '${version}'\n${output}\n${errors}")
  endif()
endforeach()

file(WRITE "${source}/VERSION" "0.1.1\n0.1.2\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${source}/cmake/version.cmake"
  RESULT_VARIABLE result ERROR_VARIABLE errors)
if(result EQUAL 0 OR NOT errors MATCHES "SRC_LINT_VERSION must be")
  message(FATAL_ERROR "Invalid archive metadata was accepted")
endif()
