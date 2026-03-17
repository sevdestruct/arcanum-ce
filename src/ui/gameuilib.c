#include "ui/gameuilib.h"

#include <string.h>

#include "game/gamelib.h"
#include "game/target.h"
#include "tig/bmp.h"
#include "ui/anim_ui.h"
#include "ui/broadcast_ui.h"
#include "ui/charedit_ui.h"
#include "ui/combat_ui.h"
#include "ui/compact_ui.h"
#include "ui/cyclic_ui.h"
#include "ui/dialog_ui.h"
#include "ui/fate_ui.h"
#include "ui/follower_ui.h"
#include "ui/hotkey_ui.h"
#include "ui/intgame.h"
#include "ui/inven_ui.h"
#include "ui/iso.h"
#include "ui/item_ui.h"
#include "ui/logbook_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/roller_ui.h"
#include "ui/schematic_ui.h"
#include "ui/scrollbar_ui.h"
#include "ui/skill_ui.h"
#include "ui/sleep_ui.h"
#include "ui/slide_ui.h"
#include "ui/spell_ui.h"
#include "ui/tb_ui.h"
#include "ui/textedit_ui.h"
#include "ui/wmap_rnd.h"
#include "ui/wmap_ui.h"
#include "ui/written_ui.h"

/**
 * Path to the current save directory.
 */
#define SAVE_CURRENT "Save\\Current"

/**
 * Path to the `data2.sav` file in the current save directory.
 */
#define SAVE_CURRENT_DATA2_SAV "Save\\Current\\data2.sav"

/**
 * Version number for the save file format.
 */
#define VERSION 25

/**
 * Sentinel value to mark module data boundaries in the save file.
 */
#define SENTINEL 0xBEEFCAFE

typedef bool(GameUiInitFunc)(GameInitInfo* init_info);
typedef void(GameUiResetFunc)(void);
typedef bool(GameUiModuleLoadFunc)(void);
typedef void(GameUiModuleUnloadFunc)(void);
typedef void(GameUiExitFunc)(void);
typedef bool(GameUiSaveFunc)(TigFile* stream);
typedef bool(GameUiLoadFunc)(GameLoadInfo* load_info);
typedef void(GameUiResizeFunc)(GameResizeInfo* resize_info);

typedef struct GameUiLibModule {
    const char* name;
    GameUiInitFunc* init_func;
    GameUiResetFunc* reset_func;
    GameUiModuleLoadFunc* mod_load_func;
    GameUiModuleUnloadFunc* mod_unload_func;
    GameUiExitFunc* exit_func;
    GameUiSaveFunc* save_func;
    GameUiLoadFunc* load_func;
    GameUiResizeFunc* resize_func;
} GameUiLibModule;

static bool gameuilib_custom_ui_cache_load(const char* const* candidates);
static void gameuilib_custom_ui_cache_reset(void);

/**
 * 0x5C2AE0
 */
