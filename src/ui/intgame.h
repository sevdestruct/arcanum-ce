#ifndef ARCANUM_UI_INTGAME_H_
#define ARCANUM_UI_INTGAME_H_

#include "game/context.h"
#include "game/mes.h"
#include "game/target.h"
#include "game/timeevent.h"
#include "game/ui.h"
#include "ui/types.h"

typedef enum PcLensMode {
    PC_LENS_MODE_NONE,
    PC_LENS_MODE_PASSTHROUGH,
    PC_LENS_MODE_BLACKOUT,
} PcLensMode;

typedef struct PcLens {
    tig_window_handle_t window_handle;
    TigRect* rect;
    tig_art_id_t art_id;
} PcLens;

typedef enum IntgameBar {
    INTGAME_BAR_HEALTH,
    INTGAME_BAR_FATIGUE,
    INTGAME_BAR_COUNT,
} IntgameBar;

typedef enum IntgameMode {
    INTGAME_MODE_MAIN,
    INTGAME_MODE_SPELL,
    INTGAME_MODE_SKILL,
    INTGAME_MODE_DIALOG,
    INTGAME_MODE_BARTER,
    INTGAME_MODE_WMAP,
    INTGAME_MODE_SLEEP,
    INTGAME_MODE_LOGBOOK,
    INTGAME_MODE_INVEN,
    INTGAME_MODE_CHAREDIT,
    INTGAME_MODE_LOOT,
    INTGAME_MODE_STEAL,
    INTGAME_MODE_12,
    INTGAME_MODE_QUANTITY,
    INTGAME_MODE_SCHEMATIC,
    INTGAME_MODE_WRITTEN,
    INTGAME_MODE_ITEM,
    INTGAME_MODE_17,
    INTGAME_MODE_FOLLOWER,
    INTGAME_MODE_NPC_IDENTIFY,
    INTGAME_MODE_NPC_REPAIR,
    INTGAME_MODE_COUNT,
} IntgameMode;

typedef enum RotatingWindowType {
    ROTWIN_TYPE_INVALID = -1,
    ROTWIN_TYPE_MSG,
    ROTWIN_TYPE_SPELLS,
    ROTWIN_TYPE_SKILLS,
    ROTWIN_TYPE_CHAT,
    ROTWIN_TYPE_TRAPS,
    ROTWIN_TYPE_DIALOGUE,
    ROTWIN_TYPE_MAP_NOTE,
    ROTWIN_TYPE_BROADCAST,
    ROTWIN_TYPE_MAGICTECH,
    ROTWIN_TYPE_QUANTITY,
    ROTWIN_TYPE_MP_KICKBAN,
    ROTWIN_TYPE_COUNT,
} RotatingWindowType;

extern tig_font_handle_t intgame_morph15_white_font;

bool intgame_init(GameInitInfo* init_info);
void intgame_reset(void);
void intgame_resize(GameResizeInfo* resize_info);
void intgame_exit(void);
bool intgame_save(TigFile* stream);
bool intgame_load(GameLoadInfo* load_info);
void iso_interface_create(tig_window_handle_t window_handle);
void iso_interface_destroy(void);
void sub_54AA30(void);
bool intgame_button_create_ex(tig_window_handle_t window_handle, TigRect* rect, UiButtonInfo* button_info, unsigned int flags);
bool intgame_button_create(UiButtonInfo* button_info);
void intgame_button_destroy(UiButtonInfo* button_info);
void intgame_draw_bar(int bar);
void intgame_draw_bars(void);
void intgame_counters_refresh(void);
bool sub_54B5D0(TigMessage* msg);
void intgame_process_event(TigMessage* msg);
void sub_54EA80(TargetDescriptor* td);
bool intgame_hotkey_is_dragging(void);
void intgame_hotkey_activate(Hotkey* hotkey);
void intgame_hotkey_highlight(Hotkey* hotkey);
void iso_interface_window_set(RotatingWindowType window_type);
void intgame_message_window_clear(void);
void intgame_message_window_display_msg(UiMessage* ui_message);
void intgame_message_window_display_str(int a1, char* str);
void sub_5507D0(void (*func)(UiMessage* ui_message));
void intgame_message_window_display_spell(int spl);
void intgame_message_window_display_college(int college);
void intgame_message_window_display_skill(int value);
void intgame_message_window_clear_internal(void);
void intgame_pc_lens_do(PcLensMode mode, PcLens* pc_lens);
bool intgame_pc_lens_check_pt(int x, int y);
bool intgame_pc_lens_check_pt_unscale(int x, int y);
bool intgame_should_dismiss_overlay_click(int screen_x, int screen_y, const TigRect* menu_rect);

