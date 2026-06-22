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
// CE: Create a GPU-backed buffer (SDL_Texture with TEXTUREACCESS_TARGET)
// instead of an SDL_Surface. Mutually exclusive with the surface ops:
// lock/unlock, fill, line, blit, get_pixel_color, tint, set_color_key,
// save_to_bmp, load_from_bmp all return an error and log a warning when
// called on a GPU buffer. The flag is the entry point for the GPU render
// path (feature/perf-gpu-accel) -- intended for the world render target
// in tile_draw_iso etc., where pixels never need to round-trip through
// the CPU. COLOR_KEY is silently ignored on GPU buffers (SDL_Texture has
// no color-key equivalent; callers use SDL_BLENDMODE_BLEND instead).
#define TIG_VIDEO_BUFFER_CREATE_TEXTURE 0x0010

typedef unsigned int TigVideoBufferFlags;

#define TIG_VIDEO_BUFFER_LOCKED 0x0001
#define TIG_VIDEO_BUFFER_COLOR_KEY 0x0002
#define TIG_VIDEO_BUFFER_VIDEO_MEMORY 0x0004
#define TIG_VIDEO_BUFFER_SYSTEM_MEMORY 0x0008
#define TIG_VIDEO_BUFFER_RENDER_TARGET 0x0010
#define TIG_VIDEO_BUFFER_TEXTURE 0x0020

// CE: Flags for `tig_video_buffer_load_from_bmp`.
typedef unsigned int TigVideoBufferLoadFromBmpFlags;

// Create the destination video buffer (and return it via the out pointer).
// Without this bit, the caller must pre-allocate `*video_buffer_ptr`.
#define TIG_VIDEO_BUFFER_LOAD_BMP_ALLOCATE 0x0001

// Treat #00FF00 (pure green) as a chroma-key transparency color: keyed
// pixels are skipped on downstream blits. Use this when loading custom
// UI overrides (BMPs that paint #00FF00 in the cut-out regions). Also
// auto-converts alpha=0 pixels in 32-bit BMPs to the key colour so an
// artist can choose either authoring style.
#define TIG_VIDEO_BUFFER_LOAD_BMP_CHROMAKEY 0x0002

typedef unsigned int TigVideoBufferBlitFlags;

#define TIG_VIDEO_BUFFER_BLIT_FLIP_X 0x0001
#define TIG_VIDEO_BUFFER_BLIT_FLIP_Y 0x0002
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ADD 0x0004
// CE (feature/perf-gpu-accel): subtractive blend for the GPU blit path
// (object shadows): dst = max(dst - src, 0). tig_art_blit handles SUB in its
// own loop and never sets a vbuffer SUB flag, so this bit is GPU-path-only.
#define TIG_VIDEO_BUFFER_BLIT_BLEND_SUB 0x0008
#define TIG_VIDEO_BUFFER_BLIT_BLEND_MUL 0x0010
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_AVG 0x0020
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST 0x0040
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_SRC 0x0080
#define TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_LERP 0x0100
#define TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST 0x0200
#define TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP 0x0400
// CE (feature/perf-gpu-accel): per-column light field (walls). Unlike COLOR_LERP
// (4 corners), this carries the full per-screen-column color array so the GPU
// samples color_array[column] across a fine grid -- reproducing the software
// per-column wall vignette that flows seamlessly across adjacent wall tiles,
// instead of a 2-endpoint linear gradient that flattens each wall and seams at
// tile boundaries.
#define TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_ARRAY 0x0800

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

// CE (feature/perf-gpu-accel step 6): register a GPU world texture to composite
// UNDER the framebuffer at flip (instead of reading it back). Drawn opaque at
// dst_rect (NULL = full target), then the ARGB framebuffer alpha-blends on top so
// transparent iso-region pixels reveal the world. One frame only; re-register each
// frame; texture=NULL clears.
void tig_video_set_world_underlay(SDL_Texture* texture, const TigRect* dst_rect);

