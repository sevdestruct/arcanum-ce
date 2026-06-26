#include "ui/mainmenu_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <tig/tig.h>

#include "game/area.h"
#include "game/background.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/gfade.h"
#include "game/gmovie.h"
#include "game/gsound.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/map.h"
#include "game/mes.h"
#include "game/obj.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/proto.h"
#include "game/reaction.h"
#include "game/script.h"
#include "game/snd.h"
#include "game/stat.h"
#include "game/teleport.h"
#include "game/timeevent.h"
#include "ui/broadcast_ui.h"
#include "ui/charedit_ui.h"
#include "ui/fate_ui.h"
#include "ui/gameuilib.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/logbook_ui.h"
#include "ui/options_ui.h"
#include "ui/schematic_ui.h"
#include "ui/scrollbar_ui.h"
#include "ui/sleep_ui.h"
#include "ui/slide_ui.h"
#include "ui/spell_ui.h"
#include "ui/textedit_ui.h"
#include "ui/ui_anim.h"
#include "ui/wmap_rnd.h"
#include "ui/wmap_ui.h"

typedef enum MainMenuUiNewCharHoverMode {
    MMUI_NEW_CHAR_HOVER_MODE_BACKGROUND,
    MMUI_NEW_CHAR_HOVER_MODE_GENDER,
    MMUI_NEW_CHAR_HOVER_MODE_RACE,
} MainMenuUiNewCharHoverMode;

typedef struct S64B870 {
    /* 0000 */ tig_art_id_t art_id;
    /* 0004 */ int max_frame;
    /* 0008 */ int fps;
    /* 000C */ int x;
    /* 0010 */ int y;
} S64B870;

static void sub_5412E0(bool a1);
static TigWindowModalDialogChoice mainmenu_ui_confirm(int num);
static void sub_541830(char* dst, const char* src);
static void sub_5418A0(char* str, TigRect* rect, tig_font_handle_t font, unsigned int flags);
static void mainmenu_ui_create_mainmenu(void);
static void mainmenu_ui_draw_version(void);
static bool mainmenu_ui_press_mainmenu_in_play(tig_button_handle_t button_handle);
static bool mainmenu_ui_press_mainmenu_in_play_locked(tig_button_handle_t button_handle);
static void mainmenu_ui_create_options(void);
static void sub_541D40(void);
static void mainmenu_ui_destroy_options(void);
static bool mainmenu_ui_press_options(tig_button_handle_t button_handle);
static void sub_541E20(int a1);
static void mainmenu_ui_load_game_create(void);
static void sub_542200(void);
static void mainmenu_ui_load_game_destroy(void);
static void sub_542280(int a1);
static void sub_5422A0(TigRect* rect);
static bool mainmenu_ui_load_game_execute(int btn);
static bool mainmenu_ui_load_game_button_pressed(tig_button_handle_t button_handle);
static bool mainmenu_ui_load_game_button_released(tig_button_handle_t button_handle);
static void mainmenu_ui_load_game_mouse_up(int a1, int a2);
static void sub_542560(void);
static void mainmenu_ui_load_game_refresh(TigRect* rect);
static void sub_542D00(char* str, TigRect* rect, tig_font_handle_t font);
static void mainmenu_ui_draw_list_name(const char* name, TigRect* rect, tig_font_handle_t font);
static void sub_542DF0(char* str, TigRect* rect, tig_font_handle_t font);
static void sub_542EA0(char* str, TigRect* rect, tig_font_handle_t font);
static void mmUITextWriteCenteredToArray(char* str, TigRect* rects, int cnt, tig_font_handle_t font);
static char* sub_543040(int index);
static const char* mainmenu_ui_row_module_tag(int index);
static void sub_543060(void);
static void sub_5430D0(void);
static bool mainmenu_ui_load_game_handle_delete(void);
static bool sub_5432B0(const char* name, const char* module_hint);
static void mainmenu_ui_save_game_create(void);
static void mainmenu_ui_save_game_destroy(void);
static bool mainmenu_ui_save_game_execute(int btn);
static bool mainmenu_ui_save_game_button_pressed(tig_button_handle_t button_handle);
static bool mainmenu_ui_save_game_button_released(tig_button_handle_t button_handle);
static void mainmenu_ui_save_game_mouse_up(int x, int y);
static void mainmenu_ui_save_game_refresh(TigRect* rect);
static void sub_544100(const char* str, TigRect* rect, tig_font_handle_t font, bool left_align);
static void sub_544210(void);
static void sub_544250(void);
static void sub_544290(void);
static bool mainmenu_ui_save_game_handle_delete(void);
static void mainmenu_ui_last_save_create(void);
static void mainmenu_ui_intro_create(void);
static void mainmenu_ui_credits_create(void);
static void mainmenu_ui_last_save_refresh(TigRect* rect);
static void mainmenu_ui_create_single_player(void);
static void mainmenu_ui_pick_new_or_pregen_create(void);
static void mainmenu_ui_new_char_create(void);
static void mainmenu_ui_new_char_refresh(TigRect* rect);
static void mmUISharedCharRefreshFunc(int64_t obj, TigRect* rect);
static bool mainmenu_ui_new_char_button_released(tig_button_handle_t button_handle);
static bool mainmenu_ui_new_char_next_background(int64_t obj, int* background_ptr);
static bool mainmenu_ui_new_char_prev_background(int64_t obj, int* background_ptr);
static bool mainmenu_ui_new_char_prev_gender(int64_t obj);
static bool mainmenu_ui_new_char_set_gender(int64_t obj, int gender);
static bool mainmenu_ui_new_char_next_gender(int64_t obj);
static bool mainmenu_ui_new_char_prev_race(int64_t obj);
static void mainmenu_ui_new_char_set_race(int64_t obj, int race);
static bool mainmenu_ui_new_char_next_race(int64_t obj);
static bool mainmenu_ui_new_char_button_hover(tig_button_handle_t button_handle);
static bool mainmenu_ui_new_char_button_leave(tig_button_handle_t button_handle);
static void mainmenu_ui_new_char_mouse_idle(int x, int y);
static bool mainmenu_ui_new_char_execute(int btn);
static void mainmenu_ui_pregen_char_create(void);
static void mainmenu_ui_pregen_char_refresh(TigRect* rect);
static bool mainmenu_ui_pregen_char_button_released(tig_button_handle_t button_handle);
static bool mainmenu_ui_pregen_char_execute(int btn);
static void mainmenu_ui_charedit_create(void);
static void mainmenu_ui_charedit_destroy(void);
static bool mainmenu_ui_charedit_button_released(tig_button_handle_t button_handle);
static void mainmenu_ui_charedit_refresh(TigRect* rect);
static void mainmenu_ui_shop_create(void);
static void mainmenu_ui_shop_destroy(void);
static bool mainmenu_ui_shop_button_released(tig_button_handle_t button_handle);
static void mainmenu_ui_shop_refresh(TigRect* rect);
static bool main_menu_button_create(MainMenuButtonInfo* info, int width, int height);
static bool main_menu_button_create_ex(MainMenuButtonInfo* info, int width, int height, unsigned int flags);
static void mainmenu_ui_refresh_text(tig_window_handle_t window_handle, const char* str, TigRect* rect, unsigned int flags);
static void sub_546DD0(void);
static bool mainmenu_ui_is_top_level(MainMenuWindowType t);
static bool mainmenu_ui_is_shell_menu(MainMenuWindowType t);
static void mainmenu_ui_finalize_close(void* ctx_v);
static void mainmenu_ui_destroy_persistent_backdrop(void);
static void mainmenu_ui_bg_finalize_exit(void* ctx_v);
static MainMenuWindowType mainmenu_ui_bg_window_type_resolve(void);
static void mainmenu_ui_blit_custom_bg_to_window(tig_window_handle_t wnd, TigRect win_rect);
static void mainmenu_ui_blit_custom_bg_at(tig_window_handle_t wnd, TigRect win_screen_rect, TigRect local_rect);
static void mainmenu_ui_apply_legacy_vignette(tig_window_handle_t window);
static void mainmenu_ui_restore_text_backdrop(tig_window_handle_t window_handle, TigRect* rect);
static bool mainmenu_ui_load_bg_vb(MainMenuWindowType type);
static void mainmenu_ui_free_custom_bg(void);
static bool mainmenu_ui_reload_custom_bg(MainMenuWindowType type);
static void mainmenu_ui_reapply_custom_bg(void);
static void mainmenu_ui_create_shared_radio_buttons(void);
static bool mainmenu_ui_message_filter(TigMessage* msg);
static void mainmenu_ui_refresh_button_text(int btn, unsigned int flags);
static void sub_547EF0(void);
static void sub_5480C0(int a1);
static void mmUIWinRefreshScrollBar(void);
static void sub_548FF0(int a1);
static void sub_549450(void);
static void mainmenu_ui_textedit_on_enter(TextEdit* textedit);
static void mainmenu_ui_textedit_on_change(TextEdit* textedit);
static void mainmenu_ui_textedit_on_arrow_up(TextEdit* textedit);
static void mainmenu_ui_textedit_on_arrow_down(TextEdit* textedit);
static void mainmenu_ui_feedback(int num);
static void mainmenu_fonts_init(void);
static void mainmenu_fonts_exit(void);
static void sub_549A80(void);

// 0x5993D0
static int mainmenu_font_nums[MM_FONT_COUNT] = {
    229,
    26,
    27,
    841,
};

// 0x5993E0
static int mainmenu_font_colors[MM_COLOR_COUNT][3] = {
    { 255, 255, 255 },
    { 255, 0, 0 },
    { 0, 0, 255 },
    { 255, 210, 0 },
    { 32, 15, 0 },
    { 0, 255, 0 },
    { 150, 0, 150 },
    { 60, 160, 255 },
    { 255, 128, 0 },
    { 128, 128, 128 },
};

// 0x5C3618
int dword_5C3618 = -1;

// 0x5C361C
static int mainmenu_ui_pregen_char_cnt = 9;

// 0x5C3620
static bool dword_5C3620 = true;

// 0x5C3624
static tig_window_handle_t mainmenu_ui_window_handle = TIG_WINDOW_HANDLE_INVALID;

static tig_window_handle_t mainmenu_ui_backdrop_handle = TIG_WINDOW_HANDLE_INVALID;

// CE: cached offsets from 800x600 design space into the persistent
// backdrop's local coords. Set once when the backdrop is first
// created in mainmenu_ui_create_window_func. The backdrop is sized
// screen-width × MM_BG_OVERDRAW (slightly larger than the screen so
// it can recede to 0.96 without exposing black edges), so the
// design-space (0, 0) origin lands at ((ow - 800)/2, (oh - 600)/2)
// inside the backdrop's VB.
//
// Shell menus (mainmenu / pause / single-player / pick-new-or-pregen)
// host their buttons + text directly on the backdrop — there's no
// separate per-screen panel for them. mainmenu_ui_refresh_text and
// main_menu_button_create_ex apply these offsets centrally whenever
// the host window equals mainmenu_ui_backdrop_handle, so every text /
// button positioning path (initial create, rollover refresh, the
// per-frame redraw_foreground in video-playback mode) ends up at the
// same screen position regardless of which call site initiated it.
//
// Sub-window panels (Options / NewChar / Load / Save / ...) are their
// own tig windows positioned via hrp_apply, so they ignore these
// offsets — the centralized helpers no-op for non-backdrop hosts.
static int mainmenu_ui_backdrop_offset_x = 0;
static int mainmenu_ui_backdrop_offset_y = 0;

static TigVideoBuffer* mainmenu_ui_custom_bg_vb = NULL;
static int mainmenu_ui_custom_bg_width = 0;
static int mainmenu_ui_custom_bg_height = 0;
static bool mainmenu_ui_has_custom_bg = false;
// True when the loaded BMP is a generic fallback (e.g. mainmenu_bg.bmp used
// for a screen that lacks its own bespoke file). In that case the BMP fills
// the backdrop behind the stock menu art but must NOT overlay the menu's own
// window / bar covers, otherwise it paints over the screen-specific art.
static bool mainmenu_ui_custom_bg_is_fallback = false;
static MainMenuWindowType mainmenu_ui_custom_bg_window_type = MM_WINDOW_0;
static bool mainmenu_ui_custom_bg_window_type_override = false;

// 0x5C3628
static TigRect mainmenu_ui_window_rect = { 0, 0, 800, 600 };

// 0x5C3638
static TigRect mainmenu_ui_window_fullscreen_rect = { 0, 0, 800, 600 };

// 0x5C3648
static TigRect mainmenu_ui_window_partial_rect = { 0, 41, 800, 400 };

// 0x5C3658
static tig_window_handle_t mainmenu_ui_top_bar_cover_window_handle = TIG_WINDOW_HANDLE_INVALID;

// 0x5C3660
static TigRect mainmenu_ui_top_bar_cover_rect = { 0, 0, 800, 41 };

// 0x5C3670
static tig_window_handle_t mainmenu_ui_bottom_bar_cover_window_handles[3] = {
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
};

// CE: captured tig window handles for a hide-animation-deferred destroy.
// sub_546DD0 (close current menu sub-window) captures the live PANEL
// handle into this single-instance ctx, clears the static handle
// variable (so the next mainmenu_ui_create_window_func can proceed
// cleanly while the OLD panel is still composited and animating out),
// and starts the hide animation. When the panel's hide spring settles,
// ui_anim fires mainmenu_ui_finalize_close which destroys the captured
// panel.
//
// The BACKDROP is NOT included here — it persists across sub-window
// swaps so the bg art doesn't pulse on every menu navigation. It only
// recedes / un-recedes on top↔sub transitions, and is destroyed when
// the mainmenu session ends (mainmenu_ui_handle returns).
//
// Cover strips ARE captured: they're per-sub-window chrome and need to
// be destroyed when their sub-window closes.
//
// Single-slot — only one deferred close can be in flight at a time. If
// sub_546DD0 is called while a previous deferred close is still pending
// (rapid menu navigation), we force-flush the previous destroys
// synchronously before starting the new deferred close.
typedef struct MainmenuUiCloseCtx {
    bool in_flight;
    tig_window_handle_t panel;
    tig_window_handle_t top_cover;
    tig_window_handle_t bottom_covers[3];
} MainmenuUiCloseCtx;

static MainmenuUiCloseCtx mainmenu_ui_pending_close;

// CE: persistent-backdrop state. Tracks whether the backdrop is
// currently "receded" (scale 0.96, when displaying a sub-window) or at
// rest (scale 1.0, when at top mainmenu). The transition between these
// states is the only time the backdrop's transform animates — once
// receded, it STAYS receded across sub-window swaps. Default false
// (rest) on every new mainmenu session.
static bool mainmenu_ui_bg_receded = false;

// CE: true when the mainmenu_ui_handle loop has ended and the bg exit
// tween (fade-out + scale to 0.96) is in flight. The on_complete
// (mainmenu_ui_bg_finalize_exit) destroys the backdrop only when this
// flag is still set — a re-open mid-fade clears it and retargets the
// tween back to the entrance state, in which case the previous
// callback fires harmlessly. Pattern mirrors fate_ui/sleep_ui's
// dismiss_pending guard.
static bool mainmenu_ui_bg_exit_pending = false;

// 0x5C3680
static TigRect mainmenu_ui_bottom_bar_cover_rects[3] = {
    { 200, 441, 400, 47 },
    { 0, 441, 200, 159 },
    { 600, 441, 200, 159 },
};

// 0x5C36B0
static bool stru_5C36B0[6][2] = {
    /*        MM_TYPE_DEFAULT */ { false, true },
    /*              MM_TYPE_1 */ { false, true },
    /*        MM_TYPE_IN_PLAY */ { true, false },
    /* MM_TYPE_IN_PLAY_LOCKED */ { true, false },
    /*        MM_TYPE_OPTIONS */ { true, false },
    /*              MM_TYPE_5 */ { false, false },
};