// CE: Snap the iso camera back to the local PC. Used by overlay screens
// that close via a PC-lens click — the lens widget acts as a "back to
// the player" button, so clicking it implies "take me back to the PC"
// even when the recenter-camera-on-overlay setting is off.
void intgame_recenter_on_pc(void);
void intgame_pc_lens_redraw(void);
void iso_interface_refresh(void);
bool sub_5517A0(TigMessage* msg);
bool intgame_get_location_under_cursor(int64_t* loc_ptr);
IntgameMode intgame_mode_get(void);
bool intgame_mode_set(IntgameMode mode);
bool intgame_mode_supports_scrolling(IntgameMode mode);
RotatingWindowType iso_interface_window_get(void);
void iso_interface_window_set_animated(RotatingWindowType window_type);
void intgame_text_edit_refresh(const char* str, tig_font_handle_t font);
void intgame_text_edit_refresh_color(const char* str, tig_font_handle_t font, tig_color_t color, bool a4);
bool intgame_clock_process_callback(TimeEvent* timeevent);
bool intgame_dialog_begin(bool (*func)(TigMessage* msg));
void intgame_dialog_end(void);
void intgame_dialog_clear(void);
void intgame_dialog_set_option(int index, const char* str);
int intgame_dialog_get_option(TigMessage* msg);
RotatingWindowType iso_interface_window_get_2(void);
void intgame_spell_maintain_art_set(int slot, tig_art_id_t art_id);
void intgame_spell_maintain_refresh(int slot, bool active);
void intgame_refresh_cursor(void);
void intgame_item_mode_cursor_set(int art_num);
void intgame_examine_object(int64_t pc_obj, int64_t target_obj, char* str);
bool intgame_examine_portrait(int64_t pc_obj, int64_t target_obj, int* portrait_ptr);
tig_art_id_t sub_554BE0(int64_t obj);
void intgame_message_window_display_attack(int64_t obj);
void intgame_message_window_display_defense(int64_t obj);
void intgame_toggle_primary_button(UiPrimaryButton btn, bool on);
void intgame_set_map_button(UiPrimaryButton btn);
void sub_556E60(void);
void sub_5570A0(int64_t obj);
void intgame_notify_item_inserted_or_removed(int64_t item_obj, bool removed, int inventory_location);
void intgame_refresh_health_bar(int64_t obj);
bool intgame_big_window_lock(TigWindowMessageFilterFunc func, tig_window_handle_t* window_handle_ptr);
void intgame_big_window_unlock(void);
bool intgame_big_window_screen_rect(TigRect* rect);
void intgame_big_window_promote(void);
void intgame_iso_strips_promote(void);
void intgame_iso_strips_hide_full(void);
void intgame_iso_strips_show_as_band(void);
void intgame_iso_world_show(void);
void sub_557370(int64_t source_obj, int64_t target_obj);
void intgame_there_is_nothing_to_loot(void);
void sub_5576B0(void);
void sub_557730(int index);
void sub_557790(int64_t obj);
unsigned int intgame_get_iso_window_flags(void);
void intgame_set_iso_window_flags(unsigned int flags);
void intgame_set_iso_window_width(int width);
void intgame_set_iso_window_height(int height);
bool intgame_create_iso_window(tig_window_handle_t* window_handle_ptr);
bool intgame_is_compact_interface(void);
bool intgame_iso_interface_is_created(void);
void intgame_set_fullscreen(void);
void intgame_toggle_interface(void);
RotatingWindowType iso_interface_window_get_3(void);
int sub_557AB0(void);
void sub_557AC0(int group, int index, UiButtonInfo* button_info);
int64_t sub_557B00(void);
mes_file_handle_t intgame_hotkey_mes_file(void);
UiButtonInfo* intgame_recent_action_button_get(int index);
void intgame_recent_action_button_position_set(int index, int x, int y);
int sub_557B50(int index);
int sub_557B60(void);
int sub_557C00(void);
int sub_557CF0(void);
void intgame_hide(void);
void intgame_show(void);

