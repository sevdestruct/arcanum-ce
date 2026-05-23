#include "game/gamelib.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tig/font.h"

#include "game/camera_follow.h"
#include "game/camera_tween.h"
#include "game/dialog_camera.h"
#include "game/iso_zoom.h"
#include "game/location.h"
#include "game/tile.h"
#include "ui/anim_ui.h"
#include "ui/fate_ui.h"
#include "ui/follower_ui.h"
#include "ui/intgame.h"
#include "ui/sleep_ui.h"
#include "ui/ui_anim.h"

#include "game/ai.h"
#include "game/anim.h"
#include "game/anim_private.h"
#include "game/animfx.h"
#include "game/antiteleport.h"
#include "game/area.h"
#include "game/background.h"
#include "game/bless.h"
#include "game/brightness.h"
#include "game/broadcast.h"
#include "game/ci.h"
#include "game/critter.h"
#include "game/curse.h"
#include "game/description.h"
#include "game/dialog.h"
#include "game/facade.h"
#include "game/gameinit.h"
#include "game/gfade.h"
#include "game/gmovie.h"
#include "game/gsound.h"
#include "game/highres_config.h"
#include "game/hrp.h"
#include "game/invensource.h"
#include "game/item.h"
#include "game/item_effect.h"
#include "game/jumppoint.h"
#include "game/level.h"
#include "game/li.h"
#include "game/light.h"
#include "game/light_scheme.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/mes.h"
#include "game/monstergen.h"
#include "game/mt_ai.h"
#include "game/mt_item.h"
#include "game/mt_obj_node.h"
#include "game/multiplayer.h"
#include "game/name.h"
#include "game/newspaper.h"
#include "game/party.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/quest.h"
#include "game/random.h"
#include "game/reaction.h"
#include "game/reputation.h"
#include "game/roof.h"
#include "game/rumor.h"
#include "game/script.h"
#include "game/script_name.h"
#include "game/sector.h"
#include "game/sector_script.h"
#include "game/skill.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/void_edge_fade.h"
#include "game/tb.h"
#include "game/tc.h"
#include "game/tech.h"
#include "game/teleport.h"
#include "game/tf.h"
#include "game/tile.h"
#include "game/tile_block.h"
#include "game/tile_script.h"
#include "game/timeevent.h"
#include "game/townmap.h"
#include "game/trap.h"
#include "game/ui.h"
#include "game/wall.h"
#include "game/wallcheck.h"
#include "game/wp.h"

#define GAMELIB_LONG_VERSION_LENGTH 40
#define GAMELIB_SHORT_VERSION_LENGTH 36
#define GAMELIB_LOCALE_LENGTH 4
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 7
#define VERSION_BUILD 4

#define GAMELIB_MAX_PATCH_COUNT 10

typedef bool (*GameInitFunc)(GameInitInfo* init_info);
typedef void (*GameResetFunc)(void);
typedef bool (*GameModuleLoadFunc)(void);
typedef void (*GameModuleUnloadFunc)(void);
typedef void (*GameExitFunc)(void);
typedef void (*GamePingFunc)(unsigned int time);
typedef void (*GameUpdateViewFunc)(ViewOptions* view_options);
typedef bool (*GameSaveFunc)(TigFile* stream);
typedef bool (*GameLoadFunc)(GameLoadInfo* load_info);
typedef void (*GameResizeFunc)(GameResizeInfo* resize_info);

typedef struct GameLibModule {
    const char* name;
    GameInitFunc init_func;
    GameResetFunc reset_func;
    GameModuleLoadFunc mod_load_func;
    GameModuleUnloadFunc mod_unload_func;
    GameExitFunc exit_func;
    GamePingFunc ping_func;
    GameUpdateViewFunc update_view_func;
    GameSaveFunc save_func;
    GameLoadFunc load_func;
    GameResizeFunc resize_func;
} GameLibModule;

typedef struct GameSaveEntry {
    time_t modify_time;
    char* path;
    // CE: owning module for this entry, carried through the sort so it stays
    // aligned with the name when entries are reordered (NULL when untracked).
    char* module;
} GameSaveEntry;

static int game_save_entry_compare_by_date(const void* va, const void* vb);
static int game_save_entry_compare_by_name(const void* va, const void* vb);
static void difficulty_changed(void);
static void dialogue_emote_dim_changed(void);
static void gamelib_draw_game(GameDrawInfo* draw_info);
static void gamelib_draw_editor(GameDrawInfo* draw_info);
static uint64_t gamelib_zoom_perf_now_ns(void);
static void gamelib_zoom_perf_log(const char* line);
static void gamelib_logo(void);
static void gamelib_splash(tig_window_handle_t window_handle);
static void gamelib_load_data(void);
static bool gamelib_load_module_data(const char* module_name);
static void gamelib_unload_module_data(void);

// 0x59A330
static GameLibModule gamelib_modules[] = {
    { "HighResolution", hrp_init, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, hrp_resize },
    { "Description", description_init, NULL, description_mod_load, description_mod_unload, description_exit, NULL, NULL, NULL, NULL, NULL },
    { "Item-Effect", item_effect_init, NULL, item_effect_mod_load, item_effect_mod_unload, item_effect_exit, NULL, NULL, NULL, NULL, NULL },
    { "Teleport", teleport_init, teleport_reset, NULL, NULL, teleport_exit, teleport_ping, NULL, NULL, NULL, NULL },
    { "Sector", sector_history_init, sector_history_reset, NULL, NULL, sector_history_exit, NULL, NULL, sector_history_save, sector_history_load, NULL },
    { "Random", random_init, NULL, NULL, NULL, random_exit, NULL, NULL, NULL, NULL, NULL },
    { "Critter", critter_init, NULL, NULL, NULL, critter_exit, NULL, NULL, NULL, NULL, NULL },
    { "Name", name_init, NULL, NULL, NULL, name_exit, NULL, NULL, NULL, NULL, NULL },
    { "ScriptName", script_name_init, NULL, script_name_mod_load, script_name_mod_unload, script_name_exit, NULL, NULL, NULL, NULL, NULL },
    { "Portait", portrait_init, NULL, NULL, NULL, portrait_exit, NULL, NULL, NULL, NULL, NULL },
    { "AnimFX", animfx_init, NULL, NULL, NULL, animfx_exit, NULL, NULL, NULL, NULL, NULL },
    { "Script", script_init, script_reset, script_mod_load, script_mod_unload, script_exit, NULL, NULL, script_save, script_load, NULL },
    { "MagicTech", magictech_init, magictech_reset, NULL, NULL, magictech_exit, NULL, NULL, NULL, NULL, NULL },
    { "MT-AT", mt_ai_init, mt_ai_reset, NULL, NULL, mt_ai_exit, NULL, NULL, NULL, NULL, NULL },
    { "MT-Item", mt_item_init, NULL, NULL, NULL, mt_item_exit, NULL, NULL, NULL, NULL, NULL },
    { "Spell", spell_init, NULL, NULL, NULL, spell_exit, NULL, NULL, NULL, NULL, NULL },
    { "Stat", stat_init, NULL, NULL, NULL, stat_exit, NULL, NULL, NULL, NULL, NULL },
    { "Level", level_init, NULL, NULL, NULL, level_exit, NULL, NULL, NULL, NULL, NULL },
    { "Map", map_init, map_reset, map_mod_load, map_mod_unload, map_exit, map_ping, map_update_view, map_save, map_load, map_resize },
    { "LightScheme", light_scheme_init, light_scheme_reset, light_scheme_mod_load, light_scheme_mod_unload, light_scheme_exit, NULL, NULL, light_scheme_save, light_scheme_load, NULL },
    { "MagicTech-Post", magictech_post_init, NULL, NULL, NULL, NULL, NULL, NULL, magictech_post_save, magictech_post_load, NULL },
    { "Player", player_init, player_reset, 0, NULL, player_exit, NULL, NULL, player_save, player_load, NULL },
    { "Area", area_init, area_reset, area_mod_load, area_mod_unload, area_exit, NULL, NULL, area_save, area_load, NULL },
    { "Facade", facade_init, NULL, NULL, NULL, facade_exit, NULL, facade_update_view, NULL, NULL, facade_resize },
    { "TC", tc_init, NULL, NULL, NULL, tc_exit, NULL, NULL, NULL, NULL, tc_resize },
    { "Dialog", dialog_init, NULL, NULL, NULL, dialog_exit, NULL, NULL, NULL, NULL, NULL },
    { "Skill", skill_init, NULL, NULL, NULL, skill_exit, NULL, NULL, skill_save, skill_load, NULL },
    { "SoundGame", gsound_init, gsound_reset, gsound_mod_load, gsound_mod_unload, gsound_exit, gsound_ping, NULL, gsound_save, gsound_load, NULL },
    { "Item", item_init, NULL, NULL, NULL, item_exit, NULL, NULL, NULL, NULL, item_resize },
    { "Combat", combat_init, combat_reset, 0, NULL, combat_exit, NULL, NULL, combat_save, combat_load, NULL },
    { "TimeEvent", timeevent_init, timeevent_reset, NULL, NULL, timeevent_exit, timeevent_ping, NULL, timeevent_save, timeevent_load, NULL },
    { "Rumor", rumor_init, rumor_reset, rumor_mod_load, rumor_mod_unload, rumor_exit, NULL, NULL, rumor_save, rumor_load, NULL },
    { "Quest", quest_init, quest_reset, quest_mod_load, quest_mod_unload, quest_exit, NULL, NULL, quest_save, quest_load, NULL },
    { "Bless", NULL, NULL, bless_mod_load, bless_mod_unload, NULL, NULL, NULL, NULL, NULL, NULL },
    { "Curse", NULL, NULL, curse_mod_load, curse_mod_unload, NULL, NULL, NULL, NULL, NULL, NULL },
    { "AI", ai_init, NULL, ai_mod_load, ai_mod_unload, ai_exit, NULL, NULL, NULL, NULL, NULL },
    { "Broadcast", broadcast_init, NULL, NULL, NULL, broadcast_exit, NULL, NULL, NULL, NULL, NULL },
    { "Anim", anim_init, anim_reset, NULL, NULL, anim_exit, NULL, NULL, anim_save, anim_load, NULL },
    { "Anim-Private", anim_private_init, anim_private_reset, NULL, NULL, anim_private_exit, NULL, NULL, NULL, NULL, NULL },
    { "Multiplayer", multiplayer_init, multiplayer_reset, multiplayer_mod_load, multiplayer_mod_unload, multiplayer_exit, multiplayer_ping, NULL, multiplayer_save, mutliplayer_load, NULL },
    { "Tech", tech_init, NULL, NULL, NULL, tech_exit, NULL, NULL, NULL, NULL, NULL },
    { "Background", background_init, NULL, NULL, NULL, background_exit, NULL, NULL, NULL, NULL, NULL },
    { "Reputation", reputation_init, NULL, reputation_mod_load, reputation_mod_unload, reputation_exit, NULL, 0, NULL, NULL, NULL },
    { "Reaction", reaction_init, NULL, NULL, NULL, reaction_exit, NULL, NULL, NULL, NULL, NULL },
    { "Tile-Script", tile_script_init, tile_script_reset, NULL, NULL, tile_script_exit, 0, tile_script_update_view, NULL, NULL, tile_script_resize },
    { "Sector-Script", sector_script_init, sector_script_reset, NULL, NULL, sector_script_exit, NULL, NULL, NULL, NULL, NULL },
    { "WP", wp_init, NULL, NULL, NULL, wp_exit, NULL, wp_update_view, NULL, NULL, wp_resize },
    { "Inven-Source", invensource_init, NULL, NULL, NULL, invensource_exit, NULL, NULL, NULL, NULL, NULL },
    { "Newspaper", newspaper_init, newspaper_reset, NULL, NULL, newspaper_exit, NULL, NULL, newspaper_save, newspaper_load, NULL },
    { "TownMap", NULL, townmap_reset, townmap_mod_load, townmap_mod_unload, NULL, NULL, NULL, NULL, NULL, NULL },
    { "GMovie", NULL, NULL, gmovie_mod_load, gmovie_mod_unload, NULL, NULL, NULL, NULL, NULL, NULL },
    { "Brightness", brightness_init, NULL, NULL, NULL, brightness_exit, NULL, NULL, NULL, NULL, NULL },
    { "GFade", gfade_init, NULL, NULL, NULL, gfade_exit, NULL, NULL, NULL, NULL, gfade_resize },
    { "Anti-Teleport", antiteleport_init, NULL, antiteleport_mod_load, antiteleport_mod_unload, antiteleport_exit, NULL, NULL, NULL, NULL, NULL },
    { "Trap", trap_init, NULL, NULL, NULL, trap_exit, NULL, NULL, NULL, NULL, NULL },
    { "WallCheck", wallcheck_init, wallcheck_reset, NULL, NULL, wallcheck_exit, wallcheck_ping, NULL, NULL, NULL, NULL },
    { "LI", li_init, NULL, NULL, NULL, li_exit, NULL, NULL, NULL, NULL, li_resize },
    { "CI", ci_init, NULL, NULL, NULL, ci_exit, NULL, NULL, NULL, NULL, NULL },
    { "TileBlock", tileblock_init, NULL, NULL, NULL, tileblock_exit, NULL, tileblock_update_view, NULL, NULL, tileblock_resize },
    { "MT-Obj-Node", mt_obj_node_init, NULL, NULL, NULL, mt_obj_node_exit, NULL, NULL, NULL, NULL, NULL },
    { "MonsterGen", monstergen_init, monstergen_reset, NULL, NULL, monstergen_exit, NULL, NULL, monstergen_save, monstergen_load, monstergen_resize },
    { "Party", party_init, party_reset, NULL, 0, party_exit, NULL, NULL, NULL, NULL, NULL },
    { "gameinit", gameinit_init, gameinit_reset, gameinit_mod_load, gameinit_mod_unload, gameinit_exit, NULL, NULL, NULL, NULL, NULL },
};

#define MODULE_COUNT ((int)SDL_arraysize(gamelib_modules))

// 0x59ADD8
static int gamelib_renderlock_cnt = 1;

// 0x59ADDC
static char gamelib_current_module_name[TIG_MAX_PATH] = "Arcanum";

// 0x59AEDC
static char gamelib_default_module_name[TIG_MAX_PATH] = "Arcanum";

// 0x5CFF08
static char gamelib_mod_dir_path[TIG_MAX_PATH];

// 0x5D000C
static TigRectListNode* gamelib_pending_dirty_rects_head;

// 0x5D0010
static bool gamelib_dirty;

// 0x5D0018
static TigRect gamelib_iso_content_rect;

// 0x5D0028
static char gamelib_mod_patch_paths[GAMELIB_MAX_PATCH_COUNT][TIG_MAX_PATH];

// FIXME: Should be of TIG_MAX_PATH (260), not 256.
//
// 0x5D0A50
static char byte_5D0A50[256];

// 0x5D0B50
static ViewOptions gamelib_view_options;

// 0x5D0B58
static char byte_5D0B58[TIG_MAX_PATH];

// 0x5D0D60
static TigRect gamelib_iso_content_rect_ex;

// 0x5D0D74
static bool in_draw;

// 0x5D0D78
static int gamelib_window_rect_x;

// 0x5D0D7C
static int gamelib_window_rect_y;

// 0x5D0D80
static int gamelib_thumbnail_height;

// 0x5D0E88
static GameInitInfo gamelib_init_info;

// 0x5D0E98
static TigRectListNode* gamelib_dirty_rects_head;

// 0x5D0E9C
static int gamelib_game_difficulty;

// 0x5D0EA0
static bool gamelib_mod_loaded;

// 0x5D0EA4
static char byte_5D0EA4[TIG_MAX_PATH];

// 0x5D0FA8
static char gamelib_mod_dat_path[TIG_MAX_PATH];

// CE: per-module custom override dir (custom\modules\<name>) currently mounted.
static char gamelib_mod_custom_path[TIG_MAX_PATH];

// 0x5D10AC
static void (*gamelib_draw_func)(GameDrawInfo* draw_info);

// 0x5D10B0
static TigGuid gamelib_mod_guid;

// 0x5D10C0
static int gamelib_thumbnail_width;

// 0x5D10C4
static bool dword_5D10C4;

// 0x5D10D4
static GameExtraSaveFunc gamelib_extra_save_func;

// 0x5D10D8
static GameExtraLoadFunc gamelib_extra_load_func;

// 0x5D10DC
static bool gamelib_savelist_sort_check_autosave;

// 0x5D10E0
static bool in_save;

// 0x5D10E4
static bool in_load;

// CE: set by the load UI around a load-driven module switch. While true,
// gameinit_reset skips its fresh-game setup (opening the module start map +
// player_create). That fresh game is immediately discarded by the load anyway, and
// -- critically -- the start map it opens gets flushed to Save\Current on the next
// map_close, leaving stale start-map mobile state that merges into the loaded save
// (a leftover NPC "from another save"). Distinct from in_load, which gamelib_load
// only sets AFTER the switch has already happened.
static bool gamelib_loading_active;

// 0x5D10E8
static bool in_reset;

// 0x5D10EC
static int gamelib_cheat_level;

// 0x739E60
unsigned int gamelib_ping_time;

// 0x739E70
Settings settings;

// 0x739E7C
TigVideoBuffer* gamelib_scratch_video_buffer;

static TigVideoBuffer* gamelib_world_video_buffer = NULL;
static TigVideoBuffer* gamelib_iso_window_vb = NULL;
static bool gamelib_zoom_world_pass_active = false;

// Zoom-out draw perf counter. See gamelib_zoom_perf_toggle().
static bool gamelib_zoom_perf_enabled = false;
static int gamelib_zoom_perf_frames = 0;
static int gamelib_zoom_perf_full_frames = 0;
// Switched to ns precision so we can compute zoom_total - render - blit
// = other-work (setup/teardown) without losing precision.
static uint64_t gamelib_zoom_perf_total_render_ns = 0;
static uint64_t gamelib_zoom_perf_total_blit_ns = 0;
static int64_t gamelib_zoom_perf_total_dirty_px = 0;
static float gamelib_zoom_perf_last_z = 1.0f;
// High-precision wall-clock frame-interval tracker. Captures TOTAL
// frame cost (render + AI + script + present + everything else
// between draws), not just our render bucket. Reports avg, max, and
// stddev so we can see jitter/spikes that aren't visible in averages.
static uint64_t gamelib_zoom_perf_last_ns = 0;
static uint64_t gamelib_zoom_perf_total_frame_ns = 0;
static uint64_t gamelib_zoom_perf_max_frame_ns = 0;
static double gamelib_zoom_perf_sum_sq_frame_ms = 0.0;
static int gamelib_zoom_perf_frame_ns_samples = 0;
// Per-frame breakdown of the zoom-active path. zoom_total = start of
// zoom-active processing through end of blit; other = total - render
// - blit (setup/teardown bucket).
static uint64_t gamelib_zoom_perf_total_zoom_ns = 0;
static uint64_t gamelib_zoom_perf_max_zoom_ns = 0;
static uint64_t gamelib_zoom_perf_total_other_ns = 0;
static uint64_t gamelib_zoom_perf_max_other_ns = 0;
// Per-subsystem ping-time bucket, accumulated in gamelib_ping. Only sums
// while gamelib_zoom_perf_enabled is on. Lets the perf log surface which
// subsystem is eating the gap between zoom-active render time and total
// frame time (typically 15-30ms outside the zoom path).
#define GAMELIB_PERF_MAX_MODULES 64
static uint64_t gamelib_zoom_perf_ping_module_total_ns[GAMELIB_PERF_MAX_MODULES];
static uint64_t gamelib_zoom_perf_ping_module_max_ns[GAMELIB_PERF_MAX_MODULES];
static uint64_t gamelib_zoom_perf_ping_total_ns = 0;
static uint64_t gamelib_zoom_perf_ping_max_ns = 0;
static int gamelib_zoom_perf_ping_samples = 0;
// Main-loop bucket accumulators. Recorded by main.c around each call so
// we can attribute the inter-frame gap (which gamelib_ping data showed
// is ~0.1ms) to the actual culprits — tig_ping subsystems, render
// dispatch, or window present / vsync.
static uint64_t gamelib_zoom_perf_tig_ping_total_ns = 0;
static uint64_t gamelib_zoom_perf_tig_ping_max_ns = 0;
static int gamelib_zoom_perf_tig_ping_samples = 0;
static uint64_t gamelib_zoom_perf_iso_redraw_total_ns = 0;
static uint64_t gamelib_zoom_perf_iso_redraw_max_ns = 0;
static int gamelib_zoom_perf_iso_redraw_samples = 0;
static uint64_t gamelib_zoom_perf_window_display_total_ns = 0;
static uint64_t gamelib_zoom_perf_window_display_max_ns = 0;
static int gamelib_zoom_perf_window_display_samples = 0;
static uint64_t gamelib_zoom_perf_key_repeat_total_ns = 0;
static uint64_t gamelib_zoom_perf_key_repeat_max_ns = 0;
static int gamelib_zoom_perf_key_repeat_samples = 0;
static uint64_t gamelib_zoom_perf_event_dispatch_total_ns = 0;
static uint64_t gamelib_zoom_perf_event_dispatch_max_ns = 0;
static int gamelib_zoom_perf_event_dispatch_samples = 0;
// First few main-loop iterations after F9-toggle-on always show cold-cache
// outliers: 108ms tig_ping, 47ms object_max, etc. — perf counters warming
// up, CPU caches cold, accumulators allocating. Skip those samples so they
// don't pollute worst-case numbers. Driven by the first record-function
// called per loop iteration (tig_ping).
#define GAMELIB_PERF_WARMUP_ITERATIONS 2
static int gamelib_zoom_perf_warmup_count = 0;
static bool gamelib_zoom_perf_warmed_up = false;
// Per-render-pass accumulators. iso_redraw is dominated by gamelib_draw_game
// which calls light/tile/object/roof in sequence. Breaking out each pass
// lets us identify which one drives the heavy-frame (10-20ms iso_redraw)
// spikes during scroll-at-zoom-out.
static uint64_t gamelib_zoom_perf_pass_light_total_ns = 0;
static uint64_t gamelib_zoom_perf_pass_light_max_ns = 0;
static uint64_t gamelib_zoom_perf_pass_tile_total_ns = 0;
static uint64_t gamelib_zoom_perf_pass_tile_max_ns = 0;
static uint64_t gamelib_zoom_perf_pass_object_total_ns = 0;
static uint64_t gamelib_zoom_perf_pass_object_max_ns = 0;
static uint64_t gamelib_zoom_perf_pass_roof_total_ns = 0;
static uint64_t gamelib_zoom_perf_pass_roof_max_ns = 0;
static int gamelib_zoom_perf_pass_samples = 0;
#define GAMELIB_ZOOM_PERF_INTERVAL 60

