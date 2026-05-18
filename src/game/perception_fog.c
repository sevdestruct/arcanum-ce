#include "game/perception_fog.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game/gamelib.h"
#include "game/iso_zoom.h"
#include "game/scroll.h"
#include "game/settings.h"
#include "tig/video.h"

/* -------------------------------------------------------------------------
 * Runtime config (read from arcanum.cfg via the shared settings system)
 * ---------------------------------------------------------------------- */

static bool pfog_cfg_enabled     = false;
static bool pfog_cfg_blur        = true;
static int  pfog_cfg_alpha_pct   = 75;  /* 0-100 */
static int  pfog_cfg_inner_pct   = 90;  /* 0-200 */
static int  pfog_cfg_outer_pct   = 130; /* 0-200 */
static int  pfog_cfg_blur_radius = 8;
static int  pfog_cfg_dim_pct     = 50;  /* 0-100 */

static void pfog_enabled_changed(void)
{
    pfog_cfg_enabled = settings_get_value(&settings, PERCEPTION_FOG_ENABLED_KEY) != 0;
    perception_fog_mark_dirty();
}

static void pfog_blur_changed(void)
{
    pfog_cfg_blur = settings_get_value(&settings, PERCEPTION_FOG_BLUR_KEY) != 0;
    perception_fog_mark_dirty();
}

static void pfog_alpha_changed(void)
{
    int v = settings_get_value(&settings, PERCEPTION_FOG_ALPHA_KEY);
    if (v >= 0 && v <= 100) {
        pfog_cfg_alpha_pct = v;
        perception_fog_mark_dirty();
    }
}

static void pfog_inner_changed(void)
{
    int v = settings_get_value(&settings, PERCEPTION_FOG_INNER_KEY);
    if (v >= 0) {
        pfog_cfg_inner_pct = v;
        perception_fog_mark_dirty();
    }
}

static void pfog_outer_changed(void)
{
    int v = settings_get_value(&settings, PERCEPTION_FOG_OUTER_KEY);
    if (v >= 0) {
        pfog_cfg_outer_pct = v;
        perception_fog_mark_dirty();
    }
}

static void pfog_blur_radius_changed(void)
{
    int v = settings_get_value(&settings, PERCEPTION_FOG_BLUR_RADIUS_KEY);
    if (v >= 0 && v <= 64) {
        pfog_cfg_blur_radius = v;
        perception_fog_mark_dirty();
    }
}

static void pfog_dim_changed(void)
{
    int v = settings_get_value(&settings, PERCEPTION_FOG_DIM_KEY);
    if (v >= 0 && v <= 100) {
        pfog_cfg_dim_pct = v;
        perception_fog_mark_dirty();
    }
}

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

/* Viewport dimensions these buffers were sized for. */
static int pfog_width  = 0;
static int pfog_height = 0;

/* Per-pixel elliptical alpha mask (0 = clear, 255 = full fog).
 * Sized pfog_width * pfog_height uint8_t.
 * Regenerated lazily when pfog_dirty is true. */
static uint8_t* pfog_alpha_mask = NULL;

/* Scratch buffers for the separable box blur.
 * pfog_blur_h: game pixels after horizontal blur pass.
 * pfog_blur_v: pfog_blur_h after vertical blur + dim (final foggy pixels).
 * Both are pfog_width * pfog_height uint32_t. */
static uint32_t* pfog_blur_h = NULL;
static uint32_t* pfog_blur_v = NULL;

/* Per-column channel accumulators for the cache-friendly row-major vertical
 * blur pass.  Allocated once per buffer size (pfog_width elements each). */
static int64_t* pfog_col_r = NULL;
static int64_t* pfog_col_g = NULL;
static int64_t* pfog_col_b = NULL;

/* True when the alpha mask must be rebuilt before the next draw. */
static bool pfog_dirty = true;

/* True when any mask pixel was > 0 in the last regeneration.
 * Used to skip the composite pass when the fog area is entirely off-screen. */
