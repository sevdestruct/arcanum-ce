#ifndef ARCANUM_UI_FATE_UI_H_
#define ARCANUM_UI_FATE_UI_H_

#include "game/context.h"

bool fate_ui_init(GameInitInfo* init_info);
void fate_ui_reset(void);
void fate_ui_exit(void);
void fate_ui_toggle(int64_t obj);
void fate_ui_close(void);
// CE: Re-snap the fate window to its docked-below-top-bar position
// after the TAB HUD-crop toggle changes the top bar's visibility.
// No-op when the panel isn't currently open.
void fate_ui_reposition(void);

// CE: per-frame integrator — reads the spring-tweened slide offset
// and calls tig_window_move so the panel slides down on appear / up
// on dismiss. Also tracks the dismiss-when-settled state and fires
// the actual tig_window_destroy when the slide-up tween finishes.
// Cheap no-op when the panel isn't open and not pending dismiss.
void fate_ui_ping(void);

#endif /* ARCANUM_UI_FATE_UI_H_ */