static GameUiLibModule gameuilib_modules[] = {
    { "Scrollbar", scrollbar_ui_init, scrollbar_ui_reset, NULL, NULL, scrollbar_ui_exit, NULL, NULL, NULL },
    { "Cyclic-UI", cyclic_ui_init, NULL, NULL, NULL, cyclic_ui_exit, NULL, NULL, NULL },
    { "MainMenu-UI", mainmenu_ui_init, NULL, NULL, NULL, mainmenu_ui_exit, NULL, NULL, NULL },
    { "Intgame", intgame_init, intgame_reset, NULL, NULL, intgame_exit, intgame_save, intgame_load, intgame_resize },
    { "Hotkey-UI", hotkey_ui_init, NULL, NULL, NULL, hotkey_ui_exit, NULL, NULL, hotkey_ui_resize },
    { "Target", target_init, target_reset, NULL, NULL, target_exit, NULL, NULL, target_resize },
    { "Anim-UI", anim_ui_init, anim_ui_reset, NULL, NULL, anim_ui_exit, anim_ui_save, anim_ui_load, NULL },
    { "Iso", iso_init, iso_reset, NULL, NULL, iso_exit, NULL, NULL, iso_resize },
    { "TB-UI", tb_ui_init, tb_ui_reset, NULL, NULL, tb_ui_exit, NULL, NULL, NULL },
    { "TextEdit-UI", textedit_ui_init, textedit_ui_reset, NULL, NULL, textedit_ui_exit, NULL, NULL, NULL },
    { "WMap-UI", wmap_ui_init, wmap_ui_reset, NULL, NULL, wmap_ui_exit, wmap_ui_save, wmap_ui_load, NULL },
    { "WMap-Rnd", wmap_rnd_init, wmap_rnd_reset, wmap_rnd_mod_load, wmap_rnd_mod_unload, wmap_rnd_exit, wmap_rnd_save, wmap_rnd_load, NULL },
    { "LogBook-UI", logbook_ui_init, logbook_ui_reset, NULL, NULL, logbook_ui_exit, NULL, NULL, NULL },
    { "Spell-UI", spell_ui_init, spell_ui_reset, NULL, NULL, spell_ui_exit, spell_ui_save, spell_ui_load, NULL },
    { "Sleep-UI", sleep_ui_init, sleep_ui_reset, NULL, NULL, sleep_ui_exit, NULL, NULL, NULL },
    { "Skill_UI", skill_ui_init, skill_ui_reset, NULL, NULL, skill_ui_exit, NULL, NULL, NULL },
    { "Char-Edit", charedit_init, charedit_reset, NULL, NULL, charedit_exit, NULL, NULL, NULL },
    { "Inven-UI", inven_ui_init, inven_ui_reset, NULL, NULL, inven_ui_exit, NULL, NULL, NULL },
    { "Item-UI", item_ui_init, NULL, NULL, NULL, item_ui_exit, NULL, NULL, NULL },
    { "Broadcast-UI", broadcast_ui_init, broadcast_ui_reset, NULL, NULL, broadcast_ui_exit, NULL, NULL, NULL },
    { "Dialog-UI", dialog_ui_init, dialog_ui_reset, NULL, NULL, dialog_ui_exit, NULL, NULL, NULL },
    { "Fate-UI", fate_ui_init, fate_ui_reset, NULL, NULL, fate_ui_exit, NULL, NULL, NULL },
    { "Combat-UI", combat_ui_init, combat_ui_reset, NULL, NULL, combat_ui_exit, NULL, NULL, combat_ui_resize },
    { "Schematic-UI", schematic_ui_init, schematic_ui_reset, NULL, NULL, schematic_ui_exit, NULL, NULL, NULL },
    { "Written-UI", NULL, NULL, written_ui_mod_load, written_ui_mod_unload, NULL, NULL, NULL, NULL },
    { "Follower-UI", follower_ui_init, follower_ui_reset, NULL, NULL, follower_ui_exit, follower_ui_save, follower_ui_load, follower_ui_resize },
    { "Roller-UI", roller_ui_init, NULL, NULL, NULL, roller_ui_exit, NULL, NULL, NULL },
    { "Slide-UI", NULL, NULL, slide_ui_mod_load, slide_ui_mod_unload, NULL, NULL, NULL, NULL },
    { "Compact-UI", compact_ui_init, compact_ui_reset, NULL, NULL, compact_ui_exit, NULL, NULL, NULL },
};

#define MODULE_COUNT ((int)SDL_arraysize(gameuilib_modules))

/**
 * Flag indicating if the game UI wants to present mainmenu.
 *
 * 0x63CBE0
 */
static bool wants_mainmenu;

/**
 * Flag indicating if the game UI library is initialized.
 *
 * 0x63CBE4
 */
static bool gameuilib_initialized;

/**
 * Flag indicating if the game UI module is loaded.
 *
 * 0x63CBE8
 */
static bool gameuilib_mod_loaded;