static bool pfog_has_fog = false;

/* -------------------------------------------------------------------------
 * Helper macros for XRGB8888 pixel packing
 * ---------------------------------------------------------------------- */

#define PIXEL_R(p)        (((p) >> 16) & 0xFF)
#define PIXEL_G(p)        (((p) >>  8) & 0xFF)
#define PIXEL_B(p)        ((p) & 0xFF)
#define PIXEL_MAKE(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void pfog_free_buffers(void)
{
    free(pfog_alpha_mask); pfog_alpha_mask = NULL;
    free(pfog_blur_h);     pfog_blur_h     = NULL;
    free(pfog_blur_v);     pfog_blur_v     = NULL;
    free(pfog_col_r);      pfog_col_r      = NULL;
    free(pfog_col_g);      pfog_col_g      = NULL;
    free(pfog_col_b);      pfog_col_b      = NULL;
}

static bool pfog_alloc_buffers(int w, int h)
{
    pfog_free_buffers();

    pfog_alpha_mask = (uint8_t*)malloc((size_t)w * (size_t)h * sizeof(uint8_t));
    pfog_blur_h     = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    pfog_blur_v     = (uint32_t*)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    pfog_col_r      = (int64_t*)malloc((size_t)w * sizeof(int64_t));
    pfog_col_g      = (int64_t*)malloc((size_t)w * sizeof(int64_t));
    pfog_col_b      = (int64_t*)malloc((size_t)w * sizeof(int64_t));

    if (!pfog_alpha_mask || !pfog_blur_h || !pfog_blur_v
        || !pfog_col_r || !pfog_col_g || !pfog_col_b) {
        pfog_free_buffers();
        return false;
    }

    pfog_width  = w;
    pfog_height = h;
    return true;
}

/**
 * Rebuild pfog_alpha_mask using the current player screen position, zoom
 * level, and perception pixel limits.
 */
static void pfog_regenerate_mask(void)
{
    int hor_limit, vert_limit;
    int player_sx, player_sy;
    float z;
    float ha, va;
    float inner_r, outer_r;
    int x, y;
    bool any_fog = false;
    uint8_t max_alpha;

    pfog_dirty = false;

    scroll_perception_pixel_limits(&hor_limit, &vert_limit);
    if (hor_limit <= 0 || vert_limit <= 0) {
        /* ScrollDist=0: no leash, no fog. */
        memset(pfog_alpha_mask, 0, (size_t)pfog_width * (size_t)pfog_height);
        pfog_has_fog = false;
        return;
    }

    scroll_get_player_screen_pos(&player_sx, &player_sy);
    z = iso_zoom_current();

    /* The zoom blit (when z != 1) is centred on the viewport midpoint.
     * scroll_get_player_screen_pos returns 1x-viewport coordinates, but the
     * fog is composited onto the already-zoomed output buffer.  Any player
     * offset from the viewport centre is scaled by z in the output, so we
     * must apply the same transform to get the correct fog-buffer position.
     *
     *   fog_pos = vp_centre + (player_1x - vp_centre) * z
     *
     * At z=1 this is a no-op. */
    {
        float vp_cx = (float)(pfog_width  / 2);
        float vp_cy = (float)(pfog_height / 2);
        player_sx = (int)(vp_cx + ((float)player_sx - vp_cx) * z + 0.5f);
        player_sy = (int)(vp_cy + ((float)player_sy - vp_cy) * z + 0.5f);
    }

    /* Semi-axes of the fog ellipse in screen pixels. */
    ha = (float)hor_limit  * z;
    va = (float)vert_limit * z;

    inner_r   = (float)pfog_cfg_inner_pct / 100.0f;
    outer_r   = (float)pfog_cfg_outer_pct / 100.0f;
    max_alpha = (uint8_t)((float)pfog_cfg_alpha_pct * 255.0f / 100.0f + 0.5f);

    for (y = 0; y < pfog_height; y++) {
        uint8_t* row = pfog_alpha_mask + y * pfog_width;
        float dy  = (float)(y - player_sy) / va;
        float dy2 = dy * dy;

        for (x = 0; x < pfog_width; x++) {
            float dx = (float)(x - player_sx) / ha;
            float d  = sqrtf(dx * dx + dy2);
            uint8_t alpha;

            if (d <= inner_r) {
                alpha = 0;
            } else if (d >= outer_r) {
                alpha = max_alpha;
                any_fog = true;
            } else {
                float t = (d - inner_r) / (outer_r - inner_r);
                /* Smooth step: t*t*(3 - 2*t) */
                t = t * t * (3.0f - 2.0f * t);
                alpha = (uint8_t)(t * (float)max_alpha + 0.5f);
                if (alpha > 0) any_fog = true;
            }

            row[x] = alpha;
        }
    }

    pfog_has_fog = any_fog;
}