// 0x4020F0
bool gamelib_init(GameInitInfo* init_info)
{
    char version[40];
    TigWindowData window_data;
    TigVideoBufferCreateInfo vb_create_info;
    int index;

    gamelib_copy_version(version, NULL, NULL);
    tig_debug_printf("\n%s\n", version);

    if (init_info->editor) {
        settings_init(&settings, "worlded.cfg");
    } else {
        settings_init(&settings, "arcanum.cfg");
    }

    settings_load(&settings);

    // CE: prune deprecated keys left over in old arcanum.cfg files so they
    // don't linger. "translucent black method" selected between a snapshot
    // and a realtime tint path; the snapshot path was removed, leaving only
    // one method, so the toggle no longer means anything.
    settings_remove(&settings, "translucent black method");

    settings_register(&settings, DIFFICULTY_KEY, "1", difficulty_changed);
    difficulty_changed();

    // CE: optional translucent-black effect on the HUD bar (and any
    // other window that opts in via intgame_apply_translucent_black).
    // Dialog-backdrop-style tint: pre-bake the bar's near-black pixels
    // to color-key transparency, per-tick subtract-tint the iso pixels
    // under the bar, plain hardware color-key blit in compositor.
    settings_register(&settings, TRANSLUCENT_BLACK_UI_KEY, "1", NULL);

    // CE: master toggle for the ui_anim spring-driven UI animation
    // system. Defaults on. Disable for instant-snap UIs (accessibility,
    // screenshots, personal preference).
    settings_register(&settings, UI_ANIMATIONS_KEY, "1", NULL);

    // CE: numbered dialogue options (default on).
    settings_register(&settings, DIALOGUE_OPTION_NUMBERS_KEY, "1", NULL);

    // CE: dim bracketed emote spans in dialogue (default on, 50%).
    // Register both keys, then mirror their values into the tig font
    // layer; either changing re-applies both.
    settings_register(&settings, DIALOGUE_EMOTE_DIM_KEY, "1", dialogue_emote_dim_changed);
    settings_register(&settings, DIALOGUE_EMOTE_DIM_PERCENT_KEY, "60", dialogue_emote_dim_changed);
    settings_register(&settings, DIALOGUE_EMOTE_DIM_CHOICES_KEY, "1", dialogue_emote_dim_changed);
    dialogue_emote_dim_changed();

    // CE: Opt-in for vanilla "snap camera to PC on overlay open". Default
    // off — opening Inventory / Logbook / Schematic / Written / Options
    // (in-play) / Charedit no longer yanks the view back to the player.
    settings_register(&settings, RECENTER_CAMERA_ON_OVERLAY_KEY, "0", NULL);

    // CE: PC lens widget tracks the player even when the camera is panned.
    // Defaults on — the lens is logically a "back to the player" button so
    // it should always show the player. Set to "0" for vanilla behavior
    // (lens copies whatever is at screen center).
    settings_register(&settings, PC_LENS_FOLLOWS_PLAYER_KEY, "1", NULL);

    // CE: who has priority for the cells a growing gold pile needs. Default
    // "0" — existing items win and the gold pile relocates itself. Set to "1"
    // to give the pile priority: it keeps its spot and the items it overlaps
    // are shuffled out to other free slots.
    settings_register(&settings, GOLD_EXPANSION_SLOT_PRIORITY_KEY, "0", NULL);

    // CE: restore the rotating bottom-bar window page (Spells / Skills) that
    // was open at save time instead of always reopening on the default
    // Messages page. Default off. See ROTWIN_RESTORE_KEY.
    settings_register(&settings, ROTWIN_RESTORE_KEY, "0", NULL);

    // CE: void-edge fade — feather terrain/cliff edges into the black void
    // exposed by the zoomed-out camera (vignette at map edges, feather around
    // black off-area facades). Default on; set to "0" for vanilla hard edges.
    // Adds a small amount of sector-load work while enabled.
    settings_register(&settings, VOID_EDGE_FADE_KEY, "1", void_edge_fade_settings_changed);

    // CE: Renderer vsync mode. Default 2 (adaptive) — measured ~17%
    // lower frame avg and ~21% lower stddev vs vanilla vsync on a 120Hz
    // ProMotion display, with no perceptible tearing during normal
    // gameplay. 1 = vsync on (zero tearing guarantee, the safer choice
    // if tearing bothers you). 0 = off (uncapped, mostly benchmarking).
    // 2 maps to SDL_RENDERER_VSYNC_ADAPTIVE (which is -1 in SDL).
    settings_register(&settings, SOUND_ASYNC_LOAD_KEY, "1", NULL);
    tig_sound_async_set_enabled(settings_get_value(&settings, SOUND_ASYNC_LOAD_KEY) != 0);

    // CE (feature/perf-gpu-accel Phase 3): tile-pass render path selector.
    // Read by tile_draw_iso each frame; cache is initialized lazily in
    // tile_init based on the same setting.
    settings_register(&settings, TILE_RENDER_PATH_KEY, TILE_RENDER_PATH_SOFTWARE, NULL);

    settings_register(&settings, VSYNC_MODE_KEY, "2", NULL);

    // CE: surface the highres "aspect snap" toggle (default on) as a real
    // cfg entry so it's visible and switchable. highres_config reads it
    // straight from the .cfg file; this registration just guarantees the
    // key is present (otherwise it only appeared via the one-time legacy
    // config migration, which omitted it for users who migrated earlier).
    settings_register(&settings, "aspect snap", "1", NULL);

    {
        int vsync_setting = settings_get_value(&settings, VSYNC_MODE_KEY);
        int sdl_mode = vsync_setting == 2 ? SDL_RENDERER_VSYNC_ADAPTIVE
            : vsync_setting == 0 ? 0
            : 1;
        tig_video_set_vsync_mode(sdl_mode);
    }

    gamelib_mod_loaded = false;
    gamelib_load_data();

    // TEMP: offline all-maps fade audit — must run before any window display
    // (a sandboxed/headless launch blocks forever in SwapWindow). Writes
    // /tmp/void_fade_audit.txt and exits the process.
    if (getenv("ARCANUM_VOID_FADE_AUDIT") != NULL) {
        void_edge_fade_run_audit();
    }

    if (!init_info->editor) {
        if (highres_config_get()->logos) {
            gamelib_logo();
        }

        gamelib_splash(init_info->iso_window_handle);
    }

    init_info->invalidate_rect_func = gamelib_invalidate_rect;
    init_info->draw_func = gamelib_draw;

    gamelib_init_info = *init_info;

    tig_window_data(init_info->iso_window_handle, &window_data);

    gamelib_window_rect_x = window_data.rect.x;
    gamelib_window_rect_y = window_data.rect.y;
    gamelib_thumbnail_width = window_data.rect.width / 4;
    gamelib_thumbnail_height = window_data.rect.height / 4;

    gamelib_iso_content_rect.x = 0;
    gamelib_iso_content_rect.y = 0;
    gamelib_iso_content_rect.width = window_data.rect.width;
    gamelib_iso_content_rect.height = window_data.rect.height;

    gamelib_iso_content_rect_ex.x = -256;
    gamelib_iso_content_rect_ex.y = -256;
    gamelib_iso_content_rect_ex.width = window_data.rect.width + 512;
    gamelib_iso_content_rect_ex.height = window_data.rect.height + 512;

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_COLOR_KEY | TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.width = window_data.rect.width;
    vb_create_info.height = window_data.rect.height;
    vb_create_info.color_key = tig_color_make(0, 255, 0);
    vb_create_info.background_color = vb_create_info.color_key;
    if (tig_video_buffer_create(&vb_create_info, &gamelib_scratch_video_buffer) != TIG_OK) {
        return false;
    }

    tig_window_vbid_get(init_info->iso_window_handle, &gamelib_iso_window_vb);

    if (!init_info->editor) {
        TigVideoBufferCreateInfo world_vb_info;
        world_vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
        world_vb_info.width = window_data.rect.width * 2;
        world_vb_info.height = window_data.rect.height * 2;
        world_vb_info.color_key = 0;
        world_vb_info.background_color = 0;
        if (tig_video_buffer_create(&world_vb_info, &gamelib_world_video_buffer) != TIG_OK) {
            gamelib_world_video_buffer = NULL;
            tig_debug_printf("gamelib_init: zoom disabled because world video buffer allocation failed.\n");
        }
    }

    iso_zoom_set_available(init_info->editor || gamelib_world_video_buffer != NULL);

    // CE (feature/perf-gpu-accel Phase 1+2): smoke-test the new GPU paths.
    // Opt-in via arcanum.cfg `gpu buffer sanity check=1`. Exercises:
    //   1. GPU-backed TigVideoBuffer create/destroy (Phase 1).
    //   2. CPU surface -> SDL_Texture upload helper (Phase 2).
    //   3. tig_video_buffer_blit_gpu in plain / COLOR_CONST / COLOR_LERP
    //      modes (Phase 2).
    //   4. tig_art_gpu_cache_init / stats / exit lifecycle (Phase 2).
    // Real callers come in Phase 3; this just confirms the path links and
    // runs end-to-end on the user's hardware before then.
    settings_register(&settings, GPU_BUFFER_SANITY_CHECK_KEY, "0", NULL);
    if (settings_get_value(&settings, GPU_BUFFER_SANITY_CHECK_KEY) != 0) {
        TigVideoBufferCreateInfo gpu_check_info;
        TigVideoBuffer* gpu_dst = NULL;
        TigVideoBuffer* cpu_src = NULL;
        SDL_Texture* gpu_src = NULL;
        int gpu_check_rc;
        bool gpu_check_ok = true;

        // (1) Create a GPU-backed destination buffer.
        gpu_check_info.flags = TIG_VIDEO_BUFFER_CREATE_TEXTURE;
        gpu_check_info.width = 256;
        gpu_check_info.height = 256;
        gpu_check_info.color_key = 0;
        gpu_check_info.background_color = tig_color_make(0x40, 0x80, 0xC0);
        gpu_check_rc = tig_video_buffer_create(&gpu_check_info, &gpu_dst);
        if (gpu_check_rc != TIG_OK || gpu_dst == NULL) {
            tig_debug_printf("gamelib_init: GPU sanity (1) buffer create FAILED rc=%d.\n", gpu_check_rc);
            gpu_check_ok = false;
        }

        // (2) Create a CPU source buffer and upload it as an SDL_Texture.
        if (gpu_check_ok) {
            TigVideoBufferCreateInfo cpu_info;
            cpu_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
            cpu_info.width = 64;
            cpu_info.height = 64;
            cpu_info.color_key = 0;
            cpu_info.background_color = tig_color_make(0xFF, 0xFF, 0xFF);
            gpu_check_rc = tig_video_buffer_create(&cpu_info, &cpu_src);
            if (gpu_check_rc != TIG_OK || cpu_src == NULL) {
                tig_debug_printf("gamelib_init: GPU sanity (2) CPU buffer create FAILED rc=%d.\n", gpu_check_rc);
                gpu_check_ok = false;
            }
        }
        if (gpu_check_ok) {
            gpu_src = tig_video_buffer_upload_to_texture(cpu_src);
            if (gpu_src == NULL) {
                tig_debug_printf("gamelib_init: GPU sanity (2) upload FAILED.\n");
                gpu_check_ok = false;
            }
        }

        // (3) Run all three blit_gpu paths.
        if (gpu_check_ok) {
            TigRect src_rect = { 0, 0, 64, 64 };
            TigRect dst_rect = { 0, 0, 64, 64 };
            TigVideoBufferBlitGpuInfo blit;
            memset(&blit, 0, sizeof(blit));
            blit.src_texture = gpu_src;
            blit.src_rect = &src_rect;
            blit.dst_video_buffer = gpu_dst;
            blit.dst_rect = &dst_rect;

            // 3a: plain.
            blit.flags = 0;
            if (tig_video_buffer_blit_gpu(&blit) != TIG_OK) {
                tig_debug_printf("gamelib_init: GPU sanity (3a) plain blit FAILED.\n");
                gpu_check_ok = false;
            }

            // 3b: COLOR_CONST.
            blit.flags = TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_CONST;
            blit.lerp_colors[0] = tig_color_make(0x80, 0xC0, 0xFF);
            if (tig_video_buffer_blit_gpu(&blit) != TIG_OK) {
                tig_debug_printf("gamelib_init: GPU sanity (3b) COLOR_CONST blit FAILED.\n");
                gpu_check_ok = false;
            }

            // 3c: COLOR_LERP.
            TigRect lerp_rect = { 0, 0, 64, 64 };
            blit.flags = TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_LERP;
            blit.lerp_rect = &lerp_rect;
            blit.lerp_colors[0] = tig_color_make(0xFF, 0x00, 0x00);
            blit.lerp_colors[1] = tig_color_make(0x00, 0xFF, 0x00);
            blit.lerp_colors[2] = tig_color_make(0x00, 0x00, 0xFF);
            blit.lerp_colors[3] = tig_color_make(0xFF, 0xFF, 0x00);
            if (tig_video_buffer_blit_gpu(&blit) != TIG_OK) {
                tig_debug_printf("gamelib_init: GPU sanity (3c) COLOR_LERP blit FAILED.\n");
                gpu_check_ok = false;
            }
        }

        // (4) Cache lifecycle.
        if (gpu_check_ok) {
            if (!tig_art_gpu_cache_init(0)) {
                tig_debug_printf("gamelib_init: GPU sanity (4) cache init FAILED.\n");
                gpu_check_ok = false;
            } else {
                TigArtGpuCacheStats stats;
                tig_art_gpu_cache_stats(&stats);
                tig_debug_printf("gamelib_init: GPU sanity (4) cache init OK -- budget=%zu bytes.\n", stats.budget_bytes);
                tig_art_gpu_cache_exit();
            }
        }

        if (gpu_check_ok) {
            tig_debug_printf("gamelib_init: GPU sanity check OK (Phase 1+2 paths verified).\n");
        }

        if (gpu_src != NULL) {
            SDL_DestroyTexture(gpu_src);
        }
        if (cpu_src != NULL) {
            tig_video_buffer_destroy(cpu_src);
        }
        if (gpu_dst != NULL) {
            tig_video_buffer_destroy(gpu_dst);
        }
    }

    if (init_info->editor) {
        gamelib_draw_func = gamelib_draw_editor;
    } else {
        gamelib_draw_func = gamelib_draw_game;
    }

    gamelib_view_options.type = VIEW_TYPE_ISOMETRIC;

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].init_func != NULL) {
            if (!gamelib_modules[index].init_func(init_info)) {
                tig_debug_printf("gamelib_init(): init function %d (%s) failed\n",
                    index,
                    gamelib_modules[index].name);

                while (--index >= 0) {
                    if (gamelib_modules[index].exit_func != NULL) {
                        gamelib_modules[index].exit_func();
                    }
                }

                return false;
            }
        }
    }

    if (gamelib_world_video_buffer != NULL) {
        TigRect zoom_content_rect = { 0, 0, gamelib_iso_content_rect.width * 2, gamelib_iso_content_rect.height * 2 };
        light_preallocate_for_zoom(&zoom_content_rect);
    }

    return true;
}

// 0x402380
void gamelib_reset(void)
{
    tig_timestamp_t reset_started_at;
    tig_timestamp_t module_started_at;
    tig_duration_t duration;
    TigRectListNode* node;
    TigRectListNode* next;
    int index;

    tig_debug_printf("gamelib_reset: Resetting.\n");
    tig_timer_now(&reset_started_at);

    in_reset = true;
    strcpy(gamelib_current_module_name, "Arcanum");
    sector_art_cache_disable();

    if (tig_file_is_directory("Save\\Current")) {
        tig_debug_printf("gamelib_reset: Begin Removing Files...");
        tig_timer_now(&module_started_at);

        if (!tig_file_empty_directory("Save\\Current")) {
            tig_debug_printf("gamelib_init(): error emptying folder %s\n", "Save\\Current");
        }

        duration = tig_timer_elapsed(module_started_at);
        tig_debug_printf("done. Time (ms): %d\n", duration);
    }

    node = gamelib_dirty_rects_head;
    while (node != NULL) {
        next = node->next;
        tig_rect_node_destroy(node);
        node = next;
    }
    gamelib_dirty_rects_head = NULL;

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].reset_func != NULL) {
            tig_debug_printf("gamelib_reset: Processing Reset Function: %d", index);
            tig_timer_now(&module_started_at);

            gamelib_modules[index].reset_func();

            duration = tig_timer_elapsed(module_started_at);
            tig_debug_printf(" done. Time (ms): %d.\n", duration);
        }
    }

    sector_art_cache_enable();
    in_reset = false;

    duration = tig_timer_elapsed(reset_started_at);
    tig_debug_printf("gamelib_reset(): Done.  Total time: %d ms.\n", duration);
}

// CE: documentation + canonical layout for arcanum.cfg. On save the file
// is written in THIS order, grouped under the section headers (a row with
// a NULL key). Each setting gets its comment as a "// ..." line above it.
// Settings not listed here are still saved — appended under an "Other"
// section — so nothing is lost. See settings_save_documented.
static const SettingsDoc gamelib_cfg_docs[] = {
    { NULL, "Display & Video" },
    { "windowed", "Run in a window instead of fullscreen (0=fullscreen, 1=windowed)." },
    { "resolution width", "Render width in pixels (minimum 800).\nNote: with 'aspect snap' on, the game rewrites this on launch to match your display's aspect ratio." },
    { "resolution height", "Render height in pixels (minimum 600).\nNote: auto-adjusted together with 'resolution width' when 'aspect snap' fits the display aspect ratio." },
    { "aspect snap", "On launch (fullscreen), snap resolution width/height to the display's aspect ratio and save the adjusted values back here (0=off, 1=on)." },
    { "vsync mode", "Frame pacing: 0=off (tearing, max throughput), 1=on (wait for vblank, no tearing), 2=adaptive (sync, but tear one frame instead of waiting on a miss)." },
    { "show fps", "Show the FPS counter (0=off, 1=on)." },
    { "tile render path", "Tile rasterizer backend: 'software' or 'gpu'." },
    { "gpu buffer sanity check", "Validate GPU buffers each frame; debugging aid (0=off, 1=on)." },
    { "macos ignore notch", "Use the full macOS display behind the camera notch (0=stay below the notch, 1=use the whole screen)." },
    { "legacy menu vignette", "Add a cinematic vignette to the legacy 800x600 menu/pause/splash art; no effect when custom-UI backgrounds are loaded (0=off, 1=on)." },
    { "brightness", "Screen brightness via gamma, 0-9 (0=normal, 9=brightest)." },
    { "object lighting", "Per-object dynamic lighting (0=off, 1=on)." },
    { "shadows", "Object drop shadows (0=off, 1=on)." },
    { "void edge fade", "Feather terrain/map edges into the black void at the edges of the zoomed-out world view (0=hard edges, 1=on)." },

    { NULL, "Zoom" },
    { "zoom enabled", "Allow zooming the isometric view (0=off, 1=on)." },
    { "min zoom", "How far you can zoom OUT — smallest factor (e.g. 0.5 = half size)." },
    { "max zoom", "How far you can zoom IN — largest factor (e.g. 1.75)." },
    { "zoom floor", "Cap how far you can zoom out to the character's perception range (0=off, 1=on)." },
    { "zoom filter", "Zoom scaling filter: 'nearest' (crisp pixels) or 'linear' (smooth)." },

    { NULL, "Startup" },
    { "intro", "Play the intro movie on launch (0=skip, 1=play)." },
    { "logos", "Play the publisher logo screens on launch (0=skip, 1=play)." },
    { "splash", "Picks which art\\splash image shows on launch; the game cycles this automatically each launch (not a toggle)." },
    { "show version", "Show the build version on screen (0=off, 1=on)." },
    { "video convert never ask", "Auto-convert legacy .bik videos without prompting (0=ask each time, 1=convert silently)." },

    { NULL, "Camera & Scrolling" },
    { "scroll dist", "Edge-scroll step size in pixels." },
    { "scroll fps", "Edge-scroll update rate in frames per second." },
    { "camera follows player", "Auto-follow the player as they move, with eased tracking and a deadband (0=off, 1=on)." },
    { "recenter camera on overlay", "Snap the camera to the PC when opening an overlay (Inventory, Logbook, etc.) (0=keep your scroll position, 1=recenter — vanilla)." },
    { "pc lens follows player", "Keep the PC lens showing the player even when the camera is panned/zoomed away (0=vanilla: shows screen center, 1=on)." },
    { "dialogue camera mode", "Camera framing during dialogue: 0=minimal (pan only to keep the speaker on screen), 1=midpoint of player & NPC, 2=center on NPC, 3=center on player." },
    { "dialogue camera zoom to fit", "Zoom out during dialogue to fit both speakers on screen (0=off, 1=on)." },
    { "dialogue camera tween back", "Glide the camera back to where it was when dialogue ends (0=off, 1=on)." },

    { NULL, "Interface" },
    { "ui animations", "Animated window open/close transitions (0=instant/snap, 1=on)." },
    { "translucent black ui", "Let the world show faintly through near-black UI panels like the HUD bar and inventory (0=off, 1=on)." },
    { "gold expansion slot priority", "When a growing gold pile no longer fits its cells: 0=the pile moves to a free slot, 1=the pile keeps its spot and other items shuffle out." },
    { "restore rotating window", "On load, reopen the rotating bottom-bar window on the page (Spells/Skills) that was open when you saved, instead of the default Messages page (0=always Messages, 1=restore saved page)." },

    { NULL, "Text & Dialogue" },
    { "text floaters", "Show floating combat/status text above characters (0=off, 1=on)." },
    { "text duration", "How long floating text stays on screen, in seconds (default 6)." },
    { "float speed", "How fast floating text rises (higher = faster)." },
    { "dialogue option numbers", "Number the dialogue reply options ('1.', '2.', ...), selectable by number key (0=off, 1=on)." },
    { "dialogue emote dim", "Dim bracketed emote/action text in dialogue (e.g. '[Sigh]') so it stands apart from spoken words (0=off, 1=on)." },
    { "dialogue emote dim choices", "Also dim bracketed emotes in your own reply options (a hovered option shows full color); needs 'dialogue emote dim' on (0=off, 1=on)." },
    { "dialogue emote dim percent", "How bright dimmed emote text stays, as a percent of normal (0=black, 100=no dimming; default 60)." },

    { NULL, "Audio" },
    { "music volume", "Music volume (0-10)." },
    { "voice volume", "Voice/dialogue volume (0-10)." },
    { "effects volume", "Sound-effects volume (0-10)." },
    { "sound async load", "Load never-heard sounds on a background thread to avoid a first-play hitch (0=synchronous/vanilla, 1=on)." },

    { NULL, "Gameplay" },
    { "difficulty", "Game difficulty: 0=easy, 1=normal, 2=hard." },
    { "always run", "Move at a run by default instead of walking (0=walk, 1=run)." },
    { "auto attack", "Automatically attack the nearest foe when nothing is targeted (0=off, 1=on)." },
    { "auto switch", "Auto-switch between melee and ranged weapons by range (0=off, 1=on)." },
    { "combat taunts", "Combatants shout taunts during combat (0=off, 1=on)." },
    { "turn-based", "Use turn-based combat instead of real-time (0=real-time, 1=turn-based)." },
    { "fast turn-based", "Speed up turn-based combat animations (0=normal, 1=fast)." },
    { "violence filter", "Reduce blood and gore effects (0=full gore, 1=reduced)." },
    { "follower skills", "Let followers use their skills automatically (0=off, 1=on)." },

    { NULL, "Perception Fog" },
    { "perception fog", "Perception fog master switch (0=off, 1=on)." },
    { "perception fog blur", "Box-blur the fog region (0=solid dark only, 1=blurred+dark)." },
    { "perception fog blur radius", "Box-blur kernel half-width in pixels (0-64; higher=softer)." },
    { "perception fog inner", "Radius in pixels where fog begins." },
    { "perception fog outer", "Radius in pixels where fog reaches full opacity; must be > inner." },
    { "perception fog alpha", "Maximum fog opacity (0-100)." },
    { "perception fog dim", "How much to darken blurred fog pixels (0=black, 100=no dimming)." },

    { NULL, NULL },
};

