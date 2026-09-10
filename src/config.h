#ifndef SRC_LINT_CONFIG_H
#define SRC_LINT_CONFIG_H

#include "internal.h"

#include <stdint.h>
#include <stdio.h>

#define SL_DEFAULT_CACHE_BYTES (8U * 1024U * 1024U)

typedef enum { SL_CONFIG_JSON, SL_CONFIG_TOML, SL_CONFIG_YAML } SlConfigFormat;

typedef struct {
  const char *name;
  SlConfigFormat format;
} SlConfigFile;

extern const SlConfigFile sl_config_files[];
extern const size_t sl_config_file_count;

/* Find a supported rc filename, optionally preceded by a directory path. */
const SlConfigFile *sl_config_file_for_path(const char *path);

typedef struct {
  char **items;
  size_t count;
  bool set;
} SlPatternList;

typedef struct {
  char *name;
  char *root;
  bool root_set;
  SlPatternList public_entries;
  SlPatternList allow;
} SlBoundaryConfig;

typedef struct {
  bool present;
  unsigned version;
  bool version_set;
  bool strict;
  bool strict_set;
  size_t cache_max_bytes;
  bool cache_set;
  char repository_root[SL_PATH_CAPACITY];
  SlBoundaryConfig *boundaries;
  size_t boundary_count;
  size_t boundary_capacity;
} SlConfig;

void sl_config_init(SlConfig *config);
void sl_config_free(SlConfig *config);
/* Apply one supported rc file to an initialized config, modifying content in place.
 * This does not validate the merged policy; free the config after a parse failure. */
bool sl_config_parse(const char *path, char *content, SlConfig *config, FILE *errors);
bool sl_config_load_for_file(const char *file_path, SlConfig *config, FILE *errors);
/* Absolute paths: preserve source overrides, then apply target-only rc layers in the policy root.
 */
bool sl_config_load_for_import(const char *source, const char *target, SlConfig *config,
                               FILE *errors);
bool sl_config_boundary_for_path(const SlConfig *config, const char *path,
                                 const SlBoundaryConfig **boundary, const char **inside);
bool sl_config_public_entry(const SlBoundaryConfig *boundary, const char *inside);
bool sl_config_allowed_target(const SlBoundaryConfig *boundary, const char *target);
uint64_t sl_config_hash(const SlConfig *config);

#endif
