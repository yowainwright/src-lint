#include "config.h"
#include "test.h"

typedef struct {
  const char *path;
  const char *parent;
  const char *child;
  const char *clear;
} ConfigCase;

static const ConfigCase formats[] = {
    {".src-lintrc.toml",
     "version = 1\nstrict = true\n[cache]\nmax_mib = 2\n"
     "[boundaries.billing]\nroot = \"services/billing\"\n"
     "public = [\"api/**\", \"proto/**\"]\nallow = [\"shared/**\"]\n",
     "strict = false\n[cache]\nmax_mib = 0\n[boundaries.billing]\npublic = [\"public/**\"]\n"
     "[boundaries.orders]\nroot = \"services/orders\"\n",
     "[boundaries.billing]\nroot = \"domains/billing\"\npublic = []\nallow = []\n"},
    {".src-lintrc.json",
     "{\"version\":1,\"strict\":true,\"cache\":{\"max_mib\":2},\"boundaries\":{\"billing\":{"
     "\"root\":\"services/billing\",\"public\":[\"api/**\",\"proto/**\"],\"allow\":[\"shared/"
     "**\"]}}}",
     "{\"strict\":false,\"cache\":{\"max_mib\":0},\"boundaries\":{\"billing\":{\"public\":["
     "\"public/**\"]},"
     "\"orders\":{\"root\":\"services/orders\"}}}",
     "{\"boundaries\":{\"billing\":{\"root\":\"domains/billing\",\"public\":[],\"allow\":[]}}}"},
    {".src-lintrc.yaml",
     "version: 1\nstrict: true\ncache:\n  max_mib: 2\nboundaries:\n  billing:\n"
     "    root: services/billing\n    public:\n      - api/**\n      - proto/**\n"
     "    allow: [\"shared/**\"]\n",
     "strict: false\ncache:\n  max_mib: 0\nboundaries:\n  billing:\n    public: [\"public/**\"]\n"
     "  orders:\n    root: services/orders\n",
     "boundaries:\n  billing:\n    root: domains/billing\n    public: []\n    allow: []\n"},
};

static void apply_layer(SlConfig *config, const char *path, const char *source) {
  char *content = strdup(source);
  CHECK(content != NULL);
  CHECK(sl_config_parse(path, content, config, stderr));
  free(content);
}

static void check_defaults(const SlConfig *config) {
  CHECK(config->version == 1);
  CHECK(config->cache_max_bytes == 8U * 1024U * 1024U);
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
                        "strict = yes",
                        "unknown = true",
                        "[cache]\nmax_mib = -1",
                        "[cache]\nmax_mib = 184467440737095516160",
                        "[boundaries.billing]\npublic = [\"api/**\", 5]"};
  for (size_t index = 0; index < COUNT(toml); index += 1)
    check_invalid(".src-lintrc.toml", toml[index], "invalid TOML configuration");
}

static void invalid_json_reports_errors(void) {
  const char *json[] = {"{",
                        "{\"strict\":\"false\"}",
                        "{\"version\":2}",
                        "{} trailing",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\u12",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\u0000\"}}}",
                        "{\"boundaries\":{\"billing\":{\"root\":\"\\ud800\"}}}"};
  for (size_t index = 0; index < COUNT(json); index += 1)
    check_invalid(".src-lintrc.json", json[index], "invalid JSON configuration");
}

static void invalid_yaml_reports_errors(void) {
  const char *yaml[] = {"strict: yes", "cache:\n  max_mib: -1", "version: 2",
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
  invalid_json_reports_errors();
  invalid_yaml_reports_errors();
  decoded_strings_preserve_policy_text();
  policy_changes_invalidate_hash();
  return 0;
}
