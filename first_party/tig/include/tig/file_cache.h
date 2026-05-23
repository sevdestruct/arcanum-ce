#ifndef TIG_FILE_CACHE_H_
#define TIG_FILE_CACHE_H_

#include <time.h>

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Represents cached file.
typedef struct TigFileCacheEntry {
    void* data;
    int size;
    int index;
    char* path;
} TigFileCacheEntry;

// An item in file cache.
typedef struct TigFileCacheItem {
    TigFileCacheEntry entry;
    int refcount;
    time_t timestamp;
} TigFileCacheItem;

// A collection of cached files.
typedef struct TigFileCache {
    int signature;
    int capacity;
    int max_size;
    int bytes;
    int items_count;
    TigFileCacheItem* items;
} TigFileCache;

// Initializes file cache system.
int tig_file_cache_init(TigInitInfo* init_info);

// Shutdowns file cache system.
void tig_file_cache_exit(void);

// Evicts ununsed entries from cache.
void tig_file_cache_flush(TigFileCache* cache);

// Creates a new file cache.
//
// - `capacity`: total nubmer of files this cache object can manage.
// - `max_size`: max size of files this cache object can manage.
TigFileCache* tig_file_cache_create(int capacity, int max_size);

// Destroys the given file cache.
//
// NOTE: It's an error to have acquired but not released entries, which is a
// memory leak, but this is neither checked, nor enforced.
void tig_file_cache_destroy(TigFileCache* cache);

// Fetches file with given path from cache loading it from the file system if
// needed.
TigFileCacheEntry* tig_file_cache_acquire(TigFileCache* cache, const char* path);

// Lookup-only variant: returns an acquired entry if `path` is already in
// the cache, or NULL if not. Never reads from disk. Used by callers that
// want to handle the miss themselves (e.g. dispatch an async load instead
// of blocking the main thread on file I/O).
TigFileCacheEntry* tig_file_cache_lookup(TigFileCache* cache, const char* path);

// Read the file at `path` into a freshly MALLOC'd buffer (caller takes
// ownership). Returns true on success and writes the buffer + size out;
// returns false (and leaves outputs untouched) on open failure. Does
// NOT touch any cache state — safe to call from a worker thread. Used
// by async loaders that then hand the buffer to tig_file_cache_insert_data
// on the main thread.
bool tig_file_cache_read_contents_into(const char* path, void** data, int* size);

// Releases access to given entry.
void tig_file_cache_release(TigFileCache* cache, TigFileCacheEntry* entry);

// Inserts pre-loaded file data into the cache, returning an acquired entry
// (refcount = 1). Used by async loaders that read the file off the main
// thread and want to hand the result to the cache. Takes ownership of
// `data` — caller must not free it after a successful call (cache will
// FREE() it on eviction). On cache hit (path already present), the
// pre-loaded `data` is FREEd by this function and the existing entry is
// returned. On failure (no free slot, allocation error), `data` is FREEd
// and the function returns NULL.
TigFileCacheEntry* tig_file_cache_insert_data(TigFileCache* cache,
    const char* path, void* data, int size);

#ifdef __cplusplus
}
#endif

#endif /* TIG_FILE_CACHE_H_ */
