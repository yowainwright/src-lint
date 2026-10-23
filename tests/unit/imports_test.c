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
    {"Julia aliases",
     "main.jl",
     "import Billing as B, Orders",
     SL_LANGUAGE_JULIA,
     {{"Billing", 1, 8}, {"Orders", 1, 22}},
     2},
    {"Kotlin raw backslash",
     "main.kt",
     "val text = \"\"\"\\\"\"\"\nimport Real\n",
     SL_LANGUAGE_KOTLIN,
     {{"Real", 2, 8}},
     1},
    {"Julia adjoint before include",
     "main.jl",
     "x = matrix'\ninclude(\"helper.jl\")",
     SL_LANGUAGE_JULIA,
     {{"./helper.jl", 2, 10}},
     1},
    {"Ruby percent and regex text",
     "main.rb",
     "x = %q{require_relative 'bad'}\ny = /require_relative 'bad'/\n"
     "z = \"#{f(\"require_relative 'bad'\")}\"\nrequire_relative 'good'\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 4, 19}},
     1},
    {"Swift raw and interpolated strings",
     "main.swift",
     "let x = #\"embedded \" import Fake \"\"#\n"
     "let y = \"\\(f(\"import Fake\"))\"\nimport Real\n",
     SL_LANGUAGE_SWIFT,
     {{"Real", 3, 8}},
     1},
    {"Julia include",
     "main.jl",
     "include(\"../billing/private.jl\")\n",
     SL_LANGUAGE_JULIA,
     {{"../billing/private.jl", 1, 10}},
     1},
    {"Julia local include",
     "main.jl",
     "include(\"helper.jl\")",
     SL_LANGUAGE_JULIA,
     {{"./helper.jl", 1, 10}},
     1},
    {"Julia module imports",
     "main.jl",
     "using Billing, Orders\nimport Billing.API: f, g\n",
     SL_LANGUAGE_JULIA,
     {{"Billing", 1, 7}, {"Orders", 1, 16}, {"Billing/API", 2, 8}},
     3},
    {"Julia mixed relative and absolute imports",
     "main.jl",
     "using .Utils, Billing\nusing A, .Utils, Billing\n",
     SL_LANGUAGE_JULIA,
     {{"Billing", 1, 15}, {"A", 2, 7}, {"Billing", 2, 18}},
     3},
    {"Julia relative aliases and selected names",
     "main.jl",
     "import ..Utils.Sub as U, Billing.API\nusing .Utils: Billing, Orders\nusing .Utils, .Other\n",
     SL_LANGUAGE_JULIA,
     {{"Billing/API", 1, 26}},
     1},
    {"Julia ignored text",
     "main.jl",
     "# include(\"bad.jl\")\n#= outer #= nested =# include(\"bad.jl\") =#\n"
     "text = \"\"\"\nimport Fake\n\"\"\"\ninclude(\"$name.jl\")\ninclude(\"a\" * suffix)\n",
     SL_LANGUAGE_JULIA,
     {{0}},
     0},
    {"Ruby require_relative",
     "main.rb",
     "require_relative 'helper'\n",
     SL_LANGUAGE_RUBY,
     {{"./helper", 1, 19}},
     1},
    {"Ruby require",
     "main.rb",
     "require('services/billing/api')\n",
     SL_LANGUAGE_RUBY,
     {{"services/billing/api", 1, 10}},
     1},
    {"Ruby call terminators",
     "main.rb",
     "task { require_relative '../billing/private' }\nrequire 'billing/private' or abort\n"
     "require 'billing/api' and ready\nif ready then require_relative 'helper' end\n",
     SL_LANGUAGE_RUBY,
     {{"../billing/private", 1, 26},
      {"billing/private", 2, 10},
      {"billing/api", 3, 10},
      {"./helper", 4, 33}},
     4},
    {"Ruby computed call arguments",
     "main.rb",
     "require 'bad' + suffix\nrequire 'bad' && path\nrequire 'bad' 'suffix'\n",
     SL_LANGUAGE_RUBY,
     {{0}},
     0},
    {"Ruby multiple heredocs",
     "main.rb",
     "puts <<A, <<B\nrequire_relative 'fake_a'\nA\nrequire_relative 'fake_b'\nB\n"
     "require_relative 'good'\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 6, 19}},
     1},
    {"Ruby heredoc declaration code",
     "main.rb",
     "puts <<'A-TEXT', <<~\"B TEXT\"; require_relative 'header'\n"
     "require_relative 'fake_a'\nA-TEXT\nrequire_relative 'fake_b'\n  B TEXT\n"
     "require_relative 'tail'\n",
     SL_LANGUAGE_RUBY,
     {{"./header", 1, 49}, {"./tail", 6, 19}},
     2},
    {"Ruby shifts before imports",
     "main.rb",
     "bits = 1<<2\nbits = 1 <<WIDTH\nbits = WIDTH<<2\n"
     "items = []; items <<value\nrequire_relative 'good'\nWIDTH\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 5, 19}},
     1},
    {"Ruby quoted heredoc markers",
     "main.rb",
     "text = <<'END-TEXT'\nrequire_relative 'bad'\nEND-TEXT\n"
     "text = <<\"END TEXT\"\nrequire_relative 'bad'\nEND TEXT\n"
     "text = <<`END-TEXT`\nrequire_relative 'bad'\nEND-TEXT\nrequire_relative 'good'\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 10, 19}},
     1},
    {"Ruby heredoc indentation",
     "main.rb",
     "text = <<TEXT\n  TEXT\nrequire_relative 'bad'\nTEXT\nrequire_relative 'good'\n"
     "text = <<-TEXT\nrequire_relative 'bad'\n  TEXT\n"
     "text = <<~TEXT\nrequire_relative 'bad'\n  TEXT\nrequire_relative 'other'\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 5, 19}, {"./other", 12, 19}},
     2},
    {"Ruby regex arguments and division",
     "main.rb",
     "puts /start\nrequire_relative \"fake\"\n/\n"
     "value = 8 / 2\nvalue = 8/2\nvalue = 8 /2\nvalue /divisor\n"
     "require_relative 'good'\n",
     SL_LANGUAGE_RUBY,
     {{"./good", 8, 19}},
     1},
    {"Ruby ignored text",
     "main.rb",
     "# require_relative 'bad'\n=begin\nrequire_relative 'bad'\n=end\n"
     "text = <<~TEXT\nrequire_relative 'bad'\nTEXT\n"
     "require_relative \"#{name}\"\nrequire_relative('a' + name)\n"
     "object.require_relative 'bad'\n__END__\nrequire_relative 'bad'\n",
     SL_LANGUAGE_RUBY,
     {{0}},
     0},
    {"Swift imports",
     "main.swift",
     "import Billing\n@testable import struct Orders.Item\n",
     SL_LANGUAGE_SWIFT,
     {{"Billing", 1, 8}, {"Orders", 2, 25}},
     2},
    {"Swift ignored text",
     "main.swift",
     "/* outer /* nested */ import Fake */\nlet text = \"\"\"\nimport Fake\n\"\"\"\n",
     SL_LANGUAGE_SWIFT,
     {{0}},
     0},
    {"Kotlin alias and wildcard",
     "main.kt",
     "import com.billing.Ledger as Book\nimport com.orders.*\n",
     SL_LANGUAGE_KOTLIN,
     {{"com/billing/Ledger", 1, 8}, {"com/orders", 2, 8}},
     2},
    {"Kotlin script",
     "main.kts",
     "import com.billing.Ledger\n",
     SL_LANGUAGE_KOTLIN,
     {{"com/billing/Ledger", 1, 8}},
     1},
    {"Kotlin ignored text",
     "main.kt",
     "/* outer /* nested */ import Fake */\nval text = \"\"\"\nimport Fake\n\"\"\"\n",
     SL_LANGUAGE_KOTLIN,
     {{0}},
     0},
    {"Java static and wildcard",
     "Main.java",
     "package com.orders; import static com.billing.Ledger.write;\nimport com.billing.*;\n",
     SL_LANGUAGE_JAVA,
     {{"com/billing/Ledger/write", 1, 35}, {"com/billing", 2, 8}},
     2},
    {"Java multiline import",
     "Main.java",
     "import /* comment */ com.billing.\n Ledger;",
     SL_LANGUAGE_JAVA,
     {{"com/billing/Ledger", 1, 22}},
     1},
    {"Java ignored text",
     "Main.java",
     "// import Fake;\n/* import Fake; */ String text = \"\"\"\nimport Fake;\n\"\"\";\n",
     SL_LANGUAGE_JAVA,
     {{0}},
     0},
    {"Terraform local module",
     "main.tf",
     "module \"billing\" { source = \"../billing\" }",
     SL_LANGUAGE_TERRAFORM,
     {{"../billing", 1, 30}},
     1},
    {"Terraform multiline module",
     "main.tf",
     "module \"billing\" {\n source =\n \"../billing\"\n}",
     SL_LANGUAGE_TERRAFORM,
     {{"../billing", 3, 3}},
     1},
    {"Terraform ignored sources",
     "main.tf",
     "# module \"fake\" { source = \"../bad\" }\n"
     "locals { source = \"../bad\" }\nresource \"x\" \"y\" { source = \"../bad\" }\n"
     "module \"remote\" { source = \"git::https://host/services/bad\" }\n"
     "module \"computed\" { source = \"../${var.name}\" }\n"
     "module \"nested\" { other = { source = \"../bad\" } }\n"
     "locals { text = <<EOT\nmodule \"fake\" { source = \"../bad\" }\nEOT\n}\n",
     SL_LANGUAGE_TERRAFORM,
     {{0}},
     0},
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
    {"Python inline suites",
     "module.py",
     "if True: import services.billing\nelse: from .api import value; import shared\n",
     SL_LANGUAGE_PYTHON,
     {{"services/billing", 1, 17}, {"./api", 2, 12}, {"shared", 2, 38}},
     3},
    {"Python colons and semicolons in strings",
     "module.py",
     "if text == 'x: import fake;': import real\nvalue = {'x: import fake': 1}\n",
     SL_LANGUAGE_PYTHON,
     {{"real", 1, 38}},
     1},
    {"Go same-line declarations and block close",
     "module.go",
     "package main; import (\"fmt\"; _ \"api\"); import \"other\"\nvar text = \"fake\"\n",
     SL_LANGUAGE_GO,
     {{"fmt", 1, 24}, {"api", 1, 33}, {"other", 1, 48}},
     3},
    {"Go escaped import paths",
     "module.go",
     "import \"ser\\u0076ices\\x2fbilling\"\nimport \"\\163ervices/\\U0001f600\"\n",
     SL_LANGUAGE_GO,
     {{"services/billing", 1, 9}, {"services/\xf0\x9f\x98\x80", 2, 9}},
     2},
    {"Go raw import and delimiters inside strings",
     "module.go",
     "import (`api\\raw`; \"a;b)c\")\nvar ignored = \"import fake\"\n",
     SL_LANGUAGE_GO,
     {{"api\\raw", 1, 10}, {"a;b)c", 1, 21}},
     2},
    {"Proto same-line statements",
     "module.proto",
     "syntax = \"proto3\"; import \"a.proto\"; import public \"b.proto\";\n",
     SL_LANGUAGE_PROTO,
     {{"a.proto", 1, 28}, {"b.proto", 1, 53}},
     2},
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
  const char *paths[] = {"module.js",  "module.py",   "module.go",    "module.proto",
                         "module.jl",  "module.rb",   "module.swift", "module.kt",
                         "module.kts", "module.java", "module.tf",    "module.txt"};
  for (size_t index = 0; index < COUNT(paths); index += 1) {
    char content[] = "";
    SlImportList imports = {0};
    CHECK(sl_parse_imports(paths[index], content, &imports));
    CHECK(imports.count == 0);
    sl_import_list_free(&imports);
  }
}

static void truncated_language_case(const ImportCase *test) {
  if (test->language < SL_LANGUAGE_JULIA) return;
  const size_t size = strlen(test->source);
  for (size_t length = 0; length < size; length += 1) {
    char *content = strndup(test->source, length);
    CHECK(content != NULL);
    SlImportList imports = {0};
    (void)sl_parse_imports(test->path, content, &imports);
    sl_import_list_free(&imports);
    free(content);
  }
}

static void truncated_language_sources(void) {
  for (size_t index = 0; index < COUNT(cases); index += 1) truncated_language_case(&cases[index]);
}

static void unsupported_extensions(void) {
  char content[] = "import './not-a-source-file';";
  SlImportList imports = {0};
  CHECK(sl_source_language("notes.txt") == SL_LANGUAGE_COUNT);
  CHECK(sl_parse_imports("notes.txt", content, &imports));
  CHECK(imports.count == 0);
  sl_import_list_free(&imports);
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
  truncated_language_sources();
  unsupported_extensions();
  deeply_nested_templates();
  list_owns_specifiers_after_growth();
  return 0;
}
