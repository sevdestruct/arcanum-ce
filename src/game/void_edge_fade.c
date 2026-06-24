#include "game/void_edge_fade.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tig/file.h"

#include "game/gamelib.h"
#include "game/name.h"
#include "game/sector.h"
#include "game/tile.h"

#define GAP_TILES_PER_SECTOR 4096
#define GAP_SECTOR_DIM 64

// Direct-mapped memo of tig_art_exists() results, keyed by a hash of the art id.
// Sector loads scan 4096 tiles; without this every scan would re-stat the art
// files (~46us each across the override tiers). Tiles share art ids heavily. The
// table must hold a town's whole unique-tile-art set or it thrashes across sector
// loads (re-stats the same arts) -- 16 bits (65536) covers it; 12 bits did not.
#define GAP_EXISTS_MEMO_BITS 16
#define GAP_EXISTS_MEMO_SIZE (1 << GAP_EXISTS_MEMO_BITS)
#define GAP_EXISTS_MEMO_MASK (GAP_EXISTS_MEMO_SIZE - 1)

static bool gap_initialized;

// Engine "repaint this rect" hook (NULL rect = whole window), captured at init like
// facade.c/gfade.c do. Needed because dark marks change how already-drawn tiles
// should look, and only an invalidate makes the renderer redraw them.
static IsoInvalidateRectFunc* gap_invalidate_rect;

static bool gap_editor_mode;

static tig_art_id_t gap_exists_memo_key[GAP_EXISTS_MEMO_SIZE];
static uint8_t gap_exists_memo_val[GAP_EXISTS_MEMO_SIZE]; // 0 = empty, 1 = no, 2 = yes

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

// The exact terrain_fill placeholder art (tig_art_tile_id_create(7,7,15,...)).
// Matched by exact id, NOT by a loose num1==num2 pattern, so real single-type
// indoor tiles are never mistaken for it.
static tig_art_id_t gap_placeholder_cached(void)
{
    static tig_art_id_t ph = TIG_ART_ID_INVALID;
    static bool init;
    if (!init) {
        init = true;
        if (tig_art_tile_id_create(7, 7, 15, 0, 0, 0, 0, 0, &ph) != TIG_OK) {
            ph = TIG_ART_ID_INVALID;
        }
    }
    return ph;
}

static bool gap_is_placeholder(tig_art_id_t aid)
{
    tig_art_id_t ph = gap_placeholder_cached();
    return ph != TIG_ART_ID_INVALID && aid == ph;
}

// "Void" terrain tile-name indices (from tilename.mes): 28 = Black Yall (the flat
// black off-map border the original fixed camera never showed), 29 = Void Dirt,
// 30 = Void Fog. These exist on disk and render as solid black. A tile is a void
// tile when BOTH halves are void names (a solid void cell, not a real-terrain
// transition that merely blends toward void).
static bool gap_is_void_name(int n)
{
    return n >= 28 && n <= 30;
}

static bool gap_is_void_black(tig_art_id_t aid)
{
    if (tig_art_type(aid) != TIG_ART_TYPE_TILE) {
        return false;
    }
    // Either half being a void name means the tile shows black: a solid void cell
    // OR a terrain<->void transition (the thin black seams at the void edge). Void
    // tile-names only ever appear at the off-map border, so this never touches
    // genuine terrain.
    return gap_is_void_name(tig_art_tile_id_num1_get(aid))
        || gap_is_void_name(tig_art_tile_id_num2_get(aid));
}

// A tile the engine can actually blit (placeholder grass counts).
static bool gap_renderable(tig_art_id_t aid)
{
    return gap_art_exists_cached(aid);
}