static TigBmp gameuilib_custom_ui_bmp;
static bool gameuilib_custom_ui_bmp_valid;
static char gameuilib_custom_ui_bmp_path[TIG_MAX_PATH];

/**
 * Called when the game is initialized.
 *
 * 0x53E600
 */
bool gameuilib_init(GameInitInfo* init_info)
{
    int index;

    // Initialize each module.
    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].init_func != NULL) {
            if (!gameuilib_modules[index].init_func(init_info)) {
                tig_debug_printf("gameuilib_init(): init function %d (%s) failed\n", index, gameuilib_modules[index].name);

                // Something went wrong - clean up prevously initialized
                // modules.
                while (--index >= 0) {
                    if (gameuilib_modules[index].exit_func != NULL) {
                        gameuilib_modules[index].exit_func();
                    }
                }

                return false;
            }
        }
    }

    gameuilib_initialized = true;

    // Register save and load callbacks.
    gamelib_set_extra_save_func(gameuilib_save);
    gamelib_set_extra_load_func(gameuilib_load);

    return true;
}

/**
 * Called when the game shuts down.
 *
 * 0x53E6A0
 */
void gameuilib_exit(void)
{
    int index;

    tig_debug_printf("[Exiting Game]\n");

    // Exit each module in reverse order.
    for (index = MODULE_COUNT - 1; index >= 0; index--) {
        if (gameuilib_modules[index].exit_func != NULL) {
            gameuilib_modules[index].exit_func();
        }
    }

    gameuilib_custom_ui_cache_reset();
    gameuilib_initialized = false;
}

/**
 * Called when the game is being reset.
 *
 * 0x53E6E0
 */
void gameuilib_reset(void)
{
    int index;

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].reset_func != NULL) {
            gameuilib_modules[index].reset_func();
        }
    }
}

/**
 * Called when the window size has changed.
 *
 * 0x53E700
 */
void gameuilib_resize(GameResizeInfo* resize_info)
{
    int index;

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].resize_func != NULL) {
            gameuilib_modules[index].resize_func(resize_info);
        }
    }
}

/**
 * Called when a module is being loaded.
 *
 * 0x53E730
 */
bool gameuilib_mod_load(void)
{
    int index;

    gameuilib_mod_unload();

    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].mod_load_func != NULL) {
            if (!gameuilib_modules[index].mod_load_func()) {
                // Something went wrong - clean up prevously loaded modules.
                while (--index >= 0) {
                    if (gameuilib_modules[index].mod_unload_func != NULL) {
                        gameuilib_modules[index].mod_unload_func();
                    }
                }

                return false;
            }
        }
    }

    gameuilib_mod_loaded = true;

    return true;
}

/**
 * Called when a module is being unloaded.
 *
 * 0x53E790
 */
void gameuilib_mod_unload(void)
{
    int index;

    if (!gameuilib_mod_loaded) {
        return;
    }

    // Unload each module in reverse order.
    for (index = MODULE_COUNT - 1; index >= 0; index--) {
        if (gameuilib_modules[index].mod_unload_func != NULL) {
            gameuilib_modules[index].mod_unload_func();
        }
    }

    gameuilib_mod_loaded = false;
    gameuilib_custom_ui_cache_reset();
}

