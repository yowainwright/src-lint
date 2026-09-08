set(FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(EXPECTED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/expected")
set(FIXTURE_ROOT "${FIXTURES}/service-boundary")
set(NESTED_ROOT "${FIXTURES}/path-with-services/services/repo")

function(add_cli_test name script)
  add_test(NAME "${name}" COMMAND "${CMAKE_COMMAND}"
    "-DCLI=$<TARGET_FILE:src-lint>"
    ${ARGN}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/e2e/${script}.cmake")
  set_tests_properties("${name}" PROPERTIES LABELS e2e)
endfunction()

function(add_json_test name fixture expected)
  if(NOT IS_ABSOLUTE "${fixture}")
    set(fixture "${FIXTURES}/${fixture}")
  endif()
  add_cli_test("${name}" finding_json
    "-DFIXTURE=${fixture}"
    "-DEXPECTED=${EXPECTED_ROOT}/${expected}.json"
    ${ARGN})
endfunction()

add_executable(api_validation integration/api_validation.c)
target_link_libraries(api_validation PRIVATE src_lint_core)
target_compile_definitions(api_validation PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(api_validation PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME api_validation COMMAND api_validation "${FIXTURE_ROOT}")
set_tests_properties(api_validation PROPERTIES LABELS integration)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(performance_test integration/performance_test.c)
  target_compile_definitions(performance_test PRIVATE _POSIX_C_SOURCE=200809L)
  target_compile_options(performance_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME performance_budgets COMMAND performance_test
    $<TARGET_FILE:src-lint> "${CMAKE_CURRENT_BINARY_DIR}/performance-work")
  set_tests_properties(performance_budgets PROPERTIES LABELS "integration;performance")
endif()

add_cli_test(service_boundary_violation service_boundary_violation
  "-DFIXTURE=${FIXTURE_ROOT}")
add_cli_test(scoped_service_directory service_boundary_violation
  "-DFIXTURE=${FIXTURE_ROOT}" "-DSCOPE=${FIXTURE_ROOT}/services/orders")
add_cli_test(scoped_source_file service_boundary_violation
  "-DFIXTURE=${FIXTURE_ROOT}" "-DSCOPE=${FIXTURE_ROOT}/services/orders/create.ts")
add_cli_test(scoped_from_services_directory service_boundary_violation
  "-DFIXTURE=${FIXTURE_ROOT}" -DSCOPE=orders
  "-DWORKING_DIRECTORY=${FIXTURE_ROOT}/services")
add_cli_test(repository_path_containing_services service_boundary_violation
  "-DFIXTURE=${NESTED_ROOT}")
add_cli_test(scoped_repository_path_containing_services service_boundary_violation
  "-DFIXTURE=${NESTED_ROOT}" "-DSCOPE=${NESTED_ROOT}/services/orders")
add_json_test(service_boundary_json service-boundary service_boundary)

add_cli_test(canonical_symlink_boundary symlink_boundary
  "-DSOURCE_FIXTURE=${FIXTURES}/symlink-boundary"
  "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/symlink-boundary-work"
  "-DEXPECTED=${EXPECTED_ROOT}/symlink_boundary.json")

add_json_test(javascript_module_syntax module-syntax module_syntax)
add_json_test(javascript_cooked_module_literals javascript-cooked javascript_cooked)
add_json_test(ignores_javascript_regex_literals javascript-regex javascript_regex)
add_json_test(javascript_spacing javascript-spacing javascript_spacing)
add_json_test(extensionless_javascript_import extensionless service_boundary)
add_json_test(typescript_runtime_specifiers typescript-runtime-specifiers empty
  -DEXPECTED_EXIT=0 -DSTRICT=ON)
add_json_test(multiple_imports_per_line multiple-imports multiple_imports)
add_json_test(ignores_block_comment block-comment empty -DEXPECTED_EXIT=0)
add_json_test(ignores_import_text import-text empty -DEXPECTED_EXIT=0)
add_json_test(import_with_comment import-comment import_comment)
add_json_test(multiline_calls multiline-calls multiline_calls)
add_json_test(nested_services_directory nested-services nested_services
  "-DSCOPE=${FIXTURES}/nested-services/services/orders/src/services/client.ts")
add_json_test(service_root_import service-root-import service_root_import)

add_json_test(python_relative_import python-relative python_relative)
add_json_test(python_absolute_import python-absolute python_relative)
add_json_test(python_direct_import python-direct python_direct)
add_json_test(python_import_lists_and_strings python-lexical python_lexical)
add_json_test(python_semicolon_import python-semicolon python_semicolon)
add_json_test(go_single_import go-single go_single)
add_json_test(go_import_block go-block go_block)
add_json_test(proto_import proto-import proto_import)
add_json_test(go_and_proto_block_comments polyglot-block-comments empty -DEXPECTED_EXIT=0)
add_json_test(go_multiline_raw_string go-raw-string empty -DEXPECTED_EXIT=0)

# Each config format gets an isolated copy of the same source tree.
set(CONFIG_SOURCES
  domains/orders/create.ts
  domains/billing/api/index.ts
  domains/billing/private/ledger.ts)
foreach(format toml json yaml)
  set(config_fixture "${CMAKE_CURRENT_BINARY_DIR}/config-${format}")
  foreach(source IN LISTS CONFIG_SOURCES)
    configure_file("${FIXTURES}/config-toml/${source}"
      "${config_fixture}/${source}" COPYONLY)
  endforeach()
  configure_file("${FIXTURES}/config-${format}/.src-lintrc.${format}"
    "${config_fixture}/.src-lintrc.${format}" COPYONLY)
  add_json_test("${format}_boundary_config" "${config_fixture}" config_toml)
endforeach()

add_json_test(toml_quoted_pattern_values config-toml-strings empty -DEXPECTED_EXIT=0)
add_json_test(go_public_package_root config-go-public-root empty -DEXPECTED_EXIT=0)
add_json_test(child_config_merge config-child config_child)
add_json_test(yaml_single_quoted_hash config-yaml-hash config_yaml_hash)
add_json_test(configured_absolute_polyglot_imports config-absolute config_absolute)
add_json_test(configured_target_precedes_inference config-target-only config_target_only)
add_json_test(json_escaped_policy_values config-json-escaped service_boundary)
foreach(format json yaml)
  add_cli_test("${format}_config_escapes" config_escapes
    "-DFORMAT=${format}"
    "-DSOURCE_FIXTURE=${FIXTURE_ROOT}"
    "-DEXPECTED_ROOT=${EXPECTED_ROOT}"
    "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/${format}-escapes-work")
endforeach()

add_cli_test(multiple_config_files invalid_config
  "-DFIXTURE=${FIXTURES}/config-multiple")
add_cli_test(empty_unsigned_config_value invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-empty"
  "-DEXPECTED_ERROR=invalid TOML configuration")
add_cli_test(signed_unsigned_config_value invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-signed"
  "-DEXPECTED_ERROR=invalid TOML configuration")
add_cli_test(unsupported_config_pattern invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-pattern"
  "-DEXPECTED_ERROR=invalid merged boundary configuration")
add_cli_test(invalid_json_escape invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-json"
  "-DEXPECTED_ERROR=invalid JSON configuration")
add_cli_test(invalid_boundary_root invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-root"
  "-DEXPECTED_ERROR=invalid merged boundary configuration")
add_cli_test(invalid_config_without_sources invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-no-source"
  "-DEXPECTED_ERROR=invalid TOML configuration")
add_cli_test(oversized_boundary_name invalid_config
  "-DFIXTURE=${FIXTURES}/config-invalid-name"
  "-DEXPECTED_ERROR=invalid merged boundary configuration")
add_cli_test(config_stops_at_repository_root config_repository_root
  "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/config-root-work"
  "-DEXPECTED=${EXPECTED_ROOT}/unresolved_advisory.json")

add_json_test(unresolved_import_advisory unresolved-import unresolved_advisory
  -DEXPECTED_EXIT=0)
add_json_test(missing_cross_service_internal missing-cross-service missing_cross_service)
add_json_test(normalized_nonrelative_import nonrelative-traversal nonrelative_traversal)
add_json_test(unresolved_import_strict unresolved-import unresolved_strict
  -DSTRICT=ON)
add_json_test(config_strict_import config-strict unresolved_strict)
add_json_test(configured_allow config-allow empty -DEXPECTED_EXIT=0)
add_cli_test(incremental_cache incremental_cache
  "-DSOURCE_FIXTURE=${FIXTURES}/cache"
  "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cache-work")

add_json_test(graph_json service-boundary graph -DCOMMAND=graph)
add_json_test(graph_repository_path_containing_services "${NESTED_ROOT}" graph
  -DCOMMAND=graph)
add_json_test(discover_json service-boundary discover -DCOMMAND=discover)
add_json_test(discover_repository_path_containing_services "${NESTED_ROOT}" discover
  -DCOMMAND=discover)
add_cli_test(discover_text discover_text "-DFIXTURE=${FIXTURE_ROOT}")
add_json_test(discover_configured_json config-toml discover_configured -DCOMMAND=discover)
add_cli_test(graph_html graph_html "-DFIXTURE=${FIXTURE_ROOT}")
add_cli_test(graph_python graph_language
  "-DFIXTURE=${FIXTURES}/python-relative" -DLANGUAGE=python)
add_cli_test(graph_go graph_language "-DFIXTURE=${FIXTURES}/go-single" -DLANGUAGE=go)
add_cli_test(graph_proto graph_language "-DFIXTURE=${FIXTURES}/proto-import" -DLANGUAGE=proto)

add_cli_test(cli_usage cli_usage)
add_cli_test(invalid_root invalid_root "-DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME git_hook_installation COMMAND "${CMAKE_COMMAND}"
  "-DREPO_ROOT=${PROJECT_SOURCE_DIR}"
  "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/hooks-work"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/install_git_hooks.cmake")
set_tests_properties(git_hook_installation PROPERTIES LABELS "integration;scripts")