// 0x4024D0
void gamelib_exit(void)
{
    settings_save_documented(&settings, gamelib_cfg_docs);

    for (int index = MODULE_COUNT - 1; index >= 0; index--) {
        if (gamelib_modules[index].exit_func != NULL) {
            gamelib_modules[index].exit_func();
        }
    }

    mes_stats();

    TigRectListNode* node = gamelib_dirty_rects_head;
    while (node != NULL) {
        TigRectListNode* next = node->next;
        tig_rect_node_destroy(node);
        node = next;
    }

    if (gamelib_scratch_video_buffer != NULL) {
        tig_video_buffer_destroy(gamelib_scratch_video_buffer);
        gamelib_scratch_video_buffer = NULL;
    }

    if (gamelib_world_video_buffer != NULL) {
        tig_video_buffer_destroy(gamelib_world_video_buffer);
        gamelib_world_video_buffer = NULL;
    }

    if (tig_file_is_directory("Save\\Current")) {
        if (!tig_file_empty_directory("Save\\Current")) {
            // FIXME: Typo in function name, this is definitely not
            // `gamelib_init`.
            tig_debug_printf("gamelib_init(): error emptying folder %s\n", "Save\\Current");
        }
    }

    settings_exit(&settings);
}

// 0x402580
void gamelib_ping(void)
{
    int index;

    tig_timer_now(&gamelib_ping_time);

    bool perf_on = gamelib_zoom_perf_enabled;
    uint64_t ping_start_ns = perf_on ? gamelib_zoom_perf_now_ns() : 0;

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].ping_func != NULL) {
            uint64_t mod_start_ns = perf_on ? gamelib_zoom_perf_now_ns() : 0;
            gamelib_modules[index].ping_func(gamelib_ping_time);
            if (perf_on && index < GAMELIB_PERF_MAX_MODULES) {
                uint64_t mod_dur_ns = gamelib_zoom_perf_now_ns() - mod_start_ns;
                gamelib_zoom_perf_ping_module_total_ns[index] += mod_dur_ns;
                if (mod_dur_ns > gamelib_zoom_perf_ping_module_max_ns[index]) {
                    gamelib_zoom_perf_ping_module_max_ns[index] = mod_dur_ns;
                }
            }
        }
    }

    if (perf_on) {
        uint64_t ping_dur_ns = gamelib_zoom_perf_now_ns() - ping_start_ns;
        gamelib_zoom_perf_ping_total_ns += ping_dur_ns;
        if (ping_dur_ns > gamelib_zoom_perf_ping_max_ns) {
            gamelib_zoom_perf_ping_max_ns = ping_dur_ns;
        }
        gamelib_zoom_perf_ping_samples++;
    }
}

// 0x4025C0
void gamelib_resize(GameResizeInfo* resize_info)
{
    TigVideoBufferCreateInfo vb_create_info;
    int index;
    TigRect bounds;
    TigRect frame;

    gamelib_init_info.iso_window_handle = resize_info->window_handle;
    gamelib_iso_content_rect = resize_info->content_rect;

    gamelib_window_rect_x = resize_info->window_rect.x;
    gamelib_window_rect_y = resize_info->window_rect.y;

    gamelib_iso_content_rect_ex.x = gamelib_iso_content_rect.x - 256;
    gamelib_iso_content_rect_ex.y = gamelib_iso_content_rect.y - 256;
    gamelib_iso_content_rect_ex.width = gamelib_iso_content_rect.width + 512;
    gamelib_iso_content_rect_ex.height = gamelib_iso_content_rect.height + 512;

    if (gamelib_scratch_video_buffer != NULL) {
        tig_video_buffer_destroy(gamelib_scratch_video_buffer);
        gamelib_scratch_video_buffer = NULL;
    }

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_COLOR_KEY | TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.width = gamelib_iso_content_rect.width;
    vb_create_info.height = gamelib_iso_content_rect.height;
    vb_create_info.color_key = tig_color_make(0, 255, 0);
    vb_create_info.background_color = vb_create_info.color_key;
    if (tig_video_buffer_create(&vb_create_info, &gamelib_scratch_video_buffer) != TIG_OK) {
        tig_debug_printf("gamelib_resize: ERROR: Failed to rebuild scratch buffer!\n");
        return;
    }

    tig_window_vbid_get(resize_info->window_handle, &gamelib_iso_window_vb);

    if (gamelib_world_video_buffer != NULL) {
        tig_video_buffer_destroy(gamelib_world_video_buffer);
        gamelib_world_video_buffer = NULL;
    }

    if (gamelib_draw_func == gamelib_draw_game) {
        TigVideoBufferCreateInfo world_vb_info;
        world_vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
        world_vb_info.width = gamelib_iso_content_rect.width * 2;
        world_vb_info.height = gamelib_iso_content_rect.height * 2;
        world_vb_info.color_key = 0;
        world_vb_info.background_color = 0;
        if (tig_video_buffer_create(&world_vb_info, &gamelib_world_video_buffer) != TIG_OK) {
            gamelib_world_video_buffer = NULL;
            tig_debug_printf("gamelib_resize: zoom disabled because world video buffer allocation failed.\n");
        }
    }

    iso_zoom_set_available(gamelib_draw_func != gamelib_draw_game || gamelib_world_video_buffer != NULL);

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].resize_func != NULL) {
            gamelib_modules[index].resize_func(resize_info);
        }
    }

    if (gamelib_world_video_buffer != NULL) {
        TigRect zoom_content_rect = { 0, 0, gamelib_iso_content_rect.width * 2, gamelib_iso_content_rect.height * 2 };
        light_preallocate_for_zoom(&zoom_content_rect);
    }

    if (gamelib_dirty_rects_head != NULL) {
        bounds = resize_info->window_rect;
        bounds.x = 0;
        bounds.y = 0;
        if (tig_rect_intersection(&(gamelib_dirty_rects_head->rect), &bounds, &frame) == TIG_OK) {
            gamelib_dirty_rects_head->rect = frame;
        }
    }
}

// 0x402780
void gamelib_default_module_name_set(const char* name)
{
    strcpy(gamelib_default_module_name, name);
}

// 0x4027B0
const char* gamelib_default_module_name_get(void)
{
    return gamelib_default_module_name;
}

// 0x4027C0
void gamelib_modlist_create(GameModuleList* module_list, int type)
{
    TigFileList file_list;
    unsigned int file_index;
    char* dot;
    bool is_file;
    bool cont;
    bool found;
    unsigned int module_index;
    bool have_mp;
    char path[TIG_MAX_PATH];

    module_list->count = 0;
    module_list->selected = 0;
    module_list->paths = NULL;

    if (type != 0 && type != 1) {
        tig_debug_printf("Invalid type passed into gamelib_modlist_create()\n");
        return;
    }

    tig_file_list_create(&file_list, "Modules\\*.*");

    for (file_index = 0; file_index < file_list.count; file_index++) {
        cont = false;
        is_file = false;
        dot = strrchr(file_list.entries[file_index].path, '.');
        if (dot != NULL) {
            if (SDL_strcasecmp(dot, ".dat") == 0
                && (file_list.entries[file_index].attributes & TIG_FILE_ATTRIBUTE_SUBDIR) == 0) {
                *dot = '\0';
                is_file = true;
                cont = true;
            }
        } else if ((file_list.entries[file_index].attributes & TIG_FILE_ATTRIBUTE_SUBDIR) != 0) {
            cont = true;
        }

        if (cont) {
            found = false;
            module_index = module_list->count;
            while (module_index > 0) {
                if (SDL_strcasecmp(file_list.entries[file_index].path, module_list->paths[module_index - 1]) == 0) {
                    found = true;
                    break;
                }
                module_index--;
            }

            if (!found) {
                strcpy(path, "Modules\\");
                strcat(path, file_list.entries[file_index].path);
                if (is_file) {
                    strcat(path, ".dat");
                }

                tig_file_repository_add(path);
                have_mp = tig_file_exists_in_path(path, "mp.txt", NULL);
                tig_file_repository_remove(path);

                if ((have_mp && type == 1) || type == 0) {
                    module_list->paths = (char**)REALLOC(module_list->paths, sizeof(module_list->paths) * (module_list->count + 1));
                    module_list->paths[module_list->count] = STRDUP(file_list.entries[file_index].path);

                    if (SDL_strcasecmp(byte_5D0EA4, file_list.entries[file_index].path) == 0) {
                        module_list->selected = module_list->count;
                    }

                    module_list->count++;
                }
            }
        }
    }

    tig_file_list_destroy(&file_list);
}

// 0x402A10
void gamelib_modlist_destroy(GameModuleList* module_list)
{
    unsigned int index;

    for (index = 0; index < module_list->count; index++) {
        FREE(module_list->paths[index]);
    }
    FREE(module_list->paths);

    module_list->count = 0;
    module_list->selected = 0;
    module_list->paths = NULL;
}

// CE: bare module name from a possibly-pathed/extensioned arg (strip leading
// directory and a trailing ".dat").
static void gamelib_module_basename(const char* name, char* out, size_t out_sz)
{
    const char* base = name;
    const char* p;
    size_t n;

    if (name == NULL || out_sz == 0) {
        if (out_sz != 0) {
            out[0] = '\0';
        }
        return;
    }
    for (p = name; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    strncpy(out, base, out_sz - 1);
    out[out_sz - 1] = '\0';
    n = strlen(out);
    if (n > 4 && out[n - 4] == '.'
        && (out[n - 3] == 'd' || out[n - 3] == 'D')
        && (out[n - 2] == 'a' || out[n - 2] == 'A')
        && (out[n - 1] == 't' || out[n - 1] == 'T')) {
        out[n - 4] = '\0';
    }
}

// CE: keep the custom override dirs on top of the just-loaded module. Head-
// insertion order matters: promote custom\default first (above the module),
// then custom\modules\<name> (above default).
static void gamelib_mount_custom_overrides(const char* name)
{
    char base[TIG_MAX_PATH];
    char custom_mod[TIG_MAX_PATH];

    tig_file_repository_add_readonly("custom\\default");

    gamelib_module_basename(name, base, sizeof(base));
    if (base[0] != '\0') {
        snprintf(custom_mod, sizeof(custom_mod), "custom\\modules\\%s", base);
        if (tig_file_is_directory(custom_mod)) {
            tig_file_repository_add_readonly(custom_mod);
            strncpy(gamelib_mod_custom_path, custom_mod, TIG_MAX_PATH - 1);
            gamelib_mod_custom_path[TIG_MAX_PATH - 1] = '\0';
        }
    }
}

// 0x402A50
bool gamelib_mod_load(const char* path)
{
    TigFileList file_list;
    unsigned int file_index;
    int module_index;

    gamelib_mod_unload();

    if (!gamelib_load_module_data(path)) {
        return false;
    }

    // CE: re-promote custom overrides above the module just mounted.
    gamelib_mount_custom_overrides(path);

    if (gamelib_mod_dat_path[0] != '\0') {
        tig_file_repository_guid(gamelib_mod_dat_path, &gamelib_mod_guid);
    }

    dword_5D10C4 = true;

    if (tig_file_is_directory("Save\\Current")) {
        if (!tig_file_is_empty_directory("Save\\Current")) {
            if (!tig_file_empty_directory("Save\\Current")) {
                tig_debug_printf("gamelib_mod_load(): error emptying folder %s\n", "Save\\Current");
                gamelib_unload_module_data();
                return false;
            }
        }
    }

    if (!gamelib_init_info.editor) {
        tig_file_list_create(&file_list, "maps\\*.*");

        for (file_index = 0; file_index < file_list.count; file_index++) {
            if ((file_list.entries[file_index].attributes & TIG_FILE_ATTRIBUTE_SUBDIR) != 0
                && file_list.entries[file_index].path[0] != '.') {
                if (!map_preprocess_mobile(file_list.entries[file_index].path)) {
                    tig_debug_printf("gamelib_mod_load(): error preprocessing mobile object data for map %s\n",
                        file_list.entries[file_index].path);
                    gamelib_unload_module_data();
                    return false;
                }
            }
        }

        tig_file_list_destroy(&file_list);
    }

    for (module_index = 0; module_index < MODULE_COUNT; module_index++) {
        if (gamelib_modules[module_index].mod_load_func != NULL) {
            if (!gamelib_modules[module_index].mod_load_func()) {
                tig_debug_printf("gamelib_load(): mod load function %d (%s) failed\n",
                    module_index,
                    gamelib_modules[module_index].name);

                while (--module_index >= 0) {
                    if (gamelib_modules[module_index].mod_unload_func != NULL) {
                        gamelib_modules[module_index].mod_unload_func();
                    }
                }

                gamelib_unload_module_data();

                return false;
            }
        }
    }

    strcpy(byte_5D0EA4, path);
    gamelib_mod_loaded = true;

    return true;
}

// 0x402C20
bool gamelib_mod_guid_get(TigGuid* guid_ptr)
{
    if (!dword_5D10C4) {
        return false;
    }

    *guid_ptr = gamelib_mod_guid;

    return true;
}

// 0x402C60
void gamelib_mod_unload(void)
{
    int index;

    if (gamelib_mod_loaded) {
        for (index = MODULE_COUNT - 1; index >= 0; index--) {
            if (gamelib_modules[index].mod_unload_func != NULL) {
                gamelib_modules[index].mod_unload_func();
            }
        }
        gamelib_mod_loaded = false;
    }
}

// 0x402CA0
bool gamelib_in_reset(void)
{
    return in_reset;
}

// 0x402CB0
int gamelib_cheat_level_get(void)
{
    return gamelib_cheat_level;
}

// 0x402CC0
void gamelib_cheat_level_set(int level)
{
    gamelib_cheat_level = level;
}

// 0x402CD0
void gamelib_update_view(ViewOptions* view_options)
{
    for (int index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].update_view_func != NULL) {
            gamelib_modules[index].update_view_func(view_options);
        }
    }

    gamelib_view_options = *view_options;

    gamelib_invalidate_rect(NULL);
}

// 0x402D10
void gamelib_get_view_options(ViewOptions* view_options)
{
    *view_options = gamelib_view_options;
}

// Monotonic nanoseconds. CLOCK_MONOTONIC is portable to macOS/Linux.
static uint64_t gamelib_zoom_perf_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t gamelib_perf_now_ns(void)
{
    return gamelib_zoom_perf_now_ns();
}

// Threshold above which a single loop-step measurement is treated as a
// megahitch and logged inline (rather than being averaged into the
// 60-frame aggregate). 100ms is ~12x the typical loop budget on a
// 120Hz display — catches real perceptible pauses (e.g. save flush,
// sector load, art cache eviction) without spamming on routine vsync
// misses.
#define GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS 100000000ull

static void gamelib_perf_log_megahitch(const char* bucket, uint64_t ns)
{
    char line[256];
    snprintf(line, sizeof(line),
        "[megahitch] %s took %.1fms (%.2fs) — single iteration\n",
        bucket, (double)ns / 1e6, (double)ns / 1e9);
    tig_debug_printf("%s", line);
    gamelib_zoom_perf_log(line);
}

void gamelib_perf_record_tig_ping_ns(uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled) return;
    // First main-loop step recorded per iteration — also drives the
    // warmup counter. Skip until we're past the first few cold-cache
    // iterations after F9-on.
    if (!gamelib_zoom_perf_warmed_up) {
        gamelib_zoom_perf_warmup_count++;
        if (gamelib_zoom_perf_warmup_count >= GAMELIB_PERF_WARMUP_ITERATIONS) {
            gamelib_zoom_perf_warmed_up = true;
        }
        return;
    }
    gamelib_zoom_perf_tig_ping_total_ns += ns;
    if (ns > gamelib_zoom_perf_tig_ping_max_ns) {
        gamelib_zoom_perf_tig_ping_max_ns = ns;
    }
    gamelib_zoom_perf_tig_ping_samples++;
    if (ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        gamelib_perf_log_megahitch("tig_ping", ns);
    }
}

void gamelib_perf_record_iso_redraw_ns(uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    gamelib_zoom_perf_iso_redraw_total_ns += ns;
    if (ns > gamelib_zoom_perf_iso_redraw_max_ns) {
        gamelib_zoom_perf_iso_redraw_max_ns = ns;
    }
    gamelib_zoom_perf_iso_redraw_samples++;
    if (ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        gamelib_perf_log_megahitch("iso_redraw", ns);
    }
}

void gamelib_perf_record_window_display_ns(uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    gamelib_zoom_perf_window_display_total_ns += ns;
    if (ns > gamelib_zoom_perf_window_display_max_ns) {
        gamelib_zoom_perf_window_display_max_ns = ns;
    }
    gamelib_zoom_perf_window_display_samples++;
    if (ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        gamelib_perf_log_megahitch("win_display", ns);
    }
}

void gamelib_perf_record_key_repeat_ns(uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    gamelib_zoom_perf_key_repeat_total_ns += ns;
    if (ns > gamelib_zoom_perf_key_repeat_max_ns) {
        gamelib_zoom_perf_key_repeat_max_ns = ns;
    }
    gamelib_zoom_perf_key_repeat_samples++;
    if (ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        gamelib_perf_log_megahitch("key_repeat", ns);
    }
}

void gamelib_perf_record_event_dispatch_ns(uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    gamelib_zoom_perf_event_dispatch_total_ns += ns;
    if (ns > gamelib_zoom_perf_event_dispatch_max_ns) {
        gamelib_zoom_perf_event_dispatch_max_ns = ns;
    }
    gamelib_zoom_perf_event_dispatch_samples++;
    if (ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        gamelib_perf_log_megahitch("event_dispatch", ns);
    }
}

void gamelib_perf_log_event(const char* context, uint64_t ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    if (ns <= GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) return;
    char line[384];
    snprintf(line, sizeof(line),
        "[megahitch] event: %s took %.1fms (%.2fs)\n",
        context, (double)ns / 1e6, (double)ns / 1e9);
    tig_debug_printf("%s", line);
    gamelib_zoom_perf_log(line);
}

// 50ms is about 3 missed vsync slots on a 120Hz display. Below the
// per-bucket megahitch threshold (100ms) so we catch cumulative
// slowness that no single call would trip on its own.
#define GAMELIB_PERF_SLOW_LOOP_THRESHOLD_NS 50000000ull

void gamelib_perf_record_loop_iteration_ns(uint64_t total_ns,
    uint64_t tig_ping_ns, uint64_t key_repeat_ns,
    uint64_t iso_redraw_ns, uint64_t win_display_ns,
    uint64_t event_dispatch_ns)
{
    if (!gamelib_zoom_perf_enabled || !gamelib_zoom_perf_warmed_up) return;
    if (total_ns <= GAMELIB_PERF_SLOW_LOOP_THRESHOLD_NS) return;

    // Suppress if any single bucket already tripped the per-bucket
    // megahitch logger (>=100ms) — avoid duplicate noise for the
    // same slow iteration. (The per-bucket logger already named
    // the culprit; the loop-total line would add nothing.)
    if (tig_ping_ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS
        || key_repeat_ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS
        || iso_redraw_ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS
        || win_display_ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS
        || event_dispatch_ns > GAMELIB_PERF_MEGAHITCH_THRESHOLD_NS) {
        return;
    }

    char line[384];
    snprintf(line, sizeof(line),
        "[slow-loop] total %.1fms — tig_ping %.2f, key_repeat %.2f, iso_redraw %.2f, win_display %.2f, event_dispatch %.2f (ms)\n",
        (double)total_ns / 1e6,
        (double)tig_ping_ns / 1e6,
        (double)key_repeat_ns / 1e6,
        (double)iso_redraw_ns / 1e6,
        (double)win_display_ns / 1e6,
        (double)event_dispatch_ns / 1e6);
    tig_debug_printf("%s", line);
    gamelib_zoom_perf_log(line);
}

