#include "game/tile.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "tig/art_gpu_cache.h"

#include "game/a_name.h"
#include "game/gamelib.h"
#include "game/light.h"
#include "game/random.h"
#include "game/roof.h"
#include "game/sector.h"
#include "game/iso_zoom.h"
#include "game/tile_block.h"
#include "game/void_edge_fade.h"

#define TILE_CACHE_CAPACITY 64

typedef struct TileCacheEntry {
    unsigned int art_id;
    TigVideoBuffer* video_buffer;
    unsigned int time;
} TileCacheEntry;

static void sub_4D7820(int64_t loc, tig_art_id_t art_id);
static void sub_4D7980(void);
static void sub_4D79C0(ViewOptions* view_options);
static void sub_4D7A00(void);
static void sub_4D7A40(int zoom);
static void sub_4D7A90(void);
static void sub_4D7AC0(int zoom);
static void sub_4D7C70(void);
static TigVideoBuffer* sub_4D7E90(unsigned int art_id);
static void tile_draw_topdown(GameDrawInfo* draw_info);
static void tile_draw_iso(GameDrawInfo* draw_info);

// 0x602AE0
static TileCacheEntry stru_602AE0[TILE_CACHE_CAPACITY];

// 0x602DE0
static TigVideoBuffer* dword_602DE0;

// 0x602DE4
static uint8_t* dword_602DE4;

// 0x602DE8
static uint8_t* dword_602DE8;

// 0x602DEC
static IsoInvalidateRectFunc* tile_invalidate_rect;

// 0x602DF0
static TigVideoBuffer* dword_602DF0;

// 0x602DF4
static bool tile_visible;

// 0x602DF8
static ViewOptions tile_view_options;

// 0x602E00
static bool tile_hardware_accelerated;

// 0x602E04
static tig_window_handle_t tile_iso_window_handle;

// 0x602E08
static bool dword_602E08;

// CE (feature/perf-gpu-accel Phase 3): GPU tile-pass state. Allocated
// lazily the first time tile_draw_iso runs in GPU mode and resized when
// the destination buffer changes (window resize / zoom toggle). The
// buffer is a TigVideoBuffer wrapping an SDL_Texture with TARGET access,
// reused frame to frame.
static TigVideoBuffer* tile_gpu_world_buffer;
static int tile_gpu_world_buffer_w;
static int tile_gpu_world_buffer_h;

// CE (step 6): roof present-layer. In gpu-present, roofs render to their own
// texture (cleared + fully re-rendered every frame -> no partial-redraw tearing /
// alpha accumulation), composited between the world and the UI at flip.
static TigVideoBuffer* tile_gpu_roof_buffer;
static int tile_gpu_roof_buffer_w;
static int tile_gpu_roof_buffer_h;
static bool tile_gpu_roof_pass_active;
// The GPU dispatch's render target: the world buffer during the world pass, the
// roof buffer during the roof pass.
static TigVideoBuffer* tile_gpu_target_buffer;

// CE (zoom->GPU): a dedicated 2x-screen GPU render target for the zoomed world.
// The zoom renders the world 1:1 (world-VB coords) into it -- like the fixed 2x CPU
// gamelib_world_video_buffer it replaces -- then the centered (ww/z x wh/z) crop is
// bilinear-downscaled to the iso rect via the world underlay. Persistent (sized
// once to 2x screen) so it isn't recreated on zoom toggle. Zoom-specific failure
// flag so a create failure falls back to CPU zoom without disabling all GPU.
static TigVideoBuffer* tile_gpu_zoom_buffer;
static int tile_gpu_zoom_buffer_w;
static int tile_gpu_zoom_buffer_h;
static bool tile_gpu_zoom_disabled;
// CE (zoom idle-time preload): set once the zoom render targets have been warmed
// off-screen so the first real zoom skips the GPU's one-time first-render pipeline
// setup (~70ms). Reset when the zoom buffer is (re)created so a resize re-warms.
static bool tile_gpu_zoom_warmed;

// CE (zoom roof layer): the zoom-pass roofs render into their OWN 2x buffer (cleared
// each frame), composited over the downscaled zoom world buffer -- exactly like the
// 1.0 roof present-layer over the world target. Keeps the alpha-blended fade roof out
// of the incremental zoom world buffer, so it can't accumulate (no streaks), with no
// PC-under-roof heuristic. Sized to match tile_gpu_zoom_buffer (2x screen).
static TigVideoBuffer* tile_gpu_zoom_roof_buffer;
static int tile_gpu_zoom_roof_buffer_w;
static int tile_gpu_zoom_roof_buffer_h;
static bool tile_gpu_zoom_roof_active; // true between zoom_roof_begin and zoom_end

// Sticky failure flag: once any part of the GPU init path fails (cache
// init, buffer create, etc.), stop attempting it for the rest of the
// session and fall back to software. Avoids per-frame retries.
static bool tile_gpu_path_disabled;

// Per-frame state for tile_blit_dispatch: true while the GPU render
// target is bound and tile blits should route through blit_gpu instead
// of tig_art_blit.
static bool tile_gpu_active;
// CE (full GPU/UI): true while gpu-ui mode composites UI windows on the GPU.
// Set in tile_gpu_world_begin; read by the (incoming) per-window GPU compositor.
static bool tile_gpu_ui_active;

// CE (feature/perf-gpu-accel Phase 3 fix): deferred cache-miss blits.
//
// During the GPU pass the render target is bound to the GPU world buffer and
// tile_gpu_end_pass reads it back over the CPU dst surface. A tile that misses
// the GPU art cache can't simply fall back to tig_art_blit mid-pass -- that
// draws to the CPU surface, which the readback then overwrites, leaving a
// black tile-shaped hole. Instead we record the miss here (deep-copying the
// blit info plus the rects/colors it points at, which are reused stack locals
// in tile_draw_iso) and replay it with tig_art_blit AFTER the readback, onto
// the now-current CPU surface. With the original-palette GPU cache fill, real
// misses are rare, so this list normally stays empty.
typedef struct TileDeferredBlit {
    TigArtBlitInfo info;
    TigRect src_rect;
    TigRect dst_rect;
    TigRect lerp_rect;
    tig_color_t lerp_colors[4];
} TileDeferredBlit;

#define TILE_DEFERRED_BLIT_MAX 4096
static TileDeferredBlit tile_deferred_blits[TILE_DEFERRED_BLIT_MAX];
static int tile_deferred_blit_count;

// CE (feature/perf-gpu-accel): one-shot textures for PALETTE_OVERRIDE (recolor)
// blits rendered in z-order on the GPU target. They can't be destroyed right
// after the blit_gpu call -- the renderer may still hold the draw in its batch.
// We collect them and free them in tile_gpu_world_end, after the readback
// (SDL_RenderReadPixels) has flushed all pending draws.
#define TILE_ONESHOT_TEX_MAX 512
static SDL_Texture* tile_oneshot_textures[TILE_ONESHOT_TEX_MAX];
static int tile_oneshot_tex_count;

// CE (feature/perf-gpu-accel): bridge cost instrumentation. The GPU tile pass
// time (gamelib's "tile" bucket) lumps together the per-frame CPU->GPU upload,
// the tile blits, and the GPU->CPU readback. These accumulators isolate the
// two transfer halves so the F9 log can attribute the cost; the blit time is
// the remainder (tile bucket - upload - readback). Read+reset once per perf
// window by gamelib via tile_gpu_perf_read_reset.
static uint64_t tile_gpu_perf_upload_ns;
static uint64_t tile_gpu_perf_readback_ns;

static uint64_t tile_gpu_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void tile_gpu_perf_read_reset(uint64_t* upload_ns, uint64_t* readback_ns)
{
    if (upload_ns != NULL) {
        *upload_ns = tile_gpu_perf_upload_ns;
    }
    if (readback_ns != NULL) {
        *readback_ns = tile_gpu_perf_readback_ns;
    }
    tile_gpu_perf_upload_ns = 0;
    tile_gpu_perf_readback_ns = 0;
}

// CE (feature/perf-gpu-accel Phase 3): return true if arcanum.cfg
// requests the GPU tile path AND the GPU init didn't previously fail.
// Re-read each frame so the user can toggle between runs.
// Return the configured render-path string, or "software" when unset. The pre-rename
// values "gpu" / "gpu-present" / "gpu-ui" are no longer accepted -- old configs must be
// updated to software / hardware / debug-gpu-present / debug-gpu-readback (an unknown
// value falls through to the software path).
static const char* tile_render_path_canon(void)
{
    const char* mode = settings_get_str_value(&settings, RENDER_PATH_KEY);
    return mode != NULL ? mode : TILE_RENDER_PATH_SOFTWARE;
}

static bool tile_should_use_gpu_path(void)
{
    if (tile_gpu_path_disabled) {
        return false;
    }
    // The zoomed world render bypasses the GPU world target (it renders to the 2x
    // world-VB + downscale-blit), so fall back to the software/readback path while
    // a zoom render is in flight.
    if (gamelib_zoom_world_pass_is_active()) {
        return false;
    }
    const char* mode = tile_render_path_canon();
    return strcmp(mode, TILE_RENDER_PATH_HARDWARE) == 0
        || strcmp(mode, TILE_RENDER_PATH_DEBUG_PRESENT) == 0
        || strcmp(mode, TILE_RENDER_PATH_DEBUG_READBACK) == 0;
}

// CE (step 6): true when the world target is composited directly at flip (drop
// the readback) instead of read back to the CPU surface (dword_602DF0). gpu-ui
// builds on this — it adds GPU UI compositing on top of the present-time world.
static bool tile_gpu_present_path(void)
{
    if (tile_gpu_path_disabled || gamelib_zoom_world_pass_is_active()) {
        return false;
    }
    const char* mode = tile_render_path_canon();
    return strcmp(mode, TILE_RENDER_PATH_HARDWARE) == 0
        || strcmp(mode, TILE_RENDER_PATH_DEBUG_PRESENT) == 0;
}

// CE (full GPU/UI): true when UI windows are composited on the GPU (each window a
// GPU texture, z-ordered at flip) instead of CPU-blitted into the framebuffer.
// Deliberately NOT gated on zoom (unlike should_use_gpu_path / present_path):
// during zoom the world still renders CPU into the iso window's VB (dword_602DF0),
// but keeping gpu-ui ACTIVE lets the GPU window walk composite that iso VB through
// its fresh per-frame mirror (gpu_world is off during zoom so the walk treats the
// iso like a normal window), alongside the UI windows' mirrors. So zoom presents
// entirely through the walk's fresh mirrors instead of the stale CPU framebuffer
// — which is what turned dynamic UI (portraits, gold stacks, the wmap) black at
// non-1.0 zoom. The world+roof underlay is cleared during zoom (present_path is
// off); the iso mirror carries the downscaled world instead.
static bool tile_gpu_ui_path(void)
{
    if (tile_gpu_path_disabled) {
        return false;
    }
    // "hardware" is the full path (GPU world + GPU UI compositing).
    return strcmp(tile_render_path_canon(), TILE_RENDER_PATH_HARDWARE) == 0;
}

// CE (gpu-ui iso overlay port): public — true when the CPU iso overlays (speech
// bubbles, floating text, dialogue) composite over the GPU world via the window walk
// (gpu-ui active). gamelib gates skipping the legacy iso-surface overlay draws on
// this so the walk's GPU composite is authoritative.
bool tile_gpu_iso_overlay_path(void)
{
    return tile_gpu_ui_path();
}

// CE (gpu present): public -- true when the GPU world target is the persistent,
// present-time target (gpu-present or gpu-ui, outside the zoom pass). The target is
// NOT cleared/re-uploaded per frame, so a camera move (scroll) leaves its un-redrawn
// regions stale/misaligned; gamelib uses this to force a full world re-render on
// camera move (the same fix the zoom path already does), keeping the world fresh
// under fade roofs (which reveal it).
bool tile_gpu_present_active(void)
{
    return tile_gpu_present_path();
}

// Allocate or resize the GPU world target to match `dst`. Returns false
// on hard failure (and trips tile_gpu_path_disabled so we don't retry).
static bool tile_gpu_ensure_world_buffer(TigVideoBuffer* dst)
{
    TigVideoBufferData dst_data;
    if (dst == NULL || tig_video_buffer_data(dst, &dst_data) != TIG_OK) {
        tile_gpu_path_disabled = true;
        return false;
    }

    int desired_w = dst_data.width;
    int desired_h = dst_data.height;

    if (tile_gpu_world_buffer != NULL
        && tile_gpu_world_buffer_w == desired_w
        && tile_gpu_world_buffer_h == desired_h) {
        return true;
    }

    if (tile_gpu_world_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_world_buffer);
        tile_gpu_world_buffer = NULL;
    }

    TigVideoBufferCreateInfo info;
    info.flags = TIG_VIDEO_BUFFER_CREATE_TEXTURE;
    info.width = desired_w;
    info.height = desired_h;
    info.color_key = 0;
    info.background_color = 0;
    if (tig_video_buffer_create(&info, &tile_gpu_world_buffer) != TIG_OK
        || tile_gpu_world_buffer == NULL) {
        tig_debug_printf("tile: GPU world buffer create (%dx%d) failed -- falling back to software.\n",
            desired_w, desired_h);
        tile_gpu_world_buffer = NULL;
        tile_gpu_path_disabled = true;
        return false;
    }
    tile_gpu_world_buffer_w = desired_w;
    tile_gpu_world_buffer_h = desired_h;
    return true;
}