// 0x5C36E0
static MainMenuButtonInfo mainmenu_ui_mainmenu_in_play_buttons[] = {
    { 410, 143, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_SAVE_GAME, 0, 0, { 0 }, -1 },
    { 410, 193, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_LOAD_GAME, 0, 0, { 0 }, -1 },
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_OPTIONS, 0, 0, { 0 }, -1 },
    { 410, 293, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 410, 343, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C37D0
static MainMenuButtonInfo mainmenu_ui_mainmenu_in_play_locked_buttons[] = {
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_OPTIONS, 0, 0, { 0 }, -1 },
    { 410, 293, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 410, 343, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C3860
static MainMenuButtonInfo stru_5C3860[] = {
    { 410, 143, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_SAVE_GAME, 0, 0, { 0 }, -1 },
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_OPTIONS, 0, 0, { 0 }, -1 },
    { 410, 293, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 410, 343, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C3AB0
static MainMenuWindowInfo stru_5C3AB0 = {
    2,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    NULL,
    0,
    0,
    0,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    1,
};

// 0x5C3B48
static MainMenuWindowInfo stru_5C3B48 = {
    2,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    NULL,
    0,
    0,
    0,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    2,
};

// 0x5C3BE0
static MainMenuWindowInfo mainmenu_ui_intro_info = {
    2,
    mainmenu_ui_intro_create,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    NULL,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C3C78
static MainMenuWindowInfo mainmenu_ui_credits_window_info = {
    2,
    mainmenu_ui_credits_create,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    NULL,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C3FB4
static mes_file_handle_t mainmenu_ui_mainmenu_mes_file = MES_FILE_HANDLE_INVALID;

// NOTE: Write-only. It's impossible to understand its meaning.
//
// 0x5C3FB8
static int dword_5C3FB8 = -1;

// 0x5C3FC0
static TigRect stru_5C3FC0 = { 260, 293, 60, 20 };

// 0x5C3FD0
static TigRect stru_5C3FD0 = { 160, 263, 150, 20 };

// 0x5C3FE0
static TigRect stru_5C3FE0 = { 160, 263, 150, 20 };

// 0x5C3FF0
static TigRect stru_5C3FF0 = { 560, 203, 150, 20 };

// 0x5C4000
static bool dword_5C4000 = true;

// 0x5C4004
static bool dword_5C4004 = true;

// 0x64C2F8
static char mainmenu_ui_textedit_buffer[128];

// 0x5C4008
static TextEdit mainmenu_ui_textedit = {
    0,
    mainmenu_ui_textedit_buffer,
    23,
    mainmenu_ui_textedit_on_enter,
    mainmenu_ui_textedit_on_change,
    NULL,
    mainmenu_ui_textedit_on_arrow_up,
    mainmenu_ui_textedit_on_arrow_down,
};

// 0x5C4030
static int dword_5C4030[2] = {
    229,
    27,
};

// 0x5C4038
static int dword_5C4038 = 171;

// 0x5C403C
static int dword_5C403C = 327;

// 0x5C4040
static int dword_5C4040[3][3] = {
    { 0, 0, 0 },
    { 97, 61, 42 },
    { 100, 0, 0 },
};

// 0x5C4064
static int dword_5C4064[3] = {
    200,
    200,
    200,
};

// 0x5C4070
static int dword_5C4070[3] = {
    -1,
    2,
    -1,
};

// 0x5C407C
static char off_5C407C[] = "...";

// 0x5C5818
static MainMenuButtonInfo mainmenu_ui_mainmenu_no_multiplayer_buttons[] = {
    { 410, 143, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_SINGLE_PLAYER, 0, 0, { 0 }, -1 },
    { 410, 193, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_OPTIONS, 0, 0, { 0 }, -1 },
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_CREDITS, 0, 0, { 0 }, -1 },
    { 410, 293, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_0, 0, 0, { 0 }, -1 },
};

// 0x5C4170
static MainMenuWindowInfo mainmenu_ui_mainmenu_window_info = {
    329,
    mainmenu_ui_create_mainmenu,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    460,
    SDL_arraysize(mainmenu_ui_mainmenu_no_multiplayer_buttons),
    mainmenu_ui_mainmenu_no_multiplayer_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0,
};

// 0x5C4208
static MainMenuWindowInfo mainmenu_ui_mainmenu_in_play_window_info = {
    329,
    NULL,
    NULL,
    0,
    NULL,
    mainmenu_ui_press_mainmenu_in_play,
    NULL,
    NULL,
    NULL,
    30,
    SDL_arraysize(mainmenu_ui_mainmenu_in_play_buttons),
    mainmenu_ui_mainmenu_in_play_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0,
};

// 0x5C42A0
static MainMenuWindowInfo mainmenu_ui_mainmenu_in_play_locked_window_info = {
    329,
    NULL,
    NULL,
    0,
    NULL,
    mainmenu_ui_press_mainmenu_in_play_locked,
    NULL,
    NULL,
    NULL,
    70,
    SDL_arraysize(mainmenu_ui_mainmenu_in_play_locked_buttons),
    mainmenu_ui_mainmenu_in_play_locked_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0,
};

// 0x5C4338
static MainMenuWindowInfo stru_5C4338 = {
    329,
    NULL,
    NULL,
    0,
    NULL,
    mainmenu_ui_press_mainmenu_in_play,
    NULL,
    NULL,
    NULL,
    30,
    SDL_arraysize(stru_5C3860),
    stru_5C3860,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0,
};

// 0x5C43D0
static MainMenuButtonInfo mainmenu_ui_options_buttons[] = {
    { 130, 221, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 130, 261, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 130, 301, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 130, 341, -1, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C4490
static TigRect stru_5C4490 = { 84, 67, 89, 89 };

// 0x5C44A0
static MainMenuWindowInfo mainmenu_ui_options_window_info = {
    556,
    mainmenu_ui_create_options,
    mainmenu_ui_destroy_options,
    0,
    NULL,
    mainmenu_ui_press_options,
    NULL,
    NULL,
    NULL,
    4000,
    SDL_arraysize(mainmenu_ui_options_buttons),
    mainmenu_ui_options_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    2,
};

// 0x5C4538
static TigRect stru_5C4538 = { 150, 273, 240, 40 };

// 0x5C4548
static MainMenuButtonInfo mainmenu_ui_load_game_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7020 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7002 },
    { 115, 396, 32, TIG_BUTTON_HANDLE_INVALID, -1, -1, 0, { 0 }, 7022 },
};

// 0x5C45D8
static MainMenuButtonInfo stru_5C45D8 = {
    499,
    82,
    747, // texttoggle.art
    TIG_BUTTON_HANDLE_INVALID,
    -1,
    -1,
    0,
    { 0 },
    7025, // "Toggle Info Display"
};

// 0x5C4608
static MainMenuWindowInfo mainmenu_ui_load_game_window_info = {
    745,
    mainmenu_ui_load_game_create,
    mainmenu_ui_load_game_destroy,
    0,
    mainmenu_ui_load_game_button_pressed,
    mainmenu_ui_load_game_button_released,
    NULL,
    NULL,
    NULL,
    -1,
    SDL_arraysize(mainmenu_ui_load_game_buttons),
    mainmenu_ui_load_game_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_load_game_refresh,
    mainmenu_ui_load_game_execute,
    // CE: widen the list text area from 145 to 168 to use the dead margin between the
    // names and the scrollbar (at x=213) -- lets save names show a few more characters
    // and pushes the dim [Module] tag toward the right edge instead of mid-panel.
    { 42, 120, 160, 213 },
    mainmenu_ui_load_game_mouse_up,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C46B0
static TigRect stru_5C46B0 = { 0, 0, 468, 300 };

// 0x5C46C0
static TigRect stru_5C46C0 = { 281, 55, 468, 300 };

// 0x5C46D0
static TigRect stru_5C46D0 = { 289, 192, 451, 18 };

// 0x5C46E0
static TigRect stru_5C46E0 = { 484, 98, 451, 18 };

// 0x5C46F0
static TigRect stru_5C46F0 = { 281, 212, 465, 18 };

// 0x5C4700
static TigRect stru_5C4700 = { 448, 138, 139, 18 };

// 0x5C4710
static TigRect stru_5C4710 = { 448, 126, 139, 18 };

// 0x5C4720
static TigRect stru_5C4720 = { 547, 126, 109, 18 };

// 0x5C4730
static TigRect stru_5C4730 = { 294, 173, 441, 18 };

// 0x5C4740
static TigRect stru_5C4740[4] = {
    { 288, 232, 456, 18 },
    { 300, 252, 432, 18 },
    { 329, 272, 386, 18 },
    { 348, 292, 338, 18 },
};

// 0x5C4780
static TigRect stru_5C4780 = { 84, 10, 89, 89 };

// 0x5C4790
static bool dword_5C4790 = true;

// 0x5C4798
static TigRect stru_5C4798 = { 213, 111, 12, 232 };

// 0x5C47A8
static MainMenuButtonInfo mainmenu_ui_save_game_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7021 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7002 },
    { 115, 396, 32, TIG_BUTTON_HANDLE_INVALID, -1, -1, 0, { 0 }, 7022 },
};

// 0x5C4838
static MainMenuButtonInfo stru_5C4838 = {
    499,
    82,
    747,
    TIG_BUTTON_HANDLE_INVALID,
    -1,
    -1,
    0,
    { 0 },
    7025,
};

// 0x5C4868
static MainMenuWindowInfo mainmenu_ui_save_game_window_info = {
    745, // saveloadbackground.art
    mainmenu_ui_save_game_create,
    mainmenu_ui_save_game_destroy,
    0,
    mainmenu_ui_save_game_button_pressed,
    mainmenu_ui_save_game_button_released,
    NULL,
    NULL,
    NULL,
    -1,
    SDL_arraysize(mainmenu_ui_save_game_buttons),
    mainmenu_ui_save_game_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_save_game_refresh,
    mainmenu_ui_save_game_execute,
    // CE: widen the list text area (see the Load window) to use the margin up to the
    // scrollbar, for longer save names + a right-aligned [Module] tag.
    { 42, 120, 160, 213 },
    mainmenu_ui_save_game_mouse_up,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C4900
static MainMenuWindowInfo mainmenu_ui_last_save_window_info = {
    2, // "black.art"
    mainmenu_ui_last_save_create,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    NULL,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_last_save_refresh,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C4998
static MainMenuButtonInfo mainmenu_ui_single_player_buttons[] = {
    { 410, 143, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_PICK_NEW_OR_PREGEN, 0, 0, { 0 }, -1 },
    { 410, 193, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_LOAD_GAME, 0, 0, { 0 }, -1 },
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_LAST_SAVE_GAME, 0, 0, { 0 }, -1 },
    { 410, 293, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_INTRO, 0, 0, { 0 }, -1 },
    { 410, 343, -1, TIG_BUTTON_HANDLE_INVALID, -2, 0, 0, { 0 }, -1 },
};

// 0x5C4A88
static MainMenuWindowInfo mainmenu_ui_single_player_window_info = {
    331,
    mainmenu_ui_create_single_player,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    50,
    SDL_arraysize(mainmenu_ui_single_player_buttons),
    mainmenu_ui_single_player_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    2,
};

// 0x5C4B20
static MainMenuButtonInfo mainmenu_ui_pick_new_or_pregen_buttons[] = {
    { 410, 143, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_PREGEN_CHAR, 0, 0, { 0 }, -1 },
    { 410, 193, -1, TIG_BUTTON_HANDLE_INVALID, MM_WINDOW_NEW_CHAR, 0, 0, { 0 }, -1 },
    { 410, 243, -1, TIG_BUTTON_HANDLE_INVALID, -2, 0, 0, { 0 }, -1 },
};

// 0x5C4BB0
static MainMenuWindowInfo mainmenu_ui_pick_new_or_pregen_window_info = {
    329,
    mainmenu_ui_pick_new_or_pregen_create,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    420,
    SDL_arraysize(mainmenu_ui_pick_new_or_pregen_buttons),
    mainmenu_ui_pick_new_or_pregen_buttons,
    0,
    0,
    0xD,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    NULL,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    5,
};

// 0x5C4C48
static MainMenuButtonInfo mainmenu_ui_new_char_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7003 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7002 },
    { 43, 118, 283, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 244, 118, 284, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 42, 331, 757, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 255, 332, 758, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 42, 381, 757, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 255, 382, 758, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 42, 281, 757, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 255, 282, 758, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C4E28
static MainMenuWindowInfo mainmenu_ui_new_char_window_info = {
    765, // createcharacterbase.art
    mainmenu_ui_new_char_create,
    NULL,
    1,
    NULL,
    mainmenu_ui_new_char_button_released,
    mainmenu_ui_new_char_button_hover,
    mainmenu_ui_new_char_button_leave,
    mainmenu_ui_new_char_mouse_idle,
    -1,
    8, // TODO: `mainmenu_ui_new_char_buttons` has 10 buttons, figure out why only 8 are used.
    mainmenu_ui_new_char_buttons,
    0,
    0,
    0x5,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_new_char_refresh,
    mainmenu_ui_new_char_execute,
    { 184, 35, 145, 196 },
    NULL,
    { 347, 42, 10, 184 },
    NULL,
    0,
    0,
    0,
    0,
    0xB,
};

// 0x5C4EC0
static TigRect mainmenu_ui_shared_char_stats_title_rect = { 339, 23, 409, 19 };

// 0x5C4ED0
static TigRect mainmenu_ui_shared_char_name_rect = { 46, 183, 228, 19 };

// 0x5C4EE0
static TigRect mainmenu_ui_new_char_name_rect = { 46, 183, 228, 18 };

// 0x5C4EF0
static TigRect mainmenu_ui_shared_char_gender_rect = { 66, 243, 186, 19 };

// 0x5C4F00
static TigRect mainmenu_ui_new_char_gender_hover_rect = { 22, 231, 276, 39 };

// 0x5C4F10
static TigRect mainmenu_ui_shared_char_race_rect = { 66, 293, 186, 19 };

// 0x5C4F20
static TigRect mainmenu_ui_new_char_race_hover_rect = { 22, 281, 276, 40 };

// 0x5C4F30
static TigRect mainmenu_ui_shared_char_background_rect = { 66, 343, 186, 19 };

// 0x5C4F40
static TigRect stru_5C4F40 = { 22, 331, 276, 40 };

// 0x5C4F50
static TigRect stru_5C4F50[15] = {
    { 346, 46, 50, 15 },
    { 346, 66, 50, 15 },
    { 346, 86, 50, 15 },
    { 346, 106, 50, 15 },
    { 406, 46, 50, 15 },
    { 406, 66, 50, 15 },
    { 406, 86, 50, 15 },
    { 406, 106, 50, 15 },
    { 472, 46, 139, 15 },
    { 472, 66, 139, 15 },
    { 472, 86, 139, 15 },
    { 472, 106, 139, 15 },
    { 618, 46, 142, 15 },
    { 618, 65, 147, 15 },
    { 618, 86, 147, 15 },
};

// 0x5C5040
static TigRect stru_5C5040 = { 618, 105, 145, 15 };

// 0x5C5050
static TigRect mainmenu_ui_shared_char_stats_view_rect = { 346, 46, 729, 74 };

// 0x5C5060
static TigRect mainmenu_ui_shared_char_portrait_rect = { 95, 28, 64, 64 };

// 0x5C5070
static TigRect mainmenu_ui_shared_char_desc_view_rect = { 350, 155, 395, 212 };

// 0x5C5080
static TigRect mainmenu_ui_shared_char_desc_title_rect = { 350, 159, 395, 19 };

// 0x5C5090
static TigRect mainmenu_ui_shared_char_desc_body_rect = { 350, 185, 395, 182 };

// 0x5C50A0
static MainMenuButtonInfo mainmenu_ui_new_char_name_button = {
    45,
    224,
    -1,
    TIG_BUTTON_HANDLE_INVALID,
    -1,
    -1,
    0,
    { 0 },
    0,
};

// 0x5C50D0
static MainMenuButtonInfo stru_5C50D0 = {
    22,
    272,
    -1,
    TIG_BUTTON_HANDLE_INVALID,
    -1,
    -1,
    0,
    { 0 },
    0,
};

// 0x5C5100
static MainMenuButtonInfo stru_5C5100 = {
    22,
    322,
    -1,
    TIG_BUTTON_HANDLE_INVALID,
    -1,
    -1,
    0,
    { 0 },
    0,
};

// 0x5C5130
static int dword_5C5130[] = {
    STAT_STRENGTH,
    STAT_CONSTITUTION,
    STAT_DEXTERITY,
    STAT_BEAUTY,
    STAT_INTELLIGENCE,
    STAT_WILLPOWER,
    STAT_PERCEPTION,
    STAT_CHARISMA,
    STAT_CARRY_WEIGHT,
    STAT_DAMAGE_BONUS,
    STAT_AC_ADJUSTMENT,
    STAT_SPEED,
    STAT_HEAL_RATE,
    STAT_POISON_RECOVERY,
    STAT_REACTION_MODIFIER,
};

// 0x5C5170
static struct {
    int body_type;
    bool available_for_female;
} stru_5C5170[] = {
    /*     RACE_HUMAN */ { TIG_ART_CRITTER_BODY_TYPE_HUMAN, true },
    /*     RACE_DWARF */ { TIG_ART_CRITTER_BODY_TYPE_DWARF, false },
    /*       RACE_ELF */ { TIG_ART_CRITTER_BODY_TYPE_ELF, true },
    /*  RACE_HALF_ELF */ { TIG_ART_CRITTER_BODY_TYPE_HUMAN, true },
    /*     RACE_GNOME */ { TIG_ART_CRITTER_BODY_TYPE_HALFLING, false },
    /*  RACE_HALFLING */ { TIG_ART_CRITTER_BODY_TYPE_HALFLING, false },
    /*  RACE_HALF_ORC */ { TIG_ART_CRITTER_BODY_TYPE_HUMAN, true },
    /* RACE_HALF_OGRE */ { TIG_ART_CRITTER_BODY_TYPE_HALF_OGRE, false },
};

// 0x5C51B0
static MainMenuButtonInfo mainmenu_ui_pregen_char_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7003 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7002 },
    { 43, 118, 283, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
    { 244, 118, 284, TIG_BUTTON_HANDLE_INVALID, -1, 0, 0, { 0 }, -1 },
};

// 0x5C5270
static MainMenuWindowInfo mainmenu_ui_pregen_char_window_info = {
    766,
    mainmenu_ui_pregen_char_create,
    NULL,
    1,
    NULL,
    mainmenu_ui_pregen_char_button_released,
    NULL,
    NULL,
    NULL,
    -1,
    SDL_arraysize(mainmenu_ui_pregen_char_buttons),
    mainmenu_ui_pregen_char_buttons,
    0,
    0,
    0x5,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_pregen_char_refresh,
    mainmenu_ui_pregen_char_execute,
    { 184, 35, 145, 196 },
    NULL,
    { 347, 42, 10, 184 },
    NULL,
    0,
    0,
    0,
    0,
    0xB,
};

// 0x5C5308
static int mainmenu_ui_pregen_char_idx = 1;

// 0x5C5310
static MainMenuButtonInfo mainmenu_ui_charedit_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7003 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -2, -1, 1, { 0 }, 7002 },
};

// 0x5C5370
static MainMenuWindowInfo mainmenu_ui_charedit_info = {
    -1,
    mainmenu_ui_charedit_create,
    mainmenu_ui_charedit_destroy,
    0,
    NULL,
    mainmenu_ui_charedit_button_released,
    NULL,
    NULL,
    NULL,
    -1,
    SDL_arraysize(mainmenu_ui_charedit_buttons),
    mainmenu_ui_charedit_buttons,
    0,
    0,
    0x5,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_charedit_refresh,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0xC,
};

// 0x5C5408
static MainMenuButtonInfo mainmenu_ui_shop_buttons[] = {
    { 675, 55, 321, TIG_BUTTON_HANDLE_INVALID, -1, -1, 1, { 0 }, 7003 },
    { 69, 55, 323, TIG_BUTTON_HANDLE_INVALID, -2, -1, 1, { 0 }, 7002 },
};

// 0x5C5468
static MainMenuWindowInfo mainmenu_ui_shop_info = {
    -1,
    mainmenu_ui_shop_create,
    mainmenu_ui_shop_destroy,
    0,
    NULL,
    mainmenu_ui_shop_button_released,
    NULL,
    NULL,
    NULL,
    -1,
    SDL_arraysize(mainmenu_ui_shop_buttons),
    mainmenu_ui_shop_buttons,
    0,
    0,
    0x5,
    {
        { -1, 0, 0 },
        { -1, 0, 0 },
    },
    mainmenu_ui_shop_refresh,
    NULL,
    { 0 },
    NULL,
    { 0 },
    NULL,
    0,
    0,
    0,
    -1,
    0xE,
};

// 0x5C3A40
static MainMenuWindowInfo* main_menu_window_info[MM_WINDOW_COUNT] = {
    /*                       MM_WINDOW_0 */ &stru_5C3AB0,
    /*                       MM_WINDOW_1 */ &stru_5C3B48,
    /*                MM_WINDOW_MAINMENU */ &mainmenu_ui_mainmenu_window_info,
    /*        MM_WINDOW_MAINMENU_IN_PLAY */ &mainmenu_ui_mainmenu_in_play_window_info,
    /* MM_WINDOW_MAINMENU_IN_PLAY_LOCKED */ &mainmenu_ui_mainmenu_in_play_locked_window_info,
    /*           MM_WINDOW_SINGLE_PLAYER */ &mainmenu_ui_single_player_window_info,
    /*                 MM_WINDOW_OPTIONS */ &mainmenu_ui_options_window_info,
    /*               MM_WINDOW_LOAD_GAME */ &mainmenu_ui_load_game_window_info,
    /*               MM_WINDOW_SAVE_GAME */ &mainmenu_ui_save_game_window_info,
    /*          MM_WINDOW_LAST_SAVE_GAME */ &mainmenu_ui_last_save_window_info,
    /*                   MM_WINDOW_INTRO */ &mainmenu_ui_intro_info,
    /*      MM_WINDOW_PICK_NEW_OR_PREGEN */ &mainmenu_ui_pick_new_or_pregen_window_info,
    /*                MM_WINDOW_NEW_CHAR */ &mainmenu_ui_new_char_window_info,
    /*             MM_WINDOW_PREGEN_CHAR */ &mainmenu_ui_pregen_char_window_info,
    /*                MM_WINDOW_CHAREDIT */ &mainmenu_ui_charedit_info,
    /*                    MM_WINDOW_SHOP */ &mainmenu_ui_shop_info,
    /*                 MM_WINDOW_CREDITS */ &mainmenu_ui_credits_window_info,
    /*                      MM_WINDOW_26 */ &stru_5C4338,
};

// 0x64B870
static S64B870 stru_64B870[2];

// 0x64B898
static GameSaveInfo mainmenu_ui_gsi;

// 0x64BBF8
static GameSaveList mainmenu_ui_gsl;

// 0x64BC04
static tig_font_handle_t dword_64BC04[3];

// 0x64BC10
static tig_font_handle_t dword_64BC10[3];

// 0x64BC1C
static char byte_64BC1C[1000];

// 0x64C004
static MainMenuWindowType mainmenu_ui_window_stack[50];

// 0x64C0CC
static tig_font_handle_t dword_64C0CC[2][3];

// 0x64C0F0
static char byte_64C0F0[128];

// 0x64C170
static tig_font_handle_t mainmenu_ui_fonts_tbl[MM_FONT_COUNT][MM_COLOR_COUNT];

// 0x64C210
static tig_font_handle_t dword_64C210[2];

// 0x64C218
static tig_font_handle_t dword_64C218[2];

// 0x64C220
static ScrollbarId stru_64C220;

// 0x64C228
static tig_font_handle_t dword_64C228[2][3];

// 0x64C240
static tig_font_handle_t dword_64C240;

// CE: dim (grey) list font for the "[Module]" tag drawn on Load-menu rows, so the
// tag reads as secondary annotation (like dialogue emotes) rather than competing
// with the save description. Created in mainmenu_ui_init, destroyed in exit.
static tig_font_handle_t mainmenu_ui_dim_font;

// 0x64C244
static MainMenuType mainmenu_ui_type;

// 0x64C260
static ScrollbarUiControlInfo stru_64C260;

// 0x64C2F4
static mes_file_handle_t mainmenu_ui_rules_mainmenu_mes_file;

// 0x64C378
static int dword_64C378;

// 0x64C37C
static char* dword_64C37C;

// 0x64C380
static bool mainmenu_ui_initialized;

// 0x64C384
static bool mainmenu_ui_active;

// 0x64C388
static bool dword_64C388;

// 0x64C38C
static bool dword_64C38C;

// 0x64C390
static int mainmenu_ui_num_windows;

// 0x64C394
static char byte_64C394[128];

// 0x64C414
static MainMenuWindowType mainmenu_ui_window_type;

// CE: tracks the most recently closed mainmenu window type. Captured
// in sub_546DD0 before the close clears state, read by
// mainmenu_ui_create_window_func to decide whether the *previous*
// screen should suppress the new screen's entrance animation
// (currently: legacy CREDITS → MAINMENU transition, which the
// credits' own slideshow fade-out already covers).
static MainMenuWindowType mainmenu_ui_prev_window_type = MM_WINDOW_0;


// 0x64C418
static bool mainmenu_ui_start_new_game;

// 0x64C41C
int64_t* dword_64C41C;

// 0x64C420
int dword_64C420;

// CE: one-shot guard. An up/down arrow that exits the Save-name input is handled on
// key PRESS (text-edit), but its key RELEASE then falls through to the list's own
// up/down nav (the input is no longer focused) and moves the selection a SECOND time
// -- "leapfrogging" the intended row. Set when the input exits via an arrow; the next
// up/down release in the Save window is swallowed instead of navigating.
static bool mainmenu_ui_arrow_exit_pending;

// 0x64C424
static bool mainmenu_ui_auto_equip_items_on_start;

// 0x64C428
static bool dword_64C428;

// 0x64C42C
static int dword_64C42C[3];

// 0x64C438
static bool mainmenu_ui_was_compact_interface;

// 0x64C43C
static int dword_64C43C;

// 0x64C440
static int dword_64C440;

// 0x64C444
static bool mainmenu_ui_gsi_loaded;

// 0x64C448
static int mainmenu_ui_progressbar_max_value;

// 0x64C44C
static int mainmenu_ui_progressbar_value;

// 0x64C450
static bool dword_64C450;

// 0x64C454
static ChareditMode dword_64C454;

// 0x64C458
static MainMenuUiNewCharHoverMode mainmenu_ui_new_char_hover_mode;

// 0x64C460
static int64_t qword_64C460;

// 0x64C468
static int dword_64C468;

// 0x540930
bool mainmenu_ui_init(GameInitInfo* init_info)
{
    int index;
    TigFont font_desc;
    MesFileEntry mes_file_entry;

    (void)init_info;

    if (mainmenu_ui_mainmenu_mes_file == MES_FILE_HANDLE_INVALID) {
        if (!mes_load("mes\\mainmenu.mes", &mainmenu_ui_mainmenu_mes_file)) {
            return false;
        }
    }

    if (!mes_load("rules\\mainmenu.mes", &mainmenu_ui_rules_mainmenu_mes_file)) {
        return false;
    }

    settings_register(&settings, "show version", "0", NULL);
    settings_register(&settings, LEGACY_MENU_VIGNETTE_KEY, "0", NULL);

    mainmenu_fonts_init();

    for (index = 0; index < 3; index++) {
        font_desc.flags = 0;
        if (index == 1) {
            font_desc.flags = TIG_FONT_BLEND_ADD;
        }
        tig_art_interface_id_create(dword_5C403C, 0, 0, 0, &(font_desc.art_id));
        font_desc.str = NULL;
        font_desc.color = tig_color_make(dword_5C4040[index][0], dword_5C4040[index][1], dword_5C4040[index][2]);
        tig_font_create(&font_desc, &(dword_64C228[0][index]));

        tig_art_interface_id_create(dword_5C403C, 0, 0, 0, &(font_desc.art_id));
        font_desc.color = index < 2
            ? tig_color_make(dword_5C4040[index][0], dword_5C4040[index][1], dword_5C4040[index][2])
            : tig_color_make(240, 15, 15);
        tig_font_create(&font_desc, &(dword_64C228[1][index]));
    }

    for (index = 0; index < 3; index++) {
        font_desc.flags = 0;
        tig_art_interface_id_create(dword_5C4038, 0, 0, 0, &(font_desc.art_id));
        font_desc.str = NULL;
        font_desc.color = tig_color_make(dword_64C42C[index], dword_64C42C[index], dword_64C42C[index]);
        tig_font_create(&font_desc, &(dword_64C0CC[0][index]));

        tig_art_interface_id_create(dword_5C4038, 0, 0, 0, &(font_desc.art_id));
        font_desc.color = index < 2
            ? tig_color_make(dword_64C42C[index], dword_64C42C[index], dword_64C42C[index])
            : tig_color_make(240, 15, 15);
        tig_font_create(&font_desc, &(dword_64C0CC[1][index]));
    }

    for (index = 0; index < 3; index++) {
        font_desc.flags = 0;
        tig_art_interface_id_create(dword_5C4038, 0, 0, 0, &(font_desc.art_id));
        font_desc.str = NULL;
        font_desc.color = tig_color_make(dword_5C4064[index], dword_5C4064[index], dword_5C4064[index]);
        tig_font_create(&font_desc, &(dword_64BC04[index]));

        tig_art_interface_id_create(dword_5C4038, 0, 0, 0, &(font_desc.art_id));
        font_desc.color = index < 2
            ? tig_color_make(dword_5C4064[index], dword_5C4064[index], dword_5C4064[index])
            : tig_color_make(240, 15, 15);
        tig_font_create(&font_desc, &(dword_64BC10[index]));
    }

    for (index = 0; index < 2; index++) {
        font_desc.flags = 0;
        tig_art_interface_id_create(dword_5C4030[index], 0, 0, 0, &(font_desc.art_id));
        font_desc.str = NULL;
        font_desc.color = tig_color_make(250, 250, 250);
        tig_font_create(&font_desc, &(dword_64C210[index]));

        font_desc.flags = 0;
        tig_art_interface_id_create(dword_5C4030[index], 0, 0, 0, &(font_desc.art_id));
        font_desc.str = NULL;
        font_desc.color = tig_color_make(255, 50, 50);
        tig_font_create(&font_desc, &(dword_64C218[index]));
    }

    font_desc.flags = 0;
    tig_art_interface_id_create(dword_5C4030[0], 0, 0, 0, &(font_desc.art_id));
    font_desc.str = NULL;
    font_desc.color = tig_color_make(255, 210, 0);
    tig_font_create(&font_desc, &dword_64C240);

    // CE: dim grey font for the Load-menu "[Module]" tag.
    font_desc.flags = 0;
    tig_art_interface_id_create(dword_5C4030[0], 0, 0, 0, &(font_desc.art_id));
    font_desc.str = NULL;
    font_desc.color = tig_color_make(140, 140, 140);
    tig_font_create(&font_desc, &mainmenu_ui_dim_font);

    mes_file_entry.num = 500;
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
    strncpy(mainmenu_ui_textedit_buffer, mes_file_entry.str, 23);

    mainmenu_ui_initialized = true;
    mainmenu_ui_window_type = MM_WINDOW_0;

    dword_64C388 = true;
    mainmenu_ui_start(MM_TYPE_DEFAULT);
    dword_64C388 = false;

    gamelib_thumbnail_size_set(468, 300);
    mainmenu_ui_start_new_game = false;
    sub_549A80();
    dword_64C37C = NULL;

    return true;
}

// 0x541050
void mainmenu_ui_exit(void)
{
    int index;

    sub_5412E0(true);
    mainmenu_fonts_exit();

    for (index = 0; index < 3; index++) {
        tig_font_destroy(dword_64C228[0][index]);
        tig_font_destroy(dword_64C228[1][index]);
        tig_font_destroy(dword_64C0CC[0][index]);
        tig_font_destroy(dword_64C0CC[1][index]);
        tig_font_destroy(dword_64BC04[index]);
        tig_font_destroy(dword_64BC10[index]);
    }

    for (index = 0; index < 2; index++) {
        tig_font_destroy(dword_64C210[index]);
        tig_font_destroy(dword_64C218[index]);
    }

    tig_font_destroy(dword_64C240);
    tig_font_destroy(mainmenu_ui_dim_font);
    mes_unload(mainmenu_ui_rules_mainmenu_mes_file);
    mes_unload(mainmenu_ui_mainmenu_mes_file);

    if (dword_64C41C != NULL) {
        FREE(dword_64C41C);
    }

    mainmenu_ui_initialized = false;
}

// 0x541150
void sub_541150(void)
{
    mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
    mainmenu_ui_start_new_game = false;
    mainmenu_ui_auto_equip_items_on_start = false;
}

// 0x541170
void sub_541170(void)
{
    dword_5C4000 = true;
}

// 0x541180
void mainmenu_ui_start(MainMenuType type)
{
    tig_art_id_t art_id;
    // CE: Separate setup-work from modal-think-time. The Escape-key
    // megahitches in the F9 perf log were almost entirely the user
    // sitting in the pause menu (mainmenu_ui_handle's modal loop blocks
    // the event dispatcher until dismissal — see commit 269fdc4e for
    // the original analysis). Time the setup body separately and log
    // when it exceeds ~50ms so we can tell a real cold-load hitch from
    // user think-time. Custom-UI BG art (inmenu_bg.bmp etc.) is loaded
    // during mainmenu_ui_open below — if THAT path is slow, this
    // counter will surface it.
    tig_timestamp_t mm_start_ts;
    tig_timer_now(&mm_start_ts);

    if (!mainmenu_ui_active) {
        // CE: defensive — if MM_TYPE_DEFAULT (pre-game main menu) is
        // requested while we're currently in an in-game menu type,
        // redirect to MM_TYPE_IN_PLAY (pause menu). MAIN MENU during
        // gameplay is an inconsistent state where ESC has no path
        // back to the game and the user gets stuck. This catches
        // stray gameuilib_wants_mainmenu_set() calls (e.g. from
        // multiplayer disconnect callbacks via sub_4A2A30) that
        // would otherwise force MAIN MENU mid-game.
        //
        // Use the PREVIOUS mainmenu_ui_type's "in_game" flag
        // (stru_5C36B0[type][0]) rather than player_get_local_pc_obj()
        // — the latter can return a stub PC in some pre-game states
        // and would mis-route the very first main menu open at cold
        // start. The static type is initialized to MM_TYPE_DEFAULT,
        // whose in_game flag is false, so cold start is unaffected.
        if (type == MM_TYPE_DEFAULT && stru_5C36B0[mainmenu_ui_type][0]) {
            type = MM_TYPE_IN_PLAY;
        }

        mainmenu_ui_num_windows = 0;

        // CE: Hide main interface to prevent world view and top/bottom bars
        // to be visible while mainmenu is being presented (visible on custom
        // resolutions).
        //
        // Exception: the in-game Options shortcut (O key) is meant to draw
        // over the live game UI with a knockout for the bottom HUD info bar,
        // matching the original 800x600 layout. Keep the in-game interface
        // visible so the HUD stays where it is behind the options panel.
        if (type != MM_TYPE_OPTIONS) {
            intgame_hide();
        }

        if (type != MM_TYPE_OPTIONS) {
            sub_45B320();
        }

        tig_art_interface_id_create(0, 0, 0, 0, &art_id);
        tig_mouse_cursor_set_art_id(art_id);
        inven_ui_destroy();

        if (type == MM_TYPE_DEFAULT && !dword_5C4000) {
            type = MM_TYPE_1;
        }

        mainmenu_ui_type = type;

        switch (type) {
        case MM_TYPE_IN_PLAY:
            mainmenu_ui_window_type = MM_WINDOW_MAINMENU_IN_PLAY;
            dword_5C4004 = false;
            mainmenu_ui_open();
            object_hover_obj_set(OBJ_HANDLE_NULL);
            break;
        case MM_TYPE_IN_PLAY_LOCKED:
            mainmenu_ui_window_type = MM_WINDOW_MAINMENU_IN_PLAY_LOCKED;
            dword_5C4004 = false;
            mainmenu_ui_open();
            object_hover_obj_set(OBJ_HANDLE_NULL);
            break;
        case MM_TYPE_OPTIONS:
            mainmenu_ui_window_type = MM_WINDOW_OPTIONS;
            dword_5C4004 = false;
            mainmenu_ui_open();
            object_hover_obj_set(OBJ_HANDLE_NULL);
            break;
        case MM_TYPE_5:
            mainmenu_ui_window_type = MM_WINDOW_26;
            dword_5C4004 = false;
            mainmenu_ui_open();
            object_hover_obj_set(OBJ_HANDLE_NULL);
            break;
        default:
            if (tig_mouse_hide() != TIG_OK) {
                tig_debug_printf("mainmenu_ui_start: ERROR: tig_mouse_hide failed!\n");
                exit(EXIT_FAILURE);
            }
            mainmenu_ui_window_type = MM_WINDOW_0;
            dword_5C4004 = true;
            mainmenu_ui_open();
            object_hover_obj_set(OBJ_HANDLE_NULL);
            break;
        }

    }

    // Emit a perf entry when the open path was actually slow. Threshold
    // matches gamelib_save/load (~100ms), with a finer 50ms floor here
    // since menu open is supposed to be effectively instant.
    {
        int dur_ms = tig_timer_elapsed(mm_start_ts);
        if (dur_ms >= 50) {
            char ctx[64];
            snprintf(ctx, sizeof(ctx), "mainmenu_ui_start type=%d", (int)type);
            gamelib_perf_log_event(ctx, (uint64_t)dur_ms * 1000000ull);
        }
    }
}

// Open the main-menu UI directly to a specific window from in-game (like
// the O shortcut does for Options). Reuses the MM_TYPE_OPTIONS flavor so
// the menu has the single-press ESC return-to-game behavior wired up via
// stru_5C36B0[MM_TYPE_OPTIONS][0]=true.
void mainmenu_ui_start_at_window(MainMenuWindowType window_type)
{
    tig_art_id_t art_id;

    if (mainmenu_ui_active) {
        return;
    }

    mainmenu_ui_num_windows = 0;
    // Don't call intgame_hide() here — it hides the iso (game-world) window
    // which would leave a black flood behind the menu instead of the live
    // game. This mirrors mainmenu_ui_start's `type != MM_TYPE_OPTIONS` gate,
    // since these in-game shortcut targets (Options / Save / Load) all want
    // to draw over the live game. The iso HUD strips are handled separately
    // by intgame_iso_strips_hide_full() inside mainmenu_ui_create_window_func
    // when skip_hires_scaffold is true.

    tig_art_interface_id_create(0, 0, 0, 0, &art_id);
    tig_mouse_cursor_set_art_id(art_id);
    inven_ui_destroy();

    mainmenu_ui_type = MM_TYPE_OPTIONS;
    mainmenu_ui_window_type = window_type;
    dword_5C4004 = false;
    mainmenu_ui_open();
    object_hover_obj_set(OBJ_HANDLE_NULL);
}

// 0x5412D0
void sub_5412D0(void)
{
    sub_5412E0(false);
}

// CE: pump the game loop briefly so a pending panel exit
// animation (started by mainmenu_ui_close) has a chance to play
// through and trigger its destroy callback. Used by the new-game
// start path before it kicks off gfade_run + teleport_do, which
// would otherwise block the loop, freeze the panel mid-animation,
// and resume it later — visible as a doubled fade.
//
// Intentionally LIMITED to what the panel animation actually
// needs: ui_anim_ping (advances the spring), intgame_hud_ping
// (applies HUD slide updates if any), and tig_window_display
// (composites + flips the frame). We do NOT call gamelib_ping
// here — that fans out to every module's ping (teleport, time
// events, scripts, etc.), and during this pump the new game's
// world state is half-set-up (PC created in SHOP, but map not
// loaded yet via teleport_do which fires AFTER us). Letting
// arbitrary module pings run mid-pump risks firing time-events
// or polling teleport state in an inconsistent context — the
// user once saw the PC missing + opening dialogue not triggering,
// which fits that pattern.
//
// timeout_ms caps the wait so we don't hang if the anim never
// settles (e.g. cfg-disabled fast-path that completes synchronously
// inside mainmenu_ui_close, leaving in_flight false — loop exits
// on the first check, so no wasted frames).
static void mainmenu_ui_pump_until_close_settled(int timeout_ms)
{
    tig_timestamp_t start;
    tig_timer_now(&start);
    while (mainmenu_ui_pending_close.in_flight) {
        tig_ping();
        ui_anim_ping();
        intgame_hud_ping();
        tig_window_display();
        if (tig_timer_elapsed(start) >= timeout_ms) {
            break;
        }
    }
}

// 0x5412E0
void sub_5412E0(bool a1)
{
    int64_t pc_obj;
    int map;
    int64_t x;
    int64_t y;
    FadeData fade_data;
    TeleportData teleport_data;
    MesFileEntry mes_file_entry;
    DateTime datetime;
    TimeEvent timeevent;

    if (mainmenu_ui_active) {
        gameuilib_wants_mainmenu_unset();

        pc_obj = player_get_local_pc_obj();

        if (mainmenu_ui_auto_equip_items_on_start) {
            if (item_wield_get(pc_obj, ITEM_INV_LOC_WEAPON) == OBJ_HANDLE_NULL) {
                item_wield_best(pc_obj, ITEM_INV_LOC_WEAPON, OBJ_HANDLE_NULL);
            }
            if (item_wield_get(pc_obj, ITEM_INV_LOC_ARMOR) == OBJ_HANDLE_NULL) {
                item_wield_best(pc_obj, ITEM_INV_LOC_ARMOR, OBJ_HANDLE_NULL);
            }
            mainmenu_ui_auto_equip_items_on_start = false;
        }

        // CE: start the menu's exit animation BEFORE the
        // fade-to-black + teleport_do, then pump the game loop
        // until the panel's scale-out completes. Previously the
        // close ran AFTER teleport_do — the menu's exit
        // animation ended up playing OVER the just-faded-in
        // game world (reading as a doubled fade: "fades to black,
        // fades in still on menu, then game").
        //
        // For the non-new-game branch (e.g. simple ESC exit), the
        // close still runs synchronously below — no fade in flight
        // to fight, so the legacy behavior is preserved.
        bool start_new_game = mainmenu_ui_start_new_game;
        if (start_new_game) {
            mainmenu_ui_close(false);
            mainmenu_ui_pump_until_close_settled(400);
        }

        if (mainmenu_ui_window_type != MM_WINDOW_0 || !stru_5C36B0[mainmenu_ui_type][1]) {
            if (start_new_game) {
                mainmenu_ui_start_new_game = false;

                map = map_by_type(MAP_TYPE_START_MAP);
                if (map == 0) {
                    tig_debug_printf("MMUI: ERROR: Teleport/World Loc Failure!\n");
                    exit(EXIT_FAILURE);
                }
                if (!map_get_starting_location(map, &x, &y)) {
                    tig_debug_printf("MMUI: ERROR: Teleport/World Loc Failure!\n");
                    exit(EXIT_FAILURE);
                }

                fade_data.flags = 0;
                fade_data.color = 0;
                fade_data.steps = 64;
                fade_data.duration = 3.0f;
                gfade_run(&fade_data);

                // CE: with the screen now fully faded to black, tear
                // down the menu backdrop synchronously — invisible
                // to the user, and removes the last menu-era window
                // so teleport_do's later FADE_IN reveals the game
                // world directly instead of the mainmenu bg. Without
                // this, the user saw the backdrop briefly come back
                // through the fade-in before the menu's async exit
                // animation caught up.
                mainmenu_ui_destroy_persistent_backdrop();
                // CE: re-evaluate the HUD bar's translucent-black
                // underlay NOW. The bar's tint was pointing at the
                // backdrop we just freed; if we leave it dangling,
                // the next tig_window_display (fired from
                // teleport_ping during fade-in) calls
                // tig_video_blit_near_black_tinted, which calls
                // SDL_LockSurface on the freed VB and segfaults.
                // The picker falls through to iso world (or no
                // underlay) now that no backdrop is up.
                intgame_refresh_hud_bar_tint();
                intgame_refresh_modal_tint();

                teleport_data.flags = TELEPORT_MOVIE1 | TELEPORT_MOVIE2 | TELEPORT_FADE_IN;
                teleport_data.obj = pc_obj;
                teleport_data.loc = LOCATION_MAKE(x, y);
                teleport_data.map = map;
                teleport_data.movie1 = 1;
                teleport_data.movie_flags1 = 0;
                teleport_data.movie2 = 7;
                teleport_data.movie_flags2 = 0;
                teleport_data.fade_in.flags = FADE_IN;
                teleport_data.fade_in.steps = 64;
                teleport_data.fade_in.duration = 3.0f;
                teleport_data.fade_in.color = tig_color_make(0, 0, 0);
                teleport_do(&teleport_data);

                gsound_stop_all(0);

                mes_file_entry.num = 6000; // "Please Wait"
                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                sub_557FD0(mes_file_entry.str);

                timeevent.type = TIMEEVENT_TYPE_NEWSPAPERS;
                sub_45A950(&datetime, 86400000 - sub_45AD70());
                timeevent_add_delay(&timeevent, &datetime);

                wmap_rnd_schedule();
            } else {
                if (dword_5C4004) {
                    sub_40FED0();
                }
                gamelib_draw();
            }
        }

        if (!start_new_game) {
            mainmenu_ui_close(false);
        }
    }

    intgame_refresh_cursor();
    sub_5507D0(0);
    if (!a1) {
        intgame_draw_bars();

        if (mainmenu_ui_was_compact_interface) {
            intgame_toggle_interface();
            mainmenu_ui_was_compact_interface = false;
        }
    }
    sub_45B340();

    // CE: Restore main interface to its normal state.
    intgame_show();

    // CE: re-open the rotating-window page that was active at save time
    // (ROTWIN_RESTORE_KEY). Done here, as the last step of the enter-world
    // path, so it lands after intgame_show and every MSG-forcing refresh.
    // No-op unless a load queued a page.
    intgame_apply_rotwin_restore();
}

// 0x541590
bool mainmenu_ui_handle(void)
{
    tig_timestamp_t timestamp;
    TigMessage msg;

    mainmenu_ui_was_compact_interface = intgame_is_compact_interface();
    if (mainmenu_ui_was_compact_interface) {
        intgame_toggle_interface();
    }

    if (mainmenu_ui_window_type < MM_WINDOW_MAINMENU) {
        mainmenu_ui_close(false);
        mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
        mainmenu_ui_open();

        if (tig_mouse_show() != TIG_OK) {
            tig_debug_printf("mainmenu_ui_handle: ERROR: tig_mouse_show failed!\n");
        }
    }

    broadcast_ui_close();

    while (mainmenu_ui_active) {
        tig_ping();
        tig_timer_now(&timestamp);
        timeevent_ping(timestamp);
        gamelib_ping();
        iso_redraw();
        tig_window_display();

        while (tig_message_dequeue(&msg) == TIG_OK) {
            switch (msg.type) {
            case TIG_MESSAGE_QUIT:
                if (mainmenu_ui_confirm_quit() == TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
                    return false;
                }
                break;
            case TIG_MESSAGE_REDRAW:
                gamelib_redraw();
                break;
            case TIG_MESSAGE_KEYBOARD:
                if (!msg.data.keyboard.pressed
                    && msg.data.keyboard.scancode == SDL_SCANCODE_ESCAPE
                    && mainmenu_ui_window_type != MM_WINDOW_MAINMENU) {
                    // CE: if a name is being typed (new save / new character), ESC
                    // cancels the INPUT first -- unfocus back to the prompt and keep
                    // the window open -- instead of closing the whole window out from
                    // under the user. A second ESC then closes as usual.
                    if (textedit_ui_is_focused()) {
                        textedit_ui_unfocus(&mainmenu_ui_textedit);
                        dword_64C428 = false;
                        mainmenu_ui_textedit_buffer[0] = '\0';
                        if (main_menu_window_info[mainmenu_ui_window_type]->refresh_func != NULL) {
                            main_menu_window_info[mainmenu_ui_window_type]->refresh_func(NULL);
                        }
                        break;
                    }
                    // If the menu stack has a parent (e.g. user opened
                    // Options from the pause menu, or Save/Load from the
                    // pause menu), pop back to the parent like the original
                    // close-back path does. Only at the top of the stack —
                    // i.e. we were launched directly into this menu (O key,
                    // Cmd+S/L, etc.) — do we route ESC through the full
                    // exit-to-game restore for the "in-game" menu types.
                    if (mainmenu_ui_num_windows <= 1
                        && stru_5C36B0[mainmenu_ui_type][0]) {
                        sub_5412D0();
                    } else {
                        mainmenu_ui_close(true);
                    }
                }
                break;
            default:
                break;
            }
        }

        tig_window_display();
    }

    // CE: session end — mainmenu_ui_active dropped to false, the loop
    // is exiting. Force-flush any pending deferred panel destroy so
    // we don't leak tig windows, then START the bg exit animation
    // ASYNCHRONOUSLY and return. The main game loop's ui_anim_ping
    // advances the spring over the next few frames; when it settles,
    // mainmenu_ui_bg_finalize_exit fires and destroys the backdrop.
    //
    // Going async (vs the previous blocking pump) is what makes the
    // exit interruptible: if the user re-opens the mainmenu via ESC
    // or button mid-fade, the next mainmenu_ui_create_window_func
    // call clears the exit-pending flag and retargets the bg tween
    // back to entrance — preserving current spring value for a
    // smooth reversal, no waiting for the exit to finish.
    if (mainmenu_ui_pending_close.in_flight) {
        mainmenu_ui_finalize_close(&mainmenu_ui_pending_close);
    }

    if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
        if (mainmenu_ui_has_custom_bg) {
            // Custom UI: animate the backdrop's bg art out
            // (scale + alpha) so the menu visually dismisses into
            // the world / pre-game black behind it.
            ui_anim_profile_t exit_profile = { 300, 1.2f };
            float exit_from = mainmenu_ui_bg_receded ? 0.96f : 1.0f;
            mainmenu_ui_bg_exit_pending = true;
            // CE: the backdrop is now leaving into gameplay. Re-pick the
            // persistent HUD bar / modal tint underlays immediately so
            // they read the live iso world for the whole exit instead of
            // sampling the fading menu backdrop they were bound to. (The
            // exit-pending gate in intgame_translucent_black_pick now
            // routes them to iso.) They're re-picked again when the
            // backdrop is finally destroyed — this just closes the gap
            // during the exit animation.
            intgame_refresh_hud_bar_tint();
            intgame_refresh_modal_tint();
            ui_anim_window_transform_from_to_with_complete(
                mainmenu_ui_backdrop_handle,
                exit_from, 1.0f, 0.96f, 0.0f,
                UI_ANIM_ANCHOR_CENTER, &exit_profile,
                mainmenu_ui_bg_finalize_exit, NULL);
        } else {
            // Legacy / no-custom-bg: destroy backdrop
            // synchronously. The backdrop is just empty black
            // filler — fading it out doesn't add anything
            // visually, and a mid-fade re-open would expose a
            // rate-mismatched retarget against a fresh panel.
            mainmenu_ui_destroy_persistent_backdrop();
        }
    }

    return true;
}

// 0x541680
bool mainmenu_ui_is_active(void)
{
    return mainmenu_ui_active;
}

// CE: see mainmenu_ui.h. Returns the hi-res backdrop window handle
// (the one with mainmenu_bg / per-screen *_bg.bmp baked into its VB)
// when the mainmenu is up in hi-res, or TIG_WINDOW_HANDLE_INVALID
// otherwise. Used by the translucent-black tint pathway as the
// underlay-VB source so menu panel dark areas reveal the menu's bg
// instead of the iso world.
tig_window_handle_t mainmenu_ui_get_backdrop_handle(void)
{
    return mainmenu_ui_backdrop_handle;
}

// CE: see mainmenu_ui.h.
bool mainmenu_ui_has_custom_backdrop_art(void)
{
    return mainmenu_ui_has_custom_bg;
}

// CE: see mainmenu_ui.h.
bool mainmenu_ui_backdrop_is_exiting(void)
{
    return mainmenu_ui_bg_exit_pending;
}

// CE: see mainmenu_ui.h.
tig_window_handle_t mainmenu_ui_get_panel_handle(void)
{
    return mainmenu_ui_window_handle;
}

// 0x541690
TigWindowModalDialogChoice mainmenu_ui_confirm_quit(void)
{
    return mainmenu_ui_confirm(5100); // "Are you sure you want to quit?"
}

// 0x5416A0
TigWindowModalDialogChoice mainmenu_ui_confirm(int num)
{
    MesFileEntry mes_file_entry;
    TigWindowModalDialogInfo modal_info;
    TigWindowModalDialogChoice choice;

    mes_file_entry.num = num;
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

    modal_info.type = TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL;
    modal_info.x = 237;
    modal_info.y = 232;
    modal_info.text = mes_file_entry.str;
    modal_info.keys[TIG_WINDOW_MODAL_DIALOG_CHOICE_OK] = 'y';
    modal_info.keys[TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL] = 'n';
    modal_info.process = NULL;
    modal_info.redraw = gamelib_redraw;
    hrp_center(&(modal_info.x), &(modal_info.y));
    tig_window_modal_dialog(&modal_info, &choice);

    return choice;
}

// 0x541710
void mainmenu_ui_reset(void)
{
    inven_ui_destroy();
    charedit_close();
    sleep_ui_close();
    wmap_ui_close();
    logbook_ui_close();
    fate_ui_close();
    schematic_ui_close();
    gamelib_reset();
    gameuilib_reset();
}

// 0x541740
void mainmenu_ui_open(void)
{
    if (!mainmenu_ui_active) {
        dword_5C3FB8 = -1;
        if (main_menu_window_info[mainmenu_ui_window_type]->init_func != NULL) {
            main_menu_window_info[mainmenu_ui_window_type]->init_func();
        } else {
            mainmenu_ui_create_window();
        }

        if (mainmenu_ui_active) {
            if (!dword_64C38C) {
                mainmenu_ui_push_window_stack(mainmenu_ui_window_type);
            }
        }

        dword_64C38C = false;
    }
}

// 0x5417A0
void mainmenu_ui_close(bool back)
{
    // CE: bail when the mainmenu has already been closed (e.g. by
    // a sub_5412D0 exit-to-game called from inside a button's
    // execute_func mid-load). Otherwise a queued ESC processed
    // after the load completes would call this with mainmenu_ui_
    // active=false, sub_546DD0 would no-op, and the back=true
    // path would pop the window stack and re-open the parent
    // menu — leaving the user stuck on a NEW MAINMENU panel
    // while the game has already loaded behind it, with no way
    // to ESC back to the game (ESC on MAINMENU is ignored).
    if (!mainmenu_ui_active) {
        return;
    }

    if (main_menu_window_info[mainmenu_ui_window_type]->exit_func != NULL) {
        main_menu_window_info[mainmenu_ui_window_type]->exit_func();
    }

    sub_546DD0();

    if (back) {
        mainmenu_ui_window_type = mainmenu_ui_pop_window_stack();
        if (mainmenu_ui_window_type != MM_WINDOW_0) {
            mainmenu_ui_open();
        }
    }
}

// 0x5417E0
MainMenuWindowType mainmenu_ui_pop_window_stack(void)
{
    if (mainmenu_ui_num_windows > 0) {
        if (--mainmenu_ui_num_windows > 0) {
            return mainmenu_ui_window_stack[--mainmenu_ui_num_windows];
        }
    } else {
        mainmenu_ui_num_windows = 0;
    }
    return MM_WINDOW_0;
}

// 0x541810
void mainmenu_ui_push_window_stack(MainMenuWindowType window_type)
{
    mainmenu_ui_window_stack[mainmenu_ui_num_windows++] = window_type;
}

