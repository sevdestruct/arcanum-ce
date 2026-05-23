#include "tig/sound.h"

#include <stdio.h>

#include <SDL3/SDL.h>
#include <mss_compat.h>

#include "tig/core.h"
#include "tig/debug.h"
#include "tig/file.h"
#include "tig/file_cache.h"
#include "tig/memory.h"
#include "tig/timer.h"

#define FIRST_VOICE_HANDLE 0
#define FIRST_MUSIC_HANDLE 2
#define FIRST_EFFECT_HANDLE 6
#define SOUND_HANDLE_MAX 60

typedef unsigned int TigSoundFlags;

#define TIG_SOUND_STREAMED 0x01u
#define TIG_SOUND_MEMORY 0x02u
#define TIG_SOUND_FADE_OUT 0x04u
#define TIG_SOUND_WAIT 0x08u
#define TIG_SOUND_FADE_IN 0x10u
#define TIG_SOUND_DESTROY 0x20u
#define TIG_SOUND_EFFECT 0x80u
#define TIG_SOUND_MUSIC 0x100u
#define TIG_SOUND_VOICE 0x200u

typedef struct TigSound {
    /* 0000 */ unsigned char active; // boolean
    /* 0004 */ TigSoundFlags flags;
    /* 0008 */ int fade_duration;
    /* 000C */ int fade_step;
    /* 0010 */ int loops;
    /* 0014 */ HSTREAM audio_stream;
    /* 0018 */ HAUDIO audio_handle;
    /* 001C */ tig_sound_handle_t next_sound_handle;
    /* 0020 */ char path[TIG_MAX_PATH];
    /* 0124 */ int id;
    /* 0128 */ int volume;
    /* 012C */ int extra_volume;
    /* 0130 */ TigFileCacheEntry* file_cache_entry;
    /* 0134 */ bool positional;
    /* 0138 */ int64_t positional_x;
    /* 0140 */ int64_t positional_y;
    /* 0148 */ TigSoundPositionalSize positional_size;
} TigSound;

static void tig_sound_update(void);
static void tig_sound_stop_from_destroy(tig_sound_handle_t sound_handle, int fade_duration);
static int tig_sound_acquire_handle(TigSoundType type);
static void tig_sound_reset_sound(TigSound* sound);
static int tig_sound_play_streamed(tig_sound_handle_t sound_handle, const char* name, int loops, int fade_duration, tig_sound_handle_t prev_sound_handle);

// Convenience.
static inline bool sound_handle_is_valid(tig_sound_handle_t sound_handle)
{
    return sound_handle >= 0 && sound_handle < SOUND_HANDLE_MAX;
}

// 0x5C246C
static TigSoundFlags tig_sound_type_flags[TIG_SOUND_TYPE_COUNT] = {
    TIG_SOUND_EFFECT,
    TIG_SOUND_MUSIC,
    TIG_SOUND_VOICE,
};

// 0x62B2C0
static TigSoundFilePathResolver tig_sound_file_path_resolver;

// 0x62B328
static TigSound tig_sounds[SOUND_HANDLE_MAX];

// 0x6301E8
static tig_sound_handle_t tig_sound_next_effect_handle;

// 0x6301EC
static bool tig_sound_initialized;

// 0x6301F0
static TigFileCache* tig_sound_cache;

// 0x6301F4
static int tig_sound_effects_volume;

// === Async first-play loader ===
//
// On a cache miss, `tig_sound_play` dispatches a detached worker thread to
// read the file from disk, then queues the loaded data for the main thread
// to insert into the cache + play. Eliminates the synchronous ~100-150ms
// disk-load-and-decode hitch the first time any sound plays (after the
// 256-file cache thrashing fix in `38fe0a04`, this is the residual sound
// hitch). Sound plays slightly late on first encounter; subsequent plays
// hit the cache and are immediate.
//
// Worker threads only do file I/O (tig_file_fopen + read). The cache
// insert and AIL_quick_load_mem + AIL_quick_play stay on the main thread
// because (a) tig_file_cache has no locks and (b) the SDL_mixer mixer
// object is shared global state. Result: ~0 thread-safety surface area
// beyond the completion-queue mutex.
typedef struct TigSoundAsyncLoad {
    char path[TIG_MAX_PATH];
    tig_sound_handle_t handle;
    int id;
    int loops;
    int volume;
    int extra_volume;
    void* data;     // file contents (worker malloc; main FREEs after cache insert OR on failure)
    int size;
    int success;    // 1 on successful read, 0 on failure (file missing, etc.)
    struct TigSoundAsyncLoad* next;
} TigSoundAsyncLoad;