// A gap is anything that would render black: missing art, the terrain_fill
// placeholder, or an authored void tile.
//
// ARCANUM_OPT_VOIDFADE=1 (aggressive mode) skips the !gap_renderable() check, whose
// gap_art_exists_cached()->tig_art_exists() does a ~46us file stat across the asset
// override tiers -- the dominant cost of a cold sector load (gap_vmask_store scans all
// 4096 tiles). For a normal install ALL tile art exists, so that check never actually
// finds a gap; the real void edges are the bit-checkable placeholder/void-black tiles.
// Skipping it makes the per-sector void scan ~free. Tradeoff: a genuinely missing-art
// tile (corrupt install / a mod that omits art) renders black but no longer feathers.
static bool gap_is_gap(tig_art_id_t aid)
{
    static int skip_exist = -1;
    if (skip_exist < 0) {
        const char* e = getenv("ARCANUM_OPT_VOIDFADE");
        skip_exist = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    if (skip_exist) {
        return gap_is_placeholder(aid) || gap_is_void_black(aid);
    }
    return !gap_renderable(aid) || gap_is_placeholder(aid) || gap_is_void_black(aid);
}

// Cached VOID_EDGE_FADE_KEY value — read per change, not per call (enabled() is
// hit per drawn tile).
static bool gap_cfg_enabled = true;

// Settings callback: refresh the cached flag and repaint so a live toggle takes
// effect immediately. Safe before init (gap_invalidate_rect is NULL then; init
// re-reads the value itself).
void void_edge_fade_settings_changed(void)
{
    gap_cfg_enabled = gamelib_void_edge_fade();
    if (gap_invalidate_rect != NULL) {
        gap_invalidate_rect(NULL);
    }
}

bool void_edge_fade_init(GameInitInfo* init_info)
{
    gap_editor_mode = init_info->editor;
    gap_invalidate_rect = init_info->invalidate_rect_func;
    memset(gap_exists_memo_val, 0, sizeof(gap_exists_memo_val));
    gap_cfg_enabled = gamelib_void_edge_fade();
    gap_initialized = true;
    return true;
}

void void_edge_fade_exit(void)
{
    gap_initialized = false;
}

bool void_edge_fade_enabled(void)
{
    return gap_initialized && !gap_editor_mode && gap_cfg_enabled;
}

bool void_edge_fade_fade_enabled(void)
{
    return void_edge_fade_enabled();
}

// Void-edge fade via a blurred void-density field (morphological rounding). A
// symmetric distance fade is the same width everywhere; instead we average the
// void mask over a screen-round kernel, so terrain darkens by HOW MUCH void
// surrounds it. That eats convex terrain points back to a tight radius and lets
// the fade bleed further through concave notches — the asymmetric, vignette-like
// rounding the markup asked for.
#define FADE_RAD 8        // convolution radius in tiles
#define FADE_RS 300.0f    // screen radius of the kernel in px (~ fade reach)
#define FADE_THRESH 0.21f // terrain hits full black at this void density. LOW pushes
                          // the fully-black zone a couple tiles INSIDE the real edge
                          // so the visible fade never rides over the cliff art's hard
                          // bright tile seams (it's already black by the time it gets
                          // there); the gradient then plays out on interior tiles.

// Per-sector fade-brightness field cache. Direct-mapped by sector id; the field
// is deterministic from the sector's tile art, so a collision just recomputes.
#define FADE_CACHE_SLOTS 256
static struct {
    int64_t sec_id;
    bool valid;
    bool has_void;
    uint8_t bright[GAP_TILES_PER_SECTOR]; // per-tile fade brightness, 0=black..255=full
} gap_fade_cache[FADE_CACHE_SLOTS];

// Screen-round tent kernel over the (2*FADE_RAD+1)^2 tile window. The grid axes
// are the two iso diagonals (+tx -> screen (-40,+20), +ty -> (+40,+20)), so
// weighting by true screen distance makes the kernel a circle ON SCREEN (an
// ellipse in tile space) — correcting the 2:1 iso aspect so the fade reads round.
static float gap_kernel[2 * FADE_RAD + 1][2 * FADE_RAD + 1];
static float gap_kernel_wsum; // total kernel weight — constant across tiles
static bool gap_kernel_ready;

static void gap_build_kernel(void)
{
    int du, dv;
    gap_kernel_wsum = 0.0f;
    for (du = -FADE_RAD; du <= FADE_RAD; du++) {
        for (dv = -FADE_RAD; dv <= FADE_RAD; dv++) {
            float dx = 40.0f * (float)(dv - du);
            float dy = 20.0f * (float)(du + dv);
            float dist = sqrtf(dx * dx + dy * dy);
            float w = 1.0f - dist / FADE_RS;
            if (w < 0.0f) {
                w = 0.0f;
            }
            gap_kernel[du + FADE_RAD][dv + FADE_RAD] = w;
            gap_kernel_wsum += w;
        }
    }
    gap_kernel_ready = true;
}

// True if the sector at (sx,sy) is "nothing": it won't be drawn, so the camera
// shows black there. This must match sector_lock's own reject test (beyond the
// sector limits), because THAT is precisely the edge content terminates to.
static bool gap_sector_off_map(int sx, int sy)
{
    int64_t lx, ly;
    if (sx < 0 || sy < 0) {
        return true;
    }
    sector_limits_get(&lx, &ly);
    return sx >= lx || sy >= ly;
}

// Cache slot for a sector id. A sector id packs sx in the low bits and sy above
// bit 26, so a plain `id & (slots-1)` would only use sx — every sector in the
// same column would collide. Hash sx AND sy so loaded sectors spread out.
static unsigned int gap_sec_slot(int64_t id, unsigned int slots)
{
    unsigned int sx = (unsigned int)SECTOR_X(id);
    unsigned int sy = (unsigned int)SECTOR_Y(id);
    return (sx * 73856093u + sy * 19349663u) & (slots - 1);
}

// Per-sector void-mask table, populated when each sector loads, so the fade can
// read a neighbor's void without locking/loading it mid-draw. Keyed by sector id;
// direct-mapped, a collision just overwrites (the owner re-stores on reload).
// Persistent dark-mark store: one bit per tile, set when the draw-time scan sees
// the tile render black. Kept SEPARATE from the vmask table because these marks
// cannot be re-derived from sector data (the art looks fine; only the rendered
// result is black) — so they must never be lost to cache churn. Open-addressed
// (probes, never overwrites a different live sector on collision), sized well
// above any plausible active-sector count.
#define DARK_SLOTS 1024
#define DARK_PROBES 8
typedef struct {
    int64_t id;
    bool used;
    bool dirty; // fresh marks since the last flush
    uint8_t bits[GAP_TILES_PER_SECTOR / 8];
} GapDarkEntry;
static GapDarkEntry gap_dark_tab[DARK_SLOTS];

static GapDarkEntry* gap_dark_find(int64_t id)
{
    unsigned int h = gap_sec_slot(id, DARK_SLOTS);
    int p;
    for (p = 0; p < DARK_PROBES; p++) {
        GapDarkEntry* e = &gap_dark_tab[(h + p) & (DARK_SLOTS - 1)];
        if (e->used && e->id == id) {
            return e;
        }
    }
    return NULL;
}

static GapDarkEntry* gap_dark_obtain(int64_t id)
{
    unsigned int h = gap_sec_slot(id, DARK_SLOTS);
    GapDarkEntry* free_slot = NULL;
    int p;
    for (p = 0; p < DARK_PROBES; p++) {
        GapDarkEntry* e = &gap_dark_tab[(h + p) & (DARK_SLOTS - 1)];
        if (e->used && e->id == id) {
            return e;
        }
        if (!e->used && free_slot == NULL) {
            free_slot = e;
        }
    }
    if (free_slot == NULL) {
        // Probe ring full (needs 8+ colliding sectors — practically never).
        // Reusing the home slot just delays that sector's fade one redraw.
        free_slot = &gap_dark_tab[h & (DARK_SLOTS - 1)];
    }
    memset(free_slot->bits, 0, sizeof(free_slot->bits));
    free_slot->id = id;
    free_slot->used = true;
    free_slot->dirty = false;
    return free_slot;
}

static bool gap_dark_bit(const GapDarkEntry* e, int index)
{
    return (e->bits[index >> 3] >> (index & 7)) & 1;
}

// Per-sector void-mask table. Open-addressed like the dark store: a hash collision
// probes to another slot instead of overwriting — the old direct-mapped overwrite
// meant a colliding sector wiped its victim's mask on every (re)load, and at
// zoom-out the sector cache reloads constantly. The victim sectors are fixed by
// the hash, which is exactly why the un-faded hard edges sat at the same map spots
// no matter how detection improved.
#define VMASK_SLOTS 512
#define VMASK_PROBES 8
static struct {
    int64_t id;
    bool valid;
    uint8_t v[GAP_TILES_PER_SECTOR]; // 1 = void/gap tile
} gap_vmask_tab[VMASK_SLOTS];

static int gap_vmask_slot_find(int64_t id)
{
    unsigned int h = gap_sec_slot(id, VMASK_SLOTS);
    int p;
    for (p = 0; p < VMASK_PROBES; p++) {
        unsigned int s = (h + p) & (VMASK_SLOTS - 1);
        if (gap_vmask_tab[s].valid && gap_vmask_tab[s].id == id) {
            return (int)s;
        }
    }
    return -1;
}

static void gap_vmask_store(int64_t id, Sector* sector)
{
    unsigned int h = gap_sec_slot(id, VMASK_SLOTS);
    int slot = gap_vmask_slot_find(id);
    const tig_art_id_t* a = sector->tiles.art_ids;
    const GapDarkEntry* dark;
    uint8_t* v;
    int i, p;

    if (slot < 0) {
        slot = (int)(h & (VMASK_SLOTS - 1));
        for (p = 0; p < VMASK_PROBES; p++) {
            unsigned int s = (h + p) & (VMASK_SLOTS - 1);
            if (!gap_vmask_tab[s].valid) {
                slot = (int)s;
                break;
            }
        }
    }
    v = gap_vmask_tab[slot].v;
    dark = gap_dark_find(id);

    // A tile is void if it's a true gap (missing art / placeholder / Black-Yall)
    // — the baseline that produced the right vignette — or a draw-time-verified
    // rendered-black tile that was ANNEXED into the void by contiguity (the dark
    // store only ever accepts marks adjacent to existing void, so connected black
    // off-area regions flood in from the void's own perimeter and merge into one
    // density bulk, while isolated black content — burnt wrecks, cave shadows,
    // dark art — can never join and never drags the fade into the scene).
    // ARCANUM_OPT_VOIDFADE=1: per-sector dedup of gap_is_gap(). A sector's 4096 tiles
    // repeat only a few hundred unique arts, but gap_is_gap()->tig_art_exists() costs
    // ~46us (file-existence across the asset override tiers) and the 12-bit direct-
    // mapped global existence memo thrashes on a sector's art set, so the repeats
    // aren't collapsed -> ~4096 file checks ~= 190ms per cold sector load. Checking each
    // UNIQUE art once cuts that ~20x. Correctness-neutral (gap_is_gap is per-art-id
    // deterministic); gated default-off so it can be A/B'd against the baseline.
    static int gvs_dedup = -1;
    if (gvs_dedup < 0) {
        const char* e = getenv("ARCANUM_OPT_VOIDFADE");
        gvs_dedup = (e != NULL && e[0] == '1') ? 1 : 0;
    }
    static tig_art_id_t gvs_dk[8192];
    static int8_t gvs_dv[8192]; // -1 empty, 0 not-gap, 1 gap
    if (gvs_dedup) {
        memset(gvs_dv, -1, sizeof(gvs_dv));
    }
    for (i = 0; i < GAP_TILES_PER_SECTOR; i++) {
        int g;
        if (gvs_dedup) {
            tig_art_id_t aid = a[i];
            unsigned int s = (unsigned int)(aid * 2654435761u) & 8191u;
            int pr = 0;
            while (gvs_dv[s] >= 0 && gvs_dk[s] != aid && pr < 48) {
                s = (s + 1) & 8191u;
                pr++;
            }
            if (gvs_dv[s] >= 0 && gvs_dk[s] == aid) {
                g = gvs_dv[s];
            } else {
                g = gap_is_gap(aid) ? 1 : 0;
                gvs_dk[s] = aid;
                gvs_dv[s] = (int8_t)g;
            }
        } else {
            g = gap_is_gap(a[i]) ? 1 : 0;
        }
        uint8_t cls = (g || (dark != NULL && gap_dark_bit(dark, i))) ? 1 : 0;
        // Roof-covered void is not "edge to nothing": the all-maps audit found
        // small void regions under the roofs of non-enterable town buildings
        // (15 building footprints on the worldmap). The roof hides them fully,
        // so without this gate they'd cast a feather shadow onto the streets
        // around the building. Roof cell = (ty/4)*16 + tx/4.
        if (cls == 1
            && sector->roofs.art_ids[((i >> 2) & 0xF) + ((i >> 8) << 4)] != TIG_ART_ID_INVALID) {
            cls = 0;
        }
        v[i] = cls;
    }

    gap_vmask_tab[slot].id = id;
    gap_vmask_tab[slot].valid = true;
}

static const uint8_t* gap_vmask_get(int64_t id)
{
    int slot = gap_vmask_slot_find(id);
    return slot >= 0 ? gap_vmask_tab[slot].v : NULL;
}

// Invalidate a sector's cached fade field (so it rebuilds with current neighbor
// masks). Called for the 3x3 block around a sector when it loads.
static void gap_fade_invalidate(int64_t id)
{
    unsigned int slot = gap_sec_slot(id, FADE_CACHE_SLOTS);
    if (gap_fade_cache[slot].sec_id == id) {
        gap_fade_cache[slot].valid = false;
    }
}

static bool gap_sector_off_map(int sx, int sy);

// True if the tile at global tile coords (gx, gy) is currently void: off-map, a
// void/gap tile in its sector's mask, or an annexed dark mark. Missing data reads
// as NOT void (annexation only grows from known void).
static bool gap_void_at(int64_t gx, int64_t gy)
{
    int64_t sxx = gx >> 6;
    int64_t syy = gy >> 6;
    int64_t sec;
    int index;
    const uint8_t* v;
    const GapDarkEntry* de;

    if (gx < 0 || gy < 0 || gap_sector_off_map((int)sxx, (int)syy)) {
        return true;
    }
    sec = SECTOR_MAKE(sxx, syy);
    index = (int)(((gy & 63) << 6) | (gx & 63));
    v = gap_vmask_get(sec);
    if (v != NULL) {
        return v[index] != 0;
    }
    de = gap_dark_find(sec);
    return de != NULL && gap_dark_bit(de, index);
}

// Mark a tile as void because it was observed rendering ~black at draw time (the
// unlit off-area cliff facades — real textured art that the engine never lights, so
// it blits pure black). CONTIGUITY RULE: the mark is accepted only if the tile
// touches existing void — the void floods along connected black regions from its
// own perimeter, so off-area black joins the fade while isolated black content
// (burnt wrecks, cave shadows) never seeds it. Accepted marks go to the persistent
// dark store and are mirrored into the resident vmask immediately.
bool void_edge_fade_note_dark(int64_t sec_id, int index)
{
    static const int ndx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int ndy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    int64_t gx = (int64_t)SECTOR_X(sec_id) * 64 + (index & 63);
    int64_t gy = (int64_t)SECTOR_Y(sec_id) * 64 + (index >> 6);
    GapDarkEntry* e;
    int vslot;
    int k;
    bool adjacent = false;

    for (k = 0; k < 8; k++) {
        if (gap_void_at(gx + ndx[k], gy + ndy[k])) {
            adjacent = true;
            break;
        }
    }
    if (!adjacent) {
        return false;
    }

    e = gap_dark_obtain(sec_id);
    if (gap_dark_bit(e, index)) {
        return false;
    }
    e->bits[index >> 3] |= (uint8_t)(1 << (index & 7));
    e->dirty = true;
    vslot = gap_vmask_slot_find(sec_id);
    if (vslot >= 0 && gap_vmask_tab[vslot].v[index] == 0) {
        gap_vmask_tab[vslot].v[index] = 1; // verified black joins the void
    }
    return true;
}

// Apply pending dark marks: invalidate the fade field of each touched sector's 3x3
// block so it rebuilds seeing the new void, and force a full-window repaint — the
// renderer is dirty-rect driven, so without this the healed fade never reaches the
// pixels already on screen until the user happens to scroll them.
void void_edge_fade_flush_dark(void)
{
    bool any = false;
    int s;
    for (s = 0; s < DARK_SLOTS; s++) {
        if (gap_dark_tab[s].used && gap_dark_tab[s].dirty) {
            int sx = SECTOR_X(gap_dark_tab[s].id);
            int sy = SECTOR_Y(gap_dark_tab[s].id);
            int dx, dy;
            gap_dark_tab[s].dirty = false;
            any = true;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    gap_fade_invalidate(SECTOR_MAKE(sx + dx, sy + dy));
                }
            }
        }
    }
    if (any && gap_invalidate_rect != NULL) {
        gap_invalidate_rect(NULL);
    }
}

