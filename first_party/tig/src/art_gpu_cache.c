#include "tig/art_gpu_cache.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tig/art.h"
#include "tig/debug.h"
#include "tig/memory.h"
#include "tig/video.h"

// CE (feature/perf-gpu-accel Phase 2): art_gpu_cache. See header for
// API contract.

#define TIG_ART_GPU_CACHE_NUM_BUCKETS 512u  // power of two
#define TIG_ART_GPU_CACHE_BUCKET_MASK (TIG_ART_GPU_CACHE_NUM_BUCKETS - 1u)
// CE (perf): 96 MB. The original 12 MB thrashed at zoom-out (a z=0.5 view's ~600-880-sprite
// working set, ~40-60MB, didn't fit -> constant LRU re-upload, 120-166ms stutters). But 256 MB
// OVERSHOT and REGRESSED z=1.0: a full-re-render scroll around big building facades references
// hundreds of large textures from a huge resident set every frame, and on a tiled GPU (Apple
// Silicon) the per-frame residency/resolve cost -- plus any mid-batch SDL_DestroyTexture
// eviction sync -- scales with the resident set, pushing iso_redraw to 60-120ms ("dogshit"
// scroll at z=1.0). 96 MB holds the zoom-out working set (no thrash) while keeping the resident
// set small enough to avoid the z=1.0 cost. Override live with ARCANUM_ART_CACHE_MB to A/B.
#define TIG_ART_GPU_CACHE_DEFAULT_BUDGET ((size_t)96 * 1024 * 1024)

typedef struct TigArtGpuCacheEntry {
    tig_art_id_t art_id;
    SDL_Texture* texture;
    size_t bytes;

    // Hash chain (single-linked by bucket).
    struct TigArtGpuCacheEntry* next_in_bucket;

    // LRU doubly-linked list. tail == most-recently used.
    struct TigArtGpuCacheEntry* lru_prev;
    struct TigArtGpuCacheEntry* lru_next;
} TigArtGpuCacheEntry;

static bool tig_art_gpu_cache_initialized = false;
static TigArtGpuCacheEntry* tig_art_gpu_cache_buckets[TIG_ART_GPU_CACHE_NUM_BUCKETS];
static TigArtGpuCacheEntry* tig_art_gpu_cache_lru_head; // least-recently used
static TigArtGpuCacheEntry* tig_art_gpu_cache_lru_tail; // most-recently used
static size_t tig_art_gpu_cache_total_bytes;
static size_t tig_art_gpu_cache_budget_bytes;
static int tig_art_gpu_cache_entry_count;
static uint64_t tig_art_gpu_cache_hits;
static uint64_t tig_art_gpu_cache_misses;
static uint64_t tig_art_gpu_cache_evictions;