static SDL_Mutex* tig_sound_async_mutex;
static TigSoundAsyncLoad* tig_sound_async_completion_head;
// Opt-out flag: caller may turn off async loading via the cfg key. Default
// is on because the data showed first-play hitches as the residual issue
// after every other optimization.
static bool tig_sound_async_enabled = true;

void tig_sound_async_set_enabled(bool enabled)
{
    tig_sound_async_enabled = enabled;
}

static int SDLCALL tig_sound_async_worker(void* arg)
{
    TigSoundAsyncLoad* req = (TigSoundAsyncLoad*)arg;
    void* buf = NULL;
    int size = 0;
    if (tig_file_cache_read_contents_into(req->path, &buf, &size)) {
        req->data = buf;
        req->size = size;
        req->success = 1;
    } else {
        req->data = NULL;
        req->size = 0;
        req->success = 0;
    }
    // Push onto completion stack — main thread drains in tig_sound_update.
    SDL_LockMutex(tig_sound_async_mutex);
    req->next = tig_sound_async_completion_head;
    tig_sound_async_completion_head = req;
    SDL_UnlockMutex(tig_sound_async_mutex);
    return 0;
}

// Called from tig_sound_update (which fires at most every 100ms) to drain
// any completed async loads and start their playback. Has to be on the
// main thread because of tig_file_cache + SDL_mixer thread-safety.
static void tig_sound_async_drain(void)
{
    SDL_LockMutex(tig_sound_async_mutex);
    TigSoundAsyncLoad* head = tig_sound_async_completion_head;
    tig_sound_async_completion_head = NULL;
    SDL_UnlockMutex(tig_sound_async_mutex);

    while (head != NULL) {
        TigSoundAsyncLoad* next = head->next;
        if (head->success && head->data != NULL) {
            // Insert into cache (takes ownership of head->data on success;
            // on cache-hit-collision, FREEs our copy and returns the
            // existing entry; on failure FREEs our copy and returns NULL).
            TigFileCacheEntry* entry = tig_file_cache_insert_data(
                tig_sound_cache, head->path, head->data, head->size);
            head->data = NULL;  // ownership transferred
            if (entry != NULL && entry->data != NULL
                && sound_handle_is_valid(head->handle)) {
                TigSound* snd = &(tig_sounds[head->handle]);
                // Two guards:
                // 1. id mismatch — handle reassigned since dispatch; the
                //    new owner gets its own dispatch (or sync load), this
                //    one is stale.
                // 2. slot busy — another sound is already active on this
                //    handle. Don't stomp it. The first-play cache miss
                //    that the user wanted to hear is gone (this drain
                //    fires up to ~100ms after dispatch); silently dropping
                //    is better than killing a sound that's currently
                //    playing.
                if (snd->id == head->id && snd->active == 0) {
                    snd->file_cache_entry = entry;
                    snd->audio_handle = AIL_quick_load_mem(entry->data, entry->size);
                    AIL_quick_set_volume(snd->audio_handle, head->volume, head->extra_volume);
                    AIL_quick_play(snd->audio_handle, head->loops);
                    snd->flags |= TIG_SOUND_MEMORY;
                    snd->active = 1;
                } else {
                    tig_file_cache_release(tig_sound_cache, entry);
                }
            }
        } else if (head->data != NULL) {
            FREE(head->data);
        }
        FREE(head);
        head = next;
    }
}