// 0x541830
void sub_541830(char* dst, const char* src)
{
    while (*src != '\0') {
        if (src[0] == '\\' && src[1] == 't') {
            strcpy(dst, "    ");
            dst += 5;
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

// 0x5418A0
void sub_5418A0(char* str, TigRect* rect, tig_font_handle_t font, unsigned int flags)
{
    TigVideoBuffer* vb;
    TigRect text_rect;
    TigRect dirty_rect;
    char* newline;
    char* chunk;
    TigFont font_desc;
    size_t pos;

    if (tig_window_vbid_get(mainmenu_ui_window_handle, &vb) != TIG_OK) {
        return;
    }

    text_rect = *rect;

    while (str != NULL) {
        newline = strstr(str, "\\n");
        if (newline != NULL) {
            *newline = '\0';
        }

        sub_541830(byte_64BC1C, str);

        chunk = byte_64BC1C;
        if ((flags & 0x1) != 0) {
            font_desc.width = 0;
            font_desc.height = 0;
            font_desc.str = chunk;
            font_desc.flags = 0;
            tig_font_measure(&font_desc);

            if (font_desc.width > rect->width) {
                pos = strlen(str);
                while (pos > 0 && font_desc.width > rect->width) {
                    chunk[pos--] = '\0';
                    font_desc.width = 0;
                    font_desc.height = 0;
                    font_desc.str = chunk;
                    font_desc.flags = 0;
                    tig_font_measure(&font_desc);
                }
            }
        } else if ((flags & 0x02) != 0) {
            font_desc.width = 0;
            font_desc.height = 0;
            font_desc.str = chunk;
            font_desc.flags = 0;
            tig_font_measure(&font_desc);

            if (font_desc.width > rect->width) {
                pos = strlen(str);
                while (pos > 0 && font_desc.width > rect->width) {
                    chunk++;
                    pos--;
                    font_desc.width = 0;
                    font_desc.height = 0;
                    font_desc.str = chunk;
                    font_desc.flags = 0;
                    tig_font_measure(&font_desc);
                }
            }
        }

        tig_font_push(font);
        if (tig_font_write(vb, chunk, &text_rect, &dirty_rect) != TIG_OK) {
            tig_debug_printf("MainMenu-UI: mmUITextWrite_func: ERROR: Couldn't write text: '%s'!\n", byte_64BC1C);
        }
        tig_font_pop();

        text_rect.y += dirty_rect.height;
        text_rect.height -= dirty_rect.height;

        if (newline != NULL) {
            *newline = '\\';
            newline += 2;
        }

        str = newline;
    }
}

// 0x541AA0
void mainmenu_ui_create_mainmenu(void)
{
    mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
    mainmenu_ui_create_window();
    ++dword_64C43C;
    mainmenu_ui_draw_version();
}

// 0x541AC0
void mainmenu_ui_draw_version(void)
{
    TigRect rect;
    char version[40];

    if (settings_get_value(&settings, SHOW_VERSION_KEY) == 0) {
        return;
    }

    if (!gamelib_copy_version(version, NULL, NULL)) {
        return;
    }

    rect.x = 10;
    rect.y = 575;
    rect.width = 400;
    rect.height = 20;

    tig_font_push(dword_64BC04[0]);
    if (tig_window_text_write(mainmenu_ui_window_handle, version, &rect) != TIG_OK) {
        tig_debug_printf("MainMenuUI: ERROR: GameLib_Version_String Failed to draw!\n");
    }
    tig_font_pop();
}

// 0x541B50
bool mainmenu_ui_press_mainmenu_in_play(tig_button_handle_t button_handle)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (button_handle == window->buttons[3].button_handle) {
        if (mainmenu_ui_confirm_quit() != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
            return false;
        }

        tig_button_hide(button_handle);

        return sub_549310(button_handle);
    }

    if (button_handle == window->buttons[4].button_handle) {
        if (stru_5C36B0[mainmenu_ui_type][0]) {
            sub_5412D0();
            return false;
        }

        mainmenu_ui_close(true);

        if (mainmenu_ui_window_type == MM_WINDOW_0) {
            sub_5412D0();
            return false;
        }

        return false;
    }

    return false;
}

// 0x541BE0
bool mainmenu_ui_press_mainmenu_in_play_locked(tig_button_handle_t button_handle)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (button_handle == window->buttons[1].button_handle) {
        if (mainmenu_ui_confirm_quit() != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
            return false;
        }

        tig_button_hide(button_handle);

        return sub_549310(button_handle);
    }

    if (button_handle == window->buttons[2].button_handle) {
        if (stru_5C36B0[mainmenu_ui_type][0]) {
            sub_5412D0();
            return false;
        }

        mainmenu_ui_close(true);

        if (mainmenu_ui_window_type == MM_WINDOW_0) {
            sub_5412D0();
            return false;
        }

        return false;
    }

    return false;
}

// 0x541C70
void mainmenu_ui_create_options(void)
{
    PcLens pc_lens;
    int64_t pc_obj;
    int64_t loc;

    sub_541D40();
    mainmenu_ui_window_type = MM_WINDOW_OPTIONS;
    mainmenu_ui_create_window_func(false);
    dword_64C440 = 0;
    options_ui_start(OPTIONS_UI_TAB_GAME, mainmenu_ui_window_handle,
        stru_5C36B0[mainmenu_ui_type][1] == 0,
        mainmenu_ui_window_rect.y);
    mainmenu_ui_draw_version();

    pc_lens.window_handle = mainmenu_ui_window_handle;
    // CE: stru_5C4490 is in design coords. When the panel's design
    // origin shifts down (hi-res top crop), every drawn element
    // anchored to design space has to subtract the same offset so
    // it lands on the right screen pixel relative to the cropped
    // chrome. The pc_lens icon is one of those — without this, the
    // icon ended up 41px below where the chrome art expects it.
    TigRect lens_rect = stru_5C4490;
    lens_rect.y -= mainmenu_ui_window_rect.y;
    pc_lens.rect = &lens_rect;
    tig_art_interface_id_create(670, 0, 0, 0, &pc_lens.art_id);
    if (stru_5C36B0[mainmenu_ui_type][0]) {
        pc_obj = player_get_local_pc_obj();
        loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
        // Opt-in via RECENTER_CAMERA_ON_OVERLAY_KEY — default off keeps
        // the player's scroll position when opening in-play Options.
        if (gamelib_recenter_camera_on_overlay()) {
            location_origin_set(loc);
        }
        intgame_pc_lens_do(PC_LENS_MODE_PASSTHROUGH, &pc_lens);
    } else {
        // Pre-game (main menu) Options has no PC to look at — hide the lens
        // entirely instead of showing the blacked-out widget.
        intgame_pc_lens_do(PC_LENS_MODE_NONE, NULL);
    }

    tig_window_display();
}

// 0x541D40
void sub_541D40(void)
{
    inven_ui_destroy();
    charedit_close();
    sleep_ui_close();
    wmap_ui_close();
    logbook_ui_close();
    fate_ui_close();
    schematic_ui_close();
}

// 0x541D70
void mainmenu_ui_destroy_options(void)
{
    options_ui_close();
    intgame_pc_lens_do(PC_LENS_MODE_NONE, NULL);
}

// 0x541D90
bool mainmenu_ui_press_options(tig_button_handle_t button_handle)
{
    int index;

    for (index = 0; index < 4; index++) {
        if (mainmenu_ui_options_buttons[index].button_handle == button_handle) {
            break;
        }
    }

    if (index >= 4) {
        return options_ui_handle_button_pressed(button_handle);
    }

    if (index != 3) {
        sub_541E20(index);
        return true;
    }

    if (mainmenu_ui_window_type == MM_WINDOW_OPTIONS) {
        if (!options_ui_load_module()) {
            return true;
        }
    }

    // Same stack-aware close as ESC: if Options was reached via a parent
    // menu (e.g. pause menu → Options), pop back to that parent. Only
    // fully exit to game when we're at the top of the stack (the O key
    // shortcut or similar direct entry).
    if (mainmenu_ui_num_windows <= 1
        && stru_5C36B0[mainmenu_ui_type][0]) {
        sub_5412D0();
    } else {
        mainmenu_ui_close(true);
        if (mainmenu_ui_window_type == MM_WINDOW_0) {
            sub_5412D0();
        }
    }

    return false;
}

// 0x541E20
void sub_541E20(int a1)
{
    if (dword_64C440 != a1 && options_ui_load_module()) {
        options_ui_close();

        dword_64C440 = a1;
        switch (dword_64C440) {
        case 0:
            options_ui_start(OPTIONS_UI_TAB_GAME, mainmenu_ui_window_handle,
                stru_5C36B0[mainmenu_ui_type][1] == 0,
                mainmenu_ui_window_rect.y);
            break;
        case 1:
            options_ui_start(OPTIONS_UI_TAB_VIDEO, mainmenu_ui_window_handle,
                stru_5C36B0[mainmenu_ui_type][1] == 0,
                mainmenu_ui_window_rect.y);
            break;
        case 2:
            options_ui_start(OPTIONS_UI_TAB_AUDIO, mainmenu_ui_window_handle,
                stru_5C36B0[mainmenu_ui_type][1] == 0,
                mainmenu_ui_window_rect.y);
            break;
        }
    }
}

// 0x541F20
void mainmenu_ui_load_game_create(void)
{
    MainMenuWindowInfo* window;
    int64_t pc_obj;
    PcLens pc_lens;

    mainmenu_ui_window_type = MM_WINDOW_LOAD_GAME;
    window = main_menu_window_info[mainmenu_ui_window_type];

    sub_542200();

    if (dword_64C37C) {
        gamelib_savelist_create_module(dword_64C37C, &mainmenu_ui_gsl);
    } else {
        // CE: aggregate saves across data\Save AND every module's own save folder,
        // each tagged with its owning module, so the Load menu shows all saves at
        // once and can auto-switch to the right module on load (no manual "set the
        // module first"). See gamelib_savelist_create_all.
        gamelib_savelist_create_all(&mainmenu_ui_gsl);
    }

    gamelib_savelist_sort(&mainmenu_ui_gsl, GAME_SAVE_LIST_ORDER_DATE, false);

    window->cnt = mainmenu_ui_gsl.count;
    if (window->selected_index == -1) {
        if (window->cnt > 0) {
            const char* path = gamelib_last_save_name();
            unsigned int index;

            window->selected_index = 0;

            if (path != NULL && *path != '\0') {
                for (index = 0; index < mainmenu_ui_gsl.count; index++) {
                    if (strcmp(mainmenu_ui_gsl.names[index], path) == 0) {
                        window->selected_index = index;
                        break;
                    }
                }
            }
        }
    } else if (window->selected_index >= window->cnt) {
        window->selected_index = window->cnt > 0 ? 0 : -1;
    }

    window->max_top_index = window->cnt - window->content_rect.height / 20 - 1;
    if (window->max_top_index < 0) {
        window->max_top_index = 0;
    }
    window->top_index = 0;

    mainmenu_ui_create_window_func(false);

    if (!main_menu_button_create_ex(&stru_5C45D8, 0, 0, 0x2)) {
        tig_debug_printf("MainMenu-UI: mainmenu_ui_create_load_game: ERROR: Failed to create button.\n");
        exit(EXIT_FAILURE);
    }

    stru_64C260.scrollbar_rect = stru_5C4798;
    stru_64C260.flags = SB_INFO_VALID
        | SB_INFO_CONTENT_RECT
        | SB_INFO_MAX_VALUE
        | SB_INFO_MIN_VALUE
        | SB_INFO_LINE_STEP
        | SB_INFO_VALUE
        | SB_INFO_ON_VALUE_CHANGED
        | SB_INFO_ON_REFRESH;
    stru_64C260.min_value = 0;
    stru_64C260.max_value = window->max_top_index + 1;
    if (stru_64C260.max_value > 0) {
        stru_64C260.max_value--;
    }
    stru_64C260.value = window->selected_index < 7 ? 0 : window->selected_index;
    stru_64C260.line_step = 1;
    stru_64C260.on_value_changed = sub_542280;
    stru_64C260.on_refresh = sub_5422A0;
    stru_64C260.content_rect.x = 34;
    stru_64C260.content_rect.y = 110;
    stru_64C260.content_rect.width = 195;
    stru_64C260.content_rect.height = 232;
    scrollbar_ui_control_create(&stru_64C220, &stru_64C260, mainmenu_ui_window_handle);

    dword_64C450 = false;

    pc_obj = player_get_local_pc_obj();

    pc_lens.window_handle = mainmenu_ui_window_handle;
    pc_lens.rect = &stru_5C4780;
    tig_art_interface_id_create(746, 0, 0, 0, &(pc_lens.art_id));

    // Opt-in via RECENTER_CAMERA_ON_OVERLAY_KEY — default off keeps the
    // viewport where the player had it when opening Load Game.
    if (pc_obj != OBJ_HANDLE_NULL && gamelib_recenter_camera_on_overlay()) {
        location_origin_set(obj_field_int64_get(pc_obj, OBJ_F_LOCATION));
    }

    if (!stru_5C36B0[mainmenu_ui_type][0]) {
        // Pre-game Load Game (reached from the main menu, no game in
        // session) — `player_get_local_pc_obj()` can return a stub PC
        // here, so gate on the same "exit to game" menu-type flag the
        // Options screen uses. No PC to look at → hide the lens entirely.
        intgame_pc_lens_do(PC_LENS_MODE_NONE, NULL);
    } else if (map_by_type(MAP_TYPE_SHOPPING_MAP) == map_current_map()) {
        intgame_pc_lens_do(PC_LENS_MODE_BLACKOUT, &pc_lens);
    } else {
        intgame_pc_lens_do(PC_LENS_MODE_PASSTHROUGH, &pc_lens);
        dword_64C450 = true;
    }

    // CE: opt the Load window into translucent-black so its near-black areas reveal
    // what's behind it -- the live game world when opened over gameplay, the menu
    // backdrop on the title screen (window-context underlay pick). Honors the
    // "translucent black ui" cfg; a no-op when off or when there's no underlay.
    intgame_apply_translucent_black_window(mainmenu_ui_window_handle, true);

    scrollbar_ui_control_redraw(stru_64C220);
    tig_window_display();
}

// 0x542200
void sub_542200(void)
{
    inven_ui_destroy();
    charedit_close();
    sleep_ui_close();
    wmap_ui_close();
    logbook_ui_close();
    fate_ui_close();
    schematic_ui_close();
}

// 0x542230
void mainmenu_ui_load_game_destroy(void)
{
    // CE: clear the translucent-black tint so it doesn't bleed onto the next screen
    // that reuses this shared mainmenu window.
    intgame_apply_translucent_black_window(mainmenu_ui_window_handle, false);

    scrollbar_ui_control_destroy(stru_64C220);

    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    gamelib_savelist_destroy(&mainmenu_ui_gsl);

    intgame_pc_lens_do(PC_LENS_MODE_NONE, NULL);
    if (dword_64C37C != NULL)
        FREE(dword_64C37C);
    dword_64C37C = NULL;
}

// 0x542280
void sub_542280(int a1)
{
    main_menu_window_info[mainmenu_ui_window_type]->top_index = a1;
}

// 0x5422A0
void sub_5422A0(TigRect* rect)
{
    (void)rect;

    main_menu_window_info[mainmenu_ui_window_type]->refresh_func(NULL);
}

// 0x5422C0
bool mainmenu_ui_load_game_execute(int btn)
{
    int index;
    char name[256];

    (void)btn;

    // CE: a queued mouse-up — the second click of a rapid double-click, or
    // input buffered during the multi-second load of the previous save —
    // can dispatch here AFTER an earlier load already completed and tore the
    // Load Game menu down. Teardown runs the window's exit_func
    // (mainmenu_ui_load_game_destroy -> gamelib_savelist_destroy), which
    // frees mainmenu_ui_gsl.names and zeroes count, while the menu window
    // lingers ~260ms through its exit animation so the stale event still
    // reaches us. window->selected_index survives in the static window
    // struct and is non-negative, so the legacy `index == -1` check passes —
    // and mainmenu_ui_gsl.names[index] then dereferences the freed NULL array
    // (faulting at names(NULL) + index*8). Same class as commit aa876684:
    // bail when the menu is gone, or when the selection is out of range for
    // the current list.
    if (mainmenu_ui_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return false;
    }

    index = main_menu_window_info[mainmenu_ui_window_type]->selected_index;
    if (index < 0
        || mainmenu_ui_gsl.names == NULL
        || (unsigned int)index >= mainmenu_ui_gsl.count) {
        return false;
    }

    strncpy(name, mainmenu_ui_gsl.names[index], 8);
    name[8] = '\0';

    // CE: pass the selected entry's OWNING MODULE (resolved by directory when the
    // list was built) as the authoritative hint. The 8-char slot alone is ambiguous
    // -- the same slot (e.g. "Slot0000") can exist under multiple modules -- so
    // re-deriving the module from the slot could switch to the wrong one. The list
    // entry already knows which module this row represents.
    return sub_5432B0(name,
        mainmenu_ui_gsl.entry_modules != NULL ? mainmenu_ui_gsl.entry_modules[index] : NULL);
}

// 0x542420
bool mainmenu_ui_load_game_button_pressed(tig_button_handle_t button_handle)
{
    if (button_handle != stru_5C45D8.button_handle) {
        return false;
    }

    dword_5C4790 = 0;
    main_menu_window_info[mainmenu_ui_window_type]->refresh_func(&stru_5C46C0);

    return true;
}

// 0x542460
bool mainmenu_ui_load_game_button_released(tig_button_handle_t button_handle)
{
    MainMenuWindowInfo* window;
    int index;

    window = main_menu_window_info[mainmenu_ui_window_type];

    for (index = 0; index < 2; index++) {
        if (button_handle == window->buttons[index].button_handle) {
            sub_5480C0(index + 2);
            return true;
        }
    }

    if (button_handle == window->buttons[2].button_handle) {
        gsound_play_sfx(0, 1);
        mainmenu_ui_load_game_handle_delete();
        return true;
    }

    if (button_handle == stru_5C45D8.button_handle) {
        dword_5C4790 = 1;
        window->refresh_func(&stru_5C46C0);
        return true;
    }

    return false;
}

// 0x5424F0
// Shared double-click tracking for the save / load slot list. A second
// click within MAINMENU_UI_DOUBLE_CLICK_MS on the same row of the same
// window fires the OK action (Load or Save) just like clicking the
// confirm button.
#define MAINMENU_UI_DOUBLE_CLICK_MS 400
static tig_timestamp_t mainmenu_ui_savelist_last_click_time;
static int mainmenu_ui_savelist_last_click_row = -1;
static MainMenuWindowType mainmenu_ui_savelist_last_click_window = MM_WINDOW_0;

static bool mainmenu_ui_savelist_register_click(int row)
{
    tig_timestamp_t now;
    bool double_click;

    tig_timer_now(&now);
    double_click = row >= 0
        && row == mainmenu_ui_savelist_last_click_row
        && mainmenu_ui_savelist_last_click_window == mainmenu_ui_window_type
        && tig_timer_elapsed(mainmenu_ui_savelist_last_click_time) <= MAINMENU_UI_DOUBLE_CLICK_MS;
    mainmenu_ui_savelist_last_click_time = now;
    mainmenu_ui_savelist_last_click_row = row;
    mainmenu_ui_savelist_last_click_window = mainmenu_ui_window_type;
    return double_click;
}

void mainmenu_ui_load_game_mouse_up(int x, int y)
{
    MainMenuWindowInfo* window;
    int row;
    bool double_click;

    (void)x;

    // CE: ignore clicks once the Load Game menu has begun tearing down. The
    // window lingers ~260ms through its exit animation (handle already
    // INVALID) but queued mouse-ups still dispatch here — and the savelist
    // they index (mainmenu_ui_gsl) has already been freed by the window's
    // exit_func, so selecting a row (sub_542560) or confirming it (sub_5480C0
    // -> mainmenu_ui_load_game_execute) would deref a NULL names array. Same
    // guard idiom as mainmenu_ui_load_game_refresh.
    if (mainmenu_ui_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }

    window = main_menu_window_info[mainmenu_ui_window_type];
    row = window->top_index + y / 20;
    if (row >= window->cnt) {
        row = -1;
    }

    double_click = mainmenu_ui_savelist_register_click(row);

    window->selected_index = row;
    sub_542560();
    window->refresh_func(NULL);
    scrollbar_ui_control_redraw(stru_64C220);

    if (double_click) {
        sub_5480C0(2);
    }
}

// 0x542560
void sub_542560(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;

        if (window->selected_index > -1
            && gamelib_saveinfo_load_located(mainmenu_ui_gsl.names[window->selected_index], &mainmenu_ui_gsi)) {
            mainmenu_ui_gsi_loaded = true;
        }
    }
}

// 0x5425C0
void mainmenu_ui_load_game_refresh(TigRect* rect)
{
    MainMenuWindowInfo* window;
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtAnimData art_anim_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigVideoBufferData video_buffer_data;
    TigWindowBlitInfo win_blit_info;
    MesFileEntry mes_file_entry;
    char str[20];
    tig_font_handle_t font;
    int area;
    char* area_name;
    char* story_state_desc;

    // CE: no-op if the menu sub-window is already gone. The window/exit
    // transition animations set mainmenu_ui_window_handle to INVALID the
    // instant an exit begins, while the real window lingers ~260ms for its
    // fade-out before mainmenu_ui_finalize_close destroys it. A refresh
    // dispatched in that gap — e.g. a redraw message pumped during a save
    // load while the menu is animating away — would blit/fill an empty
    // WinID, and the upstream fill-failure path calls exit(EXIT_FAILURE).
    // There's nothing to draw on a departing window, so bail.
    if (mainmenu_ui_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }

    window = main_menu_window_info[mainmenu_ui_window_type];
    tig_art_interface_id_create(window->background_art_num, 0, 0, 0, &art_id);
    tig_art_frame_data(art_id, &art_frame_data);
    if (tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
        src_rect.x = stru_5C4798.x;
        src_rect.y = stru_5C4798.y;
        src_rect.width = stru_5C4798.width + 1;
        src_rect.height = stru_5C4798.height + 1;

        dst_rect.x = stru_5C4798.x;
        dst_rect.y = stru_5C4798.y;
        dst_rect.width = stru_5C4798.width + 1;
        dst_rect.height = stru_5C4798.height + 1;

        art_blit_info.art_id = art_id;
        art_blit_info.flags = 0;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &dst_rect;
        tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info);
        if (mainmenu_ui_has_custom_bg && !mainmenu_ui_custom_bg_is_fallback) {
            TigWindowData wd;
            if (tig_window_data(mainmenu_ui_window_handle, &wd) == TIG_OK) {
                mainmenu_ui_blit_custom_bg_at(mainmenu_ui_window_handle, wd.rect, dst_rect);
            }
        }
    }

    if (rect == NULL
        || (window->content_rect.x < rect->x + rect->width
            && window->content_rect.y < rect->y + rect->height
            && rect->x < window->content_rect.x + window->content_rect.width
            && rect->y < window->content_rect.y + window->content_rect.height)) {
        dst_rect.x = window->content_rect.x;
        dst_rect.y = window->content_rect.y;
        dst_rect.width = window->content_rect.width;
        dst_rect.height = window->content_rect.height + 5;

        if (tig_window_fill(mainmenu_ui_window_handle, &dst_rect, tig_color_make(0, 0, 0)) != TIG_OK) {
            tig_debug_printf("mmUIMPLoadGameRefreshFunc: ERROR: tig_window_fill2 failed!\n");
            exit(EXIT_FAILURE);
        }

        dst_rect.height -= 5;

        if (mainmenu_ui_gsl.count != 0) {
            int index;
            int max_y;
            char* name;

            max_y = dst_rect.y + dst_rect.height - 1;

            dst_rect.width -= 4;
            dst_rect.height = 20;

            for (index = window->top_index; index < window->cnt; index++) {
                const char* row_tag;
                TigRect text_rect;

                if (dst_rect.y >= max_y) {
                    break;
                }

                font = window->selected_index == index ? dword_64C240 : dword_64C210[0];
                tig_font_push(font);
                name = sub_543040(index);
                text_rect = dst_rect;

                // CE: draw the "[Module]" tag dim and right-aligned, reserving its
                // width so the description truncates before it -- the module name
                // stays fully visible instead of being clipped at the row edge.
                row_tag = mainmenu_ui_row_module_tag(index);
                if (row_tag != NULL) {
                    TigFont tag_desc;
                    TigRect tag_rect;

                    tag_desc.width = 0;
                    tag_desc.height = 0;
                    tag_desc.str = (char*)row_tag;
                    tag_desc.flags = 0;
                    tig_font_push(mainmenu_ui_dim_font);
                    tig_font_measure(&tag_desc);

                    tag_rect = dst_rect;
                    tag_rect.x = dst_rect.x + dst_rect.width - tag_desc.width;
                    tag_rect.width = tag_desc.width;
                    sub_5418A0((char*)row_tag, &tag_rect, mainmenu_ui_dim_font, 0);
                    tig_font_pop();

                    text_rect.width -= tag_desc.width + 8;
                    if (text_rect.width < 0) {
                        text_rect.width = 0;
                    }
                }

                if (*name != '\0') {
                    mainmenu_ui_draw_list_name(name, &text_rect, font);
                }
                dst_rect.y += 20;
                tig_font_pop();
            }
        }
    }

    if (rect == NULL
        || (stru_5C46C0.x < rect->x + rect->width
            && stru_5C46C0.y < rect->y + rect->height
            && rect->x < stru_5C46C0.x + stru_5C46C0.width
            && rect->y < stru_5C46C0.y + stru_5C46C0.height)) {
        // CE: ALWAYS clear the detail content box when this panel is (re)drawn, before
        // we even know whether the selected save's info loads. Gating the clear on
        // gsi_loaded left the PREVIOUS save's longer text on screen whenever the newly-
        // selected save's info failed to read -- the "names collapsed onto wrong longer
        // names" bug. 281,55,468,300 is the content area inside the ornate frame; the
        // thumbnail blit below mutates stru_5C46C0 to the image size so we can't reuse it.
        {
            TigRect detail_clear = { 281, 55, 468, 300 };
            tig_window_fill(mainmenu_ui_window_handle, &detail_clear, tig_color_make(0, 0, 0));
        }

        if (window->selected_index > -1) {
            if (!mainmenu_ui_gsi_loaded
                && mainmenu_ui_gsl.count > 0
                && gamelib_saveinfo_load_located(mainmenu_ui_gsl.names[window->selected_index], &mainmenu_ui_gsi)) {
                mainmenu_ui_gsi_loaded = true;
            }

            if (mainmenu_ui_gsi_loaded) {
                if (mainmenu_ui_gsi.thumbnail_video_buffer != NULL) {
                    if (tig_video_buffer_data(mainmenu_ui_gsi.thumbnail_video_buffer, &video_buffer_data) == TIG_OK) {
                        stru_5C46B0.width = video_buffer_data.width;
                        stru_5C46B0.height = video_buffer_data.height;

                        stru_5C46C0.width = video_buffer_data.width;
                        stru_5C46C0.height = video_buffer_data.height;

                        win_blit_info.type = TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW;
                        win_blit_info.vb_blit_flags = 0;
                        win_blit_info.src_video_buffer = mainmenu_ui_gsi.thumbnail_video_buffer;
                        win_blit_info.src_rect = &stru_5C46B0;
                        win_blit_info.dst_window_handle = mainmenu_ui_window_handle;
                        win_blit_info.dst_rect = &stru_5C46C0;

                        if (tig_window_blit(&win_blit_info) != TIG_OK) {
                            tig_debug_printf("MMUI: ERROR: mmUIMPLoadGameRefreshFunc FAILED to refresh!\n");
                        }
                    }
                }

                font = dword_64C210[0];
                tig_font_push(font);

                if (mainmenu_ui_gsi.version == 25) {
                    if (dword_5C4790) {
                        sub_542DF0(mainmenu_ui_gsi.pc_name, &stru_5C46D0, font);
                        if (mainmenu_ui_gsi.pc_portrait != 0) {
                            portrait_draw_native(OBJ_HANDLE_NULL,
                                mainmenu_ui_gsi.pc_portrait,
                                mainmenu_ui_window_handle,
                                stru_5C46E0.x,
                                stru_5C46E0.y);
                        }

                        mes_file_entry.num = 5051; // "Level %d"
                        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                        snprintf(str, sizeof(str), mes_file_entry.str, mainmenu_ui_gsi.pc_level);
                        sub_542DF0(str, &stru_5C4720, font);

                        sub_542DF0(mainmenu_ui_gsi.description, &stru_5C4730, font);

                        // CE: the area NAME (map/area tables) and the quest/story text
                        // (script_story_state_info) are resolved from the CURRENTLY
                        // mounted module's data. For a save that belongs to a DIFFERENT
                        // module than the one mounted, those lookups return the wrong
                        // module's entries (e.g. a Vormantown save showed Arcanum's
                        // "Shrouded Hills" + a bogus quest). We can't load the other
                        // module's tables just to preview, so show "Unknown location."
                        // and no story for cross-module saves rather than wrong info.
                        bool same_module = mainmenu_ui_gsi.module_name[0] == '\0'
                            || SDL_strcasecmp(mainmenu_ui_gsi.module_name, gamelib_loaded_module_name_get()) == 0;

                        area = 0;
                        if (same_module) {
                            if (map_by_type(MAP_TYPE_START_MAP) == mainmenu_ui_gsi.map) {
                                area = area_get_nearest_area_in_range(mainmenu_ui_gsi.pc_location, true);
                            } else if (!map_get_area(mainmenu_ui_gsi.map, &area)) {
                                area = 0;
                            }
                        }

                        if (area > 0) {
                            area_name = area_get_name(area);
                        } else {
                            mes_file_entry.num = 5050; // "Unknown location."
                            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                            area_name = mes_file_entry.str;
                        }

                        sub_542DF0(area_name, &stru_5C46F0, font);

                        datetime_format_date(&(mainmenu_ui_gsi.datetime), str, sizeof(str));
                        sub_542EA0(str, &stru_5C4710, font);

                        datetime_format_time(&(mainmenu_ui_gsi.datetime), str, sizeof(str));
                        sub_542EA0(str, &stru_5C4700, font);

                        story_state_desc = same_module
                            ? script_story_state_info(mainmenu_ui_gsi.story_state)
                            : NULL;
                        if (story_state_desc != NULL && *story_state_desc != '\0') {
                            mmUITextWriteCenteredToArray(story_state_desc, stru_5C4740, 4, font);
                        }
                    }
                } else {
                    mes_file_entry.num = 5004; // "Version Mismatch!"
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    sub_542DF0(mes_file_entry.str, &stru_5C46D0, font);
                }
                tig_font_pop();
            } else {
                tig_window_fill(mainmenu_ui_window_handle, &stru_5C46C0, tig_color_make(0, 0, 0));

                font = dword_64C218[0];
                tig_font_push(font);
                mes_file_entry.num = 5005; // "Empty"
                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                sub_542DF0(mes_file_entry.str, &stru_5C46D0, font);
                tig_font_pop();
            }
        } else {
            tig_window_fill(mainmenu_ui_window_handle, &stru_5C46C0, tig_color_make(0, 0, 0));
        }

        tig_art_interface_id_create(748, 0, 0, 0, &art_id);
        if (tig_art_frame_data(art_id, &art_frame_data) == TIG_OK
            && tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = 281;
            dst_rect.y = 55;
            dst_rect.width = art_frame_data.width;
            dst_rect.height = art_frame_data.height;

            art_blit_info.art_id = art_id;
            art_blit_info.flags = 0;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            if (tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info) != TIG_OK) {
                tig_debug_printf("MMUI: mmUIMPLoadGameRefreshFunc: ERROR: FAILED to refresh!\n");
            }
        }
    }

    mmUIWinRefreshScrollBar();
}

// CE: draw a save list name left-aligned, truncated with a trailing "..." when it
// doesn't fit rect->width. sub_542D00 also truncates but draws its ellipsis at the
// wrong x (rect.x + ellipsis_width instead of after the text), so the dots overlap
// the start and effectively vanish -- hence list names looked hard-cut with no
// ellipsis. This builds "<prefix>..." that fits, then draws it normally.
static void mainmenu_ui_draw_list_name(const char* name, TigRect* rect, tig_font_handle_t font)
{
    char buf[COMPAT_MAX_FNAME + 4];
    TigFont fd;
    size_t len;

    fd.width = 0;
    fd.height = 0;
    fd.str = (char*)name;
    fd.flags = 0;
    tig_font_measure(&fd);
    if (fd.width <= rect->width) {
        sub_5418A0((char*)name, rect, font, 0);
        return;
    }

    len = strlen(name);
    for (;;) {
        snprintf(buf, sizeof(buf), "%.*s...", (int)len, name);
        fd.width = 0;
        fd.height = 0;
        fd.str = buf;
        fd.flags = 0;
        tig_font_measure(&fd);
        if (fd.width <= rect->width || len == 0) {
            break;
        }
        len--;
    }
    sub_5418A0(buf, rect, font, 0);
}

// 0x542D00
void sub_542D00(char* str, TigRect* rect, tig_font_handle_t font)
{
    TigFont font_desc;
    TigRect text_rect;

    font_desc.width = 0;
    font_desc.height = 0;
    font_desc.str = str;
    font_desc.flags = 0;
    tig_font_measure(&font_desc);

    text_rect = *rect;

    if (font_desc.width < rect->width) {
        sub_5418A0(str, &text_rect, font, 0);
        return;
    }

    font_desc.width = 0;
    font_desc.height = 0;
    font_desc.str = off_5C407C;
    font_desc.flags = 0;
    tig_font_measure(&font_desc);

    text_rect.width -= font_desc.width;
    sub_5418A0(str, &text_rect, font, 1);

    text_rect.x += font_desc.width;
    text_rect.width = font_desc.width;
    sub_5418A0(off_5C407C, &text_rect, font, 0);
}

// 0x542DF0
void sub_542DF0(char* str, TigRect* rect, tig_font_handle_t font)
{
    TigFont font_desc;
    TigRect text_rect;

    font_desc.width = 0;
    font_desc.height = 0;
    font_desc.str = str;
    font_desc.flags = 0;
    tig_font_measure(&font_desc);

    text_rect = *rect;
    if (font_desc.width < rect->width) {
        text_rect.x += (text_rect.width - font_desc.width) / 2;
        text_rect.width = font_desc.width + 20;
    }

    sub_5418A0(str, &text_rect, font, 0);
}

// 0x542EA0
void sub_542EA0(char* str, TigRect* rect, tig_font_handle_t font)
{
    TigFont font_desc;
    TigRect text_rect;

    font_desc.width = 0;
    font_desc.height = 0;
    font_desc.str = str;
    font_desc.flags = 0;
    tig_font_measure(&font_desc);

    text_rect = *rect;
    if (font_desc.width < rect->width) {
        text_rect.x -= font_desc.width;
        text_rect.width = font_desc.width + 10;
    }

    sub_5418A0(str, &text_rect, font, 0);
}

// CE: number of lines a greedy word-wrap at max line width W uses (a word wider
// than W still takes its own line). word_w[] = per-word pixel widths.
static int mainmenu_ui_wrap_line_count(const int* word_w, int nwords, int space_w, int W)
{
    int lines = 1;
    int cur = 0;
    int i;

    for (i = 0; i < nwords; i++) {
        if (cur != 0 && cur + space_w + word_w[i] > W) {
            lines++;
            cur = word_w[i];
        } else {
            cur += (cur == 0 ? 0 : space_w) + word_w[i];
        }
    }
    return lines;
}

// 0x542F50
// CE: BALANCED centered word-wrap into an array of cnt line rects. The original did a
// greedy wrap (fill each line to the max), which leaves a stubby last line -- e.g.
// "...you are currently attempting to" / "find its owner." Instead: find how many
// lines a full-width greedy wrap needs (clamped to cnt), then binary-search the
// SMALLEST max-line-width that still fits in that many lines, and wrap at that width.
// Minimizing the longest line makes the lines come out roughly even.
void mmUITextWriteCenteredToArray(char* str, TigRect* rects, int cnt, tig_font_handle_t font)
{
    char buf[1024];
    char* words[128];
    int word_w[128];
    char line[1024];
    TigFont fd;
    TigRect tr;
    int nwords = 0;
    int space_w;
    int full_w;
    int target_w;
    int lines_needed;
    int i;
    int line_idx;
    int cur;
    char* p;

    if (str == NULL || *str == '\0' || cnt <= 0) {
        return;
    }

    full_w = rects[0].width;

    // Tokenize (on a copy; spaces become NUL).
    snprintf(buf, sizeof(buf), "%s", str);
    p = buf;
    while (*p != '\0' && nwords < 128) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        words[nwords++] = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
    if (nwords == 0) {
        return;
    }

    for (i = 0; i < nwords; i++) {
        fd.width = 0;
        fd.height = 0;
        fd.flags = 0;
        fd.str = words[i];
        tig_font_measure(&fd);
        word_w[i] = fd.width;
    }
    fd.width = 0;
    fd.height = 0;
    fd.flags = 0;
    fd.str = " ";
    tig_font_measure(&fd);
    space_w = fd.width;

    lines_needed = mainmenu_ui_wrap_line_count(word_w, nwords, space_w, full_w);
    if (lines_needed > cnt) {
        lines_needed = cnt;
    }

    target_w = full_w;
    if (lines_needed > 1) {
        int lo = 0;
        int hi = full_w;
        for (i = 0; i < nwords; i++) {
            if (word_w[i] > lo) {
                lo = word_w[i]; // can't wrap narrower than the widest single word
            }
        }
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (mainmenu_ui_wrap_line_count(word_w, nwords, space_w, mid) <= lines_needed) {
                target_w = mid;
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        }
    }

    // Greedy-wrap at target_w; draw each line centered in its rect.
    line[0] = '\0';
    cur = 0;
    line_idx = 0;
    for (i = 0; i < nwords && line_idx < cnt; i++) {
        if (cur != 0 && cur + space_w + word_w[i] > target_w) {
            fd.width = 0;
            fd.height = 0;
            fd.flags = 0;
            fd.str = line;
            tig_font_measure(&fd);
            tr = rects[line_idx];
            if (fd.width < tr.width) {
                tr.x += (tr.width - fd.width) / 2;
                tr.width = fd.width;
            }
            sub_5418A0(line, &tr, font, 0);
            line_idx++;
            line[0] = '\0';
            cur = 0;
            if (line_idx >= cnt) {
                break;
            }
        }
        if (cur != 0) {
            strcat(line, " ");
            cur += space_w;
        }
        strcat(line, words[i]);
        cur += word_w[i];
    }
    if (line[0] != '\0' && line_idx < cnt) {
        fd.width = 0;
        fd.height = 0;
        fd.flags = 0;
        fd.str = line;
        tig_font_measure(&fd);
        tr = rects[line_idx];
        if (fd.width < tr.width) {
            tr.x += (tr.width - fd.width) / 2;
            tr.width = fd.width;
        }
        sub_5418A0(line, &tr, font, 0);
    }
}

// 0x543040
char* sub_543040(int index)
{
    if (mainmenu_ui_gsl.names != NULL) {
        return mainmenu_ui_gsl.names[index] + 8;
    } else {
        return "";
    }
}

// CE: the owning-module tag string ("[Module]") for a Load-menu row, or NULL if the
// list isn't module-tagged (the Save menu's plain list). Drawn separately, dim and
// right-aligned, by the Load refresh -- not folded into sub_543040 -- so the module
// name never gets clipped by description truncation.
static const char* mainmenu_ui_row_module_tag(int index)
{
    static char buf[64];

    if ((mainmenu_ui_window_type != MM_WINDOW_LOAD_GAME
            && mainmenu_ui_window_type != MM_WINDOW_SAVE_GAME)
        || mainmenu_ui_gsl.entry_modules == NULL
        || mainmenu_ui_gsl.entry_modules[index] == NULL) {
        return NULL;
    }

    snprintf(buf, sizeof(buf), "[%s]", mainmenu_ui_gsl.entry_modules[index]);
    return buf;
}

// 0x543060
void sub_543060(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->selected_index > 0) {
        window->selected_index--;
        if (window->selected_index < window->top_index) {
            scrollbar_ui_control_set(stru_64C220, SCROLLBAR_CURRENT_VALUE, window->selected_index);
        }
        gsound_play_sfx(0, 1);
        sub_542560();
        window->refresh_func(NULL);
        scrollbar_ui_control_redraw(stru_64C220);
    }
}

// 0x5430D0
void sub_5430D0(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->selected_index < window->cnt - 1) {
        window->selected_index++;
        if (window->selected_index > window->top_index + window->content_rect.height / 20) {
            scrollbar_ui_control_set(stru_64C220, SCROLLBAR_CURRENT_VALUE, window->selected_index - window->content_rect.height / 20);
        }
        gsound_play_sfx(0, 1);
        sub_542560();
        window->refresh_func(NULL);
        scrollbar_ui_control_redraw(stru_64C220);
    }
}

// 0x543160
bool mainmenu_ui_load_game_handle_delete(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->selected_index == -1) {
        return false;
    }

    // "Are you sure you want to delete the save game?"
    if (mainmenu_ui_confirm(5150) != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
        return false;
    }

    if (!gamelib_delete(mainmenu_ui_gsl.names[window->selected_index])) {
        return false;
    }

    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    // CE: rebuild the SAME aggregated, module-tagged list the Load menu uses (was
    // gamelib_savelist_create, which dropped cross-module entries and the [Module]
    // labels after a delete). Take the count from the rebuilt list rather than a
    // blind cnt-- (cross-module deletes may remove an entry not in the old count).
    gamelib_savelist_destroy(&mainmenu_ui_gsl);
    gamelib_savelist_create_all(&mainmenu_ui_gsl);

    gamelib_savelist_sort(&mainmenu_ui_gsl, GAME_SAVE_LIST_ORDER_DATE, false);
    window->selected_index = -1;
    window->cnt = mainmenu_ui_gsl.count;
    window->refresh_func(NULL);

    return true;
}

// 0x543220
bool sub_543220(void)
{
    const char* path;
    char name[256];
    bool rc;

    if (mainmenu_ui_active) {
        return false;
    }

    path = gamelib_last_save_name();
    if (path[0] == '\0' || !gamelib_saveinfo_load_located(path, &mainmenu_ui_gsi)) {
        return false;
    }
    mainmenu_ui_gsi_loaded = true;

    strncpy(name, path, 8);
    name[8] = '\0';

    rc = sub_5432B0(name, NULL);
    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    return rc;
}

