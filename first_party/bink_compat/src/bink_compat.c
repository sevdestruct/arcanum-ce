/*
 * bink_compat.c — Bink-shaped video playback shim.
 *
 * Provides the HBINK public surface (BinkOpen / BinkDoFrame / BinkWait /
 * BinkCopyToBuffer / ...) the rest of the engine drives, dispatching at
 * runtime between:
 *
 *   (a) Windows-native binkw32.dll, loaded via LoadLibrary when present.
 *       Required for legitimate playback of the original .bik files on
 *       32-bit Windows installs that ship binkw32.dll alongside the game.
 *
 *   (b) A vendored AVI + Motion-JPEG + PCM backend (this translation
 *       unit's AviMjpegBackend) that decodes .avi sidecar files produced
 *       by scripts/convert_videos.py or the in-game conversion modal.
 *       This path is portable C99 and runs on every supported target —
 *       Windows x86/x64, Linux x86/x64, macOS Intel/Apple Silicon, iOS,
 *       and Android — with zero third-party libraries linked into the
 *       game binary.
 *
 * Replaces the previous FFmpeg-based backend; see issue #28 for the
 * rationale. The public ABI is unchanged so tig/src/movie.c, gmovie.c,
 * mainmenu_ui.c, slide_ui.c need no edits to switch backends.
 */

#include "bink_compat.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#else
#include <time.h>
#endif

#include "mjpeg_decoder.h"
#include "avi_reader.h"
#include "bink1_decoder.h"

/* -------------------------------------------------------------------------
 * Native Bink (Windows binkw32.dll) function-pointer table
 * -------------------------------------------------------------------------
 * On Windows x86 with the original game's binkw32.dll co-installed we
 * keep the path that loads and dispatches to RAD's decoder. On every
 * other configuration (Win64, Linux, macOS, iOS, Android) the function
 * pointers stay NULL and BinkOpen falls through to the AVI backend.
 */

typedef void(BINKCALL* BINKCLOSE)(HBINK);
typedef int(BINKCALL* BINKCOPYTOBUFFER)(HBINK, void*, int, unsigned, unsigned, unsigned, unsigned);
typedef int(BINKCALL* BINKDDSURFACETYPE)(void*);
typedef int(BINKCALL* BINKDOFRAME)(HBINK);
typedef void(BINKCALL* BINKNEXTFRAME)(HBINK);
typedef HBINK(BINKCALL* BINKOPEN)(const char*, unsigned);
typedef BINKSNDOPEN(BINKCALL* BINKOPENMILES)(void*);
typedef int(BINKCALL* BINKSETSOUNDSYSTEM)(BINKSNDSYSOPEN, void*);
typedef void(BINKCALL* BINKSETSOUNDTRACK)(unsigned);
typedef int(BINKCALL* BINKWAIT)(HBINK);

#ifdef _WIN32
static HMODULE g_binkw32;
#endif

static BINKCLOSE _BinkClose;
static BINKCOPYTOBUFFER _BinkCopyToBuffer;
static BINKDDSURFACETYPE _BinkDDSurfaceType;
static BINKDOFRAME _BinkDoFrame;
static BINKNEXTFRAME _BinkNextFrame;
static BINKOPEN _BinkOpen;
static BINKOPENMILES _BinkOpenMiles;
static BINKSETSOUNDSYSTEM _BinkSetSoundSystem;
static BINKSETSOUNDTRACK _BinkSetSoundTrack;
static BINKWAIT _BinkWait;

/* Cached BinkSetSoundSystem args. The native loader on Windows-x86 calls
 * the DLL entry-point directly; the AVI backend forwards the same
 * (sysopen, param) pair to BINKSND when it opens an HBINK. */
static BINKSNDSYSOPEN g_snd_sys_open;
static void* g_snd_sys_param;
static unsigned g_snd_track;

/* -------------------------------------------------------------------------
 * Monotonic clock (no SDL dep)
 * -------------------------------------------------------------------------
 */