// 0x532D40
int tig_sound_init(TigInitInfo* init_info)
{
    // COMPAT: Load `mss32.dll`.
    mss_compat_init();

    tig_sound_initialized = false;
    tig_sound_next_effect_handle = FIRST_EFFECT_HANDLE;

    if (AIL_quick_startup(1, 0, 22050, 16, 2)) {
        tig_sound_initialized = true;
    }

    tig_sound_set_file_path_resolver(init_info->sound_file_path_resolver);

    // CE: Bumped from the original 20 files / 1MB sizing — that thrashes
    // constantly on modern hardware where every new combat / dialog /
    // terrain footstep evicts another sound, and the next time any of them
    // plays we pay ~400ms for re-load + re-decode on the main thread (see
    // megahitch log entries). 256 files / 64MB easily fits in RAM on any
    // machine that can run a 2026 build, gives effectively-infinite
    // caching for the unique-sound set of a typical play session, and
    // eliminates the first-play stutter for any sound played more than
    // once per game launch.
    tig_sound_cache = tig_file_cache_create(256, 64 * 1024 * 1024);

    tig_sound_async_mutex = SDL_CreateMutex();
    tig_sound_async_completion_head = NULL;

    return TIG_OK;
}

// 0x532DB0
void tig_sound_exit(void)
{
    if (tig_sound_initialized) {
        tig_sound_initialized = false;
        tig_sound_stop_all(0);
        // Drop any pending completions before we destroy the cache + mixer.
        // Detached worker threads might still post completions after this,
        // but the early-return guard at the top of tig_sound_async_drain
        // (and `tig_sound_initialized = false` above) prevents further
        // drains. Worst case: a leaked buffer from a thread completing
        // after exit, which the OS reclaims on process tear-down.
        if (tig_sound_async_mutex != NULL) {
            SDL_LockMutex(tig_sound_async_mutex);
            TigSoundAsyncLoad* head = tig_sound_async_completion_head;
            tig_sound_async_completion_head = NULL;
            SDL_UnlockMutex(tig_sound_async_mutex);
            while (head != NULL) {
                TigSoundAsyncLoad* next = head->next;
                if (head->data != NULL) FREE(head->data);
                FREE(head);
                head = next;
            }
            SDL_DestroyMutex(tig_sound_async_mutex);
            tig_sound_async_mutex = NULL;
        }
        tig_file_cache_destroy(tig_sound_cache);
        AIL_quick_shutdown();
    }

    // COMPAT: Unload `mss32.dll`.
    mss_compat_exit();
}

// 0x532DE0
void tig_sound_ping(void)
{
    // 0x739E84
    static tig_timestamp_t tig_sound_ping_timestamp;

    if (!tig_sound_initialized) {
        return;
    }

    if (tig_ping_timestamp < tig_sound_ping_timestamp - 1000) {
        tig_sound_ping_timestamp = tig_ping_timestamp;
    }

    if (tig_ping_timestamp > tig_sound_ping_timestamp + 1000) {
        tig_sound_ping_timestamp = tig_ping_timestamp + 100;
        tig_sound_update();
    } else if (tig_ping_timestamp >= tig_sound_ping_timestamp) {
        tig_sound_ping_timestamp += 100;
        tig_sound_update();
    }
}