// 0x5432B0
bool sub_5432B0(const char* name, const char* module_hint)
{
    MesFileEntry mes_file_entry;
    UiMessage ui_message;
    char save_module[TIG_MAX_PATH];
    GameSaveInfo save_info;
    bool save_info_ok;

    sub_542200();

    // CE: auto-switch to the module this save belongs to BEFORE anything reads the
    // save, so the player no longer has to set the module manually first. This MUST
    // happen before reading the saveinfo and before gamelib_load: a cross-module
    // save's .gsi/.tfaf only resolve once its module is mounted, so doing the version
    // check first (under the wrong module) wrongly reported "corrupt". Mirrors the
    // options-screen switch (gamelib_mod_load + gameuilib_mod_load).
    //
    // module_hint is the authoritative owning module from the Load list entry (which
    // resolved it by directory). Prefer it: the 8-char slot alone is ambiguous when
    // the same slot exists under multiple modules. Fall back to a directory search by
    // slot only when no hint is given (e.g. the "continue last save" path).
    save_module[0] = '\0';
    if (module_hint != NULL && module_hint[0] != '\0') {
        snprintf(save_module, sizeof(save_module), "%s", module_hint);
    } else if (!gamelib_find_save_module(name, save_module, sizeof(save_module))) {
        save_module[0] = '\0';
    }

    if (save_module[0] != '\0'
        && SDL_strcasecmp(save_module, gamelib_loaded_module_name_get()) != 0) {
        // Bracket the switch: gamelib_mod_load runs gameinit_reset, whose throwaway
        // fresh-game (start map + PC) would otherwise flush start-map mobiles into
        // Save\Current and leak into the loaded save. The save's own load restores the
        // real map/PC. gamelib_load (below) doesn't run gameinit, so the bracket can
        // close right after the switch.
        gamelib_loading_active_set(true);
        if (gamelib_mod_load(save_module)) {
            gameuilib_mod_load();
        } else {
            tig_debug_printf("mainmenu_ui: WARNING: auto-switch to module '%s' failed; loading with current module\n",
                save_module);
        }
        gamelib_loading_active_set(false);
    }

    // Read the saveinfo now that the save's module is mounted (recovering the full
    // <slot><description> base name from the slot). Self-contained -- do not rely on
    // the caller's mainmenu_ui_gsi, which may have been read under a different module.
    save_info_ok = gamelib_saveinfo_load_by_slot(name, &save_info);

    if (save_info_ok && save_info.version == 25) {
        mainmenu_ui_reset();
        sub_40DAB0();

        if (gamelib_load(name)) {
            // Prefer the directory-derived module (authoritative) over the .gsi stamp
            // for the active module name, so a later save re-stamps it correctly.
            gamelib_current_mode_name_set(save_module[0] != '\0' ? save_module : save_info.module_name);
            gamelib_saveinfo_exit(&save_info);

            mes_file_entry.num = 5000; // "Game Loaded Successfully."
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

            ui_message.type = UI_MSG_TYPE_EXCLAMATION;
            ui_message.str = mes_file_entry.str;
            ui_display_msg(&ui_message);

            mainmenu_ui_start_new_game = false;
            sub_5412D0();

            return true;
        }

        mainmenu_ui_reset();
    }

    if (save_info_ok) {
        gamelib_saveinfo_exit(&save_info);
    }

    mes_file_entry.num = 5001; // "Save Game Corrupt!  Load Failed!"
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

    ui_message.type = UI_MSG_TYPE_EXCLAMATION;
    ui_message.str = mes_file_entry.str;
    ui_display_msg(&ui_message);

    return false;
}

// 0x543380
void mainmenu_ui_save_game_create(void)
{
    MainMenuWindowInfo* window;
    int64_t pc_obj;
    PcLens pc_lens;

    mainmenu_ui_window_type = MM_WINDOW_SAVE_GAME;
    window = main_menu_window_info[mainmenu_ui_window_type];

    sub_542200();
    gamelib_savelist_create(&mainmenu_ui_gsl);
    // CE: tag each row with its owning module so the Save menu shows the same dim
    // "[Module]" labels as the Load menu. The list stays current-context (saving
    // writes to the current module), only the labels are added.
    gamelib_savelist_tag_modules(&mainmenu_ui_gsl);
    gamelib_savelist_sort(&mainmenu_ui_gsl, GAME_SAVE_LIST_ORDER_DATE, false);
    mainmenu_ui_textedit_buffer[0] = '\0';
    window->cnt = mainmenu_ui_gsl.count + 1;
    if (mainmenu_ui_gsl.count != 0) {
        window->selected_index = 1;
    } else {
        window->selected_index = -1;
    }

    window->max_top_index = window->cnt - window->content_rect.height / 20 - 1;
    if (window->max_top_index < 0) {
        window->max_top_index = 0;
    }

    window->top_index = 0;
    mainmenu_ui_create_window_func(false);

    if (!main_menu_button_create_ex(&stru_5C4838, 0, 0, 2)) {
        tig_debug_printf("MainMenu-UI: mainmenu_ui_create_save_game: ERROR: Failed to create button.\n");
        exit(EXIT_FAILURE);
    }

    stru_64C260.scrollbar_rect = stru_5C4798;
    stru_64C260.flags = SB_INFO_VALID
        | SB_INFO_CONTENT_RECT
        | SB_INFO_MAX_VALUE
        | SB_INFO_MIN_VALUE
        | SB_INFO_LINE_STEP
        | SB_INFO_VALUE
        | SB_INFO_ON_VALUE_CHANGED
        | SB_INFO_ON_REFRESH;
    stru_64C260.min_value = 0;
    stru_64C260.max_value = window->max_top_index + 1;
    if (stru_64C260.max_value > 0) {
        stru_64C260.max_value--;
    }
    stru_64C260.value = 0;
    stru_64C260.line_step = 1;
    stru_64C260.on_value_changed = sub_542280;
    stru_64C260.on_refresh = sub_5422A0;
    stru_64C260.content_rect.x = 34;
    stru_64C260.content_rect.y = 110;
    stru_64C260.content_rect.width = 195;
    stru_64C260.content_rect.height = 232;
    scrollbar_ui_control_create(&stru_64C220, &stru_64C260, mainmenu_ui_window_handle);

    pc_obj = player_get_local_pc_obj();

    pc_lens.window_handle = mainmenu_ui_window_handle;
    pc_lens.rect = &stru_5C4780;
    tig_art_interface_id_create(746, 0, 0, 0, &(pc_lens.art_id));

    // Opt-in via RECENTER_CAMERA_ON_OVERLAY_KEY — default off preserves
    // the player's scroll position when opening Save Game.
    if (pc_obj != OBJ_HANDLE_NULL && gamelib_recenter_camera_on_overlay()) {
        location_origin_set(obj_field_int64_get(pc_obj, OBJ_F_LOCATION));
    }

    intgame_pc_lens_do(PC_LENS_MODE_PASSTHROUGH, &pc_lens);

    // CE: same translucent-black opt-in as the Load window (see there).
    intgame_apply_translucent_black_window(mainmenu_ui_window_handle, true);

    scrollbar_ui_control_redraw(stru_64C220);

    tig_window_display();
}

// 0x543580
void mainmenu_ui_save_game_destroy(void)
{
    // CE: clear the translucent-black tint so it doesn't bleed onto the next screen
    // that reuses this shared mainmenu window.
    intgame_apply_translucent_black_window(mainmenu_ui_window_handle, false);

    scrollbar_ui_control_destroy(stru_64C220);

    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    gamelib_savelist_destroy(&mainmenu_ui_gsl);
    intgame_pc_lens_do(PC_LENS_MODE_NONE, NULL);
}

// 0x5435D0
bool mainmenu_ui_save_game_execute(int btn)
{
    int v1;
    char fname[COMPAT_MAX_FNAME];
    const char* name;
    MesFileEntry mes_file_entry;
    UiMessage ui_message;
    int num;

    (void)btn;

    v1 = main_menu_window_info[mainmenu_ui_window_type]->selected_index;
    if (v1 == -1) {
        return false;
    }

    if (v1 > 0) {
        name = strcpy(fname, mainmenu_ui_gsl.names[v1 - 1]);
        strcpy(mainmenu_ui_textedit_buffer, mainmenu_ui_gsl.names[v1 - 1] + 8);
        fname[8] = '\0';

        if (mainmenu_ui_confirm(5064)) {
            return false;
        }
    } else {
        gamelib_savelist_sort(&mainmenu_ui_gsl, GAME_SAVE_LIST_ORDER_NAME, false);

        if (mainmenu_ui_gsl.count > 0) {
            if (SDL_toupper(mainmenu_ui_gsl.names[0][4]) == 'A') {
                if (mainmenu_ui_gsl.count > 1
                    && mainmenu_ui_gsl.names[1] != NULL) {
                    strncpy(fname, mainmenu_ui_gsl.names[1], 8);
                    fname[8] = '\0';
                    num = atoi(&(fname[4])) + 1;
                    if (num >= 9999) {
                        return false;
                    }

                    strcpy(fname, mainmenu_ui_gsl.names[0]);
                    snprintf(&(fname[4]), sizeof(fname) - 4, "%04d", num);
                    name = fname;
                } else {
                    name = "Slot0000";
                }
            } else {
                if (mainmenu_ui_gsl.names[0] != NULL) {
                    strncpy(fname, mainmenu_ui_gsl.names[0], 8);
                    fname[8] = '\0';
                    num = atoi(&(fname[4])) + 1;
                    if (num >= 9999) {
                        return false;
                    }

                    strcpy(fname, mainmenu_ui_gsl.names[0]);
                    snprintf(&(fname[4]), sizeof(fname) - 4, "%04d", num);
                    name = fname;
                } else {
                    name = "Slot0000";
                }
            }
        } else {
            name = "Slot0000";
        }
    }

    sub_542200();

    if (mainmenu_ui_textedit_buffer[0] != '\0') {
        char* pch = mainmenu_ui_textedit_buffer;
        while (*pch == ' ') {
            pch++;
        }

        if (*pch == '\0') {
            mainmenu_ui_textedit_buffer[0] = '\0';
        }
    }

    if (mainmenu_ui_textedit_buffer[0] == '\0') {
        strcpy(mainmenu_ui_textedit_buffer, name);
    }

    if (!gamelib_save(name, mainmenu_ui_textedit_buffer)) {
        mes_file_entry.num = 5003; // "Save Game Corrupt!  Save Failed!"
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

        ui_message.type = UI_MSG_TYPE_EXCLAMATION;
        ui_message.str = mes_file_entry.str;
        ui_display_msg(&ui_message);

        return false;
    }

    mes_file_entry.num = 5002; // "Game Saved Successfully."
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

    ui_message.type = UI_MSG_TYPE_EXCLAMATION;
    ui_message.str = mes_file_entry.str;
    ui_display_msg(&ui_message);
    sub_5412D0();
    mainmenu_ui_textedit_buffer[0] = '\0';

    return true;
}

// 0x543850
bool mainmenu_ui_save_game_button_pressed(tig_button_handle_t button_handle)
{
    if (button_handle != stru_5C4838.button_handle) {
        return false;
    }

    dword_5C4790 = 0;
    main_menu_window_info[mainmenu_ui_window_type]->refresh_func(&stru_5C46C0);

    return true;
}

// 0x543890
bool mainmenu_ui_save_game_button_released(tig_button_handle_t button_handle)
{
    MainMenuWindowInfo* window;
    int index;

    window = main_menu_window_info[mainmenu_ui_window_type];
    for (index = 0; index < 2; index++) {
        if (button_handle == window->buttons[index].button_handle) {
            sub_5480C0(index + 2);
            return true;
        }
    }

    if (button_handle == window->buttons[2].button_handle) {
        gsound_play_sfx(0, 1);
        mainmenu_ui_save_game_handle_delete();
        return true;
    }

    if (button_handle == stru_5C4838.button_handle) {
        dword_5C4790 = 1;
        window->refresh_func(&stru_5C46C0);
        return true;
    }

    return false;
}

// 0x543920
void mainmenu_ui_save_game_mouse_up(int x, int y)
{
    MainMenuWindowInfo* window;
    int row;
    bool double_click;

    (void)x;

    window = main_menu_window_info[mainmenu_ui_window_type];
    row = window->top_index + y / 20;
    if (row >= window->cnt) {
        row = window->cnt - 1;
    }

    double_click = mainmenu_ui_savelist_register_click(row);

    window->selected_index = row;
    sub_544290();
    window->refresh_func(NULL);
    scrollbar_ui_control_redraw(stru_64C220);

    if (double_click) {
        // Double-click on a save slot = press the Save button.
        sub_5480C0(2);
    }
}

// 0x543990
void mainmenu_ui_save_game_refresh(TigRect* rect)
{
    MainMenuWindowInfo* window;
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtAnimData art_anim_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigVideoBufferData video_buffer_data;
    TigWindowBlitInfo win_blit_info;
    MesFileEntry mes_file_entry;
    char str[20];
    tig_font_handle_t font;
    int area;
    char* area_name;
    char* story_state_desc;

    // CE: no-op if the menu sub-window is already gone. The window/exit
    // transition animations set mainmenu_ui_window_handle to INVALID the
    // instant an exit begins, while the real window lingers ~260ms for its
    // fade-out before mainmenu_ui_finalize_close destroys it. A refresh
    // dispatched in that gap — e.g. a redraw message pumped during a save
    // load while the menu is animating away — would blit/fill an empty
    // WinID, and the upstream fill-failure path calls exit(EXIT_FAILURE).
    // There's nothing to draw on a departing window, so bail.
    if (mainmenu_ui_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }

    window = main_menu_window_info[mainmenu_ui_window_type];
    tig_art_interface_id_create(window->background_art_num, 0, 0, 0, &art_id);
    tig_art_frame_data(art_id, &art_frame_data);
    if (tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
        src_rect.x = stru_5C4798.x;
        src_rect.y = stru_5C4798.y;
        src_rect.width = stru_5C4798.width + 1;
        src_rect.height = stru_5C4798.height + 1;

        dst_rect.x = stru_5C4798.x;
        dst_rect.y = stru_5C4798.y;
        dst_rect.width = stru_5C4798.width + 1;
        dst_rect.height = stru_5C4798.height + 1;

        art_blit_info.art_id = art_id;
        art_blit_info.flags = 0;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &dst_rect;
        tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info);
        if (mainmenu_ui_has_custom_bg && !mainmenu_ui_custom_bg_is_fallback) {
            TigWindowData wd;
            if (tig_window_data(mainmenu_ui_window_handle, &wd) == TIG_OK) {
                mainmenu_ui_blit_custom_bg_at(mainmenu_ui_window_handle, wd.rect, dst_rect);
            }
        }
    }

    if (rect == NULL
        || (window->content_rect.x < rect->x + rect->width
            && window->content_rect.y < rect->y + rect->height
            && rect->x < window->content_rect.x + window->content_rect.width
            && rect->y < window->content_rect.y + window->content_rect.height)) {
        dst_rect = window->content_rect;

        if (tig_window_fill(mainmenu_ui_window_handle, &dst_rect, tig_color_make(0, 0, 0)) != TIG_OK) {
            tig_debug_printf("mmUIMPSaveGameRefreshFunc: ERROR: tig_window_fill2 failed!\n");
            exit(EXIT_FAILURE);
        }

        int max_y = dst_rect.y + dst_rect.height - 1;

        dst_rect.height = 20;
        dst_rect.width -= 4;

        for (int idx = window->top_index; idx < window->cnt; idx++) {
            if (dst_rect.y >= max_y) {
                break;
            }

            font = window->selected_index == idx ? dword_64C240 : dword_64C210[0];
            tig_font_push(font);

            char* name;
            const char* row_tag = NULL;
            TigRect text_rect = dst_rect;
            if (idx > 0) {
                name = sub_543040(idx - 1);
                // Existing-save rows get the same dim "[Module]" tag as the Load menu.
                row_tag = mainmenu_ui_row_module_tag(idx - 1);
            } else if (idx == 0) {
                if (textedit_ui_is_focused()) {
                    name = NULL;
                    sub_544100(mainmenu_ui_textedit_buffer, &dst_rect, font, true);
                } else {
                    mes_file_entry.num = 5055;
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    name = mes_file_entry.str;
                }
            }

            if (row_tag != NULL) {
                TigFont tag_desc;
                TigRect tag_rect;

                tag_desc.width = 0;
                tag_desc.height = 0;
                tag_desc.str = (char*)row_tag;
                tag_desc.flags = 0;
                tig_font_push(mainmenu_ui_dim_font);
                tig_font_measure(&tag_desc);

                tag_rect = dst_rect;
                tag_rect.x = dst_rect.x + dst_rect.width - tag_desc.width;
                tag_rect.width = tag_desc.width;
                sub_5418A0((char*)row_tag, &tag_rect, mainmenu_ui_dim_font, 0);
                tig_font_pop();

                text_rect.width -= tag_desc.width + 8;
                if (text_rect.width < 0) {
                    text_rect.width = 0;
                }
            }

            if (name != NULL && *name != '\0') {
                mainmenu_ui_draw_list_name(name, &text_rect, font);
            }

            dst_rect.y += 20;
            tig_font_pop();
        }
    }

    if (rect == NULL
        || (stru_5C46C0.x < rect->x + rect->width
            && stru_5C46C0.y < rect->y + rect->height
            && rect->x < stru_5C46C0.x + stru_5C46C0.width
            && rect->y < stru_5C46C0.y + stru_5C46C0.height)) {
        // CE: ALWAYS clear the detail content box when redrawn (see the Load refresh):
        // gating it on gsi_loaded left stale text from the previous save when the newly
        // selected one's info failed to read.
        {
            TigRect detail_clear = { 281, 55, 468, 300 };
            tig_window_fill(mainmenu_ui_window_handle, &detail_clear, tig_color_make(0, 0, 0));
        }

        if (window->selected_index > 0) {
            if (!mainmenu_ui_gsi_loaded
                && mainmenu_ui_gsl.count > 0
                && gamelib_saveinfo_load_located(mainmenu_ui_gsl.names[window->selected_index - 1], &mainmenu_ui_gsi)) {
                mainmenu_ui_gsi_loaded = true;
            }

            if (mainmenu_ui_gsi_loaded) {
                if (mainmenu_ui_gsi.thumbnail_video_buffer != NULL) {
                    if (tig_video_buffer_data(mainmenu_ui_gsi.thumbnail_video_buffer, &video_buffer_data) == TIG_OK) {
                        stru_5C46B0.width = video_buffer_data.width;
                        stru_5C46B0.height = video_buffer_data.height;

                        stru_5C46C0.width = video_buffer_data.width;
                        stru_5C46C0.height = video_buffer_data.height;

                        win_blit_info.type = TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW;
                        win_blit_info.vb_blit_flags = 0;
                        win_blit_info.src_video_buffer = mainmenu_ui_gsi.thumbnail_video_buffer;
                        win_blit_info.src_rect = &stru_5C46B0;
                        win_blit_info.dst_window_handle = mainmenu_ui_window_handle;
                        win_blit_info.dst_rect = &stru_5C46C0;

                        if (tig_window_blit(&win_blit_info) != TIG_OK) {
                            tig_debug_printf("MMUI: ERROR: mmUIMPSaveGameRefreshFunc FAILED to refresh!\n");
                        }
                    }
                }

                font = dword_64C210[0];
                tig_font_push(font);

                if (dword_5C4790) {
                    sub_542DF0(mainmenu_ui_gsi.pc_name, &stru_5C46D0, font);
                    if (mainmenu_ui_gsi.pc_portrait != 0) {
                        portrait_draw_native(OBJ_HANDLE_NULL,
                            mainmenu_ui_gsi.pc_portrait,
                            mainmenu_ui_window_handle,
                            stru_5C46E0.x,
                            stru_5C46E0.y);
                    }

                    mes_file_entry.num = 5051; // "Level %d"
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    snprintf(str, sizeof(str), mes_file_entry.str, mainmenu_ui_gsi.pc_level);
                    sub_542DF0(str, &stru_5C4720, font);

                    sub_542DF0(mainmenu_ui_gsi.description, &stru_5C4730, font);

                    if (map_by_type(MAP_TYPE_START_MAP) == mainmenu_ui_gsi.map) {
                        area = area_get_nearest_area_in_range(mainmenu_ui_gsi.pc_location, true);
                    } else if (!map_get_area(mainmenu_ui_gsi.map, &area)) {
                        area = 0;
                    }

                    if (area > 0) {
                        area_name = area_get_name(area);
                    } else {
                        mes_file_entry.num = 5050; // "Unknown location."
                        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                        area_name = mes_file_entry.str;
                    }

                    sub_542DF0(area_name, &stru_5C46F0, font);

                    datetime_format_date(&(mainmenu_ui_gsi.datetime), str, sizeof(str));
                    sub_542EA0(str, &stru_5C4710, font);

                    datetime_format_time(&(mainmenu_ui_gsi.datetime), str, sizeof(str));
                    sub_542EA0(str, &stru_5C4700, font);

                    story_state_desc = script_story_state_info(mainmenu_ui_gsi.story_state);
                    if (story_state_desc != NULL && *story_state_desc != '\0') {
                        mmUITextWriteCenteredToArray(story_state_desc, stru_5C4740, 4, font);
                    }
                }

                tig_font_pop();
            } else {
                tig_window_fill(mainmenu_ui_window_handle, &stru_5C46C0, tig_color_make(0, 0, 0));

                font = dword_64C218[0];
                tig_font_push(font);
                mes_file_entry.num = 5005; // "Empty"
                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                sub_542DF0(mes_file_entry.str, &stru_5C46D0, font);
                tig_font_pop();
            }
        } else {
            tig_window_fill(mainmenu_ui_window_handle, &stru_5C46C0, tig_color_make(0, 0, 0));
        }

        tig_art_interface_id_create(748, 0, 0, 0, &art_id);
        if (tig_art_frame_data(art_id, &art_frame_data) == TIG_OK
            && tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = 281;
            dst_rect.y = 55;
            dst_rect.width = art_frame_data.width;
            dst_rect.height = art_frame_data.height;

            art_blit_info.art_id = art_id;
            art_blit_info.flags = 0;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            if (tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info) != TIG_OK) {
                // FIXME: Misleading message.
                tig_debug_printf("MMUI: mmUIMPLoadGameRefreshFunc: ERROR: FAILED to refresh!\n");
            }
        }
    }

    mmUIWinRefreshScrollBar();
}

// 0x544100
void sub_544100(const char* str, TigRect* rect, tig_font_handle_t font, bool left_align)
{
    char mutable_str[200];
    TigFont font_desc;
    TigRect text_rect;
    int pos;

    strcpy(mutable_str, str);

    tig_font_push(font);
    font_desc.width = 0;
    font_desc.height = 0;
    font_desc.str = mutable_str;
    font_desc.flags = 0;
    tig_font_measure(&font_desc);

    text_rect = *rect;

    // CE: the Save-name input is left-aligned so the I-beam starts where the "New"
    // prompt was (left edge), instead of jumping to the row center the moment you
    // focus it. The new-CHARACTER name field stays centered (left_align == false).
    if (left_align) {
        if (font_desc.width < rect->width) {
            text_rect.width = font_desc.width;
        }
        sub_5418A0(mutable_str, &text_rect, font, 0);
    } else if (font_desc.width < rect->width) {
        text_rect.x += (rect->width - font_desc.width) / 2;
        text_rect.width = font_desc.width;
        sub_5418A0(mutable_str, &text_rect, font, 0);
    }

    // CE: There's a bug in the original game regarding cursor positioning. It
    // simply places the cursor at the end of the editable string, but the
    // text-edit UI actually keeps track of the cursor position with the
    // keyboard arrow keys. This isn't reflected in the UI, though backspace and
    // delete operate at the correct indexes. The fix is to render cursor
    // separately in a second pass over the already rendered text.
    pos = textedit_ui_pos_get();

    // Clamp editable string at cursor position, and re-measure part of the
    // string before I-beam.
    mutable_str[pos] = '\0';
    font_desc.width = 0;
    font_desc.height = 0;
    tig_font_measure(&font_desc);
    text_rect.x += font_desc.width;

    // Measure the width of I-beam.
    mutable_str[0] = '|';
    mutable_str[1] = '\0';
    font_desc.width = 0;
    font_desc.height = 0;
    tig_font_measure(&font_desc);
    text_rect.width = font_desc.width;

    // Put I-beam into the right place.
    sub_5418A0(mutable_str, &text_rect, font, 0);

    tig_font_pop();
}

// 0x544210
void sub_544210(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->selected_index > 0) {
        window->selected_index--;
        gsound_play_sfx(0, 1);
        sub_544290();
        window->refresh_func(NULL);
    }
}

// 0x544250
void sub_544250(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->selected_index < window->cnt - 1) {
        window->selected_index++;
        gsound_play_sfx(0, 1);
        sub_544290();
        window->refresh_func(NULL);
    }
}

// 0x544290
void sub_544290(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    sub_549450();

    if (!mainmenu_ui_gsi_loaded) {
        if (window->selected_index == 0) {
            mainmenu_ui_textedit_buffer[0] = 0;
            sub_5493C0(mainmenu_ui_textedit_buffer, 23);
        }
        return;
    }

    gamelib_saveinfo_exit(&mainmenu_ui_gsi);

    mainmenu_ui_gsi_loaded = false;
    if (window->selected_index > 0) {
        if (gamelib_saveinfo_load_located(mainmenu_ui_gsl.names[window->selected_index - 1], &mainmenu_ui_gsi)) {
            mainmenu_ui_gsi_loaded = true;
        }
    } else {
        mainmenu_ui_textedit_buffer[0] = 0;
        sub_5493C0(mainmenu_ui_textedit_buffer, 23);
    }
}

// 0x544320
bool mainmenu_ui_save_game_handle_delete(void)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];

    if (window->selected_index <= 0) {
        return false;
    }

    // "Are you sure you want to delete the save game?"
    if (mainmenu_ui_confirm(5150) != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
        return false;
    }

    if (!gamelib_delete(mainmenu_ui_gsl.names[window->selected_index - 1])) {
        return false;
    }

    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    // CE: rebuild current-context + re-tag with modules (was create with no tags ->
    // labels vanished after a delete). The Save list has a leading new-save slot, so
    // the window count is the list count + 1.
    gamelib_savelist_destroy(&mainmenu_ui_gsl);
    gamelib_savelist_create(&mainmenu_ui_gsl);
    gamelib_savelist_tag_modules(&mainmenu_ui_gsl);
    gamelib_savelist_sort(&mainmenu_ui_gsl, GAME_SAVE_LIST_ORDER_DATE, false);
    window->selected_index = -1;
    window->cnt = mainmenu_ui_gsl.count + 1;
    window->refresh_func(NULL);

    return true;
}

// 0x544440
void mainmenu_ui_last_save_create(void)
{
    const char* path;
    TigRect rect;
    MesFileEntry mes_file_entry;
    char name[256];

    mainmenu_ui_window_type = MM_WINDOW_LAST_SAVE_GAME;
    mainmenu_ui_create_window();
    mainmenu_ui_active = false;

    path = gamelib_last_save_name();
    if (path[0] != '\0') {
        if (mainmenu_ui_gsi_loaded) {
            gamelib_saveinfo_exit(&mainmenu_ui_gsi);
            mainmenu_ui_gsi_loaded = false;
        }

        if (gamelib_saveinfo_load_located(path, &mainmenu_ui_gsi)) {
            mainmenu_ui_gsi_loaded = true;
            mainmenu_ui_active = true;

            rect.x = 0;
            rect.y = 0;
            rect.width = 800;
            rect.height = 600;
            tig_window_fill(mainmenu_ui_window_handle, &rect, tig_color_make(0, 0, 0));

            // CE: in hi-res the panel is 800x600 centered on a larger
            // backdrop window (mainmenu_bg art). The black panel fill
            // above only covers the panel; the backdrop's mainmenu_bg
            // would stay visible around it during the load. Fill the
            // backdrop too so the entire screen blacks out while the
            // save loads.
            //
            // CE: fill the backdrop's FULL extent, not a screen-sized rect
            // at its local origin. The backdrop is oversized (screen ×
            // 1/0.96) and its local (0,0) maps ~2% off the top-left of the
            // screen, so a screen-sized fill at (0,0) is offset from the
            // screen-visible center and leaves the bg showing along the
            // right/bottom edges — the "perimeter edge" seen during load.
            if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
                TigWindowData backdrop_wd;
                if (tig_window_data(mainmenu_ui_backdrop_handle, &backdrop_wd) == TIG_OK) {
                    TigRect backdrop_rect;
                    backdrop_rect.x = 0;
                    backdrop_rect.y = 0;
                    backdrop_rect.width = backdrop_wd.rect.width;
                    backdrop_rect.height = backdrop_wd.rect.height;
                    tig_window_fill(mainmenu_ui_backdrop_handle,
                        &backdrop_rect,
                        tig_color_make(0, 0, 0));
                }
            }

            rect.x = 340;
            rect.y = 210;
            rect.width = 200;
            rect.height = 20;

            mes_file_entry.num = 5; // "Loading Save Game..."
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
            tig_window_text_write(mainmenu_ui_window_handle, mes_file_entry.str, &rect);

            tig_mouse_hide();
            tig_window_display();
            strncpy(name, path, 8);
            name[8] = '\0';
            sub_5432B0(name, NULL);

            if (mainmenu_ui_gsi_loaded) {
                gamelib_saveinfo_exit(&mainmenu_ui_gsi);
                mainmenu_ui_gsi_loaded = false;
            }
        }
    } else {
        mainmenu_ui_active = true;
        mainmenu_ui_close(true);
        dword_64C38C = true;
    }

    tig_mouse_show();
}

// 0x5445F0
void mainmenu_ui_intro_create(void)
{
    mainmenu_ui_window_type = MM_WINDOW_INTRO;
    gmovie_play(1, 0, 0);
    gmovie_play(7, 0, 0);
    mainmenu_ui_num_windows++;
    mainmenu_ui_pop_window_stack();
    mainmenu_ui_window_type = MM_WINDOW_SINGLE_PLAYER;
    mainmenu_ui_open();
    dword_64C38C = true;
}

// 0x544640
void mainmenu_ui_credits_create(void)
{
    mainmenu_ui_window_type = MM_WINDOW_CREDITS;
    mainmenu_ui_num_windows++;
    mainmenu_ui_pop_window_stack();
    mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
    mainmenu_ui_custom_bg_window_type = MM_WINDOW_CREDITS;
    mainmenu_ui_custom_bg_window_type_override = true;
    mainmenu_ui_open();
    mainmenu_ui_custom_bg_window_type_override = false;
    dword_64C38C = true;
    slide_ui_start(SLIDE_UI_TYPE_CREDITS);
    if (mainmenu_ui_active && mainmenu_ui_window_type == MM_WINDOW_MAINMENU) {
        if (!mainmenu_ui_reload_custom_bg(MM_WINDOW_MAINMENU)) {
            mainmenu_ui_reapply_custom_bg();
        }
        sub_549960();
        mainmenu_ui_draw_version();
        tig_window_display();
    }

    if (mainmenu_ui_active) {
        if (main_menu_window_info[mainmenu_ui_window_type]->refresh_func != NULL) {
            main_menu_window_info[mainmenu_ui_window_type]->refresh_func(NULL);
        }
    }
}

// 0x544690
void mainmenu_ui_last_save_refresh(TigRect* rect)
{
    (void)rect;
}

// 0x5446A0
void mainmenu_ui_create_single_player(void)
{
    mainmenu_ui_window_type = MM_WINDOW_SINGLE_PLAYER;
    mainmenu_ui_create_window();
    mainmenu_ui_draw_version();
    sub_5576B0();
}

// 0x5446D0
void mainmenu_ui_pick_new_or_pregen_create(void)
{
    dword_64C454 = CHAREDIT_MODE_CREATE;
    mainmenu_ui_window_type = MM_WINDOW_PICK_NEW_OR_PREGEN;
    mainmenu_ui_create_window();
    mainmenu_ui_draw_version();
}

// 0x5446F0
void mainmenu_ui_new_char_create(void)
{
    PlayerCreateInfo player_create_info;
    MesFileEntry mes_file_entry;

    mainmenu_ui_new_char_hover_mode = MMUI_NEW_CHAR_HOVER_MODE_BACKGROUND;
    mainmenu_ui_window_type = MM_WINDOW_NEW_CHAR;

    player_create_info_init(&player_create_info);
    player_create_info.loc = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
    player_create_info.basic_prototype = 16066;
    if (!player_obj_create_player(&player_create_info)) {
        tig_debug_printf("MainMenu-UI: mainmenu_ui_create_pick_new_or_pregen: ERROR: Player Creation Failed!\n");
        exit(EXIT_FAILURE);
    }

    // CE: There's a bug in the original game — the name persists when
    // navigating back and forth to the "New Char" screen. Everything else
    // (gender/race/background, just a moment above) resets, but not the name.
    mes_file_entry.num = 500; // "Choose Name"
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
    strncpy(mainmenu_ui_textedit_buffer, mes_file_entry.str, 23);

    mainmenu_ui_create_window();
    if (!main_menu_button_create(&mainmenu_ui_new_char_name_button, mainmenu_ui_new_char_name_rect.width + 2, mainmenu_ui_new_char_name_rect.height + 2)) {
        tig_debug_printf("MainMenu-UI: mainmenu_ui_create_new_char: ERROR: Failed to create button.\n");
    }
}

// 0x5447B0
void mainmenu_ui_new_char_refresh(TigRect* rect)
{
    int64_t pc_obj;
    char* str;
    MesFileEntry mes_file_entry;

    pc_obj = player_get_local_pc_obj();
    mmUISharedCharRefreshFunc(pc_obj, rect);

    if (rect == NULL
        || (mainmenu_ui_shared_char_name_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_name_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_name_rect.x + mainmenu_ui_shared_char_name_rect.width
            && rect->y < mainmenu_ui_shared_char_name_rect.y + mainmenu_ui_shared_char_name_rect.height)) {
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_name_rect, tig_color_make(0, 0, 0)) != TIG_OK) {
            tig_debug_printf("MainMenu-UI: mmUINewCharRefreshFunc: ERROR: Window Fill Failed.\n");
        }

        str = mainmenu_ui_textedit_buffer[0] != '\0' ? mainmenu_ui_textedit_buffer : " ";

        // FIXME: Useless.
        mes_file_entry.num = 500; // "Choose Name"
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

        if (textedit_ui_is_focused()) {
            sub_544100(str, &mainmenu_ui_shared_char_name_rect, dword_64C218[1], false);
        } else {
            sub_542DF0(str, &mainmenu_ui_shared_char_name_rect, dword_64C218[1]);
        }
    }
}

// 0x5448E0
void mmUISharedCharRefreshFunc(int64_t obj, TigRect* rect)
{
    int race;
    int gender;
    int portrait;
    int background;
    MesFileEntry mes_file_entry;
    tig_font_handle_t font;
    int index;
    char str[32];

    race = stat_level_get(obj, STAT_RACE);
    gender = stat_level_get(obj, STAT_GENDER);

    if (rect == NULL
        || (mainmenu_ui_shared_char_portrait_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_portrait_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_portrait_rect.x + mainmenu_ui_shared_char_portrait_rect.width
            && rect->y < mainmenu_ui_shared_char_portrait_rect.y + mainmenu_ui_shared_char_portrait_rect.height)) {
        portrait = portrait_get(obj);
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_portrait_rect, tig_color_make(0, 0, 0)) != TIG_OK) {
            tig_debug_printf("MainMenu-UI: mmUINewCharRefreshFunc: ERROR: Window Fill #0 Failed.\n");
        }
        portrait_draw_128x128(obj,
            portrait,
            mainmenu_ui_window_handle,
            mainmenu_ui_shared_char_portrait_rect.x,
            mainmenu_ui_shared_char_portrait_rect.y);
    }

    font = dword_64C218[0];
    tig_font_push(font);
    if (rect == NULL
        || (mainmenu_ui_shared_char_gender_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_gender_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_gender_rect.x + mainmenu_ui_shared_char_gender_rect.width
            && rect->y < mainmenu_ui_shared_char_gender_rect.y + mainmenu_ui_shared_char_gender_rect.height)) {
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_gender_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            mes_file_entry.num = 740 + gender; // "Female", "Male"
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
            sub_542DF0(mes_file_entry.str, &mainmenu_ui_shared_char_gender_rect, font);
        }
    }

    if (rect == NULL
        || (mainmenu_ui_shared_char_race_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_race_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_race_rect.x + mainmenu_ui_shared_char_race_rect.width
            && rect->y < mainmenu_ui_shared_char_race_rect.y + mainmenu_ui_shared_char_race_rect.height)) {
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_race_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            mes_file_entry.num = 720 + race; // "Human", "Dwarf", "Elf", etc.
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
            sub_542DF0(mes_file_entry.str, &mainmenu_ui_shared_char_race_rect, font);
        }
    }

    if (rect == NULL
        || (mainmenu_ui_shared_char_background_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_background_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_background_rect.x + mainmenu_ui_shared_char_background_rect.width
            && rect->y < mainmenu_ui_shared_char_background_rect.y + mainmenu_ui_shared_char_background_rect.height)) {
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_background_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            background = background_text_get(obj);
            sub_542DF0(background_description_get_name(background),
                &mainmenu_ui_shared_char_background_rect,
                font);
        }
    }
    tig_font_pop();

    if (rect == NULL
        || (mainmenu_ui_shared_char_desc_view_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_desc_view_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_desc_view_rect.x + mainmenu_ui_shared_char_desc_view_rect.width
            && rect->y < mainmenu_ui_shared_char_desc_view_rect.y + mainmenu_ui_shared_char_desc_view_rect.height)) {
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_desc_view_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            if (mainmenu_ui_new_char_hover_mode == MMUI_NEW_CHAR_HOVER_MODE_GENDER) {
                font = dword_64C210[0];
                tig_font_push(font);
                mes_file_entry.num = 742 + gender;
                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                if (mes_file_entry.str[0] != '\0') {
                    sub_542DF0(mes_file_entry.str, &mainmenu_ui_shared_char_desc_body_rect, font);
                } else {
                    tig_debug_printf("MainMenu-UI: mmUISharedCharRefreshFunc: ERROR: Failed to find Racial Description!\n");
                }
                tig_font_pop();
            } else {
                background = background_text_get(obj);
                if (background > 1000 && mainmenu_ui_new_char_hover_mode == MMUI_NEW_CHAR_HOVER_MODE_BACKGROUND) {
                    font = dword_64C210[0];
                    tig_font_push(font);
                    sub_542DF0(background_description_get_body(background),
                        &mainmenu_ui_shared_char_desc_view_rect,
                        font);
                    tig_font_pop();
                } else {
                    font = dword_64C210[1];
                    tig_font_push(font);
                    mes_file_entry.num = 745; // "Race"
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    if (mes_file_entry.str[0] != '\0') {
                        sub_542DF0(mes_file_entry.str, &mainmenu_ui_shared_char_desc_title_rect, font);
                    } else {
                        tig_debug_printf("MainMenu-UI: mmUISharedCharRefreshFunc: ERROR: Failed to find Racial Description!\n");
                    }
                    tig_font_pop();

                    font = dword_64C210[0];
                    tig_font_push(font);
                    mes_file_entry.num = 770 + 2 * race + gender;
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    if (mes_file_entry.str[0] != '\0') {
                        sub_542DF0(mes_file_entry.str, &mainmenu_ui_shared_char_desc_body_rect, font);
                    } else {
                        tig_debug_printf("MainMenu-UI: mmUISharedCharRefreshFunc: ERROR: Failed to find Racial Description!\n");
                    }
                    tig_font_pop();
                }
            }
        }
    }

    if (rect == NULL
        || (mainmenu_ui_shared_char_stats_view_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_stats_view_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_stats_view_rect.x + mainmenu_ui_shared_char_stats_view_rect.width
            && rect->y < mainmenu_ui_shared_char_stats_view_rect.y + mainmenu_ui_shared_char_stats_view_rect.height)) {
        font = dword_64C210[1];
        tig_font_push(font);
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_stats_title_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            mes_file_entry.num = 739; // "Initial Stats"
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
            sub_542DF0(mes_file_entry.str,
                &mainmenu_ui_shared_char_stats_title_rect,
                font);
        }
        tig_font_pop();

        font = dword_64C210[0];
        tig_font_push(font);
        for (index = 0; index < 15; index++) {
            if (tig_window_fill(mainmenu_ui_window_handle, &(stru_5C4F50[index]), tig_color_make(0, 0, 0)) == TIG_OK) {
                snprintf(str, sizeof(str),
                    "%s: %d",
                    stat_short_name(dword_5C5130[index]),
                    stat_level_get(obj, dword_5C5130[index]));
                sub_5418A0(str, &(stru_5C4F50[index]), font, 0);
            }
        }
        tig_font_pop();
    }
}

