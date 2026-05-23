#ifndef ARCANUM_GAME_GAMELIB_H_
#define ARCANUM_GAME_GAMELIB_H_

#include <stdint.h>

#include "game/context.h"
#include "game/settings.h"
#include "game/timeevent.h"

#define DIFFICULTY_KEY "difficulty"
#define VIOLENCE_FILTER_KEY "violence filter"
#define TURN_BASED_KEY "turn-based"
#define FAST_TURN_BASED_KEY "fast turn-based"
#define AUTO_ATTACK_KEY "auto attack"
#define AUTO_SWITCH_WEAPON_KEY "auto switch"
#define ALWAYS_RUN_KEY "always run"
#define FOLLOWER_SKILLS_KEY "follower skills"

#define BRIGHTNESS_KEY "brightness"
#define TEXT_DURATION_KEY "text duration"
#define TEXT_FLOATERS_KEY "text floaters"
#define FLOAT_SPEED_KEY "float speed"
#define COMBAT_TAUNTS_KEY "combat taunts"

#define EFFECTS_VOLUME_KEY "effects volume"
#define VOICE_VOLUME_KEY "voice volume"
#define MUSIC_VOLUME_KEY "music volume"

#define SPLASH_KEY "splash"
#define SHOW_VERSION_KEY "show version"
#define OBJECT_LIGHTING_KEY "object lighting"
#define SHADOWS_KEY "shadows"

// CE: blend near-black pixels in the HUD bar / inventory family of
// windows with the game world underneath. Visually opens up dark
// panel regions so the world peeks through. Defaults to 1 (enabled).
#define TRANSLUCENT_BLACK_UI_KEY "translucent black ui"

// CE: master toggle for the ui_anim spring-driven tween system.
// Defaults to 1 (enabled). When 0, ui_anim_* start functions skip the
// spring entirely and apply the end state immediately (and fire any
// on_complete callback synchronously) — useful for accessibility,
// screenshot/debugging, or anyone who prefers snap-instant UIs.
#define UI_ANIMATIONS_KEY "ui animations"

// CE: Baldur's-Gate-style "1. / 2. / ..." number prefixes on dialogue
// options, selectable with the matching number key. The prefix is a
// dim 50% color and never highlights; only the option text after it
// does. Defaults to 1 (on); set 0 for the vanilla unnumbered look.
#define DIALOGUE_OPTION_NUMBERS_KEY "dialogue option numbers"

// CE: dim bracketed emote / stage-direction spans (e.g. "[Sigh]",
// "[The guard eyes you warily]") in dialogue text — speech bubbles
// and option lines — to ~50% of the text color, keeping the brackets
// visible. Sets off in-character action text from spoken words.
// Defaults to 1 (on); set 0 for the vanilla uniform color.
#define DIALOGUE_EMOTE_DIM_KEY "dialogue emote dim"

// CE: how bright the dimmed emote text stays, as a percent of the
// normal color (0 = black, 100 = no dimming). Defaults to 60. Lower
// is dimmer. Only matters when DIALOGUE_EMOTE_DIM_KEY is on.
#define DIALOGUE_EMOTE_DIM_PERCENT_KEY "dialogue emote dim percent"

// CE: also dim bracketed emotes in the player's own dialogue choices
// (not just NPC speech bubbles). Resting choices dim; a rolled-over
// choice always highlights at full color. Defaults to 1 (on). Has no
// effect unless DIALOGUE_EMOTE_DIM_KEY is also on.
#define DIALOGUE_EMOTE_DIM_CHOICES_KEY "dialogue emote dim choices"

// CE: opt-in elliptical-vignette fade-to-black on legacy mainmenu
// panel art (mainmenu / pause / single-player / sub-windows when
// custom-UI bg art isn't loaded). Darkens the corners + edges of
// the 800x600 chrome with a smoothstep ellipse falloff, giving the
// vanilla art a more "cinematic" framed look without modifying the
// underlying BMPs. Defaults to 0 (off) — vanilla appearance
// preserved unless the user opts in via config. No effect when
// custom-UI bg art is loaded (the custom art is already designed
// for the full screen and doesn't need a vignette).
#define LEGACY_MENU_VIGNETTE_KEY "legacy menu vignette"