// CE (step 6): fill a screen-surface rect with transparent (alpha 0).
void tig_video_fill_transparent(const TigRect* rect);

// CE (step 6): register the roof present-layer texture, alpha-blended between the
// world underlay and the framebuffer at flip. Persists each frame; NULL clears.
void tig_video_set_roof_underlay(SDL_Texture* texture, const TigRect* dst_rect);

// CE (full GPU/UI): (re)sync + return the GPU mirror texture of a CPU-surface
// window VB (XRGB -> ARGB, colorkey -> alpha 0), for the gpu-ui per-window
// compositor to RenderTexture. NULL if the VB has no CPU surface.
SDL_Texture* tig_video_buffer_gpu_mirror_sync(TigVideoBuffer* video_buffer);

// CE (full GPU/UI stage 2): tint-aware mirror sync for the translucent-black HUD
// bar — near-black pixels (<= threshold) become black at alpha=darken so that
// compositing the result (BLEND) over the live GPU world reproduces the CPU tint
// (world darkened by `darken`); colorkey -> transparent, else opaque art.
SDL_Texture* tig_video_buffer_gpu_tint_mirror_sync(TigVideoBuffer* video_buffer, uint8_t threshold, uint8_t darken);

// CE (full GPU/UI): callback that composites the UI window stack directly on the
// GPU at flip (registered by the window layer). Called with the render target
// cleared and the world+roof underlay already drawn; it draws the windows on top.
typedef void (*TigUiCompositeFunc)(void);
void tig_video_set_ui_composite_func(TigUiCompositeFunc func);

// CE (full GPU/UI): enable/disable gpu-ui mode — when on, the flip skips the CPU
// framebuffer upload + draw and instead invokes the UI composite callback.
void tig_video_set_gpu_ui(bool enabled);

// CE (full GPU/UI): true when gpu-ui mode is active and a composite callback is
// registered (callers gate their CPU-framebuffer draws on this, e.g. the cursor).
bool tig_video_gpu_ui_is_enabled(void);

// CE (full GPU/UI): composite a src sub-rect (NULL = whole) of a window/cursor
// mirror texture to dst, optionally alpha-modulated (transform fade, 0..1) and
// clipped (clip=NULL for none). Keeps raw SDL out of the window/mouse layers.
void tig_video_composite_ui_texture(SDL_Texture* tex, const TigRect* src, const TigRect* dst, float alpha, const TigRect* clip);

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
// CE: world-knockout composite — window pixels matching `key` (RGB) are
// replaced by the raw underlay (world) pixel, a true untinted cut-out for
// custom-shaped windows. Separate from the near-black tint. See video.c.
int tig_video_blit_knockout(TigVideoBuffer* src_video_buffer,
    TigRect* src_rect,
    TigRect* dst_rect,
    TigVideoBuffer* underlay_video_buffer,
    int underlay_offset_x,
    int underlay_offset_y,
    tig_color_t key);
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