// Bit-mixing hash for the 32-bit packed art_id. Arcanum's tile arts cluster
// in a small numeric range so simple modulo would collide a lot; this
// spreads them across the buckets.
static uint32_t tig_art_gpu_cache_hash(tig_art_id_t art_id)
{
    uint32_t h = (uint32_t)art_id;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static void tig_art_gpu_cache_lru_remove(TigArtGpuCacheEntry* entry)
{
    if (entry->lru_prev != NULL) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        tig_art_gpu_cache_lru_head = entry->lru_next;
    }
    if (entry->lru_next != NULL) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        tig_art_gpu_cache_lru_tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

static void tig_art_gpu_cache_lru_push_back(TigArtGpuCacheEntry* entry)
{
    entry->lru_prev = tig_art_gpu_cache_lru_tail;
    entry->lru_next = NULL;
    if (tig_art_gpu_cache_lru_tail != NULL) {
        tig_art_gpu_cache_lru_tail->lru_next = entry;
    } else {
        tig_art_gpu_cache_lru_head = entry;
    }
    tig_art_gpu_cache_lru_tail = entry;
}

static void tig_art_gpu_cache_evict(TigArtGpuCacheEntry* entry)
{
    // Unlink from its hash bucket.
    uint32_t bucket = tig_art_gpu_cache_hash(entry->art_id) & TIG_ART_GPU_CACHE_BUCKET_MASK;
    TigArtGpuCacheEntry** slot = &tig_art_gpu_cache_buckets[bucket];
    while (*slot != NULL && *slot != entry) {
        slot = &(*slot)->next_in_bucket;
    }
    if (*slot == entry) {
        *slot = entry->next_in_bucket;
    }

    tig_art_gpu_cache_lru_remove(entry);

    if (entry->texture != NULL) {
        SDL_DestroyTexture(entry->texture);
    }
    tig_art_gpu_cache_total_bytes -= entry->bytes;
    tig_art_gpu_cache_entry_count--;
    tig_art_gpu_cache_evictions++;

    FREE(entry);
}

// Evict from the LRU head until the new entry will fit within budget.
static void tig_art_gpu_cache_make_room(size_t incoming_bytes)
{
    while (tig_art_gpu_cache_lru_head != NULL
        && tig_art_gpu_cache_total_bytes + incoming_bytes > tig_art_gpu_cache_budget_bytes) {
        tig_art_gpu_cache_evict(tig_art_gpu_cache_lru_head);
    }
}

bool tig_art_gpu_cache_init(size_t budget_bytes)
{
    if (budget_bytes == 0) {
        // CE (perf): live A/B override so the budget can be tuned without a rebuild while
        // we lock the z=1.0-vs-zoom-out sweet spot. e.g. ARCANUM_ART_CACHE_MB=64.
        const char* env = getenv("ARCANUM_ART_CACHE_MB");
        if (env != NULL) {
            long mb = strtol(env, NULL, 10);
            if (mb > 0) {
                budget_bytes = (size_t)mb * 1024 * 1024;
            }
        }
    }
    if (budget_bytes == 0) {
        budget_bytes = TIG_ART_GPU_CACHE_DEFAULT_BUDGET;
    }

    if (!tig_art_gpu_cache_initialized) {
        memset(tig_art_gpu_cache_buckets, 0, sizeof(tig_art_gpu_cache_buckets));
        tig_art_gpu_cache_lru_head = NULL;
        tig_art_gpu_cache_lru_tail = NULL;
        tig_art_gpu_cache_total_bytes = 0;
        tig_art_gpu_cache_entry_count = 0;
        tig_art_gpu_cache_hits = 0;
        tig_art_gpu_cache_misses = 0;
        tig_art_gpu_cache_evictions = 0;
    }

    tig_art_gpu_cache_budget_bytes = budget_bytes;
    tig_art_gpu_cache_initialized = true;

    // If a re-init shrinks the budget below current usage, evict down.
    tig_art_gpu_cache_make_room(0);

    return true;
}

void tig_art_gpu_cache_exit(void)
{
    if (!tig_art_gpu_cache_initialized) {
        return;
    }
    tig_art_gpu_cache_flush();
    tig_art_gpu_cache_initialized = false;
}

void tig_art_gpu_cache_flush(void)
{
    if (!tig_art_gpu_cache_initialized) {
        return;
    }
    while (tig_art_gpu_cache_lru_head != NULL) {
        tig_art_gpu_cache_evict(tig_art_gpu_cache_lru_head);
    }
    tig_art_gpu_cache_total_bytes = 0;
    tig_art_gpu_cache_entry_count = 0;
}

SDL_Texture* tig_art_gpu_cache_get(tig_art_id_t art_id)
{
    if (!tig_art_gpu_cache_initialized) {
        return NULL;
    }

    uint32_t bucket = tig_art_gpu_cache_hash(art_id) & TIG_ART_GPU_CACHE_BUCKET_MASK;
    TigArtGpuCacheEntry* entry;

    // Cache lookup.
    for (entry = tig_art_gpu_cache_buckets[bucket]; entry != NULL; entry = entry->next_in_bucket) {
        if (entry->art_id == art_id) {
            tig_art_gpu_cache_lru_remove(entry);
            tig_art_gpu_cache_lru_push_back(entry);
            tig_art_gpu_cache_hits++;
            return entry->texture;
        }
    }

    // Miss: render the art through its ORIGINAL palette into a scratch CPU
    // buffer, then upload. Using the original-palette render (rather than the
    // engine's working-palette surface) keeps the texture matching the
    // software tile path and stable across ambient/time-of-day tweens -- the
    // texture never needs invalidation because hdr.palette_tbl is immutable.
    // We own the scratch buffer and destroy it once the pixels are in the GPU
    // texture.
    tig_art_gpu_cache_misses++;

    TigVideoBuffer* cpu_buf = NULL;
    if (tig_art_render_original_palette(art_id, &cpu_buf) != TIG_OK || cpu_buf == NULL) {
        return NULL;
    }

    SDL_Texture* tex = tig_video_buffer_upload_to_texture(cpu_buf);
    tig_video_buffer_destroy(cpu_buf);
    if (tex == NULL) {
        return NULL;
    }

    // Size bookkeeping: SDL_GetTextureSize returns floats; we just need an
    // approximate byte count for the LRU budget. Assume 4 bytes/pixel.
    float tw = 0.0f;
    float th = 0.0f;
    SDL_GetTextureSize(tex, &tw, &th);
    size_t entry_bytes = (size_t)(tw * th) * 4u;

    tig_art_gpu_cache_make_room(entry_bytes);

    entry = (TigArtGpuCacheEntry*)MALLOC(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    entry->art_id = art_id;
    entry->texture = tex;
    entry->bytes = entry_bytes;
    entry->next_in_bucket = tig_art_gpu_cache_buckets[bucket];
    tig_art_gpu_cache_buckets[bucket] = entry;
    tig_art_gpu_cache_lru_push_back(entry);
    tig_art_gpu_cache_entry_count++;
    tig_art_gpu_cache_total_bytes += entry_bytes;

    return tex;
}

void tig_art_gpu_cache_stats(TigArtGpuCacheStats* out)
{
    if (out == NULL) {
        return;
    }
    out->entries = tig_art_gpu_cache_entry_count;
    out->bytes = tig_art_gpu_cache_total_bytes;
    out->budget_bytes = tig_art_gpu_cache_budget_bytes;
    out->hits = tig_art_gpu_cache_hits;
    out->misses = tig_art_gpu_cache_misses;
    out->evictions = tig_art_gpu_cache_evictions;
}
