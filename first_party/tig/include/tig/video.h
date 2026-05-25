#ifndef TIG_VIDEO_H_
#define TIG_VIDEO_H_

#include "tig/color.h"
#include "tig/rect.h"
#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned TigFadeFlags;

#define TIG_FADE_OUT 0x0u
#define TIG_FADE_IN 0x1u

typedef unsigned int TigVideoBufferCreateFlags;

#define TIG_VIDEO_BUFFER_CREATE_COLOR_KEY 0x0001
#define TIG_VIDEO_BUFFER_CREATE_VIDEO_MEMORY 0x0002
#define TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY 0x0004
#define TIG_VIDEO_BUFFER_CREATE_RENDER_TARGET 0x0008
#define TIG_VIDEO_BUFFER_CREATE_TEXTURE 0x0010

typedef unsigned int TigVideoBufferFlags;

#define TIG_VIDEO_BUFFER_LOCKED 0x0001
#define TIG_VIDEO_BUFFER_COLOR_KEY 0x0002
#define TIG_VIDEO_BUFFER_VIDEO_MEMORY 0x0004
#define TIG_VIDEO_BUFFER_SYSTEM_MEMORY 0x0008
#define TIG_VIDEO_BUFFER_RENDER_TARGET 0x0010
#define TIG_VIDEO_BUFFER_TEXTURE 0x0020

typedef unsigned int TigVideoBufferBlitFlags;

#define TIG_VIDEO_BUFFER_BLIT_FLIP_X 0x0001
#define TIG_VIDEO_BUFFER_BLIT_FLIP_Y 0x0002
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ADD 0x0004
#define TIG_VIDEO_BUFFER_BLIT_BLEND_MUL 0x0010
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_AVG 0x0020
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST 0x0040
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_SRC 0x0080
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_LERP 0x0100
#define TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST 0x0200
#define TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP 0x0400

// CE: Special case - use linear filtering instead of nearest pixel sampling.
// This is only required when creating save-game thumbnails. Default nearest
// pixel sampling produces ugly blank lines when a PC is inside a structure
// (which makes walls semitransparent).
#define TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR 0x80000000u

#define TIG_VIDEO_BUFFER_BLIT_FLIP_ANY (TIG_VIDEO_BUFFER_BLIT_FLIP_X | TIG_VIDEO_BUFFER_BLIT_FLIP_Y)

// Opaque handle.
typedef struct TigVideoBuffer TigVideoBuffer;

typedef struct TigVideoBufferCreateInfo {
    /* 0000 */ TigVideoBufferCreateFlags flags;
    /* 0004 */ int width;
    /* 0008 */ int height;
    /* 000C */ unsigned int background_color;
    /* 0010 */ unsigned int color_key;
} TigVideoBufferCreateInfo;

typedef struct TigVideoBufferData {
    /* 0000 */ TigVideoBufferFlags flags;
    /* 0004 */ int width;
    /* 0008 */ int height;
    /* 000C */ int pitch;
    /* 0010 */ int background_color;
    /* 0014 */ int color_key;
    /* 0018 */ int bpp;
    /* 001C */ void* pixels;
} TigVideoBufferData;

typedef struct TigVideoBufferBlitInfo {
    /* 0000 */ TigVideoBufferBlitFlags flags;
    /* 0004 */ TigVideoBuffer* src_video_buffer;
    /* 0008 */ TigRect* src_rect;
    /* 000C */ uint8_t alpha[4];
    /* 0010 */ tig_color_t lerp_colors[4];
    /* 0020 */ TigRect* lerp_rect;
    /* 0024 */ TigVideoBuffer* dst_video_buffer;
    /* 0028 */ TigRect* dst_rect;
} TigVideoBufferBlitInfo;

typedef struct TigVideoScreenshotSettings {
    /* 0000 */ int key;
    /* 0004 */ int field_4;
} TigVideoScreenshotSettings;

typedef enum TigVideoBufferTintMode {
    TIG_VIDEO_BUFFER_TINT_MODE_ADD,
    TIG_VIDEO_BUFFER_TINT_MODE_SUB,
    TIG_VIDEO_BUFFER_TINT_MODE_MUL,
    TIG_VIDEO_BUFFER_TINT_MODE_GRAYSCALE,
    TIG_VIDEO_BUFFER_TINT_MODE_COUNT,
} TigVideoBufferTintMode;

typedef struct TigVideoBufferSaveToBmpInfo {
    /* 0000 */ unsigned int flags;
    /* 0004 */ TigVideoBuffer* video_buffer;
    /* 0008 */ char path[TIG_MAX_PATH];
    /* 010C */ TigRect* rect;
} TigVideoBufferSaveToBmpInfo;