/**
 * Horizontal separable box blur.
 *
 * Reads XRGB pixels from src (row pitch = src_pitch_words uint32_t per row).
 * Writes blurred XRGB output into dst (flat array, w * h uint32_t).
 */
static void pfog_blur_pass_h(const uint32_t* src, int src_pitch_words,
                              uint32_t* dst, int w, int h, int radius)
{
    int y;

    for (y = 0; y < h; y++) {
        const uint32_t* srow = src + (size_t)y * src_pitch_words;
        uint32_t*       drow = dst + (size_t)y * w;
        int64_t sr = 0, sg = 0, sb = 0;
        int wnd_start = 0;
        int wnd_end   = -1;
        int x;

        for (x = 0; x < w; x++) {
            int new_end = x + radius;
            if (new_end >= w) new_end = w - 1;
            while (wnd_end < new_end) {
                uint32_t p = srow[++wnd_end];
                sr += PIXEL_R(p);
                sg += PIXEL_G(p);
                sb += PIXEL_B(p);
            }

            int new_start = x - radius;
            if (new_start < 0) new_start = 0;
            while (wnd_start < new_start) {
                uint32_t p = srow[wnd_start++];
                sr -= PIXEL_R(p);
                sg -= PIXEL_G(p);
                sb -= PIXEL_B(p);
            }

            int count = wnd_end - wnd_start + 1;
            drow[x] = PIXEL_MAKE((uint8_t)(sr / count),
                                  (uint8_t)(sg / count),
                                  (uint8_t)(sb / count));
        }
    }
}

/**
 * Vertical separable box blur + dim — cache-friendly row-major implementation.
 *
 * Row-major access patterns keep all inner loops in sequential memory,
 * avoiding the cache thrashing of a naive column-by-column approach.
 *
 * dim_pct: 0 = solid black, 100 = no dimming.
 */
