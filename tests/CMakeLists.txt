set(FIXTURE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/service-boundary")

add_test(
  NAME service_boundary_violation
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME scoped_service_directory
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DSCOPE=${FIXTURE_ROOT}/services/orders
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME scoped_source_file
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DSCOPE=${FIXTURE_ROOT}/services/orders/create.ts
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME scoped_from_services_directory
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DSCOPE=orders
    -DWORKING_DIRECTORY=${FIXTURE_ROOT}/services
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME repository_path_containing_services
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/path-with-services/services/repo
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME scoped_repository_path_containing_services
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/path-with-services/services/repo
    -DSCOPE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/path-with-services/services/repo/services/orders
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_violation.cmake
)

add_test(
  NAME service_boundary_json
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/service_boundary.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME javascript_module_syntax
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/module-syntax
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/module_syntax.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME javascript_spacing
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/javascript-spacing
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/javascript_spacing.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME multiple_imports_per_line
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/multiple-imports
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/multiple_imports.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME ignores_block_comment
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/block-comment
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME ignores_import_text
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/import-text
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME import_with_comment
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/import-comment
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/import_comment.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME multiline_calls
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/multiline-calls
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/multiline_calls.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME nested_services_directory
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nested-services
    -DSCOPE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nested-services/services/orders/src/services/client.ts
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/nested_services.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME service_root_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/service-root-import
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/service_root_import.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME invalid_root
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_root.cmake
)
