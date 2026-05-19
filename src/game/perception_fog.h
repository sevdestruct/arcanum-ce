#ifndef ARCANUM_GAME_PERCEPTION_FOG_H_
#define ARCANUM_GAME_PERCEPTION_FOG_H_

#include "tig/video.h"
#include "game/context.h"

/* Keys for arcanum.cfg (read/written via the shared settings system). */
#define PERCEPTION_FOG_ENABLED_KEY      "perception fog"
#define PERCEPTION_FOG_BLUR_KEY         "perception fog blur"
#define PERCEPTION_FOG_ALPHA_KEY        "perception fog alpha"
#define PERCEPTION_FOG_INNER_KEY        "perception fog inner"
#define PERCEPTION_FOG_OUTER_KEY        "perception fog outer"
#define PERCEPTION_FOG_BLUR_RADIUS_KEY  "perception fog blur radius"
#define PERCEPTION_FOG_DIM_KEY          "perception fog dim"

/**
 * Initialise the perception fog module.
 *
 * Called once from gamelib_init with the GameInitInfo that holds window
 * dimensions.  Allocates the alpha-mask and blur scratch buffers sized to
 * the initial viewport.
 */
bool perception_fog_init(GameInitInfo* init_info);

/**
 * Release all resources owned by the perception fog module.
 */
void perception_fog_exit(void);

/**
 * Recreate internal buffers when the viewport is resized.
 */
void perception_fog_resize(GameResizeInfo* resize_info);

/**
 * Returns true when the perception fog is currently enabled via arcanum.cfg.
 * Used by gamelib.c to decide whether to request a full-viewport refresh.
 */
bool perception_fog_is_enabled(void);

/**
 * Composite the perception fog over game_vb.
 *
 * game_vb should be the final composited game frame (gamelib_iso_window_vb)
 * in its normal viewport coordinate space (i.e. after zoom scaling if any).
 *
 * Does nothing when PerceptionFog=0 in config or ScrollDist=0.
 */
void perception_fog_draw(TigVideoBuffer* game_vb);

/**
 * Mark the fog ellipse as dirty so it is regenerated on the next draw call.
 *
 * Call this when:
 *   - The player moves (scroll_set_center)
 *   - Zoom level changes (iso_zoom_ping)
 *   - The perception stat changes (iso_zoom_update_perception_floor detects a
 *     change in the leash size)
 *   - The viewport is resized (perception_fog_resize already handles this)
 */
void perception_fog_mark_dirty(void);

#endif /* ARCANUM_GAME_PERCEPTION_FOG_H_ */