// 0x532E30
void tig_sound_update(void)
{
    int index;
    TigSound* snd;
    int new_volume;

    if (!tig_sound_initialized) {
        return;
    }

    // Drain any async-loaded sounds whose disk read finished since the
    // last tick. Cheap (typically empty or 1-2 entries).
    if (tig_sound_async_mutex != NULL) {
        tig_sound_async_drain();
    }

    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        snd = &(tig_sounds[index]);
        if (snd->active != 0 && (snd->flags & TIG_SOUND_WAIT) == 0) {
            if ((snd->flags & TIG_SOUND_FADE_OUT) != 0) {
                snd->fade_step++;
                if (snd->fade_step <= snd->fade_duration) {
                    new_volume = snd->volume * (snd->fade_duration - snd->fade_step) / snd->fade_duration;
                } else {
                    snd->flags &= ~TIG_SOUND_FADE_OUT;
                    if (snd->next_sound_handle >= 0) {
                        TigSound* next_snd = &(tig_sounds[snd->next_sound_handle]);
                        next_snd->flags &= ~TIG_SOUND_WAIT;
                        next_snd->flags |= TIG_SOUND_FADE_IN;
                    }
                    new_volume = 0;
                }

                if ((snd->flags & TIG_SOUND_STREAMED) != 0) {
                    AIL_set_stream_volume(snd->audio_stream, new_volume);
                    snd->flags |= TIG_SOUND_DESTROY;
                } else if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
                    AIL_quick_set_volume(snd->audio_handle, (new_volume * snd->volume / 128), snd->extra_volume);
                    snd->flags |= TIG_SOUND_DESTROY;
                } else {
                    snd->flags |= TIG_SOUND_DESTROY;
                }
            } else if ((snd->flags & TIG_SOUND_FADE_IN) != 0) {
                if (snd->fade_step == 0) {
                    AIL_start_stream(snd->audio_stream);
                }

                snd->fade_step++;

                new_volume = snd->volume;
                if (snd->fade_step <= snd->fade_duration) {
                    new_volume = snd->fade_step * snd->volume / snd->fade_duration;
                } else {
                    snd->flags &= ~TIG_SOUND_FADE_IN;
                }

                if ((snd->flags & TIG_SOUND_STREAMED) != 0) {
                    AIL_set_stream_volume(snd->audio_stream, new_volume);
                } else if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
                    AIL_quick_set_volume(snd->audio_handle, new_volume, 64);
                }
            } else {
                if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
                    if (AIL_quick_status(snd->audio_handle) == QSTAT_DONE) {
                        snd->flags |= TIG_SOUND_DESTROY;
                    }
                }

                if ((snd->flags & TIG_SOUND_DESTROY) != 0) {
                    tig_sound_destroy(index);
                }
            }
        }
    }
}

// 0x533000
void tig_sound_set_file_path_resolver(TigSoundFilePathResolver func)
{
    tig_sound_file_path_resolver = func;
}

// 0x533010
bool tig_sound_is_initialized(void)
{
    return tig_sound_initialized;
}

// 0x533020
void tig_sound_stop(tig_sound_handle_t sound_handle, int fade_duration)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    snd->flags &= ~(TIG_SOUND_WAIT | TIG_SOUND_FADE_IN);
    if ((snd->flags & TIG_SOUND_FADE_OUT) == 0) {
        snd->flags |= TIG_SOUND_FADE_OUT;
        snd->fade_duration = abs(fade_duration);
        snd->fade_step = 0;
    }
}

// NOTE: Purpose is unclear, used only from `tig_sound_destroy`.
//
// 0x533080
void tig_sound_stop_from_destroy(tig_sound_handle_t sound_handle, int fade_duration)
{
    tig_sound_stop(sound_handle, fade_duration);
}

// 0x5330A0
void tig_sound_stop_all(int fade_duration)
{
    int index;

    if (!tig_sound_initialized) {
        return;
    }

    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        if (tig_sounds[index].active != 0) {
            tig_sound_stop(index, fade_duration);
        }
    }

    tig_sound_next_effect_handle = FIRST_EFFECT_HANDLE;

    tig_sound_update();
}

// 0x5330F0
int tig_sound_create(tig_sound_handle_t* sound_handle, TigSoundType type)
{
    if (!tig_sound_initialized) {
        *sound_handle = TIG_SOUND_HANDLE_INVALID;
        return TIG_OK;
    }

    *sound_handle = tig_sound_acquire_handle(type);
    if (*sound_handle != TIG_SOUND_HANDLE_INVALID) {
        tig_sounds[*sound_handle].flags |= tig_sound_type_flags[type];
        tig_sounds[*sound_handle].loops = 1;
        tig_sounds[*sound_handle].volume = 127;
        tig_sounds[*sound_handle].extra_volume = 64;
    }

    return TIG_OK;
}