// CE (step 6): allocate/resize the roof present-layer texture to match the world
// buffer. Returns false on failure (the roof pass is then skipped this frame).
static bool tile_gpu_ensure_roof_buffer(void)
{
    int desired_w = tile_gpu_world_buffer_w;
    int desired_h = tile_gpu_world_buffer_h;
    if (desired_w <= 0 || desired_h <= 0) {
        return false;
    }
    if (tile_gpu_roof_buffer != NULL
        && tile_gpu_roof_buffer_w == desired_w
        && tile_gpu_roof_buffer_h == desired_h) {
        return true;
    }
    if (tile_gpu_roof_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_roof_buffer);
        tile_gpu_roof_buffer = NULL;
    }
    TigVideoBufferCreateInfo info;
    info.flags = TIG_VIDEO_BUFFER_CREATE_TEXTURE;
    info.width = desired_w;
    info.height = desired_h;
    info.color_key = 0;
    info.background_color = 0;
    if (tig_video_buffer_create(&info, &tile_gpu_roof_buffer) != TIG_OK
        || tile_gpu_roof_buffer == NULL) {
        tile_gpu_roof_buffer = NULL;
        return false;
    }
    tile_gpu_roof_buffer_w = desired_w;
    tile_gpu_roof_buffer_h = desired_h;
    return true;
}

// CE (zoom->GPU): allocate/resize the 2x zoom target to (w, h). On failure, trips
// tile_gpu_zoom_disabled (zoom stays on the CPU path) WITHOUT disabling all GPU.
static bool tile_gpu_ensure_zoom_buffer(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (tile_gpu_zoom_buffer != NULL
        && tile_gpu_zoom_buffer_w == w
        && tile_gpu_zoom_buffer_h == h) {
        return true;
    }
    if (tile_gpu_zoom_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_zoom_buffer);
        tile_gpu_zoom_buffer = NULL;
    }
    tile_gpu_zoom_warmed = false; // a (re)created target needs warming again
    TigVideoBufferCreateInfo info;
    info.flags = TIG_VIDEO_BUFFER_CREATE_TEXTURE;
    info.width = w;
    info.height = h;
    info.color_key = 0;
    info.background_color = 0;
    uint64_t alloc_t0 = tile_gpu_now_ns();
    if (tig_video_buffer_create(&info, &tile_gpu_zoom_buffer) != TIG_OK
        || tile_gpu_zoom_buffer == NULL) {
        tig_debug_printf("tile: GPU zoom buffer create (%dx%d) failed -- zoom stays on CPU path.\n", w, h);
        tile_gpu_zoom_buffer = NULL;
        tile_gpu_zoom_disabled = true;
        return false;
    }
    tile_gpu_zoom_buffer_w = w;
    tile_gpu_zoom_buffer_h = h;
    tig_debug_printf("[zoom-alloc] GPU zoom buffer %dx%d created in %.2fms\n",
        w, h, (double)(tile_gpu_now_ns() - alloc_t0) / 1e6);
    return true;
}

// CE (zoom roof layer): allocate/resize the 2x zoom roof buffer (matches the zoom
// world buffer). Returns false on create failure (the caller then bakes roofs into the
// world buffer as before -- the old behavior, just without the no-accumulation win).
static bool tile_gpu_ensure_zoom_roof_buffer(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (tile_gpu_zoom_roof_buffer != NULL
        && tile_gpu_zoom_roof_buffer_w == w
        && tile_gpu_zoom_roof_buffer_h == h) {
        return true;
    }
    if (tile_gpu_zoom_roof_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_zoom_roof_buffer);
        tile_gpu_zoom_roof_buffer = NULL;
    }
    TigVideoBufferCreateInfo info;
    info.flags = TIG_VIDEO_BUFFER_CREATE_TEXTURE;
    info.width = w;
    info.height = h;
    info.color_key = 0;
    info.background_color = 0;
    if (tig_video_buffer_create(&info, &tile_gpu_zoom_roof_buffer) != TIG_OK
        || tile_gpu_zoom_roof_buffer == NULL) {
        tig_debug_printf("tile: GPU zoom roof buffer create (%dx%d) failed -- roofs bake into the zoom world buffer.\n", w, h);
        tile_gpu_zoom_roof_buffer = NULL;
        return false;
    }
    tile_gpu_zoom_roof_buffer_w = w;
    tile_gpu_zoom_roof_buffer_h = h;
    return true;
}

// CE (zoom->GPU): true when the zoomed world should render on the GPU (gpu-ui only).
// Ensures the 2x buffer (sized to 2x the screen-sized world buffer) so the caller
// can commit to the GPU zoom path; returns false -> caller uses the CPU zoom path.
bool tile_gpu_zoom_is_enabled(void)
{
    if (tile_gpu_path_disabled || tile_gpu_zoom_disabled) {
        return false;
    }
    if (!tile_gpu_ui_path()) {
        return false;
    }
    // 2x the screen-sized world buffer. That buffer is established by the zoom-1.0
    // GPU pass; before any 1.0 pass we can't size the zoom buffer, so stay on CPU.
    if (tile_gpu_world_buffer_w <= 0 || tile_gpu_world_buffer_h <= 0) {
        return false;
    }
    return tile_gpu_ensure_zoom_buffer(tile_gpu_world_buffer_w * 2, tile_gpu_world_buffer_h * 2);
}

// CE (zoom idle-time preload): the GPU driver sets up its render-to-texture pipeline
// on the FIRST render to a fresh render target -- a one-time ~70ms hitch the first time
// the player zooms (the passes themselves are sub-millisecond; the alloc is 0.2ms). To
// hide it, warm both zoom targets (world + roof) off-screen on an idle frame after load
// so the first real zoom hits an already-warm pipeline. Self-gating: runs once, only on
// the hardware zoom path, only when no world/zoom render is in flight, and only once the
// world buffer exists (to use as a representative blit source). Called per-frame from
// gamelib_draw before the dirty early-return, so it fires during post-load idle.
void tile_gpu_zoom_prewarm(void)
{
    if (tile_gpu_zoom_warmed || tile_gpu_zoom_disabled || tile_gpu_active) {
        return;
    }
    if (!tile_gpu_zoom_is_enabled()) {
        return; // not the hardware zoom path, or the world buffer isn't sized yet
    }
    // tile_gpu_zoom_is_enabled ensured the world zoom buffer; ensure the roof one too.
    tile_gpu_ensure_zoom_roof_buffer(tile_gpu_world_buffer_w * 2, tile_gpu_world_buffer_h * 2);

    SDL_Texture* src = tile_gpu_world_buffer != NULL
        ? tig_video_buffer_get_sdl_texture(tile_gpu_world_buffer)
        : NULL;
    if (src == NULL) {
        return; // need a source texture to exercise the texture->target blit pipeline
    }
    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return;
    }
    SDL_Texture* targets[2] = {
        tig_video_buffer_get_sdl_texture(tile_gpu_zoom_buffer),
        tile_gpu_zoom_roof_buffer != NULL
            ? tig_video_buffer_get_sdl_texture(tile_gpu_zoom_roof_buffer)
            : NULL,
    };
    SDL_Texture* prev = SDL_GetRenderTarget(renderer);
    uint64_t t0 = tile_gpu_now_ns();
    for (int i = 0; i < 2; i++) {
        if (targets[i] == NULL) {
            continue;
        }
        if (SDL_SetRenderTarget(renderer, targets[i])) {
            SDL_RenderTexture(renderer, src, NULL, NULL); // first render -> pipeline warm
        }
    }
    SDL_SetRenderTarget(renderer, prev); // restore the target + flush the warm batch
    tile_gpu_zoom_warmed = true;
    tig_debug_printf("[zoom-prewarm] warmed world+roof zoom targets in %.2fms\n",
        (double)(tile_gpu_now_ns() - t0) / 1e6);
}

// Upload the current CPU dst surface into the GPU world target and bind
// the target as the render target. Returns true on success; on failure
// the caller falls back to the software path for this frame.
//
// The upload is the Phase 3 "bridge": tiles with colorkeyed edges leave
// dst pixels visible, so we need the GPU target to start with the dst's
// current contents (light pass output, prior frame's tile output, etc.)
// instead of stale/uninitialized pixels.
static bool tile_gpu_begin_pass(TigVideoBuffer* dst)
{
    if (!tile_gpu_ensure_world_buffer(dst)) {
        return false;
    }

    SDL_Texture* gpu_tex = tig_video_buffer_get_sdl_texture(tile_gpu_world_buffer);
    if (gpu_tex == NULL) {
        return false;
    }

    // CE (step 6 present mode): the world target is persistent and presented
    // directly (no readback to re-sync it), so skip the per-frame upload -- it
    // would overwrite the target with the now-stale CPU surface. The target
    // retains its own content frame-to-frame.
    if (!tile_gpu_present_path()) {
        // Pull CPU pixels from dst. tig_video_buffer_data with no lock returns
        // pixels=NULL, so we lock here to grab a pointer + pitch, then unlock.
        if (tig_video_buffer_lock(dst) != TIG_OK) {
            return false;
        }
        TigVideoBufferData dst_data;
        if (tig_video_buffer_data(dst, &dst_data) != TIG_OK || dst_data.pixels == NULL) {
            tig_video_buffer_unlock(dst);
            return false;
        }

        SDL_Rect upload_rect = { 0, 0, dst_data.width, dst_data.height };
        uint64_t upload_t0 = tile_gpu_now_ns();
        bool upload_ok = SDL_UpdateTexture(gpu_tex, &upload_rect, dst_data.pixels, dst_data.pitch);
        tile_gpu_perf_upload_ns += tile_gpu_now_ns() - upload_t0;
        tig_video_buffer_unlock(dst);
        if (!upload_ok) {
            tig_debug_printf("tile_gpu_begin_pass: SDL_UpdateTexture failed: %s\n", SDL_GetError());
            return false;
        }
    }

    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return false;
    }
    if (!SDL_SetRenderTarget(renderer, gpu_tex)) {
        tig_debug_printf("tile_gpu_begin_pass: SDL_SetRenderTarget failed: %s\n", SDL_GetError());
        return false;
    }
    tile_gpu_target_buffer = tile_gpu_world_buffer; // dispatch draws to the world buffer
    return true;
}

// Read the GPU world target back into the CPU dst surface and unbind the
// target. Logs but does not raise on individual failures -- the next
// frame will retry from begin_pass.
static void tile_gpu_end_pass(TigVideoBuffer* dst)
{
    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return;
    }

    // ReadPixels reads from the *current* render target; the begin_pass
    // bound the GPU world target so we can read directly.
    SDL_Rect read_rect = { 0, 0, tile_gpu_world_buffer_w, tile_gpu_world_buffer_h };
    uint64_t readback_t0 = tile_gpu_now_ns();
    SDL_Surface* read_surface = SDL_RenderReadPixels(renderer, &read_rect);
    if (read_surface == NULL) {
        tig_debug_printf("tile_gpu_end_pass: SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        tile_gpu_perf_readback_ns += tile_gpu_now_ns() - readback_t0;
        SDL_SetRenderTarget(renderer, NULL);
        return;
    }

    // Blit-with-conversion into the CPU dst surface. SDL handles any
    // format mismatch (RenderReadPixels may return ARGB8888 instead of
    // XRGB8888 depending on the driver).
    if (tig_video_buffer_lock(dst) == TIG_OK) {
        TigVideoBufferData dst_data;
        if (tig_video_buffer_data(dst, &dst_data) == TIG_OK && dst_data.pixels != NULL) {
            SDL_Surface* dst_surface = SDL_CreateSurfaceFrom(dst_data.width, dst_data.height,
                SDL_PIXELFORMAT_XRGB8888, dst_data.pixels, dst_data.pitch);
            if (dst_surface != NULL) {
                SDL_BlitSurface(read_surface, NULL, dst_surface, NULL);
                SDL_DestroySurface(dst_surface);
            }
        }
        tig_video_buffer_unlock(dst);
    }
    SDL_DestroySurface(read_surface);
    tile_gpu_perf_readback_ns += tile_gpu_now_ns() - readback_t0;
    SDL_SetRenderTarget(renderer, NULL);
}

