#include "config.h"
#include "test.h"

typedef struct {
  const char *path;
  const char *parent;
  const char *child;
  const char *clear;
} ConfigCase;

static const char json_parent[] =
    "{\"version\":1,\"strict\":true,\"cache\":{\"max_mib\":2},\"boundaries\":{\"billing\":{"
    "\"root\":\"services/billing\",\"public\":[\"api/**\",\"proto/**\"],\"allow\":[\"shared/"
    "**\"]}}}";
static const char json_child[] =
    "{\"strict\":false,\"cache\":{\"max_mib\":0},\"boundaries\":{\"billing\":{"
    "\"public\":[\"public/**\"]},\"orders\":{\"root\":\"services/orders\"}}}";
static const char json_clear[] =
    "{\"boundaries\":{\"billing\":{\"root\":\"domains/billing\",\"public\":[],\"allow\":[]}}}";

static const ConfigCase formats[] = {
    {".src-lintrc.toml",
     "version = 1\nstrict = true\n[cache]\nmax_mib = 2\n"
     "[boundaries.billing]\nroot = \"services/billing\"\n"
     "public = [\n  \"api/**\", # public entry\n\n  \"proto/**\",\n]\nallow = [\"shared/**\"]\n",
     "strict = false\n[cache]\nmax_mib = 0\n[boundaries.billing]\npublic = [\"public/**\"]\n"
     "[boundaries.orders]\nroot = \"services/orders\"\n",
     "[boundaries.billing]\nroot = \"domains/billing\"\npublic = [\n# empty\n]\nallow = []\n"},
    {".src-lintrc.json", json_parent, json_child, json_clear},
    {".src-lintrc.yaml",
     "version: 1\nstrict: true\ncache:\n  max_mib: 2\nboundaries:\n  billing:\n"
     "    root: services/billing\n    public:\n      - api/**\n      - proto/**\n"
     "    allow: [\"shared/**\"]\n",
     "strict: false\ncache:\n  max_mib: 0\nboundaries:\n  billing:\n    public: [\"public/**\"]\n"
     "  orders:\n    root: services/orders\n",
     "boundaries:\n  billing:\n    root: domains/billing\n    public: []\n    allow: []\n"},
    {".src-lintrc", json_parent, json_child, json_clear},
    {"package.json",
     "{\"name\":\"sample\",\"private\":true,\"version\":\"2.0.0\","
     "\"other\":[null,-1.5e+3,{\"src-lint\":false},\"\\u0000\"],"
     "\"src-lint\":{\"version\":1,\"strict\":true,\"cache\":{\"max_mib\":2},"
     "\"boundaries\":{\"billing\":{\"root\":\"services/billing\","
     "\"public\":[\"api/**\",\"proto/**\"],\"allow\":[\"shared/**\"]}}},"
     "\"scripts\":{\"test\":\"echo \\\"src-lint\\\"\"}}",
     "{\"src-lint\":{\"strict\":false,\"cache\":{\"max_mib\":0},"
     "\"boundaries\":{\"billing\":{\"public\":[\"public/**\"]},"
     "\"orders\":{\"root\":\"services/orders\"}}}}",
     "{\"src-lint\":{\"boundaries\":{\"billing\":{\"root\":\"domains/billing\","
     "\"public\":[],\"allow\":[]}}}}"},
    {"pyproject.toml",
     "[project]\nname = 'example'\ndescription = '''\n[tool.src-lint]\nstrict = invalid\n'''\n"
     "dependencies = [\n'other',\n]\n[tool.src-lint]\nversion = 1\nstrict = true\n"
     "[tool.src-lint.cache]\nmax_mib = 2\n[tool.src-lint.boundaries.billing]\n"
     "root = \"services/billing\"\npublic = [ # public entries\n"
     "  \"api/**\",\n  # ignored ] bracket\n  \"proto/**\",\n]\n"
     "allow = [\n  \"shared/**\",\n]\n[tool.other] # example = [\nstrict = 'unrelated'\n",
     "[\"tool\".\"src\\u002dlint\"]\nstrict = false\n[tool.src-lint.cache]\nmax_mib = 0\n"
     "[tool.src-lint.boundaries.billing]\npublic = [\"public/**\"]\n"
     "[tool.src-lint.boundaries.orders]\nroot = \"services/orders\"\n",
     "[tool.src-lint.boundaries.billing]\nroot = \"domains/billing\"\n"
     "public = [\r\n  # empty\r\n]\r\nallow = []\n"},
    {"src-lint.yaml",
     "---\n\"meta:data\": Don't read this as policy\nother:\n  src-lint:\n    strict: false\n"
     "src-lint:\n  version: 1\n  strict: true\n  cache:\n    max_mib: 2\n"
     "  boundaries:\n    billing:\n      root: services/billing\n"
     "      public: [api/**, proto/**]\n      allow: [shared/**]\nmetadata: done\n...\n",
     "\"src-lint\":\n  strict: false\n  cache:\n    max_mib: 0\n  boundaries:\n"
     "    billing:\n      public: [public/**]\n    orders:\n      root: services/orders\n",
     "src-lint:\n  boundaries:\n    billing:\n      root: domains/billing\n"
     "      public: []\n      allow: []\n"},
    {"src-lint.yml",
     "src-lint:\n    version: 1\n    strict: true\n    cache:\n      max_mib: 2\n"
     "    boundaries:\n      billing:\n        root: services/billing\n"
     "        public: [api/**, proto/**]\n        allow: [shared/**]\n",
     "src-lint:\n  strict: false\n  cache:\n    max_mib: 0\n  boundaries:\n"
     "    billing:\n      public: [public/**]\n    orders:\n      root: services/orders\n",
     "src-lint:\n  boundaries:\n    billing:\n      root: domains/billing\n"
     "      public: []\n      allow: []\n"},
};

