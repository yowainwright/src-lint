#include "internal.h"
#include "test.h"

typedef struct {
  const char *specifier;
  size_t line;
  size_t column;
} ExpectedImport;

typedef struct {
  const char *name;
  const char *path;
  const char *source;
  SlLanguage language;
  ExpectedImport imports[4];
  size_t count;
} ImportCase;

static const ImportCase cases[] = {
    {"import in template expression",
     "module.js",
     "`${require('./api')}`;",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 13}},
     1},
    {"nested template expressions",
     "module.js",
     "`${`${import('./api')}`}`;",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 15}},
     1},
    {"template expression braces and ignored text",
     "module.js",
     "`require('./fake') ${({value: require('./api')}).value} import('./fake')`;",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 40}},
     1},
    {"multiple template expressions",
     "module.js",
     "`${require('./a')} text ${import('./b')}`;",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./a", 1, 13}, {"./b", 1, 35}},
     2},
    {"escaped template substitution",
     "module.js",
     "`\\${require('./fake')}`; require('./real');",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./real", 1, 35}},
     1},
    {"template expression strings comments and regex braces",
     "module.js",
     "`${'}' + /}/.source /* } */ + require('./api')}`;",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 40}},
     1},
    {"static and side-effect imports",
     "module.ts",
     "import { x } from './api';\nimport './setup';\n",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 20}, {"./setup", 2, 9}},
     2},
    {"re-export",
     "module.mjs",
     "export * from './api';",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 16}},
     1},
    {"type-only import",
     "module.ts",
     "import type { X } from './types';",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./types", 1, 25}},
     1},
    {"dynamic and CommonJS calls",
     "module.cjs",
     "import('./lazy'); require('./legacy');",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./lazy", 1, 9}, {"./legacy", 1, 28}},
     2},
    {"multiline call coordinates",
     "module.tsx",
     "import(\n  /* note */ './lazy'\n);",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./lazy", 2, 15}},
     1},
    {"cooked hex and Unicode escapes",
     "module.cts",
     "import './\\x61pi';\nrequire('./\\u0062illing');",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 9}, {"./billing", 2, 10}},
     2},
    {"Unicode code point escape",
     "module.mts",
     "import './\\u{1f600}';",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./\xf0\x9f\x98\x80", 1, 9}},
     1},
    {"literal template call",
     "module.js",
     "import(`./api`);",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 1, 9}},
     1},
    {"nonliteral calls and member methods",
     "module.jsx",
     "import(`./${name}`); require(name); object.require('./fake');",
     SL_LANGUAGE_JAVASCRIPT,
     {{0}},
     0},
    {"comments strings and regex are not imports",
     "module.js",
     "// import './fake';\n/* require('./fake'); */\n"
     "const text = \"import './fake'\";\nconst pattern = /require('fake')/;\n"
     "import './real';",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./real", 5, 9}},
     1},
    {"CRLF coordinates",
     "module.js",
     "// comment\r\nimport './api';\r\n",
     SL_LANGUAGE_JAVASCRIPT,
     {{"./api", 2, 9}},
     1},
    {"unterminated literal", "module.js", "import './unfinished", SL_LANGUAGE_JAVASCRIPT, {{0}}, 0},
    {"Python relative levels",
     "module.py",
     "from .api import x\nfrom ..billing import x\nfrom ...shared.api import x\n",
     SL_LANGUAGE_PYTHON,
     {{"./api", 1, 6}, {"../billing", 2, 6}, {"../../shared/api", 3, 6}},
     3},
    {"Python aliases and lists",
     "module.py",
     "import services.api as api, shared.types\n",
     SL_LANGUAGE_PYTHON,
     {{"services/api", 1, 8}, {"shared/types", 1, 29}},
     2},
    {"Python semicolon coordinates",
     "module.py",
     "pass; import shared.api\n",
     SL_LANGUAGE_PYTHON,
     {{"shared/api", 1, 14}},
     1},
    {"Python strings comments and docstrings",
     "module.py",
     "# import fake\ntext = 'import fake'\n\"\"\"\nimport fake\n\"\"\"\nimport real\n",
     SL_LANGUAGE_PYTHON,
     {{"real", 6, 8}},
     1},
    {"Go single import",
     "module.go",
     "package main\nimport \"example.com/api\"\n",
     SL_LANGUAGE_GO,
     {{"example.com/api", 2, 9}},
     1},
    {"Go grouped aliases and raw literal",
     "module.go",
     "import (\n  alias \"example.com/api\"\n  _ `example.com/setup`\n)\n",
     SL_LANGUAGE_GO,
     {{"example.com/api", 2, 10}, {"example.com/setup", 3, 6}},
     2},
    {"Go block comments and multiline raw strings",
     "module.go",
     "/*\nimport \"fake\"\n*/\nvar text = `\nimport \"fake\"\n`\nimport \"real\"\n",
     SL_LANGUAGE_GO,
     {{"real", 7, 9}},
     1},
    {"Proto import modifiers",
     "module.proto",
     "import \"a.proto\";\nimport public \"b.proto\";\nimport weak \"c.proto\";\n",
     SL_LANGUAGE_PROTO,
     {{"a.proto", 1, 9}, {"b.proto", 2, 16}, {"c.proto", 3, 14}},
     3},
    {"Proto block comments",
     "module.proto",
     "/*\nimport \"fake.proto\";\n*/\nimport \"real.proto\";",
     SL_LANGUAGE_PROTO,
     {{"real.proto", 4, 9}},
     1},
};