// True if the draw-time scan has already verified this tile renders black. Lets
// the draw loop skip re-probing settled tiles every frame.
bool void_edge_fade_dark_marked(int64_t sec_id, int index)
{
    const GapDarkEntry* e = gap_dark_find(sec_id);
    return e != NULL && gap_dark_bit(e, index);
}

// Extended void mask = this sector's 64x64 plus a FADE_RAD-wide border sampled
// from the 8 neighbor sectors (their real masks from the load-time table; an
// off-map / not-yet-loaded neighbor reads as void). Convolving this carries the
// fade across every sector seam, in-bounds or off-map.
#define EXT_DIM (GAP_SECTOR_DIM + 2 * FADE_RAD)

// Local-feather reach in screen px (~4 tiles): the guaranteed minimum fade
// distance around ANY void tile, regardless of how small the void patch is.
#define FEATHER_PX 170

static int gap_fade_slot(int64_t sec_id, Sector* sector)
{
    static uint8_t ext[EXT_DIM * EXT_DIM];
    static int ii[EXT_DIM + 1][EXT_DIM + 1];
    unsigned int slot = gap_sec_slot(sec_id, FADE_CACHE_SLOTS);
    const uint8_t* nbrv[3][3];
    const GapDarkEntry* nbrd[3][3];
    bool nbroff[3][3];
    const uint8_t* selfv;
    uint8_t* bt;
    bool any = false;
    int sx, sy, dx, dy, x, y, ex, ey;

    if (gap_fade_cache[slot].valid && gap_fade_cache[slot].sec_id == sec_id) {
        return (int)slot;
    }
    if (!gap_kernel_ready) {
        gap_build_kernel();
    }

    selfv = gap_vmask_get(sec_id);
    if (selfv == NULL) {
        gap_vmask_store(sec_id, sector);
        selfv = gap_vmask_get(sec_id);
    }

    sx = SECTOR_X(sec_id);
    sy = SECTOR_Y(sec_id);
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) {
                nbrv[1][1] = selfv;
                nbrd[1][1] = NULL;
                nbroff[1][1] = false;
            } else if (sx + dx < 0 || sy + dy < 0) {
                nbrv[dy + 1][dx + 1] = NULL;
                nbrd[dy + 1][dx + 1] = NULL;
                nbroff[dy + 1][dx + 1] = true;
            } else {
                int64_t nid = SECTOR_MAKE(sx + dx, sy + dy);
                nbrv[dy + 1][dx + 1] = gap_vmask_get(nid);
                // vmask not resident: fall back to the neighbor's persistent dark
                // bits rather than assuming solid terrain, so a known black area
                // still feeds this sector's fade across the seam.
                nbrd[dy + 1][dx + 1] = nbrv[dy + 1][dx + 1] == NULL ? gap_dark_find(nid) : NULL;
                nbroff[dy + 1][dx + 1] = gap_sector_off_map(sx + dx, sy + dy);
            }
        }
    }

    for (ey = 0; ey < EXT_DIM; ey++) {
        for (ex = 0; ex < EXT_DIM; ex++) {
            int lx = ex - FADE_RAD;
            int ly = ey - FADE_RAD;
            int cdx = lx < 0 ? -1 : (lx >= GAP_SECTOR_DIM ? 1 : 0);
            int cdy = ly < 0 ? -1 : (ly >= GAP_SECTOR_DIM ? 1 : 0);
            const uint8_t* nv = nbrv[cdy + 1][cdx + 1];
            uint8_t val;
            if (nv != NULL) {
                val = nv[((ly - cdy * GAP_SECTOR_DIM) << 6) | (lx - cdx * GAP_SECTOR_DIM)];
            } else if (nbrd[cdy + 1][cdx + 1] != NULL) {
                val = gap_dark_bit(nbrd[cdy + 1][cdx + 1],
                    ((ly - cdy * GAP_SECTOR_DIM) << 6) | (lx - cdx * GAP_SECTOR_DIM)) ? 1 : 0;
            } else {
                val = nbroff[cdy + 1][cdx + 1] ? 1 : 0;
            }
            ext[ey * EXT_DIM + ex] = val;
            if (val) {
                any = true;
            }
        }
    }

    gap_fade_cache[slot].has_void = any;
    bt = gap_fade_cache[slot].bright;

    if (any) {
        // Integral image (window early-out) + per-row prefix sums (row skip): a
        // tile's kernel pass only touches rows that actually contain void, and
        // the kernel weight total is a precomputed constant, so tiles near a
        // sparse edge pay only for the occupied rows.
        static uint16_t rowpre[EXT_DIM][EXT_DIM + 1];
        for (ex = 0; ex <= EXT_DIM; ex++) {
            ii[0][ex] = 0;
        }
        for (ey = 0; ey < EXT_DIM; ey++) {
            rowpre[ey][0] = 0;
            for (ex = 0; ex < EXT_DIM; ex++) {
                rowpre[ey][ex + 1] = (uint16_t)(rowpre[ey][ex] + (ext[ey * EXT_DIM + ex] != 0));
            }
        }
        for (ey = 1; ey <= EXT_DIM; ey++) {
            ii[ey][0] = 0;
            for (ex = 1; ex <= EXT_DIM; ex++) {
                ii[ey][ex] = (ext[(ey - 1) * EXT_DIM + (ex - 1)] != 0)
                    + ii[ey - 1][ex] + ii[ey][ex - 1] - ii[ey - 1][ex - 1];
            }
        }
        for (y = 0; y < GAP_SECTOR_DIM; y++) {
            for (x = 0; x < GAP_SECTOR_DIM; x++) {
                int ex0 = x + FADE_RAD;
                int ey0 = y + FADE_RAD;
                float sv = 0.0f;
                float dens, t;
                int du, dv;
                if (ii[ey0 + FADE_RAD + 1][ex0 + FADE_RAD + 1] - ii[ey0 - FADE_RAD][ex0 + FADE_RAD + 1]
                    - ii[ey0 + FADE_RAD + 1][ex0 - FADE_RAD] + ii[ey0 - FADE_RAD][ex0 - FADE_RAD] == 0) {
                    bt[(y << 6) | x] = 255;
                    continue;
                }
                for (dv = -FADE_RAD; dv <= FADE_RAD; dv++) {
                    int row = ey0 + dv;
                    const uint8_t* erow;
                    if (rowpre[row][ex0 + FADE_RAD + 1] == rowpre[row][ex0 - FADE_RAD]) {
                        continue; // no void anywhere in this kernel row
                    }
                    erow = &ext[row * EXT_DIM + ex0];
                    for (du = -FADE_RAD; du <= FADE_RAD; du++) {
                        if (erow[du] != 0) {
                            sv += gap_kernel[du + FADE_RAD][dv + FADE_RAD];
                        }
                    }
                }
                dens = sv / gap_kernel_wsum;
                t = (FADE_THRESH - dens) / FADE_THRESH; // 1 far from void .. 0 at the boundary
                if (t <= 0.0f) {
                    bt[(y << 6) | x] = 0;
                } else if (t >= 1.0f) {
                    bt[(y << 6) | x] = 255;
                } else {
                    t = t * t * (3.0f - 2.0f * t); // smoothstep
                    bt[(y << 6) | x] = (uint8_t)(t * 255.0f + 0.5f);
                }
            }
        }

        // Density alone ignores thin/isolated void: a 1-3 tile black strip never
        // reaches FADE_THRESH of the big kernel, so it produced NO fade at all —
        // measured directly ([EDGE]: marked tiles with bt=255). Overlay a short
        // distance ramp from every void tile so any black patch, however small,
        // feathers its surroundings. min() with the density field keeps the long
        // vignette near bulk void. Chamfer distance in screen px over the same
        // ext mask: steps +x/+y are 45px (iso 2:1 diagonal), the (+x,+y) screen-
        // vertical step is 40px, the (+x,-y) screen-horizontal step is 80px.
        {
            static int cd[EXT_DIM * EXT_DIM];
            int i, d;
            // Seed the feather only from void tiles with void COMPANY (>=2 of 8
            // neighbors also void): strips and bulk regions qualify, but a lone
            // decorative blank tile under a prop (e.g. a tombstone base) must not
            // cast a feather shadow into the play area around it.
            for (i = 0; i < EXT_DIM * EXT_DIM; i++) {
                cd[i] = 0x0FFFFFFF;
            }
            for (ey = 0; ey < EXT_DIM; ey++) {
                for (ex = 0; ex < EXT_DIM; ex++) {
                    int nvoid = 0;
                    int ddx, ddy;
                    if (!ext[ey * EXT_DIM + ex]) {
                        continue;
                    }
                    for (ddy = -1; ddy <= 1; ddy++) {
                        for (ddx = -1; ddx <= 1; ddx++) {
                            int nx = ex + ddx;
                            int ny = ey + ddy;
                            if ((ddx != 0 || ddy != 0)
                                && nx >= 0 && nx < EXT_DIM && ny >= 0 && ny < EXT_DIM
                                && ext[ny * EXT_DIM + nx]) {
                                nvoid++;
                            }
                        }
                    }
                    if (nvoid >= 2) {
                        cd[ey * EXT_DIM + ex] = 0;
                    }
                }
            }
            for (ey = 0; ey < EXT_DIM; ey++) {
                for (ex = 0; ex < EXT_DIM; ex++) {
                    int* c = &cd[ey * EXT_DIM + ex];
                    if (ex > 0 && cd[ey * EXT_DIM + ex - 1] + 45 < *c) {
                        *c = cd[ey * EXT_DIM + ex - 1] + 45;
                    }
                    if (ey > 0) {
                        if (cd[(ey - 1) * EXT_DIM + ex] + 45 < *c) {
                            *c = cd[(ey - 1) * EXT_DIM + ex] + 45;
                        }
                        if (ex > 0 && cd[(ey - 1) * EXT_DIM + ex - 1] + 40 < *c) {
                            *c = cd[(ey - 1) * EXT_DIM + ex - 1] + 40;
                        }
                        if (ex < EXT_DIM - 1 && cd[(ey - 1) * EXT_DIM + ex + 1] + 80 < *c) {
                            *c = cd[(ey - 1) * EXT_DIM + ex + 1] + 80;
                        }
                    }
                }
            }
            for (ey = EXT_DIM - 1; ey >= 0; ey--) {
                for (ex = EXT_DIM - 1; ex >= 0; ex--) {
                    int* c = &cd[ey * EXT_DIM + ex];
                    if (ex < EXT_DIM - 1 && cd[ey * EXT_DIM + ex + 1] + 45 < *c) {
                        *c = cd[ey * EXT_DIM + ex + 1] + 45;
                    }
                    if (ey < EXT_DIM - 1) {
                        if (cd[(ey + 1) * EXT_DIM + ex] + 45 < *c) {
                            *c = cd[(ey + 1) * EXT_DIM + ex] + 45;
                        }
                        if (ex < EXT_DIM - 1 && cd[(ey + 1) * EXT_DIM + ex + 1] + 40 < *c) {
                            *c = cd[(ey + 1) * EXT_DIM + ex + 1] + 40;
                        }
                        if (ex > 0 && cd[(ey + 1) * EXT_DIM + ex - 1] + 80 < *c) {
                            *c = cd[(ey + 1) * EXT_DIM + ex - 1] + 80;
                        }
                    }
                }
            }
            for (y = 0; y < GAP_SECTOR_DIM; y++) {
                for (x = 0; x < GAP_SECTOR_DIM; x++) {
                    d = cd[(y + FADE_RAD) * EXT_DIM + (x + FADE_RAD)];
                    if (d < FEATHER_PX) {
                        float lt = (float)d / (float)FEATHER_PX;
                        int lb;
                        lt = lt * lt * (3.0f - 2.0f * lt);
                        lb = (int)(lt * 255.0f + 0.5f);
                        if (lb < bt[(y << 6) | x]) {
                            bt[(y << 6) | x] = (uint8_t)lb;
                        }
                    }
                }
            }
        }
    }

    gap_fade_cache[slot].sec_id = sec_id;
    gap_fade_cache[slot].valid = true;
    return (int)slot;
}