bool gameuilib_custom_ui_blit(tig_window_handle_t window_handle, const TigRect* canvas_rect, const TigRect* dst_rect, const char* const* candidates)
{
    TigRect canvas;
    TigRect dst;
    TigRect src;
    int src_x;
    int src_y;
    int dst_x;
    int dst_y;
    int blit_w;
    int blit_h;

    if (canvas_rect == NULL || dst_rect == NULL || !gameuilib_custom_ui_cache_load(candidates)) {
        return false;
    }

    canvas = *canvas_rect;
    dst = *dst_rect;
    src_x = dst.x - canvas.x + (gameuilib_custom_ui_bmp.width - canvas.width) / 2;
    src_y = dst.y - canvas.y + (gameuilib_custom_ui_bmp.height - canvas.height) / 2;
    dst_x = dst.x;
    dst_y = dst.y;
    blit_w = dst.width;
    blit_h = dst.height;

    if (src_x < 0) {
        dst_x -= src_x;
        blit_w += src_x;
        src_x = 0;
    }

    if (src_y < 0) {
        dst_y -= src_y;
        blit_h += src_y;
        src_y = 0;
    }

    if (src_x + blit_w > gameuilib_custom_ui_bmp.width) {
        blit_w = gameuilib_custom_ui_bmp.width - src_x;
    }

    if (src_y + blit_h > gameuilib_custom_ui_bmp.height) {
        blit_h = gameuilib_custom_ui_bmp.height - src_y;
    }

    if (blit_w <= 0 || blit_h <= 0) {
        return false;
    }

    src.x = src_x;
    src.y = src_y;
    src.width = blit_w;
    src.height = blit_h;

    dst.x = dst_x;
    dst.y = dst_y;
    dst.width = blit_w;
    dst.height = blit_h;

    tig_bmp_copy_to_window(&gameuilib_custom_ui_bmp, &src, window_handle, &dst);
    return true;
}

static bool gameuilib_custom_ui_cache_load(const char* const* candidates)
{
    int index;

    if (candidates == NULL) {
        return false;
    }

    for (index = 0; candidates[index] != NULL; index++) {
        if (gameuilib_custom_ui_bmp_valid
            && strcmp(gameuilib_custom_ui_bmp_path, candidates[index]) == 0) {
            return true;
        }

        gameuilib_custom_ui_cache_reset();
        strncpy(gameuilib_custom_ui_bmp.name, candidates[index], sizeof(gameuilib_custom_ui_bmp.name) - 1);
        gameuilib_custom_ui_bmp.name[sizeof(gameuilib_custom_ui_bmp.name) - 1] = '\0';
        if (tig_bmp_create(&gameuilib_custom_ui_bmp) == TIG_OK) {
            strncpy(gameuilib_custom_ui_bmp_path, candidates[index], sizeof(gameuilib_custom_ui_bmp_path) - 1);
            gameuilib_custom_ui_bmp_path[sizeof(gameuilib_custom_ui_bmp_path) - 1] = '\0';
            gameuilib_custom_ui_bmp_valid = true;
            return true;
        }
    }

    return false;
}

static void gameuilib_custom_ui_cache_reset(void)
{
    if (gameuilib_custom_ui_bmp_valid) {
        tig_bmp_destroy(&gameuilib_custom_ui_bmp);
        gameuilib_custom_ui_bmp_valid = false;
    }

    gameuilib_custom_ui_bmp_path[0] = '\0';
}

/**
 * Called when the game is being saved.
 *
 * 0x53E7C0
 */
bool gameuilib_save(void)
{
    int sentinel = SENTINEL;
    int start_pos = 0;
    TigFile* stream;
    int version;
    int index;
    int pos;

    // Check save directory existence.
    if (!tig_file_is_directory(SAVE_CURRENT)) {
        tig_debug_printf("gameuilib_save(): Error finding folder %s\n", SAVE_CURRENT);
        return false;
    }

    // Open save file.
    stream = tig_file_fopen(SAVE_CURRENT_DATA2_SAV, "wb");
    if (stream == NULL) {
        tig_debug_printf("gameuilib_save(): Error creating data2.sav\n");
        return false;
    }

    // Write version.
    version = VERSION;
    if (tig_file_fwrite(&version, sizeof(version), 1, stream) != 1) {
        tig_debug_printf("gameuilib_save(): Error writing version\n");
        tig_file_fclose(stream);
        tig_file_remove(SAVE_CURRENT_DATA2_SAV);
        return false;
    }

    // Retrieve start position of the first module.
    tig_file_fgetpos(stream, &start_pos);
    tig_debug_printf("\ngameuilib_save: Start Pos: %lu\n", start_pos);

    // Save each module's state.
    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].save_func != NULL) {
            if (!gameuilib_modules[index].save_func(stream)) {
                tig_debug_printf("gameuilib_save(): save function %d (%s) failed\n",
                    index,
                    gameuilib_modules[index].name);
                break;
            }

            // Log file position.
            tig_file_fgetpos(stream, &pos);
            tig_debug_printf("gameuilib_save: Function %d (%s) wrote to: %lu, Total: (%lu)\n",
                index,
                gameuilib_modules[index].name,
                pos,
                pos - start_pos);
            start_pos = pos;

            // Write sentinel.
            if (tig_file_fwrite(&sentinel, sizeof(sentinel), 1, stream) != 1) {
                tig_debug_printf("gameuilib_save(): ERROR: Sentinel Failed to Save!\n");
                break;
            }
        }
    }

    tig_file_fclose(stream);

    if (index < MODULE_COUNT) {
        // One of the module failed to write data, remove malformed data file.
        tig_file_remove(SAVE_CURRENT_DATA2_SAV);
        return false;
    }

    return true;
}