// 0x544FF0
bool mainmenu_ui_new_char_button_released(tig_button_handle_t button_handle)
{
    int index;
    int64_t pc_obj;
    MainMenuWindowInfo* window;
    int portrait;
    int background;

    for (index = 0; index < 10; index++) {
        if (button_handle == mainmenu_ui_new_char_buttons[index].button_handle) {
            break;
        }
    }

    if (index >= 10) {
        if (button_handle == mainmenu_ui_new_char_name_button.button_handle
            && sub_549520() == NULL) {
            strcpy(byte_64C0F0, mainmenu_ui_textedit_buffer);
            mainmenu_ui_textedit_buffer[0] = '\0';
            sub_5493C0(mainmenu_ui_textedit_buffer, 23);
        }

        return true;
    }

    pc_obj = player_get_local_pc_obj();
    window = main_menu_window_info[mainmenu_ui_window_type];

    switch (index) {
    case 0:
        if (window->execute_func != NULL && !window->execute_func(0)) {
            return true;
        }

        mainmenu_ui_close(false);
        mainmenu_ui_window_type = MM_WINDOW_CHAREDIT;
        mainmenu_ui_open();
        return true;
    case 1:
        mainmenu_ui_close(true);
        return true;
    case 2:
        portrait = portrait_get(pc_obj);
        if (!portrait_find_prev(pc_obj, &portrait)) {
            portrait_find_last(pc_obj, &portrait);
        }
        obj_field_int32_set(pc_obj, OBJ_F_CRITTER_PORTRAIT, portrait);
        window->refresh_func(&mainmenu_ui_shared_char_portrait_rect);
        return true;
    case 3:
        portrait = portrait_get(pc_obj);
        if (!portrait_find_next(pc_obj, &portrait)) {
            portrait_find_first(pc_obj, &portrait);
        }
        obj_field_int32_set(pc_obj, OBJ_F_CRITTER_PORTRAIT, portrait);
        window->refresh_func(&mainmenu_ui_shared_char_portrait_rect);
        return true;
    case 4:
        if (mainmenu_ui_new_char_prev_race(pc_obj)) {
            window->refresh_func(NULL);
        }
        return true;
    case 5:
        if (mainmenu_ui_new_char_next_race(pc_obj)) {
            window->refresh_func(NULL);
        }
        return true;
    case 6:
        background = background_get(pc_obj);
        if (mainmenu_ui_new_char_prev_background(pc_obj, &background)) {
            background_clear(pc_obj);
            background_set(pc_obj, background, background_get_description(background));
            window->refresh_func(NULL);
        }
        return true;
    case 7:
        background = background_get(pc_obj);
        if (mainmenu_ui_new_char_next_background(pc_obj, &background)) {
            background_clear(pc_obj);
            background_set(pc_obj, background, background_get_description(background));
            window->refresh_func(NULL);
        }
        return true;
    case 8:
        if (!dword_5C3620 && mainmenu_ui_new_char_prev_gender(pc_obj)) {
            window->refresh_func(NULL);
        }
        return true;
    case 9:
        if (!dword_5C3620 && mainmenu_ui_new_char_next_gender(pc_obj)) {
            window->refresh_func(NULL);
        }
        return true;
    default:
        return true;
    }
}

// 0x5452C0
bool mainmenu_ui_new_char_next_background(int64_t obj, int* background_ptr)
{
    if (background_find_next(obj, background_ptr)) {
        return true;
    }

    if (background_find_first(obj, background_ptr)) {
        return true;
    }

    return false;
}

// 0x545300
bool mainmenu_ui_new_char_prev_background(int64_t obj, int* background_ptr)
{
    if (background_find_prev(obj, background_ptr)) {
        return true;
    }

    while (background_find_next(obj, background_ptr)) {
    }

    return true;
}

// 0x545350
bool mainmenu_ui_new_char_prev_gender(int64_t obj)
{
    if (stat_level_get(obj, STAT_GENDER) == GENDER_FEMALE) {
        background_clear(obj);
        return mainmenu_ui_new_char_set_gender(obj, GENDER_MALE);
    } else {
        background_clear(obj);
        return mainmenu_ui_new_char_set_gender(obj, GENDER_FEMALE);
    }
}

// 0x5453A0
bool mainmenu_ui_new_char_set_gender(int64_t obj, int gender)
{
    int race;
    int portrait;

    if (stat_level_get(obj, STAT_GENDER) == gender) {
        return false;
    }

    race = stat_level_get(obj, STAT_RACE);
    if (gender == GENDER_FEMALE
        && !stru_5C5170[race].available_for_female) {
        return false;
    }

    object_set_gender_and_race(obj, stru_5C5170[race].body_type, gender, race);
    object_set_current_aid(obj, obj_field_int32_get(obj, OBJ_F_CURRENT_AID));

    if (portrait_find_first(obj, &portrait)) {
        obj_field_int32_set(obj, OBJ_F_CRITTER_PORTRAIT, portrait);
    }

    return true;
}

// 0x545440
bool mainmenu_ui_new_char_next_gender(int64_t obj)
{
    if (stat_level_get(obj, STAT_GENDER) == GENDER_MALE) {
        background_clear(obj);
        return mainmenu_ui_new_char_set_gender(obj, GENDER_FEMALE);
    } else {
        background_clear(obj);
        return mainmenu_ui_new_char_set_gender(obj, GENDER_MALE);
    }
}

// 0x545490
bool mainmenu_ui_new_char_prev_race(int64_t obj)
{
    int race;

    race = stat_level_get(obj, STAT_RACE);
    if (race > 0) {
        if (stat_level_get(obj, STAT_GENDER) == GENDER_FEMALE) {
            do {
                race--;
            } while (race >= 0 && !stru_5C5170[race].available_for_female);

            if (race < 0) {
                return false;
            }

            if (!stru_5C5170[race].available_for_female) {
                return false;
            }
        } else {
            race--;
        }

        if (race < 0) {
            return false;
        }
    } else {
        race = 7;
        if (stat_level_get(obj, STAT_GENDER) == GENDER_FEMALE) {
            while (race >= 0 && !stru_5C5170[race].available_for_female) {
                race--;
            }

            if (race < 0) {
                return false;
            }

            if (!stru_5C5170[race].available_for_female) {
                return false;
            }
        }
    }

    background_clear(obj);
    mainmenu_ui_new_char_set_race(obj, race);

    return true;
}

// 0x545550
void mainmenu_ui_new_char_set_race(int64_t obj, int race)
{
    int gender;
    int portrait;

    gender = stat_level_get(obj, STAT_GENDER);
    if (gender == GENDER_FEMALE) {
        gender = !stru_5C5170[race].available_for_female
            ? GENDER_MALE
            : GENDER_FEMALE;
    }

    object_set_gender_and_race(obj, stru_5C5170[race].body_type, gender, race);
    object_set_current_aid(obj, obj_field_int32_get(obj, OBJ_F_CURRENT_AID));

    if (portrait_find_first(obj, &portrait)) {
        obj_field_int32_set(obj, OBJ_F_CRITTER_PORTRAIT, portrait);
    }
}

// 0x5455D0
bool mainmenu_ui_new_char_next_race(int64_t obj)
{
    int race;

    race = stat_level_get(obj, STAT_RACE);
    if (race < 7) {
        if (stat_level_get(obj, STAT_GENDER) == GENDER_FEMALE) {
            do {
                race++;
            } while (race < 7 && !stru_5C5170[race].available_for_female);

            if (race >= 8) {
                // FIXME: Unreachable.
                return false;
            }

            if (!stru_5C5170[race].available_for_female) {
                race = RACE_HUMAN;
            }
        } else {
            race++;
        }
    } else {
        race = RACE_HUMAN;
        if (stat_level_get(obj, STAT_GENDER) == GENDER_FEMALE) {
            while (!stru_5C5170[race].available_for_female) {
                race++;
            }

            if (race >= 8) {
                // FIXME: Unreachable.
                return false;
            }

            if (!stru_5C5170[race].available_for_female) {
                return false;
            }
        }
    }

    if (race < 8) {
        background_clear(obj);
        mainmenu_ui_new_char_set_race(obj, race);
        return true;
    }

    return false;
}

// 0x545690
bool mainmenu_ui_new_char_button_hover(tig_button_handle_t button_handle)
{
    (void)button_handle;

    return true;
}

// 0x5456A0
bool mainmenu_ui_new_char_button_leave(tig_button_handle_t button_handle)
{
    (void)button_handle;

    return true;
}

// 0x5456B0
void mainmenu_ui_new_char_mouse_idle(int x, int y)
{
    MainMenuUiNewCharHoverMode mode;

    y -= 43;
    if (x >= mainmenu_ui_new_char_gender_hover_rect.x
        && y >= mainmenu_ui_new_char_gender_hover_rect.y
        && x < mainmenu_ui_new_char_gender_hover_rect.x + mainmenu_ui_new_char_gender_hover_rect.width
        && y < mainmenu_ui_new_char_gender_hover_rect.y + mainmenu_ui_new_char_gender_hover_rect.height) {
        mode = MMUI_NEW_CHAR_HOVER_MODE_GENDER;
    } else if (x >= mainmenu_ui_new_char_race_hover_rect.x
        && y >= mainmenu_ui_new_char_race_hover_rect.y
        && x < mainmenu_ui_new_char_race_hover_rect.x + mainmenu_ui_new_char_race_hover_rect.width
        && y < mainmenu_ui_new_char_race_hover_rect.y + mainmenu_ui_new_char_race_hover_rect.height) {
        mode = MMUI_NEW_CHAR_HOVER_MODE_RACE;
    } else {
        mode = MMUI_NEW_CHAR_HOVER_MODE_BACKGROUND;
    }

    if (mainmenu_ui_new_char_hover_mode != mode) {
        mainmenu_ui_new_char_hover_mode = mode;
        main_menu_window_info[mainmenu_ui_window_type]->refresh_func(&mainmenu_ui_shared_char_desc_view_rect);
    }
}

// 0x545780
bool mainmenu_ui_new_char_execute(int btn)
{
    int64_t pc_obj;
    MesFileEntry mes_file_entry;

    (void)btn;

    pc_obj = player_get_local_pc_obj();

    mes_file_entry.num = 500; // "Choose Name"
    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
    if (mainmenu_ui_textedit_buffer[0] == '\0') {
        strcpy(mainmenu_ui_textedit_buffer, mes_file_entry.str);
    }

    if (strcmp(mainmenu_ui_textedit_buffer, mes_file_entry.str) == 0) {
        mes_file_entry.num = 506; // "You must choose a name."
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
        intgame_message_window_display_str(-1, mes_file_entry.str);
        return false;
    }

    obj_field_string_set(pc_obj, OBJ_F_PC_PLAYER_NAME, mainmenu_ui_textedit_buffer);
    ui_spell_maintain_refresh();
    mainmenu_ui_auto_equip_items_on_start = true;

    return true;
}

// 0x545870
void mainmenu_ui_pregen_char_create(void)
{
    mainmenu_ui_window_type = MM_WINDOW_PREGEN_CHAR;
    mainmenu_ui_pregen_char_idx = 1;
    qword_64C460 = obj_pool_perm_lookup(obj_get_id(sub_4685A0(BP_MERWIN_TUMBLEBROOK)));
    mainmenu_ui_create_window();
}