// Average per-tile brightness over the (up to 4) in-sector tiles meeting at a
// vertex. The density field is already smooth, so this bilinear sample gives a
// seamless per-vertex gradient.
static int gap_vert(const uint8_t* bt, int ax, int ay, int bx, int by, int cx, int cy, int ex, int ey)
{
    int sum = 0;
    int n = 0;
    if (ax >= 0 && ax < GAP_SECTOR_DIM && ay >= 0 && ay < GAP_SECTOR_DIM) { sum += bt[(ay << 6) | ax]; n++; }
    if (bx >= 0 && bx < GAP_SECTOR_DIM && by >= 0 && by < GAP_SECTOR_DIM) { sum += bt[(by << 6) | bx]; n++; }
    if (cx >= 0 && cx < GAP_SECTOR_DIM && cy >= 0 && cy < GAP_SECTOR_DIM) { sum += bt[(cy << 6) | cx]; n++; }
    if (ex >= 0 && ex < GAP_SECTOR_DIM && ey >= 0 && ey < GAP_SECTOR_DIM) { sum += bt[(ey << 6) | ex]; n++; }
    return n > 0 ? sum / n : 255;
}

bool void_edge_fade_fade_factors(int64_t sec_id, Sector* sector, int index, unsigned char out_factor[9])
{
    const uint8_t* bt;
    int slot;
    int tx, ty;
    int bCtr, bTop, bRight, bLeft, bBot;
    int i;

    for (i = 0; i < 9; i++) {
        out_factor[i] = 255;
    }

    if (sector == NULL || !void_edge_fade_fade_enabled()) {
        return false;
    }

    slot = gap_fade_slot(sec_id, sector);
    if (!gap_fade_cache[slot].has_void) {
        return false;
    }
    bt = gap_fade_cache[slot].bright;

    tx = index & (GAP_SECTOR_DIM - 1);
    ty = index >> 6;

    bCtr = bt[index];
    // Each diamond vertex samples the 4 tiles meeting there. Screen mapping:
    // +tx=down-left, +ty=down-right, so:
    //   TOP    : (tx,ty),(tx,ty-1),(tx-1,ty),(tx-1,ty-1)
    //   RIGHT  : (tx,ty),(tx,ty+1),(tx-1,ty),(tx-1,ty+1)
    //   LEFT   : (tx,ty),(tx,ty-1),(tx+1,ty),(tx+1,ty-1)
    //   BOTTOM : (tx,ty),(tx,ty+1),(tx+1,ty),(tx+1,ty+1)
    bTop   = gap_vert(bt, tx, ty, tx, ty - 1, tx - 1, ty, tx - 1, ty - 1);
    bRight = gap_vert(bt, tx, ty, tx, ty + 1, tx - 1, ty, tx - 1, ty + 1);
    bLeft  = gap_vert(bt, tx, ty, tx, ty - 1, tx + 1, ty, tx + 1, ty - 1);
    bBot   = gap_vert(bt, tx, ty, tx, ty + 1, tx + 1, ty, tx + 1, ty + 1);

    if (bCtr == 255 && bTop == 255 && bRight == 255 && bLeft == 255 && bBot == 255) {
        return false;
    }

    // v51 3x3: [0]TL [1]TOP [2]TR / [3]LEFT [4]CTR [5]RIGHT / [6]BL [7]BOTTOM [8]BR.
    out_factor[4] = (uint8_t)bCtr;
    out_factor[1] = (uint8_t)bTop;
    out_factor[5] = (uint8_t)bRight;
    out_factor[3] = (uint8_t)bLeft;
    out_factor[7] = (uint8_t)bBot;
    out_factor[0] = (uint8_t)((bTop + bLeft) / 2);
    out_factor[2] = (uint8_t)((bTop + bRight) / 2);
    out_factor[6] = (uint8_t)((bLeft + bBot) / 2);
    out_factor[8] = (uint8_t)((bRight + bBot) / 2);
    return true;
}