// Append a single line to /tmp/arcanum-zoom-perf.log so the perf summary
// is readable without dealing with macOS unified logging filters.
static void gamelib_zoom_perf_log(const char* line)
{
    FILE* fp = fopen("/tmp/arcanum-zoom-perf.log", "a");
    if (fp != NULL) {
        fputs(line, fp);
        fclose(fp);
    }
}

void gamelib_zoom_perf_toggle(void)
{
    gamelib_zoom_perf_enabled = !gamelib_zoom_perf_enabled;
    tig_video_flip_perf_set_enabled(gamelib_zoom_perf_enabled);
    // CE: ride the same toggle to enable per-call timing of the
    // translucent-black tint composite blit, so the perf-log dump
    // can report tint cost alongside the existing flip breakdown.
    tig_video_tint_blit_perf_set_enabled(gamelib_zoom_perf_enabled);
    // CE: + ui_anim spring-integrator timing (per-frame + per-apply +
    // active slot count) so motion-system cost is observable in the
    // same dump.
    ui_anim_perf_set_enabled(gamelib_zoom_perf_enabled);
    gamelib_zoom_perf_frames = 0;
    gamelib_zoom_perf_full_frames = 0;
    gamelib_zoom_perf_total_render_ns = 0;
    gamelib_zoom_perf_total_blit_ns = 0;
    gamelib_zoom_perf_total_dirty_px = 0;
    gamelib_zoom_perf_last_ns = 0;
    gamelib_zoom_perf_total_frame_ns = 0;
    gamelib_zoom_perf_max_frame_ns = 0;
    gamelib_zoom_perf_sum_sq_frame_ms = 0.0;
    gamelib_zoom_perf_frame_ns_samples = 0;
    gamelib_zoom_perf_total_zoom_ns = 0;
    gamelib_zoom_perf_max_zoom_ns = 0;
    gamelib_zoom_perf_total_other_ns = 0;
    gamelib_zoom_perf_max_other_ns = 0;
    gamelib_zoom_perf_ping_total_ns = 0;
    gamelib_zoom_perf_ping_max_ns = 0;
    gamelib_zoom_perf_ping_samples = 0;
    for (int i = 0; i < GAMELIB_PERF_MAX_MODULES; i++) {
        gamelib_zoom_perf_ping_module_total_ns[i] = 0;
        gamelib_zoom_perf_ping_module_max_ns[i] = 0;
    }
    gamelib_zoom_perf_tig_ping_total_ns = 0;
    gamelib_zoom_perf_tig_ping_max_ns = 0;
    gamelib_zoom_perf_tig_ping_samples = 0;
    gamelib_zoom_perf_iso_redraw_total_ns = 0;
    gamelib_zoom_perf_iso_redraw_max_ns = 0;
    gamelib_zoom_perf_iso_redraw_samples = 0;
    gamelib_zoom_perf_window_display_total_ns = 0;
    gamelib_zoom_perf_window_display_max_ns = 0;
    gamelib_zoom_perf_window_display_samples = 0;
    gamelib_zoom_perf_key_repeat_total_ns = 0;
    gamelib_zoom_perf_key_repeat_max_ns = 0;
    gamelib_zoom_perf_key_repeat_samples = 0;
    gamelib_zoom_perf_event_dispatch_total_ns = 0;
    gamelib_zoom_perf_event_dispatch_max_ns = 0;
    gamelib_zoom_perf_event_dispatch_samples = 0;
    gamelib_zoom_perf_warmup_count = 0;
    gamelib_zoom_perf_warmed_up = false;
    gamelib_zoom_perf_pass_light_total_ns = 0;
    gamelib_zoom_perf_pass_light_max_ns = 0;
    gamelib_zoom_perf_pass_tile_total_ns = 0;
    gamelib_zoom_perf_pass_tile_max_ns = 0;
    gamelib_zoom_perf_pass_object_total_ns = 0;
    gamelib_zoom_perf_pass_object_max_ns = 0;
    gamelib_zoom_perf_pass_roof_total_ns = 0;
    gamelib_zoom_perf_pass_roof_max_ns = 0;
    gamelib_zoom_perf_pass_samples = 0;
    char line[128];
    snprintf(line, sizeof(line), "[zoom-perf] %s\n",
        gamelib_zoom_perf_enabled ? "ON" : "OFF");
    tig_debug_printf("%s", line);
    gamelib_zoom_perf_log(line);
}

bool gamelib_zoom_perf_is_enabled(void)
{
    return gamelib_zoom_perf_enabled;
}

// 0x402D30
void gamelib_invalidate_rect(TigRect* rect)
{
    TigRect dirty_rect;
    TigRect clip_rect;

    // Always queue dirty rects in screen-space (the original ww x wh
    // viewport). gamelib_draw translates them to world-VB space at draw
    // time if zoom is active, so the coordinate system stays consistent
    // even if the zoom level changes between invalidate and draw.
    //
    // Clip-to-viewport logic: at zoom = 1.0 we just clip to the original
    // content rect. At zoom < 1.0 the visible area is wider than the
    // original viewport (we render a centered crop of size ww/z), so we
    // must expand the clip rect accordingly. Otherwise an NPC outside
    // the original [0, ww] viewport — but inside the zoomed-out visible
    // area — has its invalidation discarded here and the world VB stays
    // stale at its old position. Matches the expansion math in
    // object_get_effective_iso_content_rect_ex.
    clip_rect = gamelib_iso_content_rect;
    {
        float z = iso_zoom_current();
        bool zoom_active = (z != 1.0f)
            && (gamelib_world_video_buffer != NULL)
            && (gamelib_draw_func == gamelib_draw_game);
        if (zoom_active && z < 1.0f) {
            int exp_w = (int)ceilf((float)gamelib_iso_content_rect.width / z);
            int exp_h = (int)ceilf((float)gamelib_iso_content_rect.height / z);
            clip_rect.x = gamelib_iso_content_rect.x
                + (gamelib_iso_content_rect.width - exp_w) / 2 - 256;
            clip_rect.y = gamelib_iso_content_rect.y
                + (gamelib_iso_content_rect.height - exp_h) / 2 - 256;
            clip_rect.width = exp_w + 512;
            clip_rect.height = exp_h + 512;
        }
    }

    if (rect != NULL) {
        dirty_rect = *rect;

        if (tig_rect_intersection(&dirty_rect, &clip_rect, &dirty_rect) != TIG_OK) {
            return;
        }
    } else {
        dirty_rect = clip_rect;
    }

    if (in_draw) {
        if (gamelib_pending_dirty_rects_head != NULL) {
            sub_52D480(&gamelib_pending_dirty_rects_head, &dirty_rect);
        } else {
            gamelib_pending_dirty_rects_head = tig_rect_node_create();
            gamelib_pending_dirty_rects_head->rect = dirty_rect;
        }
    } else {
        if (gamelib_dirty_rects_head != NULL) {
            sub_52D480(&gamelib_dirty_rects_head, &dirty_rect);
        } else {
            gamelib_dirty_rects_head = tig_rect_node_create();
            gamelib_dirty_rects_head->rect = dirty_rect;
        }

        gamelib_dirty = true;
    }
}

