#include "tig/video.h"

#include <limits.h>
#include <stdio.h>

#if SDL_PLATFORM_MACOS
#include <objc/message.h>
#include <objc/runtime.h>
#endif

// CE: NEON intrinsics on ARM (Apple Silicon, mobile). Used by the
// near-black-to-color-key one-shot bake in
// tig_video_buffer_replace_near_black_with_color_key.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define TIG_HAVE_NEON 1
#endif

#include "tig/art.h"
#include "tig/color.h"
#include "tig/core.h"
#include "tig/debug.h"
#include "tig/draw.h"
#include "tig/file.h"
#include "tig/memory.h"
#include "tig/message.h"
#include "tig/mouse.h"
#include "tig/timer.h"
#include "tig/window.h"

typedef struct TigVideoBuffer {
    TigVideoBufferFlags flags;
    TigRect frame;
    int texture_width;
    int texture_height;
    unsigned int background_color;
    unsigned int color_key;
    SDL_Surface* surface;
    int lock_count;
} TigVideoBuffer;

typedef struct TigVideoState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_Surface* surface;
    int fps;
} TigVideoState;

typedef struct TigFadeState {
    bool enabled;
    SDL_Color color;
} TigFadeState;

static bool tig_video_window_create(TigInitInfo* init_info);
static void tig_video_window_destroy(void);
static bool sub_524830(void);
static int tig_video_screenshot_make_internal(int key);
static int tig_video_buffer_data_to_bmp(SDL_Surface* surface, TigRect* rect, const char* file_name);

#if SDL_PLATFORM_MACOS
// Tracks whether we created the window with the borderless-cover-full-display
// path. macOS resets the app's presentation options and our NSWindow level on
// app deactivation/reactivation, so we re-apply them on focus regain to keep
// the menu bar (and its accompanying title-bar slide-down) from reappearing.
static bool tig_video_macos_cover_full_display;

static void tig_video_macos_apply_chrome(SDL_Window* window);
#endif

// 0x5BF3D8
static int tig_video_screenshot_key = -1;

// 0x60F250
static TigVideoState tig_video_state;

// 0x60FEF8
static float tig_video_gamma;

// 0x61030C
static int tig_video_bpp;

// 0x61031C
static bool tig_video_3d_initialized;

// 0x610320
static bool tig_video_3d_is_hardware;

// 0x610358
static bool tig_video_3d_scene_started;

// 0x610388
static TigRect stru_610388;

// 0x61039C
static bool tig_video_initialized;

// 0x6103A0
static bool tig_video_show_fps;

// 0x6103A4
static int dword_6103A4;

static TigFadeState tig_fade_state;

// Optional dirty rect for the next tig_video_flip. When set (width > 0), the
// flip uploads only that rect from the surface to the GPU texture instead of
// the whole surface, saving the per-frame CPU→GPU bandwidth (~8MB at 1080p32)
// for the unchanged pixels. Cleared automatically after each flip — every
// caller has to opt back in per frame, which keeps the safe default
// (full-surface upload) in any code path that doesn't know to set it.
static TigRect tig_video_present_dirty_rect;
static bool tig_video_present_dirty_rect_valid;

// Intra-flip timing breakdown. Driven by gamelib's F9 perf toggle.
static bool tig_video_flip_perf_enabled = false;
static TigVideoFlipPerf tig_video_flip_perf;

// CE: per-call timing for tig_video_blit_near_black_tinted. Used to
// quantify the translucent-black tint pathway's CPU cost so we can
// compare it against the alpha-blend variant in the sibling branch.
// Driven by the same F9 perf toggle so collection turns on/off
// alongside the existing flip-perf counters.
static bool tig_video_tint_blit_perf_enabled = false;
static TigVideoTintBlitPerf tig_video_tint_blit_perf;

// 0x51F330
int tig_video_init(TigInitInfo* init_info)
{
    memset(&tig_video_state, 0, sizeof(tig_video_state));

    if (init_info->width < 800 || init_info->height < 600) {
        return TIG_ERR_GENERIC;
    }

    if (init_info->bpp != 32) {
        return TIG_ERR_GENERIC;
    }

    if (!tig_video_window_create(init_info)) {
        return TIG_ERR_GENERIC;
    }

    if (!sub_524830()) {
        tig_video_window_destroy();
        return TIG_ERR_GENERIC;
    }

    tig_video_show_fps = (init_info->flags & TIG_INITIALIZE_FPS) != 0;
    tig_video_bpp = init_info->bpp;

    tig_video_screenshot_key = -1;
    dword_6103A4 = 0;

    tig_video_initialized = true;

    return TIG_OK;
}

// 0x51F3F0
void tig_video_exit(void)
{
    tig_video_window_destroy();
    tig_video_initialized = false;
#if SDL_PLATFORM_MACOS
    tig_video_macos_cover_full_display = false;
#endif
}

#if SDL_PLATFORM_MACOS
// Apply the borderless-cover-full-display NSWindow chrome (level above the
// menu bar, no drop shadow, app-wide hide of dock+menu bar). Safe to call
// repeatedly; we re-run this on focus regain because macOS resets both the
// app's presentation options and the NSWindow level when the app deactivates,
// which would otherwise let the menu bar (and its accompanying title-bar
// slide-down) reappear at the top of the screen.
static void tig_video_macos_apply_chrome(SDL_Window* window)
{
    if (window != NULL) {
        SDL_PropertiesID window_props = SDL_GetWindowProperties(window);
        void* nswindow = SDL_GetPointerProperty(window_props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        if (nswindow != NULL) {
            // NSMainMenuWindowLevel = 24; one above keeps us on top of the
            // menu bar without entering NSPopUpMenuWindowLevel territory.
            const long kAboveMenuBarLevel = 25;
            ((void (*)(void*, SEL, long))objc_msgSend)(nswindow, sel_registerName("setLevel:"), kAboveMenuBarLevel);

            // Borderless NSWindows still get the system drop shadow, which
            // shows up as a faint 1px border around the edges of the screen
            // for an edge-to-edge window.
            ((void (*)(void*, SEL, signed char))objc_msgSend)(nswindow, sel_registerName("setHasShadow:"), 0);
        }
    }

    // Hide the dock and menu bar app-wide. We use full HIDE (not auto-hide)
    // so the menu bar can never reappear at the top edge -- auto-hide leaves
    // a hot zone there where macOS will pop the menu bar back and slide a
    // window title bar in alongside it.
    id ns_app_class = (id)objc_getClass("NSApplication");
    id ns_app = ((id (*)(id, SEL))objc_msgSend)(ns_app_class, sel_registerName("sharedApplication"));
    if (ns_app != NULL) {
        // NSApplicationPresentationHideDock     = 1 << 1
        // NSApplicationPresentationHideMenuBar  = 1 << 3
        const unsigned long kHideDockAndMenuBar = (1UL << 1) | (1UL << 3);
        ((void (*)(id, SEL, unsigned long))objc_msgSend)(ns_app, sel_registerName("setPresentationOptions:"), kHideDockAndMenuBar);
    }
}

void tig_video_macos_reapply_chrome(void)
{
    if (!tig_video_initialized || !tig_video_macos_cover_full_display) {
        return;
    }
    tig_video_macos_apply_chrome(tig_video_state.window);
}
#endif

int tig_video_window_get(SDL_Window** window_ptr)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    *window_ptr = tig_video_state.window;

    return TIG_OK;
}

int tig_video_renderer_get(SDL_Renderer** renderer_ptr)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    *renderer_ptr = tig_video_state.renderer;

    return TIG_OK;
}

// 0x51F4A0
void tig_video_display_fps(void)
{
    // 0x60F248
    static tig_timestamp_t curr;

    // 0x610380
    static tig_timestamp_t prev;

    // 0x610384
    static unsigned int counter;

    tig_duration_t elapsed;

    if (tig_video_show_fps) {
        ++counter;
        if (counter >= 10) {
            tig_timer_now(&curr);
            elapsed = tig_timer_between(prev, curr);
            tig_video_state.fps = (int)((float)counter / ((float)elapsed / 1000.0f));
            prev = curr;
            counter = 0;
        }
    }
}

// 0x51F600
int tig_video_blit(TigVideoBuffer* src_video_buffer, TigRect* src_rect, TigRect* dst_rect)
{
    int rc;
    TigRect clamped_dst_rect;
    SDL_Rect native_src_rect;
    SDL_Rect native_dst_rect;

    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    rc = tig_rect_intersection(dst_rect, &stru_610388, &clamped_dst_rect);
    if (rc != TIG_OK) {
        return rc;
    }

    native_src_rect.x = src_rect->x;
    native_src_rect.y = src_rect->y;
    native_src_rect.w = src_rect->width;
    native_src_rect.h = src_rect->height;

    native_dst_rect.x = clamped_dst_rect.x;
    native_dst_rect.y = clamped_dst_rect.y;
    native_dst_rect.w = clamped_dst_rect.width;
    native_dst_rect.h = clamped_dst_rect.height;

    SDL_BlitSurface(src_video_buffer->surface,
        &native_src_rect,
        tig_video_state.surface,
        &native_dst_rect);

    return TIG_OK;
}