// 0x5331A0
int tig_sound_acquire_handle(TigSoundType type)
{
    int index;
    TigSound* snd;

    switch (type) {
    case TIG_SOUND_TYPE_EFFECT:
        for (index = 0; index < SOUND_HANDLE_MAX; index++) {
            snd = &(tig_sounds[tig_sound_next_effect_handle]);
            if (snd->active == 0) {
                tig_sound_reset_sound(snd);
                snd->active = 1;
                return tig_sound_next_effect_handle;
            }

            if (++tig_sound_next_effect_handle >= SOUND_HANDLE_MAX) {
                tig_sound_next_effect_handle = FIRST_EFFECT_HANDLE;
            }
        }
        break;
    case TIG_SOUND_TYPE_MUSIC:
        for (index = FIRST_MUSIC_HANDLE; index < FIRST_EFFECT_HANDLE; index++) {
            snd = &(tig_sounds[index]);
            if (snd->active == 0) {
                tig_sound_reset_sound(snd);
                snd->active = 1;
                return index;
            }
        }
        break;
    case TIG_SOUND_TYPE_VOICE:
        for (index = FIRST_VOICE_HANDLE; index < FIRST_MUSIC_HANDLE; index++) {
            snd = &(tig_sounds[index]);
            if (snd->active == 0) {
                tig_sound_reset_sound(snd);
                snd->active = 1;
                return index;
            }
        }
        break;
    default:
        // Should be unreachable.
        abort();
    }

    tig_debug_printf("No sound handle available for sound type %d! Current sounds are:\n");
    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        // FIXME: This approach does not respect lower and upper bounds for the
        // requested sound type. Let's say we're out of music handles, it will
        // dump voice and sound handles, which (1) cannot store music handles,
        // and (2) can be empty or obsolete value (no check for `active`).
        tig_debug_printf("%s\n", tig_sounds[index].path);
    }

    return TIG_SOUND_HANDLE_INVALID;
}

// 0x5332F0
void tig_sound_reset_sound(TigSound* sound)
{
    memset(sound, 0, sizeof(*sound));
    sound->next_sound_handle = TIG_SOUND_HANDLE_INVALID;
}

// 0x533310
void tig_sound_destroy(tig_sound_handle_t sound_handle)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->active != 0) {
        tig_sound_stop_from_destroy(sound_handle, 0);

        if ((snd->flags & TIG_SOUND_STREAMED) != 0) {
            AIL_close_stream(snd->audio_stream);
            snd->active = 0;
        } else if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
            AIL_quick_unload(snd->audio_handle);
            tig_file_cache_release(tig_sound_cache, snd->file_cache_entry);
            snd->active = 0;
        } else {
            snd->active = 0;
        }
    }
}

// 0x5333A0
int tig_sound_play(tig_sound_handle_t sound_handle, const char* path, int id)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return TIG_OK;
    }

    // FIXME: No `SOUND_HANDLE_MAX` guard.
    if (!(sound_handle >= 0)) {
        return TIG_OK;
    }

    snd = &(tig_sounds[sound_handle]);
    strcpy(snd->path, path);
    // Lookup-only first — if the sound is cached we play it now (fast
    // path, identical to pre-async behavior). On miss, EITHER dispatch
    // an async load (async path), OR fall back to the original
    // synchronous load (if async is disabled or thread create fails).
    snd->file_cache_entry = tig_file_cache_lookup(tig_sound_cache, path);

    if (snd->file_cache_entry != NULL && snd->file_cache_entry->data != NULL) {
        snd->audio_handle = AIL_quick_load_mem(snd->file_cache_entry->data, snd->file_cache_entry->size);
        AIL_quick_set_volume(snd->audio_handle, snd->volume, snd->extra_volume);
        AIL_quick_play(snd->audio_handle, snd->loops);
        snd->flags |= TIG_SOUND_MEMORY;
        snd->id = id;
        return TIG_OK;
    }

    snd->file_cache_entry = NULL;
    snd->id = id;

    if (tig_sound_async_enabled && tig_sound_async_mutex != NULL) {
        TigSoundAsyncLoad* req = (TigSoundAsyncLoad*)MALLOC(sizeof(*req));
        if (req != NULL) {
            size_t n = strlen(path);
            if (n >= sizeof(req->path)) n = sizeof(req->path) - 1;
            memcpy(req->path, path, n);
            req->path[n] = '\0';
            req->handle = sound_handle;
            req->id = id;
            req->loops = snd->loops;
            req->volume = snd->volume;
            req->extra_volume = snd->extra_volume;
            req->data = NULL;
            req->size = 0;
            req->success = 0;
            req->next = NULL;
            SDL_Thread* thread = SDL_CreateThread(tig_sound_async_worker, "tig_snd_async", req);
            if (thread != NULL) {
                SDL_DetachThread(thread);
                // Sound is pending — DON'T touch active/flags/audio_handle/
                // file_cache_entry here. Those stay zeroed until the drain
                // on the main thread inserts the loaded buffer into the
                // cache and starts playback. Any other code that checks
                // `snd->active != 0` will correctly treat the slot as
                // not-yet-playing (tig_sound_stop is a no-op, fade logic
                // skips it, etc.) — no NULL deref window.
                snd->active = 0;
                return TIG_OK;
            }
            FREE(req);
        }
    }

    // Fallback: synchronous load (pre-async behavior). Used when async
    // is disabled, mutex isn't set up, allocation failed, or thread
    // creation failed.
    snd->file_cache_entry = tig_file_cache_acquire(tig_sound_cache, path);
    if (snd->file_cache_entry->data != NULL) {
        snd->audio_handle = AIL_quick_load_mem(snd->file_cache_entry->data, snd->file_cache_entry->size);
        AIL_quick_set_volume(snd->audio_handle, snd->volume, snd->extra_volume);
        AIL_quick_play(snd->audio_handle, snd->loops);
        snd->flags |= TIG_SOUND_MEMORY;
    } else {
        snd->active = 0;
    }

    return TIG_OK;
}