// At sector load: record this sector's void mask and invalidate the cached fade
// fields of the 3x3 block around it, so neighbors rebuild seeing this sector's
// void (and this sector sees theirs) — the fade carries across the seam
// regardless of the order sectors stream in. The void tiles themselves are left
// untouched (they render black); the fade feathers the content around them.
void void_edge_fade_sector(int64_t sec_id, Sector* sector)
{
    int sx;
    int sy;
    int ddx, ddy;

    if (!void_edge_fade_enabled()) {
        return;
    }

    sx = SECTOR_X(sec_id);
    sy = SECTOR_Y(sec_id);
    gap_vmask_store(sec_id, sector);
    for (ddy = -1; ddy <= 1; ddy++) {
        for (ddx = -1; ddx <= 1; ddx++) {
            gap_fade_invalidate(SECTOR_MAKE(sx + ddx, sy + ddy));
        }
    }
}

// ============================================================================
// TEMP ALL-MAPS STRUCTURAL AUDIT
// Run with ARCANUM_VOID_FADE_AUDIT=1. Mounts every module group under modules/,
// reads every sector of every map with the engine's own file layer, classifies
// tiles (gap / facade / terrain), floods void regions across sector seams, and
// reports edge-case classes for the void-edge fade. Writes /tmp/void_fade_audit.txt
// and exits.
// ============================================================================