/**
 * Called when the game is being loaded.
 *
 * 0x53E950
 */
bool gameuilib_load(void)
{
    int start_pos = 0;
    TigFile* stream;
    GameLoadInfo load_info;
    int index;
    int pos;
    int sentinel;

    // Check save directory existence.
    if (!tig_file_is_directory(SAVE_CURRENT)) {
        tig_debug_printf("gameuilib_load(): Error finding folder %s\n", SAVE_CURRENT);
        return false;
    }

    // Open save file.
    stream = tig_file_fopen(SAVE_CURRENT_DATA2_SAV, "rb");
    if (stream == NULL) {
        tig_debug_printf("gameuilib_load(): Error creating data2.sav\n");
        return false;
    }

    // Read version.
    if (tig_file_fread(&(load_info.version), sizeof(load_info.version), 1, stream) != 1) {
        tig_debug_printf("gameuilib_load(): Error reading version\n");
        tig_file_fclose(stream);
        return false;
    }

    load_info.stream = stream;

    // Retrieve start position of the first module.
    tig_file_fgetpos(stream, &start_pos);
    tig_debug_printf("gameuilib_load: Start Pos: %lu\n", start_pos);

    // Load each module's state.
    for (index = 0; index < MODULE_COUNT; index++) {
        if (gameuilib_modules[index].load_func != NULL) {
            if (!gameuilib_modules[index].load_func(&load_info)) {
                tig_debug_printf("gameuilib_load(): load function %d (%s) failed\n",
                    index,
                    gameuilib_modules[index].name);
                break;
            }

            // Log file position.
            tig_file_fgetpos(stream, &pos);
            tig_debug_printf("gameuilib_load: Function %d (%s) read to: %lu, Total: (%lu)\n",
                index,
                gameuilib_modules[index].name,
                pos,
                pos - start_pos);
            start_pos = pos;

            // Read & verify sentinel.
            if (tig_file_fread(&sentinel, sizeof(sentinel), 1, stream) != 1) {
                tig_debug_printf("gameuilib_load(): ERROR: Load Sentinel Failed to Load!");
                break;
            }

            if (sentinel != SENTINEL) {
                tig_debug_printf("gameuilib_load(): ERROR: Load Sentinel Failed to Match!");
                break;
            }
        }
    }

    tig_file_fclose(stream);

    if (index < MODULE_COUNT) {
        // One of the module failed to read data.
        return false;
    }

    return true;
}

/**
 * 0x53EAD0
 */
bool gameuilib_wants_mainmenu(void)
{
    if (!wants_mainmenu) {
        return false;
    }

    wants_mainmenu = false;

    return true;
}

/**
 * 0x53EAF0
 */
void gameuilib_wants_mainmenu_set(void)
{
    wants_mainmenu = true;
}

/**
 * 0x53EB00
 */
void gameuilib_wants_mainmenu_unset(void)
{
    wants_mainmenu = false;
}