int tig_video_init(TigInitInfo* init_info);
void tig_video_exit(void);
int tig_video_window_get(SDL_Window** window_ptr);
int tig_video_renderer_get(SDL_Renderer** renderer_ptr);
void tig_video_display_fps(void);
int tig_video_blit(TigVideoBuffer* src_video_buffer, TigRect* src_rect, TigRect* dst_rect);

// CE: blit a window VB to the screen with optional scale (dst.w/h
// differ from src.w/h triggers SDL_BlitSurfaceScaled at nearest-
// neighbor) AND optional constant alpha (alpha < 255 enables
// SDL_BLENDMODE_BLEND with alpha-mod). Used by the tig compositor's
// per-window transform path that the ui_anim spring tween drives for
// entrance/exit animations. Pass alpha=255 + src.w/h == dst.w/h for
// equivalent of plain tig_video_blit (still cheap to call).
int tig_video_blit_scaled_alpha(TigVideoBuffer* src_video_buffer,
    TigRect* src_rect,
    TigRect* dst_rect,
    uint8_t alpha);

// CE: integrated scale + near-black-tint + alpha blit. Used by the
// tig compositor when a transformed window also has the translucent-
// black tint enabled (e.g. inventory mid-entrance). Doing all three
// operations in one pass gives the entrance the same "see-through
// tinted" look the panel has at rest — without this integrated path,
// the entrance would render the near-black panel areas as solid
// (because the scaled+alpha SDL path can't read the underlay) and
// only snap to the tinted appearance when the transform clears.
//
// Per-pixel scalar — heavier than the plain scaled+alpha (no NEON
// in this path yet, the dst pixel count is bounded by typical UI
// window sizes, and the path only runs during the ~200ms entrance/
// exit windows). See F9 perf log's tint-blit line for measured cost.
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
    uint8_t alpha);

// CE: composite blit that replaces near-black source pixels with
// MUL-darkened underlay pixels at the same screen position. Used by
// the HUD bar's translucent-black pathway: the bar's panel art has
// dark regions the user wants the world to peek through (with the
// world darkened so chrome contrast stays readable). Reads the
// underlay VB directly rather than relying on the screen surface
// being painted by lower windows first — the iso world uses
// VIDEO_MEMORY and its SDL_Surface isn't a reliable source of the
// live world content during composite.
//
// `tint_r/g/b` are per-channel "darken by N out of 255" values: 0
// preserves the channel, 255 zeroes it. Math is a per-channel
// multiply (output = underlay * (255 - tint) / 256), which keeps
// the underlay's hue. A saturating-subtract version was tried first
// but visibly burned colors (channels clipped to 0 independently,
// shifting hue toward whichever channel was strongest).
//
// `reveal` (0..255) blends the result between the original source
// pixel (reveal=0 — near-black areas stay opaque, the tint pathway
// does nothing) and the fully-tinted underlay (reveal=255 — original
// behavior). Used by ui_anim to fade the see-through effect IN after
// a window's scale+alpha entrance settles, so the panel doesn't snap
// from "opaque" to "tinted see-through" — the see-through reveals
// gradually. reveal=255 is the runtime default for static UIs.
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
    uint8_t reveal);
int tig_video_fill(const TigRect* rect, tig_color_t color);
int tig_video_flip(void);
// Hint the next tig_video_flip to upload only `rect` of the surface to the GPU
// texture instead of the whole surface. Cleared after the next flip. NULL or
// an empty rect means "upload everything" (the default). Callers that touch
// only a portion of the surface during a present cycle (window compositor +
// mouse cursor) should call this with the union of their writes to skip the
// CPU→GPU bandwidth of a full-surface SDL_UpdateTexture (~8MB at 1080p32).
void tig_video_set_present_dirty_rect(const TigRect* rect);

// Internal-breakdown timing for tig_video_flip. Used by the gamelib zoom-perf
// log to attribute the ~7ms-per-frame cost between SDL_UpdateTexture
// (CPU→GPU upload) and SDL_RenderPresent (typically vsync wait). Counters
// are only accumulated while collection is enabled; the gamelib F9 toggle
// drives that flag.
typedef struct {
    uint64_t update_total_ns;
    uint64_t update_max_ns;
    uint64_t present_total_ns;
    uint64_t present_max_ns;
    int samples;
    int partial_samples;
} TigVideoFlipPerf;
void tig_video_flip_perf_set_enabled(bool enabled);
void tig_video_flip_perf_get(TigVideoFlipPerf* out);
void tig_video_flip_perf_reset(void);

