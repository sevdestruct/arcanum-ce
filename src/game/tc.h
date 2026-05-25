#ifndef ARCANUM_GAME_TC_H_
#define ARCANUM_GAME_TC_H_

#include "game/context.h"

bool tc_init(GameInitInfo* init_info);
void tc_exit(void);
void tc_resize(GameResizeInfo* resize_info);
void tc_draw(GameDrawInfo* draw_info);
void tc_scroll(int dx, int dy);
void tc_show(void);
void tc_hide(void);
void tc_clear(bool compact);
void tc_set_option(int index, const char* str);
int tc_handle_message(TigMessage* msg);
int tc_check_size(const char* str);
bool tc_is_active(void);
TigRect tc_get_content_rect(void);

// CE: shift the dialog options backdrop down by `offset` design-coord
// pixels — used to reclaim half the visual real-estate that the cropped
// HUD bar leaves behind. Pushed from intgame's HUD-stage transitions.
// 0 = original position (just above the full bar). Higher = lower.
//
// The offset is tweened via ui_anim_int_to so rapid TAB-HUD stage
// cycling produces a smooth glide rather than a snap. tc_ping (called
// each frame from gamelib_draw) picks up the interpolated value,
// recomputes the backdrop rect, and invalidates the affected screen
// region so the iso world repaints underneath.
void tc_set_bottom_gap_offset(int offset);

// CE: per-frame integrator hook — recomputes the dialog backdrop's
// vertical position from the (possibly mid-tween) gap-offset value
// and invalidates the screen region if the rect moved. Cheap when no
// tween is active (early return on value-unchanged).
void tc_ping(void);

// CE: true while the bottom-gap offset is mid-tween toward a new target.
// Used by tb to inhibit pin-state transitions during UI animations —
// without this the bubble can briefly find space below TC (or lose it)
// while the dialog box is sliding, flipping pin choice and jumping
// the bubble back and forth.
bool tc_is_settling(void);

#endif /* ARCANUM_GAME_TC_H_ */
