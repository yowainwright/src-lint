#ifndef TREE_LEGIBILITY_CONFIG_H
#define TREE_LEGIBILITY_CONFIG_H

#include "internal.h"

#include <stdint.h>
#include <stdio.h>

#define TL_DEFAULT_CACHE_BYTES (8U * 1024U * 1024U)

typedef struct {
  char **items;
  size_t count;
  bool set;
} TlPatternList;

typedef struct {
  char *name;
  char *root;
  bool root_set;
  TlPatternList public_entries;
  TlPatternList allow;
} TlBoundaryConfig;

typedef struct {
  bool present;
  unsigned version;
  bool version_set;
  bool strict;
  bool strict_set;
  size_t cache_max_bytes;
  bool cache_set;
  char repository_root[TL_PATH_CAPACITY];
  TlBoundaryConfig *boundaries;
  size_t boundary_count;
  size_t boundary_capacity;
} TlConfig;

void tl_config_init(TlConfig *config);
void tl_config_free(TlConfig *config);
bool tl_config_load_for_file(const char *file_path, TlConfig *config, FILE *errors);
bool tl_config_boundary_for_path(const TlConfig *config, const char *path,
                                 const TlBoundaryConfig **boundary, const char **inside);
bool tl_config_public_entry(const TlBoundaryConfig *boundary, const char *inside);
bool tl_config_allowed_target(const TlBoundaryConfig *boundary, const char *target);
uint64_t tl_config_hash(const TlConfig *config);

#endif