// 0x402E50
bool gamelib_draw(void)
{
    bool ret = false;
    TigRectListNode* node;
    TigRectListNode* next;
    TigRect rect;
    LocRect loc_rect;
    SectorRect sector_rect;
    SectorListNode* sectors;
    GameDrawInfo draw_info;
    float z;
    bool zoom_active;
    TigRect orig_content_rect;
    TigRect orig_content_rect_ex;
    int64_t orig_ox;
    int64_t orig_oy;
    int ww;
    int wh;

    if (gamelib_renderlock_cnt <= 0) {
        return false;
    }

    // CE: UI animation spring integrator. Must run BEFORE the
    // gamelib_dirty early-return below — ui_anim drives window
    // transforms via tig's invalidate path (separate dirty list
    // from gamelib's), so when nothing in iso world is moving
    // (e.g. pre-game mainmenu, fully-paused game) the gamelib
    // dirty flag stays false but ui_anim still needs to advance
    // springs for entrance/exit/slide animations. tig_window_display
    // (called from each main loop AFTER iso_redraw → gamelib_draw)
    // picks up tig's own invalidations from transform_set and
    // composites the new frame.
    ui_anim_ping();

    // CE: HUD bar slide must run BEFORE the gamelib_dirty early-
    // return — ui_anim_ping just integrated the slide offsets, but
    // they're not applied to the bar's tig position until
    // intgame_hud_ping calls tig_window_move. When the game is
    // paused or we're at the mainmenu (gamelib_dirty=false), this
    // function returned early without ever moving the bars, leaving
    // them visible at whatever position the previous band-mode /
    // hide path put them. Run hud_ping first so slide_hide / slide_
    // show actually animate during the mainmenu navigation too.
    intgame_hud_ping();
    // CE: follower / fate / sleep UI per-frame slide updates must
    // also run before the gamelib_dirty early-return. They read
    // ui_anim_ping's just-integrated spring values and call
    // tig_window_move to apply them; if skipped on idle frames the
    // tween updates land in chunks instead of per-frame, reading
    // as stutter. (They appeared smooth during dialogue only
    // because TC's invalidations were keeping gamelib_dirty true
    // on those frames.) Each is a cheap no-op when its panel isn't
    // moving.
    follower_ui_ping();
    fate_ui_ping();
    sleep_ui_ping();
    // CE: ease the time-of-day ambient lighting toward the current
    // fractional hour so dawn/dusk fade gradually instead of snapping on
    // the hour. Self-throttled; no-op outside a game session.
    ambient_lighting_ping();
    // CE: dialog choice box slide (tc_bottom_gap_offset spring) also
    // belongs in the pre-dirty group. In practice TC's own
    // invalidations keep gamelib_dirty true through its tween, but
    // edge cases (dialogue starting while the game is paused on the
    // first frame; TC settling after a pause window closes) can
    // miss frames. Cheap no-op when the rect hasn't shifted.
    tc_ping();

    if (!gamelib_dirty) {
        return false;
    }

    iso_zoom_ping();
    // Order matters: tween_ping advances any in-flight tween; the
    // policy modules below (dialog_camera_ping, camera_follow_ping)
    // react to "just finished" or decide whether to start a new tween.
    camera_tween_ping();
    dialog_camera_ping();
    camera_follow_ping();
    // Note: intgame_hud_ping / follower_ui_ping / fate_ui_ping /
    // sleep_ui_ping / tc_ping are all called earlier (before the
    // gamelib_dirty early-return) so their slide animations
    // integrate smoothly on idle frames too. They read
    // intgame_hud_top_offset() / etc. each frame and pick up
    // ui_anim_ping's just-integrated values the same way.
    z = iso_zoom_current();
    zoom_active = (z != 1.0f)
        && (gamelib_world_video_buffer != NULL)
        && (gamelib_draw_func == gamelib_draw_game);

    // Start the zoom-active-total timer BEFORE the camera-move detection
    // block (which queues a full-invalidate on scroll). This captures the
    // full setup/teardown cost of the zoom-active path including any
    // sub_52D480 merges from camera-move-triggered invalidates.
    uint64_t perf_zoom_start_ns_outer = 0;
    if (gamelib_zoom_perf_enabled && zoom_active) {
        perf_zoom_start_ns_outer = gamelib_zoom_perf_now_ns();
    }

    // Phase A: world-VB content is built up across frames (we don't clear
    // it). If zoom_active just turned on, OR the zoom level lerped this
    // frame, OR the camera origin moved without a scroll-style invalidate,
    // the world VB has stale or empty regions that partial-render alone
    // won't refresh. Force a full screen invalidate in those cases so the
    // dirty rect translation downstream produces a full world-VB rect.
    {
        static float gamelib_prev_zoom = 1.0f;
        static int64_t gamelib_prev_ox = 0;
        static int64_t gamelib_prev_oy = 0;
        int64_t cur_ox;
        int64_t cur_oy;
        location_origin_get(&cur_ox, &cur_oy);
        bool zoom_step = (z != gamelib_prev_zoom);
        bool camera_moved = (cur_ox != gamelib_prev_ox || cur_oy != gamelib_prev_oy);
        if (zoom_active && (zoom_step || camera_moved)) {
            gamelib_invalidate_rect(NULL);
        }
        gamelib_prev_zoom = z;
        gamelib_prev_ox = cur_ox;
        gamelib_prev_oy = cur_oy;
    }

    in_draw = true;

    // Phase C: world-VB-space active render area. The world VB is
    // allocated at 2*ww x 2*wh (sized for z=0.5, the deepest zoom-out).
    // For z > 0.5 the final blit only samples a centered crop of size
    // (ww/z, wh/z), so anything we render outside that crop is wasted.
    // Setting the content rect to just the active area below restricts
    // sector iteration and dirty-rect translation to the area that
    // actually shows on screen.
    int active_w = 0;
    int active_h = 0;
    int active_x = 0;
    int active_y = 0;

    if (zoom_active) {
        orig_content_rect = gamelib_iso_content_rect;
        orig_content_rect_ex = gamelib_iso_content_rect_ex;
        location_origin_get(&orig_ox, &orig_oy);
        ww = orig_content_rect.width;
        wh = orig_content_rect.height;

        active_w = (int)ceilf((float)ww / z);
        active_h = (int)ceilf((float)wh / z);
        if (active_w > ww * 2) active_w = ww * 2;
        if (active_h > wh * 2) active_h = wh * 2;
        active_x = ww - active_w / 2;
        active_y = wh - active_h / 2;

        gamelib_iso_content_rect.x = active_x;
        gamelib_iso_content_rect.y = active_y;
        gamelib_iso_content_rect.width = active_w;
        gamelib_iso_content_rect.height = active_h;

        gamelib_iso_content_rect_ex.x = active_x - 256;
        gamelib_iso_content_rect_ex.y = active_y - 256;
        gamelib_iso_content_rect_ex.width = active_w + 512;
        gamelib_iso_content_rect_ex.height = active_h + 512;

        location_origin_pixel_set(orig_ox + ww / 2, orig_oy + wh / 2);

        tig_window_set_video_buffer(gamelib_init_info.iso_window_handle, gamelib_world_video_buffer);
        tile_set_render_target(gamelib_world_video_buffer);

        // Phase A: do NOT clear the world VB. Previous-frame pixels stay
        // valid as long as the camera hasn't moved (scrolling forces a
        // full screen invalidate in scroll.c at zoom != 1.0). We overpaint
        // only the dirty world-VB rects below.
    }

    if (location_screen_rect_to_loc_rect(&gamelib_iso_content_rect_ex, &loc_rect)) {
        uint64_t perf_render_start_ns = 0;
        uint64_t perf_blit_start_ns = 0;
        uint64_t perf_frame_render_ns = 0;
        uint64_t perf_frame_blit_ns = 0;
        int64_t perf_frame_dirty_px = 0;
        bool perf_frame_full = false;

        if (gamelib_view_options.type == VIEW_TYPE_ISOMETRIC) {
            sector_rect_from_loc_rect(&loc_rect, &sector_rect);
        }

        sectors = sector_list_create(&loc_rect);
        draw_info.screen_rect = &gamelib_iso_content_rect_ex;
        draw_info.loc_rect = &loc_rect;
        draw_info.sector_rect = &sector_rect;
        draw_info.sectors = sectors;
        draw_info.rects = &gamelib_dirty_rects_head;
        if (zoom_active) {
            // Phase A + C: translate each queued dirty rect from
            // screen-space (0..ww, 0..wh) to world-VB-space, clipped to
            // the active render area (centered, sized active_w x
            // active_h - just the area the final blit will sample). A
            // rect covering the full screen maps to the full active
            // area; smaller object rects get camera-shifted by (ww/2,
            // wh/2) and clipped to active bounds.
            TigRect active_bounds = { active_x, active_y, active_w, active_h };
            node = gamelib_dirty_rects_head;
            gamelib_dirty_rects_head = NULL;
            while (node != NULL) {
                next = node->next;
                TigRect translated;
                bool covers_full_screen = node->rect.x <= 0
                    && node->rect.y <= 0
                    && node->rect.x + node->rect.width >= ww
                    && node->rect.y + node->rect.height >= wh;
                if (covers_full_screen) {
                    translated = active_bounds;
                } else {
                    translated.x = node->rect.x + ww / 2;
                    translated.y = node->rect.y + wh / 2;
                    translated.width = node->rect.width;
                    translated.height = node->rect.height;
                    if (tig_rect_intersection(&translated, &active_bounds, &translated) != TIG_OK) {
                        tig_rect_node_destroy(node);
                        node = next;
                        continue;
                    }
                }
                tig_rect_node_destroy(node);
                if (gamelib_dirty_rects_head == NULL) {
                    gamelib_dirty_rects_head = tig_rect_node_create();
                    gamelib_dirty_rects_head->rect = translated;
                    gamelib_dirty_rects_head->next = NULL;
                } else {
                    sub_52D480(&gamelib_dirty_rects_head, &translated);
                }
                node = next;
            }
            tig_window_set_invalidate_suppressed(true);
            gamelib_zoom_world_pass_active = true;
            TigRect zoom_content_rect = { active_x, active_y, active_w, active_h };
            object_set_iso_content_rect(&zoom_content_rect);
            light_set_iso_content_rect(&zoom_content_rect);
        }
        if (gamelib_zoom_perf_enabled && zoom_active) {
            // Snapshot the area the renderer is about to touch. After
            // Phase C, the "full" reference is the active render area,
            // not the whole 2x world VB.
            int64_t full_px = (int64_t)active_w * (int64_t)active_h;
            for (node = gamelib_dirty_rects_head; node != NULL; node = node->next) {
                perf_frame_dirty_px += (int64_t)node->rect.width * (int64_t)node->rect.height;
            }
            perf_frame_full = (perf_frame_dirty_px >= full_px);
            perf_render_start_ns = gamelib_zoom_perf_now_ns();
        }
        gamelib_draw_func(&draw_info);
        if (gamelib_zoom_perf_enabled && zoom_active) {
            perf_frame_render_ns = gamelib_zoom_perf_now_ns() - perf_render_start_ns;
            gamelib_zoom_perf_total_render_ns += perf_frame_render_ns;
        }
        if (zoom_active) {
            gamelib_zoom_world_pass_active = false;
            tig_window_set_invalidate_suppressed(false);
            object_set_iso_content_rect(&orig_content_rect);
            light_set_iso_content_rect(&orig_content_rect);
        }
        sector_list_destroy(sectors);

        if (zoom_active) {
            int src_w;
            int src_h;
            TigRect src;
            TigRect dst;
            TigVideoBufferBlitInfo blit = { 0 };

            tig_window_set_video_buffer(gamelib_init_info.iso_window_handle, gamelib_iso_window_vb);
            tile_set_render_target(gamelib_iso_window_vb);
            gamelib_iso_content_rect = orig_content_rect;
            gamelib_iso_content_rect_ex = orig_content_rect_ex;
            location_origin_pixel_set(orig_ox, orig_oy);
            zoom_active = false;

            // Phase A: render is partial (driven by dirty rects above),
            // but blit is ALWAYS full. Reason: world-VB content is
            // zoom-independent (rendered at world-VB coords) but the
            // downscale-to-window mapping is zoom-dependent. If we
            // partial-blit and the zoom changed since the previous frame,
            // un-blitted window regions keep the old downscale and look
            // out of sync with the freshly-blitted partials. Full blit at
            // 0.5-1ms is small change vs. the ~8ms render savings.
            src_w = (int)roundf((float)ww / z);
            src_h = (int)roundf((float)wh / z);
            src.x = ww - src_w / 2;
            src.y = wh - src_h / 2;
            src.width = src_w;
            src.height = src_h;
            dst.x = 0;
            dst.y = 0;
            dst.width = ww;
            dst.height = wh;
            blit.src_video_buffer = gamelib_world_video_buffer;
            blit.src_rect = &src;
            blit.dst_video_buffer = gamelib_iso_window_vb;
            blit.dst_rect = &dst;
            if (z < 1.0f) {
                // Bilinear when downscaling so the stipple dither used for
                // fade-for-occlusion walls/roofs averages to ~50% alpha
                // instead of being erased by NEAREST sampling.
                blit.flags = TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR;
            }
            if (gamelib_zoom_perf_enabled) {
                perf_blit_start_ns = gamelib_zoom_perf_now_ns();
            }
            tig_video_buffer_blit(&blit);

            if (gamelib_zoom_perf_enabled) {
                perf_frame_blit_ns = gamelib_zoom_perf_now_ns() - perf_blit_start_ns;
                gamelib_zoom_perf_total_blit_ns += perf_frame_blit_ns;
                gamelib_zoom_perf_total_dirty_px += perf_frame_dirty_px;
                if (perf_frame_full) {
                    gamelib_zoom_perf_full_frames++;
                }
                gamelib_zoom_perf_last_z = z;

                // Total zoom-active time (start of this if-block to end of
                // blit) and "other" = total - render - blit.
                uint64_t zoom_total_ns = gamelib_zoom_perf_now_ns() - perf_zoom_start_ns_outer;
                uint64_t other_ns = (zoom_total_ns > perf_frame_render_ns + perf_frame_blit_ns)
                    ? zoom_total_ns - perf_frame_render_ns - perf_frame_blit_ns
                    : 0;
                gamelib_zoom_perf_total_zoom_ns += zoom_total_ns;
                gamelib_zoom_perf_total_other_ns += other_ns;
                if (zoom_total_ns > gamelib_zoom_perf_max_zoom_ns) {
                    gamelib_zoom_perf_max_zoom_ns = zoom_total_ns;
                }
                if (other_ns > gamelib_zoom_perf_max_other_ns) {
                    gamelib_zoom_perf_max_other_ns = other_ns;
                }

                // Wall-clock frame interval: time from previous zoom-active
                // draw to this one. Captures the TOTAL frame cost (render +
                // AI + scripts + present), not just our render bucket. The
                // first sample after toggle-ON has no prior reference; skip.
                uint64_t now_ns = gamelib_zoom_perf_now_ns();
                if (gamelib_zoom_perf_last_ns != 0 && now_ns >= gamelib_zoom_perf_last_ns) {
                    uint64_t delta_ns = now_ns - gamelib_zoom_perf_last_ns;
                    gamelib_zoom_perf_total_frame_ns += delta_ns;
                    if (delta_ns > gamelib_zoom_perf_max_frame_ns) {
                        gamelib_zoom_perf_max_frame_ns = delta_ns;
                    }
                    double delta_ms = (double)delta_ns / 1e6;
                    gamelib_zoom_perf_sum_sq_frame_ms += delta_ms * delta_ms;
                    gamelib_zoom_perf_frame_ns_samples++;
                }
                gamelib_zoom_perf_last_ns = now_ns;

                if (++gamelib_zoom_perf_frames >= GAMELIB_ZOOM_PERF_INTERVAL) {
                    int64_t full_px = (int64_t)active_w * (int64_t)active_h;
                    float avg_render_ms = (float)((double)gamelib_zoom_perf_total_render_ns
                        / (double)gamelib_zoom_perf_frames / 1e6);
                    float avg_blit_ms = (float)((double)gamelib_zoom_perf_total_blit_ns
                        / (double)gamelib_zoom_perf_frames / 1e6);
                    float avg_zoom_ms = (float)((double)gamelib_zoom_perf_total_zoom_ns
                        / (double)gamelib_zoom_perf_frames / 1e6);
                    float avg_other_ms = (float)((double)gamelib_zoom_perf_total_other_ns
                        / (double)gamelib_zoom_perf_frames / 1e6);
                    float max_zoom_ms = (float)((double)gamelib_zoom_perf_max_zoom_ns / 1e6);
                    float max_other_ms = (float)((double)gamelib_zoom_perf_max_other_ns / 1e6);
                    float avg_dirty_pct = full_px > 0
                        ? (float)gamelib_zoom_perf_total_dirty_px * 100.0f
                            / ((float)full_px * (float)gamelib_zoom_perf_frames)
                        : 0.0f;
                    int full_pct = (gamelib_zoom_perf_full_frames * 100) / gamelib_zoom_perf_frames;

                    // Frame-interval stats (wall clock, captures EVERYTHING).
                    float avg_frame_ms = 0.0f;
                    float max_frame_ms = 0.0f;
                    float stddev_frame_ms = 0.0f;
                    if (gamelib_zoom_perf_frame_ns_samples > 0) {
                        avg_frame_ms = (float)((double)gamelib_zoom_perf_total_frame_ns
                            / (double)gamelib_zoom_perf_frame_ns_samples / 1e6);
                        max_frame_ms = (float)((double)gamelib_zoom_perf_max_frame_ns / 1e6);
                        double mean = avg_frame_ms;
                        double var = (gamelib_zoom_perf_sum_sq_frame_ms
                            / (double)gamelib_zoom_perf_frame_ns_samples) - mean * mean;
                        if (var < 0.0) var = 0.0;
                        stddev_frame_ms = (float)sqrt(var);
                    }

                    char line[512];
                    snprintf(line, sizeof(line),
                        "[zoom-perf] z=%.2f over %d frames: render %.2fms, blit %.2fms, OTHER %.2fms (max %.2fms), zoom-total %.2fms (max %.2fms), dirty %.0f%%, full-redraws %d%% | frame avg %.2fms max %.2fms stddev %.2fms\n",
                        gamelib_zoom_perf_last_z,
                        gamelib_zoom_perf_frames,
                        avg_render_ms,
                        avg_blit_ms,
                        avg_other_ms,
                        max_other_ms,
                        avg_zoom_ms,
                        max_zoom_ms,
                        avg_dirty_pct,
                        full_pct,
                        avg_frame_ms,
                        max_frame_ms,
                        stddev_frame_ms);
                    tig_debug_printf("%s", line);
                    gamelib_zoom_perf_log(line);

                    // Second line: per-subsystem ping breakdown. Total ping
                    // avg/max for the same window, then the top 3 hottest
                    // modules by accumulated time. Helps identify which
                    // subsystem owns the gap between zoom-active render
                    // time and total frame time.
                    if (gamelib_zoom_perf_ping_samples > 0) {
                        float avg_ping_ms = (float)((double)gamelib_zoom_perf_ping_total_ns
                            / (double)gamelib_zoom_perf_ping_samples / 1e6);
                        float max_ping_ms = (float)((double)gamelib_zoom_perf_ping_max_ns / 1e6);
                        int top_idx[3] = { -1, -1, -1 };
                        uint64_t top_ns[3] = { 0, 0, 0 };
                        int module_count = MODULE_COUNT;
                        if (module_count > GAMELIB_PERF_MAX_MODULES) {
                            module_count = GAMELIB_PERF_MAX_MODULES;
                        }
                        for (int i = 0; i < module_count; i++) {
                            uint64_t t = gamelib_zoom_perf_ping_module_total_ns[i];
                            if (t > top_ns[0]) {
                                top_ns[2] = top_ns[1]; top_idx[2] = top_idx[1];
                                top_ns[1] = top_ns[0]; top_idx[1] = top_idx[0];
                                top_ns[0] = t; top_idx[0] = i;
                            } else if (t > top_ns[1]) {
                                top_ns[2] = top_ns[1]; top_idx[2] = top_idx[1];
                                top_ns[1] = t; top_idx[1] = i;
                            } else if (t > top_ns[2]) {
                                top_ns[2] = t; top_idx[2] = i;
                            }
                        }
                        char hot[3][64] = { "", "", "" };
                        for (int k = 0; k < 3; k++) {
                            if (top_idx[k] >= 0 && top_ns[k] > 0) {
                                double avg_ms = (double)top_ns[k]
                                    / (double)gamelib_zoom_perf_ping_samples / 1e6;
                                double max_ms = (double)gamelib_zoom_perf_ping_module_max_ns[top_idx[k]] / 1e6;
                                snprintf(hot[k], sizeof(hot[k]), "%s(avg %.2f max %.2f)",
                                    gamelib_modules[top_idx[k]].name, avg_ms, max_ms);
                            }
                        }
                        char ping_line[512];
                        snprintf(ping_line, sizeof(ping_line),
                            "[zoom-perf]   ping avg %.2fms max %.2fms (%d samples) | hot: %s %s %s\n",
                            avg_ping_ms, max_ping_ms, gamelib_zoom_perf_ping_samples,
                            hot[0], hot[1], hot[2]);
                        tig_debug_printf("%s", ping_line);
                        gamelib_zoom_perf_log(ping_line);
                    }

                    gamelib_zoom_perf_frames = 0;
                    gamelib_zoom_perf_full_frames = 0;
                    gamelib_zoom_perf_total_frame_ns = 0;
                    gamelib_zoom_perf_max_frame_ns = 0;
                    gamelib_zoom_perf_sum_sq_frame_ms = 0.0;
                    gamelib_zoom_perf_frame_ns_samples = 0;
                    gamelib_zoom_perf_total_render_ns = 0;
                    gamelib_zoom_perf_total_blit_ns = 0;
                    gamelib_zoom_perf_total_dirty_px = 0;
                    gamelib_zoom_perf_total_zoom_ns = 0;
                    gamelib_zoom_perf_max_zoom_ns = 0;
                    gamelib_zoom_perf_total_other_ns = 0;
                    gamelib_zoom_perf_max_other_ns = 0;
                    // Third line: main-loop bucket breakdown (tig_ping,
                    // iso_redraw, tig_window_display) + intra-flip split
                    // (SDL_UpdateTexture vs SDL_RenderPresent). After
                    // confirming gamelib_ping is ~0.1ms, these are the
                    // remaining candidates for the inter-frame gap.
                    // window_display = composite + flip; flip splits as
                    // update (CPU→GPU upload) + present (typically vsync).
                    TigVideoFlipPerf flip_perf;
                    tig_video_flip_perf_get(&flip_perf);
                    tig_video_flip_perf_reset();
                    float avg_flip_update_ms = flip_perf.samples > 0
                        ? (float)((double)flip_perf.update_total_ns / (double)flip_perf.samples / 1e6)
                        : 0.0f;
                    float max_flip_update_ms = (float)((double)flip_perf.update_max_ns / 1e6);
                    float avg_flip_present_ms = flip_perf.samples > 0
                        ? (float)((double)flip_perf.present_total_ns / (double)flip_perf.samples / 1e6)
                        : 0.0f;
                    float max_flip_present_ms = (float)((double)flip_perf.present_max_ns / 1e6);
                    int partial_pct = flip_perf.samples > 0
                        ? (flip_perf.partial_samples * 100) / flip_perf.samples
                        : 0;
                    if (gamelib_zoom_perf_tig_ping_samples > 0
                        || gamelib_zoom_perf_iso_redraw_samples > 0
                        || gamelib_zoom_perf_window_display_samples > 0) {
                        float avg_tig_ms = gamelib_zoom_perf_tig_ping_samples > 0
                            ? (float)((double)gamelib_zoom_perf_tig_ping_total_ns
                                / (double)gamelib_zoom_perf_tig_ping_samples / 1e6)
                            : 0.0f;
                        float max_tig_ms = (float)((double)gamelib_zoom_perf_tig_ping_max_ns / 1e6);
                        float avg_iso_ms = gamelib_zoom_perf_iso_redraw_samples > 0
                            ? (float)((double)gamelib_zoom_perf_iso_redraw_total_ns
                                / (double)gamelib_zoom_perf_iso_redraw_samples / 1e6)
                            : 0.0f;
                        float max_iso_ms = (float)((double)gamelib_zoom_perf_iso_redraw_max_ns / 1e6);
                        float avg_win_ms = gamelib_zoom_perf_window_display_samples > 0
                            ? (float)((double)gamelib_zoom_perf_window_display_total_ns
                                / (double)gamelib_zoom_perf_window_display_samples / 1e6)
                            : 0.0f;
                        float max_win_ms = (float)((double)gamelib_zoom_perf_window_display_max_ns / 1e6);
                        char loop_line[384];
                        snprintf(loop_line, sizeof(loop_line),
                            "[zoom-perf]   loop: tig_ping avg %.2fms max %.2fms | iso_redraw avg %.2fms max %.2fms | win_display avg %.2fms max %.2fms | flip: update avg %.2fms max %.2fms, present avg %.2fms max %.2fms, partial %d%%\n",
                            avg_tig_ms, max_tig_ms,
                            avg_iso_ms, max_iso_ms,
                            avg_win_ms, max_win_ms,
                            avg_flip_update_ms, max_flip_update_ms,
                            avg_flip_present_ms, max_flip_present_ms,
                            partial_pct);
                        tig_debug_printf("%s", loop_line);
                        gamelib_zoom_perf_log(loop_line);

                        // CE: translucent-black tint composite blit
                        // cost. Per-call avg/max and total time spent
                        // across the window, so the alpha-blend and
                        // MUL-tint pathways can be compared head to
                        // head between branches. Only fires when at
                        // least one tint blit ran in the window —
                        // expected zero when the cfg is off or no
                        // tint-opted-in window is visible.
                        TigVideoTintBlitPerf tint_perf;
                        tig_video_tint_blit_perf_get(&tint_perf);
                        tig_video_tint_blit_perf_reset();
                        if (tint_perf.samples > 0) {
                            float avg_us = (float)((double)tint_perf.total_ns
                                / (double)tint_perf.samples / 1e3);
                            float max_us = (float)((double)tint_perf.max_ns / 1e3);
                            float total_ms = (float)((double)tint_perf.total_ns / 1e6);
                            double avg_px = (double)tint_perf.pixels_total
                                / (double)tint_perf.samples;
                            char tint_line[256];
                            snprintf(tint_line, sizeof(tint_line),
                                "[zoom-perf]   tint-blit: samples %d, avg %.1fus max %.1fus, total %.1fms, avg %.0f px/call (%.1fM px total)\n",
                                tint_perf.samples,
                                avg_us, max_us, total_ms,
                                avg_px,
                                (double)tint_perf.pixels_total / 1e6);
                            tig_debug_printf("%s", tint_line);
                            gamelib_zoom_perf_log(tint_line);
                        }

                        // CE: ui_anim spring-integrator cost. Active
                        // slots = concurrent tweens (typical session:
                        // 0–4; opens / closes spike briefly). ping =
                        // full integrator including per-slot apply.
                        // apply = subset spent inside the per-kind
                        // writer (transform_set, int_var write, etc).
                        TigUiAnimPerf ui_perf;
                        ui_anim_perf_get(&ui_perf);
                        ui_anim_perf_reset();
                        if (ui_perf.ping_samples > 0) {
                            float avg_ping_us = (float)((double)ui_perf.ping_total_ns
                                / (double)ui_perf.ping_samples / 1e3);
                            float max_ping_us = (float)((double)ui_perf.ping_max_ns / 1e3);
                            float avg_apply_us = (float)((double)ui_perf.apply_total_ns
                                / (double)ui_perf.ping_samples / 1e3);
                            float avg_active = (float)((double)ui_perf.active_slots_total
                                / (double)ui_perf.ping_samples);
                            char ui_line[256];
                            snprintf(ui_line, sizeof(ui_line),
                                "[zoom-perf]   ui_anim: ping avg %.1fus max %.1fus (apply avg %.1fus) | active avg %.1f max %d\n",
                                avg_ping_us, max_ping_us, avg_apply_us,
                                avg_active, ui_perf.active_slots_max);
                            tig_debug_printf("%s", ui_line);
                            gamelib_zoom_perf_log(ui_line);
                        }

                        // Fourth line: remaining main-loop buckets that
                        // sit between the measured slots above —
                        // handle_zoom_key_repeat and the inner message
                        // dequeue/dispatch loop. Frame-max spikes of
                        // 25-70ms aren't visible in any of the buckets
                        // above; one of these should reveal where they
                        // come from.
                        if (gamelib_zoom_perf_key_repeat_samples > 0
                            || gamelib_zoom_perf_event_dispatch_samples > 0) {
                            float avg_key_ms = gamelib_zoom_perf_key_repeat_samples > 0
                                ? (float)((double)gamelib_zoom_perf_key_repeat_total_ns
                                    / (double)gamelib_zoom_perf_key_repeat_samples / 1e6)
                                : 0.0f;
                            float max_key_ms = (float)((double)gamelib_zoom_perf_key_repeat_max_ns / 1e6);
                            float avg_evt_ms = gamelib_zoom_perf_event_dispatch_samples > 0
                                ? (float)((double)gamelib_zoom_perf_event_dispatch_total_ns
                                    / (double)gamelib_zoom_perf_event_dispatch_samples / 1e6)
                                : 0.0f;
                            float max_evt_ms = (float)((double)gamelib_zoom_perf_event_dispatch_max_ns / 1e6);
                            char extra[256];
                            snprintf(extra, sizeof(extra),
                                "[zoom-perf]   loop2: key_repeat avg %.2fms max %.2fms | event_dispatch avg %.2fms max %.2fms\n",
                                avg_key_ms, max_key_ms, avg_evt_ms, max_evt_ms);
                            tig_debug_printf("%s", extra);
                            gamelib_zoom_perf_log(extra);
                        }

                        // Fifth line: render-pass breakdown. Identifies
                        // which pass dominates the heavy iso_redraw
                        // frames (the ones that overrun the 8.3ms
                        // ProMotion budget and cause remaining stutter).
                        if (gamelib_zoom_perf_pass_samples > 0) {
                            double n = (double)gamelib_zoom_perf_pass_samples;
                            float al = (float)((double)gamelib_zoom_perf_pass_light_total_ns / n / 1e6);
                            float at = (float)((double)gamelib_zoom_perf_pass_tile_total_ns / n / 1e6);
                            float ao = (float)((double)gamelib_zoom_perf_pass_object_total_ns / n / 1e6);
                            float ar = (float)((double)gamelib_zoom_perf_pass_roof_total_ns / n / 1e6);
                            float ml = (float)((double)gamelib_zoom_perf_pass_light_max_ns / 1e6);
                            float mt = (float)((double)gamelib_zoom_perf_pass_tile_max_ns / 1e6);
                            float mo = (float)((double)gamelib_zoom_perf_pass_object_max_ns / 1e6);
                            float mr = (float)((double)gamelib_zoom_perf_pass_roof_max_ns / 1e6);
                            char passes[384];
                            snprintf(passes, sizeof(passes),
                                "[zoom-perf]   passes: light avg %.2fms max %.2fms | tile avg %.2fms max %.2fms | object avg %.2fms max %.2fms | roof avg %.2fms max %.2fms\n",
                                al, ml, at, mt, ao, mo, ar, mr);
                            tig_debug_printf("%s", passes);
                            gamelib_zoom_perf_log(passes);
                        }
                    }

                    gamelib_zoom_perf_ping_total_ns = 0;
                    gamelib_zoom_perf_ping_max_ns = 0;
                    gamelib_zoom_perf_ping_samples = 0;
                    for (int i = 0; i < GAMELIB_PERF_MAX_MODULES; i++) {
                        gamelib_zoom_perf_ping_module_total_ns[i] = 0;
                        gamelib_zoom_perf_ping_module_max_ns[i] = 0;
                    }
                    gamelib_zoom_perf_tig_ping_total_ns = 0;
                    gamelib_zoom_perf_tig_ping_max_ns = 0;
                    gamelib_zoom_perf_tig_ping_samples = 0;
                    gamelib_zoom_perf_iso_redraw_total_ns = 0;
                    gamelib_zoom_perf_iso_redraw_max_ns = 0;
                    gamelib_zoom_perf_iso_redraw_samples = 0;
                    gamelib_zoom_perf_window_display_total_ns = 0;
                    gamelib_zoom_perf_window_display_max_ns = 0;
                    gamelib_zoom_perf_window_display_samples = 0;
                    gamelib_zoom_perf_key_repeat_total_ns = 0;
                    gamelib_zoom_perf_key_repeat_max_ns = 0;
                    gamelib_zoom_perf_key_repeat_samples = 0;
                    gamelib_zoom_perf_event_dispatch_total_ns = 0;
                    gamelib_zoom_perf_event_dispatch_max_ns = 0;
                    gamelib_zoom_perf_event_dispatch_samples = 0;
                    gamelib_zoom_perf_pass_light_total_ns = 0;
                    gamelib_zoom_perf_pass_light_max_ns = 0;
                    gamelib_zoom_perf_pass_tile_total_ns = 0;
                    gamelib_zoom_perf_pass_tile_max_ns = 0;
                    gamelib_zoom_perf_pass_object_total_ns = 0;
                    gamelib_zoom_perf_pass_object_max_ns = 0;
                    gamelib_zoom_perf_pass_roof_total_ns = 0;
                    gamelib_zoom_perf_pass_roof_max_ns = 0;
                    gamelib_zoom_perf_pass_samples = 0;
                }
            }

            // Re-run fixed-screen HUD draws (tc/tf/tb) directly onto
            // iso_window_vb at normal viewport coordinates. They rendered into
            // world_vb at fixed coords outside the scale-blit src rect, so
            // they would have been clipped away. We stamp them on top here.
            {
                TigRectListNode hud_node;
                TigRectListNode* hud_head;
                GameDrawInfo hud_info;

                hud_node.rect = orig_content_rect;
                hud_node.next = NULL;
                hud_head = &hud_node;

                memset(&hud_info, 0, sizeof(hud_info));
                hud_info.rects = &hud_head;

                if (tig_video_3d_begin_scene() == TIG_OK) {
                    tb_draw(&hud_info);
                    tf_draw(&hud_info);
                    tc_draw(&hud_info);
                    tig_video_3d_end_scene();
                }
            }

            // Phase A: drop dirty world-VB rects (consumed) and do a full
            // window invalidate. Matches the full blit above.
            node = gamelib_dirty_rects_head;
            while (node != NULL) {
                next = node->next;
                tig_rect_node_destroy(node);
                node = next;
            }
            gamelib_dirty_rects_head = NULL;
            tig_window_invalidate_rect(NULL);
        } else {
            node = gamelib_dirty_rects_head;
            while (node != NULL) {
                next = node->next;
                rect = node->rect;
                rect.x += gamelib_window_rect_x;
                rect.y += gamelib_window_rect_y;
                tig_window_invalidate_rect(&rect);
                tig_rect_node_destroy(node);
                node = next;
            }
        }
        ret = true;
    }

    if (zoom_active) {
        tig_window_set_video_buffer(gamelib_init_info.iso_window_handle, gamelib_iso_window_vb);
        tile_set_render_target(gamelib_iso_window_vb);
        gamelib_iso_content_rect = orig_content_rect;
        gamelib_iso_content_rect_ex = orig_content_rect_ex;
        location_origin_pixel_set(orig_ox, orig_oy);
        object_set_iso_content_rect(&orig_content_rect);
        light_set_iso_content_rect(&orig_content_rect);
    }

    gamelib_dirty_rects_head = gamelib_pending_dirty_rects_head;
    gamelib_pending_dirty_rects_head = NULL;

    if (gamelib_dirty_rects_head == NULL) {
        gamelib_dirty = false;
    }

    // Keep rendering each frame while zoom is still lerping.
    if (iso_zoom_is_animating()) {
        gamelib_dirty = true;
    }

    if (dialog_camera_is_animating()) {
        gamelib_dirty = true;
    }

    in_draw = false;

    return ret;
}

// 0x402F90
void gamelib_renderlock_acquire(void)
{
    gamelib_renderlock_cnt--;
}