// When enabled, opening an overlay screen (Logbook, Inventory, Schematic,
// Written, Options-while-in-play, Charedit) snaps the camera back to the
// PC's location before showing the panel. Defaulted off — many players
// find the snap disruptive (you can't open Inventory mid-scroll without
// losing your place). Set to "1" to restore the vanilla behavior.
#define RECENTER_CAMERA_ON_OVERLAY_KEY "recenter camera on overlay"

// When enabled (default), the PC lens widget at the top of overlay screens
// renders the PC's surroundings even when the iso camera has been panned
// away. The lens widget acts as a "back to the player" button, so it
// makes sense for it to always show the player. Set to "0" to fall back
// to vanilla behavior — lens shows whatever is at screen center.
#define PC_LENS_FOLLOWS_PLAYER_KEY "pc lens follows player"

// First-play sound loading mode. Default 1 (async — sound file read
// happens on a detached worker thread, sound starts playing when the
// read completes; eliminates the ~100-150ms first-play hitch every time
// a never-before-heard sound triggers, at the cost of starting playback
// up to one tig_sound_update tick (~100ms) late). Set to 0 to force
// synchronous loads (vanilla behavior; pick this if a thread-safety
// issue surfaces).
#define SOUND_ASYNC_LOAD_KEY "sound async load"

// Renderer vsync mode applied after settings load. Values:
//   2 (default) — vsync ADAPTIVE. Vsync when we hit refresh rate; on
//                 miss, present returns immediately (tears one frame
//                 instead of waiting). Measured ~17% lower frame avg
//                 and ~21% lower stddev vs vsync ON on a 120Hz
//                 ProMotion display, with no perceptible tearing.
//   1           — vsync ON. SDL_RenderPresent blocks until next vblank.
//                 Zero tearing guarantee. Each missed vsync slot costs
//                 a full refresh period (~8.3ms at 120Hz, ~16.6ms at
//                 60Hz). Set this if tearing is noticed and disliked.
//   0           — vsync OFF. No pacing. Maximum throughput, constant
//                 tearing. Mostly useful for benchmarking.
// Set in arcanum.cfg. Reapplied at startup.
#define VSYNC_MODE_KEY "vsync mode"

// When a gold pile's inventory footprint grows past a slot threshold (1->2->4
// cells) and no longer fits where it sits, this controls who has priority for
// the contested cells. Default 0 — the existing items win; the gold pile
// relocates itself to the first free slot that fits. Set to 1 to give the
// growing gold pile priority — it keeps its spot and the items it now overlaps
// are shuffled out to other free slots.
#define GOLD_EXPANSION_SLOT_PRIORITY_KEY "gold expansion slot priority"

// CE: restore the rotating bottom-bar window page (Spells / Skills) that
// was open when the game was saved. The original load read the saved page
// but threw it away, so a load always snapped the window back to the
// default Messages page. Default 0 (vanilla: always reopen on Messages);
// set to 1 to reopen on the saved page. Only the persistent, user-toggled
// pages are restored — transient/context-bound pages (Magick & Tech,
// dialogue, quantity, ...) are left on Messages since their context no
// longer exists after a load.
#define ROTWIN_RESTORE_KEY "restore rotating window"

// CE: feather terrain/cliff edges into the black void the zoomed-out camera
// exposes (vignette at map edges, feather around black off-area facades).
// Default 1; set to 0 for vanilla hard edges (also skips the fade's
// sector-load mask work).
#define VOID_EDGE_FADE_KEY "void edge fade"

