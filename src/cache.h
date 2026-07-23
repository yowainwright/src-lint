#ifndef TREE_LEGIBILITY_CACHE_H
#define TREE_LEGIBILITY_CACHE_H

#include "config.h"
#include "internal.h"

typedef struct {
  char directory[TL_PATH_CAPACITY];
  size_t max_bytes;
  size_t tracked_bytes;
  uint64_t config_hash;
  int lock_fd;
  bool enabled;
} TlCache;

typedef struct {
  TlCache *items;
  size_t count;
  size_t capacity;
} TlCacheSet;

bool tl_cache_init(TlCache *cache, const TlConfig *config, const char *source_path,
                   const char *scan_root);
bool tl_cache_load(TlCache *cache, const char *source_path, const char *content,
                   TlImportList *imports);
size_t tl_cache_store(TlCache *cache, const char *source_path, const char *content,
                      const TlImportList *imports);
bool tl_cache_set_add(TlCacheSet *set, const TlCache *cache);
void tl_cache_set_record(TlCacheSet *set, const TlCache *cache, size_t bytes);
bool tl_cache_set_trim(TlCacheSet *set);
void tl_cache_set_free(TlCacheSet *set);

#endif