static void check_import(const SlImport *actual, const ExpectedImport *expected,
                         SlLanguage language) {
  CHECK(strcmp(actual->specifier, expected->specifier) == 0);
  CHECK(actual->line == expected->line);
  CHECK(actual->column == expected->column);
  CHECK(actual->language == language);
}

static void check_case(const ImportCase *test) {
  fprintf(stderr, "case: %s\n", test->name);
  char *content = strdup(test->source);
  CHECK(content != NULL);
  SlImportList imports = {0};
  CHECK(sl_parse_imports(test->path, content, &imports));
  free(content);
  CHECK(imports.count == test->count);
  for (size_t index = 0; index < test->count; index += 1) {
    check_import(&imports.items[index], &test->imports[index], test->language);
  }
  sl_import_list_free(&imports);
}

static void rejects_invalid_escapes(void) {
  const char *sources[] = {"import './\\x0';", "import './\\u12';", "import './\\u{110000}';",
                           "import './\\u0000';", "import './\\0';"};
  for (size_t index = 0; index < COUNT(sources); index += 1) {
    fprintf(stderr, "invalid escape: %s\n", sources[index]);
    char *content = strdup(sources[index]);
    CHECK(content != NULL);
    SlImportList imports = {0};
    CHECK(!sl_parse_imports("module.ts", content, &imports));
    CHECK(imports.count == 0);
    sl_import_list_free(&imports);
    free(content);
  }
}

static void empty_sources(void) {
  const char *paths[] = {"module.js", "module.py", "module.go", "module.proto"};
  for (size_t index = 0; index < COUNT(paths); index += 1) {
    char content[] = "";
    SlImportList imports = {0};
    CHECK(sl_parse_imports(paths[index], content, &imports));
    CHECK(imports.count == 0);
    sl_import_list_free(&imports);
  }
}

static void deeply_nested_templates(void) {
  char content[512];
  char *cursor = content;
  for (size_t depth = 0; depth < 64; depth += 1) {
    memcpy(cursor, "`${", 3);
    cursor += 3;
  }
  const char *call = "require('./api')";
  memcpy(cursor, call, strlen(call));
  cursor += strlen(call);
  for (size_t depth = 0; depth < 64; depth += 1) {
    memcpy(cursor, "}`", 2);
    cursor += 2;
  }
  *cursor = '\0';
  const ImportCase test = {"deeply nested templates", "module.js",         content,
                           SL_LANGUAGE_JAVASCRIPT,    {{"./api", 1, 202}}, 1};
  check_case(&test);
}

static void list_owns_specifiers_after_growth(void) {
  SlImportList imports = {0};
  for (size_t index = 0; index < 40; index += 1) {
    char specifier[] = "./api";
    CHECK(sl_import_list_add(&imports, specifier, index + 1, 9, SL_LANGUAGE_JAVASCRIPT));
    memset(specifier, 'x', strlen(specifier));
  }
  CHECK(imports.count == 40);
  for (size_t index = 0; index < imports.count; index += 1) {
    const ExpectedImport expected = {"./api", index + 1, 9};
    check_import(&imports.items[index], &expected, SL_LANGUAGE_JAVASCRIPT);
  }
  sl_import_list_free(&imports);
  CHECK(imports.items == NULL && imports.count == 0 && imports.capacity == 0);
  sl_import_list_free(&imports);
}

int main(void) {
  for (size_t index = 0; index < COUNT(cases); index += 1) check_case(&cases[index]);
  rejects_invalid_escapes();
  empty_sources();
  deeply_nested_templates();
  list_owns_specifiers_after_growth();
  return 0;
}