static void apply_layer(SlConfig *config, const char *path, const char *source) {
  char *content = strdup(source);
  CHECK(content != NULL);
  CHECK(sl_config_parse(path, content, config, stderr));
  free(content);
}

static void check_defaults(const SlConfig *config) {
  CHECK(config->version == 1);
  CHECK(config->cache_max_bytes == 0);
  CHECK(!config->strict && !config->present);
  CHECK(!config->version_set && !config->strict_set && !config->cache_set);
  CHECK(config->boundaries == NULL && config->boundary_count == 0);
  CHECK(config->repository_root[0] == '\0');
}

static void check_parent(const SlConfig *config) {
  CHECK(config->version == 1 && config->version_set);
  CHECK(config->strict && config->strict_set);
  CHECK(config->cache_max_bytes == 2U * 1024U * 1024U && config->cache_set);
  CHECK(config->boundary_count == 1);
  const SlBoundaryConfig *billing = &config->boundaries[0];
  CHECK(strcmp(billing->name, "billing") == 0);
  CHECK(strcmp(billing->root, "services/billing") == 0 && billing->root_set);
  CHECK(billing->public_entries.set && billing->public_entries.count == 2);
  CHECK(strcmp(billing->public_entries.items[0], "api/**") == 0);
  CHECK(strcmp(billing->public_entries.items[1], "proto/**") == 0);
  CHECK(billing->allow.set && billing->allow.count == 1);
  CHECK(strcmp(billing->allow.items[0], "shared/**") == 0);
}

static void formats_produce_equivalent_policy(void) {
  uint64_t expected_hash = 0;
  for (size_t index = 0; index < COUNT(formats); index += 1) {
    SlConfig config;
    sl_config_init(&config);
    check_defaults(&config);
    apply_layer(&config, formats[index].path, formats[index].parent);
    check_parent(&config);
    const uint64_t hash = sl_config_hash(&config);
    if (index == 0) expected_hash = hash;
    CHECK(hash == expected_hash);
    sl_config_free(&config);
    check_defaults(&config);
    sl_config_free(&config);
  }
}

static void check_inherited_policy(const SlConfig *config) {
  CHECK(!config->strict && config->strict_set);
  CHECK(config->cache_max_bytes == 0 && config->cache_set);
  CHECK(config->version == 1 && config->version_set);
  CHECK(config->boundary_count == 2);
  const SlBoundaryConfig *billing = &config->boundaries[0];
  CHECK(strcmp(billing->root, "services/billing") == 0);
  CHECK(billing->public_entries.count == 1);
  CHECK(strcmp(billing->public_entries.items[0], "public/**") == 0);
  CHECK(billing->allow.count == 1 && strcmp(billing->allow.items[0], "shared/**") == 0);
  const SlBoundaryConfig *orders = &config->boundaries[1];
  CHECK(strcmp(orders->name, "orders") == 0);
  CHECK(strcmp(orders->root, "services/orders") == 0);
  CHECK(!orders->public_entries.set && !orders->allow.set);
}

