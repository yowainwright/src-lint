#include "config.h"
#include "test.h"

static void check_owner(const SlConfig *config, const char *path, const char *name,
                        const char *expected_inside) {
  const SlBoundaryConfig *boundary = NULL;
  const char *inside = NULL;
  CHECK(sl_config_boundary_for_path(config, path, &boundary, &inside));
  CHECK(strcmp(boundary->name, name) == 0);
  CHECK(strcmp(inside, expected_inside) == 0);
}

static void longest_boundary_root_wins(void) {
  const SlBoundaryConfig parent = {.name = "billing", .root = "services/billing", .root_set = true};
  const SlBoundaryConfig child = {
      .name = "ledger", .root = "services/billing/ledger", .root_set = true};
  SlBoundaryConfig boundaries[] = {parent, child};
  SlConfig config = {.repository_root = "repo", .boundaries = boundaries, .boundary_count = 2};
  for (size_t order = 0; order < 2; order += 1) {
    check_owner(&config, "repo/services/billing/ledger/entry.ts", "ledger", "entry.ts");
    check_owner(&config, "repo/services/billing/ledger", "ledger", "");
    check_owner(&config, "repo/services/billing/ledger-old/entry.ts", "billing",
                "ledger-old/entry.ts");
    check_owner(&config, "repo/services/billing", "billing", "");
    boundaries[0] = child;
    boundaries[1] = parent;
  }
}

static void rejects_partial_roots(void) {
  SlBoundaryConfig boundary = {.name = "billing", .root = "services/billing", .root_set = true};
  const SlConfig config = {.repository_root = "repo", .boundaries = &boundary, .boundary_count = 1};
  const char *paths[] = {"repo/services/billing-old/api.ts", "repo/services/bill", "repo/services",
                         "other/repo/services/billing/api.ts", "repo-old/services/billing/api.ts"};
  for (size_t index = 0; index < COUNT(paths); index += 1) {
    const SlBoundaryConfig *owner = NULL;
    const char *inside = NULL;
    CHECK(!sl_config_boundary_for_path(&config, paths[index], &owner, &inside));
  }
}

static void public_patterns_respect_segments(void) {
  char *patterns[] = {"api/**", "index.ts"};
  const SlBoundaryConfig boundary = {.public_entries = {patterns, COUNT(patterns), true}};
  const char *allowed[] = {"api", "api/", "api/nested/entry.ts", "index.ts"};
  const char *denied[] = {"", "api-old/entry.ts", "api.ts", "internal/api/entry.ts",
                          "index.ts/child"};
  for (size_t index = 0; index < COUNT(allowed); index += 1) {
    CHECK(sl_config_public_entry(&boundary, allowed[index]));
  }
  for (size_t index = 0; index < COUNT(denied); index += 1) {
    CHECK(!sl_config_public_entry(&boundary, denied[index]));
  }
  CHECK(!sl_config_allowed_target(&boundary, "api/entry.ts"));
}

static void allow_patterns_use_repository_paths(void) {
  char *patterns[] = {"shared/**", "services/billing/api.ts"};
  const SlBoundaryConfig boundary = {.allow = {patterns, COUNT(patterns), true}};
  const char *allowed[] = {"shared", "shared/types/index.ts", "services/billing/api.ts"};
  const char *denied[] = {"shared-old/types.ts", "/shared/types.ts", "api.ts",
                          "services/billing/api.ts/child"};
  for (size_t index = 0; index < COUNT(allowed); index += 1) {
    CHECK(sl_config_allowed_target(&boundary, allowed[index]));
  }
  for (size_t index = 0; index < COUNT(denied); index += 1) {
    CHECK(!sl_config_allowed_target(&boundary, denied[index]));
  }
  CHECK(!sl_config_public_entry(&boundary, "shared/types.ts"));
}

static void empty_patterns_deny_access(void) {
  const SlBoundaryConfig unset = {0};
  const SlBoundaryConfig cleared = {.public_entries = {.set = true}, .allow = {.set = true}};
  CHECK(!sl_config_public_entry(&unset, "api/entry.ts"));
  CHECK(!sl_config_allowed_target(&unset, "shared/types.ts"));
  CHECK(!sl_config_public_entry(&cleared, "api/entry.ts"));
  CHECK(!sl_config_allowed_target(&cleared, "shared/types.ts"));
}

int main(void) {
  longest_boundary_root_wins();
  rejects_partial_roots();
  public_patterns_respect_segments();
  allow_patterns_use_repository_paths();
  empty_patterns_deny_access();
  return 0;
}
