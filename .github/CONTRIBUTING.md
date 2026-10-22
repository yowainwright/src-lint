# Changes

Use a branch and keep pull requests focused. Describe the changed behavior and
include validation results; cover bug fixes with a regression test.

See the [README](../README.md#development) for setup, build, and test commands.
C formatting uses `clang-format --style=file:scripts/.clang-format` from the
repository root. Update documentation when CLI or configuration behavior changes.

Match tests to the file they cover: `src/imports.c` → `tests/unit/imports_test.c`,
executable `imports_test`, CTest name `imports`. Tests spanning files use a
behavior name, such as `cli_root` or `polyglot_imports`. Use lowercase snake_case;
directories and CTest labels identify the suite without prefixes in test names.