// CE: handle of the bottom HUD strip window (the one chargen /
// vanilla shell menus repurpose as their chrome band via
// intgame_iso_strips_show_as_band). Callers use this to animate
// the band in/out alongside their own panel chrome so the whole
// composite enters / exits as one. Returns INVALID before the iso
// interface has been created.
tig_window_handle_t intgame_get_band_bar_handle(void);

// CE: true while the bottom strip is currently parked at mid-screen
// as panel chrome (band mode). Used by callers (mainmenu sub_546DD0)
// to decide whether their exit animation should also fade the band
// out as part of the composite.
bool intgame_hud_is_band_mode(void);

// CE: Slide the top + bottom HUD bars off-screen. Used by shell
// menus (Options / Save / Load / ESC pause) that previously hid
// the bars instantly. The bars stay tig-shown; only their position
// is animated. Pair with intgame_hud_slide_show on dismiss.
void intgame_hud_slide_hide(void);
// CE: Slide the bars back to their TAB-stage rest. TAB-cropped
// states (MEDIUM / MINI / HIDDEN) are honored — the bars end up
// where the user left them before the menu opened.
void intgame_hud_slide_show(void);
// CE: cancel any in-flight slide and snap both bars to their
// TAB-rest position. For callers (vanilla band mode, etc.) that
// take over the bar's position directly.
void intgame_hud_slide_reset_to_rest(void);
// CE: seed offsets to off-screen without animating. For level
// load: bars start hidden, then intgame_hud_slide_show animates
// them in.
void intgame_hud_slide_prepare_offscreen(void);

// CE: User-toggleable HUD visibility (TAB key). Hides both iso HUD
// strips (top and bottom bars) by moving them off-screen so their
// message filters keep receiving keyboard / dialog input even while
// the HUD is visually hidden. Distinct from intgame_hide (modal
// hiding); this one tracks its own state so the user can toggle
// on/off at will, and intgame_show re-applies it on modal close.
void intgame_hud_user_toggle(void);
bool intgame_hud_is_user_hidden(void);
// CE: Auto-pop the TAB stage to MEDIUM when a rotwin is invoked
// (K/M/skill/spell shortcut) while the HUD is in MINI or HIDDEN.
// No-op for FULL and MEDIUM stages.
void intgame_hud_auto_pop_for_rotwin(void);
// CE: Restore the stage that was active before intgame_hud_auto_pop_for_rotwin
// last fired (called from iso_interface_window_set when the rotwin
// returns to MSG). No-op when no snapshot is stashed.
void intgame_hud_restore_after_rotwin(void);
// CE: Y-offset of the top HUD strip in design coords. With the
// Phase 1.5 slide tween wired up this returns the smoothly-animated
// position — 41 when the bar is fully at rest (FULL stage), 0 when
// fully hidden (MEDIUM/MINI/HIDDEN), and any value in between
// during a stage transition. fate_ui / sleep_ui ride this value
// every frame via their own pings so they slide with the bar.
int intgame_hud_top_offset(void);

// CE: per-frame integrator hook — applies the spring-tweened bar
// slide offset to the top HUD strip via tig_window_move. Driven by
// the ui_anim tween started in intgame_hud_apply_clips on stage
// changes. Cheap no-op when the offset hasn't moved since last
// invocation, or when the iso interface isn't currently created.
void intgame_hud_ping(void);