typedef struct AuditSec {
    int64_t sx;
    int64_t sy;
    uint8_t* cls; // 4096 cells: 0=terrain, 1=gap/void, 2=facade-type art; |0x80 visited
} AuditSec;

static AuditSec* aud_secs;
static int aud_count;
static int aud_cap;
static int* aud_hash; // open-addressed, value=index+1
static int aud_hash_cap;

static int aud_find(int64_t sx, int64_t sy)
{
    unsigned int h;
    if (aud_hash_cap == 0) {
        return -1;
    }
    h = (unsigned int)((uint64_t)sx * 73856093u ^ (uint64_t)sy * 19349663u) & (aud_hash_cap - 1);
    while (aud_hash[h] != 0) {
        int i = aud_hash[h] - 1;
        if (aud_secs[i].sx == sx && aud_secs[i].sy == sy) {
            return i;
        }
        h = (h + 1) & (aud_hash_cap - 1);
    }
    return -1;
}

static void aud_build_hash(void)
{
    int i;
    aud_hash_cap = 64;
    while (aud_hash_cap < aud_count * 2) {
        aud_hash_cap <<= 1;
    }
    aud_hash = (int*)calloc(aud_hash_cap, sizeof(int));
    for (i = 0; i < aud_count; i++) {
        unsigned int h = (unsigned int)((uint64_t)aud_secs[i].sx * 73856093u ^ (uint64_t)aud_secs[i].sy * 19349663u) & (aud_hash_cap - 1);
        while (aud_hash[h] != 0) {
            h = (h + 1) & (aud_hash_cap - 1);
        }
        aud_hash[h] = i + 1;
    }
}