// 0x5458D0
void mainmenu_ui_pregen_char_refresh(TigRect* rect)
{
    char* name;

    mmUISharedCharRefreshFunc(qword_64C460, rect);
    if (rect == NULL
        || (mainmenu_ui_shared_char_name_rect.x < rect->x + rect->width
            && mainmenu_ui_shared_char_name_rect.y < rect->y + rect->height
            && rect->x < mainmenu_ui_shared_char_name_rect.x + mainmenu_ui_shared_char_name_rect.width
            && rect->y < mainmenu_ui_shared_char_name_rect.y + mainmenu_ui_shared_char_name_rect.height)) {
        tig_font_push(dword_64C218[1]);
        if (tig_window_fill(mainmenu_ui_window_handle, &mainmenu_ui_shared_char_name_rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            obj_field_string_get(qword_64C460, OBJ_F_PC_PLAYER_NAME, &name);
            if (name != NULL) {
                sub_542DF0(name, &mainmenu_ui_shared_char_name_rect, dword_64C218[1]);
                FREE(name);
            }
        }
        tig_font_pop();
    }
}

// 0x5459F0
bool mainmenu_ui_pregen_char_button_released(tig_button_handle_t button_handle)
{
    int index;
    int64_t pc_obj;
    MainMenuWindowInfo* window;

    for (index = 0; index < 4; index++) {
        if (button_handle == mainmenu_ui_pregen_char_buttons[index].button_handle) {
            break;
        }
    }

    if (index >= 4) {
        return true;
    }

    pc_obj = player_get_local_pc_obj();
    window = main_menu_window_info[mainmenu_ui_window_type];

    switch (index) {
    case 0:
        if (window->execute_func != NULL && !window->execute_func(0)) {
            return true;
        }
        sub_5412D0();
        return true;
    case 1:
        mainmenu_ui_close(true);
        return true;
    case 2:
        if (mainmenu_ui_pregen_char_idx > 1) {
            mainmenu_ui_pregen_char_idx--;
        } else {
            mainmenu_ui_pregen_char_idx = mainmenu_ui_pregen_char_cnt - 1;
        }
        qword_64C460 = obj_pool_perm_lookup(obj_get_id(sub_4685A0(mainmenu_ui_pregen_char_idx + BP_GENERIC_PC)));
        window->refresh_func(NULL);
        return true;
    case 3:
        if (mainmenu_ui_pregen_char_idx < mainmenu_ui_pregen_char_cnt - 1) {
            mainmenu_ui_pregen_char_idx++;
        } else {
            mainmenu_ui_pregen_char_idx = 1;
        }
        qword_64C460 = obj_pool_perm_lookup(obj_get_id(sub_4685A0(mainmenu_ui_pregen_char_idx + BP_GENERIC_PC)));
        window->refresh_func(NULL);
        return true;
    default:
        return true;
    }
}

// 0x545B60
bool mainmenu_ui_pregen_char_execute(int btn)
{
    PlayerCreateInfo player_create_info;
    MesFileEntry mes_file_entry;
    int index;
    int flag;

    (void)btn;

    mainmenu_ui_start_new_game = true;

    player_create_info_init(&player_create_info);
    player_create_info.loc = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
    player_create_info.basic_prototype = mainmenu_ui_pregen_char_idx + BP_GENERIC_PC;
    if (!player_obj_create_player(&player_create_info)) {
        tig_debug_printf("MainMenu-UI: mmUIPregenCharExecuteFunc: ERROR: Player Creation Failed!\n");
        exit(EXIT_FAILURE);
    }

    for (index = 0; index < mainmenu_ui_pregen_char_cnt - 1; index++) {
        mes_file_entry.num = 551 + index;
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
        flag = atoi(mes_file_entry.str);
        if (flag > 0) {
            script_global_flag_set(flag, mainmenu_ui_pregen_char_idx - 1 == index);
        }
    }

    item_wield_best_all(player_create_info.obj, OBJ_HANDLE_NULL);
    sub_5412D0();

    return true;
}

// CE harness (newgame): start a new game with a premade character, exactly as the
// menu's "New Game -> pregen -> Accept" path does (pregen_char_create sets up the
// pregen state + window; pregen_char_execute creates the PC, sets pregen flags,
// auto-equips, and calls sub_5412D0 to fade into the start map). No interactive
// character creation, so a benchmark can land in a real start map on ANY module or
// branch -- save-format independent. pregen_idx 1 = Merwin (first premade); clamped.
void mainmenu_ui_harness_newgame(int pregen_idx)
{
    mainmenu_ui_pregen_char_create();
    if (pregen_idx >= 1 && pregen_idx < mainmenu_ui_pregen_char_cnt) {
        mainmenu_ui_pregen_char_idx = pregen_idx;
    }
    mainmenu_ui_pregen_char_execute(0);
}

// 0x545C50
void mainmenu_ui_charedit_create(void)
{
    mainmenu_ui_window_type = MM_WINDOW_CHAREDIT;
    mainmenu_ui_create_window();
}

// 0x545C60
void mainmenu_ui_charedit_destroy(void)
{
    if (charedit_is_created()) {
        charedit_close();
    }
}

// 0x545C70
bool mainmenu_ui_charedit_button_released(tig_button_handle_t button_handle)
{
    if (button_handle == mainmenu_ui_charedit_buttons[0].button_handle) {
        sub_5480C0(2);
        return true;
    }

    if (button_handle == mainmenu_ui_charedit_buttons[1].button_handle) {
        mainmenu_ui_close(true);
        return true;
    }

    return false;
}

// 0x545DF0
void mainmenu_ui_charedit_refresh(TigRect* rect)
{
    int64_t pc_obj;

    (void)rect;

    pc_obj = player_get_local_pc_obj();
    if (!charedit_open(pc_obj, dword_64C454)) {
        mainmenu_ui_close(true);
    } else {
        // Backdrop is created earlier in the same z-class. Promote the
        // charedit base window AND each tab sub-window (skills / tech /
        // spells / scheme) so they all sit above the backdrop, with the
        // sub-windows ending up above the base for their tab content.
        charedit_promote_overlay();
    }
    dword_64C454 = CHAREDIT_MODE_3;
}

// 0x545E20
void mainmenu_ui_shop_create(void)
{
    mainmenu_ui_window_type = MM_WINDOW_SHOP;
    mainmenu_ui_create_window();
}

// 0x545E30
void mainmenu_ui_shop_destroy(void)
{
    if (inven_ui_is_created()) {
        inven_ui_destroy();
    }
}

// 0x545E40
bool mainmenu_ui_shop_button_released(tig_button_handle_t button_handle)
{
    if (button_handle == mainmenu_ui_shop_buttons[0].button_handle) {
        sub_5412D0();
        return true;
    }

    if (button_handle == mainmenu_ui_shop_buttons[1].button_handle) {
        mainmenu_ui_close(true);
        return true;
    }

    return false;
}

// 0x545E80
void mainmenu_ui_shop_refresh(TigRect* rect)
{
    int64_t pc_obj;
    LocRect loc_rect;
    ObjectList objects;
    int64_t npc_obj;
    int64_t substitute_inventory_obj;

    (void)rect;

    pc_obj = player_get_local_pc_obj();
    item_generate_inventory(pc_obj);
    if (map_by_type(MAP_TYPE_SHOPPING_MAP) == 0) {
        sub_5412D0();
        return;
    }

    loc_rect.x1 = 0;
    loc_rect.y1 = 0;
    location_limits_get(&(loc_rect.x2), &(loc_rect.y2));
    object_list_rect(&loc_rect, OBJ_TM_NPC, &objects);

    if (objects.head != NULL) {
        npc_obj = objects.head->obj;
    } else {
        npc_obj = OBJ_HANDLE_NULL;
    }
    object_list_destroy(&objects);

    mainmenu_ui_start_new_game = true;

    if (npc_obj == OBJ_HANDLE_NULL) {
        sub_5412D0();
        return;
    }

    reaction_forget(npc_obj, pc_obj);
    sub_463E20(npc_obj);

    substitute_inventory_obj = critter_substitute_inventory_get(npc_obj);
    if (substitute_inventory_obj != OBJ_HANDLE_NULL) {
        sub_463E20(substitute_inventory_obj);
    }

    if (!inven_ui_open(pc_obj, npc_obj, INVEN_UI_MODE_BARTER)) {
        sub_5412D0();
        return;
    }

    // Backdrop is created earlier in the same z-class; bring the inventory
    // window forward so its panel art isn't obscured.
    intgame_big_window_promote();
}

// 0x5461A0
bool main_menu_button_create(MainMenuButtonInfo* info, int width, int height)
{
    return main_menu_button_create_ex(info, width, height, TIG_BUTTON_MOMENTARY);
}

// 0x5461C0
bool main_menu_button_create_ex(MainMenuButtonInfo* info, int width, int height, unsigned int flags)
{
    TigButtonData button_data;
    int index;

    button_data.flags = flags;
    button_data.x = info->x;
    button_data.y = info->y;

    if (info->art_num != -1) {
        tig_art_interface_id_create(info->art_num, 0, 0, 0, &(button_data.art_id));
    } else {
        button_data.art_id = TIG_ART_ID_INVALID;
        button_data.width = width;
        button_data.height = height;
    }

    if ((info->flags & 0x1) != 0) {
        for (index = 0; index < 3; index++) {
            if (info->x >= mainmenu_ui_bottom_bar_cover_rects[index].x
                && info->x < mainmenu_ui_bottom_bar_cover_rects[index].x + mainmenu_ui_bottom_bar_cover_rects[index].width
                && info->y + 441 >= mainmenu_ui_bottom_bar_cover_rects[index].y
                && info->y + 441 < mainmenu_ui_bottom_bar_cover_rects[index].y + mainmenu_ui_bottom_bar_cover_rects[index].height) {
                break;
            }
        }

        if (index >= 3) {
            return false;
        }

        // Save / Load screens skip the bottom bar covers; buttons parented
        // to those covers have no host window. Report success without
        // creating the button so callers don't treat it as a fatal error.
        if (mainmenu_ui_bottom_bar_cover_window_handles[index] == TIG_WINDOW_HANDLE_INVALID) {
            info->button_handle = TIG_BUTTON_HANDLE_INVALID;
            return true;
        }

        button_data.window_handle = mainmenu_ui_bottom_bar_cover_window_handles[index];
        button_data.x -= mainmenu_ui_bottom_bar_cover_rects[index].x;
    } else {
        button_data.window_handle = mainmenu_ui_window_handle;
        button_data.y -= mainmenu_ui_window_rect.y;
        // CE: shell-menu buttons live on the persistent backdrop
        // (no separate panel). The backdrop's local coords are
        // offset from design space by ((ow-800)/2, (oh-600)/2)
        // because the backdrop is screen-sized × MM_BG_OVERDRAW
        // with the 800x600 design area centered inside it. Add the
        // cached offset so design-space info->x/y land at the
        // right screen position for any resolution. Sub-window
        // panels are positioned via hrp_apply so their interior
        // coords already match design space and this no-ops.
        if (mainmenu_ui_window_handle == mainmenu_ui_backdrop_handle
            && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
            button_data.x += mainmenu_ui_backdrop_offset_x;
            button_data.y += mainmenu_ui_backdrop_offset_y;
        }
    }

    if ((info->flags & 0x2) != 0) {
        button_data.flags |= TIG_BUTTON_TOGGLE;
    }

    button_data.mouse_enter_snd_id = -1;
    button_data.mouse_exit_snd_id = -1;

    if (info->art_num != -1) {
        button_data.mouse_down_snd_id = SND_INTERFACE_BUTTON_MEDIUM;
        button_data.mouse_up_snd_id = SND_INTERFACE_BUTTON_MEDIUM_RELEASE;
    } else {
        button_data.mouse_up_snd_id = -1;
        button_data.mouse_down_snd_id = SND_INTERFACE_MORPHTEXT_CLICK;
        button_data.mouse_enter_snd_id = SND_INTERFACE_MORPHTEXT_HOVER;
    }

    return tig_button_create(&button_data, &(info->button_handle)) == TIG_OK;
}

// 0x546330
void mainmenu_ui_create_window(void)
{
    mainmenu_ui_create_window_func(true);
}

// Each entry: { primary file, fallback file (or NULL) }.
// Screens that share the main menu look fall back to mainmenu_bg.bmp when
// no bespoke file is present.  Screens with two NULLs use original game art.
static MainMenuWindowType mainmenu_ui_bg_window_type_resolve(void)
{
    if (mainmenu_ui_custom_bg_window_type_override) {
        return mainmenu_ui_custom_bg_window_type;
    }

    return mainmenu_ui_window_type;
}

static bool mainmenu_ui_load_bg_vb(MainMenuWindowType type)
{
    // CE: First slot is the original ART stem (the engine's
    // background_art_num for that window, .ART → .bmp); second slot is the
    // legacy CE-only name for back-compat with existing custom packs.
    // Originals from interface.mes:
    //   329 mainmenuback.art   556 optionsmenuback.art
    //   745 saveloadbackground.art
    static const char* candidates[MM_WINDOW_COUNT][2] = {
        /* MM_WINDOW_0                    */ { NULL, NULL },
        /* MM_WINDOW_1                    */ { NULL, NULL },
        /* MM_WINDOW_MAINMENU             */ { "art\\interface\\mainmenuback.bmp", "art\\interface\\mainmenu_bg.bmp" },
        /* MM_WINDOW_MAINMENU_IN_PLAY     */ { "art\\interface\\inmenu_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_MAINMENU_IN_PLAY_LOCKED */ { "art\\interface\\inmenu_locked_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_SINGLE_PLAYER        */ { "art\\interface\\singleplayer_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_OPTIONS              */ { "art\\interface\\optionsmenuback.bmp", "art\\interface\\options_bg.bmp" },
        /* MM_WINDOW_LOAD_GAME            */ { "art\\interface\\saveloadbackground.bmp", "art\\interface\\loadgame_bg.bmp" },
        /* MM_WINDOW_SAVE_GAME            */ { "art\\interface\\saveloadbackground.bmp", "art\\interface\\savegame_bg.bmp" },
        /* MM_WINDOW_LAST_SAVE_GAME       */ { "art\\interface\\saveloadbackground.bmp", "art\\interface\\savegame_bg.bmp" },
        /* MM_WINDOW_INTRO                */ { "art\\interface\\intro_bg.bmp", NULL },
        /* MM_WINDOW_PICK_NEW_OR_PREGEN   */ { "art\\interface\\newchar_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_NEW_CHAR             */ { "art\\interface\\newchar_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_PREGEN_CHAR          */ { "art\\interface\\newchar_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_CHAREDIT             */ { "art\\interface\\charedit_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_SHOP                 */ { "art\\interface\\shop_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_CREDITS              */ { "art\\interface\\credits_bg.bmp", "art\\interface\\mainmenuback.bmp" },
        /* MM_WINDOW_26                   */ { NULL, NULL },
    };
    int i;
    TigVideoBuffer* vb;
    TigVideoBufferData vb_data;

    if (type < 0 || type >= MM_WINDOW_COUNT) {
        return false;
    }

    for (i = 0; i < 2; i++) {
        if (candidates[type][i] == NULL) {
            break;
        }
        // CE: ALLOCATE | CHROMAKEY — see gameuilib_custom_ui_blit.
        if (tig_video_buffer_load_from_bmp(candidates[type][i], &vb,
                TIG_VIDEO_BUFFER_LOAD_BMP_ALLOCATE | TIG_VIDEO_BUFFER_LOAD_BMP_CHROMAKEY) == TIG_OK) {
            if (tig_video_buffer_data(vb, &vb_data) != TIG_OK) {
                tig_video_buffer_destroy(vb);
                continue;
            }
            mainmenu_ui_free_custom_bg();
            mainmenu_ui_custom_bg_vb = vb;
            mainmenu_ui_custom_bg_width = vb_data.width;
            mainmenu_ui_custom_bg_height = vb_data.height;
            // Only suppress the menu-window overlay when a fallback BMP is
            // serving a screen whose stock art has critical UI (portrait /
            // race / stats / save-slot list / options controls / ...).
            // Other screens (in-play menu, credits, single-player splash,
            // ...) intentionally let any loaded custom bg replace their
            // decorative stock art.
            mainmenu_ui_custom_bg_is_fallback = (i > 0)
                && (type == MM_WINDOW_NEW_CHAR
                    || type == MM_WINDOW_PREGEN_CHAR
                    || type == MM_WINDOW_CHAREDIT
                    || type == MM_WINDOW_SHOP
                    || type == MM_WINDOW_OPTIONS
                    || type == MM_WINDOW_LOAD_GAME
                    || type == MM_WINDOW_SAVE_GAME
                    || type == MM_WINDOW_LAST_SAVE_GAME);
            return true;
        }
    }

    return false;
}

static void mainmenu_ui_free_custom_bg(void)
{
    if (mainmenu_ui_custom_bg_vb != NULL) {
        tig_video_buffer_destroy(mainmenu_ui_custom_bg_vb);
        mainmenu_ui_custom_bg_vb = NULL;
    }
    mainmenu_ui_custom_bg_width = 0;
    mainmenu_ui_custom_bg_height = 0;
}

static bool mainmenu_ui_reload_custom_bg(MainMenuWindowType type)
{
    if (!mainmenu_ui_load_bg_vb(type)) {
        return false;
    }

    mainmenu_ui_has_custom_bg = true;
    mainmenu_ui_reapply_custom_bg();

    return true;
}

static void mainmenu_ui_blit_custom_bg_to_window(tig_window_handle_t wnd, TigRect win_rect)
{
    int sw = hrp_iso_window_width_get();
    int sh = hrp_iso_window_height_get();
    int bw = mainmenu_ui_custom_bg_width;
    int bh = mainmenu_ui_custom_bg_height;
    int bmp_ox = (sw - bw) / 2;
    int bmp_oy = (sh - bh) / 2;
    int src_x = win_rect.x - bmp_ox;
    int src_y = win_rect.y - bmp_oy;
    int dst_x = 0;
    int dst_y = 0;
    int blit_w = win_rect.width;
    int blit_h = win_rect.height;
    TigRect src_r;
    TigRect dst_r;

    if (mainmenu_ui_custom_bg_vb == NULL) {
        return;
    }

    if (src_x < 0) { dst_x -= src_x; blit_w += src_x; src_x = 0; }
    if (src_y < 0) { dst_y -= src_y; blit_h += src_y; src_y = 0; }
    if (src_x + blit_w > bw) { blit_w = bw - src_x; }
    if (src_y + blit_h > bh) { blit_h = bh - src_y; }

    if (blit_w <= 0 || blit_h <= 0) {
        return;
    }

    src_r.x = src_x;
    src_r.y = src_y;
    src_r.width = blit_w;
    src_r.height = blit_h;
    dst_r.x = dst_x;
    dst_r.y = dst_y;
    dst_r.width = blit_w;
    dst_r.height = blit_h;
    tig_window_copy_from_vbuffer(wnd, &dst_r, mainmenu_ui_custom_bg_vb, &src_r);
}

// Blit the custom background art to a sub-rect of a window.
// win_screen_rect: the window's screen rect (used to map to BMP coordinates).
// local_rect: destination rect in window-local coordinates.
static void mainmenu_ui_blit_custom_bg_at(tig_window_handle_t wnd, TigRect win_screen_rect, TigRect local_rect)
{
    int sw = hrp_iso_window_width_get();
    int sh = hrp_iso_window_height_get();
    int bw = mainmenu_ui_custom_bg_width;
    int bh = mainmenu_ui_custom_bg_height;
    int bmp_ox = (sw - bw) / 2;
    int bmp_oy = (sh - bh) / 2;
    int sx = win_screen_rect.x + local_rect.x;
    int sy = win_screen_rect.y + local_rect.y;
    int src_x = sx - bmp_ox;
    int src_y = sy - bmp_oy;
    int dst_x = local_rect.x;
    int dst_y = local_rect.y;
    int blit_w = local_rect.width;
    int blit_h = local_rect.height;
    TigRect src_r;
    TigRect dst_r;

    if (mainmenu_ui_custom_bg_vb == NULL) {
        return;
    }

    if (src_x < 0) { dst_x -= src_x; blit_w += src_x; src_x = 0; }
    if (src_y < 0) { dst_y -= src_y; blit_h += src_y; src_y = 0; }
    if (src_x + blit_w > bw) { blit_w = bw - src_x; }
    if (src_y + blit_h > bh) { blit_h = bh - src_y; }

    if (blit_w <= 0 || blit_h <= 0) {
        return;
    }

    src_r.x = src_x;
    src_r.y = src_y;
    src_r.width = blit_w;
    src_r.height = blit_h;
    dst_r.x = dst_x;
    dst_r.y = dst_y;
    dst_r.width = blit_w;
    dst_r.height = blit_h;
    tig_window_copy_from_vbuffer(wnd, &dst_r, mainmenu_ui_custom_bg_vb, &src_r);
}

// CE: fill the OVERSIZED backdrop window with the bg. The backdrop is
// sized screen × (1/0.96) so it stays screen-covering when it recedes /
// exits to scale 0.96 (its whole extent shrinks to the screen). The
// plain centered copy only places the asset at native size centered on
// the SCREEN, so the ~2%-per-side overdraw margin is left as the window's
// black background unless the asset happens to be >=4% larger than the
// screen — and that black margin sweeps on-screen as a perimeter edge
// around the bg when the backdrop scales toward 0.96 (most visibly during
// the menu->game exit). Fix: first stretch the full bg to cover the
// entire backdrop (puts slightly-zoomed bg content in the margin instead
// of black), then overlay the native-framing centered copy so the
// screen-visible center stays pixel-exact at scale 1.0.
static void mainmenu_ui_fill_backdrop_bg(tig_window_handle_t wnd, TigRect win_rect)
{
    if (mainmenu_ui_custom_bg_vb != NULL
        && mainmenu_ui_custom_bg_width > 0
        && mainmenu_ui_custom_bg_height > 0) {
        TigRect src_r = { 0, 0, mainmenu_ui_custom_bg_width, mainmenu_ui_custom_bg_height };
        TigRect dst_r = { 0, 0, win_rect.width, win_rect.height };
        tig_window_copy_from_vbuffer(wnd, &dst_r, mainmenu_ui_custom_bg_vb, &src_r);
    }
    mainmenu_ui_blit_custom_bg_to_window(wnd, win_rect);
}

// CE: thin wrapper around the shared gamelib helper — extracts the
// panel window's video buffer and delegates the actual pixel work.
// See gamelib_apply_legacy_vignette_to_vb for the full behavior +
// resolution gating.
static void mainmenu_ui_apply_legacy_vignette(tig_window_handle_t window)
{
    TigVideoBuffer* vb;
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    if (tig_window_vbid_get(window, &vb) != TIG_OK) return;
    gamelib_apply_legacy_vignette_to_vb(vb);
}

// Restore the text area from the currently active backdrop source so shared
// morph-text entries can stay transparent over custom backgrounds.
static void mainmenu_ui_restore_text_backdrop(tig_window_handle_t window_handle, TigRect* rect)
{
    TigWindowData wd;
    TigArtAnimData art_anim_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    tig_art_id_t art_id;

    if (mainmenu_ui_has_custom_bg && !mainmenu_ui_custom_bg_is_fallback) {
        if (tig_window_data(window_handle, &wd) == TIG_OK) {
            mainmenu_ui_blit_custom_bg_at(window_handle, wd.rect, *rect);
        }
        return;
    }

    if (main_menu_window_info[mainmenu_ui_window_type]->background_art_num == -1) {
        return;
    }

    tig_art_interface_id_create(main_menu_window_info[mainmenu_ui_window_type]->background_art_num, 0, 0, 0, &art_id);
    if (tig_art_anim_data(art_id, &art_anim_data) != TIG_OK) {
        return;
    }

    src_rect.x = rect->x;
    src_rect.y = rect->y;
    src_rect.width = rect->width + 1;
    src_rect.height = rect->height + 1;

    dst_rect = src_rect;

    // CE: for hi-res Options the panel was created with src_rect.y =
    // mainmenu_ui_window_rect.y (= 41) — see the panel blit in
    // mainmenu_ui_create_window_func. So panel-local y N corresponds
    // to art y N+41. mainmenu_ui_refresh_text passes rect already
    // translated to panel-local for sub-window panels, so we need
    // to add the same offset back when sampling from the source art
    // to restore the chrome under the text. Otherwise the restored
    // pixels come from 41 rows higher in the art (= the cropped
    // header chrome) and look misaligned baked into the buttons.
    bool is_hires = (hrp_iso_window_width_get() > 800
        || hrp_iso_window_height_get() > 600);
    if (is_hires
        && mainmenu_ui_window_type == MM_WINDOW_OPTIONS
        && window_handle == mainmenu_ui_window_handle) {
        src_rect.y += mainmenu_ui_window_rect.y;
    }

    art_blit_info.flags = 0;
    art_blit_info.art_id = art_id;
    art_blit_info.src_rect = &src_rect;
    art_blit_info.dst_rect = &dst_rect;
    tig_window_blit_art(window_handle, &art_blit_info);
}

static void mainmenu_ui_reapply_custom_bg(void)
{
    TigWindowData window_data;
    int idx;

    if (!mainmenu_ui_has_custom_bg) {
        return;
    }

    if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID
        && tig_window_data(mainmenu_ui_backdrop_handle, &window_data) == TIG_OK) {
        mainmenu_ui_fill_backdrop_bg(mainmenu_ui_backdrop_handle, window_data.rect);
    }

    if (mainmenu_ui_custom_bg_is_fallback) {
        return;
    }

    // CE: re-blit the panel's bg too, but ONLY for shell menus (which
    // have an OPAQUE panel with bg painted into it — see panel
    // creation block for the policy split). Sub-window panels are
    // TIG_WINDOW_TRANSPARENT and composite through to the backdrop,
    // so painting bg redundantly inside them would just re-create
    // the panel/backdrop scale-mismatch tear during animations.
    if (mainmenu_ui_is_shell_menu(mainmenu_ui_window_type)
        && mainmenu_ui_window_handle != TIG_WINDOW_HANDLE_INVALID
        && tig_window_data(mainmenu_ui_window_handle, &window_data) == TIG_OK) {
        mainmenu_ui_blit_custom_bg_to_window(mainmenu_ui_window_handle, window_data.rect);
    }

    for (idx = 0; idx < SDL_arraysize(mainmenu_ui_bottom_bar_cover_window_handles); idx++) {
        if (mainmenu_ui_bottom_bar_cover_window_handles[idx] != TIG_WINDOW_HANDLE_INVALID
            && tig_window_data(mainmenu_ui_bottom_bar_cover_window_handles[idx], &window_data) == TIG_OK) {
            mainmenu_ui_blit_custom_bg_to_window(mainmenu_ui_bottom_bar_cover_window_handles[idx], window_data.rect);
        }
    }

    if (mainmenu_ui_top_bar_cover_window_handle != TIG_WINDOW_HANDLE_INVALID
        && tig_window_data(mainmenu_ui_top_bar_cover_window_handle, &window_data) == TIG_OK) {
        mainmenu_ui_blit_custom_bg_to_window(mainmenu_ui_top_bar_cover_window_handle, window_data.rect);
    }
}

// CE: true when the hi-res patch's border art archive
// (ArcanumZHighResBorders.dat) is present. Only then do the panel arts
// (Schematic_Base etc.) carry the ornate HD borders we steal from.
// Cached — the archive can't appear/disappear at runtime.
static bool mainmenu_ui_hires_borders_present(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = tig_file_exists("ArcanumZHighResBorders.dat", NULL) ? 1 : 0;
    }
    return cached != 0;
}

// CE: the HD OptionsBase art is a borderless "full" panel, so the cropped
// hi-res Options window has no frame. Synthesize one by stealing the
// outer 6px ring of the Schematic_Base panel (art 365) — an 800x400
// bordered panel that matches our cropped Options size 1:1 — and blitting
// just its four edge strips around the Options window. Interior untouched
// (the Options art shows through). Only meaningful when the hi-res patch
// art is present (gated by the caller).
static void mainmenu_ui_draw_stolen_border(tig_window_handle_t window_handle)
{
    // Schematic_Base is drawn into an 800x400 window (schematic_ui_window_rect),
    // so its visible bordered region — and our cropped Options panel — are
    // both 800x400; the ring maps 1:1.
    enum { PW = 800, PH = 400, B = 6 };
    tig_art_id_t art_id;
    TigArtBlitInfo bi;
    TigRect s;
    TigRect d;

    if (tig_art_interface_id_create(365, 0, 0, 0, &art_id) != TIG_OK) {
        return;
    }
    bi.flags = 0;
    bi.art_id = art_id;
    bi.src_rect = &s;
    bi.dst_rect = &d;

    // Top edge (full width, includes the two top corners).
    s.x = 0; s.y = 0; s.width = PW; s.height = B; d = s;
    tig_window_blit_art(window_handle, &bi);
    // Bottom edge (full width, includes the two bottom corners).
    s.x = 0; s.y = PH - B; s.width = PW; s.height = B; d = s;
    tig_window_blit_art(window_handle, &bi);
    // Left edge (between the corners).
    s.x = 0; s.y = B; s.width = B; s.height = PH - 2 * B; d = s;
    tig_window_blit_art(window_handle, &bi);
    // Right edge (between the corners).
    s.x = PW - B; s.y = B; s.width = B; s.height = PH - 2 * B; d = s;
    tig_window_blit_art(window_handle, &bi);
}

// 0x546340
void mainmenu_ui_create_window_func(bool should_display)
{
    MainMenuWindowInfo* window;
    MainMenuButtonInfo* button;
    MesFileEntry mes_file_entry;
    TigArtBlitInfo art_blit_info;
    TigArtFrameData art_frame_data;
    TigArtAnimData art_anim_data;
    TigFont font_desc;
    TigRect src_rect;
    TigRect dst_rect;
    TigRect text_rect;
    TigWindowData window_data;
    tig_art_id_t art_id;
    tig_font_handle_t font;
    tig_window_handle_t window_handle;
    mainmenu_ui_has_custom_bg = false;
    mainmenu_ui_custom_bg_is_fallback = false;
    bool v1 = false;
    int idx;
    int rc;
    // CE: true if THIS call to create_window_func created the
    // persistent backdrop fresh (start of a mainmenu session). Used
    // below to trigger the bg entrance animation. Subsequent calls
    // in the same session reuse the existing backdrop and leave
    // this false.
    bool created_backdrop_now = false;

    if (dword_64C388) {
        should_display = false;
    }

    if (mainmenu_ui_active) {
        return;
    }

    // Hi-res mode adds a backdrop + top/bottom bar covers around the 800x600
    // menu panel.  At native 800x600 there's no extra scaffolding to skip —
    // the original chrome is exactly the menu — so all the recent "show over
    // game world" / chrome-skipping logic only applies in hi-res mode. At
    // 800x600, behavior stays vanilla.
    bool is_hires = (hrp_iso_window_width_get() > 800
        || hrp_iso_window_height_get() > 600);

    // "In-game" uses stru_5C36B0[type][0] — the menu flavor's "exit to
    // game" flag — rather than intgame_iso_interface_is_created().
    // The latter never gets cleared (iso_interface_destroy doesn't reset
    // the flag), so it can read `true` from main-menu Load Game after a
    // game has previously run.
    //
    // "Shortcut path" means the user invoked the menu via an in-game
    // keyboard shortcut (Cmd+Shift+S, Cmd+O, plain O) — which routes
    // through mainmenu_ui_start_at_window() and sets type=MM_TYPE_OPTIONS.
    // The pause-menu chain goes through mainmenu_ui_start(MM_TYPE_IN_PLAY)
    // instead and keeps type=MM_TYPE_IN_PLAY across window transitions.
    bool is_in_game = stru_5C36B0[mainmenu_ui_type][0];
    bool shortcut_path = is_in_game && mainmenu_ui_type == MM_TYPE_OPTIONS;
    bool save_load_screen = (mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME
        || mainmenu_ui_window_type == MM_WINDOW_LOAD_GAME
        || mainmenu_ui_window_type == MM_WINDOW_LAST_SAVE_GAME);

    // Skip the hi-res backdrop ONLY for in-game shortcuts:
    //   - Cmd+Shift+S / Cmd+O / plain O → render over the live game.
    //
    // Pause-menu → Save/Load and main-menu → Load both KEEP the backdrop
    // (with mainmenu_bg art, per the override in the backdrop block) —
    // the user wants them to paint over mainmenu_bg, not the game world
    // and not the chrome-painted loadgame_bg / savegame_bg.
    bool skip_hires_scaffold = is_hires
        && shortcut_path
        && (save_load_screen
            || mainmenu_ui_window_type == MM_WINDOW_OPTIONS);

    // CE: shortcut paths (Cmd+O / Cmd+S / Cmd+L / plain O while in
    // game) want NO backdrop — the menu draws over the live game
    // world. But if a previous mainmenu session is still mid-fade
    // when the shortcut fires (user ESC'd out of pause and
    // immediately pressed O), the backdrop window is still alive
    // (mainmenu_ui_bg_exit_pending == true) and the bg animation
    // state machine further down would retarget it back to the
    // entrance state — leaving the pause-menu bg painted behind
    // the Options panel instead of the live game world.
    //
    // Force-destroy the backdrop immediately on shortcut entry to
    // prevent that retargeting from happening. Cancels the in-
    // flight exit tween (mainmenu_ui_destroy_persistent_backdrop
    // doesn't go through the spring) and frees the bg VB.
    if (skip_hires_scaffold
        && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
        mainmenu_ui_bg_exit_pending = false;
        mainmenu_ui_destroy_persistent_backdrop();
    }

    // Skip the cosmetic top/bottom bar covers around Save / Load / Last-
    // Save AND Options at hi-res. There's never a HUD info-bar use case
    // for those screens, so the cover band is always pure cosmetic
    // chrome that the user wants gone. skip_bar_covers also forces the
    // backdrop bg to mainmenu_bg.bmp (instead of the per-screen
    // *_bg.bmp), which would otherwise re-introduce the chrome bar at
    // the bottom of the screen for Options (the options_bg.bmp art has
    // the bottom info-bar chrome baked in, visible where the cropped
    // Options panel doesn't cover).
    // At 800x600 those covers ARE the vanilla menu chrome — keep them.
    bool skip_bar_covers = is_hires
        && (save_load_screen || mainmenu_ui_window_type == MM_WINDOW_OPTIONS);

    // CE: chargen / shop screens keep the bottom-bar-cover wings (those
    // frame the HUD info-bar band the chargen UI uses) but DROP the
    // decorative top header in hi-res. The header is purely cosmetic
    // chrome that the user finds redundant on a wide modern screen,
    // and at 800x600 the original game art already includes it as part
    // of the panel.
    bool chargen_screen =
        mainmenu_ui_window_type == MM_WINDOW_NEW_CHAR
        || mainmenu_ui_window_type == MM_WINDOW_PREGEN_CHAR
        || mainmenu_ui_window_type == MM_WINDOW_CHAREDIT
        || mainmenu_ui_window_type == MM_WINDOW_SHOP;
    bool skip_top_bar_cover = skip_bar_covers
        || (is_hires && chargen_screen);

    // Always create the persistent backdrop (when not on a shortcut
    // path that wants to render over the live game world). The
    // backdrop serves two purposes:
    //   1) Custom-UI: holds the bg art (mainmenu_bg.bmp etc) —
    //      visible everywhere the panel isn't.
    //   2) Legacy / vanilla / no-custom-bg: black filler behind
    //      the legacy panel. Needed even at 800x600 because the
    //      panel entrance/exit animation scales the panel down
    //      momentarily, and without a backdrop the area exposed
    //      around the scaled-down panel would reveal the pregame
    //      world (or whatever was behind).
    //
    // The size gate (>800 || >600) was removed — the legacy panel
    // animations need fill behind them at any resolution. The
    // overdraw cost is small (a screen-sized window with black
    // fill) and the visual benefit is meaningful.
    if (!skip_hires_scaffold) {
        TigWindowData backdrop_data;
        MainMenuWindowType backdrop_bg_type;
        // CE: oversize the backdrop slightly so that when it recedes
        // (scale 0.96) the screen is still fully covered — no black
        // edges exposed. Oversize factor = 1.0 / RECEDE_SCALE; the
        // frame extends off-screen on each side by half the surplus.
        // At rest (scale 1.0) the user sees the SCREEN-sized center
        // of the backdrop VB; at receded (0.96) the user sees the
        // entire oversized VB shrunk to fit the screen. The bg art
        // (mainmenu_bg.bmp is 1920x1080) is sampled wider/taller
        // than the screen for the overdraw region to land on real
        // pixels — relies on the asset being larger than the screen.
        const float MM_BG_OVERDRAW = 1.0f / 0.96f;
        int sw = hrp_iso_window_width_get();
        int sh = hrp_iso_window_height_get();
        int ow = (int)((float)sw * MM_BG_OVERDRAW + 0.5f);
        int oh = (int)((float)sh * MM_BG_OVERDRAW + 0.5f);
        // Not ALWAYS_ON_TOP: the per-screen mainmenu_ui_window is itself
        // ALWAYS_ON_TOP and sits above the backdrop. For CHAREDIT (no main
        // window — uses intgame_big_window), we move the big window to the
        // top of its z-class so it lands above this backdrop.
        backdrop_data.flags = TIG_WINDOW_MESSAGE_FILTER;
        backdrop_data.rect.x = -(ow - sw) / 2;
        backdrop_data.rect.y = -(oh - sh) / 2;
        backdrop_data.rect.width = ow;
        backdrop_data.rect.height = oh;
        backdrop_data.background_color = tig_color_make(0, 0, 0);
        backdrop_data.color_key = tig_color_make(0, 0, 0);
        backdrop_data.message_filter = mainmenu_ui_message_filter;
        // Shell menus (mainmenu / pause / single-player / pick-new-or-
        // pregen) and Save/Load all use the plain mainmenu_bg.bmp as
        // their backdrop. Their per-screen *_bg.bmp arts have legacy
        // chrome painted in that we no longer want to show (the panel
        // is gone for shell menus; for Save/Load the chrome was the
        // info-bar at the bottom). Other sub-windows (Options /
        // NewChar / Charedit / Credits / ...) use their own bg art.
        backdrop_bg_type = (skip_bar_covers
                || mainmenu_ui_is_shell_menu(mainmenu_ui_window_type))
            ? MM_WINDOW_MAINMENU
            : mainmenu_ui_bg_window_type_resolve();

        // CE: persistent backdrop — if a previous create in this
        // mainmenu session already created the backdrop window, just
        // reuse it. Re-load the bg VB (different sub-windows can want
        // different bg art) and re-blit onto the existing window. The
        // window's transform state is preserved (already at receded
        // 0.96 if a sub-window was previously shown), avoiding the
        // bg-pulse-per-window-change visual.
        if (mainmenu_ui_backdrop_handle == TIG_WINDOW_HANDLE_INVALID) {
            if (tig_window_create(&backdrop_data, &mainmenu_ui_backdrop_handle) == TIG_OK) {
                created_backdrop_now = true;
                // Cache the design-space → backdrop-local offsets so
                // shell-menu buttons + text land centered on screen
                // regardless of resolution.
                mainmenu_ui_backdrop_offset_x = (ow - 800) / 2;
                mainmenu_ui_backdrop_offset_y = (oh - 600) / 2;
            }
        }
        if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
            if (mainmenu_ui_load_bg_vb(backdrop_bg_type)) {
                mainmenu_ui_has_custom_bg = true;
                // is_fallback gates two things:
                //   1) the per-window panel overlay later down the
                //      function — blits the bg through the panel's
                //      chromakey when !is_fallback. For Save/Load/
                //      Options this would paint mainmenu_bg through
                //      the panel chrome, which we don't want.
                //   2) mainmenu_ui_restore_text_backdrop — uses the
                //      custom bg as the restore source when
                //      !is_fallback, otherwise falls through to
                //      blitting background_art_num (the panel chrome
                //      art) over the text area.
                //
                // For shell menus (backdrop-as-host: buttons + text
                // live directly on the backdrop), restore MUST use
                // the custom bg — falling through to the chrome art
                // would paint decorative panel pixels over the
                // backdrop's bg and leave white-ish halos where
                // text rolls over. Leave is_fallback false for them.
                //
                // For Save/Load/Options the chrome restore path is
                // correct (their text lives on the panel, not the
                // backdrop) so is_fallback stays true.
                if (skip_bar_covers
                    && !mainmenu_ui_is_shell_menu(mainmenu_ui_window_type)) {
                    mainmenu_ui_custom_bg_is_fallback = true;
                }
                mainmenu_ui_fill_backdrop_bg(mainmenu_ui_backdrop_handle, backdrop_data.rect);
            }
        }
        // The backdrop is newer in MIDDLE z-class than the iso-interface
        // strips, so it would otherwise occlude them. Promote the strips
        // above the backdrop so the menu art's chromakey knockouts reveal
        // the strip content (rotwin / info bar / counters) underneath the
        // way upstream's z-compositing always did.
        //
        // CE: only character-creation screens (new-char / pregen /
        // charedit) legitimately use the bottom HUD strip as part
        // of their layout. Every other menu screen hides it via
        // intgame_iso_strips_hide_full() further down — skip the
        // promote here on those paths so the strip doesn't flash
        // visible between promote and hide.
        bool screen_uses_hud_strip =
            mainmenu_ui_window_type == MM_WINDOW_NEW_CHAR
            || mainmenu_ui_window_type == MM_WINDOW_PREGEN_CHAR
            || mainmenu_ui_window_type == MM_WINDOW_CHAREDIT;
        if (screen_uses_hud_strip) {
            intgame_iso_strips_promote();
        }
    }

    window = main_menu_window_info[mainmenu_ui_window_type];
    if (window->background_art_num != -1) {
        tig_art_interface_id_create(window->background_art_num, 0, 0, 0, &art_id);
        if (tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
            if (art_frame_data.height == 600) {
                mainmenu_ui_window_rect = mainmenu_ui_window_fullscreen_rect;
            } else {
                mainmenu_ui_window_rect = mainmenu_ui_window_partial_rect;
                v1 = true;
            }
        }

        // The Options panel is a single 800x600 art whose bottom 157px is
        // decorative filler (knockout where the rotwin would normally show
        // through). Nothing in Options drives content into that band, so
        // crop it off — the menu becomes 800x443 and the surrounding area
        // shows the backdrop / world through cleanly.
        //
        // Save / Load are *not* single-art panels — they're composited
        // from a top bar (art 336), a partial 800x400 main panel, and the
        // 800x159 bottom bar covers (art 335). The bottom covers are the
        // decorative rotwin band there, and they're skipped separately
        // below; we must NOT crop the main panel itself or its slot list
        // gets clipped.
        //
        // Hi-res only — at native 800x600 the panel IS the screen and the
        // bottom 157px is the legitimate (vanilla) info-bar area.
        //
        // CE: also crop the TOP 41px decorative header off Options at
        // hi-res — same rationale as the bottom: no content lives in
        // those rows. After both crops the Options panel is exactly
        // 800x400 (600 - 159 - 41), matching the in-game 800x400 panels
        // (and Schematic_Base) so the stolen border ring maps 1:1.
        // Button positions (read in design space and translated via
        // mainmenu_ui_window_rect.y) ride along correctly. src_rect.y
        // is set from rect.y in the Options blit branch below so the
        // panel's VB actually shows art y=41..441.
        if (is_hires
            && mainmenu_ui_window_type == MM_WINDOW_OPTIONS) {
            if (mainmenu_ui_window_rect.height > 159) {
                mainmenu_ui_window_rect.height -= 159;
            }
            if (mainmenu_ui_window_rect.height > 41) {
                mainmenu_ui_window_rect.y += 41;
                mainmenu_ui_window_rect.height -= 41;
            }
        }

        if (tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
            // CE: host-window strategy depends on the menu type.
            //
            // - SHELL MENUS (mainmenu / pause / pause-locked / single-
            //   player / pick-new-or-pregen):
            //     NO separate panel window. The persistent backdrop
            //     hosts the shell's buttons + text directly —
            //     mainmenu_ui_window_handle is aliased to
            //     mainmenu_ui_backdrop_handle so all the downstream
            //     button/text positioning code keeps working. The
            //     centralized offset (mainmenu_ui_backdrop_offset_*)
            //     applied inside mainmenu_ui_refresh_text and
            //     main_menu_button_create_ex translates the 800x600
            //     design coords into the backdrop's local coords so
            //     everything ends up centered on screen at any
            //     resolution.
            //
            //     This is what's required to play nicely with the
            //     video-playback bg: video frames overwrite the
            //     backdrop's VB each frame, then video-playback's
            //     redraw_foreground re-blits buttons + re-renders
            //     text from the original button/text data — both go
            //     through the same offset-aware helpers, so rollover
            //     and idle states stay aligned regardless of
            //     animation phase or frame number.
            //
            // - SUB-WINDOWS (Options / Load / Save / NewChar /
            //   Charedit / Credits / ...):
            //     TRANSPARENT panel — chrome art painted, no bg paint
            //     overlay. The chromakey'd regions composite through
            //     to the persistent backdrop (which carries the bg
            //     art or live video). Sub-window panels animate
            //     scale + alpha for entrance/exit independently of
            //     the backdrop.
            bool is_shell = mainmenu_ui_is_shell_menu(mainmenu_ui_window_type);
            // CE: only use the backdrop-as-host shortcut when ALL of:
            //   - this is a shell menu (mainmenu / pause / single-
            //     player / pick-new-or-pregen)
            //   - the persistent backdrop exists (hi-res mode)
            //   - a custom bg art was successfully loaded
            //   - the bg isn't a generic mainmenu_bg fallback for
            //     a screen whose normal bg art has chrome baked in
            //
            // Otherwise fall back to legacy OPAQUE panel rendering:
            //   - vanilla 800x600: no backdrop, panel chrome is the
            //     entire visual.
            //   - hi-res no-custom-bg: backdrop exists but is empty;
            //     panel chrome provides the menu visual, backdrop
            //     shows as black around it.
            //   - sub-windows: their per-screen chrome panel is the
            //     visual anchor; custom bg is painted into the panel
            //     overlay so its chromakey'd regions show the bg.
            bool use_backdrop_host = is_shell
                && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID
                && mainmenu_ui_has_custom_bg
                && !mainmenu_ui_custom_bg_is_fallback;

            if (use_backdrop_host) {
                // Shell: re-use the backdrop as the host window.
                // mainmenu_ui_window_rect stays as the 800x600 design
                // rect — downstream code reads its .y for partial-
                // rect button-y adjustment (which is 0 for shell
                // menus so no-op).
                mainmenu_ui_window_handle = mainmenu_ui_backdrop_handle;

                // CE: when transitioning sub→backdrop-hosted-shell
                // (e.g. ESC out of Options to mainmenu), the still-
                // animating sub-window panel is ALWAYS_ON_TOP and
                // the backdrop (now the shell's host) is MIDDLE z-
                // class. Without intervention the dying sub panel
                // would sit on top of the new shell for the
                // ~260ms hide animation, blocking clicks to the
                // shell's buttons — the user reported this as
                // mis-clicking risk on rapid dismissals. Flush
                // the pending close synchronously so the sub
                // panel is destroyed immediately and the
                // backdrop-hosted shell is clickable from frame 1.
                // The visual cost is losing the panel's fade-out
                // animation in this specific transition, but the
                // backdrop's own scale-up to 1.0 still provides
                // visual continuity of "entering the shell".
                //
                // Legacy / no-custom-bg path is untouched: the new
                // shell panel is also ALWAYS_ON_TOP (created
                // newer), so z-order puts it above the dying sub
                // panel and clicks already work — no need to
                // sacrifice the hide animation there.
                if (mainmenu_ui_pending_close.in_flight) {
                    mainmenu_ui_finalize_close(&mainmenu_ui_pending_close);
                }
            } else {
                // Legacy / separate-panel path. Almost always OPAQUE
                // (no TIG_WINDOW_TRANSPARENT) — matches the original
                // Arcanum render path. Chrome art is blitted in; if
                // a custom bg is present it's also painted into the
                // panel so the panel's background matches the
                // backdrop's. No chromakey composite — every pixel
                // of the panel is visible.
                //
                // EXCEPTION: Options panel at vanilla 800x600 (and
                // any case where the panel isn't cropped to omit the
                // bottom rotwin band). The user wants a rotwin-sized
                // hole knocked out at the bottom-center so the iso
                // HUD's rotwin shows through (in-game shortcut Options
                // path). For these cases, set TIG_WINDOW_TRANSPARENT
                // and fill the rotwin rect with the color-key after
                // blitting chrome. Hi-res Options is already cropped
                // (height reduced by 157px) so it doesn't cover the
                // rotwin area — no KO needed there.
                bool ko_rotwin =
                    (mainmenu_ui_window_type == MM_WINDOW_OPTIONS
                     && mainmenu_ui_window_rect.height > 488);

                window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP
                    | TIG_WINDOW_MESSAGE_FILTER;
                if (ko_rotwin) {
                    window_data.flags |= TIG_WINDOW_TRANSPARENT;
                }
                window_data.rect = mainmenu_ui_window_rect;
                window_data.background_color = art_anim_data.color_key;
                window_data.color_key = art_anim_data.color_key;
                window_data.message_filter = mainmenu_ui_message_filter;
                hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

                src_rect.x = mainmenu_ui_window_rect.x;
                // CE: hi-res Options crops the top 41 header rows
                // off the source art (along with the bottom 157). The
                // src_rect.y offset routes the blit past those rows.
                // Save/Load already shifts mainmenu_ui_window_rect.y
                // to 41 in its partial-rect definition but its art is
                // laid out so y=0..400 IS the panel body — for those
                // src.y stays 0. The is_hires + Options gate is the
                // single special case.
                src_rect.y = (is_hires
                        && mainmenu_ui_window_type == MM_WINDOW_OPTIONS)
                    ? mainmenu_ui_window_rect.y
                    : 0;
                src_rect.width = mainmenu_ui_window_rect.width;
                src_rect.height = mainmenu_ui_window_rect.height;

                dst_rect.x = 0;
                dst_rect.y = 0;
                dst_rect.width = mainmenu_ui_window_rect.width;
                dst_rect.height = mainmenu_ui_window_rect.height;

                art_blit_info.flags = 0;
                art_blit_info.art_id = art_id;
                art_blit_info.src_rect = &src_rect;
                art_blit_info.dst_rect = &dst_rect;

                if (tig_window_create(&window_data, &mainmenu_ui_window_handle) != TIG_OK) {
                    tig_debug_printf("mainmenu_ui_create_window_func: ERROR: tig_art_anim_data failed!\n");
                    exit(EXIT_SUCCESS); // FIXME: Should be `EXIT_FAILURE`.
                }

                tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info);

                // Paint the custom bg overlay into the panel when
                // available (and not a fallback overlay-suppress case
                // like Save/Load where the mainmenu_bg fallback is
                // backdrop-only). Matches the original CE custom-bg
                // pattern: the panel's bg slice and the backdrop's
                // bg slice come from the same source, so the panel's
                // chromakey'd regions composite seamlessly into the
                // surrounding backdrop bg without visible seams.
                bool custom_bg_painted = mainmenu_ui_has_custom_bg
                    && !mainmenu_ui_custom_bg_is_fallback;
                if (custom_bg_painted) {
                    mainmenu_ui_blit_custom_bg_to_window(
                        mainmenu_ui_window_handle, window_data.rect);
                }

                // CE: give the cropped hi-res Options panel a frame. Its
                // own HD OptionsBase art is borderless, so steal the outer
                // ring of Schematic_Base and blit it around the edges.
                // Only when: hi-res, this is Options, the hi-res patch art
                // is present, and custom UI didn't already paint its own
                // frame (options_bg.bmp / mainmenu_bg overrides this).
                if (is_hires
                    && mainmenu_ui_window_type == MM_WINDOW_OPTIONS
                    && !custom_bg_painted
                    && mainmenu_ui_hires_borders_present()) {
                    mainmenu_ui_draw_stolen_border(mainmenu_ui_window_handle);
                }

                // CE: opt-in elliptical-vignette fade on legacy
                // chrome. Only fires when:
                //   - no custom-bg art is loaded (custom UI is
                //     designed for the full screen and doesn't
                //     need a vignette)
                //   - the user enabled LEGACY_MENU_VIGNETTE_KEY
                //   - the screen is a "non-functional" art panel:
                //     mainmenu / pause / single-player landing,
                //     plus the loading splash transitions
                //     (WINDOW_0 / WINDOW_1) and the intro /
                //     credits screens. Functional dialogs (Options,
                //     Save/Load, NewChar / PickChar, Charedit)
                //     are excluded — their chrome readability
                //     matters.
                //
                // One-shot post-process — happens once per panel
                // creation, no per-frame cost.
                bool vignette_eligible =
                    mainmenu_ui_window_type == MM_WINDOW_MAINMENU
                    || mainmenu_ui_window_type == MM_WINDOW_MAINMENU_IN_PLAY
                    || mainmenu_ui_window_type == MM_WINDOW_MAINMENU_IN_PLAY_LOCKED
                    || mainmenu_ui_window_type == MM_WINDOW_SINGLE_PLAYER
                    || mainmenu_ui_window_type == MM_WINDOW_PICK_NEW_OR_PREGEN
                    || mainmenu_ui_window_type == MM_WINDOW_0
                    || mainmenu_ui_window_type == MM_WINDOW_1
                    || mainmenu_ui_window_type == MM_WINDOW_INTRO
                    || mainmenu_ui_window_type == MM_WINDOW_CREDITS;
                if (vignette_eligible
                    && !mainmenu_ui_has_custom_bg
                    && settings_get_value(&settings, LEGACY_MENU_VIGNETTE_KEY)) {
                    mainmenu_ui_apply_legacy_vignette(mainmenu_ui_window_handle);
                }

                // KO the rotwin rect after chrome + bg are painted,
                // so the fill is the LAST thing in the panel's VB at
                // that area. Rect matches the iso HUD's rotwin
                // (between the bottom_bar_cover_rects[0/1/2] center
                // band): 410x112 at (195, 488) in design space.
                if (ko_rotwin) {
                    TigRect rotwin_ko = { 195, 488, 410, 112 };
                    tig_window_fill(mainmenu_ui_window_handle,
                        &rotwin_ko,
                        art_anim_data.color_key);
                }
            }
        }
    } else {
        v1 = true;
    }

    if (skip_hires_scaffold) {
        v1 = false;
    }

    // skip_bar_covers is declared near the top of the function so the
    // backdrop block can consult it. Skip BOTH the decorative top bar
    // (header) and the bottom bar covers (where the rotwin / info bar
    // would normally appear) for Save / Load / Last-Save — those screens
    // don't use the info bar and the surrounding bands are purely
    // cosmetic chrome that conflicts with the user's preferred "panel
    // over game world / mainmenu_bg" look. Hi-res only — at 800x600 those
    // bar covers ARE the vanilla menu chrome.

    // Manage the iso-interface HUD strips. intgame_hide() (run when the
    // menu opens via the pause-menu chain) leaves the bottom strip
    // visible and moved up so it acts as the menu's rotwin / info-bar
    // band — the live game's bottom HUD essentially shows through the
    // menu's chrome gap.
    //
    // - Save/Load (any path) and shortcut Options: fully hide both
    //   strips. We don't want the HUD band visible over the panel
    //   regardless of whether the backdrop is mainmenu_bg or absent.
    //
    // - Other in-game chrome menus (pause menu itself, etc.): keep the
    //   band visible (it's the menu's info-bar slot).
    //
    // The iso (game-world) window is force-shown only when we're
    // skipping the backdrop (shortcut path), since the pause-menu
    // chain's intgame_hide() hides the iso window — and we want the
    // shortcut path to actually reveal the game world behind. For
    // pause-path Save/Load we *don't* re-show it: the mainmenu_bg
    // backdrop is what should appear, and the iso world stays masked.
    //
    // Hi-res only — at 800x600 the strip geometry coincides with the
    // menu's bar covers, so vanilla intgame_hide() already produces the
    // right composite.
    //
    // The chrome-less branch (Save/Load any path, shortcut Options) is
    // NOT gated on is_in_game: if a game ran earlier in this launch and
    // was exited to main menu, the iso strip windows persist and would
    // bleed the HUD band through a main-menu Load Game backdrop. Hide
    // them defensively here. intgame_iso_strips_hide_full() short-
    // circuits when no iso interface ever existed, so the fresh-boot
    // main-menu case stays a no-op.
    // CE: HUD strip management — runs for both hi-res and vanilla.
    //
    // Strips visible (used as bottom info-bar / rotwin underlay):
    //   - Character-creation screens always (newchar / pregen
    //     / charedit) — their panel layout uses the strip as
    //     part of the chrome.
    //   - In VANILLA 800x600 mode: Options / Save / Load /
    //     Last-Save also keep strips visible — their full-height
    //     panel uses the rotwin KO (TRANSPARENT + color_key fill)
    //     to expose the HUD area as underlay. Hi-res versions
    //     crop the panel and fill the exposed band with the
    //     backdrop instead — strips hidden there.
    //
    // Strips hidden (panel covers screen, no underlay needed):
    //   - Shell menus (mainmenu / pause / pause-locked / single
    //     player / pick-new-or-pregen) at any resolution.
    //   - Hi-res Options / Save / Load (cropped panel).
    //   - Intro / Credits / etc.
    //
    // PREVIOUSLY this block was gated on `is_hires` only — that
    // left vanilla 800x600 shell menus showing the bottom HUD
    // bar through the panel during entrance/exit scale animation
    // (panel < 100% scale = smaller than its design rect, area
    // around exposes the still-visible HUD bar). intgame_hide()'s
    // re-show of the bottom strip was right for hi-res chargen
    // screens but wrong for vanilla shell menus. Run the same
    // hide/show logic for vanilla too.
    bool screen_uses_hud_strip =
        mainmenu_ui_window_type == MM_WINDOW_NEW_CHAR
        || mainmenu_ui_window_type == MM_WINDOW_PREGEN_CHAR
        || mainmenu_ui_window_type == MM_WINDOW_CHAREDIT
        || mainmenu_ui_window_type == MM_WINDOW_SHOP;
    if (!is_hires
        && (mainmenu_ui_window_type == MM_WINDOW_OPTIONS
            || mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME
            || mainmenu_ui_window_type == MM_WINDOW_LOAD_GAME
            || mainmenu_ui_window_type == MM_WINDOW_LAST_SAVE_GAME)) {
        screen_uses_hud_strip = true;
    }
    if (!screen_uses_hud_strip) {
        intgame_iso_strips_hide_full();
        // CE: re-show the iso world window behind the bg
        // backdrop on in-game menu paths. mainmenu_ui_start
        // calls intgame_hide() up-front which hides the iso
        // window — without this re-show, the bg entrance
        // animation would fade in over BLACK instead of the
        // live game world. We re-show whenever there's a
        // chance the world matters behind the bg:
        //
        //   - skip_hires_scaffold: in-game shortcut paths
        //     (Cmd+O Options / Cmd+S Save / Cmd+L Load) that
        //     never had a backdrop to begin with — original
        //     behavior; not new.
        //   - is_in_game: any menu opened with the in-game
        //     flag set (pause menu = MM_TYPE_IN_PLAY,
        //     pause-locked, in-game sub-windows). Bg fades
        //     in OVER the world rather than over black; the
        //     exit fade reveals the world progressively too.
        if (skip_hires_scaffold || is_in_game) {
            intgame_iso_world_show();
        }
    } else {
        intgame_iso_strips_show_as_band();
    }

    if (v1) {
        v1 = false;

        tig_art_interface_id_create(335, 0, 0, 0, &art_id);
        if (!skip_bar_covers && tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
            window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER;
            window_data.background_color = art_anim_data.color_key;
            window_data.color_key = art_anim_data.color_key;
            window_data.message_filter = mainmenu_ui_message_filter;

            for (idx = 0; idx < 3; idx++) {
                window_data.rect.x = mainmenu_ui_bottom_bar_cover_rects[idx].x;
                window_data.rect.y = mainmenu_ui_bottom_bar_cover_rects[idx].y;
                window_data.rect.width = mainmenu_ui_bottom_bar_cover_rects[idx].width;
                window_data.rect.height = mainmenu_ui_bottom_bar_cover_rects[idx].height;
                hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

                src_rect.x = mainmenu_ui_bottom_bar_cover_rects[idx].x;
                src_rect.y = 0;
                src_rect.width = mainmenu_ui_bottom_bar_cover_rects[idx].width;
                src_rect.height = mainmenu_ui_bottom_bar_cover_rects[idx].height;

                dst_rect.x = 0;
                dst_rect.y = 0;
                dst_rect.width = src_rect.width;
                dst_rect.height = src_rect.height;

                art_blit_info.flags = 0;
                art_blit_info.art_id = art_id;
                art_blit_info.src_rect = &src_rect;
                art_blit_info.dst_rect = &dst_rect;

                rc = tig_window_create(&window_data, &(mainmenu_ui_bottom_bar_cover_window_handles[idx]));
                if (rc != TIG_OK) {
                    tig_debug_printf("MainMenu-UI: mainmenu_ui_create_window_func: ERROR: tig_window_create failed: Result: %d!\n", rc);
                    exit(EXIT_FAILURE);
                }

                tig_window_blit_art(mainmenu_ui_bottom_bar_cover_window_handles[idx], &art_blit_info);
                if (mainmenu_ui_has_custom_bg && !mainmenu_ui_custom_bg_is_fallback) {
                    mainmenu_ui_blit_custom_bg_to_window(mainmenu_ui_bottom_bar_cover_window_handles[idx], window_data.rect);
                }

                v1 = true;
            }
        }

        tig_art_interface_id_create(336, 0, 0, 0, &art_id);
        if (!skip_top_bar_cover && tig_art_anim_data(art_id, &art_anim_data) == TIG_OK) {
            window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MESSAGE_FILTER;
            window_data.rect = mainmenu_ui_top_bar_cover_rect;
            window_data.background_color = art_anim_data.color_key;
            window_data.color_key = art_anim_data.color_key;
            window_data.message_filter = mainmenu_ui_message_filter;
            hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

            src_rect.x = mainmenu_ui_top_bar_cover_rect.x;
            src_rect.y = 0;
            src_rect.width = mainmenu_ui_top_bar_cover_rect.width;
            src_rect.height = mainmenu_ui_top_bar_cover_rect.height;

            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.width = mainmenu_ui_top_bar_cover_rect.width;
            dst_rect.height = mainmenu_ui_top_bar_cover_rect.height;

            art_blit_info.flags = 0;
            art_blit_info.art_id = art_id;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;

            if (tig_window_create(&window_data, &mainmenu_ui_top_bar_cover_window_handle) != TIG_OK) {
                if (!v1) {
                    tig_debug_printf("mainmenu_ui_create_window_func: ERROR: tig_art_anim_data2 failed!\n");
                    exit(EXIT_SUCCESS); // FIXME: Should be `EXIT_FAILURE`.
                }
            }

            tig_window_blit_art(mainmenu_ui_top_bar_cover_window_handle, &art_blit_info);
            if (mainmenu_ui_has_custom_bg && !mainmenu_ui_custom_bg_is_fallback) {
                mainmenu_ui_blit_custom_bg_to_window(mainmenu_ui_top_bar_cover_window_handle, window_data.rect);
            }
        } else if (!skip_top_bar_cover) {
            // Only treat a missing top bar cover as fatal when we *expected*
            // to draw chrome. skip_top_bar_cover (Save / Load / Last-Save,
            // Options, hi-res chargen) deliberately omits it, so the
            // "no art" branch is the success path there.
            if (!v1) {
                tig_debug_printf("mainmenu_ui_create_window_func: ERROR: tig_art_anim_data2 failed!\n");
                exit(EXIT_SUCCESS); // FIXME: Should be `EXIT_FAILURE`.
            }
        }
    }

    if (window->background_art_num != -1) {
        src_rect.x = 0;
        src_rect.y = 0;
        art_blit_info.flags = 0;

        for (idx = 0; idx < 2; idx++) {
            if (window->field_3C[idx].field_0 != -1) {
                tig_art_interface_id_create(window->field_3C[idx].field_0, 0, 0, 0, &art_id);
                stru_64B870[idx].art_id = art_id;
                if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
                    tig_debug_printf("mainmenu_ui_create_window_func: ERROR: tig_art_frame_data failed!\n");
                    exit(EXIT_FAILURE);
                }

                src_rect.width = art_frame_data.width;
                src_rect.height = art_frame_data.height;

                dst_rect.x = window->field_3C[idx].x;
                dst_rect.y = window->field_3C[idx].y;
                dst_rect.width = art_frame_data.width;
                dst_rect.height = art_frame_data.height;

                art_blit_info.art_id = art_id;
                tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info);
            }
        }

        sub_547EF0();
    }

    mes_file_entry.num = window->num;
    font_desc.width = 0;
    font_desc.height = 0;

    if ((window->refresh_text_flags & 0x1) != 0) {
        if ((window->refresh_text_flags & 0x8) != 0) {
            font = dword_64C228[0][0];
        } else {
            font = dword_64C0CC[0][0];
        }
    } else {
        if ((window->refresh_text_flags & 0x10) != 0) {
            font = dword_64C218[0];
        } else {
            font = dword_64C210[0];
        }
    }

    for (idx = 0; idx < window->num_buttons; idx++) {
        button = &(window->buttons[idx]);

        if ((button->flags & 0x1) != 0) {
            int j;

            for (j = 0; j < 3; j++) {
                if (button->x >= mainmenu_ui_bottom_bar_cover_rects[j].x
                    && button->y + 441 >= mainmenu_ui_bottom_bar_cover_rects[j].y
                    && button->x < mainmenu_ui_bottom_bar_cover_rects[j].x + mainmenu_ui_bottom_bar_cover_rects[j].width
                    && button->y + 441 < mainmenu_ui_bottom_bar_cover_rects[j].y + mainmenu_ui_bottom_bar_cover_rects[j].height) {
                    break;
                }
            }

            if (j >= 3) {
                tig_debug_printf("mainmenu_ui_create_window_func: ERROR: j >= MM_UI_NUM_ROTWIN_COVERS!\n");
                exit(EXIT_FAILURE);
            }
            window_handle = mainmenu_ui_bottom_bar_cover_window_handles[j];

            // Save / Load screens may skip the bottom bar covers entirely
            // (see `skip_bottom_bar_covers` above). In that case the cover
            // handle is INVALID and any button parented to it must also be
            // skipped — otherwise `tig_button_create` would fail and the
            // unconditional exit below would crash the process.
            if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
                button->button_handle = TIG_BUTTON_HANDLE_INVALID;
                continue;
            }
        } else {
            window_handle = mainmenu_ui_window_handle;
        }

        if (mes_file_entry.num != -1
            && (button->flags & 0x4) == 0) {
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
            tig_font_push(font);
            font_desc.str = mes_file_entry.str;
            font_desc.width = 0;
            font_desc.height = 0;
            font_desc.flags = 0;
            tig_font_measure(&font_desc);
            tig_font_pop();

            if ((window->refresh_text_flags & 0x20) == 0) {
                button->rect.width = font_desc.width;
                button->rect.height = font_desc.height;

                if ((window->refresh_text_flags & 0x04) != 0) {
                    button->x -= font_desc.width / 2;
                }
            }

            text_rect = button->rect;
            text_rect.x = button->x - window->field_30;
            text_rect.y = button->y - window->field_34;
            mainmenu_ui_refresh_text(window_handle,
                mes_file_entry.str,
                &text_rect,
                window->refresh_text_flags | 0x20);

            mes_file_entry.num += 10;

            if (button->field_14 == 0 && (button->flags & 0x08) == 0) {
                if (mes_search(mainmenu_ui_mainmenu_mes_file, &mes_file_entry)) {
                    mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
                    button->field_14 = SDL_toupper(mes_file_entry.str[0]);
                } else {
                    tig_debug_printf("MainMenu: Error: Can't Find Hotkey!");
                    button->field_14 = -1;
                }
            }

            mes_file_entry.num -= 9;
        }

        if (!main_menu_button_create(button, font_desc.width, font_desc.height)) {
            tig_debug_printf("mainmenu_ui_create_window_func: ERROR: main_menu_button_create failed!\n");
            exit(EXIT_FAILURE);
        }
    }

    window->refresh_text_flags |= 0x20;
    mainmenu_ui_active = true;

    // CE: the panel is now TIG_WINDOW_TRANSPARENT and the persistent
    // backdrop carries the bg art at full screen. We deliberately do
    // NOT enable the translucent-black tint pathway on the panel
    // anymore: tig_video_blit_near_black_tinted writes ALL src
    // pixels (it doesn't honor color_key), so enabling tint on a
    // TRANSPARENT panel would re-introduce the keyed-color magenta
    // overpaint in the chromakey'd panel regions that TRANSPARENT
    // was meant to skip. The dark chrome borders inside the panel
    // stay solid dark (no see-through effect on chrome itself), but
    // the keyed regions correctly composite-through to the backdrop
    // bg behind. Net visual: same overall look, no scale-mismatch
    // tear between panel and backdrop.

    // CE: panel entrance animation — scales + fades in.
    //
    // Skip when the window handle is the backdrop alias (custom-UI
    // shell case): the backdrop has its own transform tween driven
    // by the bg state machine below, and a second show() on the same
    // handle would conflict.
    //
    // EXCEPTION: the credits flow. mainmenu_ui_credits_create sets
    // mainmenu_ui_custom_bg_window_type_override + bg_type=CREDITS
    // before calling mainmenu_ui_open to create the MAINMENU panel
    // with credits-bg art underneath. The slide_ui then plays a
    // slideshow on top with its own fade transitions — adding a
    // panel scale-in here reads as a redundant "pop" inside the
    // slideshow's fade.
    //
    // Two animation variants for non-backdrop-alias panels:
    //
    //  - LEGACY SHELL menus with separate panel (vanilla 800x600
    //    or hi-res no-custom-bg + shell type): match the backdrop's
    //    entrance exactly — scale 0.98→1.0, alpha=1, 300ms. Same
    //    profile + same scale range = panel and backdrop grow in
    //    lockstep, so the panel chrome never exposes a difference
    //    between its own scale and the backdrop's filler around it.
    //
    //  - Everything else (sub-window panels in either mode, custom-
    //    UI sub-windows): Phase 1.1 entrance — scale 0.92→1.0 +
    //    alpha 0→1, default entrance profile. The more pronounced
    //    scale + alpha-in pop is appropriate when the panel is a
    //    distinct sub-screen appearing over a different bg.
    bool panel_is_backdrop_alias =
        (mainmenu_ui_window_handle == mainmenu_ui_backdrop_handle
         && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID);
    bool entering_credits_overlay =
        (mainmenu_ui_custom_bg_window_type_override
         && mainmenu_ui_custom_bg_window_type == MM_WINDOW_CREDITS);
    // For legacy session-entrance (no custom bg + backdrop being
    // freshly created), the panel show is chained from the
    // backdrop fade-in's on_complete — skip firing it here so
    // the sequence runs cleanly. For other cases (sub-windows,
    // custom UI shell, transitions where the backdrop already
    // exists), fire the panel show now.
    if (mainmenu_ui_window_handle != TIG_WINDOW_HANDLE_INVALID
        && !panel_is_backdrop_alias
        && !entering_credits_overlay) {
        ui_anim_window_show(mainmenu_ui_window_handle,
            UI_ANIM_ANCHOR_CENTER, 0.92f, NULL);

        // CE: animate the chrome covers in tandem with the panel so the
        // chargen / shell window enters as ONE composite — not just a
        // middle panel scaling in over already-snapped-in header /
        // wings. Same anchor + scale-from so all pieces feel like one
        // unit. The covers were created above; they're at their final
        // positions, ui_anim_window_show seeds the transform to scale
        // 0.92 / alpha 0 and springs to (1, 1).
        if (mainmenu_ui_top_bar_cover_window_handle != TIG_WINDOW_HANDLE_INVALID) {
            ui_anim_window_show(mainmenu_ui_top_bar_cover_window_handle,
                UI_ANIM_ANCHOR_CENTER, 0.92f, NULL);
        }
        for (int ci = 0; ci < 3; ci++) {
            if (mainmenu_ui_bottom_bar_cover_window_handles[ci] != TIG_WINDOW_HANDLE_INVALID) {
                ui_anim_window_show(mainmenu_ui_bottom_bar_cover_window_handles[ci],
                    UI_ANIM_ANCHOR_CENTER, 0.92f, NULL);
            }
        }
        // CE: HUD strip used as band by chargen / vanilla Options-Save-
        // Load is the bottom-most chunk of the same composite. Animate
        // it in too — but ONLY when entering band mode from a non-
        // band menu (e.g. MAIN_MENU → NEW_CHAR). On band↔band
        // transitions (NEW_CHAR → CHAREDIT → SHOP) the band stays
        // visible across the swap; firing ui_anim_window_show would
        // briefly snap its transform to (0.92, 0) and animate up,
        // creating a visible flicker for ~200ms on every chargen
        // step. Detecting via prev_window_type is reliable because
        // sub_546DD0 captures it right before the close runs.
        bool prev_was_band =
            mainmenu_ui_prev_window_type == MM_WINDOW_NEW_CHAR
            || mainmenu_ui_prev_window_type == MM_WINDOW_PREGEN_CHAR
            || mainmenu_ui_prev_window_type == MM_WINDOW_CHAREDIT
            || mainmenu_ui_prev_window_type == MM_WINDOW_SHOP;
        if ((chargen_screen || (!is_hires && save_load_screen)
                || (!is_hires && mainmenu_ui_window_type == MM_WINDOW_OPTIONS))
            && !prev_was_band) {
            tig_window_handle_t band = intgame_get_band_bar_handle();
            if (band != TIG_WINDOW_HANDLE_INVALID) {
                ui_anim_window_show(band,
                    UI_ANIM_ANCHOR_CENTER, 0.92f, NULL);
            }
        }
    }

    // CE: persistent-backdrop animation state machine.
    //
    //  - SESSION ENTRANCE (created_backdrop_now == true):
    //      Bg fades + scales in from (0.98, 0) to the resting target
    //      state. If the user opened into a shell menu, target is
    //      (1.0, 1.0). If they opened directly into a sub-window
    //      (e.g. in-game Cmd+O for Options), target is (0.96, 1.0).
    //      300ms settle, damping 1.2.
    //
    //  - RE-OPEN MID-EXIT (mainmenu_ui_bg_exit_pending was set):
    //      A previous session was fading out — clear the pending
    //      flag and retarget the in-flight tween to the new entrance
    //      target. ui_anim_window_transform_to preserves the current
    //      spring value + velocity, so the fade reverses smoothly
    //      from wherever it was. The previous on_complete
    //      (mainmenu_ui_bg_finalize_exit) fires harmlessly during the
    //      retarget because exit_pending is now false.
    //
    //  - SHELL ↔ SUB-WINDOW RECEDE / SCALE-UP:
    //      Backdrop scales 1.0 ↔ 0.96 on transitions between shell
    //      and sub-window states. Both directions 240ms, damping 1.2.
    //      The retarget pattern handles rapid back-and-forth (user
    //      hammering ESC / buttons) — current value preserved, new
    //      target swapped in.
    //
    //  - INTRA-SHELL OR INTRA-SUB SWAPS:
    //      No bg animation (shell menus all hold at 1.0; sub-windows
    //      all hold at 0.96).
    //
    //  - SESSION EXIT:
    //      Started in mainmenu_ui_handle's post-loop and runs async
    //      in the main game loop's ui_anim_ping.
    // Backdrop animation state machine. Runs whenever a backdrop
    // exists — animating an empty backdrop (no custom bg) is mostly
    // invisible (scale of black = black; alpha of black against
    // black-tig-default = unchanged) and is necessary because the
    // initial mainmenu open at cold start often goes through a
    // bgless WINDOW_0 splash before transitioning to MAINMENU. If
    // we suppress the WINDOW_0 animation, the spring state isn't
    // primed and the follow-up MAINMENU transition reads
    // bg_receded as still-default → no animation fires either.
    if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
        bool want_receded = !mainmenu_ui_is_shell_menu(mainmenu_ui_window_type);
        float target_scale = want_receded ? 0.96f : 1.0f;

        if (created_backdrop_now) {
            // CE: bg entrance — backdrop scales 0.98 → 1.0/0.96
            // over 300ms.
            //
            // Alpha fade-in (alpha 0→1) when EITHER:
            //   - Custom UI bg art is loaded (the user wants to
            //     see the custom art fade in), OR
            //   - We're pre-game (cold-start main menu, no game
            //     world behind to expose during the fade).
            //
            // No alpha fade (alpha=1 from frame 1) for the legacy
            // IN-GAME case (e.g. ESC into pause menu over an
            // active game world). Fading alpha 0→1 there would
            // momentarily reveal the live game world through the
            // partially-transparent black backdrop.
            float alpha_from =
                (mainmenu_ui_has_custom_bg || !is_in_game) ? 0.0f : 1.0f;
            // Damping 1.0 (critical) when ending at the receded
            // 0.96 soft rest; 1.2 (overdamped) when ending at 1.0
            // where transform_clear gives a crisp 1:1 endpoint
            // (see the want_receded != bg_receded branch below for
            // the full rationale).
            ui_anim_profile_t entrance_profile = {
                300,
                want_receded ? 1.0f : 1.2f };
            ui_anim_window_transform_from_to(
                mainmenu_ui_backdrop_handle,
                0.98f, alpha_from, target_scale, 1.0f,
                UI_ANIM_ANCHOR_CENTER, &entrance_profile);
            mainmenu_ui_bg_receded = want_receded;
        } else if (mainmenu_ui_bg_exit_pending) {
            // Reverse course mid-exit. Clear flag BEFORE retargeting
            // so the previous on_complete (when fired by the retarget)
            // sees a cleared flag and no-ops the destroy. The exit
            // tween is still active, so the from_* values are ignored
            // — retarget preserves current spring value + velocity.
            mainmenu_ui_bg_exit_pending = false;
            ui_anim_profile_t entrance_profile = {
                300,
                want_receded ? 1.0f : 1.2f };
            ui_anim_window_transform_from_to(mainmenu_ui_backdrop_handle,
                1.0f, 1.0f, target_scale, 1.0f,
                UI_ANIM_ANCHOR_CENTER, &entrance_profile);
            mainmenu_ui_bg_receded = want_receded;
        } else if (want_receded != mainmenu_ui_bg_receded) {
            // sub ↔ shell scale transition. Shell-menu buttons +
            // text ride this transform on the backdrop — no
            // separate panel animation.
            //
            // Asymmetric timing: recede (shell→sub, scale-down)
            // gets 240ms — slower to read as "settling into a
            // sub-screen". Return (sub→shell, scale-up) gets
            // 180ms — snappy so dismissing the sub-window feels
            // immediate, not slow.
            //
            // IMPORTANT: seed the spring from the CURRENT visual
            // scale rather than the from-default of 1.0. The
            // previous transition's spring settled and cleared its
            // slot (tig holds the visual via transform_set, but
            // ui_anim has no active state), so without an explicit
            // from-value the new spring would start at 1.0 → 1.0
            // for the sub→shell case (already at target = snap).
            //
            // Damping 1.0 (critical) for the recede instead of 1.2
            // (overdamped): the integer-pixel dst has to step ~22
            // times during the 1.0→0.96 transition (each 1px
            // shrink), with the FINAL step landing at scale ≈
            // 0.9605. Overdamped's slow tail puts that last step
            // at near-zero velocity, which the eye reads as a
            // perceptible 1px snap right at lock-in. Critical
            // damping is the fastest monotonic-no-overshoot
            // profile — same total settle time but the tail moves
            // faster, so the final 1px step happens during
            // meaningful motion and is masked. The un-recede stays
            // at 1.2 because its 1.0 endpoint reverts to a crisp
            // 1:1 blit via transform_clear and doesn't have a
            // visible settle snap to worry about.
            float current_scale = mainmenu_ui_bg_receded ? 0.96f : 1.0f;
            ui_anim_profile_t bg_profile = {
                want_receded ? 240 : 180,
                want_receded ? 1.0f : 1.2f };
            ui_anim_window_transform_from_to(mainmenu_ui_backdrop_handle,
                current_scale, 1.0f, target_scale, 1.0f,
                UI_ANIM_ANCHOR_CENTER, &bg_profile);
            mainmenu_ui_bg_receded = want_receded;
        }
    }

    // CE: now that the mainmenu's backdrop is up (in hi-res), refresh
    // the modal-dialog auto-tint AND the HUD bar's tint underlay so
    // both point at the backdrop instead of whatever was selected
    // before (typically the iso world from iso_interface_create).
    // Modals raised from inside the mainmenu — overwrite confirms,
    // quit confirm, etc. — now see through to menu bg. The HUD bar
    // matters for the pre-game new-char / pregen / charedit flow,
    // where the bar is shown as a chrome band over the mainmenu
    // backdrop — without this its near-black pixels would punch
    // through to an unloaded iso world.
    intgame_refresh_modal_tint();
    intgame_refresh_hud_bar_tint();

    if (window->refresh_func != NULL) {
        window->refresh_func(NULL);
    }

    if (should_display) {
        tig_window_display();
    }
}

