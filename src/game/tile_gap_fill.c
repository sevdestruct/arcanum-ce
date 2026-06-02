#include "game/tile_gap_fill.h"

#include <string.h>

#include "game/sector.h"
#include "game/terrain.h"
#include "game/tile.h"

#define GAP_TILES_PER_SECTOR 4096
#define GAP_SECTOR_DIM 64

// Direct-mapped memo of tig_art_exists() results, keyed by a hash of the art id.
// Sector loads scan 4096 tiles; without this every scan would re-stat the art
// files. Tiles share art ids heavily, so a small cache has a high hit rate.
#define GAP_EXISTS_MEMO_BITS 12
#define GAP_EXISTS_MEMO_SIZE (1 << GAP_EXISTS_MEMO_BITS)
#define GAP_EXISTS_MEMO_MASK (GAP_EXISTS_MEMO_SIZE - 1)

static bool gap_initialized;
static bool gap_editor_mode;

// One representative, known-good authored tile per terrain type. Populated from
// real loaded sectors so out-of-bounds void can be filled with terrain that
// actually exists on disk.
static tig_art_id_t gap_repr_art[TERRAIN_TYPE_COUNT];
static bool gap_repr_valid[TERRAIN_TYPE_COUNT];

static int64_t gap_oob_cache_sec = -1;
static tig_art_id_t gap_oob_cache_art = TIG_ART_ID_INVALID;
static bool gap_oob_cache_set;

static tig_art_id_t gap_exists_memo_key[GAP_EXISTS_MEMO_SIZE];
static uint8_t gap_exists_memo_val[GAP_EXISTS_MEMO_SIZE]; // 0 = empty, 1 = no, 2 = yes