static void check_cleared_policy(const SlConfig *config) {
  CHECK(config->boundary_count == 2);
  const SlBoundaryConfig *billing = &config->boundaries[0];
  CHECK(strcmp(billing->root, "domains/billing") == 0);
  CHECK(billing->public_entries.set && billing->public_entries.count == 0);
  CHECK(billing->allow.set && billing->allow.count == 0);
  CHECK(!sl_config_public_entry(billing, "public/api.ts"));
  CHECK(!sl_config_allowed_target(billing, "shared/types.ts"));
  CHECK(strcmp(config->boundaries[1].root, "services/orders") == 0);
}

static void check_layer_pair(const ConfigCase *parent, const ConfigCase *child) {
  fprintf(stderr, "inheritance: %s -> %s\n", parent->path, child->path);
  SlConfig config;
  sl_config_init(&config);
  apply_layer(&config, parent->path, parent->parent);
  apply_layer(&config, child->path, child->child);
  check_inherited_policy(&config);
  apply_layer(&config, child->path, child->clear);
  check_cleared_policy(&config);
  sl_config_free(&config);
}

static void inheritance_across_formats(void) {
  for (size_t parent = 0; parent < COUNT(formats); parent += 1) {
    for (size_t child = 0; child < COUNT(formats); child += 1) {
      check_layer_pair(&formats[parent], &formats[child]);
    }
  }
}

static void check_invalid(const char *path, const char *source, const char *diagnostic) {
  fprintf(stderr, "invalid config: %s: %s\n", path, source);
  char *message = NULL;
  size_t size = 0;
  FILE *errors = open_memstream(&message, &size);
  CHECK(errors != NULL);
  SlConfig config;
  sl_config_init(&config);
  char *content = strdup(source);
  CHECK(content != NULL);
  CHECK(!sl_config_parse(path, content, &config, errors));
  CHECK(fclose(errors) == 0);
  CHECK(size > 0 && strstr(message, path) != NULL);
  CHECK(strstr(message, diagnostic) != NULL);
  free(message);
  free(content);
  sl_config_free(&config);
}

static void invalid_toml_reports_errors(void) {
  const char *toml[] = {"version = 2",
                        "strict = true\nstrict = false",
                        "version = 1\nversion = 1",
                        "[cache]\n[cache]",
                        "[cache]\nmax_mib = 1\nmax_mib = 0",
                        "[boundaries.billing]\n[boundaries.billing]",
                        "[boundaries.billing]\nroot = \"a\"\nroot = \"b\"",
                        "[boundaries.billing]\npublic = []\npublic = []",
                        "[boundaries.billing]\nallow = []\nallow = []",
                        "strict = yes",
                        "unknown = true",
                        "[cache]\nmax_mib = -1",
                        "[cache]\nmax_mib = 184467440737095516160",
                        "[boundaries.billing]\npublic = [\"api/**\", 5]"};
  for (size_t index = 0; index < COUNT(toml); index += 1)
    check_invalid(".src-lintrc.toml", toml[index], "invalid TOML configuration");
}

static void invalid_multiline_toml_arrays_report_errors(void) {
  const char *values[] = {"[\n\"api/**\",", "[\n\"api/**\"\n\"proto/**\"\n]", "[\n5\n]",
                          "[\n\"unterminated\n]", "[\n# only a comment"};
  for (size_t index = 0; index < COUNT(values); index += 1) {
    char source[256];
    snprintf(source, sizeof(source), "[boundaries.billing]\npublic = %s", values[index]);
    check_invalid(".src-lintrc.toml", source, ":2: invalid TOML configuration");
    snprintf(source, sizeof(source), "[tool.src-lint.boundaries.billing]\npublic = %s",
             values[index]);
    check_invalid("pyproject.toml", source, ":2: invalid TOML configuration");
  }
  check_invalid("pyproject.toml",
                "[tool.src-lint.boundaries.billing]\npublic = [\n\"api/**\",\n]\nunknown = true",
                ":5: invalid TOML configuration");
}