// CE (feature/perf-gpu-accel Phase 3): dispatch a tile blit through the
// CPU or GPU path based on `tile_gpu_active`. Called from tile_draw_iso
// at every site that used to call tig_art_blit directly. Software path
// is bit-identical to the original.
// Queue a blit that the GPU path can't draw this frame (unsupported blend, or
// a cache miss) for replay with tig_art_blit *after* the readback, onto the CPU
// world surface (dword_602DF0). Drawing it now would be clobbered by the
// readback. Deep-copies the rects (and the LERP corner colors / sub-rect, which
// are stack locals in tile_draw_iso); other pointers (palette, COLOR_ARRAY
// field_14) point at persistent per-object storage and stay valid until replay.
static void tile_gpu_defer_blit(TigArtBlitInfo* art_info)
{
    if (tile_deferred_blit_count >= TILE_DEFERRED_BLIT_MAX) {
        // Overflow (shouldn't happen with the original-palette cache): draw
        // immediately to the CPU surface and accept a possible clobber.
        TigArtBlitInfo tmp = *art_info;
        tmp.dst_video_buffer = dword_602DF0;
        tig_art_blit(&tmp);
        return;
    }

    TileDeferredBlit* d = &tile_deferred_blits[tile_deferred_blit_count++];
    d->info = *art_info;
    d->info.dst_video_buffer = dword_602DF0; // objects arrive with dst unset
    d->src_rect = *art_info->src_rect;
    d->info.src_rect = &d->src_rect;
    d->dst_rect = *art_info->dst_rect;
    d->info.dst_rect = &d->dst_rect;
    if ((art_info->flags & TIG_ART_BLT_BLEND_COLOR_LERP) != 0) {
        if (art_info->field_14 != NULL) {
            memcpy(d->lerp_colors, art_info->field_14, sizeof(d->lerp_colors));
            d->info.field_14 = d->lerp_colors;
        }
        if (art_info->field_18 != NULL) {
            d->lerp_rect = *art_info->field_18;
            d->info.field_18 = &d->lerp_rect;
        }
    }
}

// Shared GPU blit dispatch for the world passes (tile/object/roof). Returns
// true if the blit was handled -- drawn on the GPU world target, or queued for
// CPU replay at tile_gpu_world_end -- and false if the GPU pass is inactive and
// the caller should perform its own (CPU) blit.
// CE DEBUG: one-frame dispatch trace. Set ARCANUM_GPU_TRACE=1 in the env, then
// load the scene; on the first GPU world pass after the env is seen, every
// dispatch logs its art type / flags / dst rect / path (gpu / oneshot-override /
// deferred). The trace self-disables after one frame so the log isn't flooded.
static int tile_gpu_trace_state; // 0 = off, 1 = arm next frame, 2 = tracing now
static int tile_gpu_trace_order;

// Called from the harness `trace` command. Arms the trace for the NEXT world
// pass; world_end re-arms across empty (non-rendering) frames until a frame
// with real content gets logged.
void tile_gpu_trace_arm(void)
{
    if (tile_gpu_trace_state == 0) {
        tile_gpu_trace_state = 1;
    }
}

static const char* tile_gpu_art_type_name(int t)
{
    switch (t) {
    case TIG_ART_TYPE_TILE:        return "TILE";
    case TIG_ART_TYPE_WALL:        return "WALL";
    case TIG_ART_TYPE_CRITTER:     return "CRITTER";
    case TIG_ART_TYPE_PORTAL:      return "PORTAL";
    case TIG_ART_TYPE_SCENERY:     return "SCENERY";
    case TIG_ART_TYPE_INTERFACE:   return "INTERFACE";
    case TIG_ART_TYPE_ITEM:        return "ITEM";
    case TIG_ART_TYPE_CONTAINER:   return "CONTAINER";
    case TIG_ART_TYPE_LIGHT:       return "LIGHT";
    case TIG_ART_TYPE_ROOF:        return "ROOF";
    case TIG_ART_TYPE_FACADE:      return "FACADE";
    case TIG_ART_TYPE_MONSTER:     return "MONSTER";
    case TIG_ART_TYPE_UNIQUE_NPC:  return "UNIQUE_NPC";
    case TIG_ART_TYPE_EYE_CANDY:   return "EYE_CANDY";
    case TIG_ART_TYPE_MISC:        return "MISC";
    default:                       return "?";
    }
}

static void tile_gpu_trace_log(const char* path, TigArtBlitInfo* a)
{
    int dx = (a->dst_rect != NULL) ? a->dst_rect->x : -1;
    int dy = (a->dst_rect != NULL) ? a->dst_rect->y : -1;
    int dw = (a->dst_rect != NULL) ? a->dst_rect->width : -1;
    int dh = (a->dst_rect != NULL) ? a->dst_rect->height : -1;
    tig_debug_printf("GPU#%d %-9s %-6s art=0x%08x flags=0x%08x dst=(%d,%d %dx%d)\n",
        tile_gpu_trace_order++, path, tile_gpu_art_type_name(tig_art_type(a->art_id)),
        (unsigned int)a->art_id, (unsigned int)a->flags, dx, dy, dw, dh);
}

bool tile_gpu_dispatch(TigArtBlitInfo* art_info)
{
    if (!tile_gpu_active) {
        return false;
    }

    // The blit primitive covers FLIP / COLOR_LERP / COLOR_CONST / COLOR_ARRAY
    // (mapped to LERP) / ADD / SUB / MUL / ALPHA_CONST / ALPHA_LERP_X/Y/BOTH
    // (all 3 map to per-corner alphas in the bilinear grid). These flags it
    // can't reproduce, so any blit carrying one is deferred to CPU replay.
    // STIPPLE_D would defer wall/roof fades to CPU and break z-order; the
    // dispatch translates STIPPLE_D into an ALPHA_CONST=128 approximation
    // below to keep them on the GPU in z-order.
    const unsigned int gpu_reject_blends = TIG_ART_BLT_BLEND_ALPHA_AVG
        | TIG_ART_BLT_BLEND_ALPHA_SRC
        | TIG_ART_BLT_BLEND_ALPHA_STIPPLE_S;
    // Color/palette intents the original-palette art cache can serve. A blit
    // with none of these (a plain working-palette blit) would render unlit on
    // GPU, so it's deferred -- except PALETTE_OVERRIDE, handled below.
    const unsigned int gpu_ok_intent = TIG_ART_BLT_BLEND_COLOR_LERP
        | TIG_ART_BLT_BLEND_COLOR_CONST | TIG_ART_BLT_BLEND_COLOR_ARRAY
        | TIG_ART_BLT_PALETTE_ORIGINAL;

    SDL_Texture* src_tex = NULL;
    SDL_Texture* oneshot_tex = NULL; // owned here; freed in tile_gpu_world_end
    // When a blit arrives with NO color intent, it's a plain working-palette
    // blit -- the software path bakes the ambient tint into art->palette_tbl.
    // The GPU cache holds the ORIGINAL palette, so we synthesize that same
    // ambient tint as a runtime COLOR_CONST (see light_default_tint_for).
    // Without this these blits (walls/scenery in flat-ambient zones) would
    // defer to the post-readback CPU replay and land on top of the sprites in
    // front of them -- the wall-over-character bug.
    tig_color_t implicit_tint = 0;
    bool implicit_tint_set = false;
    if ((art_info->flags & gpu_reject_blends) == 0) {
        if ((art_info->flags & TIG_ART_BLT_PALETTE_OVERRIDE) != 0 && art_info->palette != NULL) {
            // Recolored object / sign: render the art through its OVERRIDE
            // palette to a one-shot texture and draw it in z-order here, rather
            // than deferring (deferred blits replay after the readback, so a
            // recolored wall drew on top of the sprites in front of it). Not
            // cached: override palettes can change in place and these are rare.
            TigVideoBuffer* ovr = NULL;
            if (tig_art_render_with_palette(art_info->art_id, art_info->palette, &ovr) == TIG_OK
                && ovr != NULL) {
                oneshot_tex = tig_video_buffer_upload_to_texture(ovr);
                tig_video_buffer_destroy(ovr);
                src_tex = oneshot_tex;
            }
        } else if ((art_info->flags & gpu_ok_intent) != 0) {
            src_tex = tig_art_gpu_cache_get(art_info->art_id);
        } else {
            // No color/palette intent: plain (or alpha-only) working-palette
            // blit. Render through the original-palette cache and apply the
            // ambient tint as COLOR_CONST below.
            src_tex = tig_art_gpu_cache_get(art_info->art_id);
            if (src_tex != NULL && light_default_tint_for(art_info->art_id, &implicit_tint)) {
                implicit_tint_set = true;
            }
        }
    }
    if (src_tex == NULL) {
        if (tile_gpu_trace_state == 2) tile_gpu_trace_log("DEFER", art_info);
        tile_gpu_defer_blit(art_info);
        return true;
    }
    if (tile_gpu_trace_state == 2) {
        tile_gpu_trace_log((oneshot_tex != NULL) ? "GPU-OVR" : "GPU", art_info);
    }

    // Defer freeing the one-shot texture until after the readback flush.
    if (oneshot_tex != NULL) {
        if (tile_oneshot_tex_count < TILE_ONESHOT_TEX_MAX) {
            tile_oneshot_textures[tile_oneshot_tex_count++] = oneshot_tex;
        } else {
            // Overflow (cap is far above any real override count): free now to
            // avoid a leak. The blit was already issued; this risks a mid-batch
            // destroy only in the pathological >512-overrides/frame case.
            SDL_DestroyTexture(oneshot_tex);
        }
    }

    TigVideoBufferBlitGpuInfo gpu_info;
    memset(&gpu_info, 0, sizeof(gpu_info));
    gpu_info.src_texture = src_tex;
    gpu_info.src_rect = art_info->src_rect;
    gpu_info.dst_video_buffer = (tile_gpu_target_buffer != NULL)
        ? tile_gpu_target_buffer
        : tile_gpu_world_buffer;
    gpu_info.dst_rect = art_info->dst_rect;

    TigVideoBufferBlitFlags vb_flags = 0;
    if ((art_info->flags & TIG_ART_BLT_FLIP_X) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_FLIP_X;
    }
    if ((art_info->flags & TIG_ART_BLT_FLIP_Y) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_FLIP_Y;
    }
    // Arithmetic blends (object eye-candy / shadows). At most one applies.
    if ((art_info->flags & TIG_ART_BLT_BLEND_ADD) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ADD;
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_SUB) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_SUB;
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_MUL) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_MUL;
    }
    // Alpha modulation. ALPHA_CONST = uniform. ALPHA_LERP_X/Y/BOTH translate
    // to per-corner alphas under ALPHA_LERP (vbuffer-side), matching the
    // software art_blit mapping (art.c:802-820). STIPPLE_D is approximated as
    // ALPHA_CONST=128 -- the visual differs (stipple is dither, alpha is
    // smooth) but the in-z-order render avoids the deferral-on-top artifact.
    if ((art_info->flags & TIG_ART_BLT_BLEND_ALPHA_CONST) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST;
        gpu_info.alpha[0] = art_info->alpha[0];
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_ALPHA_LERP_X) != 0) {
        // X variant: alpha[0]=left, alpha[1]=right -> corners (L,R,R,L).
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_LERP;
        gpu_info.alpha[0] = art_info->alpha[0];
        gpu_info.alpha[1] = art_info->alpha[0];
        gpu_info.alpha[2] = art_info->alpha[1];
        gpu_info.alpha[3] = art_info->alpha[1];
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_ALPHA_LERP_Y) != 0) {
        // Y variant: alpha[0]=top, alpha[1]=bottom -> corners (T,T,B,B).
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_LERP;
        gpu_info.alpha[0] = art_info->alpha[0];
        gpu_info.alpha[1] = art_info->alpha[0];
        gpu_info.alpha[2] = art_info->alpha[1];
        gpu_info.alpha[3] = art_info->alpha[1];
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_ALPHA_LERP_BOTH) != 0) {
        // BOTH variant: alpha[0..3] are the 4 corners directly (TL,TR,BR,BL).
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_LERP;
        gpu_info.alpha[0] = art_info->alpha[0];
        gpu_info.alpha[1] = art_info->alpha[1];
        gpu_info.alpha[2] = art_info->alpha[2];
        gpu_info.alpha[3] = art_info->alpha[3];
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_ALPHA_STIPPLE_D) != 0) {
        // Approximate stipple dither with uniform 50% alpha so the blit stays
        // in z-order on the GPU instead of deferring (which lands it on top).
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST;
        gpu_info.alpha[0] = 128;
    }
    if ((art_info->flags & TIG_ART_BLT_BLEND_COLOR_LERP) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP;
        if (art_info->field_18 != NULL && art_info->field_14 != NULL) {
            gpu_info.lerp_rect = art_info->field_18;
            gpu_info.lerp_colors[0] = art_info->field_14[0];
            gpu_info.lerp_colors[1] = art_info->field_14[1];
            gpu_info.lerp_colors[2] = art_info->field_14[2];
            gpu_info.lerp_colors[3] = art_info->field_14[3];
        }
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_COLOR_CONST) != 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST;
        gpu_info.lerp_colors[0] = art_info->color;
    } else if ((art_info->flags & TIG_ART_BLT_BLEND_COLOR_ARRAY) != 0
        && art_info->field_14 != NULL) {
        // Full per-column light field. field_14[i] is the lit color of screen
        // column i across the wall's full width; the GPU samples it per grid
        // column (blit_gpu COLOR_ARRAY path) so the vignette flows smoothly
        // across the wall AND across adjacent walls -- their shared edge columns
        // sample the same world position and agree, so no seam.
        //
        // light_hardware_accelerated is false in this build, so sub_4DC210
        // leaves the array un-collapsed (full per-column). The old code read
        // field_14[0]/[1] as left/right endpoints, but [1] is the SECOND column
        // (~equal to [0]), which flattened each wall to roughly its left-edge
        // tint and seamed hard at every tile boundary -- the reported bug.
        int aw = 0;
        int ah = 0;
        if (tig_art_size(art_info->art_id, &aw, &ah) == TIG_OK && aw > 0) {
            if (aw > 160) {
                aw = 160; // ObjectRenderColors holds 160 columns
            }
            vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_ARRAY;
            gpu_info.color_array = (const uint32_t*)art_info->field_14;
            gpu_info.color_array_count = aw;
        } else {
            vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST;
            gpu_info.lerp_colors[0] = art_info->field_14[0];
        }
    }
    // Implicit ambient tint for plain working-palette blits (no color intent of
    // their own). Skipped if a color modulation is already set.
    if (implicit_tint_set
        && (vb_flags & (TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP
                | TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST
                | TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_ARRAY)) == 0) {
        vb_flags |= TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST;
        gpu_info.lerp_colors[0] = implicit_tint;
    }
    gpu_info.flags = vb_flags;

    tig_video_buffer_blit_gpu(&gpu_info);
    return true;
}

