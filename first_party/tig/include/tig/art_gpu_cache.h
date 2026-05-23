#ifndef TIG_ART_GPU_CACHE_H_
#define TIG_ART_GPU_CACHE_H_

#include "tig/art.h"
#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// CE (feature/perf-gpu-accel Phase 2): lazy art-texture GPU cache.
//
// Memoizes the result of `tig_art_video_buffer_get(art_id)` ->
// `tig_video_buffer_upload_to_texture(...)`. The first lookup for an
// art_id uploads its CPU SDL_Surface to a new SDL_Texture; subsequent
// lookups return the cached texture. An LRU eviction policy keeps the
// total resident texture memory under a budget (default 12MB, covers
// ~1000 tile arts at 78x40x4 = 12480 bytes each).
//
// Used by Phase 3's tile_draw_iso GPU path to source per-tile textures
// for `tig_video_buffer_blit_gpu`. Stays uncalled in Phase 2; the
// gamelib sanity check exercises init/exit only.
//
// The cache assumes the renderer (tig_video_init) and the art system
// (tig_art_init) are both up before tig_art_gpu_cache_init runs.

typedef struct TigArtGpuCacheStats {
    int entries;
    size_t bytes;
    size_t budget_bytes;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} TigArtGpuCacheStats;

// Initialize the cache with the given memory budget (in bytes). Returns
// true on success. Safe to call repeatedly -- a second init resizes the
// budget without dropping existing entries (entries above the new budget
// are evicted LRU-first).
bool tig_art_gpu_cache_init(size_t budget_bytes);

// Tear down the cache: destroys all cached SDL_Textures and resets stats.
void tig_art_gpu_cache_exit(void);

// Drop all cached entries without tearing down the bookkeeping. Useful
// after a palette change or art reload.
void tig_art_gpu_cache_flush(void);

// Look up (or upload) the GPU texture for an art_id. Returns the cached
// texture on a hit, uploads on a miss, NULL on hard failure (e.g., the
// art_id has no CPU video buffer or texture creation failed). The
// returned pointer is owned by the cache and remains valid until the
// next eviction or flush.
SDL_Texture* tig_art_gpu_cache_get(tig_art_id_t art_id);

void tig_art_gpu_cache_stats(TigArtGpuCacheStats* out);

#ifdef __cplusplus
}
#endif

#endif // TIG_ART_GPU_CACHE_H_