// Class at global tile coords; -1 = sector not present in this map's data.
static int aud_cls_at(int64_t gx, int64_t gy)
{
    int i;
    if (gx < 0 || gy < 0) {
        return -1;
    }
    i = aud_find(gx >> 6, gy >> 6);
    if (i < 0) {
        return -1;
    }
    return aud_secs[i].cls[((gy & 63) << 6) | (gx & 63)] & 0x3F;
}

typedef struct AuditTotals {
    int maps;
    int sectors;
    int gap_tiles;
    int regions;
    int regions_offarea;   // attached to a large facade field (true off-area)
    int regions_mixed;     // touches both terrain and facade, large super-region
    int specks;            // terrain-enclosed, size 1-2 (feather-suppressed)
    int interior_risk;     // terrain-enclosed, size >= 3 (feather shadow risk)
    int prop_holes;        // gap inside a SMALL facade cluster amid terrain (prop)
    int prop_hole_max;     // largest gap-region size among prop holes
} AuditTotals;

// Flood the connected {gap|facade} super-region from (gx,gy); returns its size,
// capped at `cap` (a hit of the cap means "large" — true off-area shell). Uses
// cls bit 0x40 as the super-visited flag.
static int aud_super_size(int64_t gx, int64_t gy, int64_t* queue, int cap)
{
    int head = 0;
    int tail = 0;
    int size = 0;
    int si = aud_find(gx >> 6, gy >> 6);
    int ti = (int)(((gy & 63) << 6) | (gx & 63));
    if (si < 0 || (aud_secs[si].cls[ti] & 0x40) != 0) {
        return 0;
    }
    aud_secs[si].cls[ti] |= 0x40;
    queue[tail++] = (gx << 32) | (uint32_t)gy;
    while (head < tail) {
        int64_t cur = queue[head++];
        int64_t cx = cur >> 32;
        int64_t cy = (int32_t)(cur & 0xFFFFFFFF);
        static const int ddx[4] = { 1, -1, 0, 0 };
        static const int ddy[4] = { 0, 0, 1, -1 };
        int k;
        size++;
        if (size >= cap) {
            return size;
        }
        for (k = 0; k < 4; k++) {
            int64_t nx = cx + ddx[k];
            int64_t ny = cy + ddy[k];
            int nsi;
            int nti;
            if (nx < 0 || ny < 0) {
                continue;
            }
            nsi = aud_find(nx >> 6, ny >> 6);
            if (nsi < 0) {
                continue;
            }
            nti = (int)(((ny & 63) << 6) | (nx & 63));
            if ((aud_secs[nsi].cls[nti] & 0x7F & 0x3F) != 0 // gap or facade
                && (aud_secs[nsi].cls[nti] & 0x40) == 0
                && tail < 1024 * 1024) {
                aud_secs[nsi].cls[nti] |= 0x40;
                queue[tail++] = (nx << 32) | (uint32_t)ny;
            }
        }
    }
    return size;
}

static void aud_audit_map(FILE* rep, const char* group, const char* map, AuditTotals* tot)
{
    char pat[TIG_MAX_PATH];
    char path[TIG_MAX_PATH];
    TigFileList fl;
    static uint32_t arts[GAP_TILES_PER_SECTOR];
    int64_t* queue = NULL;
    unsigned int fi;
    int i, t;
    int map_regions = 0;
    int map_risk = 0;

    snprintf(pat, sizeof(pat), "maps\\%s\\*.sec", map);
    tig_file_list_create(&fl, pat);
    if (fl.count == 0) {
        tig_file_list_destroy(&fl);
        return;
    }

    aud_secs = (AuditSec*)malloc(fl.count * sizeof(AuditSec));
    aud_count = 0;

    for (fi = 0; fi < fl.count; fi++) {
        uint64_t id = strtoull(fl.entries[fi].path, NULL, 10);
        TigFile* f;
        int cnt;
        snprintf(path, sizeof(path), "maps\\%s\\%s", map, fl.entries[fi].path);
        f = tig_file_fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        if (tig_file_fread(&cnt, sizeof(cnt), 1, f) != 1
            || tig_file_fseek(f, (long)cnt * 48, SEEK_CUR) != 0
            || tig_file_fread(arts, sizeof(uint32_t), GAP_TILES_PER_SECTOR, f) != GAP_TILES_PER_SECTOR) {
            tig_file_fclose(f);
            continue;
        }
        tig_file_fclose(f);

        {
            uint8_t* cls = (uint8_t*)malloc(GAP_TILES_PER_SECTOR);
            for (t = 0; t < GAP_TILES_PER_SECTOR; t++) {
                tig_art_id_t aid = (tig_art_id_t)arts[t];
                cls[t] = gap_is_gap(aid) ? 1
                    : (tig_art_type(aid) != TIG_ART_TYPE_TILE ? 2 : 0);
            }
            aud_secs[aud_count].sx = (int64_t)SECTOR_X((int64_t)id);
            aud_secs[aud_count].sy = (int64_t)SECTOR_Y((int64_t)id);
            aud_secs[aud_count].cls = cls;
            aud_count++;
        }
    }
    tig_file_list_destroy(&fl);

    if (aud_count == 0) {
        free(aud_secs);
        aud_secs = NULL;
        return;
    }

    aud_build_hash();
    queue = (int64_t*)malloc((size_t)aud_count * GAP_TILES_PER_SECTOR * sizeof(int64_t) / 8);
    // queue worst case: all gap tiles; allocate lazily grown instead
    free(queue);
    queue = (int64_t*)malloc(1024 * 1024 * sizeof(int64_t));

    tot->maps++;
    tot->sectors += aud_count;

    for (i = 0; i < aud_count; i++) {
        for (t = 0; t < GAP_TILES_PER_SECTOR; t++) {
            int size, cT, cF, cMiss;
            int head, tail;
            int64_t g0x, g0y;
            if ((aud_secs[i].cls[t] & 0x3F) != 1 || (aud_secs[i].cls[t] & 0x80) != 0) {
                continue;
            }
            // BFS this region
            size = 0;
            cT = 0;
            cF = 0;
            cMiss = 0;
            head = 0;
            tail = 0;
            g0x = aud_secs[i].sx * 64 + (t & 63);
            g0y = aud_secs[i].sy * 64 + (t >> 6);
            queue[tail++] = (g0x << 32) | (uint32_t)g0y;
            aud_secs[i].cls[t] |= 0x80;
            while (head < tail) {
                int64_t cur = queue[head++];
                int64_t cx = cur >> 32;
                int64_t cy = (int32_t)(cur & 0xFFFFFFFF);
                static const int ddx[4] = { 1, -1, 0, 0 };
                static const int ddy[4] = { 0, 0, 1, -1 };
                int k;
                size++;
                for (k = 0; k < 4; k++) {
                    int64_t nx = cx + ddx[k];
                    int64_t ny = cy + ddy[k];
                    int c = aud_cls_at(nx, ny);
                    if (c == -1) {
                        cMiss++;
                    } else if (c == 0) {
                        cT++;
                    } else if (c == 2) {
                        cF++;
                    } else {
                        // unvisited gap -> enqueue
                        int si = aud_find(nx >> 6, ny >> 6);
                        int ti = (int)(((ny & 63) << 6) | (nx & 63));
                        if ((aud_secs[si].cls[ti] & 0x80) == 0 && tail < 1024 * 1024) {
                            aud_secs[si].cls[ti] |= 0x80;
                            queue[tail++] = (nx << 32) | (uint32_t)ny;
                        }
                    }
                }
            }
            tot->regions++;
            tot->gap_tiles += size;
            map_regions++;
            if (cF == 0 && cMiss == 0 && cT > 0) {
                if (size <= 2) {
                    tot->specks++;
                } else {
                    tot->interior_risk++;
                    map_risk++;
                    fprintf(rep, "RISK interior-void group=%s map=%s size=%d at=(%lld,%lld) terrain-contact=%d\n",
                        group, map, size, (long long)g0x, (long long)g0y, cT);
                }
            } else {
                // Distinguish a gap inside a SMALL facade cluster amid terrain (a
                // decorative prop hole, e.g. a tombstone base) from true off-area
                // (gap inside a huge facade shell): flood the connected gap+facade
                // super-region and look at its size.
                int super = aud_super_size(g0x, g0y, queue, 256);
                if (super > 0 && super < 64) {
                    tot->prop_holes++;
                    if (size > tot->prop_hole_max) {
                        tot->prop_hole_max = size;
                    }
                    if (size >= 3) {
                        map_risk++;
                        fprintf(rep, "RISK prop-hole group=%s map=%s gapsize=%d super=%d at=(%lld,%lld)\n",
                            group, map, size, super, (long long)g0x, (long long)g0y);
                    }
                } else if (cT > 0) {
                    tot->regions_mixed++;
                } else {
                    tot->regions_offarea++;
                }
            }
        }
    }

    if (map_regions > 0) {
        fprintf(rep, "map group=%s name=%s sectors=%d regions=%d risk=%d\n",
            group, map, aud_count, map_regions, map_risk);
    }

    for (i = 0; i < aud_count; i++) {
        free(aud_secs[i].cls);
    }
    free(aud_secs);
    aud_secs = NULL;
    free(aud_hash);
    aud_hash = NULL;
    aud_hash_cap = 0;
    aud_count = 0;
    free(queue);
}