// Convenience for tile_draw_iso's own blit sites: dispatch to GPU, else CPU.
static int tile_blit_dispatch(TigArtBlitInfo* art_info)
{
    if (tile_gpu_dispatch(art_info)) {
        return TIG_OK;
    }
    return tig_art_blit(art_info);
}

// 0x4D6840
bool tile_init(GameInitInfo* init_info)
{
    if (tig_window_vbid_get(init_info->iso_window_handle, &dword_602DF0) != TIG_OK) {
        return false;
    }

    tile_hardware_accelerated = tig_video_3d_check_initialized() == TIG_OK;
    tile_iso_window_handle = init_info->iso_window_handle;
    tile_invalidate_rect = init_info->invalidate_rect_func;
    tile_view_options.type = VIEW_TYPE_ISOMETRIC;
    tile_visible = true;

    // CE (feature/perf-gpu-accel Phase 3): if arcanum.cfg requests the GPU
    // tile path, initialize the art-texture cache now. Cheap if "software"
    // (init is a no-op until first get) but cleaner to gate up-front so
    // the cache doesn't allocate buckets in the software-only case.
    if (tile_should_use_gpu_path()) {
        if (!tig_art_gpu_cache_init(0)) {
            tig_debug_printf("tile_init: art GPU cache init failed -- falling back to software.\n");
            tile_gpu_path_disabled = true;
        } else {
            tig_debug_printf("tile_init: tile render path = gpu (art GPU cache initialized).\n");
        }
    }

    return true;
}

// 0x4D68A0
void tile_exit(void)
{
    sub_4D7980();
    tile_iso_window_handle = TIG_WINDOW_HANDLE_INVALID;
    tile_invalidate_rect = NULL;

    if (tile_gpu_world_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_world_buffer);
        tile_gpu_world_buffer = NULL;
        tile_gpu_world_buffer_w = 0;
        tile_gpu_world_buffer_h = 0;
    }
    if (tile_gpu_zoom_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_zoom_buffer);
        tile_gpu_zoom_buffer = NULL;
        tile_gpu_zoom_buffer_w = 0;
        tile_gpu_zoom_buffer_h = 0;
    }
    if (tile_gpu_zoom_roof_buffer != NULL) {
        tig_video_buffer_destroy(tile_gpu_zoom_roof_buffer);
        tile_gpu_zoom_roof_buffer = NULL;
        tile_gpu_zoom_roof_buffer_w = 0;
        tile_gpu_zoom_roof_buffer_h = 0;
    }
    tile_gpu_zoom_roof_active = false;
    tile_gpu_zoom_disabled = false;
    tig_art_gpu_cache_exit();
    tile_gpu_path_disabled = false;
}

// 0x4D68C0
void tile_resize(GameResizeInfo* resize_info)
{
    if (tig_window_vbid_get(resize_info->window_handle, &dword_602DF0) != TIG_OK) {
        tig_debug_printf("tile_resize: ERROR: couldn't grab window vbid!");
        exit(EXIT_FAILURE);
    }

    tile_iso_window_handle = resize_info->window_handle;
}

void tile_set_render_target(TigVideoBuffer* vb)
{
    dword_602DF0 = vb;
}

// 0x4D6900
void tile_update_view(ViewOptions* view_options)
{
    sub_4D79C0(view_options);
    tile_view_options = *view_options;
}

// 0x4D6930
void tile_toggle_visibility(void)
{
    tile_visible = !tile_visible;
}

// 0x4D6950
void tile_draw(GameDrawInfo* draw_info)
{
    if (!tile_visible) {
        return;
    }

    switch (tile_view_options.type) {
    case VIEW_TYPE_ISOMETRIC:
        tile_draw_iso(draw_info);
        break;
    case VIEW_TYPE_TOP_DOWN:
        // NOTE: Refactored into separate function for clarity.
        tile_draw_topdown(draw_info);
        break;
    }
}

// 0x4D7090
int tile_id_from_loc(int64_t loc)
{
    int tile_x;
    int tile_y;

    tile_x = LOCATION_GET_X(loc) & 0x3F;
    tile_y = LOCATION_GET_Y(loc) & 0x3F;

    return TILE_MAKE(tile_x, tile_y);
}

// 0x4D70B0
tig_art_id_t tile_art_id_at(int64_t loc)
{
    int64_t sector_id;
    Sector* sector;
    int tile;
    tig_art_id_t art_id;

    sector_id = sector_id_from_loc(loc);
    if (!sector_lock(sector_id, &sector)) {
        return TIG_ART_ID_INVALID;
    }

    tile = tile_id_from_loc(loc);
    art_id = sector->tiles.art_ids[tile];
    sector_unlock(sector_id);

    return art_id;
}

// 0x4D7110
bool tile_is_blocking(int64_t loc, bool a2)
{
    tig_art_id_t art_id;
    bool v1;

    if (tileblock_is_tile_blocked(loc)) {
        return true;
    }

    art_id = tile_art_id_at(loc);
    if (tig_art_type(art_id) == TIG_ART_TYPE_FACADE) {
        return !tig_art_facade_id_walkable_get(art_id);
    }

    v1 = a_name_tile_is_blocking(art_id);
    if (a2) {
        if (v1) {
            // FIXME: Useless.
            tile_is_sinkable(loc);
        }
        v1 = false;
    }

    return v1;
}

// 0x4D7180
bool tile_is_soundproof(int64_t loc)
{
    return a_name_tile_is_soundproof(tile_art_id_at(loc));
}

// 0x4D71A0
bool tile_is_sinkable(int64_t loc)
{
    return a_name_tile_is_sinkable(tile_art_id_at(loc));
}

// 0x4D71C0
bool tile_is_slippery(int64_t loc)
{
    return a_name_tile_is_slippery(tile_art_id_at(loc));
}

// 0x4D71E0
void sub_4D71E0(void)
{
    // TODO: Incomplete.
}

// 0x4D7430
void sub_4D7430(int64_t loc)
{
    tig_art_id_t art_id;

    art_id = tile_art_id_at(loc);
    do {
        if (art_id != TIG_ART_ID_INVALID) {
            art_id = sub_503800(art_id, sub_5037B0(art_id) + 1);
        }
    } while (tig_art_exists(art_id) != TIG_OK);

    sub_4D7820(loc, art_id);
}

// 0x4D7480
tig_art_id_t sub_4D7480(tig_art_id_t art_id, int num2, bool flippable2, int a4)
{
    int num1;
    int type;
    int flippable1;
    int palette;
    int v1;
    int v2;
    int cnt;
    int tmp;

    num1 = tig_art_tile_id_num1_get(art_id);
    type = tig_art_tile_id_type_get(art_id);
    flippable1 = tig_art_tile_id_type_get(art_id);
    palette = tig_art_id_palette_get(art_id);

    if (flippable1) {
        if (flippable2 && num2 < num1) {
            v1 = 15 - a4;

            tmp = num2;
            num2 = num1;
            num1 = tmp;

            tmp = flippable2;
            flippable2 = flippable1;
            flippable1 = tmp;
        } else {
            v1 = a4;
        }
    } else {
        if (flippable2 || num2 < num1) {
            v1 = 15 - a4;

            tmp = num2;
            num2 = num1;
            num1 = tmp;

            tmp = flippable2;
            flippable2 = flippable1;
            flippable1 = tmp;
        } else {
            v1 = a4;
        }
    }

    if (v1 == 0) {
        flippable1 = flippable2;
        num1 = num2;
    } else if (num1 == num2 && flippable1 == flippable2) {
        v1 = 0;
    } else if (v1 == 15) {
        flippable2 = flippable1;
        num2 = num1;
        v1 = 0;
    }

    cnt = sub_4EBEF0(num1, num2, v1, type, flippable1, flippable2);
    v2 = random_between(0, cnt - 1);

    if (flippable1
        && flippable2
        && num1 != num2
        && random_between(0, 1) != 0) {
        v2 += 8;
    }

    tig_art_tile_id_create(num1, num2, v1, v2, type, flippable1, flippable2, palette, &art_id);

    return art_id;
}

// 0x4D7590
void sub_4D7590(tig_art_id_t art_id, TigVideoBuffer* video_buffer)
{
    TigRect rect;
    TigArtBlitInfo art_blit_spec;
    TigVideoBufferData src_video_buffer_data;
    TigVideoBufferData dst_video_buffer_data;

    if (tile_view_options.type == VIEW_TYPE_TOP_DOWN) {
        rect.x = 0;
        rect.y = 0;
        rect.width = 80;
        rect.height = 40;

        art_blit_spec.flags = TIG_ART_BLT_PALETTE_ORIGINAL;
        art_blit_spec.art_id = art_id;
        art_blit_spec.src_rect = &rect;
        art_blit_spec.dst_video_buffer = dword_602DE0;
        art_blit_spec.dst_rect = &rect;
        tig_art_blit(&art_blit_spec);

        tig_video_buffer_lock(dword_602DE0);
        tig_video_buffer_data(dword_602DE0, &src_video_buffer_data);

        tig_video_buffer_lock(video_buffer);
        tig_video_buffer_data(video_buffer, &dst_video_buffer_data);

        for (int y = 0; y < tile_view_options.zoom; y++) {
            for (int x = 0; x < tile_view_options.zoom; x++) {
                int index = y * tile_view_options.zoom + x;
                int src_index = dword_602DE4[index] * src_video_buffer_data.pitch / 4 + dword_602DE8[index];
                int dst_index = y * dst_video_buffer_data.pitch / 4 + x;
                ((uint32_t*)dst_video_buffer_data.pixels)[dst_index] = ((uint32_t*)src_video_buffer_data.pixels)[src_index];
            }
        }

        tig_video_buffer_unlock(video_buffer);
        tig_video_buffer_unlock(dword_602DE0);
    }
}

// 0x4D7820
void sub_4D7820(int64_t loc, tig_art_id_t art_id)
{
    int64_t sector_id;
    Sector* sector;
    int tile;
    int64_t x;
    int64_t y;
    TigRect rect;

    sector_id = sector_id_from_loc(loc);
    if (sector_lock(sector_id, &sector)) {
        tile = tile_id_from_loc(loc);
        sector->tiles.art_ids[tile] = art_id;
        sector->tiles.difmask[tile / 32] |= 1 << (tile & 31);
        sector->tiles.dif = 1;
        sector_unlock(sector_id);

        location_xy(loc, &x, &y);
        if (x > INT_MIN && x < INT_MAX
            && y > INT_MIN && y < INT_MAX) {
            rect.x = (int)x;
            rect.y = (int)y;
            if (tile_view_options.type == VIEW_TYPE_ISOMETRIC) {
                rect.width = 80;
                rect.height = 40;
            } else {
                rect.width = tile_view_options.zoom;
                rect.height = tile_view_options.zoom;
            }
            tile_invalidate_rect(&rect);
        }
    }
}

// 0x4D7980
void sub_4D7980(void)
{
    if (dword_602DE0 != NULL) {
        tig_video_buffer_destroy(dword_602DE0);
        dword_602DE0 = NULL;
    }

    sub_4D7C70();
    sub_4D7A90();

    dword_602E08 = false;
}

// 0x4D79C0
void sub_4D79C0(ViewOptions* view_options)
{
    if (view_options->type == VIEW_TYPE_TOP_DOWN) {
        if (view_options->zoom != tile_view_options.zoom) {
            if (!dword_602E08) {
                sub_4D7A00();
            }

            sub_4D7A40(view_options->zoom);
            sub_4D7AC0(view_options->zoom);
        }
    }
}

// 0x4D7A00
void sub_4D7A00(void)
{
    TigVideoBufferCreateInfo vb_create_info;

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.width = 80;
    vb_create_info.height = 40;
    vb_create_info.background_color = 0;
    tig_video_buffer_create(&vb_create_info, &dword_602DE0);

    dword_602E08 = true;
}

// 0x4D7A40
void sub_4D7A40(int zoom)
{
    TigVideoBufferCreateInfo vb_create_info;
    int index;

    sub_4D7A90();

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.width = zoom;
    vb_create_info.height = zoom;
    vb_create_info.background_color = 0;

    for (index = 0; index < TILE_CACHE_CAPACITY; index++) {
        stru_602AE0[index].art_id = TIG_ART_ID_INVALID;
        tig_video_buffer_create(&vb_create_info, &(stru_602AE0[index].video_buffer));
    }
}