// CE: per-call timing for the translucent-black tint composite blit
// (tig_video_blit_near_black_tinted). Lets us quantify the tint
// pathway's CPU cost so it can be compared against the alpha-blend
// variant in the sibling branch (same struct layout exists there
// for tig_video_blit_near_black_alpha). Driven by the same F9 perf
// toggle as the flip-perf counters so collection turns on/off
// together.
//
// pixels_total counts the destination pixel area covered by each
// call (clamped rect width × height) — divide by samples for the
// average pixels-per-blit, multiply by samples for total pixels
// touched in the window.
typedef struct {
    uint64_t total_ns;
    uint64_t max_ns;
    int samples;
    uint64_t pixels_total;
} TigVideoTintBlitPerf;
void tig_video_tint_blit_perf_set_enabled(bool enabled);
void tig_video_tint_blit_perf_get(TigVideoTintBlitPerf* out);
void tig_video_tint_blit_perf_reset(void);

// Reapply the renderer's vsync mode. Values match SDL_SetRenderVSync:
//   1 = vsync on (default at init), 0 = vsync off, -1 = adaptive vsync
//   (SDL_RENDERER_VSYNC_ADAPTIVE). Returns TIG_OK on success.
int tig_video_set_vsync_mode(int mode);
int tig_video_screenshot_set_settings(TigVideoScreenshotSettings* settings);
int tig_video_screenshot_make(void);
int tig_video_get_palette(unsigned int* colors);
int tig_video_3d_check_initialized(void);
int tig_video_3d_check_hardware(void);
int tig_video_3d_begin_scene(void);
int tig_video_3d_end_scene(void);
int tig_video_check_gamma_control(void);
int tig_video_fade(tig_color_t color, int steps, float duration, TigFadeFlags flags);
int tig_video_set_gamma(float gamma);
int tig_video_buffer_create(TigVideoBufferCreateInfo* vb_create_info, TigVideoBuffer** video_buffer);
int tig_video_buffer_destroy(TigVideoBuffer* video_buffer);
int tig_video_buffer_data(TigVideoBuffer* video_buffer, TigVideoBufferData* video_buffer_data);
int tig_video_buffer_set_color_key(TigVideoBuffer* video_buffer, int color_key);
int tig_video_buffer_lock(TigVideoBuffer* video_buffer);
int tig_video_buffer_unlock(TigVideoBuffer* video_buffer);
int tig_video_buffer_outline(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color);
int tig_video_buffer_fill(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t color);
int tig_video_buffer_line(TigVideoBuffer* video_buffer, TigLine* line, TigRect* a3, tig_color_t color);
int sub_520FB0(TigVideoBuffer* video_buffer, unsigned int flags);
int tig_video_buffer_blit(TigVideoBufferBlitInfo* blit_info);
int tig_video_buffer_get_pixel_color(TigVideoBuffer* video_buffer, int x, int y, unsigned int* color);
int tig_video_buffer_tint(TigVideoBuffer* video_buffer, TigRect* rect, tig_color_t tint_color, TigVideoBufferTintMode mode);

// CE: scan the rect for pixels whose R, G, B are all ≤ threshold
// ("near-black") and overwrite them with the VB's color_key so the
// next SDL blit treats those pixels as transparent. Used by the
// translucent-black-UI tint pathway to bake panel art's dark
// regions to color-key transparency at window creation, after which
// the standard color-key blit shows whatever's beneath through the
// holes. NEON-vectorized fast path on Apple Silicon.
int tig_video_buffer_replace_near_black_with_color_key(TigVideoBuffer* video_buffer,
    TigRect* rect,
    uint8_t threshold);
int tig_video_buffer_save_to_bmp(TigVideoBufferSaveToBmpInfo* save_info);
int tig_video_buffer_load_from_bmp(const char* filename, TigVideoBuffer** video_buffer_ptr, unsigned int flags);

#if defined(SDL_PLATFORM_MACOS) && SDL_PLATFORM_MACOS
// Re-apply the macOS borderless-full-display window chrome (NSWindow level
// above the menu bar, hide dock + menu bar). No-op unless the window was
// created with TIG_INITIALIZE_IGNORE_NOTCH. Call on focus regain -- macOS
// resets these on app deactivation, otherwise the menu bar and a window
// title bar slide down at the top of the screen.
void tig_video_macos_reapply_chrome(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TIG_VIDEO_H_ */
