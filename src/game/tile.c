#include "game/tile.h"

#define _USE_MATH_DEFINES
#include <math.h>

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

    return true;
}

// 0x4D68A0
void tile_exit(void)
{
    sub_4D7980();
    tile_iso_window_handle = TIG_WINDOW_HANDLE_INVALID;
    tile_invalidate_rect = NULL;
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

// NOTE: In the original code this function is a part of `tile_draw`, however
// if `tile_draw_topdown` is definitely there, why `tile_draw_iso` should not?
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

    for (v2 = 0; v2 < v1->num_rows; v2++) {
        v3 = &(v1->rows[v2]);

        for (v4 = 0; v4 < v3->num_cols; v4++) {
            indexes[v4] = v3->tile_ids[v4];
            widths[v4] = 64 - v3->num_hor_tiles[v4];
            sector_lock_results[v4] = sector_lock(v3->sector_ids[v4], &(sectors[v4]));
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

                                            tig_art_blit(&art_blit_info);
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

                                            tig_art_blit(&art_blit_info);
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

                                            tig_art_blit(&art_blit_info);
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

                                            tig_art_blit(&art_blit_info);
                                        }
                                    } else {
                                        tig_art_blit(&art_blit_info);
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
                                && gap_scan_count < (int)(sizeof(gap_scan_list) / sizeof(gap_scan_list[0]))) {
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
                sector_unlock(v3->sector_ids[v4]);
            }
        }
    }

    // CE: read back the queued facade tiles' rendered pixels in one pass (single VB
    // lock). Any that blit pure black are the black off-camera scenery — mark them
    // void so next frame's fade feathers from them. note_dark ignores already-void
    // tiles, so this settles after the area's first full draw.
    //
    // Skip the scan entirely while the zoom is animating: at zoom != 1 the world
    // VB is rendered across frames and "has stale or empty regions" during a zoom
    // step (see gamelib draw), so reading it then marks tiles black at positions
    // that were only momentarily black mid-zoom — leaving fade painted in the
    // wrong place. The marks are persistent, so detecting once the zoom settles is
    // enough.
    if (void_edge_fade_enabled() && gap_scan_count > 0 && !iso_zoom_is_animating()) {
        TigVideoBufferData vbd;
        if (tig_video_buffer_lock(dword_602DF0) == TIG_OK) {
            if (tig_video_buffer_data(dword_602DF0, &vbd) == TIG_OK && vbd.pixels != NULL) {
                int i;
                // Probe offsets within the 80x40 tile diamond around its center.
                // A tile is "renders black" only if EVERY probe is black: true
                // black.ART chunks pass; good cliff art with a dark crevice at
                // one probe fails (single-pixel sampling marked those and sprayed
                // false dark blotches onto lit faces).
                static const int probe_dx[5] = { 0, -14, 14, 0, 0 };
                static const int probe_dy[5] = { 0, 0, 0, -7, 7 };
                for (i = 0; i < gap_scan_count; i++) {
                    int px = gap_scan_list[i].px;
                    int py = gap_scan_list[i].py;
                    int ffc = gap_scan_list[i].ff_center;
                    // Divide the fade back out to recover the pre-fade pixel, so a
                    // cliff the fade merely dimmed isn't mistaken for black art.
                    // Skip the innermost (ffc<16) ring: too faded to tell, and it
                    // sits right at the void where the fade already exists.
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

    light_buffers_unlock();
}