static void multiline_toml_arrays_preserve_strings(void) {
  SlConfig config;
  sl_config_init(&config);
  apply_layer(&config, "pyproject.toml",
              "[tool.src-lint.boundaries.billing]\npublic = [\n"
              "  \"quote\\\"#]entry\", # ignored [\n  \"api/**\",\n]");
  CHECK(config.boundary_count == 1);
  CHECK(config.boundaries[0].public_entries.count == 2);
  CHECK(strcmp(config.boundaries[0].public_entries.items[0], "quote\"#]entry") == 0);
  CHECK(strcmp(config.boundaries[0].public_entries.items[1], "api/**") == 0);
  sl_config_free(&config);
}

static void invalid_json_reports_errors(void) {
  const char *json[] = {"{",
                        "{\"strict\":true,\"strict\":false}",
                        "{\"version\":1,\"version\":1}",
                        "{\"cache\":{},\"cache\":{}}",
                        "{\"boundaries\":{},\"boundaries\":{}}",
                        "{\"cache\":{\"max_mib\":1,\"max_mib\":0}}",
                        "{\"boundaries\":{\"a\":{},\"a\":{}}}",
                        "{\"boundaries\":{\"a\":{\"root\":\"a\",\"root\":\"b\"}}}",
                        "{\"boundaries\":{\"a\":{\"public\":[],\"public\":[]}}}",
                        "{\"boundaries\":{\"a\":{\"allow\":[],\"allow\":[]}}}",
                        "{\"strict\":\"false\"}",
                        "{\"version\":2}",
                        "{} trailing",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\u12",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\u0000\"}}}",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\ud800\"}}}"};
  for (size_t index = 0; index < COUNT(json); index += 1) {
    check_invalid(".src-lintrc.json", json[index], "invalid JSON configuration");
    check_invalid(".src-lintrc", json[index], "invalid JSON configuration");
  }
}

static void invalid_yaml_reports_errors(void) {
  const char *yaml[] = {"strict: yes",
                        "cache:\n  max_mib: -1",
                        "version: 2",
                        "strict: true\nstrict: false",
                        "cache:\ncache:",
                        "cache:\n  max_mib: 1\n  max_mib: 0",
                        "boundaries:\nboundaries:",
                        "boundaries:\n  billing:\n  billing:",
                        "boundaries:\n  billing:\n    root: a\n    root: b",
                        "boundaries:\n  billing:\n    public: []\n    public: []",
                        "boundaries:\n  billing:\n    allow: []\n    allow: []",
                        "boundaries:\n  billing:\n    root: services/billing\nstrict: true\n"
                        "    public: [internal/**]",
                        "strict: true\n  billing:\n    root: services/billing",
                        "boundaries:\n  billing:\n    public:\n      - api/**\n"
                        "    root: services/billing\n      - internal/**",
                        "boundaries:\n  billing:\n    root: \"\\q\""};
  for (size_t index = 0; index < COUNT(yaml); index += 1)
    check_invalid(".src-lintrc.yaml", yaml[index], "invalid YAML configuration");
}

static void check_decoded_root(const char *path, const char *source, const char *expected) {
  SlConfig config;
  sl_config_init(&config);
  apply_layer(&config, path, source);
  CHECK(config.boundary_count == 1);
  CHECK(strcmp(config.boundaries[0].root, expected) == 0);
  sl_config_free(&config);
}

static void invalid_embedded_configs_report_errors(void) {
  const char *json[] = {"{\"src-lint\":null}",
                        "{\"src-lint\":[]}",
                        "{\"src-lint\":{},\"src-lint\":{}}",
                        "{\"src-lint\":{\"strict\":true,\"strict\":false}}",
                        "{\"src-lint\":{\"unknown\":0}}",
                        "{\"metadata\":[true,]}",
                        "{\"metadata\":01}",
                        "{\"metadata\":\"\\",
                        "{\"metadata\":{\"key\":false,}}",
                        "{\"src-lint\":{}} trailing"};
  for (size_t index = 0; index < COUNT(json); index += 1)
    check_invalid("package.json", json[index], "invalid JSON configuration");
  const char *toml[] = {"[tool.src-lint]\nstrict = true\nstrict = false",
                        "[tool.src-lint]\n[tool.src-lint]",
                        "[tool.src-lint.unknown]\nx = 1",
                        "[[tool.src-lint]]",
                        "[tool]\nsrc-lint = 1",
                        "tool.src-lint.strict = true",
                        "[project]\ntext = '''unterminated"};
  for (size_t index = 0; index < COUNT(toml); index += 1)
    check_invalid("pyproject.toml", toml[index], "invalid TOML configuration");
  const char *yaml[] = {"src-lint: null",
                        "src-lint: []",
                        "src-lint: {}\nsrc-lint: {}",
                        "src-lint:\n  strict: true\n  strict: false",
                        "src-lint:\n  unknown: true",
                        "src-lint:\n  strict: true\n    cache:\n      max_mib: 1",
                        "other: [unterminated",
                        "---\nsrc-lint: {}\n---\nsrc-lint: {}"};
  for (size_t index = 0; index < COUNT(yaml); index += 1)
    check_invalid("src-lint.yaml", yaml[index], "invalid YAML configuration");
}

