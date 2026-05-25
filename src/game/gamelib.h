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
void gamelib_renderlock_acquire(void);
void gamelib_renderlock_release(void);
void gamelib_clear_screen(void);
const char* gamelib_current_module_name_get(void);
void gamelib_current_mode_name_set(const char* name);
bool gamelib_save(const char* name, const char* description);
bool gamelib_load(const char* name);
bool gamelib_delete(const char* name);
const char* gamelib_last_save_name(void);
bool gamelib_in_save(void);
bool gamelib_in_load(void);
void gamelib_set_extra_save_func(GameExtraSaveFunc func);
void gamelib_set_extra_load_func(GameExtraLoadFunc func);
void gamelib_savelist_create(GameSaveList* save_list);
void gamelib_savelist_create_module(const char* module, GameSaveList* save_list);
void gamelib_savelist_destroy(GameSaveList* save_list);
void gamelib_savelist_sort(GameSaveList* save_list, GameSaveListOrder order, bool a3);
bool gamelib_saveinfo_init(const char* name, const char* description, GameSaveInfo* save_info);
void gamelib_saveinfo_exit(GameSaveInfo* save_info);
bool gamelib_saveinfo_save(GameSaveInfo* save_info);
bool gamelib_saveinfo_load(const char* name, GameSaveInfo* save_info);
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
