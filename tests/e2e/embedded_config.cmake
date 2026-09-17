cmake_minimum_required(VERSION 3.20)

function(check_config expected rule)
  execute_process(COMMAND "${CLI}" check "${WORK_ROOT}" --format json
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "Expected exit ${expected}, got ${result}\n${output}\n${errors}")
  endif()
  if(NOT "${output}${errors}" MATCHES "${rule}")
    message(FATAL_ERROR "Missing ${rule}\n${output}\n${errors}")
  endif()
endfunction()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/.git" "${WORK_ROOT}/domains/orders"
  "${WORK_ROOT}/domains/billing/api")
file(WRITE "${WORK_ROOT}/domains/billing/api/index.ts" "export {};\n")
file(WRITE "${WORK_ROOT}/domains/orders/create.ts" "import '../billing/api/index';\n")

set(policy [[{"strict":true,"boundaries":{"orders":{"root":"domains/orders"},
  "billing":{"root":"domains/billing","public":["api/**"]}}}]])
if(CONFIG_NAME STREQUAL "package.json")
  set(parent "{\"name\":\"example\",\"src-lint\":${policy}}")
  set(invalid "{\"src-lint\":{\"strict\":\"invalid\"}}")
elseif(CONFIG_NAME STREQUAL "pyproject.toml")
  set(parent [[
[project]
name = "example"
[tool.src-lint]
strict = true
[tool.src-lint.boundaries.orders]
root = "domains/orders"
[tool.src-lint.boundaries.billing]
root = "domains/billing"
public = [
  # Public entry points.
  "api/**",
]
]])
  set(invalid "[tool.src-lint]\nstrict = 'invalid'\n")
else()
  set(parent [[
metadata: example
src-lint:
  strict: true
  boundaries:
    orders:
      root: domains/orders
    billing:
      root: domains/billing
      public: [api/**]
]])
  set(invalid "src-lint:\n  strict: invalid\n")
endif()

file(WRITE "${WORK_ROOT}/${CONFIG_NAME}" "${parent}")
check_config(0 "findings")
file(WRITE "${WORK_ROOT}/domains/orders/.src-lintrc.json"
  [[{"boundaries":{"billing":{"public":[]}}}]])
check_config(1 "SL1001")
file(WRITE "${WORK_ROOT}/domains/orders/create.ts" "import './missing';\n")
check_config(1 "SL2001")
file(WRITE "${WORK_ROOT}/domains/orders/.src-lintrc.json" [[{"strict":false}]])
check_config(0 "SL2001")
file(REMOVE "${WORK_ROOT}/domains/orders/.src-lintrc.json")
file(WRITE "${WORK_ROOT}/domains/orders/src-lint.yaml" "src-lint:\n  strict: false\n")
check_config(0 "SL2001")

file(WRITE "${WORK_ROOT}/.src-lintrc.json" "{}")
check_config(2 "multiple rc files")
file(REMOVE "${WORK_ROOT}/.src-lintrc.json")
file(WRITE "${WORK_ROOT}/empty/deep/${CONFIG_NAME}" "${invalid}")
check_config(2 "invalid .* configuration")
file(REMOVE_RECURSE "${WORK_ROOT}/empty")

file(REMOVE "${WORK_ROOT}/${CONFIG_NAME}" "${WORK_ROOT}/domains/orders/src-lint.yaml")
file(WRITE "${WORK_ROOT}/.src-lintrc.json" "${policy}")
file(WRITE "${WORK_ROOT}/package.json" [[{"name":"unrelated"}]])
file(WRITE "${WORK_ROOT}/pyproject.toml" "[project]\nname = 'unrelated'\n")
file(WRITE "${WORK_ROOT}/src-lint.yaml" "other:\n  src-lint:\n    strict: false\n")
file(WRITE "${WORK_ROOT}/src-lint.yml" "other: true\n")
check_config(1 "SL2001")
file(REMOVE "${WORK_ROOT}/.src-lintrc.json")
check_config(0 "SL2001")
