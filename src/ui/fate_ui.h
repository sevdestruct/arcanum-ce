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

#endif /* ARCANUM_UI_FATE_UI_H_ */
