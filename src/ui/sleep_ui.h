#ifndef ARCANUM_UI_SLEEP_UI_H_
#define ARCANUM_UI_SLEEP_UI_H_

#include "game/context.h"
#include "game/timeevent.h"

bool sleep_ui_init(GameInitInfo* init_info);
void sleep_ui_exit(void);
void sleep_ui_reset(void);
void sleep_ui_toggle(int64_t bed_obj);
void sleep_ui_close(void);
// CE: Re-snap the sleep window to its docked-below-top-bar position
// after the TAB HUD-crop toggle changes the top bar's visibility.
// No-op when the panel isn't currently open.
void sleep_ui_reposition(void);
bool sleep_ui_is_active(void);
bool sleep_ui_process_callback(TimeEvent* timeevent);

// CE: per-frame integrator — reads the spring-tweened slide offset
// and calls tig_window_move so the panel slides down on appear / up
// on dismiss. Mirrors fate_ui_ping. Cheap no-op when the panel isn't
// open and not pending dismiss.
void sleep_ui_ping(void);

#endif /* ARCANUM_UI_SLEEP_UI_H_ */