// 0x402FA0
void gamelib_renderlock_release(void)
{
    gamelib_renderlock_cnt++;
}

// 0x402FC0
void gamelib_clear_screen(void)
{
    tig_window_fill(gamelib_init_info.iso_window_handle, NULL, 0);
    tig_window_display();
    gamelib_invalidate_rect(NULL);
}

// 0x402FE0
const char* gamelib_current_module_name_get(void)
{
    return gamelib_current_module_name;
}

// CE: the module whose data is actually mounted (set by gamelib_mod_load @1143).
// Unlike gamelib_current_module_name, gamelib_reset never clobbers this, so it is
// the reliable answer to "what module is loaded right now".
const char* gamelib_loaded_module_name_get(void)
{
    return byte_5D0EA4;
}

// 0x402FF0
void gamelib_current_mode_name_set(const char* name)
{
    if (name != NULL && *name != '\0') {
        strcpy(gamelib_current_module_name, name);
    }
}

// 0x403030
bool gamelib_save(const char* name, const char* description)
{
    int start_pos = 0;
    int pos;
    tig_timestamp_t start_time;
    tig_timestamp_t time;
    tig_duration_t duration;
    char path[TIG_MAX_PATH];
    TigFile* stream;
    int index;
    unsigned int sentinel = 0xBEEFCAFE;
    int version;
    GameSaveInfo save_info;

    tig_debug_printf("\ngamelib_save(): Saving to File: %s.\n", name);
    tig_timer_now(&start_time);

    in_save = true;

    ui_progressbar_init(MODULE_COUNT + 2);

    if (!tig_file_is_directory("Save\\Current")) {
        tig_debug_printf("gamelib_save(): Error finding folder %s\n", "Save\\Current");
        in_save = false;
        return false;
    }

    strcpy(path, "Save\\Current");
    strcat(path, "\\data.sav");

    stream = tig_file_fopen(path, "wb");
    if (stream == NULL) {
        tig_debug_printf("gamelib_save(): Error creating %s\n", path);
        in_save = false;
        return false;
    }

    version = 25;
    if (tig_file_fwrite(&version, sizeof(version), 1, stream) != 1) {
        tig_debug_printf("gamelib_save(): Error writing version\n");
        tig_file_fclose(stream);
        tig_file_remove(path);
        in_save = false;
        return false;
    }

    tig_file_fgetpos(stream, &start_pos);
    tig_debug_printf("gamelib_save: Start Pos: %lu\n", start_pos);

    ui_progressbar_update(1);

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].save_func != NULL) {
            tig_debug_printf("gamelib_save: Function %d (%s)", index, gamelib_modules[index].name);
            tig_timer_now(&time);

            if (!gamelib_modules[index].save_func(stream)) {
                tig_debug_printf("gamelib_save(): save function %d (%s) failed\n", index, gamelib_modules[index].name);
                break;
            }

            duration = tig_timer_elapsed(time);
            tig_file_fgetpos(stream, &pos);
            tig_debug_printf(" wrote to: %lu, Total: (%lu), Time (ms): %d\n",
                pos,
                pos - start_pos,
                duration);
            start_pos = pos;

            if (tig_file_fwrite(&sentinel, sizeof(sentinel), 1, stream) != 1) {
                tig_debug_printf("gamelib_save(): ERROR: Sentinel Failed to Save!\n");
                break;
            }

            ui_progressbar_update(index + 1);
        }
    }

    tig_file_fclose(stream);

    if (index < MODULE_COUNT) {
        tig_file_remove(path);
        in_save = false;
        return false;
    }

    if (gamelib_extra_save_func != NULL) {
        tig_debug_printf("gamelib_save: Begin gamelib_extra_save_func()...");
        tig_timer_now(&time);
        if (!gamelib_extra_save_func()) {
            tig_file_remove(path);
            in_save = false;
            return false;
        }
        duration = tig_timer_elapsed(time);
        tig_debug_printf("done. Time (ms): %d\n", duration);
    }

    if (!gamelib_saveinfo_init(name, description, &save_info)) {
        tig_debug_printf("gamelib_save(): error creating saveinfo\n");
        in_save = false;
        return false;
    }

    if (!gamelib_saveinfo_save(&save_info)) {
        tig_debug_printf("gamelib_save(): error saving saveinfo\n");
        gamelib_saveinfo_exit(&save_info);
        in_save = false;
        return false;
    }

    gamelib_saveinfo_exit(&save_info);

    snprintf(path, sizeof(path), "save\\%s", name);
    tig_debug_printf("gamelib_save: creating folder archive...");

    tig_timer_now(&time);
    if (!tig_file_archive(path, "Save\\Current")) {
        tig_debug_printf("gamelib_save(): error archiving folder to %s\n", path);
        in_save = false;
        return false;
    }
    duration = tig_timer_elapsed(time);
    tig_debug_printf("done. Time (ms): %d\n", duration);

    ui_progressbar_update(MODULE_COUNT + 2);

    in_save = false;

    duration = tig_timer_elapsed(start_time);
    tig_debug_printf("gamelib_save(): Save Complete.  Total time: %d ms.\n", duration);

    if (gamelib_zoom_perf_enabled && duration > 100) {
        gamelib_perf_log_event("gamelib_save", (uint64_t)duration * 1000000ull);
    }

    return true;
}

// 0x403410
bool gamelib_load(const char* name)
{
    int start_pos = 0;
    int pos;
    tig_timestamp_t start_time;
    tig_timestamp_t time;
    tig_duration_t duration;
    char path[TIG_MAX_PATH];
    TigFile* stream;
    GameLoadInfo load_info;
    int index;
    unsigned int sentinel;

    tig_debug_printf("\ngamelib_load: Loading from File: %s.\n", name);
    tig_timer_now(&start_time);

    in_load = true;

    ui_progressbar_init(MODULE_COUNT + 2);

    if (!tig_file_is_directory("Save\\Current")) {
        tig_debug_printf("gamelib_load(): Error finding folder %s\n", "Save\\Current");
        in_load = false;
        return false;
    }

    snprintf(path, sizeof(path), "save\\%s", name);

    tig_debug_printf("gamelib_load: begin removing files...");
    tig_timer_now(&time);
    if (!tig_file_empty_directory("Save\\Current")) {
        tig_debug_printf("gamelib_load(): Error clearing folder %s\n", "Save\\Current");
        in_load = false;
        return false;
    }
    duration = tig_timer_elapsed(time);
    tig_debug_printf("done.  Time (ms): %d\n", duration);

    tig_debug_printf("gamelib_load: begin restoring folder archive...");
    tig_timer_now(&time);
    if (!tig_file_unarchive(path, "Save\\Current")) {
        tig_debug_printf("gamelib_load(): error restoring archive %s to save\\test\n", path);
        in_load = false;
        return false;
    }
    duration = tig_timer_elapsed(time);
    tig_debug_printf("done.  Time (ms): %d\n", duration);

    stream = tig_file_fopen("Save\\Current\\data.sav", "rb");
    if (stream == NULL) {
        tig_debug_printf("gamelib_load(): Error reading data.sav\n");
        in_load = false;
        return false;
    }

    if (tig_file_fread(&(load_info.version), sizeof(load_info.version), 1, stream) != 1) {
        tig_debug_printf("gamelib_load(): Error reading version\n");
        in_load = false;
        return false;
    }

    load_info.stream = stream;

    tig_file_fgetpos(stream, &start_pos);
    tig_debug_printf("gamelib_load: Start Pos: %lu\n", start_pos);

    ui_progressbar_update(1);

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gamelib_modules[index].load_func != NULL) {
            tig_debug_printf("gamelib_load: Function %d (%s)", index, gamelib_modules[index].name);
            tig_timer_now(&time);

            if (!gamelib_modules[index].load_func(&load_info)) {
                tig_debug_printf("gamelib_load(): load function %d (%s) failed\n", index, gamelib_modules[index].name);
                break;
            }

            duration = tig_timer_elapsed(time);
            tig_file_fgetpos(stream, &pos);
            tig_debug_printf(" read to: %lu, Total: (%lu), Time (ms): %d\n",
                pos,
                pos - start_pos,
                duration);
            start_pos = pos;

            if (tig_file_fread(&sentinel, sizeof(sentinel), 1, load_info.stream) != 1) {
                tig_debug_printf("gamelib_load(): ERROR: Load Sentinel Failed to Load!\n");
                break;
            }

            if (sentinel != 0xBEEFCAFE) {
                tig_debug_printf("gamelib_load(): ERROR: Load Sentinel Failed to Match!\n");
                break;
            }

            ui_progressbar_update(index + 1);
        }
    }

    tig_file_fclose(stream);

    if (index < MODULE_COUNT) {
        in_load = false;
        return false;
    }

    if (gamelib_extra_load_func != NULL) {
        tig_debug_printf("gamelib_load: Begin gamelib_extra_load_func()...");
        tig_timer_now(&time);
        if (!gamelib_extra_load_func()) {
            in_load = false;
            return false;
        }
        duration = tig_timer_elapsed(time);
        tig_debug_printf("done. Time (ms): %d\n", duration);
    }

    ui_progressbar_update(MODULE_COUNT + 2);

    in_load = false;

    duration = tig_timer_elapsed(start_time);
    tig_debug_printf("gamelib_load: Load Complete.  Total time: %d ms.\n", duration);

    if (gamelib_zoom_perf_enabled && duration > 100) {
        gamelib_perf_log_event("gamelib_load", (uint64_t)duration * 1000000ull);
    }

    return true;
}

// 0x403790
// CE: rewritten to be directory-authoritative and cross-module aware.
// The old version assumed `name` was the 8-char slot and that "save\" resolved to
// the right place -- both false now: a save's .gsi is "<slot><description>.gsi"
// while its archive parts are slot-named, and a save may live under any module's
// own save folder (writable per-module model), not the currently-mounted one. The
// old code therefore left the .gsi behind (dead entry lingered in the list) and
// couldn't touch cross-module saves at all. Now: find the directory that physically
// holds this slot's .gsi (any module folder, else data\Save), and remove EVERY
// "<slot>*" file there (.gsi/.bmp/.tfaf/.tfai). The autosave is no longer special-
// cased -- it deletes like any other (it simply regenerates on the next autosave).
bool gamelib_delete(const char* name)
{
    GameModuleList module_list;
    TigFileList file_list;
    unsigned int index;
    char slot[16];
    char dir[TIG_MAX_PATH];
    char glob[TIG_MAX_PATH];
    char path[TIG_MAX_PATH];
    bool found = false;

    strncpy(slot, name, 8);
    slot[8] = '\0';

    // Locate the directory holding this save: a module's own save folder (where its
    // directory-resolved module lives), else the legacy/default data\Save.
    gamelib_modlist_create(&module_list, 0);
    for (index = 0; index < module_list.count && !found; index++) {
        snprintf(glob, sizeof(glob), ".\\Modules\\%s\\save\\%s*.gsi", module_list.paths[index], slot);
        tig_file_list_create(&file_list, glob);
        if (file_list.count > 0) {
            snprintf(dir, sizeof(dir), ".\\Modules\\%s\\save", module_list.paths[index]);
            found = true;
        }
        tig_file_list_destroy(&file_list);
    }
    gamelib_modlist_destroy(&module_list);

    if (!found) {
        snprintf(glob, sizeof(glob), ".\\data\\Save\\%s*.gsi", slot);
        tig_file_list_create(&file_list, glob);
        if (file_list.count > 0) {
            snprintf(dir, sizeof(dir), ".\\data\\Save");
            found = true;
        }
        tig_file_list_destroy(&file_list);
    }

    if (!found) {
        // Last resort: the repo-resolved current save dir.
        snprintf(dir, sizeof(dir), "save");
    }

    snprintf(glob, sizeof(glob), "%s\\%s*.*", dir, slot);
    tig_file_list_create(&file_list, glob);
    for (index = 0; index < file_list.count; index++) {
        compat_join_path(path, sizeof(path), dir, file_list.entries[index].path);
        tig_file_remove(path);
    }
    tig_file_list_destroy(&file_list);

    return true;
}

// 0x403850
const char* gamelib_last_save_name(void)
{
    // 0x5D0D88
    static char byte_5D0D88[TIG_MAX_PATH];

    // 0x5D10C8
    static GameSaveList save_list;

    byte_5D0D88[0] = '\0';
    gamelib_savelist_create(&save_list);
    gamelib_savelist_sort(&save_list, GAME_SAVE_LIST_ORDER_DATE, true);

    if (save_list.count > 0) {
        strcpy(byte_5D0D88, save_list.names[0]);
    }

    // FIX: Memory leak.
    gamelib_savelist_destroy(&save_list);

    return byte_5D0D88;
}

// 0x4038C0
bool gamelib_in_save(void)
{
    return in_save;
}

// 0x4038D0
bool gamelib_in_load(void)
{
    return in_load;
}

// CE: see gamelib_loading_active. The load UI brackets a load (including the module
// switch it does first) with these so gameinit_reset can skip the throwaway fresh
// game that would otherwise leak start-map state into the save being loaded.
void gamelib_loading_active_set(bool active)
{
    gamelib_loading_active = active;
}

bool gamelib_loading_active_get(void)
{
    return gamelib_loading_active;
}

// 0x4038E0
void gamelib_set_extra_save_func(GameExtraSaveFunc func)
{
    gamelib_extra_save_func = func;
}

// 0x4038F0
void gamelib_set_extra_load_func(GameExtraLoadFunc func)
{
    gamelib_extra_load_func = func;
}

// 0x403900
void gamelib_savelist_create(GameSaveList* save_list)
{
    TigFileList file_list;
    unsigned int index;
    char fname[COMPAT_MAX_FNAME];
    char ext[COMPAT_MAX_EXT];

    save_list->count = 0;
    save_list->names = NULL;
    save_list->module = NULL;
    save_list->entry_modules = NULL;

    tig_file_list_create(&file_list, "save\\slot*.*");

    for (index = 0; index < file_list.count; index++) {
        compat_splitpath(file_list.entries[index].path, NULL, NULL, fname, ext);
        if (SDL_strcasecmp(ext, ".gsi") == 0) {
            save_list->names = (char**)REALLOC(save_list->names, sizeof(save_list->names) * (save_list->count + 1));
            save_list->names[save_list->count++] = STRDUP(fname);
        }
    }

    tig_file_list_destroy(&file_list);
}

// 0x4039E0
void gamelib_savelist_create_module(const char* module, GameSaveList* save_list)
{
    TigFileList file_list;
    unsigned int index;
    char fname[COMPAT_MAX_FNAME];
    char ext[COMPAT_MAX_EXT];
    char path[TIG_MAX_PATH];

    save_list->count = 0;
    save_list->names = NULL;
    save_list->module = STRDUP(module);
    save_list->entry_modules = NULL;

    snprintf(path, sizeof(path), ".\\Modules\\%s\\save\\slot*.*", module);
    tig_file_list_create(&file_list, path);

    for (index = 0; index < file_list.count; index++) {
        compat_splitpath(file_list.entries[index].path, NULL, NULL, fname, ext);
        if (SDL_strcasecmp(ext, ".gsi") == 0) {
            save_list->names = (char**)REALLOC(save_list->names, sizeof(save_list->names) * (save_list->count + 1));
            save_list->names[save_list->count++] = STRDUP(fname);
        }
    }

    tig_file_list_destroy(&file_list);
}

// CE: aggregate every save the player could load -- the legacy/default location
// (data\Save, resolved through the repo stack as "save\") plus each installed
// module's own modules\<M>\save folder -- into one list, with per-entry owning
// module. data\ saves are tagged with the default module; module-folder saves are
// tagged with that folder's module (directory-authoritative, ignoring the possibly
// wrong .gsi stamp). Lets the Load menu show all saves at once and auto-switch to
// the right module on load. Backward compatible: pre-existing data\Save saves and
// per-module saves both appear.
static void gamelib_savelist_append_scan(GameSaveList* save_list, const char* glob, const char* owning_module)
{
    TigFileList file_list;
    unsigned int index;
    char fname[COMPAT_MAX_FNAME];
    char ext[COMPAT_MAX_EXT];

    tig_file_list_create(&file_list, glob);

    for (index = 0; index < file_list.count; index++) {
        compat_splitpath(file_list.entries[index].path, NULL, NULL, fname, ext);
        if (SDL_strcasecmp(ext, ".gsi") == 0) {
            save_list->names = (char**)REALLOC(save_list->names, sizeof(*save_list->names) * (save_list->count + 1));
            save_list->entry_modules = (char**)REALLOC(save_list->entry_modules, sizeof(*save_list->entry_modules) * (save_list->count + 1));
            save_list->names[save_list->count] = STRDUP(fname);
            save_list->entry_modules[save_list->count] = owning_module != NULL ? STRDUP(owning_module) : NULL;
            save_list->count++;
        }
    }

    tig_file_list_destroy(&file_list);
}

void gamelib_savelist_create_all(GameSaveList* save_list)
{
    GameModuleList module_list;
    unsigned int index;
    char glob[TIG_MAX_PATH];
    const char* default_module = gamelib_default_module_name_get();

    save_list->count = 0;
    save_list->names = NULL;
    save_list->module = NULL;
    save_list->entry_modules = NULL;

    // Legacy / default save location: data\Save. Scanned by explicit path (NOT the
    // repo-resolved "save\", which would also pull in whatever module is mounted
    // right now and mis-tag those saves as the default module). Tag with the default
    // module so loading switches there if needed.
    gamelib_savelist_append_scan(save_list, ".\\data\\Save\\slot*.*", default_module);

    // Each installed module's own per-module save folder (the writable-mount /
    // upstream model -- where new saves land). The default module is included too:
    // its loose-dir saves live in modules\<default>\save, distinct from the legacy
    // data\Save scanned above. Directory is authoritative for the tag.
    gamelib_modlist_create(&module_list, 0);
    for (index = 0; index < module_list.count; index++) {
        snprintf(glob, sizeof(glob), ".\\Modules\\%s\\save\\slot*.*", module_list.paths[index]);
        gamelib_savelist_append_scan(save_list, glob, module_list.paths[index]);
    }
    gamelib_modlist_destroy(&module_list);
}

// CE: which module does save <name> belong to? Directory-authoritative: if it
// lives under modules\<M>\save it is module M; otherwise it is the default module
// (data\Save). Independent of the .gsi module stamp (which can be wrong).
//
// `name` is the 8-char slot (e.g. "Slot0020") -- what callers pass to gamelib_load.
// The .gsi on disk is named <slot><description> ("Slot0020Vtown.gsi"), so we GLOB
// "<slot>*.gsi" rather than testing "<slot>.gsi" (which never matches a save that
// has a description -- the bug that made auto-switch silently never fire).
bool gamelib_find_save_module(const char* name, char* out_module, size_t out_size)
{
    GameModuleList module_list;
    TigFileList file_list;
    unsigned int index;
    char glob[TIG_MAX_PATH];
    bool found = false;

    gamelib_modlist_create(&module_list, 0);
    for (index = 0; index < module_list.count && !found; index++) {
        snprintf(glob, sizeof(glob), ".\\Modules\\%s\\save\\%s*.gsi", module_list.paths[index], name);
        tig_file_list_create(&file_list, glob);
        if (file_list.count > 0) {
            snprintf(out_module, out_size, "%s", module_list.paths[index]);
            found = true;
        }
        tig_file_list_destroy(&file_list);
    }
    gamelib_modlist_destroy(&module_list);

    if (found) {
        return true;
    }

    // Not in any module folder -- the legacy/default location (data\Save),
    // reachable through the repo stack as "save\<slot>*.gsi".
    snprintf(glob, sizeof(glob), "save\\%s*.gsi", name);
    tig_file_list_create(&file_list, glob);
    found = file_list.count > 0;
    tig_file_list_destroy(&file_list);
    if (found) {
        snprintf(out_module, out_size, "%s", gamelib_default_module_name_get());
        return true;
    }

    return false;
}

// CE: load a save's GameSaveInfo given only its 8-char SLOT (what the load flow
// has), recovering the full <slot><description> base name the .gsi is stored under.
// The save's module must already be mounted so "save\" resolves to its folder.
bool gamelib_saveinfo_load_by_slot(const char* slot, GameSaveInfo* save_info)
{
    TigFileList file_list;
    unsigned int index;
    char glob[TIG_MAX_PATH];
    char base[TIG_MAX_PATH];
    char fname[COMPAT_MAX_FNAME];
    char ext[COMPAT_MAX_EXT];
    bool have_base = false;

    snprintf(glob, sizeof(glob), "save\\%s*.gsi", slot);
    tig_file_list_create(&file_list, glob);
    for (index = 0; index < file_list.count; index++) {
        compat_splitpath(file_list.entries[index].path, NULL, NULL, fname, ext);
        if (SDL_strcasecmp(ext, ".gsi") == 0) {
            snprintf(base, sizeof(base), "%s", fname);
            have_base = true;
            break;
        }
    }
    tig_file_list_destroy(&file_list);

    if (!have_base) {
        return false;
    }

    return gamelib_saveinfo_load(base, save_info);
}

// CE: fill entry_modules for an already-built list (e.g. the plain create() list the
// Save menu uses) by resolving each save's owning module from disk. Lets the Save
// menu show the same "[Module]" tags as the Load menu without changing which saves
// it lists or where a save is written. Allocates entry_modules if absent; the array
// and its strings are freed by gamelib_savelist_destroy.
void gamelib_savelist_tag_modules(GameSaveList* save_list)
{
    unsigned int index;
    char slot[16];
    char module[TIG_MAX_PATH];

    if (save_list->count == 0) {
        return;
    }

    if (save_list->entry_modules == NULL) {
        save_list->entry_modules = (char**)MALLOC(sizeof(*save_list->entry_modules) * save_list->count);
        for (index = 0; index < save_list->count; index++) {
            save_list->entry_modules[index] = NULL;
        }
    }

    for (index = 0; index < save_list->count; index++) {
        strncpy(slot, save_list->names[index], 8);
        slot[8] = '\0';

        if (save_list->entry_modules[index] != NULL) {
            FREE(save_list->entry_modules[index]);
            save_list->entry_modules[index] = NULL;
        }

        if (gamelib_find_save_module(slot, module, sizeof(module))) {
            save_list->entry_modules[index] = STRDUP(module);
        }
    }
}