// 0x546B40
void mainmenu_ui_refresh_text(tig_window_handle_t window_handle, const char* str, TigRect* rect, unsigned int flags)
{
    TigRect host_rect;
    TigRect text_rect;
    TigFont font_desc;
    tig_font_handle_t* fonts;
    int pass;

    // CE: every text render path (initial create, rollover state
    // refresh, per-frame redraw_foreground in video-playback mode)
    // funnels through here. If the host window is the persistent
    // backdrop (shell menu case), translate the caller-supplied
    // design-space rect into backdrop-local coords ONCE and use the
    // result for both the bg restore and the glyph write. Centralizing
    // here means callers stay design-space-only and rollover never
    // jumps because some code path forgot the offset.
    host_rect = *rect;
    if (window_handle == mainmenu_ui_backdrop_handle
        && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
        host_rect.x += mainmenu_ui_backdrop_offset_x;
        host_rect.y += mainmenu_ui_backdrop_offset_y;
    } else if (window_handle == mainmenu_ui_window_handle
        && window_handle != TIG_WINDOW_HANDLE_INVALID) {
        // CE: sub-window panel — design-space rect needs to be
        // translated to panel-local coords by subtracting the
        // panel's design y-offset. Required when the panel was
        // cropped (hi-res Options shifts rect.y to 41) so labels
        // and other text drawn through this helper track the
        // chrome graphic, not the uncropped art's coords. Buttons
        // already do this subtraction in main_menu_button_create_ex.
        host_rect.y -= mainmenu_ui_window_rect.y;
    }
    text_rect = host_rect;

    if ((flags & 0x1) == 0) {
        fonts = (flags & 0x10) != 0 ? dword_64C0CC[0] : dword_64C210;

        if ((flags & 0x20) == 0) {
            tig_font_push(fonts[0]);
            font_desc.width = 0;
            font_desc.height = 0;
            font_desc.str = str;
            font_desc.flags = 0;
            tig_font_measure(&font_desc);
            tig_font_pop();

            text_rect.width = font_desc.width;
            text_rect.height = font_desc.height;
            if ((flags & 0x4) != 0) {
                text_rect.x -= font_desc.width / 2;
            }
        }

        tig_font_push(fonts[0]);
        if (tig_window_text_write(window_handle, str, &text_rect) != TIG_OK) {
            tig_debug_printf("mainmenu_ui_refresh_text: ERROR: tig_window_text_write failed!\n");
        }
        tig_font_pop();
    } else {
        if ((flags & 0x08) != 0) {
            fonts = dword_64C228[(flags & 0x2) != 0 ? 1 : 0];
        } else {
            fonts = dword_64C0CC[(flags & 0x2) != 0 ? 1 : 0];
        }

        if ((flags & 0x20) == 0) {
            tig_font_push(fonts[0]);
            font_desc.width = 0;
            font_desc.height = 0;
            font_desc.str = str;
            font_desc.flags = 0;
            tig_font_measure(&font_desc);
            tig_font_pop();

            text_rect.width = font_desc.width;
            text_rect.height = font_desc.height;
            if ((flags & 0x4) != 0) {
                text_rect.x -= font_desc.width / 2;
            }
        }

        // Restore receives the offset-applied rect (host-local
        // coords) so the bg sample lands at the actual on-screen
        // text area, not the design-space origin.
        mainmenu_ui_restore_text_backdrop(window_handle, &host_rect);

        for (pass = 0; pass < 3; pass++) {
            tig_font_push(fonts[pass]);
            text_rect.x += dword_5C4070[pass];
            text_rect.y += dword_5C4070[pass];
            if (tig_window_text_write(window_handle, str, &text_rect) != TIG_OK) {
                tig_debug_printf("mainmenu_ui_refresh_text: ERROR: tig_window_text_write2 failed!\n");
            }
            tig_font_pop();
        }
    }
}

// CE: returns true for the "shell" mainmenu types — main menu, pause
// menu (in-play + locked variants), single-player landing, and the
// pick-new-or-pregen "new game" landing. These are all UX-equivalent
// from the bg's perspective: bg sits at full scale (1.0), recedes
// only when transitioning into a real sub-window (Options / Load /
// Save / NewChar / Charedit / Credits / ...). Transitions BETWEEN
// shell menus (e.g. main menu → single player → pick new game)
// don't recede or scale the bg at all.
//
// Shell menus also:
//   - Skip painting the legacy chrome — they're visually JUST
//     bg + buttons. The custom-UI mainmenu_bg art replaces the
//     panel decoration entirely.
//   - Skip the scale/fade entrance + exit on the panel. Buttons
//     appear/disappear with the snap show/hide. The bg's recede
//     (when applicable) is the only animation.
//
// is_top_level was the original name for the same predicate; it
// stays as an alias because the bg-recede logic uses the same set.
static bool mainmenu_ui_is_shell_menu(MainMenuWindowType t)
{
    return t == MM_WINDOW_MAINMENU
        || t == MM_WINDOW_MAINMENU_IN_PLAY
        || t == MM_WINDOW_MAINMENU_IN_PLAY_LOCKED
        || t == MM_WINDOW_SINGLE_PLAYER
        || t == MM_WINDOW_PICK_NEW_OR_PREGEN;
}

static bool mainmenu_ui_is_top_level(MainMenuWindowType t)
{
    return mainmenu_ui_is_shell_menu(t);
}

// 0x546DD0
// CE: ui_anim on_complete callback — fired when the panel's hide
// spring settles (~260ms after sub_546DD0 starts the exit). Destroys
// the panel + cover strips that the now-departing menu sub-window
// owned. The backdrop is NOT touched here — it persists across the
// whole mainmenu session and only the OUTER mainmenu_ui_handle exit
// destroys it.
static void mainmenu_ui_finalize_close(void* ctx_v)
{
    MainmenuUiCloseCtx* ctx = (MainmenuUiCloseCtx*)ctx_v;
    int idx;
    if (!ctx->in_flight) {
        return;
    }
    if (ctx->panel != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_destroy(ctx->panel);
        ctx->panel = TIG_WINDOW_HANDLE_INVALID;
    }
    for (idx = 0; idx < 3; idx++) {
        if (ctx->bottom_covers[idx] != TIG_WINDOW_HANDLE_INVALID) {
            tig_window_destroy(ctx->bottom_covers[idx]);
            ctx->bottom_covers[idx] = TIG_WINDOW_HANDLE_INVALID;
        }
    }
    if (ctx->top_cover != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_destroy(ctx->top_cover);
        ctx->top_cover = TIG_WINDOW_HANDLE_INVALID;
    }
    ctx->in_flight = false;
}

// CE: ui_anim on_complete — fires when the bg exit fade-out tween
// settles. Guarded by mainmenu_ui_bg_exit_pending: if a re-open
// mid-fade cleared the flag (retargeting the tween back to the
// entrance state), this callback no-ops because the previous
// "destroy" intent was overridden. Otherwise we tear down the
// backdrop here, completing the deferred exit. Pattern matches
// fate/sleep slide reverse.
static void mainmenu_ui_bg_finalize_exit(void* ctx_v)
{
    (void)ctx_v;
    if (!mainmenu_ui_bg_exit_pending) {
        return;
    }
    mainmenu_ui_bg_exit_pending = false;
    mainmenu_ui_destroy_persistent_backdrop();
}

// CE: destroy the persistent backdrop. Called by either the bg-exit
// on_complete (normal session-end path) or directly during session-
// end cleanup (cfg-disabled / abort paths). Resets bg_receded so the
// next session starts from rest.
static void mainmenu_ui_destroy_persistent_backdrop(void)
{
    bool had_backdrop = (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID);
    if (mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_destroy(mainmenu_ui_backdrop_handle);
        mainmenu_ui_backdrop_handle = TIG_WINDOW_HANDLE_INVALID;
    }
    if (mainmenu_ui_has_custom_bg) {
        mainmenu_ui_free_custom_bg();
        mainmenu_ui_has_custom_bg = false;
    }
    mainmenu_ui_bg_receded = false;

    // CE: the modal-dialog tint and HUD bar tint pick their underlay
    // via intgame_translucent_black_pick, which previously favoured
    // the backdrop window whenever its handle was valid. Now that
    // we've just freed that handle, the picker would otherwise
    // continue handing the stale (freed) window to the compositor
    // for tinted blits — SDL_LockSurface'ing freed memory and
    // either crashing or rendering the tinted area as solid black.
    // Re-pick the underlay now so it falls through to iso world
    // (during gameplay) or no-tint (pre-game).
    if (had_backdrop) {
        intgame_refresh_modal_tint();
        intgame_refresh_hud_bar_tint();
    }
}

void sub_546DD0(void)
{
    int index;

    if (mainmenu_ui_active) {
        // CE: remember what we're closing so the next
        // create_window_func can make context-aware decisions
        // (e.g. suppress the new screen's entrance animation
        // when the previous screen was CREDITS — the credits
        // slideshow's own fade-out already covers the visual
        // transition and an extra panel scale-in reads as
        // redundant).
        mainmenu_ui_prev_window_type = mainmenu_ui_window_type;

        // CE: if a previous close's deferred destroy is still pending
        // (rapid menu navigation — e.g. user clicked Back during the
        // tail of an exit animation), flush it synchronously before
        // capturing the new handles. Single-slot ctx — can't carry two
        // pending closes at once.
        if (mainmenu_ui_pending_close.in_flight) {
            mainmenu_ui_finalize_close(&mainmenu_ui_pending_close);
        }

        sub_549450();
        timeevent_clear_all_typed(TIMEEVENT_TYPE_MAINMENU);

        for (index = 0; index < 2; index++) {
            stru_64B870[index].art_id = TIG_ART_ID_INVALID;
        }

        // CE: exit policy splits on host-window strategy.
        //
        // - BACKDROP-HOSTED shell (mainmenu_ui_window_handle ==
        //   mainmenu_ui_backdrop_handle): no panel to animate or
        //   destroy. Buttons live on the backdrop, so we destroy
        //   them in place and re-blit the bg art over the text
        //   area to wipe the painted glyphs. The backdrop survives
        //   the swap — its own scale tween (handled in the bg
        //   state machine in create_window_func) is the shell's
        //   exit animation if there is one.
        //
        // - SEPARATE PANEL (sub-windows always; shell menus in
        //   vanilla / no-custom-bg fallback): capture the panel +
        //   cover handles into the deferred-close ctx and start
        //   the panel's hide-animation. on_complete
        //   (mainmenu_ui_finalize_close) destroys the captured
        //   handles when the spring settles. The backdrop (if any)
        //   is persistent across the swap.
        bool is_backdrop_hosted =
            (mainmenu_ui_window_handle == mainmenu_ui_backdrop_handle
             && mainmenu_ui_backdrop_handle != TIG_WINDOW_HANDLE_INVALID);

        if (is_backdrop_hosted) {
            // Backdrop-hosted shell exit. Destroy buttons on the
            // backdrop, then re-blit the bg art to clear painted
            // text. The next create_window_func paints fresh
            // buttons + text on the same backdrop. In video-
            // playback mode the bg is the current video frame
            // which gets repainted by the per-frame tick anyway —
            // the one-shot bg blit here keeps the still-image
            // case clean.
            TigWindowData backdrop_data;
            tig_window_button_destroy(mainmenu_ui_backdrop_handle);
            if (mainmenu_ui_has_custom_bg
                && !mainmenu_ui_custom_bg_is_fallback
                && tig_window_data(mainmenu_ui_backdrop_handle, &backdrop_data) == TIG_OK) {
                mainmenu_ui_blit_custom_bg_to_window(
                    mainmenu_ui_backdrop_handle, backdrop_data.rect);
            }
            mainmenu_ui_pending_close.in_flight = false;
            mainmenu_ui_pending_close.panel = TIG_WINDOW_HANDLE_INVALID;
            mainmenu_ui_pending_close.top_cover = TIG_WINDOW_HANDLE_INVALID;
            for (index = 0; index < 3; index++) {
                mainmenu_ui_pending_close.bottom_covers[index] = TIG_WINDOW_HANDLE_INVALID;
            }
        } else {
            // Separate-panel exit (sub-windows, vanilla / no-bg
            // shells). Capture handles for deferred destruction
            // after the hide animation settles.
            mainmenu_ui_pending_close.in_flight = true;
            mainmenu_ui_pending_close.panel = mainmenu_ui_window_handle;
            mainmenu_ui_pending_close.top_cover = mainmenu_ui_top_bar_cover_window_handle;
            for (index = 0; index < 3; index++) {
                mainmenu_ui_pending_close.bottom_covers[index] = mainmenu_ui_bottom_bar_cover_window_handles[index];
            }
        }

        mainmenu_ui_window_handle = TIG_WINDOW_HANDLE_INVALID;
        mainmenu_ui_top_bar_cover_window_handle = TIG_WINDOW_HANDLE_INVALID;
        for (index = 0; index < 3; index++) {
            mainmenu_ui_bottom_bar_cover_window_handles[index] = TIG_WINDOW_HANDLE_INVALID;
        }

        // NOTE: custom_bg_vb is NOT freed here anymore — the next
        // sub-window's create_window_func loads a fresh bg via
        // mainmenu_ui_load_bg_vb, which frees the previous VB. If
        // there's no next create (session end), the destroy fn frees
        // it.

        mainmenu_ui_active = false;

        if (!is_backdrop_hosted) {
            // Separate panel: Phase 1.1 exit — hide from (1.0, 1.0)
            // to (0.92, 0) over EXIT-profile-default time. Cover
            // strips destroyed by the panel's on_complete.
            //
            // CE: also animate the chrome covers (top header + bottom
            // wings) and the HUD strip band out at the same time, so
            // the whole chargen / shell composite exits as one piece
            // — not just a middle panel collapsing while the chrome
            // around it snaps off. on_complete is only wired to the
            // panel; the covers complete around the same time (same
            // profile) and finalize_close destroys them all.
            if (mainmenu_ui_pending_close.panel != TIG_WINDOW_HANDLE_INVALID) {
                ui_anim_window_hide(mainmenu_ui_pending_close.panel,
                    UI_ANIM_ANCHOR_CENTER, 0.92f, NULL,
                    mainmenu_ui_finalize_close, &mainmenu_ui_pending_close);
                if (mainmenu_ui_pending_close.top_cover != TIG_WINDOW_HANDLE_INVALID) {
                    ui_anim_window_hide(mainmenu_ui_pending_close.top_cover,
                        UI_ANIM_ANCHOR_CENTER, 0.92f, NULL,
                        NULL, NULL);
                }
                for (index = 0; index < 3; index++) {
                    if (mainmenu_ui_pending_close.bottom_covers[index] != TIG_WINDOW_HANDLE_INVALID) {
                        ui_anim_window_hide(
                            mainmenu_ui_pending_close.bottom_covers[index],
                            UI_ANIM_ANCHOR_CENTER, 0.92f, NULL,
                            NULL, NULL);
                    }
                }
                // HUD strip band — only fire the scale-fade hide on
                // the band when we're ACTUALLY leaving band mode
                // (i.e. the chargen→game new-game path, where the
                // screen fades to black anyway). On chargen→chargen
                // transitions the band stays in band mode through
                // the swap, so applying a scale/alpha hide here
                // would leave the band at (0.92, 0) — and the next
                // chargen's entrance ui_anim_window_show, retargeting
                // from a settled slot, doesn't always animate the
                // band back to visible. Leave the band's transform
                // untouched on chargen↔chargen; the new menu's
                // show retarget then snaps from (1.0, 1.0) → (1.0,
                // 1.0) (no-op), keeping the band visible the whole
                // time. The new-game-start path covers the band with
                // gfade_run + slide_prepare_offscreen so missing its
                // exit animation here is invisible to the user.
                if (intgame_hud_is_band_mode()
                    && mainmenu_ui_start_new_game) {
                    tig_window_handle_t band = intgame_get_band_bar_handle();
                    if (band != TIG_WINDOW_HANDLE_INVALID) {
                        ui_anim_window_hide(band,
                            UI_ANIM_ANCHOR_CENTER, 0.92f, NULL,
                            NULL, NULL);
                    }
                }
            } else {
                mainmenu_ui_finalize_close(&mainmenu_ui_pending_close);
            }
        }

        // CE: mainmenu (and its backdrop) are gone — refresh both
        // the modal-dialog auto-tint and the HUD bar's tint so they
        // fall back to iso underlay if we're still in-game, or
        // disable if we returned to a pre-game state. Without this,
        // the next modal raised in gameplay would still target the
        // now-destroyed backdrop window handle as its underlay, and
        // the HUD bar (which uses a heavier subtract while pointed
        // at the menu backdrop) would keep that look in active
        // gameplay.
        intgame_refresh_modal_tint();
        intgame_refresh_hud_bar_tint();
    }
}

// 0x546E80
void mainmenu_ui_create_shared_radio_buttons(void)
{
    MainMenuWindowInfo* info;
    tig_button_handle_t group[2];

    info = main_menu_window_info[mainmenu_ui_window_type];
    group[0] = info->buttons[4].button_handle;
    group[1] = info->buttons[5].button_handle;
    if (tig_button_radio_group_create(2, group, info->flags & 1) != TIG_OK) {
        tig_debug_printf("mainmenu_ui_create_shared_radio_buttons: ERROR: tig_button_radio_group failed!\n");
    }
}