// CE: blit a window VB to the screen with optional scale + constant
// alpha. Used by the tig compositor's transform path (driven by the
// ui_anim spring system). When alpha < 255 the source is alpha-blended
// over the screen via SDL_BLENDMODE_BLEND; when scale != 1 the dst
// rect's dimensions differ from src and SDL_BlitSurfaceScaled handles
// the resampling. NEAREST sampling for performance — the small scale
// deltas (~5–10%) don't need bilinear.
int tig_video_blit_scaled_alpha(TigVideoBuffer* src_video_buffer,
    TigRect* src_rect,
    TigRect* dst_rect,
    uint8_t alpha)
{
    if (!tig_video_initialized) return TIG_ERR_NOT_INITIALIZED;
    if (src_video_buffer == NULL || src_video_buffer->surface == NULL) {
        return TIG_ERR_INVALID_PARAM;
    }
    TigRect clamped_dst;
    int rc = tig_rect_intersection(dst_rect, &stru_610388, &clamped_dst);
    if (rc != TIG_OK) return rc;

    // Scale-aware clip: derive the proportional src sub-rect that maps
    // to the clamped dst (in case the original dst extended off-screen
    // and got cropped). Without this we'd sample the full src into a
    // smaller dst, producing a mis-scaled image at screen edges.
    int dx_off = clamped_dst.x - dst_rect->x;
    int dy_off = clamped_dst.y - dst_rect->y;
    int dw = dst_rect->width;
    int dh = dst_rect->height;
    SDL_Rect native_src;
    if (dw > 0 && dh > 0) {
        native_src.x = src_rect->x + (int)((float)src_rect->width  * dx_off / (float)dw);
        native_src.y = src_rect->y + (int)((float)src_rect->height * dy_off / (float)dh);
        native_src.w = (int)((float)src_rect->width  * clamped_dst.width  / (float)dw);
        native_src.h = (int)((float)src_rect->height * clamped_dst.height / (float)dh);
        if (native_src.w <= 0) native_src.w = 1;
        if (native_src.h <= 0) native_src.h = 1;
    } else {
        native_src.x = src_rect->x;
        native_src.y = src_rect->y;
        native_src.w = src_rect->width;
        native_src.h = src_rect->height;
    }

    SDL_Rect native_dst = {
        clamped_dst.x, clamped_dst.y,
        clamped_dst.width, clamped_dst.height,
    };

    // Stash + set blend mode + alpha mod on the source surface; restore
    // afterward so other consumers (e.g. plain tig_video_blit) see the
    // surface in its original state.
    SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
    uint8_t prev_alpha = 255;
    SDL_GetSurfaceBlendMode(src_video_buffer->surface, &prev_blend);
    SDL_GetSurfaceAlphaMod(src_video_buffer->surface, &prev_alpha);
    if (alpha < 255) {
        SDL_SetSurfaceBlendMode(src_video_buffer->surface, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(src_video_buffer->surface, alpha);
    }

    // Linear sampling — for the small scale deltas the ui_anim spring
    // produces frame-to-frame (~1% per step), NEAREST jitters: as the
    // integer dst dimensions stair-step by ±1px, source rows / cols
    // snap to different pixels, visibly wiggling the content. Linear
    // interpolates so the transition reads smooth. Marginally slower
    // per-pixel; the affected windows are small enough that it doesn't
    // show in frame budget.
    SDL_BlitSurfaceScaled(src_video_buffer->surface,
        &native_src,
        tig_video_state.surface,
        &native_dst,
        SDL_SCALEMODE_LINEAR);

    if (alpha < 255) {
        SDL_SetSurfaceBlendMode(src_video_buffer->surface, prev_blend);
        SDL_SetSurfaceAlphaMod(src_video_buffer->surface, prev_alpha);
    }
    return TIG_OK;
}

int tig_video_blit_transform_tinted(TigVideoBuffer* src_video_buffer,
    TigRect* src_rect,
    TigRect* dst_rect,
    TigVideoBuffer* underlay_video_buffer,
    int underlay_offset_x,
    int underlay_offset_y,
    uint8_t threshold,
    uint8_t tint_r,
    uint8_t tint_g,
    uint8_t tint_b,
    uint8_t alpha)
{
    uint64_t perf_t0 = tig_video_tint_blit_perf_enabled
        ? SDL_GetPerformanceCounter()
        : 0;

    if (!tig_video_initialized) return TIG_ERR_NOT_INITIALIZED;
    if (src_video_buffer == NULL || src_video_buffer->surface == NULL
        || tig_video_state.surface == NULL) {
        return TIG_ERR_INVALID_PARAM;
    }
    TigRect clamped_dst;
    int rc = tig_rect_intersection(dst_rect, &stru_610388, &clamped_dst);
    if (rc != TIG_OK) return rc;

    // Offset within the un-clamped dst (used to keep the proportional
    // src-pixel mapping correct when clipping at screen edges).
    int dx_off = clamped_dst.x - dst_rect->x;
    int dy_off = clamped_dst.y - dst_rect->y;
    int dw = dst_rect->width;
    int dh = dst_rect->height;
    if (dw <= 0 || dh <= 0) return TIG_OK;

    SDL_Surface* src = src_video_buffer->surface;
    SDL_Surface* dst = tig_video_state.surface;
    SDL_Surface* under = (underlay_video_buffer != NULL)
        ? underlay_video_buffer->surface
        : NULL;

    if (!SDL_LockSurface(src)) return TIG_ERR_GENERIC;
    if (!SDL_LockSurface(dst)) { SDL_UnlockSurface(src); return TIG_ERR_GENERIC; }
    if (under != NULL && !SDL_LockSurface(under)) {
        SDL_UnlockSurface(dst);
        SDL_UnlockSurface(src);
        return TIG_ERR_GENERIC;
    }

    int src_pitch_px = src->pitch / 4;
    int dst_pitch_px = dst->pitch / 4;
    int under_pitch_px = (under != NULL) ? (under->pitch / 4) : 0;
    uint32_t* src_pixels = (uint32_t*)src->pixels;
    uint32_t* dst_pixels = (uint32_t*)dst->pixels;
    uint32_t* under_pixels = (under != NULL) ? (uint32_t*)under->pixels : NULL;

    // CE: per-channel multiply factor — same convention as the plain
    // tint blit (output = underlay * (255 - tint) / 256 per channel,
    // hue-preserving).
    int factor_r = 255 - tint_r;
    int factor_g = 255 - tint_g;
    int factor_b = 255 - tint_b;

    // Alpha scaled 0..256 to land on /256 instead of /255 (matches
    // alpha mod math; saves a div per pixel).
    int a256 = alpha + (alpha >> 7);
    int inv_a256 = 256 - a256;

    // Inverse-scale ratio: dst pixel (di, dj) maps to src pixel
    // (src_rect.x + di * sw / dw, src_rect.y + dj * sh / dh). Use
    // integer math, nearest-neighbor sampling. The scale deltas during
    // an entrance are small (~5% per step), so the per-frame difference
    // in src-pixel mapping is gradual; nearest is acceptable here. If
    // wiggle is visible, switch to a 2-tap horizontal bilinear later.
    int sw = src_rect->width;
    int sh = src_rect->height;

    int x_start = clamped_dst.x;
    int y_start = clamped_dst.y;
    int x_end = clamped_dst.x + clamped_dst.width;
    int y_end = clamped_dst.y + clamped_dst.height;

    for (int dy = y_start; dy < y_end; dy++) {
        int dj = (dy - dst_rect->y);
        int sy = src_rect->y + (dj * sh) / dh;
        if (sy < 0) sy = 0;
        if (sy >= src_rect->y + sh) sy = src_rect->y + sh - 1;
        uint32_t* sp_row = src_pixels + sy * src_pitch_px;
        uint32_t* dp_row = dst_pixels + dy * dst_pitch_px;
        uint32_t* up_row = NULL;
        if (under_pixels != NULL) {
            int uy = dy + underlay_offset_y;
            if (uy >= 0 && uy < under->h) {
                up_row = under_pixels + uy * under_pitch_px;
            }
        }
        for (int dx = x_start; dx < x_end; dx++) {
            int di = (dx - dst_rect->x);
            int sx = src_rect->x + (di * sw) / dw;
            if (sx < 0) sx = 0;
            if (sx >= src_rect->x + sw) sx = src_rect->x + sw - 1;
            uint32_t s = sp_row[sx];
            uint8_t sr = (uint8_t)(s >> 16);
            uint8_t sg = (uint8_t)(s >> 8);
            uint8_t sb = (uint8_t)s;

            uint8_t cr, cg, cb;
            if (sr <= threshold && sg <= threshold && sb <= threshold
                && up_row != NULL) {
                int ux = dx + underlay_offset_x;
                if (ux >= 0 && ux < under->w) {
                    uint32_t u = up_row[ux];
                    int ur = (int)((uint8_t)(u >> 16));
                    int ug = (int)((uint8_t)(u >> 8));
                    int ub = (int)((uint8_t)u);
                    cr = (uint8_t)((ur * factor_r + 128) >> 8);
                    cg = (uint8_t)((ug * factor_g + 128) >> 8);
                    cb = (uint8_t)((ub * factor_b + 128) >> 8);
                } else {
                    cr = sr; cg = sg; cb = sb;
                }
            } else {
                cr = sr; cg = sg; cb = sb;
            }

            if (a256 >= 256) {
                dp_row[dx] = 0xFF000000u
                    | ((uint32_t)cr << 16)
                    | ((uint32_t)cg << 8)
                    | (uint32_t)cb;
            } else {
                uint32_t d = dp_row[dx];
                int dr = (int)((uint8_t)(d >> 16));
                int dg = (int)((uint8_t)(d >> 8));
                int db = (int)((uint8_t)d);
                int rr = ((int)cr * a256 + dr * inv_a256) >> 8;
                int gg = ((int)cg * a256 + dg * inv_a256) >> 8;
                int bb = ((int)cb * a256 + db * inv_a256) >> 8;
                dp_row[dx] = 0xFF000000u
                    | ((uint32_t)rr << 16)
                    | ((uint32_t)gg << 8)
                    | (uint32_t)bb;
            }
        }
    }

    (void)dx_off; (void)dy_off;

    if (under != NULL) SDL_UnlockSurface(under);
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    if (tig_video_tint_blit_perf_enabled) {
        uint64_t perf_t1 = SDL_GetPerformanceCounter();
        uint64_t ns = (uint64_t)((double)(perf_t1 - perf_t0)
            * 1e9 / (double)SDL_GetPerformanceFrequency());
        tig_video_tint_blit_perf.total_ns += ns;
        if (ns > tig_video_tint_blit_perf.max_ns) {
            tig_video_tint_blit_perf.max_ns = ns;
        }
        tig_video_tint_blit_perf.samples++;
        tig_video_tint_blit_perf.pixels_total +=
            (uint64_t)clamped_dst.width * (uint64_t)clamped_dst.height;
    }

    return TIG_OK;
}

// CE: composite a window's VB to the screen, replacing "near-black"
// source pixels with subtract-tinted underlay-VB pixels at the same
// screen position. Other source pixels copy through opaque.
//
// Direct-paint architecture (same as the old near_black_alpha blit):
// reads the underlay VB directly so it doesn't depend on the lower
// window having actually painted the screen surface. The screen-
// surface route was unreliable for the iso world window because it
// uses TIG_WINDOW_VIDEO_MEMORY — its SDL_Surface isn't necessarily
// in sync with what reaches the screen during composite, so a
// color-key blit would leave holes showing stale black instead of
// the live world.
//
// underlay_offset_x/y locate the screen's (0, 0) inside the underlay
// VB — for a fullscreen underlay window (e.g. iso) at frame (0, 0)
// these are 0. Fallback (NULL underlay): just blit src opaque.
//
// `tint_r/g/b` express how much to DARKEN each underlay channel on
// a 0..255 scale: 0 = no darkening (full underlay brightness), 255
// = full black. Math is a per-channel MULTIPLY (output = underlay *
// (255 - tint) / 256), so channels scale in proportion and the
// underlay's hue survives — unlike a saturating subtract, which
// clips low channels independently and visibly shifts color toward
// whichever channel was strongest (the "color burn" we hit earlier
// when using SUB). NEON-vectorized fast path on Apple Silicon.
int tig_video_blit_near_black_tinted(TigVideoBuffer* src_video_buffer,
    TigRect* src_rect,
    TigRect* dst_rect,
    TigVideoBuffer* underlay_video_buffer,
    int underlay_offset_x,
    int underlay_offset_y,
    uint8_t threshold,
    uint8_t tint_r,
    uint8_t tint_g,
    uint8_t tint_b,
    uint8_t reveal)
{
    // CE: timing for the tint pathway perf comparison (F9-gated).
    // Wrapping the whole function so the surface lock/unlock cost
    // is included — that's part of the real per-call price.
    uint64_t perf_t0 = tig_video_tint_blit_perf_enabled
        ? SDL_GetPerformanceCounter()
        : 0;

    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }
    if (src_video_buffer == NULL || src_video_buffer->surface == NULL
        || tig_video_state.surface == NULL) {
        return TIG_ERR_INVALID_PARAM;
    }
    TigRect clamped_dst;
    int rc = tig_rect_intersection(dst_rect, &stru_610388, &clamped_dst);
    if (rc != TIG_OK) {
        return rc;
    }
    int dx_off = clamped_dst.x - dst_rect->x;
    int dy_off = clamped_dst.y - dst_rect->y;

    SDL_Surface* src = src_video_buffer->surface;
    SDL_Surface* dst = tig_video_state.surface;
    SDL_Surface* under = (underlay_video_buffer != NULL)
        ? underlay_video_buffer->surface
        : NULL;

    if (!SDL_LockSurface(src)) return TIG_ERR_GENERIC;
    if (!SDL_LockSurface(dst)) { SDL_UnlockSurface(src); return TIG_ERR_GENERIC; }
    if (under != NULL && !SDL_LockSurface(under)) {
        SDL_UnlockSurface(dst);
        SDL_UnlockSurface(src);
        return TIG_ERR_GENERIC;
    }

    int src_pitch_px = src->pitch / 4;
    int dst_pitch_px = dst->pitch / 4;
    int under_pitch_px = (under != NULL) ? (under->pitch / 4) : 0;
    uint32_t* src_base = (uint32_t*)src->pixels
        + (src_rect->y + dy_off) * src_pitch_px
        + (src_rect->x + dx_off);
    uint32_t* dst_base = (uint32_t*)dst->pixels
        + clamped_dst.y * dst_pitch_px
        + clamped_dst.x;
    uint32_t* under_base = NULL;
    if (under != NULL) {
        int uy = clamped_dst.y + underlay_offset_y;
        int ux = clamped_dst.x + underlay_offset_x;
        if (uy < 0 || ux < 0 || uy >= under->h || ux >= under->w) {
            under = NULL;
        } else {
            under_base = (uint32_t*)under->pixels + uy * under_pitch_px + ux;
        }
    }

    // Convert "darken by N out of 255" to per-channel multiply
    // factors (N=0 → factor 255 = full brightness; N=255 → 0).
    uint8_t factor_r = (uint8_t)(255 - tint_r);
    uint8_t factor_g = (uint8_t)(255 - tint_g);
    uint8_t factor_b = (uint8_t)(255 - tint_b);

    int w = clamped_dst.width;
    int h = clamped_dst.height;
    // CE: reveal < 255 path — blend each near-black pixel between
    // (a) the original opaque source and (b) the tinted underlay,
    // using reveal as the alpha. Scalar-only — reveal != 255 only
    // happens during the brief tint-fade-in after a window's
    // entrance settles, so paying the per-pixel cost during ~150ms
    // is acceptable. reveal=255 uses the original NEON fast path
    // below.
    if (reveal < 255) {
        int rev_n = reveal + (reveal >> 7); // 0..256 like alpha-mod
        int inv_n = 256 - rev_n;
        for (int y = 0; y < h; y++) {
            uint32_t* sp = src_base + y * src_pitch_px;
            uint32_t* dp = dst_base + y * dst_pitch_px;
            uint32_t* up = (under_base != NULL) ? (under_base + y * under_pitch_px) : NULL;
            for (int x = 0; x < w; x++) {
                uint32_t s = sp[x];
                uint8_t sr = (uint8_t)(s >> 16);
                uint8_t sg = (uint8_t)(s >> 8);
                uint8_t sb = (uint8_t)s;
                if (sr <= threshold && sg <= threshold && sb <= threshold
                    && up != NULL) {
                    uint32_t u = up[x];
                    uint8_t ur = (uint8_t)(u >> 16);
                    uint8_t ug = (uint8_t)(u >> 8);
                    uint8_t ub = (uint8_t)u;
                    int tr = ((int)ur * (int)factor_r + 128) >> 8;
                    int tg = ((int)ug * (int)factor_g + 128) >> 8;
                    int tb = ((int)ub * (int)factor_b + 128) >> 8;
                    // Blend source (opaque near-black) with tinted
                    // underlay by reveal. (s * (256-rev) + t * rev) >> 8.
                    int rr = ((int)sr * inv_n + tr * rev_n) >> 8;
                    int gg = ((int)sg * inv_n + tg * rev_n) >> 8;
                    int bb = ((int)sb * inv_n + tb * rev_n) >> 8;
                    if (rr > 255) rr = 255;
                    if (gg > 255) gg = 255;
                    if (bb > 255) bb = 255;
                    dp[x] = 0xFF000000u
                        | ((uint32_t)rr << 16)
                        | ((uint32_t)gg << 8)
                        | (uint32_t)bb;
                } else {
                    dp[x] = s;
                }
            }
        }
        if (under != NULL) SDL_UnlockSurface(under);
        SDL_UnlockSurface(dst);
        SDL_UnlockSurface(src);
        if (tig_video_tint_blit_perf_enabled) {
            uint64_t perf_t1 = SDL_GetPerformanceCounter();
            uint64_t ns = (uint64_t)((double)(perf_t1 - perf_t0)
                * 1e9 / (double)SDL_GetPerformanceFrequency());
            tig_video_tint_blit_perf.total_ns += ns;
            if (ns > tig_video_tint_blit_perf.max_ns) {
                tig_video_tint_blit_perf.max_ns = ns;
            }
            tig_video_tint_blit_perf.samples++;
            tig_video_tint_blit_perf.pixels_total +=
                (uint64_t)clamped_dst.width * (uint64_t)clamped_dst.height;
        }
        return TIG_OK;
    }
    for (int y = 0; y < h; y++) {
        uint32_t* sp = src_base + y * src_pitch_px;
        uint32_t* dp = dst_base + y * dst_pitch_px;
        uint32_t* up = (under_base != NULL) ? (under_base + y * under_pitch_px) : NULL;
        int x = 0;
#if TIG_HAVE_NEON
        if (w >= 4 && up != NULL) {
            uint8x16_t thresh_v = vdupq_n_u8(threshold);
            uint8x16_t alpha_clear = vreinterpretq_u8_u32(vdupq_n_u32(0x00FFFFFFu));
            uint8x16_t alpha_set = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000u));
            uint32x4_t allones = vdupq_n_u32(0xFFFFFFFFu);
            // Factor as a 4-byte pixel (X=0, R, G, B) replicated per lane.
            // X (alpha lane) doesn't matter — we mask it out after the
            // multiply and force alpha back to 0xFF below.
            uint32_t factor_pix = ((uint32_t)factor_r << 16)
                                | ((uint32_t)factor_g << 8)
                                | (uint32_t)factor_b;
            uint8x16_t factor_v = vreinterpretq_u8_u32(vdupq_n_u32(factor_pix));
            for (; x + 4 <= w; x += 4) {
                uint32x4_t s4 = vld1q_u32(sp + x);
                uint32x4_t u4 = vld1q_u32(up + x);
                // Near-black mask per pixel.
                uint8x16_t s_rgb = vandq_u8(vreinterpretq_u8_u32(s4), alpha_clear);
                uint8x16_t le = vcleq_u8(s_rgb, thresh_v);
                uint32x4_t le_u32 = vreinterpretq_u32_u8(le);
                uint32x4_t nb_mask = vceqq_u32(le_u32, allones);
                // Per-channel multiply: 8-bit underlay × 8-bit factor →
                // 16-bit product, then rounding shift-right by 8 to
                // approximate /255 (max error 1 LSB per channel). Hue
                // survives because each channel scales independently in
                // proportion to its underlay value.
                uint8x16_t u_b = vreinterpretq_u8_u32(u4);
                uint16x8_t mul_lo = vmull_u8(vget_low_u8(u_b),
                                             vget_low_u8(factor_v));
                uint16x8_t mul_hi = vmull_u8(vget_high_u8(u_b),
                                             vget_high_u8(factor_v));
                uint8x8_t res_lo = vqrshrn_n_u16(mul_lo, 8);
                uint8x8_t res_hi = vqrshrn_n_u16(mul_hi, 8);
                uint8x16_t tinted = vcombine_u8(res_lo, res_hi);
                // Force alpha lane back to 0xFF (the multiply produced
                // whatever the factor's X×underlay-X happens to be).
                tinted = vorrq_u8(vandq_u8(tinted, alpha_clear), alpha_set);
                uint32x4_t tinted32 = vreinterpretq_u32_u8(tinted);
                // Select: near-black → MUL-tinted underlay; else → src.
                uint32x4_t out = vbslq_u32(nb_mask, tinted32, s4);
                vst1q_u32(dp + x, out);
            }
        }