// 0x533480
int tig_sound_play_id(tig_sound_handle_t sound_handle, int id)
{
    char path[TIG_MAX_PATH];

    if (!tig_sound_initialized) {
        return TIG_OK;
    }

    tig_sound_file_path_resolver(id, path, sizeof(path));

    return tig_sound_play(sound_handle, path, id);
}

// 0x5334D0
int tig_sound_play_streamed(tig_sound_handle_t sound_handle, const char* name, int loops, int fade_duration, tig_sound_handle_t prev_sound_handle)
{
    char path[TIG_MAX_PATH];
    TigSound* snd;
    HDIGDRIVER dig;
    TigSound* prev_snd;

    if (!tig_sound_initialized) {
        return TIG_OK;
    }

    if (!tig_file_extract(name, path)) {
        return TIG_OK;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return TIG_OK;
    }

    snd = &(tig_sounds[sound_handle]);
    snd->fade_duration = abs(fade_duration);
    snd->fade_step = 0;

    AIL_quick_handles(&dig, NULL, NULL);

    snd->flags |= TIG_SOUND_STREAMED;
    snd->audio_stream = AIL_open_stream(dig, path, 0);
    if (snd->audio_stream == NULL) {
        snd->active = 0;
        return TIG_OK;
    }

    strcpy(snd->path, name);
    snd->loops = loops;

    AIL_set_stream_loop_count(snd->audio_stream, loops);

    // FIXME: Fade duration checked twice.
    // FIXME: Prev sound handle should be validated with `sound_handle_is_valid`.
    if (fade_duration != 0 && prev_sound_handle >= 0 && fade_duration > 0) {
        snd->flags |= TIG_SOUND_WAIT;
    } else {
        snd->flags |= TIG_SOUND_FADE_IN;
    }

    // FIXME: Prev sound handle should be validated with `sound_handle_is_valid`.
    if (prev_sound_handle >= 0) {
        prev_snd = &(tig_sounds[prev_sound_handle]);
        if ((prev_snd->flags & TIG_SOUND_FADE_IN) != 0) {
            prev_snd->flags &= ~TIG_SOUND_FADE_IN;
        }

        if ((prev_snd->flags & TIG_SOUND_WAIT) != 0) {
            prev_snd->flags &= ~TIG_SOUND_WAIT;
        }

        prev_snd->flags |= TIG_SOUND_FADE_OUT;
        prev_snd->fade_duration = abs(fade_duration);
        prev_snd->fade_step = 0;
        prev_snd->next_sound_handle = sound_handle;
    }

    return TIG_OK;
}

// 0x533680
int tig_sound_play_streamed_indefinitely(tig_sound_handle_t sound_handle, const char* name, int fade_duration, tig_sound_handle_t prev_sound_handle)
{
    return tig_sound_play_streamed(sound_handle, name, 0, fade_duration, prev_sound_handle);
}

