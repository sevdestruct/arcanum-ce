#ifndef ARCANUM_GAME_TILE_GAP_FILL_H_
#define ARCANUM_GAME_TILE_GAP_FILL_H_

#include "game/context.h"
#include "game/sector.h"

// Void-edge fade: feather visible content into the black void at map edges and
// off-area boundaries, so the zoomed-out camera never shows a hard jagged cut.
//
// Sources of "void": true gaps in the sector data (missing art, the terrain_fill
// placeholder, authored Black-Yall/void tiles) recorded at sector load, plus
// draw-time-verified rendered-black tiles (the unfinished black.ART off-area
// facades) annexed by contiguity — a black tile only joins the void if it
// touches existing void, so the void floods along connected off-area regions
// while isolated black content (burnt wrecks, cave shadows, dark art) never
// seeds a fade. Roof-covered void is excluded (hidden, not "edge to nothing").
//
// The fade itself is a per-sector brightness field: void density blurred over a
// screen-round kernel (long vignette near bulk void, rounded corners), combined
// with a short chamfer-distance feather so even thin black strips soften their
// surroundings. tile_draw_iso multiplies the field into each tile's per-vertex
// lighting; indoor-type tiles never fade (caves/dungeons keep hard edges).

bool tile_gap_fill_init(GameInitInfo* init_info);
void tile_gap_fill_exit(void);

bool tile_gap_fill_enabled(void);

// Settings-changed hook for VOID_EDGE_FADE_KEY (live toggle + repaint).
void tile_gap_fill_settings_changed(void);

// True when the void-edge fade should run (enabled, not editor).
bool tile_gap_fill_fade_enabled(void);

// At sector load: record the sector's void mask and invalidate the fade fields
// of the surrounding 3x3 so the fade carries across seams.
void tile_gap_fill_sector(int64_t sec_id, Sector* sector);

// Per-vertex fade brightness for the tile at `index` in `sector`'s 64x64 tile
// list. Fills out_factor[9] (one per v51 vertex-grid cell, row-major 3x3) with
// 0=black .. 255=unchanged. Returns true if any factor < 255 (the caller should
// multiply v51 and force the lerp blit). Cheap after the per-sector field is
// built (cached) — safe in the draw loop.
bool tile_gap_fill_fade_factors(int64_t sec_id, Sector* sector, int index, unsigned char out_factor[9]);

// Draw-time "renders black" feedback: the draw loop reports tiles whose rendered
// pixels are pure black; if contiguous with existing void they're annexed so the
// fade feathers from them. flush_dark applies pending marks once per frame and
// repaints. dark_marked lets the scan skip already-settled tiles.
bool tile_gap_fill_note_dark(int64_t sec_id, int index);
bool tile_gap_fill_dark_marked(int64_t sec_id, int index);
void tile_gap_fill_flush_dark(void);

// Offline all-maps structural audit (run with ARCANUM_TGF_AUDIT=1). Mounts every
// module group, classifies every sector of every map, floods void regions, and
// reports fade edge-case classes to /tmp/tgf_audit.txt, then exits the process.
// Must run before any window display (gamelib_init hooks it after data load).
void tile_gap_fill_run_audit(void);

#endif