#endif
        for (; x < w; x++) {
            uint32_t s = sp[x];
            uint8_t sr = (uint8_t)(s >> 16);
            uint8_t sg = (uint8_t)(s >> 8);
            uint8_t sb = (uint8_t)s;
            if (sr <= threshold && sg <= threshold && sb <= threshold) {
                if (up != NULL) {
                    uint32_t u = up[x];
                    uint8_t ur = (uint8_t)(u >> 16);
                    uint8_t ug = (uint8_t)(u >> 8);
                    uint8_t ub = (uint8_t)u;
                    // (under * factor + 128) >> 8 matches the NEON
                    // rounding shift, giving consistent results across
                    // the SIMD and scalar paths.
                    int rr = ((int)ur * (int)factor_r + 128) >> 8;
                    int gg = ((int)ug * (int)factor_g + 128) >> 8;
                    int bb = ((int)ub * (int)factor_b + 128) >> 8;
                    if (rr > 255) rr = 255;
                    if (gg > 255) gg = 255;
                    if (bb > 255) bb = 255;
                    dp[x] = 0xFF000000u
                        | ((uint32_t)rr << 16)
                        | ((uint32_t)gg << 8)
                        | (uint32_t)bb;
                } else {
                    dp[x] = s;
                }
            } else {
                dp[x] = s;
            }
        }
    }

    if (under != NULL) SDL_UnlockSurface(under);
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    if (tig_video_tint_blit_perf_enabled) {
        uint64_t perf_t1 = SDL_GetPerformanceCounter();
        uint64_t ns = (uint64_t)((double)(perf_t1 - perf_t0)
            * 1e9 / (double)SDL_GetPerformanceFrequency());
        tig_video_tint_blit_perf.total_ns += ns;
        if (ns > tig_video_tint_blit_perf.max_ns) {
            tig_video_tint_blit_perf.max_ns = ns;
        }
        tig_video_tint_blit_perf.samples++;
        tig_video_tint_blit_perf.pixels_total +=
            (uint64_t)clamped_dst.width * (uint64_t)clamped_dst.height;
    }

    return TIG_OK;
}