// 0x5336A0
int tig_sound_play_streamed_once(tig_sound_handle_t sound_handle, const char* name, int fade_duration, tig_sound_handle_t prev_sound_handle)
{
    return tig_sound_play_streamed(sound_handle, name, 1, fade_duration, prev_sound_handle);
}

// FIXME: Should return by reference, otherwise there is no way to communicate
// error.
//
// 0x5336C0
int tig_sound_get_loops(tig_sound_handle_t sound_handle)
{
    if (!tig_sound_initialized) {
        // NOTE: Probably `TIG_ERR_NOT_INITIALIZED`.
        return 1;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return 0;
    }

    return tig_sounds[sound_handle].loops;
}

// 0x533700
void tig_sound_set_loops(tig_sound_handle_t sound_handle, int loops)
{
    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    tig_sounds[sound_handle].loops = loops;
}

// FIXME: Should return by reference, otherwise there is no way to communicate
// error.
//
// 0x533730
int tig_sound_get_volume(tig_sound_handle_t sound_handle)
{
    if (!tig_sound_initialized) {
        return 0;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return 0;
    }

    return tig_sounds[sound_handle].volume;
}

// 0x533760
void tig_sound_set_volume(tig_sound_handle_t sound_handle, int volume)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->volume != volume) {
        snd->volume = volume;

        if ((snd->flags & TIG_SOUND_STREAMED) != 0) {
            AIL_set_stream_volume(snd->audio_stream, volume);
        } else if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
            AIL_quick_set_volume(snd->audio_handle, volume, snd->extra_volume);
        }
    }
}

// 0x5337D0
void tig_sound_set_volume_by_type(TigSoundType type, int volume)
{
    int index;
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        snd = &(tig_sounds[index]);
        if (snd->active != 0) {
            if ((tig_sound_type_flags[type] & snd->flags) != 0) {
                tig_sound_set_volume(index, volume);
            }
        }
    }

    if (type == TIG_SOUND_TYPE_EFFECT) {
        tig_sound_quick_play_set_volume(volume);
    }
}

// FIXME: Should return by reference, otherwise there is no way to communicate
// error.
//
// 0x533830
TigSoundType tig_sound_get_type(tig_sound_handle_t sound_handle)
{
    TigSoundType type;

    if (!tig_sound_initialized) {
        return 0;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return 0;
    }

    for (type = 0; type < TIG_SOUND_TYPE_COUNT; type++) {
        if ((tig_sounds[sound_handle].flags & tig_sound_type_flags[type]) != 0) {
            return type;
        }
    }

    return 0;
}

// 0x533880
void tig_sound_set_type(tig_sound_handle_t sound_handle, TigSoundType type)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    snd->flags &= ~(TIG_SOUND_EFFECT | TIG_SOUND_MUSIC | TIG_SOUND_VOICE);
    snd->flags |= tig_sound_type_flags[type];
}

// 0x5338D0
TigSoundPositionalSize tig_sound_get_positional_size(tig_sound_handle_t sound_handle)
{
    if (!tig_sound_initialized) {
        return TIG_SOUND_SIZE_LARGE;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return TIG_SOUND_SIZE_LARGE;
    }

    return tig_sounds[sound_handle].positional_size;
}

// 0x533910
void tig_sound_set_positional_size(tig_sound_handle_t sound_handle, TigSoundPositionalSize size)
{
    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    tig_sounds[sound_handle].positional_size = size;
}

// FIXME: Should return by reference, otherwise there is no way to communicate
// error.
//
// 0x533940
int tig_sound_get_extra_volume(tig_sound_handle_t sound_handle)
{
    if (!tig_sound_initialized) {
        return 64;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return 64;
    }

    return tig_sounds[sound_handle].extra_volume;
}

// 0x533980
void tig_sound_set_extra_volume(tig_sound_handle_t sound_handle, int extra_volume)
{
    TigSound* snd;

    if (!tig_sound_initialized) {
        return;
    }

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->extra_volume != extra_volume) {
        snd->extra_volume = extra_volume;

        if ((snd->flags & TIG_SOUND_STREAMED) == 0
            && (snd->flags & TIG_SOUND_MEMORY) != 0) {
            AIL_quick_set_volume(snd->audio_handle, snd->volume, extra_volume);
        }
    }
}