// 0x546EE0
bool mainmenu_ui_message_filter(TigMessage* msg)
{
    MainMenuWindowInfo* window;
    int idx;
    MesFileEntry mes_file_entry;
    MesFileEntry description_mes_file_entry;
    UiMessage ui_message;
    char str[MAX_STRING];
    int v2;
    int original_screen_x = 0;
    int original_screen_y = 0;
    TigMessage tmp_msg = *msg;
    msg = &tmp_msg;

    // Convert mouse position from screen coordinate system to centered 800x600
    // area.
    if (msg->type == TIG_MESSAGE_MOUSE) {
        TigRect rect = { 0, 0, 800, 600 };
        original_screen_x = msg->data.mouse.x;
        original_screen_y = msg->data.mouse.y;
        hrp_apply(&rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
        msg->data.mouse.x -= rect.x;
        msg->data.mouse.y -= rect.y;
    }

    window = main_menu_window_info[mainmenu_ui_window_type];

    if (slide_ui_is_active()) {
        return false;
    }

    if (msg->type == TIG_MESSAGE_MOUSE) {
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN) {
            if (window->scrollbar_rect.width > 0
                && msg->data.mouse.x >= window->content_rect.x
                && msg->data.mouse.y >= window->content_rect.y
                && msg->data.mouse.x < window->content_rect.x + window->content_rect.width
                && msg->data.mouse.y < window->content_rect.y + window->content_rect.height) {
                if (window->mouse_down_func != NULL) {
                    window->mouse_down_func(msg->data.mouse.x - window->scrollbar_rect.x, msg->data.mouse.y - window->scrollbar_rect.y - mainmenu_ui_window_rect.y);
                    return true;
                }
            }

            if (mainmenu_ui_window_type != MM_WINDOW_SAVE_GAME
                || (msg->data.mouse.x < mainmenu_ui_window_partial_rect.x
                    || msg->data.mouse.y < mainmenu_ui_window_partial_rect.y
                    || msg->data.mouse.x >= mainmenu_ui_window_partial_rect.x + mainmenu_ui_window_partial_rect.width
                    || msg->data.mouse.y >= mainmenu_ui_window_partial_rect.y + mainmenu_ui_window_partial_rect.height)) {
                sub_549450();
            }
        }

        switch (msg->data.mouse.event) {
        case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP:
            // Click outside the menu's actual screen rect and outside both
            // HUD strips dismisses overlay menus — same effect as ESC or
            // clicking the PC lens.
            //
            // Eligible for dismiss:
            //   - Any in-game flavor menu (stru_5C36B0[type][0] == true)
            //   - Main-menu Options / Load / Save: even though they aren't
            //     "in-game" they still have the main menu as a parent on
            //     the stack to pop back to.
            //
            // Exit behavior:
            //   - In-game flavor at the top of the stack (num_windows <= 1):
            //     full restore to game via sub_5412D0().
            //   - Otherwise: pop to parent via mainmenu_ui_close(true).
            //     This covers pause → Options → click-outside (back to
            //     pause) and main-menu → Load → click-outside (back to
            //     main menu) symmetrically.
            {
                bool in_game = stru_5C36B0[mainmenu_ui_type][0];
                bool dismissible_window = in_game
                    || mainmenu_ui_window_type == MM_WINDOW_OPTIONS
                    || mainmenu_ui_window_type == MM_WINDOW_LOAD_GAME
                    || mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME;
                if (dismissible_window
                    && mainmenu_ui_window_handle != TIG_WINDOW_HANDLE_INVALID) {
                    TigRect menu_rect;
                    bool have_rect = false;
                    // Shell menus alias mainmenu_ui_window_handle to
                    // the persistent backdrop, whose rect extends
                    // past the screen edges (overshoot for the
                    // recede animation) — using it for dismiss
                    // detection means clicks anywhere on screen
                    // would register as "inside" and never dismiss.
                    // For the dismiss check, use the design 800x600
                    // rect centered on screen instead — matches the
                    // visual footprint of the menu's buttons + text.
                    if (mainmenu_ui_window_handle == mainmenu_ui_backdrop_handle) {
                        menu_rect = mainmenu_ui_window_fullscreen_rect;
                        hrp_apply(&menu_rect,
                            GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
                        have_rect = true;
                    } else {
                        TigWindowData menu_wd;
                        if (tig_window_data(mainmenu_ui_window_handle, &menu_wd) == TIG_OK) {
                            menu_rect = menu_wd.rect;
                            have_rect = true;
                        }
                    }
                    if (have_rect
                        && intgame_should_dismiss_overlay_click(
                            original_screen_x, original_screen_y, &menu_rect)) {
                        if (in_game && mainmenu_ui_num_windows <= 1) {
                            sub_5412D0();
                        } else {
                            mainmenu_ui_close(true);
                        }
                        return true;
                    }
                }
            }
            switch (mainmenu_ui_window_type) {
            case MM_WINDOW_0:
            case MM_WINDOW_1:
                mainmenu_ui_close(false);
                mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
                mainmenu_ui_open();
                return true;
            case MM_WINDOW_OPTIONS:
                // Use _unscale: msg coords are 800x600-local here but the
                // lens check expects screen coords.
                if (intgame_pc_lens_check_pt_unscale(msg->data.mouse.x, msg->data.mouse.y)) {
                    // Mirror the Done button: commit module changes first
                    // (bails if module load failed so the user stays on the
                    // Options screen). Then:
                    //
                    //   - In-game flavor (lens is PASSTHROUGH showing the
                    //     game world): treat the lens tap as a shortcut
                    //     straight back to game, regardless of how deep
                    //     we are in the menu stack. This matches Save
                    //     Game's PC-lens behavior and lets pause→Options
                    //     → lens-tap return to game in one click instead
                    //     of popping back to the pause menu first.
                    //
                    //   - Pre-game main-menu Options (lens is NONE, so
                    //     this branch isn't normally reachable via a
                    //     real click): fall back to pop-to-parent.
                    if (options_ui_load_module()) {
                        if (stru_5C36B0[mainmenu_ui_type][0]) {
                            // CE: Recenter on the PC — see logbook_ui for
                            // rationale. No-op when there is no local PC
                            // (pre-game main menu).
                            intgame_recenter_on_pc();
                            sub_5412D0();
                        } else {
                            gsound_play_sfx(0, 1);
                            mainmenu_ui_close(true);
                        }
                    }
                    return true;
                }
                break;
            case MM_WINDOW_LOAD_GAME:
                if (intgame_pc_lens_check_pt_unscale(msg->data.mouse.x, msg->data.mouse.y)) {
                    // CE: Recenter on the PC — see logbook_ui for rationale.
                    intgame_recenter_on_pc();
                    if (dword_64C450) {
                        sub_5412D0();
                    } else {
                        mainmenu_ui_close(true);
                    }
                    return true;
                }
                break;
            case MM_WINDOW_SAVE_GAME:
                if (intgame_pc_lens_check_pt_unscale(msg->data.mouse.x, msg->data.mouse.y)) {
                    // CE: Recenter on the PC — see logbook_ui for rationale.
                    intgame_recenter_on_pc();
                    sub_5412D0();
                    return true;
                }
                break;
            default:
                break;
            }

            if (window->mouse_up_func != NULL
                && window->content_rect.width > 0
                && msg->data.mouse.x >= window->content_rect.x
                && msg->data.mouse.y - mainmenu_ui_window_rect.y >= window->content_rect.y
                && msg->data.mouse.x < window->content_rect.x + window->content_rect.width
                && msg->data.mouse.y - mainmenu_ui_window_rect.y < window->content_rect.y + window->content_rect.height) {
                gsound_play_sfx(0, 1);
                window->mouse_up_func(msg->data.mouse.x - window->content_rect.x, msg->data.mouse.y - window->content_rect.y - mainmenu_ui_window_rect.y);
                return true;
            }

            return false;
        case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
        case TIG_MESSAGE_MOUSE_MIDDLE_BUTTON_UP:
            switch (mainmenu_ui_window_type) {
            case MM_WINDOW_0:
            case MM_WINDOW_1:
                mainmenu_ui_close(false);
                mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
                mainmenu_ui_open();
                return true;
            default:
                break;
            }
            return false;
        case TIG_MESSAGE_MOUSE_IDLE:
            if (window->mouse_idle_func != NULL) {
                window->mouse_idle_func(msg->data.mouse.x, msg->data.mouse.y);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    if (msg->type == TIG_MESSAGE_BUTTON) {
        switch (msg->data.button.state) {
        case TIG_BUTTON_STATE_PRESSED:
            if (window->button_press_func != NULL && window->button_press_func(msg->data.button.button_handle)) {
                return true;
            }
            return false;
        case TIG_BUTTON_STATE_RELEASED:
            if (window->button_release_func != NULL && window->button_release_func(msg->data.button.button_handle)) {
                return true;
            }

            for (idx = 0; idx < window->num_buttons; idx++) {
                if (msg->data.button.button_handle == window->buttons[idx].button_handle) {
                    if (window->buttons[idx].field_10 > 0) {
                        if (window->execute_func != NULL) {
                            dword_5C3FB8 = window->execute_func(idx);
                            if (dword_5C3FB8 == 0) {
                                return true;
                            }
                        }

                        mainmenu_ui_close(false);
                        mainmenu_ui_window_type = window->buttons[idx].field_10;
                        mainmenu_ui_open();
                        return true;
                    }

                    if (window->buttons[idx].field_10 == 0) {
                        sub_5412D0();
                        if (stru_5C36B0[mainmenu_ui_type][1]
                            || mainmenu_ui_window_type == MM_WINDOW_MAINMENU
                            || mainmenu_ui_window_type == MM_WINDOW_MAINMENU_IN_PLAY) {
                            mainmenu_ui_exit_game();
                        }
                        mainmenu_ui_window_type = MM_WINDOW_0;
                        return true;
                    }

                    if (window->buttons[idx].field_10 == -2) {
                        mainmenu_ui_close(true);
                        if (mainmenu_ui_window_type == MM_WINDOW_0) {
                            sub_5412D0();
                        }
                    }
                    return true;
                }
            }

            return false;
        case TIG_BUTTON_STATE_MOUSE_INSIDE:
            if (window->button_hover_func != NULL) {
                window->button_hover_func(msg->data.button.button_handle);
            }

            for (idx = 0; idx < window->num_buttons; idx++) {
                if (msg->data.button.button_handle == window->buttons[idx].button_handle) {
                    break;
                }
            }

            if (idx < window->num_buttons) {
                if (window->num != -1) {
                    mainmenu_ui_refresh_button_text(idx, 0x2);
                }

                v2 = window->buttons[idx].field_2C;
            } else {
                v2 = -1;
            }

            if (v2 == 0) {
                if (msg->data.button.button_handle == stru_5C45D8.button_handle) {
                    v2 = stru_5C45D8.field_2C;
                } else if (msg->data.button.button_handle == stru_5C4838.button_handle) {
                    v2 = stru_5C4838.field_2C;
                } else {
                    return false;
                }
            }

            if (v2 > 0) {
                mes_file_entry.num = 6999;
                if (!mes_search(mainmenu_ui_mainmenu_mes_file, &mes_file_entry)) {
                    tig_debug_printf("MMUI: ERROR: Hover Text for button is Unreachable!\n");
                    return false;
                }

                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

                description_mes_file_entry.num = v2;
                mes_get_msg(mainmenu_ui_mainmenu_mes_file, &description_mes_file_entry);

                snprintf(str, sizeof(str), "%s\n%s", mes_file_entry.str, description_mes_file_entry.str);

                ui_message.type = UI_MSG_TYPE_FEEDBACK;
                ui_message.str = str;
                intgame_message_window_display_msg(&ui_message);
                return true;
            }

            return false;
        case TIG_BUTTON_STATE_MOUSE_OUTSIDE:
            if (window->button_leave_func != NULL) {
                window->button_leave_func(msg->data.button.button_handle);
            }

            for (idx = 0; idx < window->num_buttons; idx++) {
                if (msg->data.button.button_handle == window->buttons[idx].button_handle) {
                    break;
                }
            }

            if (idx < window->num_buttons) {
                if (window->num != -1) {
                    mainmenu_ui_refresh_button_text(idx, 0);
                }

                v2 = window->buttons[idx].field_2C;
            } else {
                v2 = -1;
            }

            if (v2 == 0) {
                if (msg->data.button.button_handle == stru_5C45D8.button_handle) {
                    v2 = stru_5C45D8.field_2C;
                } else if (msg->data.button.button_handle == stru_5C4838.button_handle) {
                    v2 = stru_5C4838.field_2C;
                } else {
                    return false;
                }
            }

            if (v2 > 0) {
                intgame_message_window_clear();
                return true;
            }

            return false;
        default:
            return false;
        }
    }

    if (msg->type == TIG_MESSAGE_KEYBOARD) {
        // CE: With intgame hidden we have to manually route keyboard events to
        // textedit ui.
        if (textedit_ui_is_focused()) {
            return textedit_ui_process_message(msg);
        }

        if (!msg->data.keyboard.pressed) {
            switch (mainmenu_ui_window_type) {
            case MM_WINDOW_0:
                return false;
            case MM_WINDOW_1:
                mainmenu_ui_close(false);
                mainmenu_ui_window_type = 2;
                mainmenu_ui_open();
                return true;
            case MM_WINDOW_MAINMENU:
                return false;
            case MM_WINDOW_OPTIONS:
                if (msg->data.keyboard.scancode == SDL_SCANCODE_O) {
                    // O toggles Options closed only when we entered here
                    // directly via the in-game O shortcut (top of menu
                    // stack). When stacked under another menu — e.g. pause
                    // menu → Options via its "O" hotkey button — the keyup
                    // raced in here right after the keydown opened the
                    // window and was auto-dismissing it. Swallow the key
                    // either way so it doesn't fall through to other
                    // handlers.
                    if (!stru_5C36B0[mainmenu_ui_type][1]
                        && mainmenu_ui_num_windows <= 1) {
                        sub_5412D0();
                    }
                    return true;
                }
                return false;
            case MM_WINDOW_LOAD_GAME:
                switch (msg->data.keyboard.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    gsound_play_sfx(0, 1);
                    mainmenu_ui_close(true);
                    if (mainmenu_ui_window_type == MM_WINDOW_0) {
                        sub_5412D0();
                    }
                    return true;
                case SDL_SCANCODE_UP:
                    sub_543060();
                    return true;
                case SDL_SCANCODE_DOWN:
                    sub_5430D0();
                    return true;
                case SDL_SCANCODE_BACKSPACE:
                case SDL_SCANCODE_DELETE:
                    gsound_play_sfx(0, 1);
                    mainmenu_ui_load_game_handle_delete();
                    return true;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                    sub_5480C0(2);
                    return true;
                default:
                    break;
                }
                return false;
            case MM_WINDOW_SAVE_GAME:
                switch (msg->data.keyboard.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    gsound_play_sfx(0, 1);
                    mainmenu_ui_close(true);
                    if (mainmenu_ui_window_type == MM_WINDOW_0) {
                        sub_5412D0();
                    }
                    return true;
                case SDL_SCANCODE_UP:
                    // CE: swallow the release from an arrow that just exited the name
                    // input (already landed on the right row -- don't move again).
                    if (mainmenu_ui_arrow_exit_pending) {
                        mainmenu_ui_arrow_exit_pending = false;
                        return true;
                    }
                    sub_544210();
                    return true;
                case SDL_SCANCODE_DOWN:
                    if (mainmenu_ui_arrow_exit_pending) {
                        mainmenu_ui_arrow_exit_pending = false;
                        return true;
                    }
                    sub_544250();
                    return true;
                case SDL_SCANCODE_BACKSPACE:
                case SDL_SCANCODE_DELETE:
                    gsound_play_sfx(0, 1);
                    mainmenu_ui_save_game_handle_delete();
                    return true;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                    sub_5480C0(2);
                    return true;
                default:
                    break;
                }
                return false;
            default:
                switch (msg->data.keyboard.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    gsound_play_sfx(0, 1);
                    mainmenu_ui_close(true);
                    if (mainmenu_ui_window_type == MM_WINDOW_0) {
                        sub_5412D0();
                    }
                    return true;
                default:
                    break;
                }
                return false;
            }
        } else {
            if (sub_549A60()) {
                return false;
            }

            if ((mainmenu_ui_window_type == MM_WINDOW_SHOP || mainmenu_ui_window_type == MM_WINDOW_CHAREDIT)
                && msg->data.keyboard.key == SDLK_RETURN
                && iso_interface_window_get() != ROTWIN_TYPE_QUANTITY) {
                gsound_play_sfx(0, 1);
                sub_5480C0(2);
                return true;
            }

            // CE: Skip the auto-derived first-letter button hotkeys when Cmd /
            // Ctrl is held. Cmd+Q on macOS fires both SDL_EVENT_QUIT and a
            // KEY_DOWN for Q, and without this guard the latter activates the
            // pause menu's "Quit Game" button (which does unload-to-main-menu)
            // before the QUIT path reaches the confirm-and-exit-to-desktop
            // handler. Letting TIG_MESSAGE_QUIT own all modifier-Q variants
            // makes Cmd+Q behavior consistent everywhere (in-play, pause
            // menu, main menu): always confirm + quit to desktop.
            // Same defense for Cmd+S/L/O which already have explicit handlers
            // in main.c — without this they'd race button hotkeys here too.
            if (tig_kb_get_modifier(SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
                return false;
            }

            for (idx = 0; idx < window->num_buttons; idx++) {
                bool hidden;
                tig_button_is_hidden(window->buttons[idx].button_handle, &hidden);
                if (!hidden && SDL_toupper(msg->data.keyboard.key) == window->buttons[idx].field_14) {
                    break;
                }
            }

            if (idx >= window->num_buttons) {
                return false;
            }

            if (window->buttons[idx].art_num == -1) {
                gsound_play_sfx(SND_INTERFACE_MORPHTEXT_CLICK, 1);
            } else {
                gsound_play_sfx(0, 1);
            }

            if (window->buttons[idx].field_10 > 0) {
                if (window->execute_func != NULL) {
                    dword_5C3FB8 = window->execute_func(idx);
                    if (!dword_5C3FB8) {
                        return true;
                    }
                }

                mainmenu_ui_close(false);
                mainmenu_ui_window_type = window->buttons[idx].field_10;
                mainmenu_ui_open();
                return true;
            }

            if (window->buttons[idx].field_10 == 0) {
                sub_5412D0();
                if (stru_5C36B0[mainmenu_ui_type][1]
                    || mainmenu_ui_window_type == MM_WINDOW_MAINMENU
                    || mainmenu_ui_window_type == MM_WINDOW_MAINMENU_IN_PLAY) {
                    mainmenu_ui_exit_game();
                }
                mainmenu_ui_window_type = MM_WINDOW_0;
                return true;
            }

            if (window->buttons[idx].field_10 == -2) {
                mainmenu_ui_close(true);
                if (mainmenu_ui_window_type == MM_WINDOW_0) {
                    sub_5412D0();
                }
                return true;
            }

            if (window->buttons[idx].field_10 == -1) {
                switch (mainmenu_ui_window_type) {
                case MM_WINDOW_MAINMENU_IN_PLAY:
                    switch (idx) {
                    case 3:
                        if (mainmenu_ui_confirm_quit() == TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
                            // FIXME: Looks wrong.
                            tig_button_hide(3);

                            return sub_549310(3);
                        }
                        return true;
                    case 4:
                        if (!stru_5C36B0[mainmenu_ui_type][0]) {
                            mainmenu_ui_close(true);

                            if (mainmenu_ui_window_type == MM_WINDOW_0) {
                                sub_5412D0();
                            }
                        }
                        return true;
                    }

                    return true;
                case MM_WINDOW_MAINMENU_IN_PLAY_LOCKED:
                    switch (idx) {
                    case 1:
                        if (mainmenu_ui_confirm_quit() == TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
                            // FIXME: Looks wrong.
                            tig_button_hide(1);

                            return sub_549310(1);
                        }
                        return true;
                    case 2:
                        if (!stru_5C36B0[mainmenu_ui_type][0]) {
                            mainmenu_ui_close(true);

                            if (mainmenu_ui_window_type == MM_WINDOW_0) {
                                sub_5412D0();
                            }
                        }
                        return true;
                    }

                    return true;
                default:
                    break;
                }

                return true;
            }

            return true;
        }
    }

    // CE: With intgame hidden we have to manually route keyboard events to
    // textedit ui.
    if (msg->type == TIG_MESSAGE_TEXT_INPUT) {
        return textedit_ui_process_message(msg);
    }

    return false;
}

// 0x547E00
void mainmenu_ui_refresh_button_text(int btn, unsigned int flags)
{
    MainMenuWindowInfo* window;
    MainMenuButtonInfo* button;
    bool hidden;
    MesFileEntry mes_file_entry;
    TigRect rect;

    window = main_menu_window_info[mainmenu_ui_window_type];
    button = &(window->buttons[btn]);

    // FIXME: Result is not being checked.
    tig_button_is_hidden(button->button_handle, &hidden);
    if (!hidden) {
        mes_file_entry.num = window->num;
        if ((button->flags & 0x1) != 0) {
            tig_debug_printf("mainmenu_ui_refresh_button_text: ERROR: flags wrong!\n");
            exit(EXIT_FAILURE);
        }

        if (window->num != -1 && (button->flags & 0x4) == 0) {
            mes_file_entry.num = window->num + btn;
            mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);

            rect = button->rect;
            rect.x = button->x - window->field_30;
            rect.y = button->y - window->field_34;
            mainmenu_ui_refresh_text(mainmenu_ui_window_handle,
                mes_file_entry.str,
                &rect,
                window->refresh_text_flags | flags);
        }
    }
}

// 0x547EF0
void sub_547EF0(void)
{
    TigArtAnimData art_anim_data;
    DateTime datetime;
    TimeEvent timeevent;
    int index;

    for (index = 0; index < 2; index++) {
        if (tig_art_anim_data(stru_64B870[index].art_id, &art_anim_data) == TIG_OK
            && art_anim_data.num_frames > 1) {
            stru_64B870[index].max_frame = art_anim_data.num_frames - 1;
            stru_64B870[index].fps = 1000 / art_anim_data.fps;
            stru_64B870[index].x = main_menu_window_info[mainmenu_ui_window_type]->field_3C[index].x;
            stru_64B870[index].y = main_menu_window_info[mainmenu_ui_window_type]->field_3C[index].y;

            timeevent.type = TIMEEVENT_TYPE_MAINMENU;
            timeevent.params[0].integer_value = index;
            sub_45A950(&datetime, stru_64B870[index].fps);
            timeevent_add_delay(&timeevent, &datetime);
        }
    }
}

// 0x547F90
bool mainmenu_ui_process_callback(TimeEvent* timeevent)
{
    int index;
    tig_art_id_t next_art_id;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    DateTime datetime;
    TimeEvent next_timeevent;

    index = timeevent->params[0].integer_value;
    if (stru_64B870[index].art_id == TIG_ART_ID_INVALID) {
        return true;
    }

    next_art_id = tig_art_id_frame_inc(stru_64B870[index].art_id);
    if (tig_art_id_frame_get(next_art_id) >= stru_64B870[index].max_frame) {
        next_art_id = tig_art_id_frame_set(next_art_id, 0);
    }

    if (tig_art_frame_data(next_art_id, &art_frame_data) == TIG_OK) {
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = art_frame_data.width;
        src_rect.height = art_frame_data.height;

        dst_rect.x = stru_64B870[index].x;
        dst_rect.y = stru_64B870[index].y;
        dst_rect.width = art_frame_data.width;
        dst_rect.height = art_frame_data.height;

        art_blit_info.flags = 0;
        art_blit_info.art_id = next_art_id;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &dst_rect;
        tig_window_blit_art(mainmenu_ui_window_handle, &art_blit_info);
    } else {
        tig_debug_printf("main_menu_ui_process_callback: ERROR: tig_art_frame_data failed!\n");
    }

    next_timeevent.type = TIMEEVENT_TYPE_MAINMENU;
    next_timeevent.params[0].integer_value = index;
    sub_45A950(&datetime, stru_64B870[index].fps);
    timeevent_add_delay(&next_timeevent, &datetime);

    return true;
}

// 0x5480C0
void sub_5480C0(int a1)
{
    MainMenuWindowInfo* window;

    window = main_menu_window_info[mainmenu_ui_window_type];
    switch (a1) {
    case 0:
        sub_548FF0(window->top_index - 1);
        return;
    case 1:
        sub_548FF0(window->top_index + 1);
        return;
    case 2:
        if (window->execute_func != NULL) {
            dword_5C3FB8 = window->execute_func(-1);
            if (!dword_5C3FB8) {
                return;
            }
        }
        if (mainmenu_ui_active) {
            if (mainmenu_ui_window_type == MM_WINDOW_SHOP) {
                sub_5412D0();
            } else {
                mainmenu_ui_close(false);
                mainmenu_ui_window_type++;
                mainmenu_ui_open();
            }
        }
        return;
    case 3:
        if (mainmenu_ui_window_type != MM_WINDOW_OPTIONS || options_ui_load_module()) {
            // Same stack-aware close as ESC: when this menu was launched
            // directly into the world (Cmd+Shift+S, Cmd+O, etc. — top of
            // stack with an "exit to game" type), route through sub_5412D0
            // so intgame_show() and the rest of the restore steps run.
            // When stacked under a parent menu (pause menu → Save/Load),
            // fall back to the normal close-back that pops to the parent.
            if (mainmenu_ui_num_windows <= 1
                && stru_5C36B0[mainmenu_ui_type][0]) {
                sub_5412D0();
            } else {
                mainmenu_ui_close(true);
            }
        }
        return;
    case 4:
        window->flags &= ~0x1;
        if ((window->flags & 0x2) != 0) {
            window->selected_index = 0;
        }
        if ((window->flags & 0x4) != 0) {
            if (window->selected_index > 0) {
                window->selected_index--;
            }
        }
        if (window->refresh_func != NULL) {
            window->refresh_func(NULL);
        }
        return;
    case 5:
        window->flags |= 0x1;
        if ((window->flags & 0x2) != 0) {
            window->selected_index = -1;
        }
        if ((window->flags & 0x4) != 0) {
            if (window->selected_index < window->cnt - 1) {
                window->selected_index++;
            }
        }
        if (window->refresh_func != NULL) {
            window->refresh_func(NULL);
        }
        return;
    }
}

// 0x548F10
void mmUIWinRefreshScrollBar(void)
{
    TigRect src_rect;
    TigRect dst_rect;
    TigArtBlitInfo blit_info;
    TigArtFrameData art_frame_data;
    tig_art_id_t art_id;
    MainMenuWindowInfo* curr_window_info;

    curr_window_info = main_menu_window_info[mainmenu_ui_window_type];
    if (curr_window_info->scrollbar_rect.width > 0) {
        tig_art_interface_id_create(316, 0, 0, 0, &art_id);
        if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
            tig_debug_printf("mmUIWinRefreshScrollBar: ERROR: tig_art_frame_data failed!\n");
            exit(EXIT_FAILURE);
        }

        src_rect.x = 0;
        src_rect.y = curr_window_info->top_index + art_frame_data.height / 2 - curr_window_info->scrollbar_rect.height / 2;
        src_rect.width = art_frame_data.width;
        src_rect.height = curr_window_info->scrollbar_rect.height;

        dst_rect.x = curr_window_info->scrollbar_rect.x;
        dst_rect.y = curr_window_info->scrollbar_rect.y;
        dst_rect.width = art_frame_data.width;
        dst_rect.height = curr_window_info->scrollbar_rect.height;

        blit_info.flags = 0;
        blit_info.art_id = art_id;
        blit_info.src_rect = &src_rect;
        blit_info.dst_rect = &dst_rect;

        tig_window_blit_art(mainmenu_ui_window_handle, &blit_info);
    }
}

// 0x548FF0
void sub_548FF0(int a1)
{
    MainMenuWindowInfo* curr_window_info;

    curr_window_info = main_menu_window_info[mainmenu_ui_window_type];
    if (a1 < 0) {
        a1 = 0;
    } else if (a1 > curr_window_info->max_top_index) {
        a1 = curr_window_info->max_top_index;
    }

    if (curr_window_info->top_index != a1) {
        curr_window_info->top_index = a1;
        mmUIWinRefreshScrollBar();
        if (curr_window_info->refresh_func != NULL) {
            curr_window_info->refresh_func(NULL);
        }
    }
}

// 0x549310
bool sub_549310(tig_button_handle_t button_handle)
{
    if (button_handle != TIG_BUTTON_HANDLE_INVALID) {
        tig_button_show(button_handle);
    }

    if (mainmenu_ui_active) {
        mainmenu_ui_close(false);
        mainmenu_ui_window_type = MM_WINDOW_MAINMENU;
        mainmenu_ui_num_windows = 0;
        mainmenu_ui_type = !dword_5C4000 ? MM_TYPE_1 : MM_TYPE_DEFAULT;
        mainmenu_ui_open();
    } else {
        gameuilib_wants_mainmenu_set();
    }

    if (!gamelib_mod_load(gamelib_default_module_name_get())
        || !gameuilib_mod_load()) {
        tig_debug_printf("MainMenu: Unable to load default module %s.\n",
            gamelib_default_module_name_get());
        exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE.
    }

    mainmenu_ui_reset();

    return true;
}

// 0x5493C0
void sub_5493C0(char* buffer, int size)
{
    MainMenuWindowInfo* curr_window_info;

    if (!dword_64C428) {
        if (mainmenu_ui_textedit_buffer[0] != '\0') {
            strcpy(byte_64C0F0, buffer);
        }

        mainmenu_ui_textedit.size = size;
        mainmenu_ui_textedit.flags = mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME
            ? TEXTEDIT_PATH_SAFE
            : 0;
        mainmenu_ui_textedit.buffer = buffer;
        textedit_ui_focus(&mainmenu_ui_textedit);
        dword_64C428 = true;

        curr_window_info = main_menu_window_info[mainmenu_ui_window_type];
        curr_window_info->refresh_func(&(curr_window_info->content_rect));

        sub_549A40();
    }
}

// 0x549450
void sub_549450(void)
{
    MainMenuWindowInfo* curr_window_info;

    if (dword_64C428) {
        if (mainmenu_ui_textedit.buffer[0] == '\0') {
            strcpy(mainmenu_ui_textedit.buffer, byte_64C0F0);
        }
        textedit_ui_unfocus(&mainmenu_ui_textedit);
        dword_64C428 = false;

        curr_window_info = main_menu_window_info[mainmenu_ui_window_type];
        curr_window_info->refresh_func(&(curr_window_info->content_rect));

        sub_549A50();
    }
}

// 0x5494C0
void mainmenu_ui_textedit_on_enter(TextEdit* textedit)
{
    MesFileEntry mes_file_entry;

    if (textedit->buffer[0] == '\0' && mainmenu_ui_window_type != MM_WINDOW_SAVE_GAME) {
        mes_file_entry.num = 500; // "Choose Name"
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
        strncpy(mainmenu_ui_textedit_buffer, mes_file_entry.str, 23);
    }

    sub_549450();

    if (mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME) {
        mainmenu_ui_save_game_execute(-1);
    }
}

// 0x549520
char* sub_549520(void)
{
    return dword_64C428 != 0 ? mainmenu_ui_textedit.buffer : NULL;
}

// 0x549540
void mainmenu_ui_textedit_on_change(TextEdit* textedit)
{
    MainMenuWindowInfo* curr_window_info;

    (void)textedit;

    curr_window_info = main_menu_window_info[mainmenu_ui_window_type];
    curr_window_info->refresh_func(&(curr_window_info->content_rect));

    if (mainmenu_ui_window_type == MM_WINDOW_SAVE_GAME) {
        scrollbar_ui_control_redraw(stru_64C220);
    }
}

// CE: leave the new-save name input and move list selection. Shared exit step for the
// up/down arrow-at-boundary handlers below. `new_selected` is the row to highlight
// after exiting (clamped by the caller), or -1 to leave nothing selected.
static void mainmenu_ui_textedit_exit_to_row(int new_selected)
{
    MainMenuWindowInfo* window = main_menu_window_info[mainmenu_ui_window_type];

    textedit_ui_unfocus(&mainmenu_ui_textedit);
    dword_64C428 = false;
    mainmenu_ui_textedit_buffer[0] = '\0';

    // Swallow the matching key release so the list nav doesn't move the selection
    // again (the exit happened on press; the release would otherwise leapfrog a row).
    mainmenu_ui_arrow_exit_pending = true;

    // Reset the cached saveinfo so the refresh reloads it for the new selection.
    if (mainmenu_ui_gsi_loaded) {
        gamelib_saveinfo_exit(&mainmenu_ui_gsi);
        mainmenu_ui_gsi_loaded = false;
    }

    window->selected_index = new_selected;
    window->refresh_func(NULL);
    scrollbar_ui_control_redraw(stru_64C220);
}

// CE: DOWN from the Save-name input -> exit the input and select the first existing
// save (row 0 is the "New" input row). No-op outside the Save window.
void mainmenu_ui_textedit_on_arrow_down(TextEdit* textedit)
{
    MainMenuWindowInfo* window;

    (void)textedit;
    if (mainmenu_ui_window_type != MM_WINDOW_SAVE_GAME) {
        return;
    }
    window = main_menu_window_info[mainmenu_ui_window_type];
    // cnt = saves + 1 (row 0 = New). Land on the first save if any, else stay put.
    mainmenu_ui_textedit_exit_to_row(window->cnt > 1 ? 1 : window->selected_index);
}

// CE: UP from a BLANK Save-name input -> exit and wrap to the bottom save. (Non-blank
// up just moves the cursor to the line start, handled in textedit_ui_process_message.)
void mainmenu_ui_textedit_on_arrow_up(TextEdit* textedit)
{
    MainMenuWindowInfo* window;

    (void)textedit;
    if (mainmenu_ui_window_type != MM_WINDOW_SAVE_GAME) {
        return;
    }
    window = main_menu_window_info[mainmenu_ui_window_type];
    mainmenu_ui_textedit_exit_to_row(window->cnt > 1 ? window->cnt - 1 : window->selected_index);
}

// 0x549580
void mainmenu_ui_exit_game(void)
{
    tig_debug_printf("mainmenu_ui_exit_game: Exiting Game!\n");
    gameuilib_mod_unload();
    gamelib_mod_unload();
    gameuilib_exit();
    gamelib_exit();
    tig_exit();
    tig_memory_print_stats(TIG_MEMORY_STATS_PRINT_GROUPED_BLOCKS);
    exit(EXIT_SUCCESS);
}

// 0x5495F0
void mainmenu_ui_progressbar_init(int max_value)
{
    mainmenu_ui_progressbar_max_value = max_value;

    if (mainmenu_ui_active) {
        if (main_menu_window_info[mainmenu_ui_window_type]->refresh_func != NULL) {
            main_menu_window_info[mainmenu_ui_window_type]->refresh_func(&stru_5C4538);
        }
    }
}

// 0x549620
void mainmenu_ui_progressbar_update(int value)
{
    mainmenu_ui_progressbar_value = value;

    if (mainmenu_ui_active) {
        if (main_menu_window_info[mainmenu_ui_window_type]->refresh_func != NULL) {
            main_menu_window_info[mainmenu_ui_window_type]->refresh_func(&stru_5C4538);
        }
    }
}

// 0x5496C0
MainMenuWindowInfo* sub_5496C0(int index)
{
    return main_menu_window_info[index];
}

// 0x5496D0
MainMenuWindowType mainmenu_ui_window_type_get(void)
{
    return mainmenu_ui_window_type;
}

// 0x5496E0
void mainmenu_ui_feedback_saving(void)
{
    mainmenu_ui_feedback(5060); // "Saving..."
}

// 0x5496F0
void mainmenu_ui_feedback(int num)
{
    MesFileEntry mes_file_entry;
    UiMessage ui_message;

    mes_file_entry.num = num;
    if (mes_search(mainmenu_ui_mainmenu_mes_file, &mes_file_entry)) {
        mes_get_msg(mainmenu_ui_mainmenu_mes_file, &mes_file_entry);
        ui_message.type = UI_MSG_TYPE_FEEDBACK;
        ui_message.str = mes_file_entry.str;
        intgame_message_window_display_msg(&ui_message);
        tig_window_display();
    }
}

// 0x549750
void mainmenu_ui_feedback_saving_completed(void)
{
    mainmenu_ui_feedback(5062); // "Save completed."
}

// 0x549760
void mainmenu_ui_feedback_cannot_save_in_tb(void)
{
    mainmenu_ui_feedback(5065); // "Cannot save during turn-based combat when it isn't your turn."
}

// 0x549770
void mainmenu_ui_feedback_loading(void)
{
    mainmenu_ui_feedback(5061); // "Loading..."
}

// 0x549780
void mainmenu_ui_feedback_loading_completed(void)
{
    mainmenu_ui_feedback(5063); // "Load completed."
}

// 0x549820
tig_window_handle_t sub_549820(void)
{
    return mainmenu_ui_window_handle;
}

// 0x549830
void mainmenu_ui_window_type_set(MainMenuWindowType window_type)
{
    mainmenu_ui_window_type = window_type;
}

// 0x549840
mes_file_handle_t mainmenu_ui_mes_file(void)
{
    return mainmenu_ui_mainmenu_mes_file;
}

// 0x549850
void mainmenu_fonts_init(void)
{
    int fnt;
    int clr;
    TigFont font_info;

    font_info.flags = 0;
    font_info.str = NULL;

    for (fnt = 0; fnt < MM_FONT_COUNT; fnt++) {
        tig_art_interface_id_create(mainmenu_font_nums[fnt], 0, 0, 0, &(font_info.art_id));

        for (clr = 0; clr < MM_COLOR_COUNT; clr++) {
            font_info.color = tig_color_make(mainmenu_font_colors[clr][0],
                mainmenu_font_colors[clr][1],
                mainmenu_font_colors[clr][2]);
            tig_font_create(&font_info, &(mainmenu_ui_fonts_tbl[fnt][clr]));
        }
    }
}

// 0x549910
void mainmenu_fonts_exit(void)
{
    int fnt;
    int clr;

    for (fnt = 0; fnt < MM_FONT_COUNT; fnt++) {
        for (clr = 0; clr < MM_COLOR_COUNT; clr++) {
            tig_font_destroy(mainmenu_ui_fonts_tbl[fnt][clr]);
        }
    }
}

// 0x549940
tig_font_handle_t mainmenu_ui_font(MainMenuFont font, MainMenuColor color)
{
    return mainmenu_ui_fonts_tbl[font][color];
}

// 0x549960
void sub_549960(void)
{
    int index;

    for (index = 0; index < main_menu_window_info[mainmenu_ui_window_type]->num_buttons; index++) {
        mainmenu_ui_refresh_button_text(index, 0);
    }
}

// 0x549990
void sub_549990(int* a1, int num)
{
    memcpy(&(mainmenu_ui_window_stack[1]), a1, sizeof(*a1) * num);
    mainmenu_ui_num_windows = num + 1;
}

// 0x549A40
void sub_549A40(void)
{
    dword_64C468++;
}

// 0x549A50
void sub_549A50(void)
{
    dword_64C468--;
}

// 0x549A60
int sub_549A60(void)
{
    return dword_64C468;
}

// 0x549A70
void sub_549A70(void)
{
    dword_5C3620 = false;
}

// 0x549A80
void sub_549A80(void)
{
    int64_t obj;

    if (!dword_5C3620) {
        obj = obj_pool_perm_lookup(obj_get_id(sub_4685A0(BP_VICTORIA_WARRINGTON)));
        if (obj != OBJ_HANDLE_NULL
            && tig_art_exists(obj_field_int32_get(obj, OBJ_F_CURRENT_AID)) == TIG_OK) {
            dword_5C3620 = false;
            mainmenu_ui_pregen_char_cnt = 13;
            mainmenu_ui_new_char_window_info.num_buttons = 10;
        } else {
            dword_5C3620 = true;
        }
    }
}