void void_edge_fade_run_audit(void)
{
    FILE* rep = fopen("/tmp/void_fade_audit.txt", "w");
    // The classifier needs art-id -> filename resolution (tig_art_exists), which
    // is gated on the Name system. Initialize it directly — the audit runs before
    // the normal game-systems init (so the loading screen's blocking SwapWindow
    // is never reached). name_init ignores its init_info.
    if (!name_init(NULL)) {
        fprintf(rep, "ERROR: name_init failed — classifications would be garbage\n");
        fclose(rep);
        exit(1);
    }
    TigFileList ml;
    char groups[64][64];
    int ngroups = 0;
    unsigned int mi;
    int gi;
    AuditTotals tot;

    memset(&tot, 0, sizeof(tot));
    if (rep == NULL) {
        exit(1);
    }
    fprintf(rep, "=== void_edge_fade all-maps audit ===\n");

    tig_file_list_create(&ml, "modules\\*");
    for (mi = 0; mi < ml.count; mi++) {
        char base[64];
        const char* dot;
        size_t n;
        int g;
        if (ml.entries[mi].path[0] == '.') {
            continue;
        }
        dot = strchr(ml.entries[mi].path, '.');
        n = dot != NULL ? (size_t)(dot - ml.entries[mi].path) : strlen(ml.entries[mi].path);
        if (n >= sizeof(base)) {
            n = sizeof(base) - 1;
        }
        memcpy(base, ml.entries[mi].path, n);
        base[n] = '\0';
        for (g = 0; g < ngroups; g++) {
            if (strcmp(groups[g], base) == 0) {
                break;
            }
        }
        if (g == ngroups && ngroups < 64) {
            strcpy(groups[ngroups++], base);
        }
    }

    for (gi = 0; gi < ngroups; gi++) {
        char added[16][TIG_MAX_PATH];
        int nadded = 0;
        TigFileList maps;
        unsigned int k;

        for (mi = 0; mi < ml.count; mi++) {
            const char* dot = strchr(ml.entries[mi].path, '.');
            size_t n = dot != NULL ? (size_t)(dot - ml.entries[mi].path) : strlen(ml.entries[mi].path);
            if (n == strlen(groups[gi]) && strncmp(ml.entries[mi].path, groups[gi], n) == 0 && nadded < 16) {
                snprintf(added[nadded], TIG_MAX_PATH, "modules\\%s", ml.entries[mi].path);
                if (tig_file_repository_add(added[nadded])) {
                    nadded++;
                }
            }
        }

        fprintf(rep, "--- module group: %s (mounted %d) ---\n", groups[gi], nadded);

        tig_file_list_create(&maps, "maps\\*");
        for (k = 0; k < maps.count; k++) {
            if ((maps.entries[k].attributes & TIG_FILE_ATTRIBUTE_SUBDIR) != 0
                && maps.entries[k].path[0] != '.') {
                aud_audit_map(rep, groups[gi], maps.entries[k].path, &tot);
            }
        }
        tig_file_list_destroy(&maps);

        while (nadded > 0) {
            nadded--;
            tig_file_repository_remove(added[nadded]);
        }
    }
    tig_file_list_destroy(&ml);

    fprintf(rep, "=== totals: maps=%d sectors=%d gap_tiles=%d regions=%d offarea=%d mixed=%d specks=%d INTERIOR_RISK=%d prop_holes=%d prop_hole_max=%d ===\n",
        tot.maps, tot.sectors, tot.gap_tiles, tot.regions,
        tot.regions_offarea, tot.regions_mixed, tot.specks, tot.interior_risk,
        tot.prop_holes, tot.prop_hole_max);
    fclose(rep);
    exit(0);
}
