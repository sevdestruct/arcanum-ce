#ifndef ARCANUM_GAME_CE_SPRITE_H_
#define ARCANUM_GAME_CE_SPRITE_H_

#include "tig/tig.h"

// CE engine-side composite-sprite registry.
//
// Builds NAMED sprites by compositing slices of EXISTING .dat artwork (and/or
// other named sprites) into offscreen video buffers — slice, horizontal/vertical
// flip, color-key transparency, center-relative placement, back-to-front
// layering. Results are reusable and nestable (a layer's source may be another
// named sprite), and the names are stable handles for later mod / custom-UI
// replacement. No derived art is shipped; everything is recomposed at runtime
// from art already in the .dat files.
//
// This is the foundation for engine-side sprite/UI hacking (variant coin stacks
// are its first client; a chopped-up UI-component library is a future one).

#ifdef __cplusplus
extern "C" {
#endif

// One compositing layer. Exactly one source: either another named sprite
// (`src_sprite` non-NULL) or a real art id (`src_sprite` NULL → `src_art`).
typedef struct CeSpriteLayer {
    const char* src_sprite; // name of another ce_sprite, or NULL
    tig_art_id_t src_art;   // art id, used when src_sprite == NULL
    // Source slice in source pixels. sw/sh == 0 means "use the full source".
    int sx;
    int sy;
    int sw;
    int sh;
    bool flip_x;
    bool flip_y;
    // Placement: the slice is centered on the canvas, then shifted by
    // (off_x, off_y). Center-first convention (avoid odd deltas to dodge
    // rounding). Positive x = right, positive y = down.
    int off_x;
    int off_y;
} CeSpriteLayer;

// Define (or redefine) a named sprite of canvas_w x canvas_h, composited from
// layers[0..count) back-to-front. Returns false on failure (sprite is then
// left undefined). Allocates a video buffer — define once, not per frame.
bool ce_sprite_define(const char* name, int canvas_w, int canvas_h,
    const CeSpriteLayer* layers, int count);

// True if a sprite with this name is currently defined.
bool ce_sprite_exists(const char* name);

// Pixel size of a defined sprite (sets 0,0 if undefined).
void ce_sprite_size(const char* name, int* width, int* height);

// Draw a defined sprite into a window with its top-left at (x, y), honoring
// color-key transparency. alpha 255 = opaque. No-op if undefined.
void ce_sprite_draw(tig_window_handle_t window_handle, const char* name,
    int x, int y, uint8_t alpha);

// Underlying video buffer of a defined sprite (for nesting / advanced use);
// NULL if undefined.
TigVideoBuffer* ce_sprite_vbuffer(const char* name);

// Free every registered sprite (call at engine shutdown).
void ce_sprite_shutdown(void);

// Reserve a stable art id backed by a raw .dat art PATH (e.g.
// "art\\item\\i_coins00.art"). Lets us address art that has no entry in the
// item/critter name tables — superseded assets that still live in the .dat —
// by loading the .ART directly via the path. Idempotent per path. Returns
// TIG_ART_ID_INVALID if the registry is full. The art is loaded lazily by the
// engine on first use (guard call sites with tig_file_exists for the path).
tig_art_id_t ce_named_art(const char* art_path);

// Path resolver hook: if `aid` is one of our reserved ids, fill `path` and
// return true. Wired into the game's art file-path resolver (name_resolve_path).
bool ce_named_art_resolve(tig_art_id_t aid, char* path, size_t maxlen);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_GAME_CE_SPRITE_H_ */