static void pfog_blur_pass_v_dim(const uint32_t* src_flat,
                                  uint32_t* dst_flat,
                                  int w, int h, int radius, int dim_pct)
{
    int x, y;
    int current_bot = -1;
    int current_top = 0;

    memset(pfog_col_r, 0, (size_t)w * sizeof(int64_t));
    memset(pfog_col_g, 0, (size_t)w * sizeof(int64_t));
    memset(pfog_col_b, 0, (size_t)w * sizeof(int64_t));

    for (y = 0; y < h; y++) {
        int count;
        uint32_t* drow;

        /* Extend window downward — sequential row read (cache-friendly). */
        int new_bot = y + radius;
        if (new_bot >= h) new_bot = h - 1;
        while (current_bot < new_bot) {
            const uint32_t* row = src_flat + (size_t)(++current_bot) * w;
            for (x = 0; x < w; x++) {
                uint32_t p = row[x];
                pfog_col_r[x] += PIXEL_R(p);
                pfog_col_g[x] += PIXEL_G(p);
                pfog_col_b[x] += PIXEL_B(p);
            }
        }

        /* Shrink window from top — sequential row read (cache-friendly). */
        int new_top = y - radius;
        if (new_top < 0) new_top = 0;
        while (current_top < new_top) {
            const uint32_t* row = src_flat + (size_t)(current_top++) * w;
            for (x = 0; x < w; x++) {
                uint32_t p = row[x];
                pfog_col_r[x] -= PIXEL_R(p);
                pfog_col_g[x] -= PIXEL_G(p);
                pfog_col_b[x] -= PIXEL_B(p);
            }
        }

        count = current_bot - current_top + 1;
        drow  = dst_flat + (size_t)y * w;
        for (x = 0; x < w; x++) {
            uint8_t r = (uint8_t)((pfog_col_r[x] / count) * dim_pct / 100);
            uint8_t g = (uint8_t)((pfog_col_g[x] / count) * dim_pct / 100);
            uint8_t b = (uint8_t)((pfog_col_b[x] / count) * dim_pct / 100);
            drow[x] = PIXEL_MAKE(r, g, b);
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool perception_fog_init(GameInitInfo* init_info)
{
    TigWindowData window_data;

    if (tig_window_data(init_info->iso_window_handle, &window_data) != TIG_OK) {
        tig_debug_printf("perception_fog_init: tig_window_data failed\n");
        return false;
    }

    if (!pfog_alloc_buffers(window_data.rect.width, window_data.rect.height)) {
        /* Non-fatal: fog just won't work. */
        tig_debug_printf("perception_fog_init: buffer allocation failed — fog disabled\n");
        return true;
    }

    /* Register all fog settings with the shared arcanum.cfg system.
     * Call each callback immediately so the statics match the on-disk value. */
    settings_register(&settings, PERCEPTION_FOG_ENABLED_KEY,      "0",   pfog_enabled_changed);
    pfog_enabled_changed();

    settings_register(&settings, PERCEPTION_FOG_BLUR_KEY,         "1",   pfog_blur_changed);
    pfog_blur_changed();

    settings_register(&settings, PERCEPTION_FOG_ALPHA_KEY,        "75",  pfog_alpha_changed);
    pfog_alpha_changed();

    settings_register(&settings, PERCEPTION_FOG_INNER_KEY,        "90",  pfog_inner_changed);
    pfog_inner_changed();

    settings_register(&settings, PERCEPTION_FOG_OUTER_KEY,        "130", pfog_outer_changed);
    pfog_outer_changed();

    settings_register(&settings, PERCEPTION_FOG_BLUR_RADIUS_KEY,  "8",   pfog_blur_radius_changed);
    pfog_blur_radius_changed();

    settings_register(&settings, PERCEPTION_FOG_DIM_KEY,          "50",  pfog_dim_changed);
    pfog_dim_changed();

    pfog_dirty = true;
    return true;
}

void perception_fog_exit(void)
{
    pfog_free_buffers();
    pfog_width  = 0;
    pfog_height = 0;
}

void perception_fog_resize(GameResizeInfo* resize_info)
{
    if (!pfog_alloc_buffers(resize_info->content_rect.width,
                             resize_info->content_rect.height)) {
        /* Non-fatal. */
    }
    pfog_dirty = true;
}

bool perception_fog_is_enabled(void)
{
    return pfog_cfg_enabled;
}

void perception_fog_mark_dirty(void)
{
    pfog_dirty = true;
    /* Ensure the game redraws this frame so the updated fog is visible.
     * Skip the invalidate when fog is off to avoid unnecessary full redraws
     * during zoom animation or player movement. */
    if (pfog_cfg_enabled) {
        gamelib_invalidate_rect(NULL);
    }
}

void perception_fog_draw(TigVideoBuffer* game_vb)
{
    TigVideoBufferData vbd;
    uint32_t* src_pixels;
    int x, y;
    int pitch_words;
    bool blur_enabled;

    if (!pfog_cfg_enabled || pfog_alpha_mask == NULL || game_vb == NULL) {
        return;
    }

    /* Rebuild the elliptical mask if perception, zoom, or player pos changed. */
    if (pfog_dirty) {
        pfog_regenerate_mask();
    }

    /* Nothing to draw if the fog ellipse is entirely off-screen. */
    if (!pfog_has_fog) {
        return;
    }

    if (tig_video_buffer_lock(game_vb) != TIG_OK) {
        return;
    }
    if (tig_video_buffer_data(game_vb, &vbd) != TIG_OK) {
        tig_video_buffer_unlock(game_vb);
        return;
    }

    if (vbd.width != pfog_width || vbd.height != pfog_height
        || vbd.pitch == 0 || vbd.pixels == NULL) {
        tig_video_buffer_unlock(game_vb);
        return;
    }

    src_pixels  = (uint32_t*)vbd.pixels;
    pitch_words = vbd.pitch / 4; /* uint32_t elements per row */
    blur_enabled = pfog_cfg_blur && pfog_cfg_blur_radius > 0;

    if (blur_enabled) {
        /* Blur path: build the dimmed/blurred fog buffer, then alpha-composite
         * it onto the game frame using the elliptical mask. */
        pfog_blur_pass_h(src_pixels, pitch_words,
                         pfog_blur_h, pfog_width, pfog_height,
                         pfog_cfg_blur_radius);
        pfog_blur_pass_v_dim(pfog_blur_h, pfog_blur_v,
                             pfog_width, pfog_height,
                             pfog_cfg_blur_radius,
                             pfog_cfg_dim_pct);

        for (y = 0; y < pfog_height; y++) {
            uint32_t*       drow = src_pixels + (size_t)y * pitch_words;
            const uint8_t*  mrow = pfog_alpha_mask + (size_t)y * pfog_width;
            const uint32_t* frow = pfog_blur_v + (size_t)y * pfog_width;

            for (x = 0; x < pfog_width; x++) {
                int alpha = mrow[x];
                uint32_t game_px;
                uint32_t fog_px;

                if (alpha == 0) {
                    continue;
                }
                game_px = drow[x];
                fog_px  = frow[x];

                if (alpha >= 255) {
                    drow[x] = fog_px;
                } else {
                    int inv = 255 - alpha;
                    drow[x] = PIXEL_MAKE(
                        (uint8_t)((PIXEL_R(game_px) * inv + PIXEL_R(fog_px) * alpha) / 255),
                        (uint8_t)((PIXEL_G(game_px) * inv + PIXEL_G(fog_px) * alpha) / 255),
                        (uint8_t)((PIXEL_B(game_px) * inv + PIXEL_B(fog_px) * alpha) / 255));
                }
            }
        }
    } else {
        /* Solid-dark fast path: fog = game * dim/100, so
         *   out = game*(1-α) + fog*α = game * (25500 - α*(100-dim)) / 25500.
         * Skip the intermediate fog buffer and fold dim+composite into one
         * per-pixel multiplier. */
        int neg_dim = 100 - pfog_cfg_dim_pct;
        for (y = 0; y < pfog_height; y++) {
            uint32_t*      drow = src_pixels + (size_t)y * pitch_words;
            const uint8_t* mrow = pfog_alpha_mask + (size_t)y * pfog_width;

            for (x = 0; x < pfog_width; x++) {
                int alpha = mrow[x];
                int k;
                uint32_t game_px;

                if (alpha == 0) {
                    continue;
                }
                k = 25500 - alpha * neg_dim;
                game_px = drow[x];
                drow[x] = PIXEL_MAKE(
                    (uint8_t)(PIXEL_R(game_px) * k / 25500),
                    (uint8_t)(PIXEL_G(game_px) * k / 25500),
                    (uint8_t)(PIXEL_B(game_px) * k / 25500));
            }
        }
    }

    tig_video_buffer_unlock(game_vb);
}