// CE (feature/perf-gpu-accel Phase 2): GPU blit primitive.
//
// The source is a raw SDL_Texture (typically from the art GPU cache); the
// destination must be a GPU-backed TigVideoBuffer (TIG_VIDEO_BUFFER_TEXTURE
// flag set, i.e. TEXTUREACCESS_TARGET). The function temporarily binds the
// destination as the render target and restores the previous target before
// returning.
//
// Supported blend modes (subset of TigVideoBufferBlitFlags used by
// tile_draw_iso):
//   - TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP: per-corner color modulation.
//     `lerp_rect` (in source-texture coordinates) defines the gradient field
//     where lerp_colors[0..3] = TL, TR, BR, BL. `src_rect` is the visible
//     sub-region of that field; the function bilinearly interpolates
//     lerp_colors at src_rect's corners and emits 4 vertices with those
//     colors via SDL_RenderGeometry. Matches the CPU
//     TIG_ART_BLT_BLEND_COLOR_LERP semantics.
//
//   - TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST: SDL_SetTextureColorMod with
//     lerp_colors[0], then SDL_RenderTexture. The color is reset before
//     returning so subsequent blits aren't affected.
//
//   - No blend / FLIP_X / FLIP_Y: plain SDL_RenderTexture (with flip if
//     either flag is set).
//
// Source-texture alpha is honored via SDL_BLENDMODE_BLEND so art uploaded
// with a colorkey -> alpha=0 conversion renders transparent over the dst.
//
// Supported blends: plain copy, COLOR_CONST (tint), COLOR_LERP (bilinear grid),
// ADD, SUB, MUL, ALPHA_CONST (alpha[0]), and ALPHA_LERP_BOTH (per-corner alpha
// in the bilinear grid); these compose (e.g. COLOR_CONST | SUB for object
// shadows; COLOR_CONST | ALPHA_LERP_BOTH for fading walls). ALPHA_AVG /
// ALPHA_SRC / ALPHA_LERP_X / ALPHA_LERP_Y / STIPPLE remain unsupported and
// return TIG_ERR_GENERIC (callers fall back to software).
typedef struct TigVideoBufferBlitGpuInfo {
    TigVideoBufferBlitFlags flags;
    SDL_Texture* src_texture;
    TigRect* src_rect;
    TigRect* lerp_rect;
    tig_color_t lerp_colors[4];
    // ALPHA_CONST uses alpha[0] only. ALPHA_LERP_BOTH uses all 4 (TL, TR, BR,
    // BL -- same corner order as lerp_colors), bilinearly interpolated across
    // the same grid as COLOR_LERP.
    uint8_t alpha[4];
    // CE: COLOR_ARRAY per-column light field. color_array[i] is the lit color of
    // screen column i (left to right) of the source sprite; color_array_count is
    // the number of valid columns. Sampled per grid vertex by source column.
    const uint32_t* color_array;
    int color_array_count;
    TigVideoBuffer* dst_video_buffer;
    TigRect* dst_rect;
} TigVideoBufferBlitGpuInfo;

int tig_video_buffer_blit_gpu(const TigVideoBufferBlitGpuInfo* blit_info);

// CE (feature/perf-gpu-accel Phase 2): one-shot upload of a CPU-backed
// TigVideoBuffer (SDL_Surface) into a freshly created SDL_Texture suitable
// for use as a `tig_video_buffer_blit_gpu` source. Returns NULL if the
// input buffer is GPU-backed or the renderer isn't ready. The caller owns
// the returned texture and must SDL_DestroyTexture it when done.
//
// Honors any color key set on the source surface (via
// SDL_CreateTextureFromSurface, which converts the keyed pixels to alpha=0
// and sets SDL_BLENDMODE_BLEND on the resulting texture). Sampling defaults
// to nearest to match the rest of the pixel-art pipeline.
SDL_Texture* tig_video_buffer_upload_to_texture(TigVideoBuffer* video_buffer);

// CE (feature/perf-gpu-accel Phase 3): expose the SDL_Texture underlying a
// GPU-backed TigVideoBuffer. Returns NULL for CPU buffers. The texture is
// owned by the TigVideoBuffer; callers must not destroy it.
//
// Used by the tile-pass GPU path to call SDL_UpdateTexture / SDL_RenderRead
// Pixels directly against the world render target -- routing those through
// a wrapper would just add an indirection without buying anything since
// they're SDL-specific by nature.
SDL_Texture* tig_video_buffer_get_sdl_texture(TigVideoBuffer* video_buffer);
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

// CE (feature/perf-gpu-accel): dump a CPU-backed video buffer to an absolute
// path via SDL_SaveBMP (raw stdio, not tig file IO -- so /tmp works). For the
// self-test harness that compares gpu vs software world renders. Returns
// TIG_ERR_GENERIC for GPU buffers or on write failure.
int tig_video_buffer_debug_save_bmp(TigVideoBuffer* video_buffer, const char* abs_path);

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
