file(REMOVE_RECURSE "${WORK_ROOT}")
set(install_root "${WORK_ROOT}/prefix")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_ROOT}"
    --config "${BUILD_CONFIG}" --prefix "${install_root}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE errors
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Install failed\n${output}\n${errors}")
endif()
if(NOT EXISTS "${install_root}/${INSTALL_DATAROOTDIR}/doc/src-lint/LICENSE")
  message(FATAL_ERROR "Installed license is missing")
endif()

set(CLI "${install_root}/${INSTALL_BINDIR}/src-lint")
include("${CMAKE_CURRENT_LIST_DIR}/cli_version.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/finding_json.cmake")
