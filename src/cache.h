#ifndef SRC_LINT_CACHE_H
#define SRC_LINT_CACHE_H

#include "config.h"
#include "internal.h"

typedef struct {
  char directory[SL_PATH_CAPACITY];
  size_t max_bytes;
  size_t tracked_bytes;
  uint64_t config_hash;
  int lock_fd;
  bool enabled;
} SlCache;

typedef struct {
  SlCache *items;
  size_t count;
  size_t capacity;
} SlCacheSet;

bool sl_cache_init(SlCache *cache, const SlConfig *config, const char *source_path,
                   const char *scan_root);
bool sl_cache_load(SlCache *cache, const char *source_path, const char *content,
                   SlImportList *imports);
size_t sl_cache_store(SlCache *cache, const char *source_path, const char *content,
                      const SlImportList *imports);
bool sl_cache_set_add(SlCacheSet *set, const SlCache *cache);
void sl_cache_set_record(SlCacheSet *set, const SlCache *cache, size_t bytes);
bool sl_cache_set_trim(SlCacheSet *set);
void sl_cache_set_free(SlCacheSet *set);

#endif