static uint64_t bink_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER count;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (uint64_t)((double)count.QuadPart * 1.0e9 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* -------------------------------------------------------------------------
 * Backend dispatch tag
 * -------------------------------------------------------------------------
 * Both vendored backends start with the public BINK struct so engine
 * code can read FrameNum/Frames/Width/Height directly. The `kind`
 * field placed immediately after `pub` lets the public Bink* dispatch
 * functions tell them apart without per-call type registries.
 */

typedef enum {
    BINK_COMPAT_KIND_AVI = 1,
    BINK_COMPAT_KIND_BINK1 = 2,
} BinkCompatKind;

static BinkCompatKind backend_kind_of(HBINK bnk)
{
    /* Read the tag at offset sizeof(BINK) -- both struct layouts
     * place it there as their second member. */
    if (!bnk) return BINK_COMPAT_KIND_AVI;
    return *(const BinkCompatKind*)((const char*)bnk + sizeof(BINK));
}

/* -------------------------------------------------------------------------
 * AVI + MJPEG backend
 * -------------------------------------------------------------------------
 */

typedef struct AviMjpegBackend {
    BINK pub;       /* must be first — code casts (HBINK)backend */
    BinkCompatKind kind; /* always BINK_COMPAT_KIND_AVI */

    AviReader* avi;
    MjpegDecoder* dec;
    AviInfo info;

    /* Decoder output buffer (BGRA, 4 bytes/pixel). The buffer is sized
     * to `out_w * out_h`; out_w/out_h default to the AVI's native
     * dimensions and may be overridden by BinkSetOutputSize for the
     * menu background-video native-resolution path. */
    uint8_t* frame_rgba;
    int out_w, out_h;

    /* BINKSND wiring — populated when the engine has called
     * BinkSetSoundSystem before this HBINK was opened. */
    BINKSND snd;
    bool snd_open;

    /* Wall-clock pacing. frame_start_ns is the clock value when playback
     * started; the "ideal" display time for frame N is
     * frame_start_ns + N * info.frame_duration_us * 1000. */
    uint64_t frame_start_ns;
    int64_t current_frame_time_ns;

    /* End-of-stream tracker: BinkDoFrame() pumps chunks until it
     * consumes a video chunk; if EOF arrives we surface that by
     * clamping FrameNum to Frames so the engine loop exits. */
    bool stream_eof;

    /* True once frame_rgba contains a fully decoded image ready for
     * BinkCopyToBuffer to consume. Cleared by BinkNextFrame so the
     * next BinkDoFrame call doesn't double-decode. */
    bool frame_ready;
} AviMjpegBackend;

static void avi_push_audio(AviMjpegBackend* b, const uint8_t* data, size_t size)
{
    if (!b->snd_open || size == 0 || data == NULL) return;
    BINKSND* snd = &b->snd;
    if (snd->OnOff == 0) return;

    size_t pos = 0;
    while (pos < size) {
        /* Respect the BINKSND backpressure contract — if the consumer
         * isn't ready we drop the audio rather than block the UI
         * thread. Lost chunks correspond to ~10ms windows; the wall-
         * clock A/V sync in tig/src/movie.c re-aligns at the next
         * video frame. */
        if (snd->Ready && !snd->Ready(snd)) break;

        u8* dst = NULL;
        u32 cap = 0;
        if (!snd->Lock || !snd->Lock(snd, &dst, &cap) || !dst || cap == 0) break;

        size_t copy = size - pos;
        if (copy > (size_t)cap) copy = (size_t)cap;
        memcpy(dst, data + pos, copy);

        if (!snd->Unlock || !snd->Unlock(snd, (u32)copy)) break;
        pos += copy;
    }
}

static bool avi_decode_chunk_to_frame(AviMjpegBackend* b,
    const uint8_t* data, size_t size)
{
    if (!b->dec || !b->frame_rgba || data == NULL || size == 0) return false;
    return mjpeg_decoder_decode(b->dec, data, size,
        b->frame_rgba, b->out_w * 4, b->out_w, b->out_h);
}

/* Pump chunks until one video chunk has been decoded into frame_rgba.
 * Audio chunks encountered along the way are pushed through BINKSND. */
static bool avi_advance_to_next_video(AviMjpegBackend* b)
{
    if (b->stream_eof) return false;

    for (;;) {
        AviChunk c;
        if (!avi_reader_next_chunk(b->avi, &c)) {
            b->stream_eof = true;
            return false;
        }
        if (c.type == AVI_CHUNK_EOF) {
            b->stream_eof = true;
            return false;
        }
        if (c.type == AVI_CHUNK_AUDIO) {
            avi_push_audio(b, c.data, c.size);
            continue;
        }
        if (c.type == AVI_CHUNK_VIDEO) {
            if (c.size == 0) {
                /* Drop frame: leave frame_rgba alone and report
                 * success — the engine will just repaint the prior
                 * frame for one tick. */
                b->frame_ready = true;
                return true;
            }
            if (!avi_decode_chunk_to_frame(b, c.data, c.size)) {
                return false;
            }
            b->frame_ready = true;
            return true;
        }
    }
}

static void avi_destroy(AviMjpegBackend* b)
{
    if (!b) return;
    if (b->snd_open && b->snd.Close) {
        b->snd.Close(&b->snd);
        b->snd_open = false;
    }
    avi_reader_close(b->avi);
    mjpeg_decoder_destroy(b->dec);
    free(b->frame_rgba);
    free(b);
}

static AviMjpegBackend* avi_open(const char* path, unsigned flags)
{
    (void)flags;

    AviReader* r = avi_reader_open(path);
    if (!r) return NULL;

    AviInfo info;
    if (!avi_reader_get_info(r, &info) || info.width == 0 || info.height == 0) {
        avi_reader_close(r);
        return NULL;
    }

    AviMjpegBackend* b = (AviMjpegBackend*)calloc(1, sizeof(*b));
    if (!b) {
        avi_reader_close(r);
        return NULL;
    }
    b->kind = BINK_COMPAT_KIND_AVI;
    b->avi = r;
    b->info = info;
    b->out_w = (int)info.width;
    b->out_h = (int)info.height;

    b->pub.Width = info.width;
    b->pub.Height = info.height;
    b->pub.Frames = info.frame_count;
    b->pub.FrameNum = 0;
    b->pub.FrameDurationMs = (info.frame_duration_us + 500) / 1000;
    if (b->pub.FrameDurationMs == 0) b->pub.FrameDurationMs = 33;

    b->dec = mjpeg_decoder_create();
    if (!b->dec) {
        avi_destroy(b);
        return NULL;
    }

    size_t fb = (size_t)b->out_w * (size_t)b->out_h * 4u;
    b->frame_rgba = (uint8_t*)calloc(1, fb);
    if (!b->frame_rgba) {
        avi_destroy(b);
        return NULL;
    }

    /* Open the BINKSND track if the engine has registered a sound
     * system (it always does in tig_movie_init). Audio is 16-bit
     * signed PCM out of the AVI; the WAVEFORMATEX in the file carries
     * the freq/channels. */
    if (g_snd_sys_open && info.has_audio
        && info.audio_format == 1
        && (info.audio_bits_per_sample == 16 || info.audio_bits_per_sample == 8)) {
        BINKSNDOPEN snd_open = g_snd_sys_open(g_snd_sys_param);
        if (snd_open) {
            memset(&b->snd, 0, sizeof(b->snd));
            if (snd_open(&b->snd,
                    (uint32_t)info.audio_freq,
                    info.audio_bits_per_sample,
                    info.audio_channels,
                    0,
                    (HBINK)b)) {
                b->snd_open = true;
                b->snd.OnOff = 1;
            }
        }
    }

    b->frame_start_ns = bink_now_ns();
    b->current_frame_time_ns = 0;
    return b;
}

static int avi_wait(AviMjpegBackend* b)
{
    if (!b) return -1;
    if (b->frame_ready) return 0;
    if (b->stream_eof) return 0;

    uint64_t now = bink_now_ns();
    uint64_t elapsed = now - b->frame_start_ns;
    uint64_t target = (uint64_t)b->pub.FrameNum
        * (uint64_t)b->info.frame_duration_us
        * 1000ULL;
    return elapsed < target ? 1 : 0;
}

static int avi_do_frame(AviMjpegBackend* b)
{
    if (!b) return -1;
    if (b->frame_ready) return 0;
    if (b->stream_eof) {
        /* Engine exit check is `FrameNum > Frames` (strict), not >=.
         * Push FrameNum past Frames so tig_movie_do_frame's early-exit
         * triggers on the next iteration and the playback loop unwinds.
         * Without this clamp the engine spins forever after the last
         * audio chunk drains. */
        if (b->pub.FrameNum <= b->pub.Frames) {
            b->pub.FrameNum = b->pub.Frames + 1;
        }
        return 0;
    }
    if (!avi_advance_to_next_video(b)) {
        b->pub.FrameNum = b->pub.Frames + 1;
        return 0;
    }
    b->current_frame_time_ns = (int64_t)b->pub.FrameNum
        * (int64_t)b->info.frame_duration_us * 1000;
    return 0;
}

static void avi_next_frame(AviMjpegBackend* b)
{
    if (!b) return;
    b->frame_ready = false;
    /* Allow FrameNum to reach Frames + 1 so the engine's strict
     * "FrameNum > Frames" exit check can trigger at end-of-stream. */
    if (b->pub.FrameNum <= b->pub.Frames) b->pub.FrameNum++;
}

static int avi_copy_to_buffer(AviMjpegBackend* b, void* dest, int destpitch,
    unsigned destheight, unsigned destx, unsigned desty, unsigned flags)
{
    (void)flags;
    if (!b || !dest || destpitch <= 0 || destheight == 0) return 0;
    if (!b->frame_rgba) return 0;

    int copy_w = b->out_w;
    int copy_h = b->out_h;
    if ((int)destheight < copy_h) copy_h = (int)destheight;

    const uint8_t* src = b->frame_rgba;
    uint8_t* dst = (uint8_t*)dest + desty * destpitch + destx * 4;
    int src_pitch = b->out_w * 4;
    int row_bytes = copy_w * 4;
    if (row_bytes > destpitch - (int)destx * 4) {
        row_bytes = destpitch - (int)destx * 4;
        if (row_bytes <= 0) return 0;
    }
    for (int y = 0; y < copy_h; ++y) {
        memcpy(dst + y * destpitch, src + y * src_pitch, row_bytes);
    }
    return 1;
}

static void avi_rewind(AviMjpegBackend* b)
{
    if (!b) return;
    if (!avi_reader_rewind(b->avi)) return;
    b->stream_eof = false;
    b->frame_ready = false;
    b->pub.FrameNum = 0;
    b->frame_start_ns = bink_now_ns();
    b->current_frame_time_ns = 0;
}

static void avi_set_output_size(AviMjpegBackend* b, int w, int h)
{
    if (!b || w <= 0 || h <= 0) return;
    if (w == b->out_w && h == b->out_h) return;
    uint8_t* nb = (uint8_t*)realloc(b->frame_rgba, (size_t)w * (size_t)h * 4u);
    if (!nb) return;
    b->frame_rgba = nb;
    b->out_w = w;
    b->out_h = h;
    /* Existing decoded frame becomes stale at the new size — clear
     * ready flag so the next BinkDoFrame re-decodes. */
    b->frame_ready = false;
}

static bool is_avi_backend(HBINK bnk)
{
    if (!bnk) return false;
    if (_BinkOpen != NULL) return false;       /* native -- not us */
    return backend_kind_of(bnk) == BINK_COMPAT_KIND_AVI;
}

static bool is_bink1_backend(HBINK bnk)
{
    if (!bnk) return false;
    if (_BinkOpen != NULL) return false;
    return backend_kind_of(bnk) == BINK_COMPAT_KIND_BINK1;
}

/* -------------------------------------------------------------------------
 * Bink1 native decoder backend
 * -------------------------------------------------------------------------
 * Mirrors the AVI backend's shape so the public BinkOpen/BinkDoFrame/
 * BinkWait/BinkCopyToBuffer/BinkClose dispatchers can route to either
 * implementation based on the backend kind tag. The bink1_decoder
 * itself handles the format-specific work; this layer adds wall-clock
 * pacing, BGRA composition into an output buffer, and BINKSND audio
 * routing on the SDL_Mixer side.
 */

typedef struct Bink1Backend {
    BINK pub;                   /* must be first */
    BinkCompatKind kind;        /* always BINK_COMPAT_KIND_BINK1 */

    Bink1Decoder* dec;
    Bink1Info info;

    uint8_t* frame_rgba;
    int out_w, out_h;

    BINKSND snd;
    bool snd_open;

    /* Pacing / state mirrors the AVI backend so tig/movie.c sees the
     * same semantics from both. */
    uint64_t frame_start_ns;
    int64_t current_frame_time_ns;
    bool stream_eof;
    bool frame_ready;

    /* Audio scratch buffer; sized to bink1_decoder_max_audio_bytes. */
    uint8_t* aud_scratch;
    size_t aud_scratch_cap;
} Bink1Backend;

static void bink1_destroy(Bink1Backend* b)
{
    if (!b) return;
    if (b->snd_open && b->snd.Close) {
        b->snd.Close(&b->snd);
        b->snd_open = false;
    }
    bink1_decoder_close(b->dec);
    free(b->frame_rgba);
    free(b->aud_scratch);
    free(b);
}

static void bink1_push_audio(Bink1Backend* b, const uint8_t* data, size_t size)
{
    if (!b->snd_open || size == 0 || data == NULL) return;
    BINKSND* snd = &b->snd;
    if (snd->OnOff == 0) return;
    size_t pos = 0;
    while (pos < size) {
        if (snd->Ready && !snd->Ready(snd)) break;
        u8* dst = NULL;
        u32 cap = 0;
        if (!snd->Lock || !snd->Lock(snd, &dst, &cap) || !dst || cap == 0) break;
        size_t copy = size - pos;
        if (copy > (size_t)cap) copy = (size_t)cap;
        memcpy(dst, data + pos, copy);
        if (!snd->Unlock || !snd->Unlock(snd, (u32)copy)) break;
        pos += copy;
    }
}

static Bink1Backend* bink1_open(const char* path)
{
    Bink1Decoder* dec = bink1_decoder_open(path);
    if (!dec) {
        fprintf(stderr, "bink_compat: bink1_open FAILED for %s\n", path ? path : "(null)");
        return NULL;
    }

    Bink1Info info;
    if (!bink1_decoder_get_info(dec, &info) || info.width == 0 || info.height == 0) {
        bink1_decoder_close(dec);
        return NULL;
    }
    fprintf(stderr,
        "bink_compat: bink1_open %s: BIK%c, %ux%u, %u frames, %u us/frame, "
        "%d audio tracks (rate=%d ch=%d %s)\n",
        path, info.video_version,
        info.width, info.height, info.frame_count, info.frame_duration_us,
        info.audio_track_count, info.audio_sample_rate, info.audio_channels,
        info.audio_is_dct ? "DCT" : "RDFT");

    Bink1Backend* b = (Bink1Backend*)calloc(1, sizeof(*b));
    if (!b) {
        bink1_decoder_close(dec);
        return NULL;
    }
    b->kind = BINK_COMPAT_KIND_BINK1;
    b->dec = dec;
    b->info = info;
    b->out_w = (int)info.width;
    b->out_h = (int)info.height;

    b->pub.Width = info.width;
    b->pub.Height = info.height;
    b->pub.Frames = info.frame_count;
    b->pub.FrameNum = 0;
    b->pub.FrameDurationMs = (info.frame_duration_us + 500) / 1000;
    if (b->pub.FrameDurationMs == 0) b->pub.FrameDurationMs = 33;

    size_t fb = (size_t)b->out_w * (size_t)b->out_h * 4u;
    b->frame_rgba = (uint8_t*)calloc(1, fb);
    if (!b->frame_rgba) {
        bink1_destroy(b);
        return NULL;
    }

    /* Audio scratch sized to one frame's worth of s16 stereo. */
    b->aud_scratch_cap = bink1_decoder_max_audio_bytes(dec);
    if (b->aud_scratch_cap > 0) {
        b->aud_scratch = (uint8_t*)malloc(b->aud_scratch_cap);
        if (!b->aud_scratch) {
            bink1_destroy(b);
            return NULL;
        }
    }

    /* Open the BINKSND sound channel if the engine registered one. */
    if (g_snd_sys_open && info.audio_track_count > 0
        && info.audio_sample_rate > 0 && info.audio_channels > 0) {
        BINKSNDOPEN snd_open = g_snd_sys_open(g_snd_sys_param);
        if (snd_open) {
            memset(&b->snd, 0, sizeof(b->snd));
            if (snd_open(&b->snd,
                    (u32)info.audio_sample_rate,
                    16,
                    info.audio_channels,
                    0,
                    (HBINK)b)) {
                b->snd_open = true;
                b->snd.OnOff = 1;
            }
        }
    }

    b->frame_start_ns = bink_now_ns();
    return b;
}

static int bink1_wait(Bink1Backend* b)
{
    if (!b) return -1;
    if (b->frame_ready) return 0;
    if (b->stream_eof) return 0;
    uint64_t now = bink_now_ns();
    uint64_t elapsed = now - b->frame_start_ns;
    uint64_t target = (uint64_t)b->pub.FrameNum
        * (uint64_t)b->info.frame_duration_us * 1000ULL;
    return elapsed < target ? 1 : 0;
}

static int bink1_do_frame(Bink1Backend* b)
{
    if (!b) return -1;
    if (b->frame_ready) return 0;
    if (b->stream_eof) {
        if (b->pub.FrameNum <= b->pub.Frames) {
            b->pub.FrameNum = b->pub.Frames + 1;
        }
        return 0;
    }

    /* Pull audio for this frame and route through BINKSND first; if
     * the decoder is past EOF, advance state appropriately. */
    if (b->aud_scratch && b->aud_scratch_cap > 0) {
        size_t produced = 0;
        if (bink1_decoder_decode_audio(b->dec, b->aud_scratch,
                b->aud_scratch_cap, &produced)) {
            if (produced > 0) {
                bink1_push_audio(b, b->aud_scratch, produced);
            }
        }
    }

    /* Decode the video frame into our BGRA buffer. */
    if (!bink1_decoder_decode_video(b->dec, b->frame_rgba,
            b->out_w * 4, b->out_w, b->out_h)) {
        b->stream_eof = true;
        b->pub.FrameNum = b->pub.Frames + 1;
        return 0;
    }
    b->frame_ready = true;
    b->current_frame_time_ns = (int64_t)b->pub.FrameNum
        * (int64_t)b->info.frame_duration_us * 1000;
    return 0;
}

static void bink1_next_frame(Bink1Backend* b)
{
    if (!b) return;
    b->frame_ready = false;
    if (!bink1_decoder_next_frame(b->dec)) {
        b->stream_eof = true;
    }
    if (b->pub.FrameNum <= b->pub.Frames) b->pub.FrameNum++;
}

static int bink1_copy_to_buffer(Bink1Backend* b, void* dest, int destpitch,
    unsigned destheight, unsigned destx, unsigned desty, unsigned flags)
{
    (void)flags;
    if (!b || !dest || destpitch <= 0 || destheight == 0) return 0;
    if (!b->frame_rgba) return 0;
    int copy_w = b->out_w;
    int copy_h = b->out_h;
    if ((int)destheight < copy_h) copy_h = (int)destheight;
    const uint8_t* src = b->frame_rgba;
    uint8_t* dst = (uint8_t*)dest + desty * destpitch + destx * 4;
    int src_pitch = b->out_w * 4;
    int row_bytes = copy_w * 4;
    if (row_bytes > destpitch - (int)destx * 4) {
        row_bytes = destpitch - (int)destx * 4;
        if (row_bytes <= 0) return 0;
    }
    for (int y = 0; y < copy_h; ++y) {
        memcpy(dst + y * destpitch, src + y * src_pitch, row_bytes);
    }
    return 1;
}

static void bink1_rewind(Bink1Backend* b)
{
    if (!b) return;
    bink1_decoder_rewind(b->dec);
    b->stream_eof = false;
    b->frame_ready = false;
    b->pub.FrameNum = 0;
    b->frame_start_ns = bink_now_ns();
    b->current_frame_time_ns = 0;
}

/* True if the user has opted into the direct-Bink path via env var.
 * Off by default until the video bundle layer + DCT are fully wired
 * up; until then, even files that decode-open successfully will play
 * back as black frames. */
static bool bink1_path_enabled(void)
{
    const char* v = getenv("ARCANUM_BINK_DIRECT");
    if (!v || !*v) return false;
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T';
}

bool bink_compat_native_bink_enabled(void)
{
    return bink1_path_enabled();
}

/* -------------------------------------------------------------------------
 * Public dispatch
 * -------------------------------------------------------------------------
 */

void BINKCALL BinkClose(HBINK bnk)
{
    if (_BinkClose != NULL) {
        _BinkClose(bnk);
        return;
    }
    if (is_bink1_backend(bnk)) {
        bink1_destroy((Bink1Backend*)bnk);
        return;
    }
    avi_destroy((AviMjpegBackend*)bnk);
}

int BINKCALL BinkCopyToBuffer(HBINK bnk, void* dest, int destpitch, unsigned destheight,
    unsigned destx, unsigned desty, unsigned flags)
{
    if (_BinkCopyToBuffer != NULL) {
        return _BinkCopyToBuffer(bnk, dest, destpitch, destheight, destx, desty, flags);
    }
    if (is_bink1_backend(bnk)) {
        return bink1_copy_to_buffer((Bink1Backend*)bnk,
            dest, destpitch, destheight, destx, desty, flags);
    }
    return avi_copy_to_buffer((AviMjpegBackend*)bnk,
        dest, destpitch, destheight, destx, desty, flags);
}

int BINKCALL BinkDDSurfaceType(void* lpDDS)
{
    if (_BinkDDSurfaceType != NULL) {
        return _BinkDDSurfaceType(lpDDS);
    }
    (void)lpDDS;
    return 0;
}

int BINKCALL BinkDoFrame(HBINK bnk)
{
    if (_BinkDoFrame != NULL) {
        return _BinkDoFrame(bnk);
    }
    if (is_bink1_backend(bnk)) {
        return bink1_do_frame((Bink1Backend*)bnk);
    }
    return avi_do_frame((AviMjpegBackend*)bnk);
}

void BINKCALL BinkNextFrame(HBINK bnk)
{
    if (_BinkNextFrame != NULL) {
        _BinkNextFrame(bnk);
        return;
    }
    if (is_bink1_backend(bnk)) {
        bink1_next_frame((Bink1Backend*)bnk);
        return;
    }
    avi_next_frame((AviMjpegBackend*)bnk);
}

HBINK BINKCALL BinkOpen(const char* name, unsigned flags)
{
    if (_BinkOpen != NULL) {
        HBINK h = _BinkOpen(name, flags);
        if (h != NULL) return h;
        /* Native open failed: fall through to the AVI sidecar so the
         * Win32 build still plays videos on machines where binkw32.dll
         * is present but the specific .bik can't be opened. */
    }
    if (!name) return NULL;

    /* Opt-in v2 path: ARCANUM_BINK_DIRECT=1 enables the from-scratch
     * Bink1 decoder for .bik files. Off by default while the video
     * bundle decoder is still WIP -- when enabled it will currently
     * play .bik streams as black-frame placeholders with audio
     * silence (container/demux works, but block reconstruction is
     * not wired up yet). The AVI/MJPEG path remains primary.  */
    if (bink1_path_enabled()) {
        size_t nlen = strlen(name);
        bool looks_bik = nlen >= 4
            && (name[nlen - 4] == '.')
            && (name[nlen - 3] == 'b' || name[nlen - 3] == 'B')
            && (name[nlen - 2] == 'i' || name[nlen - 2] == 'I')
            && (name[nlen - 1] == 'k' || name[nlen - 1] == 'K');
        if (looks_bik) {
            Bink1Backend* bb = bink1_open(name);
            if (bb) return (HBINK)bb;
        }
    }

    /* Try the requested path verbatim first (caller may already point
     * at a .avi), then swap the extension. */
    AviMjpegBackend* b = avi_open(name, flags);
    if (b) return (HBINK)b;

    size_t len = strlen(name);
    if (len > 4) {
        char* alt = (char*)malloc(len + 5);
        if (alt) {
            memcpy(alt, name, len);
            /* Replace the trailing extension with ".avi". */
            char* dot = NULL;
            for (size_t i = len; i > 0; --i) {
                if (alt[i - 1] == '.') { dot = alt + (i - 1); break; }
                if (alt[i - 1] == '/' || alt[i - 1] == '\\') break;
            }
            if (dot) {
                memcpy(dot, ".avi", 5);
            } else {
                memcpy(alt + len, ".avi", 5);
            }
            b = avi_open(alt, flags);
            free(alt);
            if (b) return (HBINK)b;
        }
    }
    return NULL;
}

BINKSNDOPEN BINKCALL BinkOpenMiles(void* param)
{
    if (_BinkOpenMiles != NULL) {
        return _BinkOpenMiles(param);
    }
    (void)param;
    return NULL;
}

int BINKCALL BinkSetSoundSystem(BINKSNDSYSOPEN open, void* param)
{
    g_snd_sys_open = open;
    g_snd_sys_param = param;
    if (_BinkSetSoundSystem != NULL) {
        return _BinkSetSoundSystem(open, param);
    }
    return 1;
}

void BINKCALL BinkSetSoundTrack(unsigned track)
{
    g_snd_track = track;
    if (_BinkSetSoundTrack != NULL) {
        _BinkSetSoundTrack(track);
    }
}

int BINKCALL BinkWait(HBINK bnk)
{
    if (_BinkWait != NULL) {
        return _BinkWait(bnk);
    }
    if (is_bink1_backend(bnk)) {
        return bink1_wait((Bink1Backend*)bnk);
    }
    return avi_wait((AviMjpegBackend*)bnk);
}

void BINKCALL BinkRewind(HBINK bnk)
{
    if (!bnk) return;
    if (is_bink1_backend(bnk)) {
        bink1_rewind((Bink1Backend*)bnk);
        return;
    }
    if (!is_avi_backend(bnk)) {
        /* binkw32.dll exposes no public rewind on the build of the DLL
         * we ship with; menu-loop playback uses the AVI backend so this
         * is fine in practice. */
        return;
    }
    avi_rewind((AviMjpegBackend*)bnk);
}

void BinkSetOutputSize(HBINK bnk, int w, int h)
{
    if (!bnk) return;
    if (is_bink1_backend(bnk)) {
        /* The Bink1 path decodes at native size; downstream blit handles
         * scaling. Output-size override not honored on this backend. */
        (void)w; (void)h;
        return;
    }
    if (!is_avi_backend(bnk)) return;
    avi_set_output_size((AviMjpegBackend*)bnk, w, h);
}

bool bink_compat_get_frame_time_ns(HBINK bnk, int64_t* frame_time_ns)
{
    if (!bnk || !frame_time_ns) return false;
    if (is_bink1_backend(bnk)) {
        Bink1Backend* b = (Bink1Backend*)bnk;
        *frame_time_ns = b->current_frame_time_ns;
        return *frame_time_ns >= 0;
    }
    if (!is_avi_backend(bnk)) return false;
    AviMjpegBackend* b = (AviMjpegBackend*)bnk;
    *frame_time_ns = b->current_frame_time_ns;
    return *frame_time_ns >= 0;
}

bool bink_compat_pump_audio(HBINK bnk)
{
    /* Both vendored backends push audio inline during their do_frame;
     * the engine's master-clock pump can still call us between
     * BinkWait() spins, in which case there is nothing further to do. */
    (void)bnk;
    return false;
}

int bink_compat_get_queued_video_frames(HBINK bnk)
{
    if (!bnk) return 0;
    if (is_bink1_backend(bnk)) {
        Bink1Backend* b = (Bink1Backend*)bnk;
        return b->frame_ready ? 1 : 0;
    }
    if (!is_avi_backend(bnk)) return 0;
    AviMjpegBackend* b = (AviMjpegBackend*)bnk;
    return b->frame_ready ? 1 : 0;
}

void bink_compat_set_audio_enabled(HBINK bnk, bool enabled)
{
    if (!bnk) return;
    if (is_bink1_backend(bnk)) {
        Bink1Backend* b = (Bink1Backend*)bnk;
        if (!b->snd_open) return;
        b->snd.OnOff = enabled ? 1 : 0;
        return;
    }
    if (!is_avi_backend(bnk)) return;
    AviMjpegBackend* b = (AviMjpegBackend*)bnk;
    if (!b->snd_open) return;
    b->snd.OnOff = enabled ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Init / exit
 * -------------------------------------------------------------------------
 */

bool bink_compat_init(void)
{
    g_snd_sys_open = NULL;
    g_snd_sys_param = NULL;
    g_snd_track = 0;

#ifdef _WIN32
    /* Only the legacy 32-bit Windows build dynamically loads
     * binkw32.dll; on Win64 the DLL is the 32-bit variant which can't
     * be loaded from a 64-bit process, so we skip straight to the AVI
     * backend.  Loading is optional even on Win32 — absent DLLs just
     * mean we fall through to the AVI sidecars. */
#ifndef _WIN64
    g_binkw32 = LoadLibraryA("binkw32.dll");
    if (g_binkw32 != NULL) {
        _BinkClose = (BINKCLOSE)GetProcAddress(g_binkw32, "_BinkClose@4");
        _BinkCopyToBuffer = (BINKCOPYTOBUFFER)GetProcAddress(g_binkw32, "_BinkCopyToBuffer@28");
        _BinkDDSurfaceType = (BINKDDSURFACETYPE)GetProcAddress(g_binkw32, "_BinkDDSurfaceType@4");
        _BinkDoFrame = (BINKDOFRAME)GetProcAddress(g_binkw32, "_BinkDoFrame@4");
        _BinkNextFrame = (BINKNEXTFRAME)GetProcAddress(g_binkw32, "_BinkNextFrame@4");
        _BinkOpen = (BINKOPEN)GetProcAddress(g_binkw32, "_BinkOpen@8");
        _BinkOpenMiles = (BINKOPENMILES)GetProcAddress(g_binkw32, "_BinkOpenMiles@4");
        _BinkSetSoundSystem = (BINKSETSOUNDSYSTEM)GetProcAddress(g_binkw32, "_BinkSetSoundSystem@8");
        _BinkSetSoundTrack = (BINKSETSOUNDTRACK)GetProcAddress(g_binkw32, "_BinkSetSoundTrack@4");
        _BinkWait = (BINKWAIT)GetProcAddress(g_binkw32, "_BinkWait@4");

        if (_BinkClose == NULL
            || _BinkCopyToBuffer == NULL
            || _BinkDoFrame == NULL
            || _BinkNextFrame == NULL
            || _BinkOpen == NULL
            || _BinkWait == NULL) {
            FreeLibrary(g_binkw32);
            g_binkw32 = NULL;
            _BinkClose = NULL;
            _BinkCopyToBuffer = NULL;
            _BinkDDSurfaceType = NULL;
            _BinkDoFrame = NULL;
            _BinkNextFrame = NULL;
            _BinkOpen = NULL;
            _BinkOpenMiles = NULL;
            _BinkSetSoundSystem = NULL;
            _BinkSetSoundTrack = NULL;
            _BinkWait = NULL;
        }
    }
#endif
#endif
    return true;
}

void bink_compat_exit(void)
{
#ifdef _WIN32
    if (g_binkw32 != NULL) {
        FreeLibrary(g_binkw32);
        g_binkw32 = NULL;
    }
    _BinkClose = NULL;
    _BinkCopyToBuffer = NULL;
    _BinkDDSurfaceType = NULL;
    _BinkDoFrame = NULL;
    _BinkNextFrame = NULL;
    _BinkOpen = NULL;
    _BinkOpenMiles = NULL;
    _BinkSetSoundSystem = NULL;
    _BinkSetSoundTrack = NULL;
    _BinkWait = NULL;
#endif
    g_snd_sys_open = NULL;
    g_snd_sys_param = NULL;
    g_snd_track = 0;
}