// 0x5339E0
bool tig_sound_is_playing_id(int id)
{
    int index;
    TigSound* snd;

    if (!tig_sound_initialized) {
        return false;
    }

    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        snd = &(tig_sounds[index]);
        if (snd->active != 0 && snd->id == id) {
            return true;
        }
    }

    return false;
}

// 0x533A20
bool tig_sound_is_playing(tig_sound_handle_t sound_handle)
{
    TigSound* snd;

    // FIXME: No `tig_sound_initialized` guard.

    if (!sound_handle_is_valid(sound_handle)) {
        return false;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->active != 0) {
        if ((snd->flags & TIG_SOUND_MEMORY) != 0) {
            if (AIL_quick_status(snd->audio_handle) == QSTAT_PLAYING) {
                return true;
            }
        }

        if ((snd->flags & TIG_SOUND_STREAMED) != 0) {
            if (AIL_stream_status(snd->audio_stream) == SMP_PLAYING) {
                return true;
            }
        }
    }

    return false;
}

// 0x533A90
bool tig_sound_is_active(tig_sound_handle_t sound_handle)
{
    // FIXME: No `tig_sound_initialized` guard.

    if (!sound_handle_is_valid(sound_handle)) {
        return false;
    }

    return tig_sounds[sound_handle].active != 0;
}

// 0x533AC0
void tig_sound_cache_flush(void)
{
    if (tig_sound_initialized) {
        tig_file_cache_flush(tig_sound_cache);
    }
}

// 0x533AE0
const char* tig_sound_cache_stats(void)
{
    // 0x62B2C4
    static char buffer[100];

    SDL_snprintf(buffer, sizeof(buffer),
        "Sound Cache: %u items, %u bytes",
        tig_sound_cache->items_count,
        tig_sound_cache->bytes);
    return buffer;
}

// 0x533B10
void tig_sound_set_position(tig_sound_handle_t sound_handle, int64_t x, int64_t y)
{
    TigSound* snd;

    // FIXME: No `tig_sound_initialized` guard.

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    snd->positional_x = x;
    snd->positional_y = y;
    snd->positional = true;
}

// 0x533B60
void tig_sound_get_position(tig_sound_handle_t sound_handle, int64_t* x, int64_t* y)
{
    TigSound* snd;

    // FIXME: No `tig_sound_initialized` guard.

    if (!sound_handle_is_valid(sound_handle)) {
        return;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->active != 0 && snd->positional) {
        *x = snd->positional_x;
        *y = snd->positional_y;
    }
}

// 0x533BC0
bool tig_sound_is_positional(tig_sound_handle_t sound_handle)
{
    TigSound* snd;

    // FIXME: No `tig_sound_initialized` guard.

    if (!sound_handle_is_valid(sound_handle)) {
        return false;
    }

    snd = &(tig_sounds[sound_handle]);
    if (snd->active == 0) {
        return false;
    }

    return snd->positional;
}

// 0x533BF0
void tig_sound_enumerate_positional(TigSoundEnumerateFunc func)
{
    int index;
    TigSound* snd;

    for (index = 0; index < SOUND_HANDLE_MAX; index++) {
        snd = &(tig_sounds[index]);
        if (snd->active != 0 && snd->positional) {
            func(index);
        }
    }
}

// 0x533C30
void tig_sound_quick_play_set_volume(int volume)
{
    tig_sound_effects_volume = volume;
}

// 0x533C40
void tig_sound_quick_play(int id)
{
    tig_sound_handle_t sound_handle;

    if (!tig_sound_initialized) {
        return;
    }

    if (tig_sound_create(&sound_handle, TIG_SOUND_TYPE_EFFECT) == TIG_OK) {
        tig_sound_play_id(sound_handle, id);
        tig_sound_set_volume(sound_handle, tig_sound_effects_volume);
    }
}

// 0x533C90
void tig_sound_set_active(bool is_active)
{
    HDIGDRIVER dig;

    if (!tig_sound_initialized) {
        return;
    }

    AIL_quick_handles(&dig, NULL, NULL);

    if (is_active) {
        AIL_digital_handle_reacquire(dig);
    } else {
        AIL_digital_handle_release(dig);
    }
}