// CE (feature/perf-gpu-accel Phase 1): when "1", gamelib_init creates a
// throwaway GPU-backed TigVideoBuffer (256x256 SDL_Texture) and logs the
// outcome. No effect at "0" (default). This is a temporary scaffold to
// verify the new GPU buffer path links and runs end-to-end before any
// real callers exist; remove once Phase 3 ships and the GPU path is
// driven from gamelib_draw_game.
#define GPU_BUFFER_SANITY_CHECK_KEY "gpu buffer sanity check"

typedef bool (*GameExtraSaveFunc)(void);
typedef bool (*GameExtraLoadFunc)(void);

typedef enum GameDifficulty {
    GAME_DIFFICULTY_EASY,
    GAME_DIFFICULTY_NORMAL,
    GAME_DIFFICULTY_HARD,
} GameDifficulty;

typedef struct GameModuleList {
    unsigned int count;
    unsigned int selected;
    char** paths;
} GameModuleList;

typedef struct GameSaveList {
    char* module;
    unsigned int count;
    char** names;
    // CE: per-entry owning module, parallel to names[] (NULL when not tracked,
    // e.g. the plain create()/create_module() lists). Populated by
    // gamelib_savelist_create_all so the Load menu can show a [module] tag and
    // auto-switch to the right module on load. Freed by gamelib_savelist_destroy.
    char** entry_modules;
} GameSaveList;

typedef enum GameSaveListOrder {
    GAME_SAVE_LIST_ORDER_DATE,
    GAME_SAVE_LIST_ORDER_NAME,
} GameSaveListOrder;

typedef struct GameSaveInfo {
    /* 0000 */ int version;
    /* 0004 */ char name[256];
    /* 0104 */ char module_name[256];
    /* 0204 */ int pc_portrait;
    /* 0208 */ int pc_level;
    /* 020C */ int field_20C;
    /* 0210 */ int64_t pc_location;
    /* 0218 */ char description[256];
    /* 0318 */ TigVideoBuffer* thumbnail_video_buffer;
    /* 031C */ int field_31C;
    /* 0320 */ DateTime datetime;
    /* 0328 */ char pc_name[24];
    /* 0340 */ int map;
    /* 0344 */ int field_344;
    /* 0348 */ int field_348;
    /* 034C */ int field_34C;
    /* 0350 */ int field_350;
    /* 0354 */ int field_354;
    /* 0358 */ int field_358;
    /* 035C */ int story_state;
} GameSaveInfo;

extern unsigned int gamelib_ping_time;
extern Settings settings;
extern TigVideoBuffer* gamelib_scratch_video_buffer;

// CE: opt-in fade-to-black post-process. Apply the same vignette /
// side-gradient logic used for the mainmenu legacy chrome to an
// arbitrary video buffer (splash BMP, credits slide, etc.). Gating:
//   - Game render > 800x600: full elliptical vignette.
//   - Vanilla 800x600 + 4:3 physical display: no fade.
//   - Vanilla 800x600 + widescreen display: horizontal-only side
//     gradient (left + right edges fade).
// Caller is responsible for checking LEGACY_MENU_VIGNETTE_KEY and
// whether the user has custom UI art (in which case skip the call).
// One-shot, in-place pixel mutation.
void gamelib_apply_legacy_vignette_to_vb(TigVideoBuffer* vb);

