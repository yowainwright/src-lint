set(FIXTURE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/service-boundary")

add_executable(api_validation api_validation.c)
target_link_libraries(api_validation PRIVATE tree_legibility_core)
target_compile_definitions(api_validation PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(api_validation PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME api_validation COMMAND api_validation ${FIXTURE_ROOT})

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(performance_test performance_test.c)
  target_compile_definitions(performance_test PRIVATE _POSIX_C_SOURCE=200809L)
  target_compile_options(performance_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(
    NAME performance_budgets
    COMMAND performance_test $<TARGET_FILE:tree-legibility>
            ${CMAKE_CURRENT_BINARY_DIR}/performance-work
  )
endif()

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
  NAME canonical_symlink_boundary
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DSOURCE_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/symlink-boundary
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/symlink-boundary-work
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/symlink_boundary.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/symlink_boundary.cmake
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
  NAME javascript_cooked_module_literals
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/javascript-cooked
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/javascript_cooked.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME ignores_javascript_regex_literals
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/javascript-regex
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/javascript_regex.json
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
  NAME extensionless_javascript_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/extensionless
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/extensionless.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME typescript_runtime_specifiers
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/typescript-runtime-specifiers
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -DEXPECTED_EXIT=0
    -DSTRICT=ON
    -P ${CMAKE_CURRENT_SOURCE_DIR}/finding_json.cmake
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
  NAME python_relative_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-relative
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/python_relative.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME python_absolute_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-absolute
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/python_absolute.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME python_direct_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-direct
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/python_direct.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME python_import_lists_and_strings
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-lexical
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/python_lexical.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME python_semicolon_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-semicolon
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/python_semicolon.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME go_single_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/go-single
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/go_single.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME go_import_block
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/go-block
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/go_block.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME proto_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/proto-import
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/proto_import.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME go_and_proto_block_comments
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/polyglot-block-comments
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME go_multiline_raw_string
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/go-raw-string
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME toml_boundary_config
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-toml
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_toml.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME toml_quoted_pattern_values
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-toml-strings
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME go_public_package_root
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-go-public-root
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME child_config_merge
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-child
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_child.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME json_boundary_config
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-json
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_toml.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME yaml_boundary_config
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-yaml
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_toml.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME yaml_single_quoted_hash
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-yaml-hash
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_yaml_hash.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME configured_absolute_polyglot_imports
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-absolute
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_absolute.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME configured_target_precedes_inference
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-target-only
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_target_only.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME json_escaped_policy_values
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-json-escaped
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/config_json_escaped.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME multiple_config_files
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-multiple
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME empty_unsigned_config_value
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-empty
    -DEXPECTED_ERROR=invalid TOML configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME signed_unsigned_config_value
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-signed
    -DEXPECTED_ERROR=invalid TOML configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME unsupported_config_pattern
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-pattern
    -DEXPECTED_ERROR=invalid merged boundary configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME invalid_json_escape
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-json
    -DEXPECTED_ERROR=invalid JSON configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME invalid_boundary_root
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-root
    -DEXPECTED_ERROR=invalid merged boundary configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME invalid_config_without_sources
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-no-source
    -DEXPECTED_ERROR=invalid TOML configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME oversized_boundary_name
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-invalid-name
    -DEXPECTED_ERROR=invalid merged boundary configuration
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_config.cmake
)

add_test(
  NAME config_stops_at_repository_root
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/config-root-work
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/unresolved_advisory.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/config_repository_root.cmake
)

add_test(
  NAME unresolved_import_advisory
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unresolved-import
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/unresolved_advisory.json
    -DEXPECTED_EXIT=0
    -P ${CMAKE_CURRENT_SOURCE_DIR}/finding_json.cmake
)

add_test(
  NAME missing_cross_service_internal
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/missing-cross-service
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/missing_cross_service.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME normalized_nonrelative_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nonrelative-traversal
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/nonrelative_traversal.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/service_boundary_json.cmake
)

add_test(
  NAME unresolved_import_strict
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unresolved-import
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/unresolved_strict.json
    -DEXPECTED_EXIT=1
    -DSTRICT=ON
    -P ${CMAKE_CURRENT_SOURCE_DIR}/finding_json.cmake
)

add_test(
  NAME config_strict_import
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-strict
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/unresolved_strict.json
    -DEXPECTED_EXIT=1
    -P ${CMAKE_CURRENT_SOURCE_DIR}/finding_json.cmake
)

add_test(
  NAME configured_allow
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-allow
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/empty.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/clean_json.cmake
)

add_test(
  NAME incremental_cache
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DSOURCE_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cache
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cache-work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/incremental_cache.cmake
)

add_test(
  NAME graph_json
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/graph.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_json.cmake
)

add_test(
  NAME graph_repository_path_containing_services
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/path-with-services/services/repo
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/graph.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_json.cmake
)

add_test(
  NAME discover_json
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/discover.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/discover_json.cmake
)

add_test(
  NAME discover_repository_path_containing_services
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/path-with-services/services/repo
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/discover.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/discover_json.cmake
)

add_test(
  NAME discover_text
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/discover_text.cmake
)

add_test(
  NAME discover_configured_json
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/config-toml
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected/discover_configured.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/discover_json.cmake
)

add_test(
  NAME graph_html
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${FIXTURE_ROOT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_html.cmake
)

add_test(
  NAME graph_python
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/python-relative
    -DLANGUAGE=python
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_language.cmake
)

add_test(
  NAME graph_go
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/go-single
    -DLANGUAGE=go
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_language.cmake
)

add_test(
  NAME graph_proto
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/proto-import
    -DLANGUAGE=proto
    -P ${CMAKE_CURRENT_SOURCE_DIR}/graph_language.cmake
)

add_test(
  NAME cli_usage
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_usage.cmake
)

add_test(
  NAME invalid_root
  COMMAND
    ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:tree-legibility>
    -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/invalid_root.cmake
)
