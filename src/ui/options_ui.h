#ifndef ARCANUM_UI_OPTIONS_UI_H_
#define ARCANUM_UI_OPTIONS_UI_H_

#include "game/context.h"

typedef enum OptionsUiTab {
    OPTIONS_UI_TAB_GAME,
    OPTIONS_UI_TAB_VIDEO,
    OPTIONS_UI_TAB_AUDIO,
    OPTIONS_UI_TAB_COUNT,
} OptionsUiTab;

// CE: panel_y_offset is the design-space y the host panel was
// shifted down by (hi-res Options crops the top header off, which
// moves the panel's design-rect to y=41). Controls (cyclic_ui)
// have hard-coded design-y values that assume the uncropped panel
// origin, so we subtract panel_y_offset from each control's y so
// they re-align with the cropped art chrome. Pass 0 if no crop.
void options_ui_start(OptionsUiTab tab, tig_window_handle_t window_handle, bool in_play, int panel_y_offset);
bool options_ui_load_module(void);
void options_ui_close(void);
bool options_ui_handle_button_pressed(tig_button_handle_t button_handle);

#endif /* ARCANUM_UI_OPTIONS_UI_H_ */
