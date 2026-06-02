#ifndef ARCANUM_GAME_TILE_GAP_FILL_H_
#define ARCANUM_GAME_TILE_GAP_FILL_H_

#include "game/context.h"
#include "game/sector.h"

bool tile_gap_fill_init(GameInitInfo* init_info);
void tile_gap_fill_exit(void);

void tile_gap_fill_sector(int64_t sec_id, Sector* sector);

// Synthesize terrain art for a tile at global tile coords (tx, ty) that lies in
// an out-of-bounds sector, sampled from the nearest in-bounds sector. Tile coords
// may be negative (pre-origin). Returns TIG_ART_ID_INVALID for in-bounds tiles.
tig_art_id_t tile_gap_fill_synth_tile(int64_t tx, int64_t ty);

bool tile_gap_fill_enabled(void);

#endif