// Orthogonal neighbors first, then diagonals, so fills prefer edge-adjacent art.
static const int gap_nx[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
static const int gap_ny[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

static bool gap_art_exists_cached(tig_art_id_t aid)
{
    unsigned int slot = (aid ^ (aid >> 13)) & GAP_EXISTS_MEMO_MASK;

    if (gap_exists_memo_val[slot] != 0 && gap_exists_memo_key[slot] == aid) {
        return gap_exists_memo_val[slot] == 2;
    }

    bool exists = tig_art_exists(aid) == TIG_OK;
    gap_exists_memo_key[slot] = aid;
    gap_exists_memo_val[slot] = exists ? 2 : 1;
    return exists;
}

static bool gap_is_placeholder_pattern(tig_art_id_t aid)
{
    if (tig_art_type(aid) != TIG_ART_TYPE_TILE) {
        return false;
    }
    return tig_art_tile_id_num1_get(aid) == tig_art_tile_id_num2_get(aid)
        && tig_art_tile_id_flippable1_get(aid) == 0
        && tig_art_tile_id_flippable2_get(aid) == 0
        && tig_art_tile_id_type_get(aid) == 0;
}

// A tile the engine can actually blit (placeholder grass counts).
static bool gap_renderable(tig_art_id_t aid)
{
    return gap_art_exists_cached(aid);
}

// A tile worth copying as context: renderable and not the hard-coded fallback.
static bool gap_good(tig_art_id_t aid)
{
    return gap_renderable(aid) && !gap_is_placeholder_pattern(aid);
}

// A gap is anything that would render black (missing art) or the fallback grass.
static bool gap_is_gap(tig_art_id_t aid)
{
    return !gap_renderable(aid) || gap_is_placeholder_pattern(aid);
}

static int gap_sector_terrain(int64_t sec_id)
{
    uint16_t v = sub_4E87F0(sec_id);
    int t;

    if (v == 0xFFFF) {
        return -1;
    }
    t = sub_4E8DC0(v);
    if (t < 0 || t >= TERRAIN_TYPE_COUNT) {
        return -1;
    }
    return t;
}

// Returns a known-good (or at worst renderable) tile from a loaded sector,
// preferring the tile at (tx, ty) so the fill matches the local terrain.
static tig_art_id_t gap_pick_from_sector(Sector* sector, int tx, int ty)
{
    tig_art_id_t at = sector->tiles.art_ids[(tx & 63) | ((ty & 63) << 6)];
    tig_art_id_t any_renderable = TIG_ART_ID_INVALID;
    int i;

    if (gap_good(at)) {
        return at;
    }

    for (i = 0; i < GAP_TILES_PER_SECTOR; i++) {
        tig_art_id_t a = sector->tiles.art_ids[i];
        if (gap_good(a)) {
            return a;
        }
        if (any_renderable == TIG_ART_ID_INVALID && gap_renderable(a)) {
            any_renderable = a;
        }
    }

    return any_renderable;
}

static void gap_remember_repr(int terrain, tig_art_id_t aid)
{
    if (terrain < 0 || terrain >= TERRAIN_TYPE_COUNT) {
        return;
    }
    if (aid == TIG_ART_ID_INVALID) {
        return;
    }
    gap_repr_art[terrain] = aid;
    gap_repr_valid[terrain] = true;
}

bool tile_gap_fill_init(GameInitInfo* init_info)
{
    int i;

    gap_editor_mode = init_info->editor;
    gap_oob_cache_sec = -1;
    gap_oob_cache_art = TIG_ART_ID_INVALID;
    gap_oob_cache_set = false;

    for (i = 0; i < TERRAIN_TYPE_COUNT; i++) {
        gap_repr_valid[i] = false;
    }
    memset(gap_exists_memo_val, 0, sizeof(gap_exists_memo_val));

    gap_initialized = true;
    return true;
}

void tile_gap_fill_exit(void)
{
    gap_initialized = false;
    gap_oob_cache_set = false;
}

bool tile_gap_fill_enabled(void)
{
    return gap_initialized && !gap_editor_mode;
}

void tile_gap_fill_sector(int64_t sec_id, Sector* sector)
{
    tig_art_id_t snapshot[GAP_TILES_PER_SECTOR];
    SectorTileList* tiles;
    int sector_terrain;
    int idx, tx, ty, i, nx, ny;

    if (!tile_gap_fill_enabled()) {
        return;
    }

    tiles = &sector->tiles;
    sector_terrain = gap_sector_terrain(sec_id);

    // Record a representative authored tile for this terrain type before filling,
    // so out-of-bounds void elsewhere can borrow real terrain art.
    for (idx = 0; idx < GAP_TILES_PER_SECTOR; idx++) {
        if (gap_good(tiles->art_ids[idx])) {
            gap_remember_repr(sector_terrain, tiles->art_ids[idx]);
            break;
        }
    }

    memcpy(snapshot, tiles->art_ids, sizeof(snapshot));

    for (idx = 0; idx < GAP_TILES_PER_SECTOR; idx++) {
        tig_art_id_t fill = TIG_ART_ID_INVALID;
        tig_art_id_t any_renderable = TIG_ART_ID_INVALID;

        if (!gap_is_gap(snapshot[idx])) {
            continue;
        }

        // Never clobber a tile the player/editor explicitly changed.
        if ((tiles->difmask[idx >> 5] & (1u << (idx & 31))) != 0) {
            continue;
        }

        tx = idx & 63;
        ty = (idx >> 6) & 63;

        for (i = 0; i < 8 && fill == TIG_ART_ID_INVALID; i++) {
            tig_art_id_t a;
            nx = tx + gap_nx[i];
            ny = ty + gap_ny[i];
            if (nx < 0 || nx >= GAP_SECTOR_DIM || ny < 0 || ny >= GAP_SECTOR_DIM) {
                continue;
            }
            a = snapshot[nx | (ny << 6)];
            if (gap_good(a)) {
                fill = a;
            } else if (any_renderable == TIG_ART_ID_INVALID && gap_renderable(a)) {
                any_renderable = a;
            }
        }

        if (fill == TIG_ART_ID_INVALID) {
            fill = any_renderable;
        }
        if (fill == TIG_ART_ID_INVALID && sector_terrain >= 0 && gap_repr_valid[sector_terrain]) {
            fill = gap_repr_art[sector_terrain];
        }

        if (fill != TIG_ART_ID_INVALID) {
            tiles->art_ids[idx] = fill;
        }
    }
}

tig_art_id_t tile_gap_fill_synth_tile(int64_t tx, int64_t ty)
{
    int64_t secx, secy, csecx, csecy, limit_x, limit_y, clamped_sec;
    int ltx, lty, terrain;
    tig_art_id_t art = TIG_ART_ID_INVALID;

    if (!tile_gap_fill_enabled()) {
        return TIG_ART_ID_INVALID;
    }

    sector_limits_get(&limit_x, &limit_y);
    if (limit_x <= 0 || limit_y <= 0) {
        return TIG_ART_ID_INVALID;
    }

    secx = tx >> 6; // arithmetic shift floors toward negative, matching sector layout
    secy = ty >> 6;

    csecx = secx < 0 ? 0 : (secx >= limit_x ? limit_x - 1 : secx);
    csecy = secy < 0 ? 0 : (secy >= limit_y ? limit_y - 1 : secy);

    // In-bounds tiles are drawn by the normal pass; not our concern.
    if (csecx == secx && csecy == secy) {
        return TIG_ART_ID_INVALID;
    }

    clamped_sec = SECTOR_MAKE(csecx, csecy);

    if (gap_oob_cache_set && clamped_sec == gap_oob_cache_sec) {
        return gap_oob_cache_art;
    }

    // Sample position within the nearest in-bounds sector.
    ltx = (int)(tx - (secx << 6));
    lty = (int)(ty - (secy << 6));

    // Prefer copying a real tile from the nearest in-bounds sector, but only if
    // it is already resident — never trigger a disk load from inside the render
    // loop.
    if (sector_loaded(clamped_sec)) {
        Sector* sector;
        if (sector_lock(clamped_sec, &sector)) {
            art = gap_pick_from_sector(sector, ltx, lty);
            sector_unlock(clamped_sec);
        }
    }

    if (art == TIG_ART_ID_INVALID) {
        terrain = gap_sector_terrain(clamped_sec);
        if (terrain >= 0 && gap_repr_valid[terrain]) {
            art = gap_repr_art[terrain];
        }
    }

    gap_oob_cache_sec = clamped_sec;
    gap_oob_cache_art = art;
    gap_oob_cache_set = true;
    return art;
}