// 0x51F7C0
int tig_video_fill(const TigRect* rect, tig_color_t color)
{
    int rc;
    TigRect clamped_rect;
    SDL_Rect native_rect;

    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    rc = tig_rect_intersection(rect != NULL ? rect : &stru_610388,
        &stru_610388,
        &clamped_rect);
    if (rc != TIG_OK) {
        return rc;
    }

    native_rect.x = clamped_rect.x;
    native_rect.y = clamped_rect.y;
    native_rect.w = clamped_rect.width;
    native_rect.h = clamped_rect.height;

    if (!SDL_FillSurfaceRect(tig_video_state.surface, &native_rect, color)) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

void tig_video_set_present_dirty_rect(const TigRect* rect)
{
    if (rect == NULL || rect->width <= 0 || rect->height <= 0) {
        tig_video_present_dirty_rect_valid = false;
        return;
    }
    tig_video_present_dirty_rect = *rect;
    tig_video_present_dirty_rect_valid = true;
}

void tig_video_flip_perf_set_enabled(bool enabled)
{
    tig_video_flip_perf_enabled = enabled;
    if (enabled) {
        memset(&tig_video_flip_perf, 0, sizeof(tig_video_flip_perf));
    }
}

void tig_video_flip_perf_get(TigVideoFlipPerf* out)
{
    if (out != NULL) {
        *out = tig_video_flip_perf;
    }
}

void tig_video_flip_perf_reset(void)
{
    memset(&tig_video_flip_perf, 0, sizeof(tig_video_flip_perf));
}

void tig_video_tint_blit_perf_set_enabled(bool enabled)
{
    tig_video_tint_blit_perf_enabled = enabled;
    if (enabled) {
        memset(&tig_video_tint_blit_perf, 0, sizeof(tig_video_tint_blit_perf));
    }
}

void tig_video_tint_blit_perf_get(TigVideoTintBlitPerf* out)
{
    if (out != NULL) {
        *out = tig_video_tint_blit_perf;
    }
}

void tig_video_tint_blit_perf_reset(void)
{
    memset(&tig_video_tint_blit_perf, 0, sizeof(tig_video_tint_blit_perf));
}

int tig_video_set_vsync_mode(int mode)
{
    if (tig_video_state.renderer == NULL) {
        return TIG_ERR_NOT_INITIALIZED;
    }
    if (!SDL_SetRenderVSync(tig_video_state.renderer, mode)) {
        tig_debug_printf("tig_video_set_vsync_mode: SDL_SetRenderVSync(%d) failed: %s\n",
            mode, SDL_GetError());
        return TIG_ERR_GENERIC;
    }
    tig_debug_printf("tig_video_set_vsync_mode: applied mode=%d (%s)\n",
        mode,
        mode == 0 ? "off" :
        mode == 1 ? "on" :
        mode == -1 ? "adaptive" : "custom");
    return TIG_OK;
}

// SDL_GetPerformanceCounter ticks → nanoseconds.
static uint64_t tig_video_flip_ticks_to_ns(uint64_t ticks)
{
    uint64_t freq = SDL_GetPerformanceFrequency();
    if (freq == 0) return 0;
    return (uint64_t)((double)ticks * 1e9 / (double)freq);
}

// 0x51F8F0
int tig_video_flip(void)
{
    // Partial-upload fast path: only re-upload the rect the compositor
    // touched this present cycle. Falls back to full-surface upload when
    // no hint is set or the rect is bigger than ~3/4 of the surface
    // (point at which partial-upload overhead exceeds savings).
    bool partial = false;
    TigRect upload_rect;
    if (tig_video_present_dirty_rect_valid && tig_video_state.surface != NULL) {
        upload_rect = tig_video_present_dirty_rect;
        TigRect surface_rect = { 0, 0, tig_video_state.surface->w, tig_video_state.surface->h };
        if (upload_rect.x < surface_rect.x) {
            upload_rect.width -= (surface_rect.x - upload_rect.x);
            upload_rect.x = surface_rect.x;
        }
        if (upload_rect.y < surface_rect.y) {
            upload_rect.height -= (surface_rect.y - upload_rect.y);
            upload_rect.y = surface_rect.y;
        }
        if (upload_rect.x + upload_rect.width > surface_rect.x + surface_rect.width) {
            upload_rect.width = surface_rect.x + surface_rect.width - upload_rect.x;
        }
        if (upload_rect.y + upload_rect.height > surface_rect.y + surface_rect.height) {
            upload_rect.height = surface_rect.y + surface_rect.height - upload_rect.y;
        }
        int64_t upload_px = (int64_t)upload_rect.width * (int64_t)upload_rect.height;
        int64_t surface_px = (int64_t)surface_rect.width * (int64_t)surface_rect.height;
        partial = upload_rect.width > 0
            && upload_rect.height > 0
            && upload_px * 4 < surface_px * 3;
    }
    tig_video_present_dirty_rect_valid = false;

    uint64_t flip_t0 = tig_video_flip_perf_enabled ? SDL_GetPerformanceCounter() : 0;

    if (partial) {
        int bpp = tig_video_state.surface->format == SDL_PIXELFORMAT_UNKNOWN
            ? 4
            : (int)SDL_BYTESPERPIXEL(tig_video_state.surface->format);
        SDL_Rect sdl_rect = { upload_rect.x, upload_rect.y, upload_rect.width, upload_rect.height };
        const uint8_t* src = (const uint8_t*)tig_video_state.surface->pixels
            + (size_t)upload_rect.y * (size_t)tig_video_state.surface->pitch
            + (size_t)upload_rect.x * (size_t)bpp;
        SDL_UpdateTexture(tig_video_state.texture, &sdl_rect, src, tig_video_state.surface->pitch);
    } else {
        SDL_UpdateTexture(tig_video_state.texture, NULL, tig_video_state.surface->pixels, tig_video_state.surface->pitch);
    }

    uint64_t flip_t1 = tig_video_flip_perf_enabled ? SDL_GetPerformanceCounter() : 0;

    SDL_RenderClear(tig_video_state.renderer);
    SDL_RenderTexture(tig_video_state.renderer, tig_video_state.texture, NULL, NULL);

    if (tig_fade_state.enabled) {
        SDL_BlendMode blend_mode;
        SDL_GetRenderDrawBlendMode(tig_video_state.renderer, &blend_mode);
        SDL_SetRenderDrawBlendMode(tig_video_state.renderer, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetRenderDrawColor(tig_video_state.renderer,
            tig_fade_state.color.r,
            tig_fade_state.color.g,
            tig_fade_state.color.b,
            tig_fade_state.color.a);
        SDL_RenderFillRect(tig_video_state.renderer, NULL);
        SDL_SetRenderDrawBlendMode(tig_video_state.renderer, blend_mode);
    }

    if (tig_video_show_fps) {
        SDL_SetRenderDrawColor(tig_video_state.renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugTextFormat(tig_video_state.renderer, 0, 0, "%d", tig_video_state.fps);
    }

    uint64_t flip_t2 = tig_video_flip_perf_enabled ? SDL_GetPerformanceCounter() : 0;

    SDL_RenderPresent(tig_video_state.renderer);

    if (tig_video_flip_perf_enabled) {
        uint64_t flip_t3 = SDL_GetPerformanceCounter();
        uint64_t upload_ns = tig_video_flip_ticks_to_ns(flip_t1 - flip_t0);
        uint64_t present_ns = tig_video_flip_ticks_to_ns(flip_t3 - flip_t2);
        tig_video_flip_perf.update_total_ns += upload_ns;
        tig_video_flip_perf.present_total_ns += present_ns;
        if (upload_ns > tig_video_flip_perf.update_max_ns) tig_video_flip_perf.update_max_ns = upload_ns;
        if (present_ns > tig_video_flip_perf.present_max_ns) tig_video_flip_perf.present_max_ns = present_ns;
        tig_video_flip_perf.samples++;
        if (partial) tig_video_flip_perf.partial_samples++;
    }

    return TIG_OK;
}

// 0x51F9E0
int tig_video_screenshot_set_settings(TigVideoScreenshotSettings* settings)
{
    int rc;

    if (tig_video_screenshot_key != -1) {
        rc = tig_message_set_key_handler(NULL, tig_video_screenshot_key);
        if (rc != TIG_OK) {
            return rc;
        }
    }

    tig_video_screenshot_key = settings->key;
    dword_6103A4 = settings->field_4;

    if (tig_video_screenshot_key != -1) {
        rc = tig_message_set_key_handler(tig_video_screenshot_make_internal, tig_video_screenshot_key);
        if (rc != TIG_OK) {
            return rc;
        }
    }

    return TIG_OK;
}

// 0x51FA30
int tig_video_screenshot_make(void)
{
    return tig_video_screenshot_make_internal(tig_video_screenshot_key);
}

// 0x51FAA0
int tig_video_get_palette(unsigned int* colors)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    memset(colors, 0, sizeof(*colors) * 256);

    return TIG_OK;
}

// 0x51FB10
int tig_video_3d_check_initialized(void)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (!tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x51FB30
int tig_video_3d_check_hardware(void)
{
    if (!tig_video_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (!tig_video_3d_is_hardware) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x51FB50
int tig_video_3d_begin_scene(void)
{
    if (tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    tig_video_3d_scene_started = true;
    return TIG_OK;
}

// 0x51FBA0
int tig_video_3d_end_scene(void)
{
    if (!tig_video_3d_scene_started) {
        return TIG_ERR_GENERIC;
    }

    if (tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    tig_video_3d_scene_started = false;
    return TIG_OK;
}

// 0x51FC90
int tig_video_check_gamma_control(void)
{
    return TIG_OK;
}

// NOTE: The original code implements fading using DirectDraw gamma ramp
// adjustments which is not available in SDL3. This implementation achieves
// fading effect by drawing alpha-blended rectangle covering entire window in
// `tig_video_flip`.
//
// This implementation prefers `steps` over `duration` - the decision is based
// on subjective perception of default fade steps (48 steps) vs. default fade
// duration (2 seconds) on a target 60 fps monitor. The steps approach takes
// about 800 ms which is perfectly fine.
//
// 0x51FCA0
int tig_video_fade(tig_color_t color, int steps, float duration, TigFadeFlags flags)
{
    int step;

    (void)duration;

    if ((flags & TIG_FADE_IN) == 0) {
        // Enable faded state.
        tig_fade_state.enabled = true;
        tig_fade_state.color.r = (Uint8)tig_color_get_red(color);
        tig_fade_state.color.g = (Uint8)tig_color_get_green(color);
        tig_fade_state.color.b = (Uint8)tig_color_get_blue(color);
    }

    if ((flags & TIG_FADE_IN) != 0) {
        // Fade in by gradually reducing alpha from 255 (fully opaque) to 0
        // (completely transparent).
        for (step = 0; step < steps; step++) {
            tig_fade_state.color.a = (Uint8)(255 - step * 255 / steps);
            tig_video_flip();
        }
    } else {
        // Fade out by gradually increasing alpha from 0 (completely
        // transparent) to 255 (fully opaque).
        for (step = 0; step < steps; step++) {
            tig_fade_state.color.a = (Uint8)(step * 255 / steps);
            tig_video_flip();
        }
    }

    if ((flags & TIG_FADE_IN) != 0) {
        // Disable faded state.
        tig_fade_state.enabled = false;
    }

    return TIG_OK;
}

// 0x51FFE0
int tig_video_set_gamma(float gamma)
{
    if (gamma == tig_video_gamma) {
        return TIG_OK;
    }

    if (gamma < 0.0 || gamma >= 2.0) {
        return TIG_ERR_INVALID_PARAM;
    }

    tig_video_gamma = gamma;
    tig_video_fade(0, 0, 0.0, 1);

    return TIG_OK;
}

// 0x5200F0
int tig_video_buffer_create(TigVideoBufferCreateInfo* vb_create_info, TigVideoBuffer** video_buffer_ptr)
{
    TigVideoBuffer* video_buffer;
    int texture_width;
    int texture_height;

    video_buffer = (TigVideoBuffer*)MALLOC(sizeof(*video_buffer));
    memset(video_buffer, 0, sizeof(*video_buffer));
    *video_buffer_ptr = video_buffer;

    texture_width = vb_create_info->width;
    texture_height = vb_create_info->height;

    video_buffer->surface = SDL_CreateSurface(texture_width, texture_height, SDL_PIXELFORMAT_XRGB8888);
    if (video_buffer->surface == NULL) {
        return TIG_ERR_OUT_OF_MEMORY;
    }

    video_buffer->flags |= TIG_VIDEO_BUFFER_SYSTEM_MEMORY;

    if ((vb_create_info->flags & TIG_VIDEO_BUFFER_CREATE_COLOR_KEY) != 0) {
        video_buffer->flags |= TIG_VIDEO_BUFFER_COLOR_KEY;
        tig_video_buffer_set_color_key(*video_buffer_ptr, vb_create_info->color_key);
    }

    video_buffer->frame.x = 0;
    video_buffer->frame.y = 0;
    video_buffer->frame.width = vb_create_info->width;
    video_buffer->frame.height = vb_create_info->height;
    video_buffer->texture_width = texture_width;
    video_buffer->texture_height = texture_height;
    video_buffer->background_color = vb_create_info->background_color;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_TEXTURE) == 0) {
        SDL_FillSurfaceRect(video_buffer->surface, NULL, vb_create_info->background_color);
    }

    video_buffer->lock_count = 0;

    return TIG_OK;
}

// 0x520390
int tig_video_buffer_destroy(TigVideoBuffer* video_buffer)
{
    if (video_buffer == NULL) {
        return TIG_ERR_GENERIC;
    }

    SDL_DestroySurface(video_buffer->surface);
    FREE(video_buffer);

    return TIG_OK;
}

// 0x5203E0
int tig_video_buffer_data(TigVideoBuffer* video_buffer, TigVideoBufferData* video_buffer_data)
{
    if (video_buffer == NULL) {
        return TIG_ERR_GENERIC;
    }

    video_buffer_data->flags = video_buffer->flags;
    video_buffer_data->width = video_buffer->frame.width;
    video_buffer_data->height = video_buffer->frame.height;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_LOCKED) != 0) {
        video_buffer_data->pitch = video_buffer->surface->pitch;
    } else {
        video_buffer_data->pitch = 0;
    }

    video_buffer_data->background_color = video_buffer->background_color;
    video_buffer_data->color_key = video_buffer->color_key;
    video_buffer_data->bpp = tig_video_bpp;

    if ((video_buffer->flags & TIG_VIDEO_BUFFER_LOCKED) != 0) {
        video_buffer_data->pixels = video_buffer->surface->pixels;
    } else {
        video_buffer_data->pixels = NULL;
    }

    return TIG_OK;
}

// 0x520450
int tig_video_buffer_set_color_key(TigVideoBuffer* video_buffer, int color_key)
{
    // CE: enable color-key matching on the surface unconditionally
    // (was guarded on TIG_VIDEO_BUFFER_COLOR_KEY, which is only auto-
    // set at create time for windows created with TIG_WINDOW_TRANSPARENT
    // — the guard silently rejected late opt-ins like the HUD bar's
    // dialog-style translucent-black bake). SDL_SetSurfaceColorKey
    // works on any surface; the tig flag is just a tracking bool we
    // flip alongside so subsequent code sees the surface as having
    // color-key.
    if (!SDL_SetSurfaceColorKey(video_buffer->surface, true, color_key)) {
        return TIG_ERR_GENERIC;
    }

    video_buffer->flags |= TIG_VIDEO_BUFFER_COLOR_KEY;
    video_buffer->color_key = color_key;

    return TIG_OK;
}

// 0x5204B0
int tig_video_buffer_lock(TigVideoBuffer* video_buffer)
{
    if (video_buffer->lock_count == 0) {
        if (!SDL_LockSurface(video_buffer->surface)) {
            return TIG_ERR_DIRECTX;
        }

        video_buffer->flags |= TIG_VIDEO_BUFFER_LOCKED;
    }

    video_buffer->lock_count++;

    return TIG_OK;
}

// 0x520500
int tig_video_buffer_unlock(TigVideoBuffer* video_buffer)
{
    if (video_buffer->lock_count == 1) {
        SDL_UnlockSurface(video_buffer->surface);
        video_buffer->flags &= ~TIG_VIDEO_BUFFER_LOCKED;
    }

    video_buffer->lock_count--;

    return TIG_OK;
}

// 0x520540
int tig_video_buffer_outline(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color)
{
    TigRect line;
    int rc;

    line.x = rect->x;
    line.y = rect->y;
    line.width = rect->width;
    line.height = 1;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x;
    line.y = rect->y;
    line.width = 1;
    line.height = rect->height;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x + rect->width - 1;
    line.y = rect->y;
    line.width = 1;
    line.height = rect->height;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    line.x = rect->x;
    line.y = rect->y + rect->height - 1;
    line.width = rect->width;
    line.height = 1;
    rc = tig_video_buffer_fill(video_buffer, &line, color);
    if (rc != TIG_OK) {
        return rc;
    }

    return TIG_OK;
}

// 0x520630
int tig_video_buffer_fill(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color)
{
    SDL_Rect native_rect;

    if (rect != NULL) {
        native_rect.x = rect->x;
        native_rect.y = rect->y;
        native_rect.w = rect->width;
        native_rect.h = rect->height;
    }

    if (!SDL_FillSurfaceRect(video_buffer->surface, rect != NULL ? &native_rect : NULL, color)) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x520660
int tig_video_buffer_line(TigVideoBuffer* video_buffer, TigLine* line, TigRect* a3, tig_color_t color)
{
    int pattern = 0;
    bool reversed;
    TigDrawLineModeInfo mode_info;
    TigDrawLineStyleInfo style_info;
    int dx;
    int dy;
    int step_x;
    int step_y;
    int error;
    int error_y;
    int error_x;
    int x1;
    int y1;
    int x2;
    int y2;
    int x;
    int y;

    (void)a3;

    if (tig_video_buffer_lock(video_buffer) != TIG_OK) {
        return TIG_ERR_DIRECTX;
    }

    if (line->y2 <= line->y1) {
        if (line->y2 < line->y1) {
            reversed = true;
        } else {
            reversed = line->x1 >= line->x2;
        }
    } else {
        reversed = false;
    }

    tig_draw_get_line_mode(&mode_info);
    tig_draw_get_line_style(&style_info);

    dx = abs(line->x2 - line->x1);
    dy = abs(line->y2 - line->y1);

    if (reversed) {
        if (dx < dy) {
            error_y = 2 * dx;
            error = 2 * dx - dy;
            error_x = 2 * (dx - dy);
        } else {
            error_y = 2 * dy;
            error = 2 * dy - dx;
            error_x = 2 * (dy - dx);
        }

        x1 = line->x2;
        y1 = line->y2;
        x2 = line->x1;
        y2 = line->y1;

        if (line->x1 < line->x2) {
            step_x = -1;
        } else if (line->x1 > line->x2) {
            step_x = 1;
        } else {
            step_x = 0;
        }
    } else {
        if (dx < dy) {
            error_y = 2 * dx;
            error = 2 * dx - dy;
            error_x = 2 * (dx - dy);
        } else {
            error_y = 2 * dy;
            error = 2 * dy - dx;
            error_x = 2 * (dy - dx);
        }

        x1 = line->x1;
        y1 = line->y1;
        x2 = line->x2;
        y2 = line->y2;

        if (line->x1 < line->x2) {
            step_x = 1;
        } else if (line->x1 > line->x2) {
            step_x = -1;
        } else {
            step_x = 0;
        }
    }

    switch (tig_video_bpp) {
    case 32:
        if (1) {
            uint32_t* dst = (uint32_t*)video_buffer->surface->pixels + (video_buffer->surface->pitch / 4) * y1 + x1;
            *dst = color;

            if (dx < dy) {
                y = y1;
                while (y != y2) {
                    if (error > 0) {
                        error += error_x;
                        y++;
                        dst += video_buffer->surface->pitch / 4 + step_x;
                    } else {
                        error += error_y;
                        y++;
                        dst += video_buffer->surface->pitch / 4;
                    }

                    switch (style_info.style) {
                    case TIG_LINE_STYLE_SOLID:
                        *dst = color;
                        break;
                    case TIG_LINE_STYLE_DOTTED:
                        if ((pattern & 1) != 0) {
                            *dst = color;
                        }
                        ++pattern;
                        break;
                    case TIG_LINE_STYLE_DASHED:
                        if (pattern < 3) {
                            *dst = color;
                        }
                        ++pattern;
                        if (pattern > 5) {
                            pattern = 0;
                        }
                        break;
                    }
                }
            } else {
                if (line->y1 == line->y2) {
                    step_y = step_x;
                } else {
                    step_y = step_x + video_buffer->surface->pitch / 4;
                }

                x = x1;
                while (x != x2) {
                    x += step_x;
                    if (error > 0) {
                        error += error_x;
                        dst += step_y;
                    } else {
                        error += error_y;
                        dst += step_x;
                    }

                    switch (style_info.style) {
                    case TIG_LINE_STYLE_SOLID:
                        *dst = (uint32_t)color;
                        break;
                    case TIG_LINE_STYLE_DOTTED:
                        if ((pattern & 1) != 0) {
                            *dst = (uint32_t)color;
                        }
                        ++pattern;
                        break;
                    case TIG_LINE_STYLE_DASHED:
                        if (pattern < 3) {
                            *dst = (uint32_t)color;
                        }
                        ++pattern;
                        if (pattern > 5) {
                            pattern = 0;
                        }
                        break;
                    }
                }
            }
        }
        break;
    }

    if (tig_video_buffer_unlock(video_buffer) != TIG_OK) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x520FB0
int sub_520FB0(TigVideoBuffer* video_buffer, unsigned int flags)
{
    TigVideoBufferData data;

    if (flags == 0) {
        return TIG_OK;
    }

    if (!tig_video_3d_initialized) {
        return TIG_ERR_GENERIC;
    }

    if (tig_video_buffer_data(video_buffer, &data) != TIG_OK) {
        return TIG_ERR_GENERIC;
    }

    if ((data.flags & TIG_VIDEO_BUFFER_RENDER_TARGET) == 0) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x521000
int tig_video_buffer_blit(TigVideoBufferBlitInfo* blit_info)
{
    bool stretched;
    float width_ratio;
    float height_ratio;
    TigRect bounds;
    TigRect blit_src_rect;
    TigRect blit_dst_rect;
    TigRect tmp_rect;
    SDL_Rect native_src_rect;
    SDL_Rect native_dst_rect;
    int rc;

    if (blit_info->src_rect->width == blit_info->dst_rect->width
        && blit_info->src_rect->height == blit_info->dst_rect->height) {
        stretched = false;

        // NOTE: Original code does not initialize these values, but we have
        // to keep compiler happy.
        width_ratio = 1;
        height_ratio = 1;
    } else {
        stretched = true;
        width_ratio = (float)blit_info->src_rect->width / (float)blit_info->dst_rect->width;
        height_ratio = (float)blit_info->src_rect->height / (float)blit_info->dst_rect->height;
    }

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = blit_info->src_video_buffer->frame.width;
    bounds.height = blit_info->src_video_buffer->frame.height;

    rc = tig_rect_intersection(blit_info->src_rect,
        &bounds,
        &blit_src_rect);
    if (rc != TIG_OK) {
        return TIG_OK;
    }

    tmp_rect = *blit_info->dst_rect;

    if (stretched) {
        tmp_rect.x += (int)((float)(blit_src_rect.x - blit_info->src_rect->x) / width_ratio);
        tmp_rect.y += (int)((float)(blit_src_rect.y - blit_info->src_rect->y) / height_ratio);
        tmp_rect.width -= (int)((float)(blit_info->src_rect->width - blit_src_rect.width) / width_ratio);
        tmp_rect.height -= (int)((float)(blit_info->src_rect->height - blit_src_rect.height) / height_ratio);
    } else {
        tmp_rect.x += blit_src_rect.x - blit_info->src_rect->x;
        tmp_rect.y += blit_src_rect.y - blit_info->src_rect->y;
        tmp_rect.width -= blit_info->src_rect->width - blit_src_rect.width;
        tmp_rect.height -= blit_info->src_rect->height - blit_src_rect.height;
    }

    bounds.x = 0;
    bounds.y = 0;
    bounds.width = blit_info->dst_video_buffer->frame.width;
    bounds.height = blit_info->dst_video_buffer->frame.height;

    rc = tig_rect_intersection(&tmp_rect,
        &bounds,
        &blit_dst_rect);
    if (rc != TIG_OK) {
        return TIG_OK;
    }

    if (stretched) {
        blit_src_rect.x += (int)((float)(blit_dst_rect.x - tmp_rect.x) / width_ratio);
        blit_src_rect.y += (int)((float)(blit_dst_rect.y - tmp_rect.y) / height_ratio);
        blit_src_rect.width -= (int)((float)(tmp_rect.width - blit_dst_rect.width) / width_ratio);
        blit_src_rect.height -= (int)((float)(tmp_rect.height - blit_dst_rect.height) / height_ratio);
    } else {
        blit_src_rect.x += blit_dst_rect.x - tmp_rect.x;
        blit_src_rect.y += blit_dst_rect.y - tmp_rect.y;
        blit_src_rect.width -= tmp_rect.width - blit_dst_rect.width;
        blit_src_rect.height -= tmp_rect.height - blit_dst_rect.height;
    }

    native_src_rect.x = blit_src_rect.x;
    native_src_rect.y = blit_src_rect.y;
    native_src_rect.w = blit_src_rect.width;
    native_src_rect.h = blit_src_rect.height;

    native_dst_rect.x = blit_dst_rect.x;
    native_dst_rect.y = blit_dst_rect.y;
    native_dst_rect.w = blit_dst_rect.width;
    native_dst_rect.h = blit_dst_rect.height;

    if ((blit_info->flags & TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP) != 0) {
        // 0x5221A2
        // The algorithm is adopted from `TIG_ART_BLT_BLEND_COLOR_LERP` blitter.
        // It is used to blit townmap tiles with partially obscured edges. These
        // tiles are loaded from a bunch of .bmp files, so it bypass art system
        // entirely.
        int tl_r = tig_color_get_red(blit_info->lerp_colors[0]);
        int tl_g = tig_color_get_green(blit_info->lerp_colors[0]);
        int tl_b = tig_color_get_blue(blit_info->lerp_colors[0]);

        int tr_r = tig_color_get_red(blit_info->lerp_colors[1]);
        int tr_g = tig_color_get_green(blit_info->lerp_colors[1]);
        int tr_b = tig_color_get_blue(blit_info->lerp_colors[1]);

        int br_r = tig_color_get_red(blit_info->lerp_colors[2]);
        int br_g = tig_color_get_green(blit_info->lerp_colors[2]);
        int br_b = tig_color_get_blue(blit_info->lerp_colors[2]);

        int bl_r = tig_color_get_red(blit_info->lerp_colors[3]);
        int bl_g = tig_color_get_green(blit_info->lerp_colors[3]);
        int bl_b = tig_color_get_blue(blit_info->lerp_colors[3]);

        float vert_start_step_r = (float)(bl_r - tl_r) / blit_info->lerp_rect->height;
        float vert_start_r = vert_start_step_r * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_r;
        float vert_end_step_r = (float)(br_r - tr_r) / blit_info->lerp_rect->height;
        float vert_end_r = vert_end_step_r * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_r;

        float vert_start_step_g = (float)(bl_g - tl_g) / blit_info->lerp_rect->height;
        float vert_start_g = vert_start_step_g * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_g;
        float vert_end_step_g = (float)(br_g - tr_g) / blit_info->lerp_rect->height;
        float vert_end_g = vert_end_step_g * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_g;

        float vert_start_step_b = (float)(bl_b - tl_b) / blit_info->lerp_rect->height;
        float vert_start_b = vert_start_step_b * (blit_src_rect.y - blit_info->lerp_rect->y) + tl_b;
        float vert_end_step_b = (float)(br_b - tr_b) / blit_info->lerp_rect->height;
        float vert_end_b = vert_end_step_b * (blit_src_rect.y - blit_info->lerp_rect->y) + tr_b;

        int x;
        int y;

        uint32_t* src = (uint32_t*)((uint8_t*)blit_info->src_video_buffer->surface->pixels
            + blit_info->src_video_buffer->surface->pitch * blit_src_rect.y
            + 4 * blit_src_rect.x);
        uint32_t* dst = (uint32_t*)((uint8_t*)blit_info->dst_video_buffer->surface->pixels
            + blit_info->dst_video_buffer->surface->pitch * blit_dst_rect.y
            + 4 * blit_dst_rect.x);

        for (y = 0; y < blit_dst_rect.height; ++y) {
            float hor_step_r = (vert_end_r - vert_start_r) / blit_info->lerp_rect->width;
            float hor_step_g = (vert_end_g - vert_start_g) / blit_info->lerp_rect->width;
            float hor_step_b = (vert_end_b - vert_start_b) / blit_info->lerp_rect->width;

            float r = vert_start_r + hor_step_r * (blit_src_rect.x - blit_info->lerp_rect->x);
            float g = vert_start_g + hor_step_g * (blit_src_rect.x - blit_info->lerp_rect->x);
            float b = vert_start_b + hor_step_b * (blit_src_rect.x - blit_info->lerp_rect->x);

            for (x = 0; x < blit_dst_rect.width; ++x) {
                if ((blit_info->src_video_buffer->flags & TIG_VIDEO_BUFFER_COLOR_KEY) == 0
                    || *src != blit_info->src_video_buffer->color_key) {
                    *dst = tig_color_mul(*src, tig_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b));
                }

                r += hor_step_r;
                g += hor_step_g;
                b += hor_step_b;

                src++;
                dst++;
            }

            vert_start_r += vert_start_step_r;
            vert_end_r += vert_end_step_r;

            vert_start_g += vert_start_step_g;
            vert_end_g += vert_end_step_g;

            vert_start_b += vert_start_step_b;
            vert_end_b += vert_end_step_b;

            src = (uint32_t*)((uint8_t*)src + blit_info->src_video_buffer->surface->pitch - 4 * blit_src_rect.width);
            dst = (uint32_t*)((uint8_t*)dst + blit_info->dst_video_buffer->surface->pitch - 4 * blit_dst_rect.width);
        }

        return TIG_OK;
    }

    // CE: BLEND_ALPHA_CONST — drive SDL's surface-level alpha blend
    // mode + alpha mod on the source surface around the blit, then
    // restore. Used by tig_window_copy_from_vbuffer_alpha (tb.c's
    // speech bubble fades). alpha[0] is the constant byte applied
    // to all pixels. Color-keyed sources still mask transparent
    // pixels correctly because SDL handles the key + alpha
    // composition in BLEND mode.
    bool alpha_const = (blit_info->flags & TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST) != 0;
    SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
    uint8_t prev_alpha = 255;
    if (alpha_const) {
        SDL_GetSurfaceBlendMode(blit_info->src_video_buffer->surface, &prev_blend);
        SDL_GetSurfaceAlphaMod(blit_info->src_video_buffer->surface, &prev_alpha);
        SDL_SetSurfaceBlendMode(blit_info->src_video_buffer->surface, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(blit_info->src_video_buffer->surface, blit_info->alpha[0]);
    }

    // CE: BLEND_ADD — additive blit modulated by lerp_colors[0]. Used for the
    // composite-sprite highlight pass (hover/Opt), which brightens the sprite
    // by adding a fraction of itself on top. SDL's color-keyed transparent
    // pixels are still skipped in ADD blend mode, so the magenta key doesn't
    // bleed in. Mirrors the alpha_const save/restore dance.
    bool color_add = (blit_info->flags & TIG_VIDEO_BUFFER_BLIT_BLEND_ADD) != 0;
    SDL_BlendMode prev_add_blend = SDL_BLENDMODE_NONE;
    uint8_t prev_mod_r = 255;
    uint8_t prev_mod_g = 255;
    uint8_t prev_mod_b = 255;
    if (color_add) {
        SDL_GetSurfaceBlendMode(blit_info->src_video_buffer->surface, &prev_add_blend);
        SDL_GetSurfaceColorMod(blit_info->src_video_buffer->surface, &prev_mod_r, &prev_mod_g, &prev_mod_b);
        SDL_SetSurfaceBlendMode(blit_info->src_video_buffer->surface, SDL_BLENDMODE_ADD);
        SDL_SetSurfaceColorMod(blit_info->src_video_buffer->surface,
            (uint8_t)tig_color_get_red(blit_info->lerp_colors[0]),
            (uint8_t)tig_color_get_green(blit_info->lerp_colors[0]),
            (uint8_t)tig_color_get_blue(blit_info->lerp_colors[0]));
    }

    // CE: FLIP_X / FLIP_Y were never honored by this blitter (only the art-id
    // path flipped). The ce_sprite compositor relies on vbuffer→vbuffer flips
    // for its "*fH" components, so support them here: extract the source region
    // into a temp surface, mirror it with SDL_FlipSurface, then blit that. Only
    // the non-stretched path is needed by current callers.
    SDL_Surface* flip_src = NULL;
    SDL_Surface* eff_src = blit_info->src_video_buffer->surface;
    SDL_Rect eff_src_rect = native_src_rect;
    SDL_FlipMode flip_mode = SDL_FLIP_NONE;
    if ((blit_info->flags & TIG_VIDEO_BUFFER_BLIT_FLIP_X) != 0) {
        flip_mode |= SDL_FLIP_HORIZONTAL;
    }
    if ((blit_info->flags & TIG_VIDEO_BUFFER_BLIT_FLIP_Y) != 0) {
        flip_mode |= SDL_FLIP_VERTICAL;
    }
    if (flip_mode != SDL_FLIP_NONE) {
        SDL_Surface* s = blit_info->src_video_buffer->surface;
        flip_src = SDL_CreateSurface(native_src_rect.w, native_src_rect.h,
            s->format);
        if (flip_src != NULL) {
            SDL_Rect whole = { 0, 0, native_src_rect.w, native_src_rect.h };
            // Preserve color-key transparency through the copy. The source's
            // color-keyed (transparent) pixels are SKIPPED by SDL_BlitSurface,
            // so they'd land on flip_src's fresh-allocation default (black) and
            // then read as opaque black. Pre-fill flip_src with the key colour
            // and carry the key forward so those areas stay transparent.
            bool had_key = SDL_SurfaceHasColorKey(s);
            uint32_t key = 0;
            if (had_key) {
                SDL_GetSurfaceColorKey(s, &key);
                SDL_FillSurfaceRect(flip_src, NULL, key);
                SDL_SetSurfaceColorKey(flip_src, true, key);
            }
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(s, &native_src_rect, flip_src, &whole);
            SDL_FlipSurface(flip_src, flip_mode);
            eff_src = flip_src;
            eff_src_rect = whole;
        }
    }

    bool ok = true;
    if (stretched) {
        SDL_ScaleMode scale_mode = (blit_info->flags & TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR)
            ? SDL_SCALEMODE_LINEAR
            : SDL_SCALEMODE_NEAREST;

        if (!SDL_BlitSurfaceScaled(eff_src, &eff_src_rect, blit_info->dst_video_buffer->surface, &native_dst_rect, scale_mode)) {
            ok = false;
        }
    } else {
        if (!SDL_BlitSurface(eff_src, &eff_src_rect, blit_info->dst_video_buffer->surface, &native_dst_rect)) {
            ok = false;
        }
    }

    if (flip_src != NULL) {
        SDL_DestroySurface(flip_src);
    }

    if (alpha_const) {
        SDL_SetSurfaceBlendMode(blit_info->src_video_buffer->surface, prev_blend);
        SDL_SetSurfaceAlphaMod(blit_info->src_video_buffer->surface, prev_alpha);
    }

    if (color_add) {
        SDL_SetSurfaceBlendMode(blit_info->src_video_buffer->surface, prev_add_blend);
        SDL_SetSurfaceColorMod(blit_info->src_video_buffer->surface, prev_mod_r, prev_mod_g, prev_mod_b);
    }

    if (!ok) {
        return TIG_ERR_GENERIC;
    }

    return TIG_OK;
}

// 0x522F30
int tig_video_buffer_get_pixel_color(TigVideoBuffer* video_buffer, int x, int y, unsigned int* color)
{
    int index;
    int rc;

    if (x < video_buffer->frame.x
        || y < video_buffer->frame.y
        || x >= video_buffer->frame.x + video_buffer->frame.width
        || y >= video_buffer->frame.y + video_buffer->frame.height) {
        return TIG_ERR_INVALID_PARAM;
    }

    rc = tig_video_buffer_lock(video_buffer);
    if (rc != TIG_OK) {
        return rc;
    }

    switch (tig_video_bpp) {
    case 32:
        index = y * video_buffer->surface->pitch + 4 * x;
        *color = *(uint32_t*)((uint8_t*)video_buffer->surface->pixels + index);
        break;
    }

    tig_video_buffer_unlock(video_buffer);

    return TIG_OK;
}

// 0x523120
int tig_video_buffer_tint(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t tint_color, TigVideoBufferTintMode mode)
{
    int rc;
    TigRect frame;
    int x;
    int y;

    if (mode >= TIG_VIDEO_BUFFER_TINT_MODE_COUNT) {
        return TIG_ERR_INVALID_PARAM;
    }

    if (tint_color == tig_color_make(0, 0, 0)
        && mode != TIG_VIDEO_BUFFER_TINT_MODE_GRAYSCALE) {
        return TIG_OK;
    }

    rc = tig_rect_intersection(rect, &(video_buffer->frame), &frame);
    if (rc != TIG_OK) {
        return rc;
    }

    rc = tig_video_buffer_lock(video_buffer);
    if (rc != TIG_OK) {
        return rc;
    }

    for (y = 0; y < frame.height; ++y) {
        switch (tig_video_bpp) {
        case 32:
            if (1) {
                uint32_t* dst = (uint32_t*)video_buffer->surface->pixels + (video_buffer->surface->pitch / 4) * (y + frame.y) + frame.x;
                uint32_t src_color;

                switch (mode) {
                case TIG_VIDEO_BUFFER_TINT_MODE_ADD:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_add(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_SUB:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_sub(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_MUL:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_mul(tint_color, src_color);
                    }
                    break;
                case TIG_VIDEO_BUFFER_TINT_MODE_GRAYSCALE:
                    for (x = 0; x < frame.width; ++x) {
                        src_color = *dst;
                        *dst++ = tig_color_rgb_to_grayscale(src_color);
                    }
                    break;
                default:
                    // Should be unreachable.
                    abort();
                }
            }
            break;
        }
    }

    tig_video_buffer_unlock(video_buffer);
    return TIG_OK;
}

// CE: one-shot bake — replace near-black pixels in `rect` with the VB's
// color_key so the standard SDL color-key blit treats them as
// transparent. Threshold is per-channel: a pixel qualifies when R, G,
// AND B are all ≤ threshold. NEON path processes 4 pixels per iter;
// scalar fallback for non-ARM targets and the trailing < 4 pixels.
int tig_video_buffer_replace_near_black_with_color_key(TigVideoBuffer* video_buffer,
    TigRect* rect,
    uint8_t threshold)
{
    TigRect frame;
    int rc;
    if (video_buffer == NULL || video_buffer->surface == NULL) {
        return TIG_ERR_INVALID_PARAM;
    }
    rc = tig_rect_intersection(rect, &(video_buffer->frame), &frame);
    if (rc != TIG_OK) {
        return rc;
    }
    rc = tig_video_buffer_lock(video_buffer);
    if (rc != TIG_OK) {
        return rc;
    }
    uint32_t key = video_buffer->color_key;
    int pitch_px = video_buffer->surface->pitch / 4;
    for (int y = 0; y < frame.height; y++) {
        uint32_t* dst = (uint32_t*)video_buffer->surface->pixels
            + pitch_px * (y + frame.y)
            + frame.x;
        int x = 0;
#if TIG_HAVE_NEON
        if (frame.width >= 4) {
            uint8x16_t thresh_v = vdupq_n_u8(threshold);
            uint8x16_t alpha_clear = vreinterpretq_u8_u32(vdupq_n_u32(0x00FFFFFFu));
            uint32x4_t allones = vdupq_n_u32(0xFFFFFFFFu);
            uint32x4_t key_v = vdupq_n_u32(key);
            for (; x + 4 <= frame.width; x += 4) {
                uint32x4_t s4 = vld1q_u32(dst + x);
                uint8x16_t s_rgb = vandq_u8(vreinterpretq_u8_u32(s4), alpha_clear);
                uint8x16_t le = vcleq_u8(s_rgb, thresh_v);
                uint32x4_t le_u32 = vreinterpretq_u32_u8(le);
                uint32x4_t nb_mask = vceqq_u32(le_u32, allones);
                uint32x4_t out = vbslq_u32(nb_mask, key_v, s4);
                vst1q_u32(dst + x, out);
            }
        }
#endif
        for (; x < frame.width; x++) {
            uint32_t s = dst[x];
            if (((s >> 16) & 0xFF) <= threshold
                && ((s >> 8) & 0xFF) <= threshold
                && (s & 0xFF) <= threshold) {
                dst[x] = key;
            }
        }
    }
    tig_video_buffer_unlock(video_buffer);
    return TIG_OK;
}

// 0x523930
int tig_video_buffer_save_to_bmp(TigVideoBufferSaveToBmpInfo* save_info)
{
    int rc;
    TigRect rect;

    if (save_info->rect != NULL) {
        rect = *save_info->rect;
    } else {
        rect.x = 0;
        rect.y = 0;
        rect.width = save_info->video_buffer->surface->w;
        rect.height = save_info->video_buffer->surface->h;
    }

    rc = tig_video_buffer_data_to_bmp(save_info->video_buffer->surface,
        &rect,
        save_info->path);

    return rc;
}

// 0x5239D0
int tig_video_buffer_load_from_bmp(const char* filename, TigVideoBuffer** video_buffer_ptr, unsigned int flags)
{
    SDL_IOStream* io;
    SDL_Surface* surface;
    SDL_Rect blit_rect;
    TigVideoBufferCreateInfo vb_create_info;
    int rc;

    io = tig_file_io_open(filename, "rb");
    if (io == NULL) {
        return TIG_ERR_IO;
    }

    surface = SDL_LoadBMP_IO(io, true);
    if (surface == NULL) {
        return TIG_ERR_IO;
    }

    if ((flags & 0x1) != 0) {
        vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
        vb_create_info.width = surface->w;
        vb_create_info.height = surface->h;
        vb_create_info.color_key = 0;
        vb_create_info.background_color = 0;

        rc = tig_video_buffer_create(&vb_create_info, video_buffer_ptr);
        if (rc != TIG_OK) {
            SDL_DestroySurface(surface);

            return rc;
        }
    }

    if ((*video_buffer_ptr)->surface->w < surface->w || (*video_buffer_ptr)->surface->h < surface->h) {
        if ((flags & 0x1) != 0) {
            tig_video_buffer_destroy(*video_buffer_ptr);
        }

        SDL_DestroySurface(surface);

        return TIG_ERR_INVALID_PARAM;
    }

    blit_rect.x = 0;
    blit_rect.y = 0;
    blit_rect.w = surface->w;
    blit_rect.h = surface->h;

    if (!SDL_BlitSurface(surface, &blit_rect, (*video_buffer_ptr)->surface, &blit_rect)) {
        if ((flags & 0x1) != 0) {
            tig_video_buffer_destroy(*video_buffer_ptr);
        }

        SDL_DestroySurface(surface);

        return TIG_ERR_GENERIC;
    }

    SDL_DestroySurface(surface);

    return TIG_OK;
}

// 0x524080
bool tig_video_window_create(TigInitInfo* init_info)
{
    const char* name = (init_info->flags & TIG_INITIALIZE_SET_WINDOW_NAME) != 0
        ? init_info->window_name
        : "TIG";

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if ((init_info->flags & TIG_INITIALIZE_WINDOWED) == 0) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    int window_width = (int)(init_info->width * scale);
    int window_height = (int)(init_info->height * scale);

#if SDL_PLATFORM_ANDROID || SDL_PLATFORM_IOS
    SDL_Rect display_bounds;
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &display_bounds);

    // Calculate the view size in logical pixels. Android reports display bounds
    // in physical pixels and provides the appropriate scale, while in iOS
    // display bounds are already in logical pixels with a scale of 1.0.
    init_info->width = (int)((float)display_bounds.w / scale + 0.5f);
    init_info->height = (int)((float)display_bounds.h / scale + 0.5f);

    const int min_height = 600;
    if (init_info->height < min_height) {
        // The logical resolution is too small to accommodate the base graphic.
        // This likely means we're on a mobile phone, where the height is
        // usually between 350 and 450 logical pixels.
        float width_scale = (float)min_height / (float)init_info->height;
        init_info->width = (int)((float)init_info->width * width_scale + 0.5f);
        init_info->height = min_height;
    }

    window_width = display_bounds.w;
    window_height = display_bounds.h;
    flags |= SDL_WINDOW_FULLSCREEN;
#endif

// We're using streaming texture which is extremely slow in Metal.
#if SDL_PLATFORM_MACOS
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl,metal");
#elif SDL_PLATFORM_IOS
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2,metal");
#endif

#if SDL_PLATFORM_MACOS
    // On macOS, by default we let the window cover the full display, including
    // the area under the camera notch. This avoids the window being clipped
    // below the menu bar / notch on notched MacBooks. When the user clears
    // `ignore notch`, the window is constrained to the display's usable bounds.
    //
    // We deliberately bypass SDL_WINDOW_FULLSCREEN here (which on macOS goes
    // through Cocoa "Spaces" fullscreen via toggleFullScreen:). Spaces
    // fullscreen still honors the system safe area on some macOS versions
    // even with `NSPrefersDisplaySafeAreaCompatibilityMode` set in Info.plist
    // (e.g. when a previous app bundle had the Get Info "Scale to fit below
    // built-in camera" checkbox toggled). Instead, we manually create a
    // borderless window sized to the full display and raise its NSWindow
    // level above the menu bar so it genuinely covers the panel edge-to-edge.
    bool macos_ignore_notch = (init_info->flags & TIG_INITIALIZE_IGNORE_NOTCH) != 0;
    SDL_Rect macos_display_bounds;
    if (macos_ignore_notch) {
        SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &macos_display_bounds);
    } else {
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &macos_display_bounds);
    }

    bool macos_cover_full_display = macos_ignore_notch;
    if (macos_cover_full_display) {
        // Drop SDL's Spaces-fullscreen path and use a borderless window we
        // can position and level-raise ourselves.
        flags &= ~SDL_WINDOW_FULLSCREEN;
        flags |= SDL_WINDOW_BORDERLESS;
        window_width = macos_display_bounds.w;
        window_height = macos_display_bounds.h;
    } else if ((init_info->flags & TIG_INITIALIZE_WINDOWED) != 0) {
        // Clamp the requested windowed size to the available area so macOS
        // does not silently shrink the window below the menu bar / notch.
        if (window_width > macos_display_bounds.w) {
            window_width = macos_display_bounds.w;
        }
        if (window_height > macos_display_bounds.h) {
            window_height = macos_display_bounds.h;
        }
    }
#endif

    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!SDL_CreateWindowAndRenderer(name, window_width, window_height, flags, &window, &renderer)) {
        return false;
    }

#if SDL_PLATFORM_MACOS
    if (macos_cover_full_display) {
        // Anchor the window to the top-left of the display so the borderless
        // window actually covers the notch / menu bar area.
        SDL_SetWindowPosition(window, macos_display_bounds.x, macos_display_bounds.y);
        SDL_SetWindowSize(window, macos_display_bounds.w, macos_display_bounds.h);

        tig_video_macos_cover_full_display = true;
        tig_video_macos_apply_chrome(window);
    }
#endif

    SDL_PropertiesID renderer_props = SDL_GetRendererProperties(renderer);
    const char* driver_name = SDL_GetStringProperty(renderer_props, SDL_PROP_RENDERER_NAME_STRING, "");
    tig_debug_printf("TIG Video: Using '%s' driver\n", driver_name);

    if ((init_info->flags & TIG_INITIALIZE_POSITIONED) != 0) {
        SDL_SetWindowPosition(window, init_info->x, init_info->y);
    }

    if (!SDL_SetRenderVSync(renderer, 1)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    if (!SDL_SetRenderLogicalPresentation(renderer, init_info->width, init_info->height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, init_info->width, init_info->height);
    if (texture == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    SDL_PropertiesID texture_props = SDL_GetTextureProperties(texture);
    SDL_PixelFormat format = (SDL_PixelFormat)SDL_GetNumberProperty(texture_props, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0);

    SDL_Surface* surface = SDL_CreateSurface(init_info->width, init_info->height, format);
    if (surface == NULL) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return false;
    }

    tig_video_state.window = window;
    tig_video_state.renderer = renderer;
    tig_video_state.texture = texture;
    tig_video_state.surface = surface;

    stru_610388.x = 0;
    stru_610388.y = 0;
    stru_610388.width = init_info->width;
    stru_610388.height = init_info->height;

    return true;
}

// 0x5242F0
void tig_video_window_destroy(void)
{
    if (tig_video_state.surface != NULL) {
        SDL_DestroySurface(tig_video_state.surface);
        tig_video_state.surface = NULL;
    }

    if (tig_video_state.texture != NULL) {
        SDL_DestroyTexture(tig_video_state.texture);
        tig_video_state.texture = NULL;
    }

    if (tig_video_state.renderer != NULL) {
        SDL_DestroyRenderer(tig_video_state.renderer);
        tig_video_state.renderer = NULL;
    }

    if (tig_video_state.window != NULL) {
        SDL_DestroyWindow(tig_video_state.window);
        tig_video_state.window = NULL;
    }
}

// 0x524830
bool sub_524830(void)
{
    int bpp;
    Uint32 r;
    Uint32 g;
    Uint32 b;
    Uint32 a;

    if (!SDL_GetMasksForPixelFormat(tig_video_state.surface->format, &bpp, &r, &g, &b, &a)) {
        return false;
    }

    if (tig_color_set_rgba_settings(r, g, b, a) != TIG_OK) {
        return false;
    }

    return true;
}

// 0x525250
int tig_video_screenshot_make_internal(int key)
{
    int rc;
    int index;
    char path[TIG_MAX_PATH];
    TigRect rect;

    if (tig_video_screenshot_key != key) {
        return TIG_ERR_GENERIC;
    }

    for (index = 0; index < INT_MAX; index++) {
        SDL_snprintf(path, sizeof(path), "screen%04d.bmp", index);
        if (!tig_file_exists(path, NULL)) {
            break;
        }
    }

    if (index == INT_MAX) {
        return TIG_ERR_IO;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = tig_video_state.surface->w;
    rect.height = tig_video_state.surface->h;

    rc = tig_video_buffer_data_to_bmp(tig_video_state.surface, &rect, path);

    return rc;
}

// 0x525ED0
int tig_video_buffer_data_to_bmp(SDL_Surface* surface, TigRect* rect, const char* file_name)
{
    SDL_Surface* intermediate_surface;
    SDL_IOStream* io;
    int rc;

    io = tig_file_io_open(file_name, "wb");
    if (io == NULL) {
        return TIG_ERR_IO;
    }

    if (rect->x == 0
        && rect->y == 0
        && rect->width == surface->w
        && rect->height == surface->h) {
        intermediate_surface = surface;
    } else {
        SDL_Rect blit_rect;

        intermediate_surface = SDL_CreateSurface(rect->width, rect->height, surface->format);
        if (intermediate_surface == NULL) {
            SDL_CloseIO(io);
            return TIG_ERR_GENERIC;
        }

        blit_rect.x = rect->x;
        blit_rect.y = rect->y;
        blit_rect.w = rect->width;
        blit_rect.h = rect->height;

        if (!SDL_BlitSurface(surface, &blit_rect, intermediate_surface, &blit_rect)) {
            SDL_DestroySurface(intermediate_surface);
            SDL_CloseIO(io);
            return TIG_ERR_GENERIC;
        }
    }

    rc = TIG_OK;
    if (!SDL_SaveBMP_IO(intermediate_surface, io, true)) {
        rc = TIG_ERR_IO;
    }

    if (intermediate_surface != surface) {
        SDL_DestroySurface(intermediate_surface);
    }

    return rc;
}