// 0x4D7A90
void sub_4D7A90(void)
{
    int index;

    for (index = 0; index < TILE_CACHE_CAPACITY; index++) {
        if (stru_602AE0[index].video_buffer != NULL) {
            tig_video_buffer_destroy(stru_602AE0[index].video_buffer);
        }

        stru_602AE0[index].video_buffer = NULL;
        stru_602AE0[index].art_id = TIG_ART_ID_INVALID;
    }
}

// 0x4D7AC0
void sub_4D7AC0(int zoom)
{
    double v1;
    double v2;
    double v3;
    double v4;
    double scale;
    double v5;
    double v6;
    double v7;
    double v8;
    int x;
    int y;

    sub_4D7C70();

    v1 = zoom * 0.5 - 1.0;

    dword_602DE8 = (uint8_t*)MALLOC(sizeof(*dword_602DE8) * (zoom * zoom));
    dword_602DE4 = (uint8_t*)MALLOC(sizeof(*dword_602DE4) * (zoom * zoom));

    v2 = ((zoom - 1) - v1) * M_SQRT1_2;
    v3 = 80.0 / (v2 * 4.0);
    v4 = 40.0 / (v2 + v1 * M_SQRT1_2 + v2 + v1 * M_SQRT1_2);

    if (zoom < 8) {
        scale = 0.5;
    } else if (zoom < 16) {
        scale = 0.65;
    } else if (zoom < 24) {
        scale = 0.7;
    } else if (zoom < 32) {
        scale = 0.8;
    } else if (zoom < 40) {
        scale = 0.9;
    } else if (zoom < 48) {
        scale = 0.91;
    } else if (zoom < 56) {
        scale = 0.92;
    } else if (zoom < 60) {
        scale = 0.93;
    } else if (zoom < 62) {
        scale = 0.94;
    } else {
        scale = 0.95;
    }

    v5 = v3 * scale;
    v6 = v4 * scale;

    for (y = 0; y < zoom; y++) {
        v7 = (y - v1) * M_SQRT1_2;
        for (x = 0; x < zoom; x++) {
            v8 = (x - v1) * M_SQRT1_2;
            dword_602DE8[y * zoom + x] = (uint8_t)((v7 + v8) * v5 + 40.0);
            dword_602DE4[y * zoom + x] = (uint8_t)((v7 - v8) * v6 + 20.0);
        }
    }
}

// 0x4D7C70
void sub_4D7C70(void)
{
    if (dword_602DE8 != NULL) {
        FREE(dword_602DE8);
        dword_602DE8 = NULL;
    }

    if (dword_602DE4 != NULL) {
        FREE(dword_602DE4);
        dword_602DE4 = NULL;
    }
}

// 0x4D7E90
TigVideoBuffer* sub_4D7E90(unsigned int art_id)
{
    int candidate = -1;
    int found = -1;
    int index;

    for (index = 0; index < TILE_CACHE_CAPACITY; index++) {
        if (stru_602AE0[index].art_id == -1) {
            found = index;
        } else {
            if (stru_602AE0[index].art_id == art_id) {
                stru_602AE0[index].time = gamelib_ping_time;
                return stru_602AE0[index].video_buffer;
            }

            if (candidate == -1) {
                candidate = index;
            } else {
                if (stru_602AE0[index].time < stru_602AE0[candidate].time) {
                    candidate = index;
                }
            }
        }
    }

    if (found == -1) {
        found = candidate;
    }

    sub_4D7590(art_id, stru_602AE0[found].video_buffer);
    stru_602AE0[found].art_id = art_id;
    stru_602AE0[found].time = gamelib_ping_time;

    return stru_602AE0[found].video_buffer;
}

// 0x4D7CB0
void tile_draw_topdown(GameDrawInfo* draw_info)
{
    SectorListNode* sector_node;
    Sector* sector;
    int tile;
    int skip;
    int x;
    int y;
    int64_t loc_x;
    int64_t loc_y;
    TigRectListNode* rect_node;
    TigRect tile_rect;
    TigRect dst_rect;
    TigRect src_rect;

    sector_node = draw_info->sectors;
    while (sector_node != NULL) {
        if (sector_lock(sector_node->sec, &sector)) {
            tile = tile_id_from_loc(sector_node->loc);
            skip = 64 - sector_node->width;
            location_xy(sector_node->loc, &loc_x, &loc_y);

            for (y = 0; y < sector_node->height; y++) {
                tile_rect.x = (int)loc_x;
                tile_rect.y = (int)loc_y + y * tile_view_options.zoom;
                tile_rect.width = tile_view_options.zoom;
                tile_rect.height = tile_view_options.zoom;

                for (x = 0; x < sector_node->width; x++) {
                    rect_node = *draw_info->rects;
                    while (rect_node != NULL) {
                        if (tig_rect_intersection(&tile_rect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                            src_rect.x = dst_rect.x - tile_rect.x;
                            src_rect.y = dst_rect.y - tile_rect.y;
                            src_rect.width = dst_rect.width;
                            src_rect.height = dst_rect.height;
                            tig_window_copy_from_vbuffer(tile_iso_window_handle,
                                &dst_rect,
                                sub_4D7E90(sector->tiles.art_ids[tile]),
                                &src_rect);
                        }
                        rect_node = rect_node->next;
                    }

                    tile++;
                    tile_rect.x -= tile_view_options.zoom;
                }

                tile += skip;
            }
            sector_unlock(sector_node->sec);
        }
        sector_node = sector_node->next;
    }
}

// CE: batched scan of facade tiles' rendered pixels. Facades that blit pure black
// are the unfinished/black-art off-camera scenery (real, renderable art that just
// draws black) — undetectable from art or lighting, only from the final pixel. We
// record candidates during the draw and read them back once per frame (one VB lock)
// to mark them void, so the existing void-edge fade feathers from them.
typedef struct {
    int64_t sec_id;
    int index;
    int px;
    int py;
    int ff_center; // center fade factor applied this frame (255 = unfaded)
} GapScanEntry;
static GapScanEntry gap_scan_list[16384];
static int gap_scan_count;

// CE: facade black-detection scan. Reads back the facade tiles queued during
// the draw (one VB lock) and marks the pure-black ones void so the void-edge
// fade feathers from them. Reads dword_602DF0, so it must run after the tiles
// are on the CPU surface: in software mode at the end of tile_draw_iso; in GPU
// mode in tile_gpu_world_end, after the readback.
static void tile_void_edge_scan(void)
{
    // Skip while the zoom is animating: the world VB has stale/empty regions
    // mid-zoom (rendered across frames), so reading it then marks tiles black
    // at positions only momentarily black. The marks are persistent, so
    // detecting once the zoom settles is enough.
    if (void_edge_fade_enabled() && gap_scan_count > 0 && !iso_zoom_is_animating()) {
        TigVideoBufferData vbd;
        if (tig_video_buffer_lock(dword_602DF0) == TIG_OK) {
            if (tig_video_buffer_data(dword_602DF0, &vbd) == TIG_OK && vbd.pixels != NULL) {
                int i;
                // Probe offsets within the 80x40 tile diamond around its center.
                // A tile "renders black" only if EVERY probe is black: true
                // black.ART chunks pass; good cliff art with a dark crevice at
                // one probe fails (single-pixel sampling sprayed false blotches).
                static const int probe_dx[5] = { 0, -14, 14, 0, 0 };
                static const int probe_dy[5] = { 0, 0, 0, -7, 7 };
                for (i = 0; i < gap_scan_count; i++) {
                    int px = gap_scan_list[i].px;
                    int py = gap_scan_list[i].py;
                    int ffc = gap_scan_list[i].ff_center;
                    // Divide the fade back out to recover the pre-fade pixel, so a
                    // cliff the fade merely dimmed isn't mistaken for black art.
                    if (px >= 0 && py >= 0 && px < vbd.width && py < vbd.height && ffc >= 16) {
                        int probes = 0;
                        int blacks = 0;
                        int k;
                        for (k = 0; k < 5; k++) {
                            int qx = px + probe_dx[k];
                            int qy = py + probe_dy[k];
                            if (qx >= 0 && qy >= 0 && qx < vbd.width && qy < vbd.height) {
                                uint32_t pix = ((uint32_t*)((uint8_t*)vbd.pixels + (size_t)qy * (size_t)vbd.pitch))[qx];
                                int pr = tig_color_get_red(pix) * 255 / ffc;
                                int pg = tig_color_get_green(pix) * 255 / ffc;
                                int pb = tig_color_get_blue(pix) * 255 / ffc;
                                probes++;
                                if (pr < 16 && pg < 16 && pb < 16) {
                                    blacks++;
                                }
                            }
                        }
                        if (probes >= 3 && blacks == probes) {
                            void_edge_fade_note_dark(gap_scan_list[i].sec_id, gap_scan_list[i].index);
                        }
                    }
                }
            }
            tig_video_buffer_unlock(dword_602DF0);
        }
        gap_scan_count = 0;
    }
}

// CE (feature/perf-gpu-accel): GPU world-pass bracketing, driven by
// gamelib_draw_game. begin runs before the first world pass (tile); end runs
// after the last GPU world pass (object/roof). Between them, tile/object/roof
// blits route to the GPU world target via tile_gpu_dispatch. No-op (software)
// unless `tile render path=gpu` is selected and begin_pass succeeds. Other
// tile_draw callers (zoom render / thumbnails) never call begin, so they stay
// on the software path (tile_gpu_active is false there).
void tile_gpu_world_begin(void)
{
    // CE (step 6): mark/unmark the iso window as the GPU-world window so the
    // compositor paints it transparent in present mode (only re-invalidates on a
    // state change, so this is cheap to call every frame).
    bool present = tile_gpu_present_path();
    if (tile_iso_window_handle != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_set_gpu_world(tile_iso_window_handle, present);
    }
    // CE (full GPU/UI): when gpu-ui is active, the flip composites UI windows on
    // the GPU (per-window mirror textures, z-ordered) instead of uploading +
    // drawing the CPU framebuffer.
    tile_gpu_ui_active = tile_gpu_ui_path();
    tig_video_set_gpu_ui(tile_gpu_ui_active);
    // Outside present mode, clear any persisted world underlay so the flip
    // presents the framebuffer normally (opaque). In present mode it's (re)set in
    // tile_gpu_world_end and persists across UI-only flips.
    if (!present) {
        tig_video_set_world_underlay(NULL, NULL, NULL, false);
        tig_video_set_roof_underlay(NULL, NULL, NULL, false);
    }

    tile_gpu_active = tile_should_use_gpu_path();
    if (tile_gpu_active) {
        if (!tile_gpu_begin_pass(dword_602DF0)) {
            tile_gpu_active = false;
        } else {
            tile_deferred_blit_count = 0;
            tile_oneshot_tex_count = 0;
            if (tile_gpu_trace_state == 1) {
                tile_gpu_trace_state = 2;
                tile_gpu_trace_order = 0;
                tig_debug_printf("=== GPU dispatch trace begin (single frame) ===\n");
            }
        }
    }
}

void tile_gpu_world_end(void)
{
    if (!tile_gpu_active) {
        return;
    }

    // CE (step 6): present-path -- keep the world on the GPU and register it as
    // the flip-time underlay instead of reading it back. Unbind the render target
    // so the UI/present draw to the screen. The compositor paints the iso window
    // transparent (gpu_world flag), so the framebuffer reveals this world texture
    // under the UI. (Roofs draw software after this into the now-unused CPU
    // surface, so they're not yet visible -- a later increment moves them to a
    // present-time layer.)
    if (tile_gpu_present_path()) {
        SDL_Renderer* renderer = NULL;
        if (tig_video_renderer_get(&renderer) == TIG_OK && renderer != NULL) {
            SDL_SetRenderTarget(renderer, NULL); // flushes the world-pass batch
        }
        SDL_Texture* world_tex = tig_video_buffer_get_sdl_texture(tile_gpu_world_buffer);
        TigRect iso_rect = { 0, 0, tile_gpu_world_buffer_w, tile_gpu_world_buffer_h };
        tig_video_set_world_underlay(world_tex, &iso_rect, NULL, false);

        if (tile_oneshot_tex_count > 0) {
            int ti;
            for (ti = 0; ti < tile_oneshot_tex_count; ti++) {
                SDL_DestroyTexture(tile_oneshot_textures[ti]);
            }
            tile_oneshot_tex_count = 0;
        }
        tile_deferred_blit_count = 0; // deferred replay needs the CPU surface; skip
        tile_gpu_active = false;
        return;
    }

    // Read the GPU world target back to the CPU surface, then replay any
    // deferred (unsupported / cache-miss) blits onto it -- tile_gpu_active is
    // now false so they take the software path and aren't re-deferred -- then
    // run the facade scan against the final pixels.
    tile_gpu_end_pass(dword_602DF0);
    tile_gpu_active = false;

    // The readback above flushed the renderer, so the one-shot OVERRIDE
    // textures drawn this frame are no longer referenced by pending draws --
    // safe to free now.
    if (tile_oneshot_tex_count > 0) {
        int ti;
        for (ti = 0; ti < tile_oneshot_tex_count; ti++) {
            SDL_DestroyTexture(tile_oneshot_textures[ti]);
        }
        tile_oneshot_tex_count = 0;
    }

    if (tile_deferred_blit_count > 0) {
        int di;
        if (tile_gpu_trace_state == 2) {
            tig_debug_printf("--- replay (%d deferred, drawn LAST on top of everything) ---\n",
                tile_deferred_blit_count);
        }
        for (di = 0; di < tile_deferred_blit_count; di++) {
            if (tile_gpu_trace_state == 2) {
                tile_gpu_trace_log("REPLAY", &tile_deferred_blits[di].info);
            }
            tig_art_blit(&tile_deferred_blits[di].info);
        }
        tile_deferred_blit_count = 0;
    }

    if (tile_gpu_trace_state == 2) {
        if (tile_gpu_trace_order == 0) {
            // Non-rendering frame (tile_draw early-exited on !tile_visible, or
            // a transition with no dispatches). Re-arm so the next frame with
            // actual content captures, instead of consuming the user's marker.
            tig_debug_printf("=== GPU dispatch trace end (0 entries -- re-arming for next world pass) ===\n");
            tile_gpu_trace_state = 1;
        } else {
            tig_debug_printf("=== GPU dispatch trace end (%d entries) ===\n", tile_gpu_trace_order);
            tile_gpu_trace_state = 0;
        }
    }

    tile_void_edge_scan();
}

// CE (zoom->GPU): open the zoomed-world GPU pass (gpu-ui only). Binds the 2x zoom
// target; the world (and roofs, kept open across roof_draw) blit onto it via the
// dispatch -- the zoom math already produces world-VB-space dst rects, so the
// dispatch needs no changes. Phase A: does NOT clear the target (previous-frame
// pixels stay valid until a camera move / scroll forces a full invalidate -- same
// incremental model as the CPU 2x VB). Returns false -> caller falls back to
// tile_gpu_world_begin (which is a no-op during zoom, i.e. CPU zoom).
bool tile_gpu_zoom_begin(void)
{
    if (!tile_gpu_zoom_is_enabled()) {
        return false;
    }
    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return false;
    }
    SDL_Texture* tex = tig_video_buffer_get_sdl_texture(tile_gpu_zoom_buffer);
    if (tex == NULL) {
        return false;
    }
    if (!SDL_SetRenderTarget(renderer, tex)) {
        return false;
    }
    tile_gpu_target_buffer = tile_gpu_zoom_buffer;
    tile_gpu_active = true;
    tile_deferred_blit_count = 0;
    tile_oneshot_tex_count = 0;
    // Keep the iso window the GPU-world window so the gpu-ui walk draws the world
    // underlay at its z-slot (not the now-unwritten iso CPU VB mirror), and keep
    // gpu-ui active so the walk composites. (tile_gpu_world_begin -- which normally
    // sets these -- is bypassed during the GPU zoom pass.)
    tile_gpu_ui_active = true;
    tig_video_set_gpu_ui(true);
    if (tile_iso_window_handle != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_set_gpu_world(tile_iso_window_handle, true);
    }
    return true;
}

