#include "cache.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
  char path[TL_PATH_CAPACITY];
  size_t size;
  struct timespec modified;
} TlCacheEntry;

typedef struct {
  TlCacheEntry *items;
  size_t count;
  size_t capacity;
  size_t total_size;
} TlCacheEntries;

typedef struct {
  unsigned language;
  size_t line;
  size_t column;
  size_t length;
} TlCacheImportRecord;

static bool directory_path(const char *path) {
  struct stat information;
  return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

static bool parent_directory(char *directory) {
  if (strcmp(directory, "/") == 0) return false;
  char *slash = strrchr(directory, '/');
  if (!slash) return false;
  if (slash == directory)
    directory[1] = '\0';
  else
    *slash = '\0';
  return true;
}

static bool containing_directory(const char *path, char *directory) {
  if (strlen(path) >= TL_PATH_CAPACITY) return false;
  strcpy(directory, path);
  if (directory_path(directory)) return true;
  return parent_directory(directory);
}

static bool git_marker(const char *directory) {
  char marker[TL_PATH_CAPACITY];
  const int written = snprintf(marker, sizeof(marker), "%s/.git", directory);
  if (written < 0 || (size_t)written >= sizeof(marker)) return false;
  struct stat information;
  return stat(marker, &information) == 0;
}

static bool find_git_root(const char *path, char *root) {
  if (!containing_directory(path, root)) return false;
  do {
    if (git_marker(root)) return true;
  } while (parent_directory(root));
  return false;
}

static bool config_root(const TlConfig *config, char *root) {
  if (!config->present) return false;
  if (config->repository_root[0] == '\0') {
    strcpy(root, "/");
    return true;
  }
  const int written = snprintf(root, TL_PATH_CAPACITY, "/%s", config->repository_root);
  return written >= 0 && written < TL_PATH_CAPACITY;
}

static bool repository_root(const TlConfig *config, const char *source_path, const char *scan_root,
                            char *root) {
  if (config_root(config, root)) return true;
  if (find_git_root(source_path, root)) return true;
  return containing_directory(scan_root, root);
}

static bool ensure_directory(const char *path) {
  if (mkdir(path, 0777) == 0) return true;
  return errno == EEXIST && directory_path(path);
}

static bool create_cache_directory(const char *root, char *directory) {
  char parent[TL_PATH_CAPACITY];
  const int parent_length = snprintf(parent, sizeof(parent), "%s/.tree-legibility", root);
  if (parent_length < 0 || (size_t)parent_length >= sizeof(parent)) return false;
  if (!ensure_directory(parent)) return false;
  const int length = snprintf(directory, TL_PATH_CAPACITY, "%s/cache", parent);
  if (length < 0 || length >= TL_PATH_CAPACITY) return false;
  return ensure_directory(directory);
}

static void hash_bytes(uint64_t *hash, const void *bytes, size_t length) {
  const unsigned char *cursor = bytes;
  for (size_t index = 0; index < length; index += 1) {
    *hash ^= cursor[index];
    *hash *= UINT64_C(1099511628211);
  }
}

static void hash_string(uint64_t *hash, const char *value) {
  hash_bytes(hash, value, strlen(value));
  const unsigned char separator = 0xff;
  hash_bytes(hash, &separator, 1);
}

static uint64_t cache_key(const TlCache *cache, const char *path, const char *content) {
  uint64_t hash = UINT64_C(1469598103934665603);
  const char *tool_version = "tree-legibility-0.1.0";
  const char *parser_version = "lexical-adapters-v1";
  const char *cache_version = "cache-v1";
  hash_string(&hash, tool_version);
  hash_string(&hash, parser_version);
  hash_string(&hash, cache_version);
  hash_bytes(&hash, &cache->config_hash, sizeof(cache->config_hash));
  hash_string(&hash, path);
  hash_string(&hash, content);
  return hash;
}

static bool record_path(const TlCache *cache, uint64_t key, char *path) {
  const int written =
      snprintf(path, TL_PATH_CAPACITY, "%s/%016llx.tlc", cache->directory, (unsigned long long)key);
  return written >= 0 && written < TL_PATH_CAPACITY;
}

static bool cache_file_name(const char *name) {
  const size_t length = strlen(name);
  return length > 4 && strcmp(name + length - 4, ".tlc") == 0;
}

static bool grow_entries(TlCacheEntries *entries) {
  const size_t capacity = entries->capacity == 0 ? 16 : entries->capacity * 2;
  TlCacheEntry *items = realloc(entries->items, capacity * sizeof(*items));
  if (!items) return false;
  entries->items = items;
  entries->capacity = capacity;
  return true;
}

static bool add_cache_entry(TlCacheEntries *entries, const char *path, const struct stat *info) {
  if (entries->count == entries->capacity && !grow_entries(entries)) return false;
  if (strlen(path) >= sizeof(entries->items[entries->count].path)) return false;
  TlCacheEntry *entry = &entries->items[entries->count++];
  strcpy(entry->path, path);
  entry->size = (size_t)info->st_size;
#if defined(__APPLE__)
  entry->modified.tv_sec = info->st_mtime;
  entry->modified.tv_nsec = info->st_mtimensec;
#else
  entry->modified = info->st_mtim;
#endif
  entries->total_size += entry->size;
  return true;
}

static bool inspect_cache_entry(TlCacheEntries *entries, const char *directory, const char *name) {
  if (!cache_file_name(name)) return true;
  char path[TL_PATH_CAPACITY];
  const int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
  if (written < 0 || (size_t)written >= sizeof(path)) return false;
  struct stat information;
  if (stat(path, &information) != 0 || !S_ISREG(information.st_mode)) return true;
  return add_cache_entry(entries, path, &information);
}

static bool collect_cache_entries(const TlCache *cache, TlCacheEntries *entries) {
  DIR *directory = opendir(cache->directory);
  if (!directory) return false;
  struct dirent *item;
  bool collected = true;
  while ((item = readdir(directory)) != NULL && collected) {
    collected = inspect_cache_entry(entries, cache->directory, item->d_name);
  }
  closedir(directory);
  return collected;
}

static int compare_cache_entries(const void *left, const void *right) {
  const TlCacheEntry *left_entry = left;
  const TlCacheEntry *right_entry = right;
  if (left_entry->modified.tv_sec < right_entry->modified.tv_sec) return -1;
  if (left_entry->modified.tv_sec > right_entry->modified.tv_sec) return 1;
  if (left_entry->modified.tv_nsec < right_entry->modified.tv_nsec) return -1;
  if (left_entry->modified.tv_nsec > right_entry->modified.tv_nsec) return 1;
  return strcmp(left_entry->path, right_entry->path);
}

static bool trim_cache(const TlCache *cache, size_t *total_size) {
  TlCacheEntries entries = {0};
  if (!collect_cache_entries(cache, &entries)) {
    free(entries.items);
    return false;
  }
  qsort(entries.items, entries.count, sizeof(*entries.items), compare_cache_entries);
  for (size_t index = 0; index < entries.count && entries.total_size > cache->max_bytes;
       index += 1) {
    if (unlink(entries.items[index].path) != 0) continue;
    entries.total_size -= entries.items[index].size;
  }
  *total_size = entries.total_size;
  const bool within_limit = entries.total_size <= cache->max_bytes;
  free(entries.items);
  return within_limit;
}

bool tl_cache_init(TlCache *cache, const TlConfig *config, const char *source_path,
                   const char *scan_root) {
  *cache = (TlCache){0};
  cache->lock_fd = -1;
  if (config->cache_max_bytes == 0) return true;
  char root[TL_PATH_CAPACITY];
  if (!repository_root(config, source_path, scan_root, root)) return false;
  if (!create_cache_directory(root, cache->directory)) return false;
  cache->max_bytes = config->cache_max_bytes;
  cache->config_hash = tl_config_hash(config);
  cache->enabled = true;
  return true;
}

static bool read_record_header(FILE *file, size_t *count) {
  char magic[8];
  if (!fgets(magic, sizeof(magic), file)) return false;
  if (strcmp(magic, "TLC1\n") != 0) return false;
  return fscanf(file, "%zu\n", count) == 1 && *count <= 1000000;
}

static bool read_import_record(FILE *file, TlCacheImportRecord *record) {
  const int read = fscanf(file, "%u %zu %zu %zu\n", &record->language, &record->line,
                          &record->column, &record->length);
  return read == 4 && record->language <= TL_LANGUAGE_PROTO && record->length < TL_PATH_CAPACITY;
}

static char *read_cached_specifier(FILE *file, size_t length) {
  char *specifier = malloc(length + 1);
  if (!specifier) return NULL;
  const bool read = fread(specifier, 1, length, file) == length;
  if (!read) {
    free(specifier);
    return NULL;
  }
  specifier[length] = '\0';
  return specifier;
}

static bool read_record_import(FILE *file, TlImportList *imports) {
  TlCacheImportRecord record;
  if (!read_import_record(file, &record)) return false;
  char *specifier = read_cached_specifier(file, record.length);
  if (!specifier) return false;
  const bool newline = fgetc(file) == '\n';
  const bool added = newline && tl_import_list_add(imports, specifier, record.line, record.column,
                                                   (TlLanguage)record.language);
  free(specifier);
  return added;
}

static bool read_record(FILE *file, TlImportList *imports) {
  size_t count;
  if (!read_record_header(file, &count)) return false;
  for (size_t index = 0; index < count; index += 1) {
    if (!read_record_import(file, imports)) return false;
  }
  return true;
}

bool tl_cache_load(TlCache *cache, const char *source_path, const char *content,
                   TlImportList *imports) {
  if (!cache->enabled) return false;
  char path[TL_PATH_CAPACITY];
  const uint64_t key = cache_key(cache, source_path, content);
  if (!record_path(cache, key, path)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  const bool loaded = read_record(file, imports);
  fclose(file);
  if (loaded) utimensat(AT_FDCWD, path, NULL, 0);
  if (!loaded) {
    tl_import_list_free(imports);
    unlink(path);
  }
  return loaded;
}

static bool write_record(FILE *file, const TlImportList *imports) {
  if (fputs("TLC1\n", file) == EOF || fprintf(file, "%zu\n", imports->count) < 0) return false;
  for (size_t index = 0; index < imports->count; index += 1) {
    const TlImport *import = &imports->items[index];
    const size_t length = strlen(import->specifier);
    const int header = fprintf(file, "%u %zu %zu %zu\n", (unsigned)import->language, import->line,
                               import->column, length);
    if (header < 0 || fwrite(import->specifier, 1, length, file) != length) return false;
    if (fputc('\n', file) == EOF) return false;
  }
  return true;
}

static bool temporary_path(const char *path, char *temporary) {
  const int written = snprintf(temporary, TL_PATH_CAPACITY, "%s.tmp.%ld", path, (long)getpid());
  return written >= 0 && written < TL_PATH_CAPACITY;
}

static bool store_record(const char *path, const TlImportList *imports) {
  char temporary[TL_PATH_CAPACITY];
  if (!temporary_path(path, temporary)) return false;
  FILE *file = fopen(temporary, "wb");
  if (!file) return false;
  const bool written = write_record(file, imports);
  const bool closed = fclose(file) == 0;
  if (written && closed && rename(temporary, path) == 0) return true;
  unlink(temporary);
  return false;
}

size_t tl_cache_store(TlCache *cache, const char *source_path, const char *content,
                      const TlImportList *imports) {
  if (!cache->enabled) return 0;
  char path[TL_PATH_CAPACITY];
  const uint64_t key = cache_key(cache, source_path, content);
  if (!record_path(cache, key, path) || !store_record(path, imports)) return 0;
  struct stat information;
  if (stat(path, &information) != 0) return 0;
  return (size_t)information.st_size;
}

static TlCache *find_cache(TlCacheSet *set, const char *directory) {
  for (size_t index = 0; index < set->count; index += 1) {
    if (strcmp(set->items[index].directory, directory) == 0) return &set->items[index];
  }
  return NULL;
}

static bool update_cache_limit(TlCacheSet *set, const TlCache *cache) {
  TlCache *existing = find_cache(set, cache->directory);
  if (!existing) return false;
  if (cache->max_bytes < existing->max_bytes) existing->max_bytes = cache->max_bytes;
  return true;
}

static bool grow_cache_set(TlCacheSet *set) {
  const size_t capacity = set->capacity == 0 ? 2 : set->capacity * 2;
  TlCache *items = realloc(set->items, capacity * sizeof(*items));
  if (!items) return false;
  set->items = items;
  set->capacity = capacity;
  return true;
}

static bool control_path(const TlCache *cache, const char *name, char *path) {
  const int written = snprintf(path, TL_PATH_CAPACITY, "%s/%s", cache->directory, name);
  return written >= 0 && written < TL_PATH_CAPACITY;
}

static bool lock_cache(TlCache *cache) {
  char path[TL_PATH_CAPACITY];
  if (!control_path(cache, ".lock", path)) return false;
  const int descriptor = open(path, O_RDWR | O_CREAT, 0666);
  if (descriptor < 0) return false;
  struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
  if (fcntl(descriptor, F_SETLK, &lock) == 0) {
    cache->lock_fd = descriptor;
    return true;
  }
  close(descriptor);
  return false;
}

static void unlock_cache(TlCache *cache) {
  if (cache->lock_fd < 0) return;
  close(cache->lock_fd);
  cache->lock_fd = -1;
}

static bool control_exists(const TlCache *cache, const char *name) {
  char path[TL_PATH_CAPACITY];
  if (!control_path(cache, name, path)) return false;
  struct stat information;
  return stat(path, &information) == 0;
}

static bool read_tracked_bytes(const TlCache *cache, size_t *bytes) {
  char path[TL_PATH_CAPACITY];
  if (!control_path(cache, ".size", path)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  const bool read = fscanf(file, "%zu", bytes) == 1;
  const bool closed = fclose(file) == 0;
  return read && closed;
}

static bool write_tracked_bytes(const TlCache *cache) {
  char path[TL_PATH_CAPACITY];
  char temporary[TL_PATH_CAPACITY];
  if (!control_path(cache, ".size", path) || !temporary_path(path, temporary)) return false;
  FILE *file = fopen(temporary, "wb");
  if (!file) return false;
  const bool written = fprintf(file, "%zu\n", cache->tracked_bytes) > 0;
  const bool closed = fclose(file) == 0;
  if (written && closed && rename(temporary, path) == 0) return true;
  unlink(temporary);
  return false;
}

static bool mark_cache_dirty(const TlCache *cache) {
  char path[TL_PATH_CAPACITY];
  if (!control_path(cache, ".dirty", path)) return false;
  const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (descriptor < 0) return false;
  return close(descriptor) == 0;
}

static void clear_cache_dirty(const TlCache *cache) {
  char path[TL_PATH_CAPACITY];
  if (control_path(cache, ".dirty", path)) unlink(path);
}

static bool prepare_cache(TlCache *cache) {
  cache->lock_fd = -1;
  if (!lock_cache(cache)) return false;
  const bool dirty = control_exists(cache, ".dirty");
  const bool tracked = read_tracked_bytes(cache, &cache->tracked_bytes);
  const bool must_inspect = dirty || !tracked || cache->tracked_bytes > cache->max_bytes;
  const bool inspected = !must_inspect || trim_cache(cache, &cache->tracked_bytes);
  if (inspected && mark_cache_dirty(cache)) return true;
  unlock_cache(cache);
  return false;
}

static bool finish_cache(TlCache *cache) {
  const bool must_trim = cache->tracked_bytes > cache->max_bytes;
  const bool trimmed = !must_trim || trim_cache(cache, &cache->tracked_bytes);
  if (trimmed && write_tracked_bytes(cache)) clear_cache_dirty(cache);
  unlock_cache(cache);
  return trimmed;
}

bool tl_cache_set_add(TlCacheSet *set, const TlCache *cache) {
  if (!cache->enabled || update_cache_limit(set, cache)) return true;
  if (set->count == set->capacity && !grow_cache_set(set)) return false;
  set->items[set->count] = *cache;
  if (!prepare_cache(&set->items[set->count])) return false;
  set->count += 1;
  return true;
}

void tl_cache_set_record(TlCacheSet *set, const TlCache *cache, size_t bytes) {
  if (bytes == 0) return;
  TlCache *tracked = find_cache(set, cache->directory);
  if (!tracked) return;
  const bool overflow = bytes > SIZE_MAX - tracked->tracked_bytes;
  tracked->tracked_bytes = overflow ? SIZE_MAX : tracked->tracked_bytes + bytes;
}

bool tl_cache_set_trim(TlCacheSet *set) {
  bool trimmed = true;
  for (size_t index = 0; index < set->count; index += 1) {
    if (!finish_cache(&set->items[index])) trimmed = false;
  }
  return trimmed;
}

void tl_cache_set_free(TlCacheSet *set) {
  for (size_t index = 0; index < set->count; index += 1) unlock_cache(&set->items[index]);
  free(set->items);
  *set = (TlCacheSet){0};
}