// CE: re-promote the top HUD strip to the top of its z-class.
// Called by fate_ui / sleep_ui right after creating their windows
// so their slide-down emerges from BEHIND the top bar (which would
// otherwise sit below the newly-created panel in z-order, leaving
// the panel visible above the bar from frame 1).
void intgame_hud_promote_top_strip(void);
// CE: per-tick hook called BEFORE iso_redraw. Marks the iso VB
// under any HUD strip using the translucent-black tint pathway as
// dirty, so iso_redraw repaints fresh world pixels there. The
// post-iso_redraw companion intgame_hud_tick_apply_tint then darkens
// those pixels. Throttled at non-1.0 zoom (every 3rd tick) to avoid
// hammering the scaled world→iso blit.
void intgame_hud_tick_invalidate_alpha_strips(void);
// CE: per-tick hook called AFTER iso_redraw. Darkens the iso VB
// pixels under any HUD strip using the translucent-black tint
// pathway, so the bar's pre-baked color-key holes show "tinted iso
// world" through them when the compositor draws the bar over iso.
void intgame_hud_tick_apply_tint(void);
// CE: half of the bottom-strip's currently cropped-out height (in design
// coords). 0 when stage is FULL. Used by the dialog options backdrop
// (tc.c) to drop its position down into the freed space when the bar
// is cropped, taking half the visual real-estate that the cropped
// portion of the bar would have occupied.
int intgame_hud_bottom_gap_offset(void);

// CE: design pixels the visible bar's TOP edge has moved DOWN from
// its FULL-stage position (= design y=441 in 600-tall reference
// space). Used by follower_ui (to ride the bar like a child) and
// tb.c (speech bubbles get more room as the bar crops down).
//   FULL    -> 0    (visible top at design y=441)
//   MEDIUM  -> 51   (visible top at design y=492)
//   MINI    -> 122  (visible top at design y=563)
//   HIDDEN  -> 159  (no visible bar)
int intgame_hud_bottom_top_crop(void);

// CE: design-space horizontal extent of the bottom HUD's currently
// visible band, in 800-wide reference coords. Used by tb.c (speech
// bubbles) so bubbles in the freed side areas (e.g. MEDIUM crops
// down to a 410px-wide center band, leaving ~195px on each side
// flush with the iso-world bottom edge) can drop into the bottom
// rows instead of being reserved against a phantom full-width bar.
//   FULL    -> x=0,   w=800
//   MEDIUM  -> x=196, w=410
//   MINI    -> x=205, w=394
//   HIDDEN  -> x=0,   w=0
void intgame_hud_bottom_band_design_x(int* out_x, int* out_w);

// CE: true while either bar's slide-offset is mid-tween toward a new
// target (TAB-HUD stage transitions). Used by tb to inhibit pin-state
// transitions during UI animations — bubbles slide with the moving
// bars rather than flipping between pin choices as the geometry
// briefly satisfies and unsatisfies different fit checks.
bool intgame_hud_is_settling(void);

// CE: current value of the bottom-bar slide-from-bottom offset (design
// pixels). 0 = bar at rest; INTGAME_HUD_BOTTOM_H = bar fully below the
// screen edge. Animated during HIDDEN↔visible transitions and on
// level-load entrance. Read by follower_ui so the toggle/scroller
// chrome rides the bar's entrance/exit slide instead of floating at
// rest while the bar is below the screen.
int intgame_hud_bottom_slide_offset_get(void);
// CE: enable/disable the optional near-black see-through alpha on the
// given window. Gated by the TranslucentBlackUI cfg flag — when off
// (or enable=false), this clears any prior alpha state on the window.
void intgame_apply_translucent_black(tig_window_handle_t window_handle, bool enable);

// CE: re-evaluate which underlay window the modal-dialog auto-tint
// should sample (mainmenu backdrop when mainmenu is up, iso world
// during gameplay, none in pre-game 800x600). Call whenever UI
// context flips: iso interface create/destroy, mainmenu open/close.
void intgame_refresh_modal_tint(void);

// CE: re-evaluate the HUD bar's tint underlay using the same
// context-aware picker. The bar's tint was originally locked in at
// iso_interface_create time and never updated; this lets pre-game
// new-char / pregen / charedit (where the bar is shown as a chrome
// band over the mainmenu backdrop) point the tint at the mainmenu
// bg instead of an unloaded iso world.
void intgame_refresh_hud_bar_tint(void);

#endif /* ARCANUM_UI_INTGAME_H_ */