static void unrelated_settings_do_not_configure_policy(void) {
  const char *paths[] = {"package.json", "pyproject.toml", "src-lint.yml", "src-lint.yaml"};
  const char *contents[] = {
      "{\"name\":\"src-lint\",\"other\":{\"src-lint\":{\"strict\":true}}}",
      ("[project]\nreadme = \"\"\"\n[tool.src-lint]\nstrict = true\n\"\"\"\n"
       "[tool.src-lint-other]\nstrict = true\n[\"tool.src-lint\"]\nstrict = true"),
      "other: |\n  src-lint:\n    strict: true\nmetadata:\n  src-lint: {}",
      "other: {\nsrc-lint: {strict: true}\n}\ntext: \"src-lint:\n  strict: true\""};
  for (size_t index = 0; index < COUNT(paths); index += 1) {
    SlConfig config;
    sl_config_init(&config);
    apply_layer(&config, paths[index], contents[index]);
    check_defaults(&config);
    sl_config_free(&config);
  }
}

static void truncated_package_json_reports_errors(void) {
  const char *source = "{\"metadata\":[{\"text\":\"\\ud83d\\ude00\"}],\"src-lint\":{}}";
  char *content = strdup(source);
  CHECK(content != NULL);
  for (size_t length = 0; length < strlen(source); length += 1) {
    memcpy(content, source, length);
    content[length] = '\0';
    check_invalid("package.json", content, "invalid JSON configuration");
  }
  free(content);
}

static void decoded_strings_preserve_policy_text(void) {
  check_decoded_root(".src-lintrc.toml",
                     "[boundaries.billing]\nroot = \"quote\\\"#entry\" # comment", "quote\"#entry");
  check_decoded_root(".src-lintrc.json",
                     "{\"boundaries\":{\"billing\":{\"root\":\"\\u0061pi\\/\\ud83d\\ude00\"}}}",
                     "api/\xf0\x9f\x98\x80");
  check_decoded_root(".src-lintrc.yaml",
                     "boundaries:\n  billing:\n    root: 'billing''s#api' # comment",
                     "billing's#api");
  check_decoded_root(".src-lintrc.yml",
                     "boundaries:\n  billing:\n    root: \"\\x61pi/\\U0001f600\"",
                     "api/\xf0\x9f\x98\x80");
}

static void policy_changes_invalidate_hash(void) {
  const char *changes[] = {
      "strict = false", "[cache]\nmax_mib = 0", "[boundaries.billing]\nroot = \"domains/billing\"",
      "[boundaries.billing]\npublic = [\"public/**\"]", "[boundaries.billing]\nallow = []"};
  for (size_t index = 0; index < COUNT(changes); index += 1) {
    SlConfig config;
    sl_config_init(&config);
    apply_layer(&config, formats[0].path, formats[0].parent);
    const uint64_t before = sl_config_hash(&config);
    CHECK(sl_config_hash(&config) == before);
    apply_layer(&config, formats[0].path, changes[index]);
    CHECK(sl_config_hash(&config) != before);
    sl_config_free(&config);
  }
}

int main(void) {
  formats_produce_equivalent_policy();
  inheritance_across_formats();
  invalid_toml_reports_errors();
  invalid_multiline_toml_arrays_report_errors();
  multiline_toml_arrays_preserve_strings();
  invalid_json_reports_errors();
  invalid_yaml_reports_errors();
  invalid_embedded_configs_report_errors();
  unrelated_settings_do_not_configure_policy();
  truncated_package_json_reports_errors();
  decoded_strings_preserve_policy_text();
  policy_changes_invalidate_hash();
  return 0;
}