// 0x403BB0
void gamelib_savelist_destroy(GameSaveList* save_list)
{
    unsigned int index;

    for (index = 0; index < save_list->count; index++) {
        FREE(save_list->names[index]);
        if (save_list->entry_modules != NULL && save_list->entry_modules[index] != NULL) {
            FREE(save_list->entry_modules[index]);
        }
    }

    if (save_list->names != NULL) {
        FREE(save_list->names);
    }

    if (save_list->entry_modules != NULL) {
        FREE(save_list->entry_modules);
    }

    save_list->count = 0;
    save_list->names = NULL;
    save_list->entry_modules = NULL;

    if (save_list->module != NULL) {
        FREE(save_list->module);
    }

    save_list->module = NULL;
}

// 0x403C10
void gamelib_savelist_sort(GameSaveList* save_list, GameSaveListOrder order, bool a3)
{
    GameSaveEntry* entries;
    unsigned int index;
    char path[TIG_MAX_PATH];
    TigFileInfo file_info;

    gamelib_savelist_sort_check_autosave = !a3;

    entries = (GameSaveEntry*)MALLOC(sizeof(*entries) * save_list->count);

    for (index = 0; index < save_list->count; index++) {
        // CE: per-entry owning module (aggregated lists) takes precedence over the
        // whole-list module (create_module); NULL falls back to the repo-resolved
        // "save\" location. Try the module folder first, then "save\" -- this finds
        // both module-folder saves and data\Save saves (default-module-tagged
        // entries whose module folder has no such file fall through to "save\").
        const char* entry_module;
        bool resolved = false;

        entries[index].path = save_list->names[index];
        entries[index].module = (save_list->entry_modules != NULL)
            ? save_list->entry_modules[index]
            : NULL;

        entry_module = entries[index].module != NULL ? entries[index].module : save_list->module;

        if (entry_module != NULL) {
            snprintf(path, sizeof(path),
                ".\\Modules\\%s\\save\\%s.gsi",
                entry_module,
                save_list->names[index]);
            resolved = tig_file_exists(path, &file_info);
        }

        if (!resolved) {
            snprintf(path, sizeof(path),
                "save\\%s.gsi",
                save_list->names[index]);
            resolved = tig_file_exists(path, &file_info);
        }

        if (!resolved) {
            tig_debug_printf("GameLib: : ERROR: Couldn't find file!\n");
            // FIX: Memory leak.
            FREE(entries);
            return;
        }

        entries[index].modify_time = file_info.modify_time;
    }

    switch (order) {
    case GAME_SAVE_LIST_ORDER_DATE:
        qsort(entries, save_list->count, sizeof(*entries), game_save_entry_compare_by_date);
        break;
    case GAME_SAVE_LIST_ORDER_NAME:
        qsort(entries, save_list->count, sizeof(*entries), game_save_entry_compare_by_name);
        break;
    }

    for (index = 0; index < save_list->count; index++) {
        save_list->names[index] = entries[index].path;
        if (save_list->entry_modules != NULL) {
            save_list->entry_modules[index] = entries[index].module;
        }
    }

    FREE(entries);
}

// 0x403D40
int game_save_entry_compare_by_date(const void* va, const void* vb)
{
    const GameSaveEntry* a = (const GameSaveEntry*)va;
    const GameSaveEntry* b = (const GameSaveEntry*)vb;

    if (gamelib_savelist_sort_check_autosave) {
        if (SDL_toupper(a->path[4]) == 'A') {
            return -1;
        }

        if (SDL_toupper(b->path[4]) == 'A') {
            return 1;
        }
    }

    if (a->modify_time < b->modify_time) {
        return 1;
    } else if (a->modify_time > b->modify_time) {
        return -1;
    } else {
        return 0;
    }
}

// 0x403DB0
int game_save_entry_compare_by_name(const void* va, const void* vb)
{
    const GameSaveEntry* a = (const GameSaveEntry*)va;
    const GameSaveEntry* b = (const GameSaveEntry*)vb;

    return -SDL_strncasecmp(a->path, b->path, 8);
}

// 0x403DD0
bool gamelib_saveinfo_init(const char* name, const char* description, GameSaveInfo* save_info)
{
    int64_t pc_obj;
    TigVideoBufferCreateInfo vb_create_info;
    TigWindowBlitInfo win_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    char* pc_name;

    if (save_info == NULL) {
        return false;
    }

    pc_obj = player_get_local_pc_obj();

    save_info->version = 25;
    strcpy(save_info->name, name);
    strcpy(save_info->module_name, gamelib_current_module_name_get());

    save_info->thumbnail_video_buffer = NULL;
    vb_create_info.width = gamelib_thumbnail_width;
    vb_create_info.height = gamelib_thumbnail_height;
    vb_create_info.flags = 0;
    vb_create_info.background_color = 0;
    if (tig_video_buffer_create(&vb_create_info, &(save_info->thumbnail_video_buffer)) != TIG_OK) {
        return false;
    }

    // CE: Source rect for thumbnails is always centered 800x600 regardless of
    // the actual window size. This makes thubnails taken at any resolution show
    // the same area.
    src_rect.x = (gamelib_iso_content_rect.width - 800) / 2;
    src_rect.y = (gamelib_iso_content_rect.height - 600) / 2;
    src_rect.width = 800;
    src_rect.height = 600;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = gamelib_thumbnail_width;
    dst_rect.height = gamelib_thumbnail_height;

    win_blit_info.type = TIG_WINDOW_BLT_WINDOW_TO_VIDEO_BUFFER;
    win_blit_info.src_window_handle = gamelib_init_info.iso_window_handle;
    win_blit_info.src_rect = &src_rect;
    win_blit_info.dst_video_buffer = save_info->thumbnail_video_buffer;
    win_blit_info.dst_rect = &dst_rect;
    win_blit_info.vb_blit_flags = TIG_VIDEO_BUFFER_BLIT_SCALE_LINEAR;
    if (tig_window_blit(&win_blit_info) != TIG_OK) {
        tig_debug_printf("gamelib: ERROR: Build thumbnail FAILED to Blit!\n");
        if (tig_video_buffer_destroy(save_info->thumbnail_video_buffer) != TIG_OK) {
            tig_debug_printf("gamelib: ERROR: Build thumbnail FAILED to destroy VBid!\n");
        }
        save_info->thumbnail_video_buffer = NULL;
        return false;
    }

    obj_field_string_get(pc_obj, OBJ_F_PC_PLAYER_NAME, &pc_name);
    strcpy(save_info->pc_name, pc_name);
    FREE(pc_name);

    save_info->map = map_current_map();
    save_info->pc_portrait = portrait_get(pc_obj);
    save_info->pc_level = stat_level_get(pc_obj, STAT_LEVEL);
    save_info->pc_location = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    save_info->story_state = script_story_state_get();
    strcpy(save_info->description, description);
    save_info->datetime = sub_45A7C0();

    return true;
}

// 0x403FF0
void gamelib_saveinfo_exit(GameSaveInfo* save_info)
{
    if (save_info->thumbnail_video_buffer != NULL) {
        tig_video_buffer_destroy(save_info->thumbnail_video_buffer);
    }
}

// 0x404010
bool gamelib_saveinfo_save(GameSaveInfo* save_info)
{
    char path[TIG_MAX_PATH];
    TigFile* stream;
    TigVideoBufferSaveToBmpInfo to_bmp_info;
    TigRect rect;
    bool success = false;
    int size;

    snprintf(path, sizeof(path), "save\\%s%s.gsi", save_info->name, save_info->description);

    stream = tig_file_fopen(path, "wb");
    if (stream == NULL) {
        return false;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = gamelib_thumbnail_width;
    rect.height = gamelib_thumbnail_height;

    to_bmp_info.flags = 0;
    to_bmp_info.video_buffer = save_info->thumbnail_video_buffer;
    to_bmp_info.rect = &rect;
    snprintf(to_bmp_info.path, sizeof(to_bmp_info.path), "save\\%s.bmp", save_info->name);

    if (tig_video_buffer_save_to_bmp(&to_bmp_info) == TIG_OK) {
        do {
            if (tig_file_fwrite(&(save_info->version), sizeof(save_info->version), 1, stream) != 1) break;

            size = (int)strlen(save_info->module_name);
            if (tig_file_fwrite(&size, sizeof(size), 1, stream) != 1) break;
            if (tig_file_fwrite(save_info->module_name, size, 1, stream) != 1) break;

            size = (int)strlen(save_info->pc_name);
            if (tig_file_fwrite(&size, sizeof(size), 1, stream) != 1) break;
            if (tig_file_fwrite(save_info->pc_name, size, 1, stream) != 1) break;

            if (tig_file_fwrite(&(save_info->map), sizeof(save_info->map), 1, stream) != 1) break;
            if (tig_file_fwrite(&(save_info->datetime), sizeof(save_info->datetime), 1, stream) != 1) break;
            if (tig_file_fwrite(&(save_info->pc_portrait), sizeof(save_info->pc_portrait), 1, stream) != 1) break;
            if (tig_file_fwrite(&(save_info->pc_level), sizeof(save_info->pc_level), 1, stream) != 1) break;
            if (tig_file_fwrite(&(save_info->pc_location), sizeof(save_info->pc_location), 1, stream) != 1) break;
            if (tig_file_fwrite(&(save_info->story_state), sizeof(save_info->story_state), 1, stream) != 1) break;

            size = (int)strlen(save_info->description);
            if (tig_file_fwrite(&size, sizeof(size), 1, stream) != 1) break;
            if (size != 0 && tig_file_fwrite(save_info->description, size, 1, stream) != 1) break;

            success = true;
        } while (0);
    }

    tig_file_fclose(stream);

    return success;
}

// 0x404270
// CE: core saveinfo reader, parameterized by the save DIRECTORY prefix so a save can
// be previewed from a module folder that isn't the currently-mounted one. save_dir
// is "save" (repo-resolved current context) or an explicit ".\Modules\<M>\save" /
// ".\data\Save". The .gsi is "<name>.gsi" (name = full <slot><description>); the
// thumbnail is "<slot>.bmp" (slot = first 8 chars).
static bool gamelib_saveinfo_load_from(const char* save_dir, const char* name, GameSaveInfo* save_info)
{
    TigFile* stream;
    int size;
    bool success = false;
    char gsi_path[TIG_MAX_PATH];
    char bmp_path[TIG_MAX_PATH];
    char slot[16];

    snprintf(gsi_path, sizeof(gsi_path), "%s\\%s.gsi", save_dir, name);

    stream = tig_file_fopen(gsi_path, "rb");
    if (stream == NULL) {
        return false;
    }

    save_info->thumbnail_video_buffer = NULL;

    strncpy(slot, name, 8);
    slot[8] = '\0';
    snprintf(bmp_path, sizeof(bmp_path), "%s\\%s.bmp", save_dir, slot);

    do {
        if (tig_video_buffer_load_from_bmp(bmp_path, &save_info->thumbnail_video_buffer, 0x1) != TIG_OK) break;

        if (tig_file_fread(&(save_info->version), sizeof(save_info->version), 1, stream) != 1) break;

        // CE: the .gsi stores each string as length + raw bytes with NO terminator.
        // GameSaveInfo (mainmenu_ui_gsi) is reused across selections, so without
        // NUL-terminating, a shorter string leaves the PREVIOUS (longer) one's tail in
        // the buffer -- e.g. selecting "Bridge" after "...Glitch...Nighttime" showed
        // "Bridgeave-Glitchhttime". Bound-check + terminate each (also closes a
        // pre-existing fread buffer overflow). [Was the 48d361f6 fix, reverted; redone.]
        if (tig_file_fread(&size, sizeof(size), 1, stream) != 1) break;
        if (size < 0 || size >= (int)sizeof(save_info->module_name)) break;
        if (tig_file_fread(save_info->module_name, size, 1, stream) != 1) break;
        save_info->module_name[size] = '\0';

        if (tig_file_fread(&size, sizeof(size), 1, stream) != 1) break;
        if (size < 0 || size >= (int)sizeof(save_info->pc_name)) break;
        if (tig_file_fread(save_info->pc_name, size, 1, stream) != 1) break;
        save_info->pc_name[size] = '\0';

        if (tig_file_fread(&(save_info->map), sizeof(save_info->map), 1, stream) != 1) break;
        if (tig_file_fread(&(save_info->datetime), sizeof(save_info->datetime), 1, stream) != 1) break;
        if (tig_file_fread(&(save_info->pc_portrait), sizeof(save_info->pc_portrait), 1, stream) != 1) break;
        if (tig_file_fread(&(save_info->pc_level), sizeof(save_info->pc_level), 1, stream) != 1) break;
        if (tig_file_fread(&(save_info->pc_location), sizeof(save_info->pc_location), 1, stream) != 1) break;
        if (tig_file_fread(&(save_info->story_state), sizeof(save_info->story_state), 1, stream) != 1) break;

        if (tig_file_fread(&size, sizeof(size), 1, stream) != 1) break;
        if (size < 0 || size >= (int)sizeof(save_info->description)) break;
        if (size != 0 && tig_file_fread(save_info->description, size, 1, stream) != 1) break;
        save_info->description[size] = '\0';

        success = true;
    } while (0);

    tig_file_fclose(stream);

    return success;
}

bool gamelib_saveinfo_load(const char* name, GameSaveInfo* save_info)
{
    return gamelib_saveinfo_load_from("save", name, save_info);
}

// CE: load saveinfo for a save that may belong to a module that ISN'T currently
// mounted, by first locating the directory that physically holds it (any module's
// save folder, else data\Save), then reading from there. The Load/Save menus use
// this so the detail panel (thumbnail, PC name, map, level...) is correct regardless
// of which module is mounted -- previously the repo-resolved "save\" read showed a
// same-slot save from the current module, or stale/empty info.
bool gamelib_saveinfo_load_located(const char* name, GameSaveInfo* save_info)
{
    GameModuleList module_list;
    TigFileList file_list;
    unsigned int index;
    char slot[16];
    char dir[TIG_MAX_PATH];
    char glob[TIG_MAX_PATH];
    bool found = false;

    strncpy(slot, name, 8);
    slot[8] = '\0';

    gamelib_modlist_create(&module_list, 0);
    for (index = 0; index < module_list.count && !found; index++) {
        snprintf(glob, sizeof(glob), ".\\Modules\\%s\\save\\%s*.gsi", module_list.paths[index], slot);
        tig_file_list_create(&file_list, glob);
        if (file_list.count > 0) {
            snprintf(dir, sizeof(dir), ".\\Modules\\%s\\save", module_list.paths[index]);
            found = true;
        }
        tig_file_list_destroy(&file_list);
    }
    gamelib_modlist_destroy(&module_list);

    if (!found) {
        snprintf(glob, sizeof(glob), ".\\data\\Save\\%s*.gsi", slot);
        tig_file_list_create(&file_list, glob);
        if (file_list.count > 0) {
            snprintf(dir, sizeof(dir), ".\\data\\Save");
            found = true;
        }
        tig_file_list_destroy(&file_list);
    }

    if (!found) {
        snprintf(dir, sizeof(dir), "save");
    }

    return gamelib_saveinfo_load_from(dir, name, save_info);
}

// 0x4044A0
void gamelib_thumbnail_size_set(int width, int height)
{
    gamelib_thumbnail_width = width;
    gamelib_thumbnail_height = height;
}

// 0x404570
void difficulty_changed(void)
{
    gamelib_game_difficulty = settings_get_value(&settings, DIFFICULTY_KEY);
}

// CE: push the dialogue-emote-dim config to the tig font layer.
void dialogue_emote_dim_changed(void)
{
    bool master = settings_get_value(&settings, DIALOGUE_EMOTE_DIM_KEY) != 0;
    bool choices = settings_get_value(&settings, DIALOGUE_EMOTE_DIM_CHOICES_KEY) != 0;
    // Primary channel = NPC speech bubbles; alt channel = PC choices.
    tig_font_dim_brackets_set_enabled(master);
    tig_font_dim_brackets_set_alt_enabled(master && choices);
    tig_font_dim_brackets_set_percent(settings_get_value(&settings, DIALOGUE_EMOTE_DIM_PERCENT_KEY));
}

// 0x404590
int gamelib_game_difficulty_get(void)
{
    return gamelib_game_difficulty;
}

// 0x4045A0
void gamelib_redraw(void)
{
    li_redraw();
    ci_redraw();
    gamelib_invalidate_rect(NULL);
    gamelib_draw();
    tig_window_invalidate_rect(NULL);
}

// 0x4045D0
bool gamelib_copy_version(char* long_version, char* short_version, char* locale)
{
    if (long_version != NULL) {
        snprintf(long_version,
            GAMELIB_LONG_VERSION_LENGTH - 1,
            "Arcanum %d.%d.%d.%d %s",
            VERSION_MAJOR,
            VERSION_MINOR,
            VERSION_PATCH,
            VERSION_BUILD,
            gamelib_get_locale());
    }

    if (short_version != NULL) {
        snprintf(short_version,
            GAMELIB_SHORT_VERSION_LENGTH - 1,
            "%d.%d.%d.%d",
            VERSION_MAJOR,
            VERSION_MINOR,
            VERSION_PATCH,
            VERSION_BUILD);
    }

    if (locale != NULL) {
        strncpy(locale, gamelib_get_locale(), GAMELIB_LOCALE_LENGTH - 1);
    }

    return true;
}

// 0x404640
void gamelib_patch_lvl_set(const char* patch_lvl)
{
    (void)patch_lvl;
}

void gamelib_get_iso_content_rect(TigRect* rect)
{
    *rect = gamelib_iso_content_rect;
}

// 0x404650
const char* gamelib_get_locale(void)
{
    // 0x5D0D84
    static char locale[GAMELIB_LOCALE_LENGTH];

    mes_file_handle_t mes_file;
    MesFileEntry mes_file_entry;

    if (mes_load("mes\\language.mes", &mes_file)) {
        mes_file_entry.num = 1;
        if (mes_search(mes_file, &mes_file_entry)) {
            mes_get_msg(mes_file, &mes_file_entry);
            strcpy(locale, mes_file_entry.str);
            mes_unload(mes_file);
            return locale;
        }

        mes_unload(mes_file);
    }

    return "en";
}

// CE: Read the RECENTER_CAMERA_ON_OVERLAY_KEY setting. Wraps the int
// settings value as a bool — the overlay screens use this to decide
// whether to snap the camera back to the PC on open.
bool gamelib_recenter_camera_on_overlay(void)
{
    return settings_get_value(&settings, RECENTER_CAMERA_ON_OVERLAY_KEY) != 0;
}

// CE: Read the PC_LENS_FOLLOWS_PLAYER_KEY setting.
bool gamelib_pc_lens_follows_player(void)
{
    return settings_get_value(&settings, PC_LENS_FOLLOWS_PLAYER_KEY) != 0;
}

// CE: Read the GOLD_EXPANSION_SLOT_PRIORITY_KEY setting.
bool gamelib_gold_expansion_slot_priority(void)
{
    return settings_get_value(&settings, GOLD_EXPANSION_SLOT_PRIORITY_KEY) != 0;
}

// CE: Read the VOID_EDGE_FADE_KEY setting.
bool gamelib_void_edge_fade(void)
{
    return settings_get_value(&settings, VOID_EDGE_FADE_KEY) != 0;
}

// 0x4046F0
void gamelib_draw_game(GameDrawInfo* draw_info)
{
    if (tig_video_3d_begin_scene() == TIG_OK) {
        // Gate pass timing on warmup too — same cold-cache outlier
        // applies to the very first render after F9 on.
        bool perf_on = gamelib_zoom_perf_enabled && gamelib_zoom_perf_warmed_up;
        uint64_t t0;

        t0 = perf_on ? gamelib_zoom_perf_now_ns() : 0;
        light_draw(draw_info);
        if (perf_on) {
            uint64_t d = gamelib_zoom_perf_now_ns() - t0;
            gamelib_zoom_perf_pass_light_total_ns += d;
            if (d > gamelib_zoom_perf_pass_light_max_ns) gamelib_zoom_perf_pass_light_max_ns = d;
            t0 = gamelib_zoom_perf_now_ns();
        }
        tile_draw(draw_info);
        if (perf_on) {
            uint64_t d = gamelib_zoom_perf_now_ns() - t0;
            gamelib_zoom_perf_pass_tile_total_ns += d;
            if (d > gamelib_zoom_perf_pass_tile_max_ns) gamelib_zoom_perf_pass_tile_max_ns = d;
        }
        sub_43C690(draw_info);  // bucketed in 'tile' bucket above-or-below if we cared; here it falls between
        if (perf_on) t0 = gamelib_zoom_perf_now_ns();
        object_draw(draw_info);
        if (perf_on) {
            uint64_t d = gamelib_zoom_perf_now_ns() - t0;
            gamelib_zoom_perf_pass_object_total_ns += d;
            if (d > gamelib_zoom_perf_pass_object_max_ns) gamelib_zoom_perf_pass_object_max_ns = d;
            t0 = gamelib_zoom_perf_now_ns();
        }
        roof_draw(draw_info);
        if (perf_on) {
            uint64_t d = gamelib_zoom_perf_now_ns() - t0;
            gamelib_zoom_perf_pass_roof_total_ns += d;
            if (d > gamelib_zoom_perf_pass_roof_max_ns) gamelib_zoom_perf_pass_roof_max_ns = d;
            gamelib_zoom_perf_pass_samples++;
        }
        if (!gamelib_zoom_world_pass_active) {
            tb_draw(draw_info);
            tf_draw(draw_info);
            tc_draw(draw_info);
        }
        tig_video_3d_end_scene();
    }
}

// CE: Render a 1:1 iso view centred on `center_loc` into `target_vb`,
// covering a (width x height) region, then restore all global render
// state so the main view is untouched. Only the world passes run
// (light / tile / object / roof) — no speech bubbles, floating text or
// combat UI.
//
// The PC lens needs this when "pc lens follows player" is on: the lens
// normally just copies pixels out of the already-rendered main view, but
// when the PC is scrolled or zoomed off that view there are no such
// pixels to copy. Here the lens gets its OWN little PC-centred render,
// rendered at 1:1 regardless of the main camera's zoom/scroll, so it
// always shows the player.
//
// Cheap by construction: the rendered region is only lens-sized, and the
// caller only invokes this in that one opt-in mode while an overlay is
// open. Must be called outside the main draw (it swaps the iso window's
// render target); iso_redraw() does exactly that (draw, then lens).
//
// Returns false (rendering nothing) when not in a normal game session.
bool gamelib_render_lens_view(TigVideoBuffer* target_vb, int64_t center_obj, int width, int height)
{
    int64_t saved_ox;
    int64_t saved_oy;
    TigRect saved_content;
    TigRect sprite_rect;
    int64_t center_x;
    int64_t center_y;
    TigRect region;
    TigRect region_ex;
    LocRect loc_rect;
    bool ok = false;

    if (target_vb == NULL
        || center_obj == OBJ_HANDLE_NULL
        || gamelib_draw_func != gamelib_draw_game
        || gamelib_view_options.type != VIEW_TYPE_ISOMETRIC) {
        return false;
    }

    location_origin_get(&saved_ox, &saved_oy);
    saved_content = gamelib_iso_content_rect;

    // Origin so the PC SPRITE's bounding-box centre lands at the lens
    // centre. Centring on the tile alone frames the feet/base, leaving the
    // sprite (which rises UP from its tile) to clip its head at the top —
    // so use the full sprite rect (0x7 = the same extent the dirty-rect
    // path uses) and centre on its middle. Computed at origin (0, 0); we
    // then offset origin so that centre maps to the lens centre.
    location_origin_pixel_set(0, 0);
    object_get_rect(center_obj, 0x7, &sprite_rect);
    if (sprite_rect.width > 0 && sprite_rect.height > 0) {
        center_x = (int64_t)sprite_rect.x + sprite_rect.width / 2;
        center_y = (int64_t)sprite_rect.y + sprite_rect.height / 2;
    } else {
        // Sprite rect unavailable (flagged invisible / off-map) — fall back
        // to the tile centre (+40/+20 within the iso tile).
        int64_t fx;
        int64_t fy;
        location_xy(obj_field_int64_get(center_obj, OBJ_F_LOCATION), &fx, &fy);
        center_x = fx + 40;
        center_y = fy + 20;
    }
    location_origin_pixel_set((int64_t)(width / 2) - center_x,
        (int64_t)(height / 2) - center_y);

    region.x = 0;
    region.y = 0;
    region.width = width;
    region.height = height;
    // Overdraw margin so tall sprites straddling the edge still draw.
    region_ex.x = -256;
    region_ex.y = -256;
    region_ex.width = width + 512;
    region_ex.height = height + 512;

    // Redirect the world renderers at the lens VB: tiles via the tile
    // render target, objects/light/roof via the iso window's VB.
    tig_window_set_video_buffer(gamelib_init_info.iso_window_handle, target_vb);
    tile_set_render_target(target_vb);
    object_set_iso_content_rect(&region);
    light_set_iso_content_rect(&region);

    // Clear first so void / map-edge gaps don't show stale lens pixels.
    tig_window_fill(gamelib_init_info.iso_window_handle, &region, tig_color_make(0, 0, 0));

    if (location_screen_rect_to_loc_rect(&region_ex, &loc_rect)) {
        SectorRect sector_rect;
        SectorListNode* sectors;
        TigRectListNode* dirty;
        GameDrawInfo draw_info;

        sector_rect_from_loc_rect(&loc_rect, &sector_rect);
        sectors = sector_list_create(&loc_rect);

        dirty = tig_rect_node_create();
        dirty->rect = region;
        dirty->next = NULL;

        draw_info.screen_rect = &region_ex;
        draw_info.loc_rect = &loc_rect;
        draw_info.sector_rect = &sector_rect;
        draw_info.sectors = sectors;
        draw_info.rects = &dirty;

        if (tig_video_3d_begin_scene() == TIG_OK) {
            light_draw(&draw_info);
            tile_draw(&draw_info);
            sub_43C690(&draw_info);
            object_draw(&draw_info);
            roof_draw(&draw_info);
            tig_video_3d_end_scene();
            ok = true;
        }

        sector_list_destroy(sectors);
        tig_rect_node_destroy(dirty);
    }

    // Restore every piece of render state we touched.
    object_set_iso_content_rect(&saved_content);
    light_set_iso_content_rect(&saved_content);
    tile_set_render_target(gamelib_iso_window_vb);
    tig_window_set_video_buffer(gamelib_init_info.iso_window_handle, gamelib_iso_window_vb);
    location_origin_pixel_set(saved_ox, saved_oy);

    return ok;
}

// CE: pixel delta (in iso-screen space) that would bring an object's SPRITE CENTRE --
// not its tile/feet -- to the centre of the iso view. Same sprite-rect basis (0x7) as
// gamelib_render_lens_view, so a PC-lens overlay recenter frames the sprite middle
// regardless of race/height instead of the feet/base. Returns false (no delta written)
// when the sprite rect is unavailable (invisible / off-map), so the caller can fall
// back to plain tile centring. The delta is suitable for camera_tween_by().
bool gamelib_sprite_center_screen_delta(int64_t obj, int64_t* dx, int64_t* dy)
{
    TigRect sprite_rect;

    if (obj == OBJ_HANDLE_NULL || dx == NULL || dy == NULL) {
        return false;
    }

    object_get_rect(obj, 0x7, &sprite_rect);
    if (sprite_rect.width <= 0 || sprite_rect.height <= 0) {
        return false;
    }

    *dx = (int64_t)(sprite_rect.x + sprite_rect.width / 2)
        - (int64_t)(gamelib_iso_content_rect.x + gamelib_iso_content_rect.width / 2);
    *dy = (int64_t)(sprite_rect.y + sprite_rect.height / 2)
        - (int64_t)(gamelib_iso_content_rect.y + gamelib_iso_content_rect.height / 2);
    return true;
}

// 0x404740
void gamelib_draw_editor(GameDrawInfo* draw_info)
{
    TigRectListNode* node;
    tig_color_t color;

    color = tig_color_make(0, 0, 255);
    node = *draw_info->rects;
    while (node != NULL) {
        tig_window_fill(gamelib_init_info.iso_window_handle, &(node->rect), color);
        node = node->next;
    }

    if (tig_video_3d_begin_scene() == TIG_OK) {
        light_draw(draw_info);
        tile_draw(draw_info);
        facade_draw(draw_info);
        jumppoint_draw(draw_info);
        tile_script_draw(draw_info);
        tileblock_draw(draw_info);
        object_draw(draw_info);
        sector_draw(draw_info);
        wall_draw(draw_info);
        wp_draw(draw_info);
        roof_draw(draw_info);
        tb_draw(draw_info);
        tig_video_3d_end_scene();
    }
}

// 0x404810
void gamelib_logo(void)
{
    gmovie_play_path("movies\\SierraLogo.bik", 0, 0);
    gmovie_play_path("movies\\TroikaLogo.bik", 0, 0);
}

// CE: see gamelib.h. Shared post-process used by gamelib_splash,
// mainmenu_ui (legacy chrome), and slide_ui (credits slides).
void gamelib_apply_legacy_vignette_to_vb(TigVideoBuffer* vb)
{
    TigVideoBufferData vbd;
    if (vb == NULL) return;

    int gw = hrp_iso_window_width_get();
    int gh = hrp_iso_window_height_get();
    bool game_is_hires = (gw > 800 || gh > 600);
    bool side_gradient_only = false;

    if (!game_is_hires) {
        SDL_Rect display_bounds = { 0, 0, 0, 0 };
        if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &display_bounds)
            || display_bounds.w <= 0 || display_bounds.h <= 0) {
            return;
        }
        float display_aspect = (float)display_bounds.w / (float)display_bounds.h;
        if (display_aspect >= 1.32f && display_aspect <= 1.35f) {
            return;
        }
        side_gradient_only = true;
    }

    if (tig_video_buffer_lock(vb) != TIG_OK) return;
    if (tig_video_buffer_data(vb, &vbd) != TIG_OK) {
        tig_video_buffer_unlock(vb);
        return;
    }
    if (vbd.pixels == NULL || vbd.width <= 0 || vbd.height <= 0) {
        tig_video_buffer_unlock(vb);
        return;
    }

    const float cx = (float)vbd.width * 0.5f;
    const float cy = (float)vbd.height * 0.5f;
    const float rx = (float)vbd.width * 0.5f;
    const float ry = (float)vbd.height * 0.5f;
    const float inner = side_gradient_only ? 0.50f : 0.55f;
    const float outer = side_gradient_only ? 1.00f : 1.10f;
    const float span = outer - inner;

    uint8_t* base = (uint8_t*)vbd.pixels;
    for (int y = 0; y < vbd.height; y++) {
        uint32_t* row = (uint32_t*)(base + (size_t)y * (size_t)vbd.pitch);
        float dy = side_gradient_only ? 0.0f : ((float)y - cy) / ry;
        float dy2 = dy * dy;
        for (int x = 0; x < vbd.width; x++) {
            float dx = ((float)x - cx) / rx;
            float dist;
            if (side_gradient_only) {
                dist = dx < 0.0f ? -dx : dx;
            } else {
                dist = sqrtf(dx * dx + dy2);
            }
            float fade;
            if (dist <= inner) {
                continue;
            } else if (dist >= outer) {
                fade = 1.0f;
            } else {
                float t = (dist - inner) / span;
                fade = t * t * (3.0f - 2.0f * t);
            }
            float keep = 1.0f - fade;
            uint32_t pix = row[x];
            int r = tig_color_get_red(pix);
            int g = tig_color_get_green(pix);
            int b = tig_color_get_blue(pix);
            r = (int)((float)r * keep);
            g = (int)((float)g * keep);
            b = (int)((float)b * keep);
            row[x] = tig_color_make(r, g, b);
        }
    }

    tig_video_buffer_unlock(vb);
}