bool gamelib_init(GameInitInfo* init_info);
void gamelib_reset(void);
void gamelib_exit(void);
void gamelib_ping(void);
void gamelib_resize(GameResizeInfo* resize_info);
void gamelib_default_module_name_set(const char* name);
const char* gamelib_default_module_name_get(void);
void gamelib_modlist_create(GameModuleList* module_list, int type);
void gamelib_modlist_destroy(GameModuleList* module_list);
bool gamelib_mod_load(const char* path);
bool gamelib_mod_guid_get(TigGuid* guid_ptr);
void gamelib_mod_unload(void);
bool gamelib_in_reset(void);
int gamelib_cheat_level_get(void);
void gamelib_cheat_level_set(int level);
void gamelib_invalidate_rect(TigRect* rect);
bool gamelib_draw(void);
// CE: render a 1:1 iso view centred on center_obj's sprite into target_vb
// (a width x height region), restoring all render state afterwards. Used
// by the PC lens to show the player even when scrolled/zoomed off the
// main view. Centres on the sprite's bounding box (not just its tile) so
// the head isn't clipped. Must be called outside the main draw. Returns
// false (no render) outside a normal iso game session.
bool gamelib_render_lens_view(TigVideoBuffer* target_vb, int64_t center_obj, int width, int height);
// CE: pixel delta to bring an object's SPRITE CENTRE (not its tile/feet) to the iso
// view centre, for camera_tween_by(). Returns false if the sprite rect is unavailable.
// Lets the PC-lens overlay recenter frame the sprite middle, height-independent.
bool gamelib_sprite_center_screen_delta(int64_t obj, int64_t* dx, int64_t* dy);
void gamelib_renderlock_acquire(void);
void gamelib_renderlock_release(void);
void gamelib_clear_screen(void);
const char* gamelib_current_module_name_get(void);
// CE: the module whose data is actually MOUNTED right now (set by gamelib_mod_load),
// as opposed to gamelib_current_module_name_get() which gamelib_reset clobbers to
// "Arcanum". Use this to decide whether a load needs to switch modules.
const char* gamelib_loaded_module_name_get(void);
void gamelib_current_mode_name_set(const char* name);
bool gamelib_save(const char* name, const char* description);
bool gamelib_load(const char* name);
bool gamelib_delete(const char* name);
const char* gamelib_last_save_name(void);
bool gamelib_in_save(void);
bool gamelib_in_load(void);
// CE: bracket a load (incl. the module switch it performs first) so gameinit_reset
// skips its throwaway fresh-game setup, which otherwise leaks start-map mobile
// state into the loaded save. See gamelib_loading_active in gamelib.c.
void gamelib_loading_active_set(bool active);
bool gamelib_loading_active_get(void);
void gamelib_set_extra_save_func(GameExtraSaveFunc func);
void gamelib_set_extra_load_func(GameExtraLoadFunc func);
void gamelib_savelist_create(GameSaveList* save_list);
void gamelib_savelist_create_module(const char* module, GameSaveList* save_list);
// CE: aggregate saves across data\Save (legacy/default location) AND every
// modules\<M>\save folder into one list, tagging each entry with its owning
// module (data\ -> default module; modules\<M>\ -> M, directory-authoritative).
void gamelib_savelist_create_all(GameSaveList* save_list);
// CE: tag an existing list's entries with their owning module (for the Save menu,
// which keeps its current-context list but wants the same [module] labels as Load).
void gamelib_savelist_tag_modules(GameSaveList* save_list);
// CE: resolve which module a save belongs to by WHERE it lives on disk: a save
// under modules\<M>\save is module M (authoritative); otherwise it is the default
// module (data\Save). Returns false if no such save exists. out_module must hold
// at least TIG_MAX_PATH. This is the directory-based module detection used to
// auto-switch on load, independent of the (possibly wrong) .gsi module stamp.
bool gamelib_find_save_module(const char* name, char* out_module, size_t out_size);
// CE: load saveinfo from just the 8-char slot (globs <slot>*.gsi to recover the
// full <slot><description> base name). The save's module must be mounted first.
bool gamelib_saveinfo_load_by_slot(const char* slot, GameSaveInfo* save_info);
void gamelib_savelist_destroy(GameSaveList* save_list);
void gamelib_savelist_sort(GameSaveList* save_list, GameSaveListOrder order, bool a3);
bool gamelib_saveinfo_init(const char* name, const char* description, GameSaveInfo* save_info);
void gamelib_saveinfo_exit(GameSaveInfo* save_info);
bool gamelib_saveinfo_save(GameSaveInfo* save_info);
bool gamelib_saveinfo_load(const char* name, GameSaveInfo* save_info);
// CE: like gamelib_saveinfo_load but locates the save's directory first (any module
// folder, else data\Save), so a cross-module save previews correctly regardless of
// which module is currently mounted.
bool gamelib_saveinfo_load_located(const char* name, GameSaveInfo* save_info);
void gamelib_thumbnail_size_set(int width, int height);
int gamelib_game_difficulty_get(void);
void gamelib_redraw(void);
bool gamelib_copy_version(char* long_version, char* short_version, char* locale);
void gamelib_patch_lvl_set(const char* patch_lvl);
const char* gamelib_get_locale(void);
void gamelib_get_iso_content_rect(TigRect* rect);

