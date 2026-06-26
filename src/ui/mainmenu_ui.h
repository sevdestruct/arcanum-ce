#ifndef ARCANUM_UI_MAINMENU_UI_H_
#define ARCANUM_UI_MAINMENU_UI_H_

#include "game/context.h"
#include "game/mes.h"
#include "game/timeevent.h"
#include "ui/types.h"

typedef enum MainMenuFont {
    MM_FONT_FLARE12,
    MM_FONT_ARIAL10,
    MM_FONT_MORPH15,
    MM_FONT_MPICONS,
    MM_FONT_COUNT,
} MainMenuFont;

typedef enum MainMenuColor {
    MM_COLOR_WHITE,
    MM_COLOR_RED,
    MM_COLOR_BLUE,
    MM_COLOR_GOLD,
    MM_COLOR_BROWN,
    MM_COLOR_GREEN,
    MM_COLOR_PURPLE,
    MM_COLOR_LIGHTBLUE,
    MM_COLOR_ORANGE,
    MM_COLOR_GRAY,
    MM_COLOR_COUNT,
} MainMenuColor;

typedef enum MainMenuType {
    MM_TYPE_DEFAULT,
    MM_TYPE_1,
    MM_TYPE_IN_PLAY,
    MM_TYPE_IN_PLAY_LOCKED,
    MM_TYPE_OPTIONS,
    MM_TYPE_5,
} MainMenuType;

typedef enum MainMenuWindowType {
    MM_WINDOW_0,
    MM_WINDOW_1,
    MM_WINDOW_MAINMENU,
    MM_WINDOW_MAINMENU_IN_PLAY,
    MM_WINDOW_MAINMENU_IN_PLAY_LOCKED,
    MM_WINDOW_SINGLE_PLAYER,
    MM_WINDOW_OPTIONS,
    MM_WINDOW_LOAD_GAME,
    MM_WINDOW_SAVE_GAME,
    MM_WINDOW_LAST_SAVE_GAME,
    MM_WINDOW_INTRO,
    MM_WINDOW_PICK_NEW_OR_PREGEN,
    MM_WINDOW_NEW_CHAR,
    MM_WINDOW_PREGEN_CHAR,
    MM_WINDOW_CHAREDIT,
    MM_WINDOW_SHOP,
    MM_WINDOW_CREDITS,
    MM_WINDOW_26,
    MM_WINDOW_COUNT,
} MainMenuWindowType;

extern int dword_5C3618;
extern int dword_64C420;
extern int64_t* dword_64C41C;

bool mainmenu_ui_init(GameInitInfo* init_info);
void mainmenu_ui_exit(void);
void mainmenu_ui_start(MainMenuType type);
void mainmenu_ui_start_at_window(MainMenuWindowType window_type);
void sub_5412D0(void);
void mainmenu_ui_harness_newgame(int pregen_idx); // CE harness: start new game w/ premade char
void mainmenu_ui_harness_newgame_at(int pregen_idx, int map, int64_t x, int64_t y); // CE harness: + spawn at map/loc
bool mainmenu_ui_handle(void);
bool mainmenu_ui_is_active(void);

// CE: handle of the hi-res full-screen backdrop window (mainmenu_bg
// or per-screen *_bg.bmp baked into its VB). Returns
// TIG_WINDOW_HANDLE_INVALID at native 800x600 (no separate backdrop
// is created — the panel itself is the full screen) or whenever
// the mainmenu isn't currently showing.
//
// Used as the underlay for the translucent-black tint pathway so
// the menu panel's dark areas knock through to the menu's
// background art instead of the iso world beneath. Without this
// the tint always targeted the iso VB even when the iso world was
// occluded by the mainmenu backdrop.
tig_window_handle_t mainmenu_ui_get_backdrop_handle(void);

// CE: true when the persistent backdrop has bespoke custom-bg art
// (mainmenu_bg.bmp or per-screen *_bg.bmp) loaded into its VB.
// False when no custom UI art is available — the backdrop is just
// empty black filler around the legacy panel chrome. The translucent-
// black tint pathway uses this to decide whether to use the backdrop
// or the panel as the tint underlay.
bool mainmenu_ui_has_custom_backdrop_art(void);

// CE: true while the custom-bg backdrop is animating OUT into gameplay
// (the ~300ms scale+alpha exit after a game starts/loads, before the
// backdrop window is destroyed). During this window the backdrop handle
// is still valid and has_custom_backdrop_art() still true, but the
// view is transitioning to the live iso world — so the translucent-
// black tint pathway must NOT pick the (fading) backdrop as its
// underlay, or a window opened right after load shows the menu / black
// through its dark areas instead of the game world.
bool mainmenu_ui_backdrop_is_exiting(void);

// CE: returns the current per-screen panel handle (mainmenu / pause
// / Options / Save / Load / etc.). For custom-UI shell menus this is
// aliased to the backdrop; for sub-windows and for legacy / no-
// custom-bg mode it's a separate window with the screen's chrome art
// painted in. Used as the tint underlay for legacy menus so modals
// raised over the menu reveal the legacy chrome through the tint
// instead of falling through to the pregame world.
tig_window_handle_t mainmenu_ui_get_panel_handle(void);
TigWindowModalDialogChoice mainmenu_ui_confirm_quit(void);
void mainmenu_ui_reset(void);
void mainmenu_ui_open(void);
void mainmenu_ui_close(bool back);
MainMenuWindowType mainmenu_ui_pop_window_stack(void);
void mainmenu_ui_push_window_stack(MainMenuWindowType window_type);
bool sub_543220(void);
void mainmenu_ui_create_window(void);
void mainmenu_ui_create_window_func(bool should_display);
bool mainmenu_ui_process_callback(TimeEvent* timeevent);
bool sub_549310(tig_button_handle_t button_handle);
void sub_5493C0(char* buffer, int size);
char* sub_549520(void);
void mainmenu_ui_exit_game(void);
void mainmenu_ui_progressbar_init(int max_value);
void mainmenu_ui_progressbar_update(int value);
MainMenuWindowInfo* sub_5496C0(int index);
MainMenuWindowType mainmenu_ui_window_type_get(void);
void mainmenu_ui_feedback_saving(void);
void mainmenu_ui_feedback_saving_completed(void);
void mainmenu_ui_feedback_cannot_save_in_tb(void);
void mainmenu_ui_feedback_loading(void);
void mainmenu_ui_feedback_loading_completed(void);
tig_window_handle_t sub_549820(void);
void mainmenu_ui_window_type_set(MainMenuWindowType window_type);
mes_file_handle_t mainmenu_ui_mes_file(void);
tig_font_handle_t mainmenu_ui_font(MainMenuFont font, MainMenuColor color);
void sub_549960(void);
void sub_549990(int* a1, int num);
void sub_549A40(void);
void sub_549A50(void);
int sub_549A60(void);
void sub_549A70(void);

#endif /* ARCANUM_UI_MAINMENU_UI_H_ */