// CE (zoom roof layer): after the zoom WORLD pass (tile_gpu_zoom_begin + tile/object),
// switch the render target to the cleared 2x zoom roof buffer so roof_draw paints the
// roofs there (full re-render) instead of into the incremental world buffer.
// tile_gpu_zoom_end then composites it over the downscaled world. Returns false ->
// caller bakes roofs into the world buffer (old behavior, fade roof can accumulate).
bool tile_gpu_zoom_roof_begin(void)
{
    if (!tile_gpu_active || tile_gpu_zoom_buffer == NULL) {
        return false; // the zoom world pass must be open
    }
    if (!tile_gpu_ensure_zoom_roof_buffer(tile_gpu_zoom_buffer_w, tile_gpu_zoom_buffer_h)) {
        return false;
    }
    SDL_Texture* roof_tex = tig_video_buffer_get_sdl_texture(tile_gpu_zoom_roof_buffer);
    SDL_Renderer* renderer = NULL;
    if (roof_tex == NULL || tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return false;
    }
    if (!SDL_SetRenderTarget(renderer, roof_tex)) {
        return false;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer); // transparent -- only the roofs paint over it
    tile_gpu_target_buffer = tile_gpu_zoom_roof_buffer; // dispatch now draws roofs here
    tile_gpu_zoom_roof_active = true;
    return true;
}

// CE (zoom->GPU): close the zoom pass. Unbinds the render target and registers the
// zoom world buffer as the world underlay + the separate zoom roof buffer as the roof
// underlay: both use the same centered crop (src) bilinear-downscaled (linear) to the
// iso rect (dst), so the roof composites over the world at flip (the roof never lives
// in the incremental world buffer -> no fade-roof accumulation).
void tile_gpu_zoom_end(const TigRect* dst_rect, const TigRect* src_rect, bool linear)
{
    if (!tile_gpu_active) {
        return;
    }
    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) == TIG_OK && renderer != NULL) {
        uint64_t flush_t0 = tile_gpu_now_ns();
        SDL_SetRenderTarget(renderer, NULL); // flush the zoom-pass batch + unbind
        double flush_ms = (double)(tile_gpu_now_ns() - flush_t0) / 1e6;
        if (flush_ms > 25.0) {
            tig_debug_printf("[zoom-end] flush/resolve %.1fms\n", flush_ms);
        }
    }
    SDL_Texture* tex = tig_video_buffer_get_sdl_texture(tile_gpu_zoom_buffer);
    tig_video_set_world_underlay(tex, dst_rect, src_rect, linear);
    // CE (zoom roof layer): composite the SEPARATE zoom roof buffer over the world with
    // the SAME centered crop + downscale, so the alpha fade roof blends over the world
    // at flip (premultiplied) -- never inside the incremental world buffer, so it can't
    // accumulate (no streaks), with no PC-under-roof heuristic. If the roof pass didn't
    // open, roofs baked into the world buffer (fallback) so clear the roof underlay.
    if (tile_gpu_zoom_roof_active && tile_gpu_zoom_roof_buffer != NULL) {
        SDL_Texture* roof_tex = tig_video_buffer_get_sdl_texture(tile_gpu_zoom_roof_buffer);
        tig_video_set_roof_underlay(roof_tex, dst_rect, src_rect, linear);
    } else {
        tig_video_set_roof_underlay(NULL, NULL, NULL, false);
    }
    tile_gpu_zoom_roof_active = false;
    if (tile_oneshot_tex_count > 0) {
        int ti;
        for (ti = 0; ti < tile_oneshot_tex_count; ti++) {
            SDL_DestroyTexture(tile_oneshot_textures[ti]);
        }
        tile_oneshot_tex_count = 0;
    }
    tile_deferred_blit_count = 0; // deferred replay needs a CPU surface; skip (as present)
    tile_gpu_active = false;
    tile_gpu_target_buffer = tile_gpu_world_buffer;
}

// CE (step 6): open the roof present-layer pass. gpu-present only -- binds a
// freshly-cleared (transparent) roof texture and routes roof_draw's blits onto it
// via the dispatch. Caller renders ALL visible roofs (not just dirty rects) so the
// cleared texture is fully repainted, then calls tile_gpu_world_roof_end. Returns
// true if the pass opened (roofs should be drawn through tile_gpu_dispatch).
bool tile_gpu_world_roof_begin(void)
{
    if (!tile_gpu_present_path() || tile_gpu_path_disabled) {
        return false;
    }
    if (!tile_gpu_ensure_roof_buffer()) {
        return false;
    }
    SDL_Texture* roof_tex = tig_video_buffer_get_sdl_texture(tile_gpu_roof_buffer);
    SDL_Renderer* renderer = NULL;
    if (roof_tex == NULL || tig_video_renderer_get(&renderer) != TIG_OK || renderer == NULL) {
        return false;
    }
    if (!SDL_SetRenderTarget(renderer, roof_tex)) {
        return false;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer); // transparent -- only the roofs paint over it
    tile_gpu_target_buffer = tile_gpu_roof_buffer;
    tile_oneshot_tex_count = 0;
    tile_deferred_blit_count = 0;
    tile_gpu_active = true;
    tile_gpu_roof_pass_active = true;
    return true;
}

// CE (step 6): close the roof pass -- unbind and register the roof texture as the
// flip-time roof layer (composited between the world and the UI).
void tile_gpu_world_roof_end(void)
{
    if (!tile_gpu_roof_pass_active) {
        return;
    }
    SDL_Renderer* renderer = NULL;
    if (tig_video_renderer_get(&renderer) == TIG_OK && renderer != NULL) {
        SDL_SetRenderTarget(renderer, NULL); // flush the roof-pass batch
    }
    SDL_Texture* roof_tex = tig_video_buffer_get_sdl_texture(tile_gpu_roof_buffer);
    TigRect iso_rect = { 0, 0, tile_gpu_roof_buffer_w, tile_gpu_roof_buffer_h };
    tig_video_set_roof_underlay(roof_tex, &iso_rect, NULL, false); // 1.0: whole texture, NEAREST

    if (tile_oneshot_tex_count > 0) {
        int ti;
        for (ti = 0; ti < tile_oneshot_tex_count; ti++) {
            SDL_DestroyTexture(tile_oneshot_textures[ti]);
        }
        tile_oneshot_tex_count = 0;
    }
    tile_deferred_blit_count = 0;
    tile_gpu_target_buffer = tile_gpu_world_buffer;
    tile_gpu_active = false;
    tile_gpu_roof_pass_active = false;
}

// CE: true when the GPU world path is selected. object_draw consults this to
// emit COLOR_CONST lighting (the hardware path) so lit objects are tinted on
// the GPU instead of via a working-palette swap the GPU cache can't follow.
bool tile_gpu_world_lighting(void)
{
    return tile_should_use_gpu_path();
}

// CE (feature/perf-gpu-accel): dump the current iso world buffer (dword_602DF0,
// the CPU surface the world is rendered/read-back into) to an absolute BMP path.
// Used by the self-test harness to capture and compare gpu vs software renders.
void tile_gpu_test_capture(const char* abs_path)
{
    if (dword_602DF0 == NULL || abs_path == NULL) {
        return;
    }
    tig_video_buffer_debug_save_bmp(dword_602DF0, abs_path);
}