// True when the user has opted into the vanilla "snap camera to PC on
// overlay open" behavior. See RECENTER_CAMERA_ON_OVERLAY_KEY. Defaults
// to false.
bool gamelib_recenter_camera_on_overlay(void);

// True when the PC lens widget should track the player even when the
// iso camera has been panned away. See PC_LENS_FOLLOWS_PLAYER_KEY.
// Defaults to true.
bool gamelib_pc_lens_follows_player(void);

// CE: when a growing gold pile no longer fits its slot, whether the pile has
// priority — keep its spot and push overlapped neighbours out to free slots
// (true) — or yields and relocates itself (false, default). See
// GOLD_EXPANSION_SLOT_PRIORITY_KEY.
bool gamelib_gold_expansion_slot_priority(void);

// CE: whether the void-edge fade is enabled. See VOID_EDGE_FADE_KEY.
bool gamelib_void_edge_fade(void);

// Debug perf counter for the zoom-out draw path. When enabled, gamelib
// times each zoom-active frame (gamelib_draw_func + downscale blit) and
// dumps a rolling summary every 60 frames via tig_debug_printf so we
// can compare before/after partial-redraw work. Off by default.
void gamelib_zoom_perf_toggle(void);
bool gamelib_zoom_perf_is_enabled(void);

// Monotonic ns clock for caller-side timing. Cheap (clock_gettime).
// Pair with gamelib_perf_record_* helpers to attribute time to a bucket.
uint64_t gamelib_perf_now_ns(void);

// Per-main-loop-step accumulators. Each one buckets time spent in a
// specific call between consecutive gamelib_draw fires. Only meaningful
// while gamelib_zoom_perf_is_enabled() — callers should gate sampling.
void gamelib_perf_record_tig_ping_ns(uint64_t ns);
void gamelib_perf_record_iso_redraw_ns(uint64_t ns);
void gamelib_perf_record_window_display_ns(uint64_t ns);
void gamelib_perf_record_key_repeat_ns(uint64_t ns);
void gamelib_perf_record_event_dispatch_ns(uint64_t ns);

// Complement to the per-bucket megahitch logger. Called from main.c
// after each loop iteration with the iteration-total time and the
// per-bucket breakdown (in ns). If the total exceeds the slow-loop
// threshold (~50ms — a cumulative miss of 3+ vsync slots on 120Hz)
// AND no single bucket already tripped the existing megahitch logger,
// emits one log line attributing the cost. Useful for the case where
// no individual call is slow but the iteration adds up.
void gamelib_perf_record_loop_iteration_ns(uint64_t total_ns,
    uint64_t tig_ping_ns, uint64_t key_repeat_ns,
    uint64_t iso_redraw_ns, uint64_t win_display_ns,
    uint64_t event_dispatch_ns);

// Single message-handler-step timing inside the inner event dispatch
// loop. Lets us attribute event_dispatch megahitches to a specific
// message (e.g. F8 quickload, mouse click on worldmap travel arrow,
// menu close). Threshold matches the per-bucket megahitch logger
// (100ms). `context` is a free-form short description — typically
// "msg=KEYBOARD scancode=N up/down" or "msg=type=N".
void gamelib_perf_log_event(const char* context, uint64_t ns);

#endif /* ARCANUM_GAME_GAMELIB_H_ */