// 0x404830
void gamelib_splash(tig_window_handle_t window_handle)
{
    int splash;
    TigVideoBuffer* video_buffer;
    TigVideoBufferData vb_data;
    TigFileList file_list;
    TigWindowData window_data;
    int value;
    char path[TIG_MAX_PATH];
    int index;
    TigRect src_rect;
    TigRect dst_rect;

    settings_register(&settings, SPLASH_KEY, "0", NULL);
    splash = settings_get_value(&settings, SPLASH_KEY);

    // CE: ensure the vignette cfg key is registered here (in case
    // gamelib_splash runs before mainmenu_ui_init, which also
    // registers it). settings_register is idempotent — second
    // registration is a no-op besides callback re-assignment.
    settings_register(&settings, LEGACY_MENU_VIGNETTE_KEY, "0", NULL);

    tig_file_list_create(&file_list, "art\\splash\\*.bmp");

    if (file_list.count != 0) {
        for (index = 0; index < 3; index++) {
            value = (index + splash) % file_list.count;
            snprintf(path, sizeof(path), "art\\splash\\%s", file_list.entries[value].path);
            if (tig_video_buffer_load_from_bmp(path, &video_buffer, 0x1) == TIG_OK) {
                break;
            }
        }

        if (video_buffer != NULL
            && tig_window_data(window_handle, &window_data) == TIG_OK
            && tig_video_buffer_data(video_buffer, &vb_data) == TIG_OK) {
            // Apply opt-in vignette to the splash BMP before
            // copying it to the window. One-shot effect on the
            // freshly-loaded VB — no per-frame cost (the VB is
            // destroyed shortly after the copy).
            if (settings_get_value(&settings, LEGACY_MENU_VIGNETTE_KEY)) {
                gamelib_apply_legacy_vignette_to_vb(video_buffer);
            }

            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = vb_data.width;
            src_rect.height = vb_data.height;

            dst_rect.x = (window_data.rect.width - vb_data.width) / 2;
            dst_rect.y = (window_data.rect.height - vb_data.height) / 2;
            dst_rect.width = vb_data.width;
            dst_rect.height = vb_data.height;

            tig_window_copy_from_vbuffer(window_handle, &dst_rect, video_buffer, &src_rect);
        }

        if (video_buffer != NULL) {
            tig_video_buffer_destroy(video_buffer);
        }

        tig_window_invalidate_rect(NULL);
        tig_window_display();

        // CE: macOS and iOS require dequeuing events for the window to be
        // presented by the window manager. I'm not sure whether there is a
        // specific event to wait for or if it's just a matter of time.
        for (int i = 0; i < 3; i++) {
            SDL_PumpEvents();
        }

        settings_set_value(&settings, SPLASH_KEY, value + 1);
    }

    tig_file_list_destroy(&file_list);
}

// CE: First-run scaffolding for the custom\default override dir. Lays down
// a few common starter folders + a README so users can see where to drop
// replacement assets without digging through docs. The README doubles as
// a sentinel: if it already exists we do nothing — so folders a user
// deletes stay deleted (we never fight them), and the cost on every later
// launch is a single stat.
//
// Paths are "."-prefixed so tig_file treats them as literal (cwd-relative)
// and create directly under <cwd>\custom\default. A bare relative path
// like "custom\default\art" would instead get the write-target repo
// prepended (which is custom\default itself), yielding a doubled
// custom\default\custom\default\art — the "." literal avoids that.
static void gamelib_scaffold_custom_default(void)
{
    static const char* const starter_dirs[] = {
        ".\\custom\\default\\art",
        ".\\custom\\default\\art\\interface",
        ".\\custom\\default\\art\\critter",
        ".\\custom\\default\\art\\item",
        ".\\custom\\default\\art\\scenery",
        ".\\custom\\default\\proto",
        ".\\custom\\default\\mes",
        ".\\custom\\default\\sound",
    };
    TigFile* readme;
    size_t i;

    // Sentinel: README present => first-run scaffolding already done.
    readme = tig_file_fopen(".\\custom\\default\\README.txt", "rb");
    if (readme != NULL) {
        tig_file_fclose(readme);
        return;
    }

    for (i = 0; i < sizeof(starter_dirs) / sizeof(starter_dirs[0]); i++) {
        tig_file_mkdir(starter_dirs[i]);
    }

    readme = tig_file_fopen(".\\custom\\default\\README.txt", "wb");
    if (readme != NULL) {
        tig_file_fputs(
            "Arcanum Community Edition - Custom Asset Overrides\r\n"
            "==================================================\r\n"
            "\r\n"
            "Drop replacement files into the matching folder here and they\r\n"
            "override the base game (and any module) with no .dat editing.\r\n"
            "Files are matched BY NAME against the vanilla layout, so keep\r\n"
            "the original filenames.\r\n"
            "\r\n"
            "Precedence (highest first):\r\n"
            "  custom\\modules\\<ModuleName>\\   per-module overrides\r\n"
            "  custom\\default\\                <-- this folder: global, always on\r\n"
            "  the loaded module\r\n"
            "  loose data\\ files\r\n"
            "  the base Arcanum*.dat archives\r\n"
            "\r\n"
            "FOLDERS (add any you need - the game reads them all):\r\n"
            "  art\\interface\\   UI art: frames, buttons, backgrounds\r\n"
            "  art\\critter\\     creature / NPC sprites\r\n"
            "  art\\item\\        item icons + ground art (i_*, g_*)\r\n"
            "  art\\scenery\\     props, trees, furniture\r\n"
            "  art\\tile\\        ground tiles\r\n"
            "  art\\wall\\        walls\r\n"
            "  art\\portal\\      doors / portals\r\n"
            "  art\\monster\\     monster sprites\r\n"
            "  art\\facade\\      building facades\r\n"
            "  art\\eye_candy\\   spell / visual effects\r\n"
            "  art\\container\\   chests, barrels\r\n"
            "  art\\light\\       light sprites\r\n"
            "  art\\roof\\        roofs\r\n"
            "  art\\unique_npc\\  named-NPC art\r\n"
            "  proto\\           object templates (.pro): stats, flags, behaviour\r\n"
            "  mes\\             text tables (.mes): names, descriptions, UI text\r\n"
            "  rules\\           gameplay tables (XP, level progression, ...)\r\n"
            "  dlg\\             dialogue files (.dlg)\r\n"
            "  scr\\             compiled scripts (.scr)\r\n"
            "  sound\\           sound effects\r\n"
            "  movies\\          cutscene videos\r\n"
            "  terrain\\         terrain definitions\r\n"
            "  townmap\\         town-map data\r\n"
            "  maps\\<mapname>\\  per-map data\r\n"
            "\r\n"
            "Only a few common starter folders are created for you; create\r\n"
            "any others from the list above as needed.\r\n"
            "\r\n"
            "Custom UI backgrounds:  art\\interface\\<screen>_bg.bmp\r\n"
            "  (e.g. mainmenu_bg.bmp). Cutscene movie swaps use a separate\r\n"
            "  flat folder: custom\\videos\\<name>.\r\n"
            "\r\n"
            "This README is recreated only when absent - delete it to\r\n"
            "regenerate the starter folders on the next launch.\r\n",
            readme);
        tig_file_fclose(readme);
    }
}

// 0x404A20
void gamelib_load_data(void)
{
    TigFileList file_list;
    unsigned int index;

    tig_file_list_create(&file_list, "arcanum*.dat");

    for (index = 0; index < file_list.count; index++) {
        tig_file_repository_add(file_list.entries[index].path);
    }

    tig_file_list_destroy(&file_list);

    tig_file_mkdir("data");
    tig_file_repository_add("data");

    // CE custom asset overrides. "custom\default" is the always-on global skin;
    // mounted here above the base .dat + loose data, and re-promoted above the
    // active module on load (gamelib_mount_custom_overrides). Per-module dirs
    // "custom\modules\<name>" mount on top of default when that module loads.
    // Original module > loose > .dat order is otherwise untouched.
    tig_file_mkdir("custom");
    tig_file_mkdir("custom\\default");
    tig_file_repository_add_readonly("custom\\default");

    // CE: first-run starter folders + README so users can see where
    // override assets go. Idempotent (README sentinel).
    gamelib_scaffold_custom_default();
}

// 0x404C10
bool gamelib_load_module_data(const char* module_name)
{
    char path1[TIG_MAX_PATH];
    char path2[TIG_MAX_PATH];
    size_t end;
    int index;

    if (module_name[0] == '\\' || module_name[0] == '.' || module_name[1] == ':') {
        path1[0] = '\0';
    } else {
        strcpy(path1, ".\\Modules\\");
    }

    strcat(path1, module_name);
    end = strlen(path1);

    strcat(path1, ".dat");
    if (tig_file_exists(path1, NULL)) {
        if (!tig_file_repository_add(path1)) {
            return false;
        }

        strcpy(gamelib_mod_dat_path, path1);

        path1[end] = '\0';

        for (index = 0; index < GAMELIB_MAX_PATCH_COUNT; index++) {
            snprintf(path2, TIG_MAX_PATH, "%s.patch%d", path1, index);
            if (tig_file_exists(path2, NULL)) {
                if (!tig_file_repository_add(path2)) {
                    return false;
                }

                strcpy(gamelib_mod_patch_paths[index], path2);
            }
        }
    }

    path1[end] = '\0';

    if (tig_file_is_directory(path1)) {
        // CE: mount the module loose dir WRITABLE, matching upstream/alexbatalov.
        // An earlier CE branch (ce21ac34) mounted it read-only in the game to keep
        // runtime writes (Save\Current, TIGCache\) out of the module dir. That broke
        // save/load: upstream keeps each module's Save\Current INSIDE its own dir
        // (per-module, self-isolating), so the roundtrip works; the read-only mount
        // forced every module to share one data\Save\Current, and imperfect clearing
        // left cross-module stale state -> loads fell back to the empty ShopMap. The
        // genuine custom-UI override dirs (custom\default, custom\modules\<name>) stay
        // read-only via gamelib_mount_custom_overrides; only the module dir reverts.
        tig_file_repository_add(path1);
        strcpy(gamelib_mod_dir_path, path1);
        return true;
    }

    if (tig_file_mkdir(path1)) {
        if (gamelib_init_info.editor) {
            if (gamelib_mod_dat_path[0] == '\0') {
                tig_file_copy_directory(path1, "Module template");
            }
        }
        tig_file_repository_add(path1);
        strcpy(gamelib_mod_dir_path, path1);
        return true;
    }

    tig_debug_printf("gamelib_mod_load(): error creating folder %s\n", path1);

    if (gamelib_mod_dat_path[0] != '\0') {
        tig_file_repository_remove(gamelib_mod_dat_path);
    }

    return false;
}

// 0x405070
void gamelib_unload_module_data(void)
{
    int index;

    tig_file_repository_remove(gamelib_mod_dir_path);

    if (byte_5D0B58[0] != '\0') {
        tig_file_repository_remove(byte_5D0B58);
    }

    for (index = GAMELIB_MAX_PATCH_COUNT - 1; index >= 0; index--) {
        if (gamelib_mod_patch_paths[index][0] != '\0') {
            tig_file_repository_remove(gamelib_mod_patch_paths[index]);
            gamelib_mod_patch_paths[index][0] = '\0';
        }
    }

    if (gamelib_mod_dat_path[0] != '\0') {
        tig_file_repository_remove(gamelib_mod_dat_path);
    }

    // CE: drop the per-module custom override dir (custom\default persists).
    if (gamelib_mod_custom_path[0] != '\0') {
        tig_file_repository_remove(gamelib_mod_custom_path);
        gamelib_mod_custom_path[0] = '\0';
    }

    gamelib_mod_dir_path[0] = '\0';
    byte_5D0B58[0] = '\0';
    gamelib_mod_dat_path[0] = '\0';
    byte_5D0EA4[0] = '\0';

    memset(&gamelib_mod_guid, 0, sizeof(gamelib_mod_guid));

    dword_5D10C4 = false;
}