// NOTE: In the original code this function is a part of `tile_draw`, however
// if `tile_draw_topdown` is definitely there, why `tile_draw_iso` should not?
// CE (gated experiment): half-res-during-lerp. While the zoom is actively
// animating, skip the blit of every other vertical tile row -- the prior frame's
// pixels stay in the persistent world buffer and the bilinear downscale blurs over
// the gaps. Halves the per-tile fill work during the lerp; blurrier mid-zoom, crisp
// when settled. ARCANUM_OPT_HALFRES_LERP=1.
static int halfres_lerp_override = -1;
static bool halfres_lerp_enabled(void)
{
    static int cached = -1;
    if (halfres_lerp_override >= 0) return halfres_lerp_override != 0;
    if (cached < 0) {
        const char* e = getenv("ARCANUM_OPT_HALFRES_LERP");
        cached = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

void tile_halfres_lerp_set(int on)
{
    halfres_lerp_override = on;
}

// CE: 2-thread split of the sector-row loop (default ON; cfg "tile threads",
// gpucmd "tilethreads", env ARCANUM_OPT_TILE_THREADS). Splits big full-redraws
// (zoom-out / camera-move) across 2 threads: worst-frame render ~-42% there,
// neutral on light frames, byte-identical output. Hazards handled: sector_lock's
// global in_sector_lock guard + shared sector cache serialized via g_tile_sector_mutex;
// the gap_scan global-counter append skipped while threaded (g_tile_threads_active);
// the shared art cache hardened in art.c (thread-local LRU dword_604714 + a
// slow-path-only resolve mutex, armed via tig_art_resolve_lock_set around the
// dispatch). sectors[] is per-call, so thread-local.
static void tile_draw_rows(GameDrawInfo*, SectorRect*, int, int, const TigRect*, bool, tig_color_t, tig_color_t, bool);
static int tile_threads_override = -1; // cfg "tile threads" / gpucmd "tilethreads"; -1 = unset
static bool tile_threads_enabled(void)
{
    // Precedence: ARCANUM_OPT_TILE_THREADS env (explicit, for the A/B harness) >
    // cfg/gpucmd override > default ON.
    static int env = -2;
    if (env == -2) {
        const char* e = getenv("ARCANUM_OPT_TILE_THREADS");
        env = (e != NULL) ? (e[0] == '1' ? 1 : 0) : -1;
    }
    if (env >= 0) return env != 0;
    if (tile_threads_override >= 0) return tile_threads_override != 0;
    return true; // CE: default ON -- hardened (thread-local LRU + slow-path-only art mutex)
}
void tile_threads_set(int on)
{
    tile_threads_override = on;
}
static SDL_Mutex* g_tile_sector_mutex = NULL;
static volatile int g_tile_threads_active = 0;
typedef struct {
    GameDrawInfo* draw_info; SectorRect* v1; int row_start; int row_end;
    const TigRect* dirty_union; bool dirty_union_set;
    tig_color_t indoor_color; tig_color_t outdoor_color; bool halfres_lerp;
} TileRowsArg;
static int tile_rows_thread_fn(void* p)
{
    TileRowsArg* a = (TileRowsArg*)p;
    tile_draw_rows(a->draw_info, a->v1, a->row_start, a->row_end, a->dirty_union,
        a->dirty_union_set, a->indoor_color, a->outdoor_color, a->halfres_lerp);
    return 0;
}

// CE (EXPERIMENTAL, gated): the body of tile_draw_iso's outer sector-row loop,
// extracted verbatim into its own function so a row range [row_start, row_end)
// can be drawn independently. Single-threaded this is byte-identical to the
// original inline loop. When ARCANUM_OPT_TILE_THREADS=1 it is the per-thread
// work unit (two threads split the rows). ALL formerly-per-iteration locals are
// declared here so two invocations never share mutable scratch.
//
// HAZARD NOTE: this function calls sector_lock/sector_unlock, which are NOT
// thread-safe (global `in_sector_lock` re-entry guard + shared cache mutation).
// The threaded caller MUST pre-lock all sectors serially and not let two of
// these run sector_lock concurrently. See tile_draw_iso's threaded path.
static void tile_draw_rows(GameDrawInfo* draw_info,
    SectorRect* v1,
    int row_start,
    int row_end,
    const TigRect* dirty_union,
    bool dirty_union_set,
    tig_color_t indoor_color,
    tig_color_t outdoor_color,
    bool halfres_lerp)
{
    SectorRectRow* v3;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigRect tile_rect;
    TigRect tile_subrect;
    int v2;
    int v4;
    int indexes[SECTOR_RECT_DIM];
    int widths[SECTOR_RECT_DIM];
    bool sector_lock_results[SECTOR_RECT_DIM];
    Sector* sectors[SECTOR_RECT_DIM];
    int64_t loc_x;
    int64_t loc_y;
    TigRectListNode* rect_node;
    int tile_type;
    tig_color_t color;
    tig_color_t v36[4];
    tig_color_t v51[9];
    int v10;
    int v11;
    int center_x;
    int center_y;
    int v15;
    int v42;
    bool blit_info_initialized;
    int v38;

    art_blit_info.flags = 0;
    art_blit_info.src_rect = &src_rect;
    art_blit_info.dst_rect = &dst_rect;

    tile_rect.width = 78;
    tile_rect.height = 40;

    for (v2 = row_start; v2 < row_end; v2++) {
        v3 = &(v1->rows[v2]);

        for (v4 = 0; v4 < v3->num_cols; v4++) {
            indexes[v4] = v3->tile_ids[v4];
            widths[v4] = 64 - v3->num_hor_tiles[v4];
            if (g_tile_threads_active) SDL_LockMutex(g_tile_sector_mutex);
            sector_lock_results[v4] = sector_lock(v3->sector_ids[v4], &(sectors[v4]));
            if (g_tile_threads_active) SDL_UnlockMutex(g_tile_sector_mutex);
        }

        location_xy(v3->origin_locs[0], &loc_x, &loc_y);

        v10 = 0;
        v11 = 0;

        for (v38 = 0; v38 < v3->num_vert_tiles; v38++) {
            center_x = (int)loc_x + v10;
            center_y = (int)loc_y + v11;

            for (v15 = 0; v15 < v3->num_cols; v15++) {
                if (sector_lock_results[v15]) {
                    for (v42 = 0; v42 < v3->num_hor_tiles[v15]; v42++) {
                        if (halfres_lerp && (v38 & 1)) {
                            indexes[v15]++;
                            center_x -= 40;
                            center_y += 20;
                            continue;
                        }
                        blit_info_initialized = false;
                        int tile_fade_center = 255;
                        int tile_pre_sum = -1;
                        bool tile_in_dirty = dirty_union_set
                            && (center_x + 1) < dirty_union->x + dirty_union->width
                            && (center_x + 1 + tile_rect.width) > dirty_union->x
                            && center_y < dirty_union->y + dirty_union->height
                            && (center_y + tile_rect.height) > dirty_union->y;
                        if (tile_in_dirty && !roof_is_covered_xy(center_x + 40, center_y + 20, false)) {
                            art_blit_info.art_id = sectors[v15]->tiles.art_ids[indexes[v15]];

                            tile_type = tig_art_tile_id_type_get(art_blit_info.art_id);
                            tile_rect.x = center_x + 1;
                            tile_rect.y = center_y;

                            rect_node = *draw_info->rects;
                            while (rect_node != NULL) {
                                if (tig_rect_intersection(&tile_rect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                    src_rect.x = dst_rect.x - tile_rect.x;
                                    src_rect.y = dst_rect.y - tile_rect.y;
                                    src_rect.width = dst_rect.width;
                                    src_rect.height = dst_rect.height;

                                    if (!blit_info_initialized) {
                                        blit_info_initialized = true;

                                        art_blit_info.dst_video_buffer = dword_602DF0;
                                        art_blit_info.field_14 = v36;

                                        color = !tile_type ? indoor_color : outdoor_color;

                                        if (sub_4DA360(center_x, center_y, color, v51)) {
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_LERP;
                                            if (!tile_hardware_accelerated) {
                                                art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                            }
                                        } else if (v51[0] != color || tile_hardware_accelerated) {
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_CONST;
                                            art_blit_info.color = v51[0];
                                            if (!tile_hardware_accelerated) {
                                                art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                            }
                                        } else if (tile_gpu_active) {
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_CONST | TIG_ART_BLT_PALETTE_ORIGINAL;
                                            art_blit_info.color = v51[0];
                                        } else {
                                            art_blit_info.flags = 0;
                                        }

                                        if (tig_art_type(art_blit_info.art_id) != TIG_ART_TYPE_TILE
                                            || tile_type == TIG_ART_TILE_TYPE_OUTDOOR) {
                                            unsigned char ff[9];
                                            tile_pre_sum = (int)tig_color_get_red(v51[4])
                                                + (int)tig_color_get_green(v51[4])
                                                + (int)tig_color_get_blue(v51[4]);
                                            if (void_edge_fade_fade_factors(v3->sector_ids[v15], sectors[v15], indexes[v15], ff)) {
                                                v51[0] = tig_color_mul(v51[0], tig_color_make(ff[0], ff[0], ff[0]));
                                                v51[1] = tig_color_mul(v51[1], tig_color_make(ff[1], ff[1], ff[1]));
                                                v51[2] = tig_color_mul(v51[2], tig_color_make(ff[2], ff[2], ff[2]));
                                                v51[3] = tig_color_mul(v51[3], tig_color_make(ff[3], ff[3], ff[3]));
                                                v51[4] = tig_color_mul(v51[4], tig_color_make(ff[4], ff[4], ff[4]));
                                                v51[5] = tig_color_mul(v51[5], tig_color_make(ff[5], ff[5], ff[5]));
                                                v51[6] = tig_color_mul(v51[6], tig_color_make(ff[6], ff[6], ff[6]));
                                                v51[7] = tig_color_mul(v51[7], tig_color_make(ff[7], ff[7], ff[7]));
                                                v51[8] = tig_color_mul(v51[8], tig_color_make(ff[8], ff[8], ff[8]));
                                                art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_LERP;
                                                if (!tile_hardware_accelerated) {
                                                    art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                                }
                                                tile_fade_center = ff[4];
                                            }
                                        }
                                    }

                                    if ((art_blit_info.flags & TIG_ART_BLT_BLEND_COLOR_LERP) != 0) {
                                        art_blit_info.field_18 = &tile_subrect;

                                        tile_subrect.x = tile_rect.x;
                                        tile_subrect.y = tile_rect.y;
                                        tile_subrect.width = 39;
                                        tile_subrect.height = 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[0];
                                            v36[1] = v51[1];
                                            v36[2] = v51[4];
                                            v36[3] = v51[3];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x + 39;
                                        tile_subrect.y = tile_rect.y;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[1];
                                            v36[1] = v51[2];
                                            v36[2] = v51[5];
                                            v36[3] = v51[4];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x;
                                        tile_subrect.y = tile_rect.y + 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[3];
                                            v36[1] = v51[4];
                                            v36[2] = v51[7];
                                            v36[3] = v51[6];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x + 39;
                                        tile_subrect.y = tile_rect.y + 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[4];
                                            v36[1] = v51[5];
                                            v36[2] = v51[8];
                                            v36[3] = v51[7];

                                            tile_blit_dispatch(&art_blit_info);
                                        }
                                    } else {
                                        tile_blit_dispatch(&art_blit_info);
                                    }
                                }
                                rect_node = rect_node->next;
                            }

                            if (void_edge_fade_enabled()
                                && blit_info_initialized
                                && tile_pre_sum >= 300
                                && tig_art_type(art_blit_info.art_id) != TIG_ART_TYPE_TILE
                                && !void_edge_fade_dark_marked(v3->sector_ids[v15], indexes[v15])
                                && gap_scan_count < (int)(sizeof(gap_scan_list) / sizeof(gap_scan_list[0]))
                                && !g_tile_threads_active) {
                                gap_scan_list[gap_scan_count].sec_id = v3->sector_ids[v15];
                                gap_scan_list[gap_scan_count].index = indexes[v15];
                                gap_scan_list[gap_scan_count].px = center_x + 40;
                                gap_scan_list[gap_scan_count].py = center_y + 20;
                                gap_scan_list[gap_scan_count].ff_center = tile_fade_center;
                                gap_scan_count++;
                            }
                        }

                        indexes[v15]++;
                        center_x -= 40;
                        center_y += 20;
                    }

                    indexes[v15] += widths[v15];
                } else {
                    center_x -= 40 * v3->num_hor_tiles[v15];
                    center_y += 20 * v3->num_hor_tiles[v15];
                }
            }

            v10 += 40;
            v11 += 20;
        }

        for (v4 = 0; v4 < v3->num_cols; v4++) {
            if (sector_lock_results[v4]) {
                if (g_tile_threads_active) SDL_LockMutex(g_tile_sector_mutex);
                sector_unlock(v3->sector_ids[v4]);
                if (g_tile_threads_active) SDL_UnlockMutex(g_tile_sector_mutex);
            }
        }
    }
}

void tile_draw_iso(GameDrawInfo* draw_info)
{
    SectorRect* v1;
    SectorRectRow* v3;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigRect tile_rect;
    TigRect tile_subrect;
    tig_color_t indoor_color;
    tig_color_t outdoor_color;
    int v2;
    int v4;
    int indexes[SECTOR_RECT_DIM];
    int widths[SECTOR_RECT_DIM];
    bool sector_lock_results[SECTOR_RECT_DIM];
    Sector* sectors[SECTOR_RECT_DIM];
    int64_t loc_x;
    int64_t loc_y;
    TigRectListNode* rect_node;
    int tile_type;
    tig_color_t color;
    tig_color_t v36[4];
    tig_color_t v51[9];
    int v10;
    int v11;
    int center_x;
    int center_y;
    int v15;
    int v42;
    bool blit_info_initialized;
    int v38;

    v1 = draw_info->sector_rect;

    art_blit_info.flags = 0; // NOTE: Initialize to silence compiler warning.
    art_blit_info.src_rect = &src_rect;
    art_blit_info.dst_rect = &dst_rect;

    tile_rect.width = 78;
    tile_rect.height = 40;

    indoor_color = light_get_indoor_color();
    outdoor_color = light_get_outdoor_color();

    // Apply any "renders black" marks collected last frame (turns the black off-area
    // facades into void so the fade feathers from them) before this frame draws.
    if (void_edge_fade_enabled()) {
        void_edge_fade_flush_dark();
    }
    gap_scan_count = 0;

    light_buffers_lock();

    // CE (feature/perf-gpu-accel): the GPU world pass is opened by gamelib via
    // tile_gpu_world_begin before tile_draw and closed by tile_gpu_world_end
    // after the last GPU world pass (so object/roof can draw on the same
    // target). tile_blit_dispatch just reads `tile_gpu_active` here.

    // Pre-compute the bounding rect of all dirty rects so we can fast-
    // reject tiles whose tile_rect can't possibly overlap any of them.
    // tile_draw iterates the whole sector_rect (visible area + 256px
    // border on every side); on heavy frames with full-screen dirty
    // areas this still wastes ~25-30% of the per-tile work (roof check
    // + rect intersect loop) on the border tiles. The earlier perf log
    // identified this pass as the dominant cost during scroll-at-zoom-
    // out, where tile_max hits 7-10ms — half the 8.3ms ProMotion
    // budget on its own.
    TigRect tile_draw_dirty_union;
    bool tile_draw_dirty_union_set = false;
    {
        TigRectListNode* union_node = *draw_info->rects;
        while (union_node != NULL) {
            if (!tile_draw_dirty_union_set) {
                tile_draw_dirty_union = union_node->rect;
                tile_draw_dirty_union_set = true;
            } else {
                int x1 = tile_draw_dirty_union.x < union_node->rect.x
                    ? tile_draw_dirty_union.x : union_node->rect.x;
                int y1 = tile_draw_dirty_union.y < union_node->rect.y
                    ? tile_draw_dirty_union.y : union_node->rect.y;
                int x2a = tile_draw_dirty_union.x + tile_draw_dirty_union.width;
                int y2a = tile_draw_dirty_union.y + tile_draw_dirty_union.height;
                int x2b = union_node->rect.x + union_node->rect.width;
                int y2b = union_node->rect.y + union_node->rect.height;
                int x2 = x2a > x2b ? x2a : x2b;
                int y2 = y2a > y2b ? y2a : y2b;
                tile_draw_dirty_union.x = x1;
                tile_draw_dirty_union.y = y1;
                tile_draw_dirty_union.width = x2 - x1;
                tile_draw_dirty_union.height = y2 - y1;
            }
            union_node = union_node->next;
        }
    }

    const bool halfres_lerp = halfres_lerp_enabled() && iso_zoom_is_animating();

    // CE (EXPERIMENTAL, gated): the outer sector-row loop body now lives in
    // tile_draw_rows. Single-threaded this draws all rows exactly as the
    // original inline loop did. The original loop below is preserved but
    // bypassed (condition v2 < 0 never runs) to keep the byte-for-byte body
    // available for diffing; the compiler drops it as dead code.
    if (tile_threads_enabled() && v1->num_rows >= 2) {
        if (g_tile_sector_mutex == NULL) g_tile_sector_mutex = SDL_CreateMutex();
        int mid = v1->num_rows / 2;
        TileRowsArg a0 = { draw_info, v1, 0, mid, &tile_draw_dirty_union,
            tile_draw_dirty_union_set, indoor_color, outdoor_color, halfres_lerp };
        TileRowsArg a1 = { draw_info, v1, mid, v1->num_rows, &tile_draw_dirty_union,
            tile_draw_dirty_union_set, indoor_color, outdoor_color, halfres_lerp };
        g_tile_threads_active = 1;
        tig_art_resolve_lock_set(1); // serialize the shared art-cache resolve across the 2 threads
        SDL_Thread* th = SDL_CreateThread(tile_rows_thread_fn, "tile_rows", &a0);
        tile_rows_thread_fn(&a1); // run the second half on this thread
        SDL_WaitThread(th, NULL);
        tig_art_resolve_lock_set(0);
        g_tile_threads_active = 0;
    } else {
        tile_draw_rows(draw_info, v1, 0, v1->num_rows,
            &tile_draw_dirty_union, tile_draw_dirty_union_set,
            indoor_color, outdoor_color, halfres_lerp);
    }

    for (v2 = 0; v2 < 0; v2++) {
        v3 = &(v1->rows[v2]);

        for (v4 = 0; v4 < v3->num_cols; v4++) {
            indexes[v4] = v3->tile_ids[v4];
            widths[v4] = 64 - v3->num_hor_tiles[v4];
            if (g_tile_threads_active) SDL_LockMutex(g_tile_sector_mutex);
            sector_lock_results[v4] = sector_lock(v3->sector_ids[v4], &(sectors[v4]));
            if (g_tile_threads_active) SDL_UnlockMutex(g_tile_sector_mutex);
        }

        location_xy(v3->origin_locs[0], &loc_x, &loc_y);

        v10 = 0;
        v11 = 0;

        for (v38 = 0; v38 < v3->num_vert_tiles; v38++) {
            center_x = (int)loc_x + v10;
            center_y = (int)loc_y + v11;

            for (v15 = 0; v15 < v3->num_cols; v15++) {
                if (sector_lock_results[v15]) {
                    for (v42 = 0; v42 < v3->num_hor_tiles[v15]; v42++) {
                        // CE half-res-during-lerp: on odd vertical rows while zooming,
                        // skip the blit work but still advance the tile index + iso
                        // step so the grid stays aligned (the prior pixels remain for
                        // the downscale to blur).
                        if (halfres_lerp && (v38 & 1)) {
                            indexes[v15]++;
                            center_x -= 40;
                            center_y += 20;
                            continue;
                        }
                        blit_info_initialized = false;
                        int tile_fade_center = 255;
                        int tile_pre_sum = -1;
                        // Fast-reject tiles outside the dirty-rect union
                        // before paying for roof_is_covered_xy (which does
                        // sector lookups). Tile rect math matches what
                        // the slow path computes below. Also defer
                        // sectors[].tiles.art_ids[] dereference and
                        // tig_art_tile_id_type_get() into the slow path —
                        // both are needed only when we actually draw.
                        bool tile_in_dirty = tile_draw_dirty_union_set
                            && (center_x + 1) < tile_draw_dirty_union.x + tile_draw_dirty_union.width
                            && (center_x + 1 + tile_rect.width) > tile_draw_dirty_union.x
                            && center_y < tile_draw_dirty_union.y + tile_draw_dirty_union.height
                            && (center_y + tile_rect.height) > tile_draw_dirty_union.y;
                        if (tile_in_dirty && !roof_is_covered_xy(center_x + 40, center_y + 20, false)) {
                            // CE: void/gap tiles were already replaced with real terrain
                            // in this sector's tile data at load (void_edge_fade_sector),
                            // so the normal draw path renders them affixed to the map.
                            art_blit_info.art_id = sectors[v15]->tiles.art_ids[indexes[v15]];

                            tile_type = tig_art_tile_id_type_get(art_blit_info.art_id);
                            tile_rect.x = center_x + 1;
                            tile_rect.y = center_y;

                            rect_node = *draw_info->rects;
                            while (rect_node != NULL) {
                                if (tig_rect_intersection(&tile_rect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                    src_rect.x = dst_rect.x - tile_rect.x;
                                    src_rect.y = dst_rect.y - tile_rect.y;
                                    src_rect.width = dst_rect.width;
                                    src_rect.height = dst_rect.height;

                                    if (!blit_info_initialized) {
                                        blit_info_initialized = true;

                                        art_blit_info.dst_video_buffer = dword_602DF0;
                                        art_blit_info.field_14 = v36;

                                        color = !tile_type ? indoor_color : outdoor_color;

                                        if (sub_4DA360(center_x, center_y, color, v51)) {
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_LERP;
                                            if (!tile_hardware_accelerated) {
                                                art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                            }
                                        } else if (v51[0] != color || tile_hardware_accelerated) {
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_CONST;
                                            art_blit_info.color = v51[0];
                                            if (!tile_hardware_accelerated) {
                                                art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                            }
                                        } else if (tile_gpu_active) {
                                            // CE (feature/perf-gpu-accel): the plain (flags=0)
                                            // software blit relies on the WORKING palette, which
                                            // sub_4DE0B0 tints by the ambient (light_*_color) via
                                            // TIG_PALETTE_MODIFY_TINT == tig_color_mul. The GPU art
                                            // cache holds the ORIGINAL palette (so LERP/CONST tiles,
                                            // which carry the ambient in their v51 lighting, aren't
                                            // double-tinted), so a plain GPU blit would drop the
                                            // ambient entirely and render at full daytime brightness.
                                            // This branch is only reached when v51[0] == color ==
                                            // ambient, so emit a CONST modulate by v51[0]: on the GPU
                                            // that multiplies the original-palette texture by the
                                            // ambient (== the working-palette tint), and on a cache
                                            // miss the deferred software replay (CONST | original)
                                            // computes the same product. Software path unchanged.
                                            art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_CONST | TIG_ART_BLT_PALETTE_ORIGINAL;
                                            art_blit_info.color = v51[0];
                                        } else {
                                            art_blit_info.flags = 0;
                                        }

                                        // CE void-edge fade: feather whatever edge tile
                                        // (terrain or facade) borders the void into
                                        // black, using a per-vertex brightness from the
                                        // density field, then force the lerp blit.
                                        // Indoor-type TILE art never fades (dungeon and
                                        // cave floors keep their hard black edges — the
                                        // original cave/dungeon exception); facade art
                                        // is always eligible so cliff perimeters fade.
                                        if (tig_art_type(art_blit_info.art_id) != TIG_ART_TYPE_TILE
                                            || tile_type == TIG_ART_TILE_TYPE_OUTDOOR) {
                                            unsigned char ff[9];
                                            tile_pre_sum = (int)tig_color_get_red(v51[4])
                                                + (int)tig_color_get_green(v51[4])
                                                + (int)tig_color_get_blue(v51[4]);
                                            if (void_edge_fade_fade_factors(v3->sector_ids[v15], sectors[v15], indexes[v15], ff)) {
                                                v51[0] = tig_color_mul(v51[0], tig_color_make(ff[0], ff[0], ff[0]));
                                                v51[1] = tig_color_mul(v51[1], tig_color_make(ff[1], ff[1], ff[1]));
                                                v51[2] = tig_color_mul(v51[2], tig_color_make(ff[2], ff[2], ff[2]));
                                                v51[3] = tig_color_mul(v51[3], tig_color_make(ff[3], ff[3], ff[3]));
                                                v51[4] = tig_color_mul(v51[4], tig_color_make(ff[4], ff[4], ff[4]));
                                                v51[5] = tig_color_mul(v51[5], tig_color_make(ff[5], ff[5], ff[5]));
                                                v51[6] = tig_color_mul(v51[6], tig_color_make(ff[6], ff[6], ff[6]));
                                                v51[7] = tig_color_mul(v51[7], tig_color_make(ff[7], ff[7], ff[7]));
                                                v51[8] = tig_color_mul(v51[8], tig_color_make(ff[8], ff[8], ff[8]));
                                                art_blit_info.flags = TIG_ART_BLT_BLEND_COLOR_LERP;
                                                if (!tile_hardware_accelerated) {
                                                    art_blit_info.flags |= TIG_ART_BLT_PALETTE_ORIGINAL;
                                                }
                                                tile_fade_center = ff[4];
                                            }
                                        }
                                    }

                                    if ((art_blit_info.flags & TIG_ART_BLT_BLEND_COLOR_LERP) != 0) {
                                        art_blit_info.field_18 = &tile_subrect;

                                        tile_subrect.x = tile_rect.x;
                                        tile_subrect.y = tile_rect.y;
                                        tile_subrect.width = 39;
                                        tile_subrect.height = 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[0];
                                            v36[1] = v51[1];
                                            v36[2] = v51[4];
                                            v36[3] = v51[3];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x + 39;
                                        tile_subrect.y = tile_rect.y;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[1];
                                            v36[1] = v51[2];
                                            v36[2] = v51[5];
                                            v36[3] = v51[4];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x;
                                        tile_subrect.y = tile_rect.y + 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[3];
                                            v36[1] = v51[4];
                                            v36[2] = v51[7];
                                            v36[3] = v51[6];

                                            tile_blit_dispatch(&art_blit_info);
                                        }

                                        tile_subrect.x = tile_rect.x + 39;
                                        tile_subrect.y = tile_rect.y + 20;
                                        if (tig_rect_intersection(&tile_subrect, &(rect_node->rect), &dst_rect) == TIG_OK) {
                                            tile_subrect.x -= tile_rect.x;
                                            tile_subrect.y -= tile_rect.y;

                                            src_rect.x = dst_rect.x - tile_rect.x;
                                            src_rect.y = dst_rect.y - tile_rect.y;
                                            src_rect.width = dst_rect.width;
                                            src_rect.height = dst_rect.height;

                                            v36[0] = v51[4];
                                            v36[1] = v51[5];
                                            v36[2] = v51[8];
                                            v36[3] = v51[7];

                                            tile_blit_dispatch(&art_blit_info);
                                        }
                                    } else {
                                        tile_blit_dispatch(&art_blit_info);
                                    }
                                }
                                rect_node = rect_node->next;
                            }

                            // CE: this facade tile was drawn this frame — queue its
                            // center for the batched rendered-pixel scan below.
                            // Record gates: facade-type art only (terrain must never
                            // mark — dark-but-lit ground at night would fade the play
                            // area into phantom clouds), lit (pre-fade lighting bright
                            // — an unlit night facade is dark lighting, not black
                            // art), and not already marked (settled tiles need no
                            // re-probing). The center fade factor is recorded so the
                            // scan can divide the fade back out and judge the
                            // PRE-fade brightness.
                            if (void_edge_fade_enabled()
                                && blit_info_initialized
                                && tile_pre_sum >= 300
                                && tig_art_type(art_blit_info.art_id) != TIG_ART_TYPE_TILE
                                && !void_edge_fade_dark_marked(v3->sector_ids[v15], indexes[v15])
                                && gap_scan_count < (int)(sizeof(gap_scan_list) / sizeof(gap_scan_list[0]))
                                && !g_tile_threads_active) {
                                gap_scan_list[gap_scan_count].sec_id = v3->sector_ids[v15];
                                gap_scan_list[gap_scan_count].index = indexes[v15];
                                gap_scan_list[gap_scan_count].px = center_x + 40;
                                gap_scan_list[gap_scan_count].py = center_y + 20;
                                gap_scan_list[gap_scan_count].ff_center = tile_fade_center;
                                gap_scan_count++;
                            }
                        }

                        indexes[v15]++;
                        center_x -= 40;
                        center_y += 20;
                    }

                    indexes[v15] += widths[v15];
                } else {
                    center_x -= 40 * v3->num_hor_tiles[v15];
                    center_y += 20 * v3->num_hor_tiles[v15];
                }
            }

            v10 += 40;
            v11 += 20;
        }

        for (v4 = 0; v4 < v3->num_cols; v4++) {
            if (sector_lock_results[v4]) {
                if (g_tile_threads_active) SDL_LockMutex(g_tile_sector_mutex);
                sector_unlock(v3->sector_ids[v4]);
                if (g_tile_threads_active) SDL_UnlockMutex(g_tile_sector_mutex);
            }
        }
    }

    // CE (feature/perf-gpu-accel): in software mode the tiles are on the CPU
    // surface now, so run the facade black-detection scan here. In GPU mode the
    // tiles are still on the GPU world target (read back later in
    // tile_gpu_world_end, after object/roof), so the scan runs there instead.
    if (!tile_gpu_active) {
        tile_void_edge_scan();
    }

    light_buffers_unlock();
}
