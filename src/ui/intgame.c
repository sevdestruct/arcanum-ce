#include "ui/intgame.h"

#include <limits.h>
#include <stdio.h>

#include "game/camera_follow.h"
#include "game/camera_tween.h"
#include "game/dialog_camera.h"
#include "game/iso_zoom.h"
#include "game/location.h"
#include "game/ai.h"
#include "game/anim.h"
#include "game/bless.h"
#include "game/broadcast.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/curse.h"
#include "game/damage_type.h"
#include "game/descriptions.h"
#include "game/gamelib.h"
#include "game/gsound.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/item_effect.h"
#include "game/level.h"
#include "game/light.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/mt_item.h"
#include "game/obj.h"
#include "game/obj_private.h"
#include "game/object.h"
#include "game/player.h"
#include "game/portrait.h"
#include "game/proto.h"
#include "game/reaction.h"
#include "game/resistance.h"
#include "game/scroll.h"
#include "game/sfx.h"
#include "game/skill.h"
#include "game/snd.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/target.h"
#include "game/tc.h"
#include "game/tech.h"
#include "game/timeevent.h"
#include "ui/anim_ui.h"
#include "ui/broadcast_ui.h"
#include "ui/charedit_ui.h"
#include "ui/compact_ui.h"
#include "ui/dialog_ui.h"
#include "ui/fate_ui.h"
#include "ui/follower_ui.h"
#include "ui/gameuilib.h"
#include "ui/hotkey_ui.h"
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
#include "ui/spell_ui.h"
#include "ui/tb_ui.h"
#include "ui/textedit_ui.h"
#include "ui/ui_anim.h"
#include "ui/wmap_ui.h"
#include "ui/written_ui.h"

#define MAX_INTERFACE_WINDOW_ROTATION_STEPS 6

#define MAX_MESSAGE_HISTORY_ITEMS 10
#define MAX_MESSAGE_HISTORY_STRING_SIZE 200

typedef enum IntgameCounter {
    INTGAME_COUNTER_HEALTH,
    INTGAME_COUNTER_FATIGUE,
    INTGAME_COUNTER_FATE,
    INTGAME_COUNTER_MONEY,
    INTGAME_COUNTER_MANA,
    INTGAME_COUNTER_POISON,
    INTGAME_COUNTER_COUNT,
} IntgameCounter;

typedef enum IntgamePrimaryButton {
    INTGAME_PRIMARY_BUTTON_LOGBOOK,
    INTGAME_PRIMARY_BUTTON_CHAR,
    INTGAME_PRIMARY_BUTTON_INVENTORY,
    INTGAME_PRIMARY_BUTTON_MAP,
    INTGAME_PRIMARY_BUTTON_FATE,
    INTGAME_PRIMARY_BUTTON_COUNT,
} IntgamePrimaryButton;

typedef enum IntgameSecondaryButton {
    INTGAME_SECONDARY_BUTTON_SKILLS,
    INTGAME_SECONDARY_BUTTON_SPELLS,
    INTGAME_SECONDARY_BUTTON_COMBAT,
    INTGAME_SECONDARY_BUTTON_SCHEMATICS,
    INTGAME_SECONDARY_BUTTON_COUNT,
} IntgameSecondaryButton;

typedef enum IntgameQuantityButton {
    INTGAME_QUANTITY_BUTTON_TAKE_ALL,
    INTGAME_QUANTITY_BUTTON_PLUS,
    INTGAME_QUANTITY_BUTTON_MINUS,
    INTGAME_QUANTITY_BUTTON_OK,
    INTGAME_QUANTITY_BUTTON_CANCEL,
    INTGAME_QUANTITY_BUTTON_COUNT,
} IntgameQuantityButton;

typedef enum IntgamePenalty {
    INTGAME_PENALTY_MSR,
    INTGAME_PENALTY_RANGE,
    INTGAME_PENALTY_PERCEPTION,
    INTGAME_PENALTY_COVER,
    INTGAME_PENALTY_LIGHT,
    INTGAME_PENALTY_INJURY,
    INTGAME_PENALTY_BLOCKED_SHOT,
    INTGAME_PENALTY_MAGIC_TECH,
    INTGAME_PENALTY_COUNT,
} IntgamePenalty;

#define INTGAME_PENALTY_SLOTS 6

typedef struct IntgameIsoWindowTypeInfo {
    /* 0000 */ TigRect rect;
    /* 0010 */ tig_window_handle_t window_handle;
} IntgameIsoWindowTypeInfo;

#define MSG_TEXT_HALIGN_LEFT 0x01u
#define MSG_TEXT_HALIGN_RIGHT 0x02u
#define MSG_TEXT_HALIGN_CENTER 0x04u
#define MSG_TEXT_VALIGN_CENTER 0x08u
#define MSG_TEXT_SECONDARY 0x10u
#define MSG_TEXT_TRUNCATE 0x20u

static bool button_create_flags(UiButtonInfo* button_info, unsigned int flags);
static bool button_create_no_art(UiButtonInfo* button_info, int width, int height);
static void intgame_draw_counter(int counter, int value, int digits);
static void intgame_draw_bar_rect(TigRect* rect);
static void intgame_ammo_icon_refresh(tig_art_id_t art_id);
static bool iso_interface_message_filter(TigMessage* msg);
static void intgame_secondary_button_toggle(IntgameSecondaryButton btn, RotatingWindowType window_type);
static bool handle_button_unhover(TigMessage* msg);
static void intgame_center_on_player(void);
static void intgame_combat_mode_toggle(void);
static void sub_54ECD0(void);
static void sub_54ED30(TargetDescriptor* td);
static void sub_550000(int64_t critter_obj, Hotkey* a2, int inventory_location);
static bool sub_5501C0(void);
static bool sub_5503F0(RotatingWindowType window_type, int progress);
static void iso_interface_window_disable(RotatingWindowType window_type);
static void intgame_message_window_write_text_centered(char* str, TigRect* rect);
static bool intgame_message_window_write_text(tig_window_handle_t window_handle, char* str, TigRect* rect, tig_font_handle_t font, unsigned int flags);
static bool intgame_spells_init(void);
static void intgame_spells_show_college_spells(int group);
static void intgame_spells_hide_college_spells(int group);
static bool intgame_mt_spells_init(void);
static void intgame_mt_spells_disable(void);
static void iso_interface_window_enable(RotatingWindowType window_type);
static void intgame_mt_spells_enable(void);
static int find_interface_window_index(int x, int y);
static void sub_5517F0(void);
static bool intgame_adjust_mouse_for_zoom(int x, int y, int* adj_x, int* adj_y);
static bool sub_5518C0(int x, int y);
static void sub_551910(TigMessage* msg);
static void sub_551A10(int64_t obj);
static void intgame_force_fullscreen(void);
static void intgame_unforce_fullscreen(void);
static void sub_551F80(void);
static bool sub_552050(int x, int y, TargetDescriptor* td);
static void sub_5520D0(RotatingWindowType window_type, int step);
static void iso_interface_window_swap(RotatingWindowType window_type);
static void intgame_hud_apply_clips(void);
// CE: HUD collapse-transition "wings slide down" ghosts (FULL->MEDIUM and
// MEDIUM->MINI), defined near the HUD window helpers; used earlier in
// iso_interface_destroy and the TAB toggle.
static void intgame_hud_ghost_destroy(void);
static void intgame_hud_ghost_slide_down(void);
static void intgame_hud_ghost_med_to_mini(void);
static void intgame_hud_ghost_ping(void);
// CE: MINI-peek press handler. Returns true if the press should be
// swallowed (peek-expand to MEDIUM, or return-from-peek to MINI),
// false if it should fall through to normal rotwin toggle-off.
static bool intgame_hud_handle_mini_peek_press(void);
// CE: re-establish MINI invariants (stage=MINI, type=SKILLS, button
// state synced) regardless of how we got here.
static void intgame_hud_enter_mini_with_skills(void);
// CE: true when the HUD is in the MINI crop stage. Used by code that
// runs ahead of where intgame_hud_stage is declared.
static bool intgame_hud_in_mini_stage(void);
static void intgame_clock_refresh(void);
static void sub_552740(int64_t obj, ChareditMode mode);
static void sub_552770(UiMessage* ui_message);
static void intgame_message_history_scroll_up(void);
static void intgame_message_history_scroll_down(void);
static void intgame_message_refresh(bool play_sound);
static void intgame_message_draw(tig_window_handle_t window_handle, UiMessage* ui_message, bool play_sound);
static void intgame_spell_maintain_art_set_func(UiButtonInfo* button, int slot, tig_art_id_t art_id, tig_window_handle_t window_handle);
static void intgame_spell_maintain_refresh_func(tig_button_handle_t button_handle, UiButtonInfo* info, int slot, bool active, tig_window_handle_t window_handle);
static void intgame_refresh_quantity(void);
static void sub_553A70(TigMessage* msg);
static void intgame_examine_critter(int64_t pc_obj, int64_t critter_obj, char* str);
static void intgame_message_window_draw_image(tig_window_handle_t window_handle, int num);
static void sub_554640(int a1, int a2, TigRect* rect, int value);
static void sub_554830(int64_t a1, int64_t a2);
static void sub_554B00(tig_window_handle_t window_handle, int art_num, int x, int y);
static int intgame_item_icon_get(int64_t item_obj);
static void intgame_examine_item(int64_t pc_obj, int64_t item_obj, char* str);
static void append_stat(char* buffer, size_t maxlen, int num, int min, int max, int a5, bool is_modifier);
static void format_weapon_stats(int64_t weapon_obj, char* buffer, size_t maxlen);
static void format_armor_stats(int64_t armor_obj, char* buffer, size_t maxlen);
static void intgame_examine_scenery(int64_t pc_obj, int64_t scenery_obj, char* str);
static void intgame_examine_portal(int64_t pc_obj, int64_t portal_obj, char* str);
static void intgame_examine_container(int64_t pc_obj, int64_t container_obj, char* str);
static void intgame_draw_portrait(int64_t obj, int portrait, tig_window_handle_t window_handle, int x, int y);
static void intgame_refresh_primary_button(UiPrimaryButton btn);
static void intgame_refresh_experience_gauges(int64_t obj);
static void sub_556EA0(int64_t item_obj);
static void intgame_mt_button_enable(void);
static void intgame_mt_button_disable(void);
static bool intgame_big_window_create(void);
static void intgame_big_window_destroy(void);
static bool intgame_big_window_message_filter(TigMessage* msg);

// 0x5C6378
static tig_window_handle_t intgame_maintain_fs_windows[5] = {
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
    TIG_WINDOW_HANDLE_INVALID,
};

// 0x5C6390
static TigRect intgame_interface_window_frames[2] = {
    { 0, 0, 800, 41 },
    { 0, 441, 800, 159 },
};

// 0x5C63B0
static TigRect intgame_pc_lens_normal_dst_frame = { 311, 96, 178, 178 };

// 0x5C63C0
static TigRect intgame_pc_lens_fullscreen_dst_frame = { 311, 196, 178, 178 };

// 0x5C63D0
static int intgame_mt_window_index = -1;

// 0x5C63D8
static TigRect intgame_health_bar_frame = { 14, 472, 28, 88 };

// 0x5C63E8
static TigRect intgame_fatigue_bar_frame = { 754, 473, 28, 88 };

// 0x5C63F8
static IntgameIsoWindowTypeInfo intgame_number_boxes[INTGAME_COUNTER_COUNT] = {
    /*  INTGAME_COUNTER_HEALTH */ { { 15, 578, 29, 12 }, TIG_WINDOW_HANDLE_INVALID },
    /* INTGAME_COUNTER_FATIGUE */ { { 755, 578, 27, 12 }, TIG_WINDOW_HANDLE_INVALID },
    /*    INTGAME_COUNTER_FATE */ { { 190, 17, 24, 12 }, TIG_WINDOW_HANDLE_INVALID },
    /*   INTGAME_COUNTER_MONEY */ { { 104, 512, 50, 20 }, TIG_WINDOW_HANDLE_INVALID },
    /*    INTGAME_COUNTER_MANA */ { { 264, 562, 50, 20 }, TIG_WINDOW_HANDLE_INVALID },
    /*  INTGAME_COUNTER_POISON */ { { 15, 500, 29, 12 }, TIG_WINDOW_HANDLE_INVALID },
};

// 0x5C6470
static UiButtonInfo intgame_ammo_button_info = { 61, 509, 251, TIG_BUTTON_HANDLE_INVALID };

// 0x5C6480
static UiButtonInfo intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_COUNT] = {
    /*     INTGAME_SECONDARY_BUTTON_SKILLS */ { 693, 456, 472, TIG_BUTTON_HANDLE_INVALID },
    /*     INTGAME_SECONDARY_BUTTON_SPELLS */ { 649, 494, 473, TIG_BUTTON_HANDLE_INVALID },
    /*     INTGAME_SECONDARY_BUTTON_COMBAT */ { 86, 457, 470, TIG_BUTTON_HANDLE_INVALID },
    /* INTGAME_SECONDARY_BUTTON_SCHEMATICS */ { 693, 539, 471, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C64C0
static UiButtonInfo intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_COUNT] = {
    /*   INTGAME_PRIMARY_BUTTON_LOGBOOK */ { 41, 2, 187, TIG_BUTTON_HANDLE_INVALID },
    /*      INTGAME_PRIMARY_BUTTON_CHAR */ { 4, 2, 169, TIG_BUTTON_HANDLE_INVALID },
    /* INTGAME_PRIMARY_BUTTON_INVENTORY */ { 115, 2, 186, TIG_BUTTON_HANDLE_INVALID },
    /*       INTGAME_PRIMARY_BUTTON_MAP */ { 78, 2, 193, TIG_BUTTON_HANDLE_INVALID },
    /*      INTGAME_PRIMARY_BUTTON_FATE */ { 157, 9, 137, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6510
static int intgame_ui_primary_button_highlighted_icons[UI_PRIMARY_BUTTON_COUNT] = {
    /*      UI_PRIMARY_BUTTON_CHAR */ 561, // "char_on.art"
    /*   UI_PRIMARY_BUTTON_LOGBOOK */ 560, // "log_on.art"
    /*   UI_PRIMARY_BUTTON_TOWNMAP */ 558, // "tmap_on.art"
    /*  UI_PRIMARY_BUTTON_WORLDMAP */ 195, // "wmap_on.art"
    /* UI_PRIMARY_BUTTON_INVENTORY */ 559, // "invn_on.art"
};

// 0x5C6524
static int intgame_ui_primary_button_normal_icons[UI_PRIMARY_BUTTON_COUNT] = {
    /*      UI_PRIMARY_BUTTON_CHAR */ 169, // "char_but.art"
    /*   UI_PRIMARY_BUTTON_LOGBOOK */ 187, // "log_but.art"
    /*   UI_PRIMARY_BUTTON_TOWNMAP */ 193, // "tmap_but.art"
    /*  UI_PRIMARY_BUTTON_WORLDMAP */ 194, // "wmap_but.art"
    /* UI_PRIMARY_BUTTON_INVENTORY */ 186, // "invn_but.art"
};

// 0x5C6538
static UiButtonInfo intgame_sleep_button_info = { 605, 9, 137, TIG_BUTTON_HANDLE_INVALID };

// 0x5C6548
static UiButtonInfo intgame_rotwin_button_info[ROTWIN_TYPE_COUNT] = {
    /*        ROTWIN_TYPE_MSG */ { 196, 492, 354, TIG_BUTTON_HANDLE_INVALID },
    // CE: SKILLS / SPELLS chrome sits 1px higher than the other rotwin
    // arts to align with the inner UI elements (college buttons / spell
    // slots / skill widgets) that were placed relative to the visual
    // indent inside the art frame.
    /*     ROTWIN_TYPE_SPELLS */ { 196, 491, 8, TIG_BUTTON_HANDLE_INVALID },
    /*     ROTWIN_TYPE_SKILLS */ { 196, 491, 274, TIG_BUTTON_HANDLE_INVALID },
    /*       ROTWIN_TYPE_CHAT */ { 196, 492, 642, TIG_BUTTON_HANDLE_INVALID },
    /*      ROTWIN_TYPE_TRAPS */ { 196, 492, 290, TIG_BUTTON_HANDLE_INVALID },
    /*   ROTWIN_TYPE_DIALOGUE */ { 196, 492, 354, TIG_BUTTON_HANDLE_INVALID },
    /*   ROTWIN_TYPE_MAP_NOTE */ { 196, 492, 200, TIG_BUTTON_HANDLE_INVALID },
    /*  ROTWIN_TYPE_BROADCAST */ { 196, 492, 291, TIG_BUTTON_HANDLE_INVALID },
    /*  ROTWIN_TYPE_MAGICTECH */ { 196, 492, 564, TIG_BUTTON_HANDLE_INVALID },
    /*   ROTWIN_TYPE_QUANTITY */ { 196, 492, 298, TIG_BUTTON_HANDLE_INVALID },
    /* ROTWIN_TYPE_MP_KICKBAN */ { 196, 492, 842, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C65F8
static UiButtonInfo stru_5C65F8[] = {
    { 210, 545, 0, TIG_BUTTON_HANDLE_INVALID },
    { 210, 504, 0, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6618
static UiButtonInfo intgame_college_buttons[COLLEGE_COUNT] = {
    /*        COLLEGE_CONVEYANCE */ { 201, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        COLLEGE_DIVINATION */ { 226, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*               COLLEGE_AIR */ { 251, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             COLLEGE_EARTH */ { 276, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*              COLLEGE_FIRE */ { 301, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             COLLEGE_WATER */ { 326, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             COLLEGE_FORCE */ { 351, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            COLLEGE_MENTAL */ { 376, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*              COLLEGE_META */ { 400, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             COLLEGE_MORPH */ { 425, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            COLLEGE_NATURE */ { 450, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /* COLLEGE_NECROMANTIC_BLACK */ { 475, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /* COLLEGE_NECROMANTIC_WHITE */ { 500, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          COLLEGE_PHANTASM */ { 525, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         COLLEGE_SUMMONING */ { 550, 497, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          COLLEGE_TEMPORAL */ { 575, 497, -1, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6718
static UiButtonInfo intgame_spell_buttons[SPELL_COUNT] = {
    /*               SPELL_DISARM */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*    SPELL_UNLOCKING_CANTRIP */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_UNSEEN_FORCE */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*   SPELL_SPATIAL_DISTORTION */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_TELEPORTATION */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_SENSE_ALIGNMENT */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_SEE_CONTENTS */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_READ_AURA */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_SENSE_HIDDEN */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_DIVINE_MAGICK */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_VITALITY_OF_AIR */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*       SPELL_POISON_VAPOURS */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*           SPELL_CALL_WINDS */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          SPELL_BODY_OF_AIR */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*   SPELL_CALL_AIR_ELEMENTAL */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*    SPELL_STRENGTH_OF_EARTH */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          SPELL_STONE_THROW */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_WALL_OF_STONE */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_BODY_OF_STONE */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /* SPELL_CALL_EARTH_ELEMENTAL */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_AGILITY_OF_FIRE */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_WALL_OF_FIRE */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_FIREFLASH */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_BODY_OF_FIRE */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*  SPELL_CALL_FIRE_ELEMENTAL */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_PURITY_OF_WATER */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             SPELL_CALL_FOG */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_SQUALL_OF_ICE */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_BODY_OF_WATER */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /* SPELL_CALL_WATER_ELEMENTAL */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /* SPELL_SHIELD_OF_PROTECTION */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*                 SPELL_JOLT */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_WALL_OF_FORCE */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*    SPELL_BOLT_OF_LIGHTNING */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_DISINTEGRATE */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*                SPELL_CHARM */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*                 SPELL_STUN */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*           SPELL_DRAIN_WILL */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_NIGHTMARE */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_DOMINATE_WILL */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_RESIST_MAGICK */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_DISPERSE_MAGICK */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*       SPELL_DWEOMER_SHIELD */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_BONDS_OF_MAGICK */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*    SPELL_REFLECTION_SHIELD */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*       SPELL_HARDENED_HANDS */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*               SPELL_WEAKEN */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*               SPELL_SHRINK */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*       SPELL_FLESH_TO_STONE */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_POLYMORPH */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          SPELL_CHARM_BEAST */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             SPELL_ENTANGLE */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_CONTROL_BEAST */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_SUCCOUR_BEAST */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*           SPELL_REGENERATE */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*                 SPELL_HARM */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*       SPELL_CONJURE_SPIRIT */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_SUMMON_UNDEAD */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_CREATE_UNDEAD */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          SPELL_QUENCH_LIFE */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_MINOR_HEALING */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*          SPELL_HALT_POISON */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_MAJOR_HEALING */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_SANCTUARY */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*            SPELL_RESURRECT */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*           SPELL_ILLUMINATE */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*                SPELL_FLASH */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*           SPELL_BLUR_SIGHT */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*     SPELL_PHANTASMAL_FIEND */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_INVISIBILITY */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*    SPELL_PLAGUE_OF_INSECTS */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*      SPELL_ORCISH_CHAMPION */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*        SPELL_GUARDIAN_OGRE */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             SPELL_HELLGATE */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             SPELL_FAMILIAR */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*             SPELL_MAGELOCK */ { 284, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_CONGEAL_TIME */ { 334, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*               SPELL_HASTEN */ { 384, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*               SPELL_STASIS */ { 435, 528, -1, TIG_BUTTON_HANDLE_INVALID },
    /*         SPELL_TEMPUS_FUGIT */ { 485, 528, -1, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6C18
static UiButtonInfo intgame_mt_spell_buttons[] = {
    { 353, 542, -1, TIG_BUTTON_HANDLE_INVALID },
    { 402, 542, -1, TIG_BUTTON_HANDLE_INVALID },
    { 451, 542, -1, TIG_BUTTON_HANDLE_INVALID },
    { 501, 542, -1, TIG_BUTTON_HANDLE_INVALID },
    { 550, 542, -1, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6C68
static UiButtonInfo stru_5C6C68[4] = {
    { 288, 514, -1, TIG_BUTTON_HANDLE_INVALID },
    { 351, 514, -1, TIG_BUTTON_HANDLE_INVALID },
    { 414, 514, -1, TIG_BUTTON_HANDLE_INVALID },
    { 477, 514, -1, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6CA8
static UiButtonInfo stru_5C6CA8[] = {
    { 396, 550, 201, TIG_BUTTON_HANDLE_INVALID },
    { 511, 550, 202, TIG_BUTTON_HANDLE_INVALID },
    { 281, 549, 203, TIG_BUTTON_HANDLE_INVALID },
    { 213, 509, 146, TIG_BUTTON_HANDLE_INVALID },
    { 213, 536, 148, TIG_BUTTON_HANDLE_INVALID },
    { 213, 563, 145, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6D08
static UiButtonInfo intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_COUNT] = {
    /* INTGAME_QUANTITY_BUTTON_TAKE_ALL */ { 364, 548, 299, TIG_BUTTON_HANDLE_INVALID },
    /*     INTGAME_QUANTITY_BUTTON_PLUS */ { 479, 547, 805, TIG_BUTTON_HANDLE_INVALID },
    /*    INTGAME_QUANTITY_BUTTON_MINUS */ { 479, 559, 804, TIG_BUTTON_HANDLE_INVALID },
    /*       INTGAME_QUANTITY_BUTTON_OK */ { 545, 506, 33, TIG_BUTTON_HANDLE_INVALID },
    /*   INTGAME_QUANTITY_BUTTON_CANCEL */ { 545, 557, 32, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6D58
static RotatingWindowType dword_5C6D58 = ROTWIN_TYPE_INVALID;

// 0x5C6D60
struct IntgameIsoWindowTypeInfo intgame_rotwin_text_frame[ROTWIN_TYPE_COUNT] = {
    /*        ROTWIN_TYPE_MSG */ { { 211, 503, 383, 82 }, TIG_WINDOW_HANDLE_INVALID },
    // CE: SPELLS/SKILLS chrome shifted up 1px (see intgame_rotwin_button_info);
    // their inner text rows shift in lockstep so the rolling text stays
    // aligned with the chrome's visible text well.
    /*     ROTWIN_TYPE_SPELLS */ { { 208, 573, 387, 18 }, TIG_WINDOW_HANDLE_INVALID },
    /*     ROTWIN_TYPE_SKILLS */ { { 208, 566, 387, 18 }, TIG_WINDOW_HANDLE_INVALID },
    /*       ROTWIN_TYPE_CHAT */ { { 291, 566, 268, 19 }, TIG_WINDOW_HANDLE_INVALID },
    /*      ROTWIN_TYPE_TRAPS */ { { 208, 574, 387, 18 }, TIG_WINDOW_HANDLE_INVALID },
    /*   ROTWIN_TYPE_DIALOGUE */ { { 211, 507, 383, 84 }, TIG_WINDOW_HANDLE_INVALID },
    /*   ROTWIN_TYPE_MAP_NOTE */ { { 262, 508, 303, 24 }, TIG_WINDOW_HANDLE_INVALID },
    /*  ROTWIN_TYPE_BROADCAST */ { { 220, 503, 350, 82 }, TIG_WINDOW_HANDLE_INVALID },
    /*  ROTWIN_TYPE_MAGICTECH */ { { 355, 506, 227, 18 }, TIG_WINDOW_HANDLE_INVALID },
    /*   ROTWIN_TYPE_QUANTITY */ { { 0, 0, 0, 0 }, TIG_WINDOW_HANDLE_INVALID },
    /* ROTWIN_TYPE_MP_KICKBAN */ { { 0, 0, 0, 0 }, TIG_WINDOW_HANDLE_INVALID },
};

// 0x5C6E40
static UiButtonInfo intgame_maintain_buttons[] = {
    { 281, 3, 188, TIG_BUTTON_HANDLE_INVALID },
    { 331, 3, 189, TIG_BUTTON_HANDLE_INVALID },
    { 381, 3, 190, TIG_BUTTON_HANDLE_INVALID },
    { 431, 3, 191, TIG_BUTTON_HANDLE_INVALID },
    { 481, 3, 192, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6E90
static UiButtonInfo intgame_maintain_fs_buttons[] = {
    { 281, 3, 188, TIG_BUTTON_HANDLE_INVALID },
    { 331, 3, 189, TIG_BUTTON_HANDLE_INVALID },
    { 381, 3, 190, TIG_BUTTON_HANDLE_INVALID },
    { 431, 3, 191, TIG_BUTTON_HANDLE_INVALID },
    { 481, 3, 192, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6EE0
static TigRect intgame_maintain_window_rects[5] = {
    { 281, 3, 32, 32 },
    { 331, 3, 32, 32 },
    { 381, 3, 32, 32 },
    { 431, 3, 32, 32 },
    { 481, 3, 32, 32 },
};

// 0x5C6F30
static UiButtonInfo stru_5C6F30 = { 616, 455, 182, TIG_BUTTON_HANDLE_INVALID };

// 0x5C6F40
static UiButtonInfo intgame_recent_action_buttons[] = {
    { 69, 548, -1, TIG_BUTTON_HANDLE_INVALID },
    { 114, 548, -1, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C6F60
static int dword_5C6F60[] = {
    1,
    0,
};

// 0x5C6F68
static UiButtonInfo intgame_mt_button_info = { 161, 443, 563, TIG_BUTTON_HANDLE_INVALID };

// 0x5C6F78
static int intgame_rotwin_step = MAX_INTERFACE_WINDOW_ROTATION_STEPS;

// 0x5C6F80
static TigRect intgame_clock_frame = { 648, 5, 128, 30 };

// 0x5C6F90
static UiButtonInfo intgame_clock_button_info = { 0, 0, -1, TIG_BUTTON_HANDLE_INVALID };

// 0x5C6FA0
static int intgame_race_icons[RACE_COUNT] = {
    /*     RACE_HUMAN */ 375, // racehumanicon.art
    /*     RACE_DWARF */ 378, // racehelficon.art
    /*       RACE_ELF */ 376, // raceelficon.art
    /*  RACE_HALF_ELF */ 377, // racehelficon.art
    /*     RACE_GNOME */ 379, // racegnomeicon.art
    /*  RACE_HALFLING */ 380, // racehalflingicon.art
    /*  RACE_HALF_ORC */ 381, // racehorcicon.art
    /* RACE_HALF_OGRE */ 382, // racehogreicon.art
    /*  RACE_DARK_ELF */ 376, // raceelficon.art
    /*      RACE_OGRE */ 382, // racehogreicon.art
    /*       RACE_ORC */ 381, // racehorcicon.art
};

// 0x5C6FCC
static int intgame_alignment_icons[6] = {
    634, // "icon_lawfulevil.art"
    390, // "icon_evil.art"
    389, // "icon_chaotic.art"
    388, // "icon_lawful.art"
    387, // "icon_good.art"
    386, // "icon_lawfulgood.art"
};

// 0x5C6FE4
static int intgame_weapon_icons[TIG_ART_WEAPON_TYPE_COUNT] = {
    /*        TIG_ART_WEAPON_TYPE_NO_WEAPON */ 391,
    /*          TIG_ART_WEAPON_TYPE_UNARMED */ 391,
    /*           TIG_ART_WEAPON_TYPE_DAGGER */ 391,
    /*            TIG_ART_WEAPON_TYPE_SWORD */ 391,
    /*              TIG_ART_WEAPON_TYPE_AXE */ 391,
    /*             TIG_ART_WEAPON_TYPE_MACE */ 391,
    /*           TIG_ART_WEAPON_TYPE_PISTOL */ 392,
    /* TIG_ART_WEAPON_TYPE_TWO_HANDED_SWORD */ 391,
    /*              TIG_ART_WEAPON_TYPE_BOW */ 395,
    /*                TIG_ART_WEAPON_TYPE_9 */ 391,
    /*            TIG_ART_WEAPON_TYPE_RIFLE */ 392,
    /*               TIG_ART_WEAPON_TYPE_11 */ 391,
    /*               TIG_ART_WEAPON_TYPE_12 */ 391,
    /*            TIG_ART_WEAPON_TYPE_STAFF */ 391,
    /*               TIG_ART_WEAPON_TYPE_14 */ 391,
};

// 0x5C7020
static int intgame_ammo_icons[TIG_ART_AMMO_TYPE_COUNT] = {
    /*  TIG_ART_AMMO_TYPE_ARROW */ 398,
    /* TIG_ART_AMMO_TYPE_BULLET */ 399,
    /* TIG_ART_AMMO_TYPE_CHARGE */ 400,
    /*   TIG_ART_AMMO_TYPE_FUEL */ 401,
};

// 0x5C7030
static int intgame_armor_type_icons[TIG_ART_ARMOR_TYPE_COUNT] = {
    /*     TIG_ART_ARMOR_TYPE_UNDERWEAR */ 402,
    /*      TIG_ART_ARMOR_TYPE_VILLAGER */ 402,
    /*       TIG_ART_ARMOR_TYPE_LEATHER */ 405,
    /*         TIG_ART_ARMOR_TYPE_CHAIN */ 405,
    /*         TIG_ART_ARMOR_TYPE_PLATE */ 405,
    /*          TIG_ART_ARMOR_TYPE_ROBE */ 408,
    /* TIG_ART_ARMOR_TYPE_PLATE_CLASSIC */ 405,
    /*     TIG_ART_ARMOR_TYPE_BARBARIAN */ 405,
    /*  TIG_ART_ARMOR_TYPE_CITY_DWELLER */ 402,
};

// 0x5C7054
static int intgame_armor_coverage_icons[TIG_ART_ARMOR_COVERAGE_COUNT] = {
    /*     TIG_ART_ARMOR_COVERAGE_TORSO */ 0,
    /*    TIG_ART_ARMOR_COVERAGE_SHIELD */ 405,
    /*    TIG_ART_ARMOR_COVERAGE_HELMET */ 405,
    /* TIG_ART_ARMOR_COVERAGE_GAUNTLETS */ 405,
    /*     TIG_ART_ARMOR_COVERAGE_BOOTS */ 405,
    /*      TIG_ART_ARMOR_COVERAGE_RING */ 411,
    /* TIG_ART_ARMOR_COVERAGE_MEDALLION */ 414,
};

// 0x5C7070
static int intgame_written_icons[WRITTEN_TYPE_COUNT] = {
    /*      WRITTEN_TYPE_BOOK */ 428, // "booksicon.art"
    /*      WRITTEN_TYPE_NOTE */ 429, // "note_icon.art"
    /* WRITTEN_TYPE_NEWSPAPER */ 431, // "newpaper_icon.art"
    /*  WRITTEN_TYPE_TELEGRAM */ 430, // "telegram_icon.art"
    /*     WRITTEN_TYPE_IMAGE */ 428, // "booksicon.art"
    /* WRITTEN_TYPE_SCHEMATIC */ 462, // "schematic_icon.art"
    /*    WRITTEN_TYPE_PLAQUE */ 428, // "booksicon.art"
};

// 0x5C708C
static int intgame_message_icons[UI_MSG_TYPE_COUNT] = {
    /*       UI_MSG_TYPE_LEVEL */ 438, // "levelupicon.art" - Level Up Icon
    /*      UI_MSG_TYPE_POISON */ 439, // "poisoned_icon.art" - Poisoned Icon
    /*       UI_MSG_TYPE_CURSE */ 440, // "hexedicon.art" - Cursed Icon
    /*       UI_MSG_TYPE_BLESS */ 441, // "blessicon.art" - Blessed Icon
    /* UI_MSG_TYPE_EXCLAMATION */ 442, // "exclaimation_icon.art" - Exclamation Icon
    /*    UI_MSG_TYPE_QUESTION */ 443, // "question_icon.art" - Question Icon
    /*    UI_MSG_TYPE_FEEDBACK */ 444, // "levelupicon.art" - Arcanum Icon
    /*       UI_MSG_TYPE_SKILL */ 0,
    /*       UI_MSG_TYPE_SPELL */ 0,
    /*     UI_MSG_TYPE_COLLEGE */ 0,
    /*        UI_MSG_TYPE_TECH */ 0,
    /*      UI_MSG_TYPE_DEGREE */ 0,
    /*        UI_MSG_TYPE_STAT */ 0,
    /*   UI_MSG_TYPE_SCHEMATIC */ 0,
};

// 0x5C70C8
static TigRect stru_5C70C8 = { 290, 63, 291, 19 };

// 0x5C70D8
static TigRect stru_5C70D8 = { 290, 87, 291, 18 };

// 0x5C70E8
static TigRect stru_5C70E8 = { 290, 105, 291, 18 };

// 0x5C70F8
static TigRect stru_5C70F8 = { 290, 123, 291, 18 };

// 0x5C7108
static TigRect stru_5C7108 = { 290, 63, 291, 78 };

// 0x5C7118
static TigRect stru_5C7118 = { 290, 63, 291, 55 };

// 0x5C7128
static TigRect stru_5C7128 = { 290, 105, 291, 36 };

// 0x5C7138
static TigRect stru_5C7138 = { 290, 87, 291, 54 };

// 0x5C7148
static TigRect stru_5C7148 = { 217, 63, 364, 19 };

// 0x5C7158
static TigRect stru_5C7158 = { 217, 63, 364, 78 };

// 0x5C7168
static TigRect stru_5C7168 = { 217, 87, 364, 54 };

// 0x5C7178
static int intgame_mode_cursors[INTGAME_MODE_COUNT] = {
    /*         INTGAME_MODE_MAIN */ -1,
    /*        INTGAME_MODE_SPELL */ 21,
    /*        INTGAME_MODE_SKILL */ 352,
    /*       INTGAME_MODE_DIALOG */ 0,
    /*       INTGAME_MODE_BARTER */ 0,
    /*         INTGAME_MODE_WMAP */ 0,
    /*        INTGAME_MODE_SLEEP */ 0,
    /*      INTGAME_MODE_LOGBOOK */ 0,
    /*        INTGAME_MODE_INVEN */ 0,
    /*     INTGAME_MODE_CHAREDIT */ 0,
    /*         INTGAME_MODE_LOOT */ 0,
    /*        INTGAME_MODE_STEAL */ 0,
    /*           INTGAME_MODE_12 */ 0,
    /*     INTGAME_MODE_QUANTITY */ 0,
    /*    INTGAME_MODE_SCHEMATIC */ 0,
    /*      INTGAME_MODE_WRITTEN */ 0,
    /*         INTGAME_MODE_ITEM */ 21,
    /*           INTGAME_MODE_17 */ 0,
    /*     INTGAME_MODE_FOLLOWER */ -1,
    /* INTGAME_MODE_NPC_IDENTIFY */ 0,
    /*   INTGAME_MODE_NPC_REPAIR */ 0,
};

// 0x5C71D0
static UiButtonInfo stru_5C71D0[10] = {
    { 211, 37, 773, TIG_BUTTON_HANDLE_INVALID },
    { 249, 37, 774, TIG_BUTTON_HANDLE_INVALID },
    { 287, 37, 775, TIG_BUTTON_HANDLE_INVALID },
    { 327, 37, 776, TIG_BUTTON_HANDLE_INVALID },
    { 365, 37, 777, TIG_BUTTON_HANDLE_INVALID },
    { 403, 37, 778, TIG_BUTTON_HANDLE_INVALID },
    { 439, 37, 779, TIG_BUTTON_HANDLE_INVALID },
    { 478, 37, 780, TIG_BUTTON_HANDLE_INVALID },
    { 516, 37, 781, TIG_BUTTON_HANDLE_INVALID },
    { 555, 37, 782, TIG_BUTTON_HANDLE_INVALID },
};

// 0x5C7270
static UiButtonInfo stru_5C7270 = { 216, 47, 772, TIG_BUTTON_HANDLE_INVALID };

// 0x5C7280
static uint64_t qword_5C7280 = TGT_OBJECT;

// 0x5C7288
static tig_window_handle_t intgame_fs_hotkey_window = TIG_WINDOW_HANDLE_INVALID;

// 0x5C728C
static int dword_5C728C[TIG_ART_AMMO_TYPE_COUNT] = {
    /*  TIG_ART_AMMO_TYPE_ARROW */ 250,
    /* TIG_ART_AMMO_TYPE_BULLET */ 251,
    /* TIG_ART_AMMO_TYPE_CHARGE */ 252,
    /*   TIG_ART_AMMO_TYPE_FUEL */ 253,
};

static int dword_5C729C[] = {
    0,
    199,
    495,
    80,
    48,
};

// 0x5C72B0
static int dword_5C72B0 = 1;

// 0x5C72B4
static bool intgame_mode_scrolling[INTGAME_MODE_COUNT] = {
    /*         INTGAME_MODE_MAIN */ true,
    /*        INTGAME_MODE_SPELL */ true,
    /*        INTGAME_MODE_SKILL */ true,
    /*       INTGAME_MODE_DIALOG */ true,
    /*       INTGAME_MODE_BARTER */ false,
    /*         INTGAME_MODE_WMAP */ true,
    // CE: sleep panel is a small overlay; the world stays visible
    // behind it and the menu has no keyboard navigation (selection
    // is mouse-only). Leaving scrolling enabled lets the player
    // edge-scroll / arrow-key the camera while deciding whether to
    // sleep — useful for checking surroundings without dismissing.
    /*        INTGAME_MODE_SLEEP */ true,
    /*      INTGAME_MODE_LOGBOOK */ false,
    /*        INTGAME_MODE_INVEN */ false,
    /*     INTGAME_MODE_CHAREDIT */ false,
    /*         INTGAME_MODE_LOOT */ false,
    /*        INTGAME_MODE_STEAL */ false,
    /*           INTGAME_MODE_12 */ false,
    /*     INTGAME_MODE_QUANTITY */ false,
    /*    INTGAME_MODE_SCHEMATIC */ false,
    /*      INTGAME_MODE_WRITTEN */ false,
    /*         INTGAME_MODE_ITEM */ true,
    /*           INTGAME_MODE_17 */ false,
    /*     INTGAME_MODE_FOLLOWER */ true,
    /* INTGAME_MODE_NPC_IDENTIFY */ false,
    /*   INTGAME_MODE_NPC_REPAIR */ false,
};

// 0x5C7308
static int dword_5C7308 = -1;

// 0x5C730C
static int dword_5C730C[8] = {
    0,
    4,
    9,
    13,
    14,
    18,
    23,
    27,
};

// 0x5C732C
static unsigned int intgame_penalty_flags[INTGAME_PENALTY_COUNT] = {
    /*          INTGAME_PENALTY_MSR */ SKILL_INVOCATION_PENALTY_MSR,
    /*        INTGAME_PENALTY_RANGE */ SKILL_INVOCATION_PENALTY_RANGE,
    /*   INTGAME_PENALTY_PERCEPTION */ SKILL_INVOCATION_PENALTY_PERCEPTION,
    /*        INTGAME_PENALTY_COVER */ SKILL_INVOCATION_PENALTY_COVER,
    /*        INTGAME_PENALTY_LIGHT */ SKILL_INVOCATION_PENALTY_LIGHT,
    /*       INTGAME_PENALTY_INJURY */ SKILL_INVOCATION_PENALTY_INJURY,
    /* INTGAME_PENALTY_BLOCKED_SHOT */ SKILL_INVOCATION_BLOCKED_SHOT,
    /*   INTGAME_PENALTY_MAGIC_TECH */ SKILL_INVOCATION_MAGIC_TECH_PENALTY,
};

// 0x5C734C
static int intgame_penalty_icons[INTGAME_PENALTY_COUNT] = {
    /*          INTGAME_PENALTY_MSR */ 586, // "pen_msr.art"
    /*        INTGAME_PENALTY_RANGE */ 588, // "pen_range.art"
    /*   INTGAME_PENALTY_PERCEPTION */ 587, // "pen_perception.art"
    /*        INTGAME_PENALTY_COVER */ 583, // "pen_cover.art"
    /*        INTGAME_PENALTY_LIGHT */ 585, // "pen_light.art"
    /*       INTGAME_PENALTY_INJURY */ 584, // "pen_injury.art"
    /* INTGAME_PENALTY_BLOCKED_SHOT */ 845, // "blockedshot.art"
    /*   INTGAME_PENALTY_MAGIC_TECH */ 846, // "magic-tech-penalty.art"
};

// 0x5C736C
static int intgame_penalty_slot_x[INTGAME_PENALTY_SLOTS] = {
    /*        INTGAME_PENALTY_MSR */ 210,
    /*      INTGAME_PENALTY_RANGE */ 248,
    /* INTGAME_PENALTY_PERCEPTION */ 210,
    /*      INTGAME_PENALTY_COVER */ 248,
    /*      INTGAME_PENALTY_LIGHT */ 210,
    /*     INTGAME_PENALTY_INJURY */ 248,
};

// 0x5C7384
static int intgame_penalty_slot_y[INTGAME_PENALTY_SLOTS] = {
    /*        INTGAME_PENALTY_MSR */ 84,
    /*      INTGAME_PENALTY_RANGE */ 84,
    /* INTGAME_PENALTY_PERCEPTION */ 104,
    /*      INTGAME_PENALTY_COVER */ 104,
    /*      INTGAME_PENALTY_LIGHT */ 124,
    /*     INTGAME_PENALTY_INJURY */ 124,
};

// 0x5C739C
static int intgame_iso_window_width = 800;

// 0x5C73A0
static int intgame_iso_window_height = 600;

// 0x5C73A4
static tig_window_handle_t intgame_iso_window = TIG_WINDOW_HANDLE_INVALID;

// 0x64C470
static tig_font_handle_t intgame_morph15_blue_font;

// 0x64C474
static TigVideoBuffer* dword_64C474;

// 0x64C478
static int intgame_max_quantity;

// 0x64C47C
static int dword_64C47C[2];

// 0x64C484
static int intgame_ui_primary_button_icons[UI_PRIMARY_BUTTON_COUNT];

// 0x64C498
static tig_font_handle_t intgame_flare12_white_font;

// 0x64C49C
static tig_font_handle_t intgame_flare12_red_font;

// 0x64C4A0
static tig_font_handle_t intgame_flare14_white_font;

// 0x64C4A8
static UiButtonInfo stru_64C4A8[5];

// 0x64C4F8
static tig_window_handle_t dword_64C4F8[2];

// 0x64C500
static tig_font_handle_t intgame_flare12_blue_font;

// 0x64C504
static mes_file_handle_t intgame_mes_file;

// 0x64C508
static tig_window_handle_t intgame_big_window_handle;

// 0x64C510
static TigRect intgame_pc_lens_dst_rect;

// 0x64C520
static PcLens intgame_pc_lens;

// 0x64C52C
static tig_window_handle_t dword_64C52C;

// 0x64C530
static int dword_64C530;

// 0x64C534
static UiPrimaryButton intgame_map_button;

// 0x64C538
static tig_font_handle_t intgame_morph15_orange_font;

// 0x64C540
static UiMessage intgame_message_history[MAX_MESSAGE_HISTORY_ITEMS];

// 0x64C630
static bool intgame_big_window_locked;

// CE: see intgame_big_window_close_animated — true while a deferred
// animated-exit teardown is pending (declared here so the lock function
// above the unlock block can consult it).
static bool intgame_big_window_exit_pending = false;

// 0x64C634
static IntgameMode intgame_mode_stack[11];

// 0x64C660
static TigRect intgame_pc_lens_src_rect;

// 0x64C670
static tig_font_handle_t intgame_cloister18_font;

// 0x64C674
static int dword_64C674;

// 0x64C678
static int intgame_quantity;

// 0x64C67C
static bool intgame_compact_interface;

// 0x64C680
static bool intgame_fullscreen;

// 0x64C688
static int64_t qword_64C688;

// 0x64C690
static int64_t qword_64C690;

// 0x64C698
static TigRect stru_64C698;

// 0x64C6A8
static RotatingWindowType intgame_iso_window_type;

// CE: rotating-window page saved by intgame_load to restore once the
// post-load "enter world" sequence has settled (ROTWIN_RESTORE_KEY).
// Restoring inside intgame_load is too early — the load completion path
// re-runs message/interface refreshes that force the window back to MSG,
// clobbering an in-load swap. intgame_apply_rotwin_restore() applies this
// as the last step of that path. ROTWIN_TYPE_INVALID = nothing pending.
static RotatingWindowType intgame_rotwin_restore_pending = ROTWIN_TYPE_INVALID;

// 0x64C6AC
static RotatingWindowType dword_64C6AC;

// 0x64C6B0
static bool dword_64C6B0;

// 0x64C6B4
static bool intgame_iso_interface_created;

// CE: forward-declared early because intgame_iso_strips_show_as_band
// (which sets band_mode) and sub_54B5D0 (which reads shell_hidden)
// live above the slide module's main static block. The defining
// declarations with initializers are further down alongside the
// other slide state.
static bool intgame_hud_band_mode;
static bool intgame_hud_shell_hidden;

// CE: translucent-black tint pathway opt-in flag. Set true when the
// bar is created with TRANSLUCENT_BLACK_UI_KEY on. Read by:
//   - intgame_hud_tick_invalidate_alpha_strips (per-tick: marks iso
//     under the bar dirty so iso_redraw refreshes it)
//   - intgame_hud_tick_apply_tint (per-tick after iso_redraw: tints
//     the freshly-rendered iso pixels under the bar)
static bool intgame_hud_bar_uses_tint = false;

// 0x64C6B8
static int intgame_mode_stack_size;

// 0x64C6BC
static PcLensMode intgame_pc_lens_mode;

// 0x64C6C0
static int intgame_message_history_size;

// 0x64C6C4
static int intgame_message_history_end;

// 0x64C6C8
static int intgame_message_history_curr;

// 0x64C6CC
static bool (*intgame_dialog_process_event_func)(TigMessage* msg);

// 0x64C6D0
static int dword_64C6D0;

// 0x64C6D4
static void (*dword_64C6D4)(UiMessage* ui_message);

// 0x64C6D8
static int dword_64C6D8;

// 0x64C6DC
static bool intgame_fullscreen_forced;

// 0x64C6E0
static bool dword_64C6E0;

// 0x64C6E4
static TigVideoBuffer* intgame_pc_lens_video_buffer;

// 0x64C6E8
static bool dword_64C6E8;

// 0x64C6F0
static unsigned int intgame_iso_window_flags;

// 0x739F88
tig_font_handle_t intgame_morph15_white_font;

// 0x549B70
bool intgame_init(GameInitInfo* init_info)
{
    TigFont font;

    (void)init_info;

    if (!mes_load("mes\\intgame.mes", &intgame_mes_file)) {
        return false;
    }

    if (!intgame_big_window_create()) {
        mes_unload(intgame_mes_file);
        return false;
    }

    font.flags = 0;
    tig_art_interface_id_create(27, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(255, 255, 255);
    tig_font_create(&font, &intgame_morph15_white_font);

    font.flags = 0;
    tig_art_interface_id_create(27, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(100, 100, 255);
    tig_font_create(&font, &intgame_morph15_blue_font);

    font.flags = 0;
    tig_art_interface_id_create(27, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(255, 114, 0);
    tig_font_create(&font, &intgame_morph15_orange_font);

    font.flags = 0;
    tig_art_interface_id_create(229, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(255, 255, 255);
    tig_font_create(&font, &intgame_flare12_white_font);

    font.flags = 0;
    tig_art_interface_id_create(229, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(255, 0, 0);
    tig_font_create(&font, &intgame_flare12_red_font);

    font.flags = 0;
    tig_art_interface_id_create(229, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(0, 0, 255);
    tig_font_create(&font, &intgame_flare12_blue_font);

    font.flags = 0;
    tig_art_interface_id_create(230, 0, 0, 0, &(font.art_id));
    font.str = NULL;
    font.color = tig_color_make(255, 255, 255);
    tig_font_create(&font, &intgame_flare14_white_font);

    memcpy(intgame_ui_primary_button_icons, intgame_ui_primary_button_normal_icons, sizeof(intgame_ui_primary_button_icons));
    intgame_map_button = UI_PRIMARY_BUTTON_TOWNMAP;
    dword_64C674 = -1;

    return true;
}

// 0x549F00
void intgame_reset(void)
{
    int index;

    dword_64C6D8 = 0;
    // CE: drop any rotwin-restore queued by a prior (failed) load so it
    // can't fire on a later unrelated menu-close. A real load re-queues it
    // after this reset runs. See intgame_apply_rotwin_restore().
    intgame_rotwin_restore_pending = ROTWIN_TYPE_INVALID;
    intgame_refresh_cursor();
    hotkey_ui_reset_recent_actions();
    intgame_clock_process_callback(NULL);
    iso_interface_window_swap(ROTWIN_TYPE_MSG);

    for (index = 0; index < 10; index++) {
        sub_57F210(index);
    }

    intgame_map_button = UI_PRIMARY_BUTTON_TOWNMAP;
    memcpy(intgame_ui_primary_button_icons, intgame_ui_primary_button_normal_icons, sizeof(intgame_ui_primary_button_icons));
    sub_54AA30();
}

// 0x549F60
void intgame_resize(GameResizeInfo* resize_info)
{
    int index;
    TigWindowData window_data;
    TigRect rect;

    hotkey_ui_end();
    dword_64C52C = resize_info->window_handle;

    if (intgame_fs_hotkey_window != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_destroy(intgame_fs_hotkey_window);
        intgame_fs_hotkey_window = TIG_WINDOW_HANDLE_INVALID;
    }

    if (!intgame_compact_interface) {
        hotkey_ui_start(dword_64C4F8[1], &(intgame_interface_window_frames[1]), dword_64C4F8[1], false);

        for (index = 0; index < 5; index++) {
            tig_window_hide(intgame_maintain_fs_windows[index]);
        }
    } else {
        window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP;
        window_data.rect.x = 196;
        window_data.rect.y = 563;
        window_data.rect.width = 411;
        window_data.rect.height = 37;
        window_data.color_key = tig_color_make(5, 5, 5);
        hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);

        if (tig_window_create(&window_data, &intgame_fs_hotkey_window) != TIG_OK) {
            tig_debug_printf("intgame_resize: ERROR: couldn't create window!");
            tig_exit();
        }

        rect.x = 0;
        rect.y = 0;
        rect.width = window_data.rect.width;
        rect.height = window_data.rect.height;
        tig_window_fill(intgame_fs_hotkey_window,
            &rect,
            tig_color_make(5, 5, 5));

        rect.x = 196;
        rect.y = 563;
        rect.width = 411;
        rect.height = 37;
        hotkey_ui_start(intgame_fs_hotkey_window, &rect, TIG_WINDOW_HANDLE_INVALID, true);

        for (index = 0; index < 5; index++) {
            if (spell_ui_maintain_has(index)) {
                tig_window_show(intgame_maintain_fs_windows[index]);
            }
        }
    }

    if (tig_window_is_hidden(dword_64C52C)) {
        intgame_hide();
    }
}

// 0x54A130
void intgame_exit(void)
{
    tig_font_destroy(intgame_morph15_white_font);
    tig_font_destroy(intgame_morph15_blue_font);
    tig_font_destroy(intgame_morph15_orange_font);
    tig_font_destroy(intgame_flare12_white_font);
    tig_font_destroy(intgame_flare12_red_font);
    tig_font_destroy(intgame_flare12_blue_font);
    tig_font_destroy(intgame_flare14_white_font);
    intgame_big_window_destroy();
    mes_unload(intgame_mes_file);
}

// 0x54A1A0
bool intgame_save(TigFile* stream)
{
    if (stream == NULL) return false;
    if (tig_file_fwrite(&intgame_iso_window_type, sizeof(intgame_iso_window_type), 1, stream) != 1) return false;
    if (tig_file_fwrite(&dword_64C530, sizeof(dword_64C530), 1, stream) != 1) return false;
    if (!hotkey_ui_save(stream)) return false;
    if (tig_file_fwrite(intgame_ui_primary_button_icons, sizeof(*intgame_ui_primary_button_icons), UI_PRIMARY_BUTTON_COUNT, stream) != UI_PRIMARY_BUTTON_COUNT) return false;
    if (tig_file_fwrite(&intgame_map_button, sizeof(intgame_map_button), 1, stream) != 1) return false;

    return true;
}

// 0x54A220
bool intgame_load(GameLoadInfo* load_info)
{
    int v1;
    int btn;
    int64_t obj;

    if (load_info->stream == NULL) return false;
    if (tig_file_fread(&v1, sizeof(v1), 1, load_info->stream) != 1) return false;
    if (tig_file_fread(&dword_64C530, sizeof(dword_64C530), 1, load_info->stream) != 1) return false;

    // CE: ROTWIN_RESTORE_KEY (default off). intgame_save writes the active
    // rotating-window page (intgame_iso_window_type) as its first field,
    // but the original load read it into a local and discarded it, so a
    // load always dropped back to the default Messages page. When enabled,
    // remember the saved page and re-open it once the load finishes — see
    // intgame_apply_rotwin_restore(). It cannot be applied here: this runs
    // mid-load (gameuilib_reset already swapped the window to MSG, and the
    // post-load enter-world path re-forces MSG via the message refresh), so
    // an in-load swap is immediately clobbered.
    //
    // Only the persistent, user-toggled pages (SPELLS / SKILLS) are
    // restored. MAGICTECH and the transient pages (dialogue, quantity, ...)
    // are bound to per-interaction context (e.g. qword_64C688, the examined
    // magic/tech item) that does not survive a load, so restoring them
    // would show a blank page — leave those at the default.
    if (settings_get_value(&settings, ROTWIN_RESTORE_KEY)
        && (v1 == ROTWIN_TYPE_SPELLS || v1 == ROTWIN_TYPE_SKILLS)) {
        intgame_rotwin_restore_pending = (RotatingWindowType)v1;
    }

    if (!hotkey_ui_load(load_info)) return false;
    if (tig_file_fread(intgame_ui_primary_button_icons, sizeof(*intgame_ui_primary_button_icons), UI_PRIMARY_BUTTON_COUNT, load_info->stream) != UI_PRIMARY_BUTTON_COUNT) return false;
    if (tig_file_fread(&intgame_map_button, sizeof(intgame_map_button), 1, load_info->stream) != 1) return false;

    for (btn = 0; btn < UI_PRIMARY_BUTTON_COUNT; btn++) {
        intgame_refresh_primary_button(btn);
    }

    intgame_counters_refresh();
    intgame_refresh_cursor();
    intgame_mt_button_enable();

    obj = player_get_local_pc_obj();
    if (obj != OBJ_HANDLE_NULL) {
        intgame_refresh_experience_gauges(obj);
        intgame_draw_counter(INTGAME_COUNTER_FATE,
            stat_level_get(obj, STAT_FATE_POINTS),
            2);
    }

    return true;
}

// CE: apply a rotating-window page restore queued by intgame_load
// (ROTWIN_RESTORE_KEY). Called at the tail of the post-load enter-world
// path (mainmenu sub_5412E0), after the interface is shown and every
// MSG-forcing refresh has run, so the swap is the last word. No-op unless
// a load queued a page (so it is harmless on new-game / menu-resume).
void intgame_apply_rotwin_restore(void)
{
    RotatingWindowType window_type;

    if (intgame_rotwin_restore_pending == ROTWIN_TYPE_INVALID) {
        return;
    }

    window_type = intgame_rotwin_restore_pending;
    intgame_rotwin_restore_pending = ROTWIN_TYPE_INVALID;

    if (!intgame_iso_interface_created) {
        return;
    }

    // CE: drive the SAME path a real spellbook/skills click takes, not a
    // bare iso_interface_window_swap. The toggle latches the secondary
    // button PRESSED and calls intgame_force_fullscreen — without that the
    // page paints once but the button looks up and the next world-hover
    // takes over the rotwin and never returns. At this point (post-load,
    // fresh reset) the buttons are RELEASED, so the toggle runs its
    // activate branch, exactly reproducing a user click.
    switch (window_type) {
    case ROTWIN_TYPE_SPELLS:
        intgame_secondary_button_toggle(INTGAME_SECONDARY_BUTTON_SPELLS, ROTWIN_TYPE_SPELLS);
        break;
    case ROTWIN_TYPE_SKILLS:
        intgame_secondary_button_toggle(INTGAME_SECONDARY_BUTTON_SKILLS, ROTWIN_TYPE_SKILLS);
        break;
    default:
        break;
    }
}

// 0x54A330
void iso_interface_create(tig_window_handle_t window_handle)
{
    TigWindowData window_data;
    TigVideoBufferCreateInfo vb_create_info;
    TigArtBlitInfo art_blit_info;
    int index;
    tig_art_id_t art_id;
    TigArtAnimData art_anim_data;
    TigArtFrameData art_frame_data;
    int iwid;
    TigFont font_desc;

    intgame_pc_lens_dst_rect = intgame_pc_lens_normal_dst_frame;

    // CE: These adjustments should be on par with `intgame_toggle_interface`.
    intgame_pc_lens_dst_rect.x = (800 - intgame_pc_lens_dst_rect.width) / 2;
    intgame_pc_lens_dst_rect.y = (600 - intgame_pc_lens_dst_rect.height) / 2;
    hrp_apply(&intgame_pc_lens_dst_rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

    dword_64C52C = window_handle;
    dword_64C6B0 = 1;
    intgame_iso_interface_created = false;

    tig_window_data(window_handle, &window_data);

    vb_create_info.flags = 0;
    vb_create_info.width = intgame_health_bar_frame.width / 2;
    vb_create_info.height = intgame_health_bar_frame.height;
    vb_create_info.background_color = 0;
    if (tig_video_buffer_create(&vb_create_info, &dword_64C474) != TIG_OK) {
        tig_debug_printf("iso_interface_create: ERROR: couldn't create video buffer!\n");
        exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE
    }

    window_data.flags = TIG_WINDOW_MESSAGE_FILTER;
    window_data.message_filter = iso_interface_message_filter;

    art_blit_info.flags = 0;
    art_blit_info.src_rect = &(window_data.rect);
    art_blit_info.dst_rect = &(window_data.rect);

    for (index = 0; index < 2; index++) {
        if (index == 0) {
            tig_art_interface_id_create(185, 0, 0, 0, &art_id);
        } else {
            tig_art_interface_id_create(184, 0, 0, 0, &art_id);
        }

        if (tig_art_anim_data(art_id, &art_anim_data) != TIG_OK) {
            tig_debug_printf("iso_interface_create: ERROR: couldn't grab art anim data!\n");
            exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE
        }

        window_data.background_color = art_anim_data.color_key;
        window_data.rect = intgame_interface_window_frames[index];
        art_blit_info.art_id = art_id;

        switch (index) {
        case 0:
            hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);
            break;
        case 1:
            hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
            break;
        }

        if (tig_window_create(&window_data, &(dword_64C4F8[index])) != TIG_OK) {
            tig_debug_printf("iso_interface_create: ERROR: couldn't create window!\n");
            exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE
        }

        window_data.rect.x = 0;
        window_data.rect.y = 0;
        tig_window_blit_art(dword_64C4F8[index], &art_blit_info);

        // CE: bottom strip opts into the translucent-black tint
        // pathway: the compositor runs tig_video_blit_near_black_tinted
        // for this window, replacing near-black source pixels with
        // subtract-tinted underlay pixels at blit time. Direct-paint
        // architecture — bypasses the layered screen-surface route
        // that fails for the iso world (which uses VIDEO_MEMORY and
        // doesn't reliably paint to the compositor's intermediate
        // surface).
        //
        // Underlay + subtract amount are picked by
        // intgame_refresh_hud_bar_tint based on current UI context
        // (mainmenu backdrop with heavier subtract when the mainmenu
        // is up — covers the pre-game new-char/charedit flow where
        // the bar is visible over the mainmenu_bg backdrop — or iso
        // world with the standard subtract during active gameplay).
        // The refresh helper gets re-invoked from mainmenu open/close
        // transitions so the underlay tracks context as it changes.
        if (index == 1 && settings_get_value(&settings, TRANSLUCENT_BLACK_UI_KEY)) {
            intgame_hud_bar_uses_tint = true;
            intgame_refresh_hud_bar_tint();
        }
    }

    for (index = 0; index < 5; index++) {
        intgame_button_create(&(intgame_maintain_buttons[index]));
        tig_button_hide(intgame_maintain_buttons[index].button_handle);
    }

    for (index = 0; index < INTGAME_SECONDARY_BUTTON_COUNT; index++) {
        if (index == INTGAME_SECONDARY_BUTTON_SKILLS
            || index == INTGAME_SECONDARY_BUTTON_SPELLS) {
            button_create_flags(&(intgame_secondary_buttons[index]), 0x2);
        } else {
            intgame_button_create(&(intgame_secondary_buttons[index]));
        }
    }

    for (index = 0; index < INTGAME_PRIMARY_BUTTON_COUNT; index++) {
        intgame_button_create(&(intgame_primary_buttons[index]));
    }

    intgame_button_create(&intgame_sleep_button_info);
    sub_5501C0();

    intgame_iso_interface_created = true;
    dword_64C530 = 0;

    // CE: now that the iso world window exists, refresh the modal-
    // dialog auto-tint underlay choice. With no mainmenu up, this
    // resolves to the iso world — so modals created from here on
    // (in-game quit/confirm dialogs, save-overwrite prompts) get
    // the translucent-black tint applied with iso as the underlay.
    // The same helpers are called from iso_interface_destroy and
    // from mainmenu open/close to keep both the modal underlay and
    // the HUD bar's underlay correct as context changes. The HUD
    // bar's initial picker call already ran above (it just got
    // opted into the tint pathway) but pick it again here in case
    // the mainmenu was somehow up during iso_interface_create.
    intgame_refresh_modal_tint();
    intgame_refresh_hud_bar_tint();

    for (index = 0; index < 11; index++) {
        iwid = find_interface_window_index(intgame_rotwin_text_frame[index].rect.x, intgame_rotwin_text_frame[index].rect.y);
        if (iwid == -1) {
            tig_debug_printf("iso_interface_create: ERROR: find iwid match!\n");
            exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE
        }

        intgame_rotwin_text_frame[index].rect.x -= intgame_interface_window_frames[iwid].x;
        intgame_rotwin_text_frame[index].rect.y -= intgame_interface_window_frames[iwid].y;
        intgame_rotwin_text_frame[index].window_handle = dword_64C4F8[iwid];
    }

    for (index = 0; index < INTGAME_COUNTER_COUNT; index++) {
        iwid = find_interface_window_index(intgame_number_boxes[index].rect.x, intgame_number_boxes[index].rect.y);
        if (iwid == -1) {
            tig_debug_printf("iso_interface_create: ERROR: find iwid match!\n");
            exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE
        }

        intgame_number_boxes[index].rect.x -= intgame_interface_window_frames[iwid].x;
        intgame_number_boxes[index].rect.y -= intgame_interface_window_frames[iwid].y;
        intgame_number_boxes[index].window_handle = dword_64C4F8[iwid];
    }

    hotkey_ui_start(dword_64C4F8[1], &(intgame_interface_window_frames[1]), dword_64C4F8[1], false);
    intgame_button_create(&stru_5C6F30);
    intgame_button_create(&intgame_mt_button_info);
    intgame_mt_button_disable();

    intgame_mode_stack_size = 0;
    intgame_mode_stack[0] = INTGAME_MODE_MAIN;
    target_flags_set(TGT_OBJECT | TGT_OBJ_NO_T_WALL | TGT_TILE);
    intgame_pc_lens_mode = PC_LENS_MODE_NONE;

    font_desc.str = NULL;
    tig_font_measure(&font_desc);
    tig_art_interface_id_create(171, 0, 0, 0, &(font_desc.art_id));
    tig_font_create(&font_desc, &intgame_cloister18_font);

    intgame_clock_button_info.x = intgame_clock_frame.x;
    intgame_clock_button_info.y = intgame_clock_frame.y;
    button_create_no_art(&intgame_clock_button_info, intgame_clock_frame.width, intgame_clock_frame.height);

    if (tig_art_interface_id_create(207, 0, 0, 0, &art_id) != TIG_OK
        || tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
        tig_debug_printf("iso_interface_create: ERROR: clock stuff failed!\n");
        exit(EXIT_FAILURE);
    }
    dword_64C47C[0] = art_frame_data.width;

    if (tig_art_interface_id_create(208, 0, 0, 0, &art_id) != TIG_OK
        || tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
        tig_debug_printf("iso_interface_create: ERROR: clock stuff failed!\n");
        exit(EXIT_FAILURE);
    }
    dword_64C47C[1] = art_frame_data.width;

    intgame_clock_process_callback(NULL);
    intgame_draw_bar(INTGAME_BAR_HEALTH);
    intgame_draw_bar(INTGAME_BAR_FATIGUE);

    // NOTE: Looks meaningless.
    font_desc.str = NULL;
    tig_font_measure(&font_desc);
    font_desc.flags = ~TIG_FONT_SHADOW;
    tig_art_interface_id_create(230, 0, 0, 0, &(font_desc.art_id));

    intgame_counters_refresh();

    for (index = 0; index < MAX_MESSAGE_HISTORY_ITEMS; index++) {
        intgame_message_history[index].str = (char*)MALLOC(MAX_MESSAGE_HISTORY_STRING_SIZE);
    }

    intgame_mt_window_index = find_interface_window_index(intgame_rotwin_button_info[ROTWIN_TYPE_MAGICTECH].x, intgame_rotwin_button_info[ROTWIN_TYPE_MAGICTECH].y);
    if (intgame_mt_window_index == -1) {
        tig_debug_printf("Intgame: ERROR: Couldn't match magic-tech window!\n");
        exit(EXIT_FAILURE);
    }

    if (intgame_is_compact_interface()) {
        for (index = 0; index < 2; index++) {
            tig_window_hide(dword_64C4F8[index]);
        }
    }

    for (index = 0; index < 5; index++) {
        window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP;
        window_data.rect = intgame_maintain_window_rects[index];
        window_data.color_key = tig_color_make(5, 5, 5);
        hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);

        if (tig_window_create(&window_data, &(intgame_maintain_fs_windows[index])) != TIG_OK) {
            tig_debug_printf("intgame_resize: ERROR: Couldn't create spellFSWid: %d!\n", index);
            tig_exit();
            return;
        }

        // FIXME: Wrong rect (should be at 0,0).
        tig_window_fill(intgame_maintain_fs_windows[index],
            &(intgame_maintain_window_rects[index]),
            tig_color_make(0, 0, 0));
    }

    for (index = 0; index < 5; index++) {
        intgame_button_create_ex(intgame_maintain_fs_windows[index],
            &(intgame_maintain_window_rects[index]),
            &(intgame_maintain_fs_buttons[index]),
            true);
        tig_button_hide(intgame_maintain_fs_buttons[index].button_handle);
    }

    // CE: bars were just created at rest. Snap them off-screen now
    // so the level-load entrance (when mainmenu closes and the iso
    // world becomes visible) slides them in from below / above
    // instead of revealing them already at rest. intgame_hide below
    // becomes a no-op for the bars — they're already where
    // slide_hide would put them.
    intgame_hud_slide_prepare_offscreen();

    intgame_hide();
}

// 0x54A9A0
void iso_interface_destroy(void)
{
    int index;

    if (intgame_iso_interface_created) {
        intgame_hud_bar_uses_tint = false;

        // CE: reap any in-flight FULL->MEDIUM wings-slide ghost before
        // the bar windows it snapshots are torn down.
        intgame_hud_ghost_destroy();

        for (index = 0; index < 2; index++) {
            tig_window_destroy(dword_64C4F8[index]);
        }

        tig_font_destroy(intgame_cloister18_font);

        for (index = 0; index < MAX_MESSAGE_HISTORY_ITEMS; index++) {
            FREE(intgame_message_history[index].str);
        }

        for (index = 0; index < 5; index++) {
            if (intgame_maintain_fs_windows[index] != TIG_WINDOW_HANDLE_INVALID) {
                tig_window_destroy(intgame_maintain_fs_windows[index]);
                intgame_maintain_fs_windows[index] = TIG_WINDOW_HANDLE_INVALID;
            }
        }

        // CE: reset the in-game flag so intgame_apply_translucent_black
        // (and any other "are we in-play?" check) correctly returns
        // false after quit-to-title. Vanilla never cleared this on
        // destroy — fine for vanilla because nothing read the flag
        // outside the creator, but the translucent-black gating reads
        // it and would tint pre-game UIs against a stale iso underlay
        // (the last loaded world) if we leave it set.
        intgame_iso_interface_created = false;

        // CE: refresh modal-dialog auto-tint underlay now that the iso
        // window is gone. With no mainmenu up either, this resolves to
        // "no underlay" and disables the modal-tint pathway — pre-game
        // modals between sessions stay opaque. If a mainmenu IS up at
        // this point (quit-to-title path: mainmenu_ui_active became
        // true before iso interface tears down), this picks the
        // mainmenu backdrop instead.
        intgame_refresh_modal_tint();
    }

    tig_video_buffer_destroy(dword_64C474);
}

// 0x54AA30
void sub_54AA30(void)
{
    intgame_mode_stack_size = 0;
    intgame_mode_stack[intgame_mode_stack_size] = INTGAME_MODE_MAIN;
    intgame_message_history_size = 0;
    intgame_message_history_end = 0;
    intgame_message_history_curr = 0;
}

// 0x54AA60
bool intgame_button_create_ex(tig_window_handle_t window_handle, TigRect* rect, UiButtonInfo* button_info, unsigned int flags)
{
    TigButtonData button_data;

    button_data.flags = flags;
    button_data.window_handle = window_handle;
    button_data.x = button_info->x - rect->x;
    button_data.y = button_info->y - rect->y;
    tig_art_interface_id_create(button_info->art_num, 0, 0, 0, &(button_data.art_id));
    button_data.mouse_down_snd_id = SND_INTERFACE_BUTTON_MEDIUM;
    button_data.mouse_up_snd_id = SND_INTERFACE_BUTTON_MEDIUM_RELEASE;
    button_data.mouse_enter_snd_id = -1;
    button_data.mouse_exit_snd_id = -1;
    return tig_button_create(&button_data, &(button_info->button_handle)) == TIG_OK;
}

// 0x54AAE0
bool intgame_button_create(UiButtonInfo* button_info)
{
    int index;

    index = find_interface_window_index(button_info->x, button_info->y);
    if (index == -1) {
        return false;
    }

    return intgame_button_create_ex(dword_64C4F8[index], &(intgame_interface_window_frames[index]), button_info, TIG_BUTTON_MOMENTARY);
}

// 0x54AB20
bool button_create_flags(UiButtonInfo* button_info, unsigned int flags)
{
    int index;

    index = find_interface_window_index(button_info->x, button_info->y);
    if (index == -1) {
        return false;
    }

    return intgame_button_create_ex(dword_64C4F8[index], &(intgame_interface_window_frames[index]), button_info, flags);
}

// 0x54ABD0
bool button_create_no_art(UiButtonInfo* button_info, int width, int height)
{
    int index;
    TigButtonData button_data;

    index = find_interface_window_index(button_info->x, button_info->y);
    if (index == -1) {
        return false;
    }

    button_data.window_handle = dword_64C4F8[index];
    button_data.x = button_info->x - intgame_interface_window_frames[index].x;
    button_data.y = button_info->y - intgame_interface_window_frames[index].y;
    button_data.width = width;
    button_data.height = height;
    button_data.flags = TIG_BUTTON_MOMENTARY;
    button_data.art_id = TIG_ART_ID_INVALID;
    button_data.mouse_down_snd_id = -1;
    button_data.mouse_up_snd_id = -1;
    button_data.mouse_enter_snd_id = -1;
    button_data.mouse_exit_snd_id = -1;
    return tig_button_create(&button_data, &(button_info->button_handle)) == TIG_OK;
}

// 0x54AC70
void intgame_button_destroy(UiButtonInfo* button_info)
{
    tig_button_destroy(button_info->button_handle);
    button_info->button_handle = TIG_BUTTON_HANDLE_INVALID;
}

// 0x54AD00
void intgame_draw_counter(int counter, int value, int digits)
{
    IntgameIsoWindowTypeInfo* info;
    TigRect rect;
    TigFont font_desc;
    char format[12];
    char str[80];
    int pos;

    if (!intgame_iso_interface_created) {
        return;
    }

    if (counter == INTGAME_COUNTER_MANA
        && intgame_iso_window_type != ROTWIN_TYPE_MAGICTECH) {
        return;
    }

    info = &(intgame_number_boxes[counter]);

    tig_font_push(intgame_cloister18_font);

    if (counter != INTGAME_COUNTER_POISON) {
        info->rect.height++;
        tig_window_fill(info->window_handle,
            &(info->rect),
            tig_color_make(0, 0, 0));
        info->rect.height--;
    }

    if (value < 0 && (counter == INTGAME_COUNTER_MANA || counter == INTGAME_COUNTER_MONEY)) {
        if (value == -1) {
            for (pos = 0; pos < digits; pos++) {
                str[pos] = '-';
            }
            str[pos] = '\0';
        } else {
            for (pos = 0; pos < digits; pos++) {
                str[pos] = '?';
            }
            str[pos] = '\0';
        }
    } else {
        snprintf(format, sizeof(format), "%%0%dd", digits);
        snprintf(str, sizeof(str), format, value);
    }

    font_desc.str = str;
    font_desc.width = 0;
    tig_font_measure(&font_desc);

    if (font_desc.width < info->rect.width) {
        rect.x = info->rect.x + (info->rect.width - font_desc.width) / 2;
        rect.y = info->rect.y + (info->rect.height - font_desc.height) / 2;
        rect.width = font_desc.width;
        rect.height = font_desc.height;
    } else {
        font_desc.width = info->rect.width;
        tig_font_measure(&font_desc);

        if (font_desc.height > info->rect.height) {
            tig_font_pop();
            return;
        }

        rect.x = info->rect.x;
        rect.y = info->rect.y + (info->rect.height - font_desc.height) / 2;
        rect.width = font_desc.width;
        rect.height = font_desc.height;
    }

    tig_window_text_write(info->window_handle, str, &rect);
    tig_font_pop();
}

// 0x54AEE0
// CE: per-vial current frame index for the bubbling-liquid animation
// (indexed by INTGAME_BAR_*). Advanced by intgame_vial_anim_ping; read by
// intgame_draw_bar_rect when blitting the liquid art. The normal health
// (redvial #18) and fatigue (bluvial #19) arts are 15-frame animations;
// poisoned (#17) and the empty tube (#20) are single-frame and resolve to
// frame 0 regardless. The two vials run independent state machines so they
// bubble out of sync.
static int intgame_vial_frame[INTGAME_BAR_COUNT] = { 0, 0 };

void intgame_draw_bar(int bar)
{
    switch (bar) {
    case INTGAME_BAR_HEALTH:
        intgame_draw_bar_rect(&intgame_health_bar_frame);
        break;
    case INTGAME_BAR_FATIGUE:
        intgame_draw_bar_rect(&intgame_fatigue_bar_frame);
        break;
    }
}

// 0x54AF10
void intgame_draw_bar_rect(TigRect* rect)
{
    int64_t pc_obj;
    TigRect rects[INTGAME_BAR_COUNT];
    int nums[INTGAME_BAR_COUNT];
    int poison;
    int bar;
    TigArtBlitInfo art_blit_info;
    TigRect blit_rect;
    TigRect tmp_rect;
    TigRect dst_rect;
    int value;
    int fullness;
    int filled_height;
    int empty_height;

    if (!dword_64C6B0) {
        return;
    }

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return;
    }

    rects[INTGAME_BAR_HEALTH] = intgame_health_bar_frame;
    rects[INTGAME_BAR_FATIGUE] = intgame_fatigue_bar_frame;

    poison = stat_level_get(pc_obj, STAT_POISON_LEVEL);
    nums[INTGAME_BAR_HEALTH] = poison > 0 ? 17 : 18;
    nums[INTGAME_BAR_FATIGUE] = 19;

    for (bar = 0; bar < INTGAME_BAR_COUNT; bar++) {
        tmp_rect = rects[bar];
        if (tig_rect_intersection(&tmp_rect, rect, &blit_rect) == TIG_OK) {
            blit_rect.x -= intgame_interface_window_frames[1].x;
            blit_rect.y -= intgame_interface_window_frames[1].y;
            tig_art_interface_id_create(184, 0, 0, 0, &(art_blit_info.art_id));

            art_blit_info.src_rect = &blit_rect;
            art_blit_info.dst_rect = &blit_rect;
            art_blit_info.flags = 0;
            tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
        }

        if (bar == INTGAME_BAR_HEALTH) {
            value = object_hp_max(pc_obj);
            if (value != 0) {
                fullness = 100 * object_hp_current(pc_obj) / value;
            } else {
                fullness = 50;
            }
        } else {
            value = critter_fatigue_max(pc_obj);
            if (value != 0) {
                fullness = 100 * critter_fatigue_current(pc_obj) / value;
            } else {
                fullness = 50;
            }
        }

        filled_height = fullness * tmp_rect.height / 100;
        empty_height = tmp_rect.height - filled_height;
        if (empty_height > 0) {
            tmp_rect.x = rects[bar].x;
            tmp_rect.y = rects[bar].y;
            tmp_rect.width = rects[bar].width;
            tmp_rect.height = empty_height;

            if (tig_rect_intersection(&tmp_rect, rect, &blit_rect) == TIG_OK) {
                tmp_rect.x = blit_rect.x - tmp_rect.x;
                tmp_rect.y = blit_rect.y - tmp_rect.y;
                tmp_rect.width = blit_rect.width;
                tmp_rect.height = blit_rect.height;
                tig_art_interface_id_create(20, 0, 0, 0, &(art_blit_info.art_id));

                dst_rect.width = tmp_rect.width;
                dst_rect.height = tmp_rect.height;
                dst_rect.x = blit_rect.x - intgame_interface_window_frames[1].x;
                dst_rect.y = blit_rect.y - intgame_interface_window_frames[1].y;

                art_blit_info.flags = 0;
                art_blit_info.src_rect = &tmp_rect;
                art_blit_info.dst_rect = &dst_rect;
                tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
            }
        }

        if (filled_height > 0) {
            int v14;
            int v15;
            int v16;

            tmp_rect.x = rects[bar].x;
            tmp_rect.y = rects[bar].y;
            tmp_rect.width = rects[bar].width;

            v14 = 8;
            v15 = filled_height + 8;
            v16 = empty_height - 8;
            if (v15 > rects[bar].height) {
                v14 += rects[bar].height - v15;
                v16 += v15 - rects[bar].height;
                v15 = rects[bar].height;
            }
            tmp_rect.y += v16;
            tmp_rect.height = v15;

            if (tig_rect_intersection(&tmp_rect, rect, &blit_rect) == TIG_OK) {
                tmp_rect.x = blit_rect.x - tmp_rect.x;
                tmp_rect.y = blit_rect.y - v14 - tmp_rect.y + 8;
                tmp_rect.width = blit_rect.width;
                tmp_rect.height = blit_rect.height;
                // CE: animate the liquid surface — redvial (#18) / bluvial
                // (#19) are multi-frame "bubbling" arts. Advance to the
                // shared vial phase frame so the surface ripples; the
                // num_frames guard keeps single-frame liquids (poisoned
                // #17) at frame 0.
                {
                    TigArtAnimData liquid_anim;
                    int liquid_frame = 0;
                    tig_art_interface_id_create(nums[bar], 0, 0, 0, &(art_blit_info.art_id));
                    if (tig_art_anim_data(art_blit_info.art_id, &liquid_anim) == TIG_OK
                        && liquid_anim.num_frames > 1) {
                        liquid_frame = intgame_vial_frame[bar] % liquid_anim.num_frames;
                        tig_art_interface_id_create(nums[bar], liquid_frame, 0, 0,
                            &(art_blit_info.art_id));
                    }
                }

                dst_rect.x = blit_rect.x - intgame_interface_window_frames[1].x;
                dst_rect.width = tmp_rect.width;
                dst_rect.y = blit_rect.y - intgame_interface_window_frames[1].y;
                dst_rect.height = tmp_rect.height;

                art_blit_info.flags = 0;
                art_blit_info.src_rect = &tmp_rect;
                art_blit_info.dst_rect = &dst_rect;
                tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
            }
        }

        if (bar == INTGAME_BAR_HEALTH) {
            intgame_draw_counter(INTGAME_COUNTER_HEALTH, object_hp_current(pc_obj), 3);
            if (poison > 0) {
                intgame_draw_counter(INTGAME_COUNTER_POISON, poison, 3);
            }
        } else {
            intgame_draw_counter(INTGAME_COUNTER_FATIGUE, critter_fatigue_current(pc_obj), 3);
        }
    }

    compact_ui_draw();
}

// 0x54B3A0
void intgame_draw_bars(void)
{
    intgame_draw_bar(INTGAME_BAR_HEALTH);
    intgame_draw_bar(INTGAME_BAR_FATIGUE);
}

// 0x54B3C0
void intgame_counters_refresh(void)
{
    int qty;
    int art_num;
    int64_t pc_obj;
    int64_t item_obj;
    int ammo_type;
    tig_art_id_t art_id;
    int mana;

    qty = 0;
    art_num = 474;
    pc_obj = player_get_local_pc_obj();

    if (pc_obj != OBJ_HANDLE_NULL) {
        item_obj = item_wield_get(pc_obj, ITEM_INV_LOC_WEAPON);
        if (item_obj != OBJ_HANDLE_NULL) {
            ammo_type = item_weapon_ammo_type(item_obj);
            if (ammo_type != 10000) {
                qty = item_ammo_quantity_get(pc_obj, ammo_type);
                art_num = dword_5C728C[ammo_type];
            } else {
                qty = obj_field_int32_get(item_obj, OBJ_F_ITEM_MANA_STORE);
                if (qty > 0) {
                    art_num = 469;
                } else {
                    qty = item_gold_get(pc_obj);
                }
            }
        } else {
            qty = item_gold_get(pc_obj);
        }
    }

    intgame_draw_counter(INTGAME_COUNTER_MONEY, qty, 6);
    tig_art_interface_id_create(art_num, 0, 0, 0, &art_id);
    intgame_ammo_icon_refresh(art_id);

    if (qword_64C688 != OBJ_HANDLE_NULL
        && intgame_iso_window_type == ROTWIN_TYPE_MAGICTECH) {
        if ((obj_field_int32_get(qword_64C688, OBJ_F_ITEM_FLAGS) & OIF_IDENTIFIED) != 0) {
            mana = obj_field_int32_get(qword_64C688, OBJ_F_ITEM_SPELL_MANA_STORE);
            if (mana >= 0) {
                intgame_draw_counter(INTGAME_COUNTER_MANA, mana, 3);
            } else {
                intgame_draw_counter(INTGAME_COUNTER_MANA, -1, 3);
            }
        } else {
            intgame_draw_counter(INTGAME_COUNTER_MANA, -2, 3);
        }
    }
}

// 0x54B500
void intgame_ammo_icon_refresh(tig_art_id_t art_id)
{
    int index;
    TigRect src_rect;
    TigRect dst_rect;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo blt;

    index = find_interface_window_index(intgame_ammo_button_info.x, intgame_ammo_button_info.y);
    if (index == -1) {
        tig_debug_printf("intgame_ammo_icon_refresh: ERROR: couldn't find iwid match!\n");
        exit(EXIT_SUCCESS); // FIXME: Should be EXIT_FAILURE.
    }

    blt.art_id = art_id;
    tig_art_frame_data(art_id, &art_frame_data);

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = art_frame_data.width;
    src_rect.height = art_frame_data.height;

    dst_rect.x = intgame_ammo_button_info.x - intgame_interface_window_frames[index].x;
    dst_rect.y = intgame_ammo_button_info.y - intgame_interface_window_frames[index].y;
    dst_rect.width = art_frame_data.width;
    dst_rect.height = art_frame_data.height;

    blt.flags = 0;
    blt.src_rect = &src_rect;
    blt.dst_rect = &dst_rect;
    tig_window_blit_art(dword_64C4F8[index], &blt);
}

// 0x54B5D0
bool sub_54B5D0(TigMessage* msg)
{
    MesFileEntry mes_file_entry;
    UiMessage ui_message;
    TigButtonState button_state;
    TigWindowData window_data;
    int index;
    DateTime datetime;
    tig_art_id_t art_id;
    char time_str_buffer[80];
    char buffer[80];

    // CE: when a shell menu has slid the bars off-screen, the bar
    // windows stay tig-shown (so the slide animation can render) —
    // which means this filter still gets keyboard / mouse messages
    // routed to it. Originally intgame_hide tig_window_hide'd the
    // bars, so the filter never fired during shell menus and game
    // hotkeys (I=inventory, M=wmap, etc) were de-facto blocked.
    // Restore that behavior by short-circuiting here whenever the
    // bars are in shell-hidden state. Band mode (chargen / vanilla
    // shells repurposing the bar as panel chrome) explicitly wants
    // input — it falls through.
    if (intgame_hud_shell_hidden) {
        return false;
    }

    if (scrollbar_ui_process_event(msg)
        || handle_button_unhover(msg)
        || hotkey_ui_process_event(msg)) {
        return true;
    }

    if (intgame_dialog_process_event_func != NULL) {
        // CE: mouse wheel falls through to the main loop's zoom
        // handler (line 2912). The dialog UI doesn't use the wheel
        // and zooming by wheel is the most natural mid-dialogue
        // re-framing gesture.
        if (msg->type == TIG_MESSAGE_MOUSE
            && msg->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL) {
            return false;
        }
        if (msg->type != TIG_MESSAGE_KEYBOARD) {
            return intgame_dialog_process_event_func(msg);
        }

        // CE: a few keys are useful enough mid-conversation to be
        // worth letting through to the main loop instead of feeding
        // them to the dialog filter. ESC and O were already
        // whitelisted (pause menu / options); zoom in/out and TAB
        // (HUD cycle) join them so the player can re-frame the scene
        // and adjust chrome density without ending the conversation.
        // The dialog UI doesn't bind these keys, and the speech-
        // bubble / dialog-options positioning is already TAB-stage
        // aware so cycling stage during dialogue places text
        // correctly relative to the current HUD layout.
        SDL_Scancode sc = msg->data.keyboard.scancode;
        bool whitelisted = sc == SDL_SCANCODE_ESCAPE
            || sc == SDL_SCANCODE_O
            || sc == SDL_SCANCODE_EQUALS
            || sc == SDL_SCANCODE_MINUS
            || sc == SDL_SCANCODE_TAB;
        if (whitelisted) {
            return false;
        }
        return intgame_dialog_process_event_func(msg);
    }

    if (combat_turn_based_is_active()) {
        if (!player_is_local_pc_obj(combat_turn_based_whos_turn_get())) {
            if (msg->type == TIG_MESSAGE_KEYBOARD
                && msg->data.keyboard.key == SDLK_SPACE
                && msg->data.keyboard.pressed) {
                combat_turn_based_toggle();
                return true;
            }
            return false;
        }
    }

    if (msg->type == TIG_MESSAGE_MOUSE) {
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_MOVE || mainmenu_ui_is_active()) {
            return false;
        }

        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_IDLE) {
            if (dword_64C674 != -1) {
                mes_file_entry.num = dword_64C674;
                ui_message.type = UI_MSG_TYPE_FEEDBACK;
                if (mes_search(intgame_mes_file, &mes_file_entry)) {
                    ui_message.str = mes_file_entry.str;
                    intgame_message_window_display_msg(&ui_message);
                }
            }

            if (msg->data.mouse.y < intgame_interface_window_frames[1].y) {
                if (intgame_iso_window_type == ROTWIN_TYPE_SPELLS
                    || intgame_iso_window_type == ROTWIN_TYPE_SKILLS) {
                    iso_interface_window_set(ROTWIN_TYPE_MSG);
                    return false;
                }
            } else if (msg->data.mouse.x >= 10
                && msg->data.mouse.x <= 790
                && msg->data.mouse.y <= 590
                && intgame_iso_window_type == ROTWIN_TYPE_MSG) {
                if (tig_button_state_get(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle, &button_state) == TIG_OK
                    && button_state == TIG_BUTTON_STATE_PRESSED) {
                    iso_interface_window_set(ROTWIN_TYPE_SPELLS);
                } else if (tig_button_state_get(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle, &button_state) == TIG_OK
                    && button_state == TIG_BUTTON_STATE_PRESSED) {
                    iso_interface_window_set(ROTWIN_TYPE_SKILLS);
                } else {
                    return false;
                }

                if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                    intgame_refresh_cursor();
                    return false;
                }
            }

            return false;
        }

        switch (msg->data.mouse.event) {
        case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_DOWN:
            if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                return false;
            }

            switch (intgame_mode_get()) {
            case INTGAME_MODE_WMAP:
            case INTGAME_MODE_LOGBOOK:
            case INTGAME_MODE_CHAREDIT:
            case INTGAME_MODE_SCHEMATIC:
            case INTGAME_MODE_WRITTEN:
                if (tig_window_data(dword_64C52C, &window_data) == TIG_OK) {
                    if (msg->data.mouse.x < window_data.rect.x
                        || msg->data.mouse.x - window_data.rect.x > window_data.rect.width) {
                        intgame_mode_set(INTGAME_MODE_MAIN);
                    }
                    if (msg->data.mouse.y < window_data.rect.y
                        || msg->data.mouse.y - window_data.rect.y > window_data.rect.height) {
                        intgame_mode_set(INTGAME_MODE_MAIN);
                    }
                }
                break;
            default:
                break;
            }

            if (hotkey_ui_begin_drag()) {
                return true;
            }
            break;
        case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
            if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                sub_57DC20();
            }
            if (sub_57E8D0(TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP)) {
                return true;
            }
            break;
        case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP:
            if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                sub_57DC20();
            }
            if (sub_57E8D0(TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP)) {
                return true;
            }
            break;
        default:
            break;
        }

        return false;
    }

    if (msg->type == TIG_MESSAGE_BUTTON) {
        if (msg->data.button.state == TIG_BUTTON_STATE_RELEASED) {
            // CE: There is no dedicated "menu" button in the default UI, so
            // it's impossible to open the main menu to save/load game on mobile
            // platforms. For now, use the clock for that.
            if (msg->data.button.button_handle == intgame_clock_button_info.button_handle) {
                if (intgame_mode_get() == INTGAME_MODE_MAIN) {
                    if (dialog_ui_is_local_pc_in_dialog()
                        || wmap_ui_is_created()
                        || (combat_turn_based_is_active()
                            && player_get_local_pc_obj() != combat_turn_based_whos_turn_get())) {
                        mainmenu_ui_start(MM_TYPE_IN_PLAY_LOCKED);
                    } else {
                        mainmenu_ui_start(MM_TYPE_IN_PLAY);
                    }
                }
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle) {
                iso_interface_window_set(ROTWIN_TYPE_SPELLS);
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle) {
                iso_interface_window_set(ROTWIN_TYPE_SKILLS);
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_COMBAT].button_handle) {
                intgame_combat_mode_toggle();
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SCHEMATICS].button_handle) {
                schematic_ui_toggle(player_get_local_pc_obj(), player_get_local_pc_obj());
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_LOGBOOK].button_handle) {
                logbook_ui_open(player_get_local_pc_obj());
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_CHAR].button_handle) {
                sub_552740(player_get_local_pc_obj(), CHAREDIT_MODE_ACTIVE);
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_INVENTORY].button_handle) {
                inven_ui_open(player_get_local_pc_obj(), OBJ_HANDLE_NULL, INVEN_UI_MODE_INVENTORY);
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_MAP].button_handle) {
                wmap_ui_open();
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_FATE].button_handle) {
                fate_ui_toggle(player_get_local_pc_obj());
                return true;
            }

            if (msg->data.button.button_handle == intgame_sleep_button_info.button_handle) {
                sleep_ui_toggle(OBJ_HANDLE_NULL);
                return true;
            }

            if (msg->data.button.button_handle == intgame_mt_button_info.button_handle) {
                sub_556EA0(OBJ_HANDLE_NULL);
                return true;
            }

            switch (intgame_iso_window_type) {
            case ROTWIN_TYPE_MSG:
                if (msg->data.button.button_handle == stru_5C65F8[1].button_handle) {
                    if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                        intgame_message_history_scroll_up();
                        return true;
                    }
                    sub_575770();
                    intgame_refresh_cursor();
                    return true;
                }
                if (msg->data.button.button_handle == stru_5C65F8[0].button_handle) {
                    if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                        intgame_message_history_scroll_down();
                        return true;
                    }
                    sub_575770();
                    intgame_refresh_cursor();
                    return true;
                }
                break;
            case ROTWIN_TYPE_SPELLS:
                for (index = 0; index < 5; index++) {
                    if (intgame_spell_buttons[5 * dword_64C530 + index].art_num != -1
                        && msg->data.button.button_handle == intgame_spell_buttons[5 * dword_64C530 + index].button_handle) {
                        if (spell_is_known(player_get_local_pc_obj(), 5 * dword_64C530 + index)) {
                            sub_57EFA0(3, 5 * dword_64C530 + index, OBJ_HANDLE_NULL);
                            spell_ui_activate(player_get_local_pc_obj(), 5 * dword_64C530 + index);
                            return true;
                        }
                    }
                }
                break;
            case ROTWIN_TYPE_SKILLS:
                for (index = 0; index < 4; index++) {
                    if (msg->data.button.button_handle == stru_5C6C68[index].button_handle) {
                        sub_57EFA0(2, index, OBJ_HANDLE_NULL);
                        sub_579FA0(player_get_local_pc_obj(), index);
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_CHAT:
                break;
            case ROTWIN_TYPE_MP_KICKBAN:
                break;
            case ROTWIN_TYPE_MAGICTECH:
                for (index = 0; index < 5; index++) {
                    if (intgame_mt_spell_buttons[index].art_num != -1
                        && msg->data.button.button_handle == intgame_mt_spell_buttons[index].button_handle) {
                        sub_57C040(qword_64C688, index);
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_MAP_NOTE:
                if (msg->data.button.button_handle == stru_5C6CA8[0].button_handle) {
                    textedit_ui_clear();
                    return true;
                }

                if (msg->data.button.button_handle == stru_5C6CA8[1].button_handle) {
                    textedit_ui_restore();
                    return true;
                }

                if (msg->data.button.button_handle == stru_5C6CA8[2].button_handle) {
                    textedit_ui_submit();
                    return true;
                }
                break;
            case ROTWIN_TYPE_QUANTITY:
                if (msg->data.button.button_handle == intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_TAKE_ALL].button_handle) {
                    intgame_quantity = intgame_max_quantity;
                    intgame_refresh_quantity();
                    return true;
                }

                if (msg->data.button.button_handle == intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_OK].button_handle) {
                    sub_578B80(intgame_quantity);
                    intgame_mode_set(INTGAME_MODE_MAIN);
                    return true;
                }

                if (msg->data.button.button_handle == intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_CANCEL].button_handle) {
                    intgame_mode_set(INTGAME_MODE_MAIN);
                    return true;
                }
                break;
            default:
                break;
            }

            for (index = 0; index < 5; index++) {
                if (msg->data.button.button_handle == intgame_maintain_buttons[index].button_handle) {
                    spell_ui_maintain_click(index);
                    return true;
                }
            }

            for (index = 0; index < 5; index++) {
                if (msg->data.button.button_handle == intgame_maintain_fs_buttons[index].button_handle) {
                    spell_ui_maintain_click(index);
                    return true;
                }
            }

            return false;
        } // msg->data.button.state == TIG_BUTTON_STATE_RELEASED

        if (msg->data.button.state == TIG_BUTTON_STATE_PRESSED) {
            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle) {
                if (tig_button_state_get(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle, &button_state) == TIG_OK
                    && button_state == TIG_BUTTON_STATE_PRESSED) {
                    tig_button_state_change(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle, TIG_BUTTON_STATE_RELEASED);
                }
                iso_interface_window_set(ROTWIN_TYPE_SPELLS);
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle) {
                if (tig_button_state_get(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle, &button_state) == TIG_OK
                    && button_state == TIG_BUTTON_STATE_PRESSED) {
                    tig_button_state_change(intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle, TIG_BUTTON_STATE_RELEASED);
                }
                iso_interface_window_set(ROTWIN_TYPE_SKILLS);
                return true;
            }

            switch (intgame_iso_window_type) {
            case ROTWIN_TYPE_SPELLS:
                for (index = 0; index < COLLEGE_COUNT; index++) {
                    if (spell_college_small_icon(index) != -1
                        && msg->data.button.button_handle == intgame_college_buttons[index].button_handle) {
                        intgame_spells_hide_college_spells(dword_64C530);
                        dword_64C530 = index;
                        intgame_spells_show_college_spells(dword_64C530);
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_QUANTITY:
                if (msg->data.button.button_handle == intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_PLUS].button_handle) {
                    if (intgame_quantity < intgame_max_quantity) {
                        intgame_quantity++;
                        intgame_refresh_quantity();
                    }
                    return true;
                }
                if (msg->data.button.button_handle == intgame_quantity_buttons[INTGAME_QUANTITY_BUTTON_MINUS].button_handle) {
                    if (intgame_quantity > 0) {
                        intgame_quantity--;
                        intgame_refresh_quantity();
                    }
                    return true;
                }
                break;
            case ROTWIN_TYPE_MAP_NOTE:
                for (index = 3; index < 6; index++) {
                    if (msg->data.button.button_handle == stru_5C6CA8[index].button_handle) {
                        sub_564000(index - 3);
                        return true;
                    }
                }
                break;
            default:
                break;
            }

            return false;
        } // msg->data.button.state == TIG_BUTTON_STATE_PRESSED

        if (msg->data.button.state == TIG_BUTTON_STATE_MOUSE_INSIDE) {
            if (msg->data.button.button_handle == intgame_clock_button_info.button_handle) {
                datetime = sub_45A7C0();

                mes_file_entry.num = 22; // "Current Time"
                mes_get_msg(intgame_mes_file, &mes_file_entry);
                datetime_format_time(&datetime, time_str_buffer, sizeof(time_str_buffer));
                snprintf(buffer, sizeof(buffer),
                    "%s: %s",
                    mes_file_entry.str,
                    time_str_buffer);

                mes_file_entry.num = 23; // "Current Date"
                mes_get_msg(intgame_mes_file, &mes_file_entry);
                datetime_format_date(&datetime, time_str_buffer, sizeof(time_str_buffer));
                snprintf(&(buffer[strlen(buffer)]), sizeof(buffer) - strlen(buffer),
                    "   %s: %s",
                    mes_file_entry.str,
                    time_str_buffer);

                ui_message.type = UI_MSG_TYPE_FEEDBACK;
                ui_message.str = buffer;
                intgame_message_window_display_msg(&ui_message);
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle) {
                dword_64C674 = 1000; // "Spells"
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle) {
                dword_64C674 = 1001; // "Skills"
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_COMBAT].button_handle) {
                dword_64C674 = 1002; // "Combat Toggle"
                return true;
            }

            if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SCHEMATICS].button_handle) {
                dword_64C674 = 1003; // "Schematics"
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_LOGBOOK].button_handle) {
                dword_64C674 = 1004; // "Log Book"
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_CHAR].button_handle) {
                dword_64C674 = 1005; // "Character Editor"
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_INVENTORY].button_handle) {
                dword_64C674 = 1006; // "Inventory"
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_MAP].button_handle) {
                dword_64C674 = 1007; // "Maps"
                return true;
            }

            if (msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_FATE].button_handle) {
                dword_64C674 = 1008; // "Fate Points"
                return true;
            }

            if (msg->data.button.button_handle == intgame_sleep_button_info.button_handle) {
                dword_64C674 = (tig_net_is_active()) ? 1010 : 1009;
            }

            switch (intgame_iso_window_type) {
            case ROTWIN_TYPE_SPELLS:
                for (index = 0; index < 16; index++) {
                    if (msg->data.button.button_handle == intgame_college_buttons[index].button_handle) {
                        intgame_message_window_display_college(index);
                        return true;
                    }
                }
                for (index = 0; index < 5; index++) {
                    if (msg->data.button.button_handle == intgame_spell_buttons[5 * dword_64C530 + index].button_handle) {
                        intgame_message_window_display_spell(5 * dword_64C530 + index);
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_MAGICTECH:
                for (index = 0; index < 5; index++) {
                    if (msg->data.button.button_handle == intgame_mt_spell_buttons[index].button_handle) {
                        intgame_message_window_display_spell(mt_item_spell(qword_64C688, index));
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_SKILLS:
                for (index = 0; index < 4; index++) {
                    if (msg->data.button.button_handle == stru_5C6C68[index].button_handle) {
                        intgame_message_window_display_skill(index);
                        return true;
                    }
                }
                break;
            case ROTWIN_TYPE_MSG:
                if (msg->data.button.button_handle == stru_5C65F8[1].button_handle) {
                    if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                        art_id = tig_mouse_cursor_get_art_id();
                        switch (tig_art_num_get(art_id)) {
                        case 0:
                        case 353:
                            tig_art_interface_id_create(498, 0, 0, 0, &art_id);
                            tig_mouse_hide();
                            tig_mouse_cursor_set_art_id(art_id);
                            tig_mouse_show();
                            break;
                        }
                    }
                    return true;
                }
                if (msg->data.button.button_handle == stru_5C65F8[0].button_handle) {
                    if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                        art_id = tig_mouse_cursor_get_art_id();
                        switch (tig_art_num_get(art_id)) {
                        case 0:
                        case 353:
                            tig_art_interface_id_create(499, 0, 0, 0, &art_id);
                            tig_mouse_hide();
                            tig_mouse_cursor_set_art_id(art_id);
                            tig_mouse_show();
                            break;
                        }
                    }
                    return true;
                }
                break;
            default:
                break;
            }

            for (index = 0; index < 5; index++) {
                if (msg->data.button.button_handle == intgame_maintain_buttons[index].button_handle) {
                    spell_ui_maintain_hover(index);
                    return true;
                }
            }

            for (index = 0; index < 5; index++) {
                if (msg->data.button.button_handle == intgame_maintain_fs_buttons[index].button_handle) {
                    spell_ui_maintain_hover(index);
                    return true;
                }
            }

            return false;
        } // msg->data.button.state == TIG_BUTTON_STATE_MOUSE_INSIDE

        return false;
    } // msg->type == TIG_MESSAGE_BUTTON

    if (msg->type == TIG_MESSAGE_KEYBOARD) {
        if (textedit_ui_is_focused()) {
            return textedit_ui_process_message(msg);
        }

        if (msg->data.keyboard.pressed) {
            switch (msg->data.keyboard.scancode) {
            case SDL_SCANCODE_BACKSPACE:
            case SDL_SCANCODE_DELETE:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity /= 10;
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_COMMA:
            case SDL_SCANCODE_PERIOD:
            case SDL_SCANCODE_SLASH:
                intgame_refresh_cursor();
                return false;
            case SDL_SCANCODE_KP_7:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 7;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_8:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 8;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_9:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 9;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_4:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 4;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_5:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 5;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_6:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 6;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_1:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 1;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_2:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 2;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_3:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity + 3;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            case SDL_SCANCODE_KP_0:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    intgame_quantity = 10 * intgame_quantity;
                    if (intgame_quantity > intgame_max_quantity) {
                        intgame_quantity = intgame_max_quantity;
                    }
                    intgame_refresh_quantity();
                }
                return true;
            default:
                break;
            }
        } else {
            switch (msg->data.keyboard.scancode) {
            case SDL_SCANCODE_K:
                if (!mainmenu_ui_is_active()) {
                    intgame_secondary_button_toggle(INTGAME_SECONDARY_BUTTON_SKILLS, ROTWIN_TYPE_SKILLS);
                }
                return true;
            case SDL_SCANCODE_M:
                if (!mainmenu_ui_is_active()) {
                    intgame_secondary_button_toggle(INTGAME_SECONDARY_BUTTON_SPELLS, ROTWIN_TYPE_SPELLS);
                }
                return true;
            case SDL_SCANCODE_COMMA:
            case SDL_SCANCODE_PERIOD:
            case SDL_SCANCODE_SLASH:
                intgame_refresh_cursor();
                return false;
            default:
                break;
            }
        }

        if (msg->data.keyboard.pressed) {
            switch (msg->data.keyboard.key) {
            case SDLK_SPACE:
                combat_turn_based_toggle();
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_C:
                sub_552740(player_get_local_pc_obj(), CHAREDIT_MODE_ACTIVE);
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_F:
                fate_ui_toggle(player_get_local_pc_obj());
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_I:
                inven_ui_open(player_get_local_pc_obj(), OBJ_HANDLE_NULL, INVEN_UI_MODE_INVENTORY);
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_R:
                intgame_combat_mode_toggle();
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_S:
                // Plain S opens the sleep menu; let Ctrl/Cmd+S fall through
                // to main.c's Save Game shortcut.
                if (tig_kb_get_modifier(SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
                    return false;
                }
                sleep_ui_toggle(OBJ_HANDLE_NULL);
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_T:
                schematic_ui_toggle(player_get_local_pc_obj(), player_get_local_pc_obj());
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_W:
                wmap_ui_open();
                gsound_play_sfx(0, 1);
                return true;
            case SDLK_RETURN:
                if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
                    sub_578B80(intgame_quantity);
                    intgame_mode_set(INTGAME_MODE_MAIN);
                    gsound_play_sfx(0, 1);
                } else if (!mainmenu_ui_is_active()) {
                    broadcast_ui_open();
                    gsound_play_sfx(0, 1);
                }
                return true;
            }
        }

        return false;
    } // msg->type == TIG_MESSAGE_KEYBOARD

    if (msg->type == TIG_MESSAGE_TEXT_INPUT) {
        return textedit_ui_process_message(msg);
    }

    return false;
}

// 0x54C8E0
bool iso_interface_message_filter(TigMessage* msg)
{
    // NOTE: Strange case - this function is huge but it's binary identical to
    // 0x54B5D0.
    return sub_54B5D0(msg);
}

// 0x54DBF0
void intgame_secondary_button_toggle(IntgameSecondaryButton btn, RotatingWindowType window_type)
{
    TigButtonState state;
    int opposite_btn;

    tig_button_state_get(intgame_secondary_buttons[btn].button_handle, &state);
    if (state != TIG_BUTTON_STATE_PRESSED) {
        opposite_btn = btn == INTGAME_SECONDARY_BUTTON_SPELLS
            ? INTGAME_SECONDARY_BUTTON_SKILLS
            : INTGAME_SECONDARY_BUTTON_SPELLS;
        tig_button_state_change(intgame_secondary_buttons[opposite_btn].button_handle, TIG_BUTTON_STATE_RELEASED);
        intgame_force_fullscreen();
        tig_button_state_change(intgame_secondary_buttons[btn].button_handle, TIG_BUTTON_STATE_PRESSED);
        iso_interface_window_set(window_type);
    } else {
        // CE: when the user is in MINI-peek for this same rotwin (TAB
        // expanded MINI to MEDIUM via re-press), a second re-press
        // should NOT dismiss the rotwin — it should collapse the crop
        // back to MINI while leaving SKILLS/SPELLS active. Otherwise
        // MINI's slim text row would lose its content. Button stays
        // PRESSED, fullscreen stays forced.
        if (intgame_iso_window_type == window_type
            && intgame_hud_handle_mini_peek_press()) {
            return;
        }
        iso_interface_window_set(ROTWIN_TYPE_MSG);
        tig_button_state_change(intgame_secondary_buttons[btn].button_handle, TIG_BUTTON_STATE_RELEASED);
        intgame_unforce_fullscreen();
    }
}

// 0x54DC80
bool handle_button_unhover(TigMessage* msg)
{
    int index;

    // FIX: Check for message type before checking for button state.
    if (msg->type != TIG_MESSAGE_BUTTON
        || msg->data.button.state != TIG_BUTTON_STATE_MOUSE_OUTSIDE) {
        return false;
    }

    sub_54ECD0();

    if (msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle
        || msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle
        || msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_COMBAT].button_handle
        || msg->data.button.button_handle == intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SCHEMATICS].button_handle
        || msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_LOGBOOK].button_handle
        || msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_CHAR].button_handle
        || msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_INVENTORY].button_handle
        || msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_MAP].button_handle
        || msg->data.button.button_handle == intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_FATE].button_handle
        || msg->data.button.button_handle == intgame_sleep_button_info.button_handle) {
        dword_64C674 = -1;
        intgame_message_window_clear();
        return true;
    }

    switch (intgame_iso_window_type) {
    case ROTWIN_TYPE_MSG:
        if (msg->data.button.button_handle == stru_5C65F8[1].button_handle
            || msg->data.button.button_handle == stru_5C65F8[0].button_handle) {
            if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                intgame_refresh_cursor();
            }
            return true;
        }
        if (msg->data.button.button_handle == intgame_clock_button_info.button_handle) {
            intgame_message_window_clear_internal();
        }
        break;
    case ROTWIN_TYPE_SPELLS:
    case ROTWIN_TYPE_SKILLS:
    case ROTWIN_TYPE_MAGICTECH:
        intgame_message_window_clear_internal();
        break;
    default:
        break;
    }

    for (index = 0; index < 5; index++) {
        if (msg->data.button.button_handle == intgame_maintain_buttons[index].button_handle) {
            intgame_message_window_clear_internal();
            return true;
        }
    }

    for (index = 0; index < 5; index++) {
        if (msg->data.button.button_handle == intgame_maintain_fs_buttons[index].button_handle) {
            intgame_message_window_clear_internal();
            return true;
        }
    }

    return false;
}

// CE: clamp a click-to-move destination to within the in-map
// pathfinder's reach. Every in-map path runs on a 64x64 local A* grid
// and bails when the straight-line distance exceeds 32 tiles (see
// path.c sub_41F840 / sub_41F9F0). At 1x zoom the clickable area is
// under that, but the zoom-out view lets the player click much farther
// — and a too-far click otherwise produces no path and does nothing.
// Clamp the target along the line toward it so the click at least
// walks the player in that direction (they can click again to
// continue). Returns loc unchanged when it's already in reach.
static int64_t intgame_clamp_move_target(int64_t pc_obj, int64_t loc)
{
    if (pc_obj == OBJ_HANDLE_NULL) {
        return loc;
    }

    int64_t from = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t dist = location_dist(from, loc);

    // Margin below the 32-tile grid cap so the A* has room to maneuver.
    const int64_t max_dist = 28;
    if (dist <= max_dist) {
        return loc;
    }

    int64_t fx = location_get_x(from);
    int64_t fy = location_get_y(from);
    int64_t tx = location_get_x(loc);
    int64_t ty = location_get_y(loc);
    int64_t cx = fx + (tx - fx) * max_dist / dist;
    int64_t cy = fy + (ty - fy) * max_dist / dist;
    return location_make(cx, cy);
}

// 0x54DE50
void intgame_process_event(TigMessage* msg)
{
    int64_t pc_obj;
    int64_t loc = 0;
    TargetDescriptor td;
    TigMouseState mouse_state;
    TigMessage fake_mouse_move;

    pc_obj = player_get_local_pc_obj();

    if (combat_turn_based_is_active()) {
        if (combat_turn_based_whos_turn_get() != pc_obj) {
            return;
        }
    }

    if (msg->type == TIG_MESSAGE_KEYBOARD
        && !textedit_ui_is_focused()
        && !msg->data.keyboard.pressed
        && msg->data.keyboard.scancode >= SDL_SCANCODE_1
        && msg->data.keyboard.scancode <= SDL_SCANCODE_0) {
        sub_57F1D0(msg->data.keyboard.scancode - SDL_SCANCODE_1);
    }

    switch (intgame_mode_get()) {
    case INTGAME_MODE_MAIN:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN:
                if (sub_5517A0(msg)
                    && sub_552050(msg->data.mouse.x, msg->data.mouse.y, &td)) {
                    if (td.is_loc) {
                        if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                            int64_t v2;

                            v2 = inven_ui_drag_item_obj_get();
                            if (hotkey_ui_is_dragging()) {
                                hotkey_ui_cancel_drag();
                                sub_573740(v2, false);
                                if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                                    v2 = inven_ui_drag_item_obj_get();
                                }
                            }

                            if (!hotkey_ui_is_dragging()) {
                                if (critter_is_active(pc_obj)) {
                                    sub_573840();
                                    intgame_refresh_cursor();
                                    anim_goal_throw_item(pc_obj, v2, td.loc);
                                } else {
                                    sub_575770();
                                }
                            }
                        } else if (critter_is_active(pc_obj)
                            && !tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                            int64_t move_loc =
                                intgame_clamp_move_target(pc_obj, td.loc);
                            if ((tig_kb_get_modifier(SDL_KMOD_CTRL)
                                    || tig_kb_get_modifier(SDL_KMOD_NUM))
                                && !settings_get_value(&settings, ALWAYS_RUN_KEY)) {
                                anim_goal_run_to_tile(pc_obj, move_loc);
                            } else {
                                anim_goal_move_to_tile(pc_obj, move_loc);
                            }

                            if (dword_64C6D8) {
                                sub_436CF0();
                            }
                            dword_64C6D8 = true;
                        }
                    } else if (!dword_64C6D8) {
                        sub_54ED30(&td);
                    }
                }
                break;
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP:
                dword_64C6D8 = false;
                break;
            case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
                if (pc_obj != OBJ_HANDLE_NULL) {
                    unsigned int spell_flags;
                    unsigned int critter_flags;

                    spell_flags = obj_field_int32_get(pc_obj, OBJ_F_SPELL_FLAGS);
                    critter_flags = obj_field_int32_get(pc_obj, OBJ_F_CRITTER_FLAGS);
                    if ((spell_flags & OSF_STONED) == 0
                        && (critter_flags & (OCF_PARALYZED | OCF_STUNNED)) == 0
                        && critter_is_active(pc_obj)
                        && !critter_is_prone(pc_obj)) {
                        if (combat_critter_is_combat_mode_active(pc_obj)) {
                            if (anim_is_current_goal_type(pc_obj, AG_ANIM_FIDGET, NULL)
                                || !sub_423300(pc_obj, 0)) {
                                intgame_combat_mode_toggle();
                            }
                        } else {
                            if (critter_is_concealed(pc_obj)
                                && !sub_423300(pc_obj, NULL)) {
                                critter_set_concealed(pc_obj, false);
                            }
                        }

                        if (sub_424070(pc_obj, 3, 0, 0)) {
                            sub_4B4320(pc_obj);

                            tig_mouse_get_state(&mouse_state);
                            if (location_at_zoomed(mouse_state.x, mouse_state.y, iso_zoom_current(), &loc)
                                && sub_5517A0(msg)) {
                                int64_t pc_loc;
                                tig_art_id_t aid;
                                int rot;

                                pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
                                aid = obj_field_int32_get(pc_obj, OBJ_F_CURRENT_AID);
                                rot = location_rot(pc_loc, loc);
                                if (!sub_423300(pc_obj, 0)) {
                                    anim_goal_rotate(pc_obj, rot);
                                } else if (anim_is_current_goal_type(pc_obj, AG_ANIM_FIDGET, NULL)) {
                                    object_set_current_aid(pc_obj, tig_art_id_rotation_set(aid, rot));
                                }
                            }
                        }
                    }

                    if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                        sub_575770();
                        intgame_refresh_cursor();
                    }
                }
                break;
            case TIG_MESSAGE_MOUSE_WHEEL:
                if (iso_zoom_is_available()) {
                    iso_zoom_wheel(msg->data.mouse.dy);
                    gamelib_invalidate_rect(NULL);
                }
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
            default:
                break;
            }
            break;
        case TIG_MESSAGE_KEYBOARD:
            if (!textedit_ui_is_focused()
                && !msg->data.keyboard.pressed) {
                switch (msg->data.keyboard.scancode) {
                case SDL_SCANCODE_F1:
                case SDL_SCANCODE_F2:
                case SDL_SCANCODE_F3:
                case SDL_SCANCODE_F4:
                case SDL_SCANCODE_F5:
                case SDL_SCANCODE_F6:
                    intgame_get_location_under_cursor(&loc);
                    sub_4C3BE0(msg->data.keyboard.scancode - SDL_SCANCODE_F1, loc);
                    break;
                case SDL_SCANCODE_HOME:
                    intgame_center_on_player();
                    break;
                default:
                    break;
                }
            }
            break;
        case TIG_MESSAGE_PING:
            if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_SPELL:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP:
                if (!inven_ui_is_created()) {
                    int mx = msg->data.mouse.x;
                    int my = msg->data.mouse.y;
                    bool picked;
                    if (intgame_adjust_mouse_for_zoom(mx, my, &mx, &my)) {
                        picked = target_pick_at_virtual_xy(mx, my, &td, intgame_fullscreen);
                    } else {
                        picked = target_pick_at_screen_xy(mx, my, &td, intgame_fullscreen);
                    }
                    if (picked) {
                        spell_ui_apply(&td);
                    } else if (target_last_rejection_get() == 0x100000) {
                        spell_ui_error_target_not_damaged();
                    }
                }
                break;
            case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
                spell_ui_cancel();
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
                break;
            default:
                break;
            }
            break;
        case TIG_MESSAGE_KEYBOARD:
            if (!textedit_ui_is_focused()) {
                if (!msg->data.keyboard.pressed) {
                    switch (msg->data.keyboard.scancode) {
                    case SDL_SCANCODE_LALT:
                    case SDL_SCANCODE_RALT:
                        if (!tig_kb_get_modifier(SDL_KMOD_ALT)) {
                            spell_ui_aggressive_mode_on();
                        }
                        break;
                    case SDL_SCANCODE_F1:
                    case SDL_SCANCODE_F2:
                    case SDL_SCANCODE_F3:
                    case SDL_SCANCODE_F4:
                    case SDL_SCANCODE_F5:
                    case SDL_SCANCODE_F6:
                        intgame_get_location_under_cursor(&loc);
                        sub_4C3BE0(msg->data.keyboard.scancode - SDL_SCANCODE_F1, loc);
                        break;
                    case SDL_SCANCODE_HOME:
                        intgame_center_on_player();
                        break;
                    default:
                        break;
                    }
                } else {
                    switch (msg->data.keyboard.scancode) {
                    case SDL_SCANCODE_LALT:
                    case SDL_SCANCODE_RALT:
                        spell_ui_aggressive_mode_off();
                        break;
                    default:
                        break;
                    }
                }
            }
            break;
        case TIG_MESSAGE_PING:
            if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_SKILL:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP: {
                int mx = msg->data.mouse.x;
                int my = msg->data.mouse.y;
                if (intgame_adjust_mouse_for_zoom(mx, my, &mx, &my)) {
                    if (target_pick_at_virtual_xy(mx, my, &td, intgame_fullscreen)) {
                        skill_ui_apply(&td);
                    }
                } else if (target_pick_at_screen_xy(mx, my, &td, intgame_fullscreen)) {
                    skill_ui_apply(&td);
                }
                break;
            }
            case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
                skill_ui_cancel();
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
                break;
            default:
                break;
            }
            break;
        case TIG_MESSAGE_KEYBOARD:
            if (!textedit_ui_is_focused()) {
                if (!msg->data.keyboard.pressed) {
                    switch (msg->data.keyboard.scancode) {
                    case SDL_SCANCODE_F1:
                    case SDL_SCANCODE_F2:
                    case SDL_SCANCODE_F3:
                    case SDL_SCANCODE_F4:
                    case SDL_SCANCODE_F5:
                    case SDL_SCANCODE_F6:
                        intgame_get_location_under_cursor(&loc);
                        sub_4C3BE0(msg->data.keyboard.scancode - SDL_SCANCODE_F1, loc);
                        break;
                    case SDL_SCANCODE_HOME:
                        intgame_center_on_player();
                        break;
                    default:
                        break;
                    }
                }
            }
            break;
        case TIG_MESSAGE_PING:
            if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_DIALOG:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_DOWN:
                if (sub_5517A0(msg)) {
                    int mx = msg->data.mouse.x;
                    int my = msg->data.mouse.y;
                    bool picked;
                    if (intgame_adjust_mouse_for_zoom(mx, my, &mx, &my)) {
                        picked = target_pick_at_virtual_xy(mx, my, &td, intgame_fullscreen);
                    } else {
                        picked = target_pick_at_screen_xy(mx, my, &td, intgame_fullscreen);
                    }
                    if (picked
                        && td.is_loc
                        && !inven_ui_drag_item_obj_get()
                        && !critter_is_dead(pc_obj)
                        && !tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        int64_t move_loc =
                            intgame_clamp_move_target(pc_obj, td.loc);
                        if ((tig_kb_get_modifier(SDL_KMOD_CTRL)
                                || tig_kb_get_modifier(SDL_KMOD_NUM))
                            && !settings_get_value(&settings, ALWAYS_RUN_KEY)) {
                            anim_goal_run_to_tile(pc_obj, move_loc);
                        } else {
                            anim_goal_move_to_tile(pc_obj, move_loc);
                        }

                        if (dword_64C6D8) {
                            sub_436CF0();
                        }
                        dword_64C6D8 = true;
                    }
                }
                break;
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP:
                dword_64C6D8 = false;
                break;
            case TIG_MESSAGE_MOUSE_WHEEL:
                // CE: mirror MODE_MAIN's wheel→zoom dispatch so
                // trackpad/wheel zoom works mid-dialogue. Pairs with
                // the dialog-filter pass-through in sub_54B5D0
                // (without that, the wheel never reaches here).
                if (iso_zoom_is_available()) {
                    iso_zoom_wheel(msg->data.mouse.dy);
                    gamelib_invalidate_rect(NULL);
                }
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
                break;
            default:
                break;
            }
            break;
        case TIG_MESSAGE_PING:
            if (intgame_dialog_process_event_func == NULL
                && tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_ITEM:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP: {
                int mx = msg->data.mouse.x;
                int my = msg->data.mouse.y;
                bool picked;
                if (intgame_adjust_mouse_for_zoom(mx, my, &mx, &my)) {
                    picked = target_pick_at_virtual_xy(mx, my, &td, intgame_fullscreen);
                } else {
                    picked = target_pick_at_screen_xy(mx, my, &td, intgame_fullscreen);
                }
                if (picked) {
                    item_ui_apply(&td);
                } else if (target_last_rejection_get() == 0x100000) {
                    spell_ui_error_target_not_damaged();
                }
                break;
            }
            case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
                item_ui_deactivate();
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
                break;
            default:
                break;
            }
            break;
        case TIG_MESSAGE_PING:
            if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_FOLLOWER:
        switch (msg->type) {
        case TIG_MESSAGE_MOUSE:
            switch (msg->data.mouse.event) {
            case TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP: {
                int mx = msg->data.mouse.x;
                int my = msg->data.mouse.y;
                if (intgame_adjust_mouse_for_zoom(mx, my, &mx, &my)) {
                    if (target_pick_at_virtual_xy(mx, my, &td, intgame_fullscreen)) {
                        follower_ui_execute_order(&td);
                    }
                } else if (target_pick_at_screen_xy(mx, my, &td, intgame_fullscreen)) {
                    follower_ui_execute_order(&td);
                }
                break;
            }
            case TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP:
                follower_ui_end_order_mode();
                break;
            case TIG_MESSAGE_MOUSE_IDLE:
                sub_551910(msg);
                break;
            default:
                break;
            }
            break;
        case TIG_MESSAGE_KEYBOARD:
            if (textedit_ui_is_focused()
                && !msg->data.keyboard.pressed) {
                switch (msg->data.keyboard.scancode) {
                case SDL_SCANCODE_F1:
                case SDL_SCANCODE_F2:
                case SDL_SCANCODE_F3:
                case SDL_SCANCODE_F4:
                case SDL_SCANCODE_F5:
                case SDL_SCANCODE_F6:
                    intgame_get_location_under_cursor(&loc);
                    sub_4C3BE0(msg->data.keyboard.scancode - SDL_SCANCODE_F1, loc);
                    break;
                case SDL_SCANCODE_HOME:
                    intgame_center_on_player();
                    break;
                default:
                    break;
                }
            }
            break;
        case TIG_MESSAGE_PING:
            if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
                fake_mouse_move.timestamp = msg->timestamp;
                fake_mouse_move.type = TIG_MESSAGE_MOUSE;
                fake_mouse_move.data.mouse.x = mouse_state.x;
                fake_mouse_move.data.mouse.y = mouse_state.y;
                fake_mouse_move.data.mouse.event = TIG_MESSAGE_MOUSE_MOVE;
                sub_553A70(&fake_mouse_move);
            }
            break;
        default:
            break;
        }
        return;
    case INTGAME_MODE_SLEEP:
        // CE: sleep panel is a small overlay; the iso world stays
        // visible behind it, so wheel→zoom is a useful re-frame
        // gesture (matches MAIN / DIALOG behavior). Without this
        // case the wheel would fall to the default below and be
        // dropped, leaving the player unable to zoom while the
        // sleep menu is open. Fate UI doesn't hit this because it
        // doesn't change intgame mode — wheel routes through
        // whatever mode (typically MAIN) was active.
        if (msg->type == TIG_MESSAGE_MOUSE
            && msg->data.mouse.event == TIG_MESSAGE_MOUSE_WHEEL) {
            if (iso_zoom_is_available()) {
                iso_zoom_wheel(msg->data.mouse.dy);
                gamelib_invalidate_rect(NULL);
            }
        }
        return;
    default:
        break;
    }
}

// 0x54EA80
void sub_54EA80(TargetDescriptor* td)
{
    int64_t pc_obj;
    S4F2680 v1;

    pc_obj = player_get_local_pc_obj();

    if (!combat_turn_based_is_active()
        || combat_turn_based_whos_turn_get() == pc_obj) {
        v1.field_0 = pc_obj;
        v1.field_8 = pc_obj;
        v1.td = td;

        switch (intgame_mode_get()) {
        case INTGAME_MODE_SPELL:
            if (sub_4F2680(&v1)) {
                spell_ui_apply(td);
            }
            break;
        case INTGAME_MODE_SKILL:
            if (sub_4F2680(&v1)) {
                skill_ui_apply(td);
            }
            break;
        case INTGAME_MODE_ITEM:
            if (sub_4F2680(&v1)) {
                item_ui_apply(td);
            }
            break;
        default:
            sub_54ED30(td);
            break;
        }
    }
}

// 0x54EB50
bool intgame_hotkey_is_dragging(void)
{
    return hotkey_ui_is_dragging();
}

// 0x54EB60
void intgame_center_on_player(void)
{
    int64_t obj;
    int64_t loc;
    int64_t x;
    int64_t y;

    obj = player_get_local_pc_obj();
    if (obj == OBJ_HANDLE_NULL) {
        return;
    }

    loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
    location_calc_dist_from_screen_center(loc, &x, &y);

    if (x != 0 || y != 0) {
        location_origin_set(loc);
        iso_redraw();
    } else {
        if (combat_turn_based_is_active()) {
            obj = combat_turn_based_whos_turn_get();
            if (obj != OBJ_HANDLE_NULL) {
                loc = obj_field_int64_get(obj, OBJ_F_LOCATION);
                location_origin_set(loc);
                iso_redraw();
            }
        }
    }
}

// 0x54EBF0
void intgame_combat_mode_toggle(void)
{
    int64_t pc_obj;
    MesFileEntry mes_file_entry;
    UiMessage ui_message;
    int64_t obj;

    pc_obj = player_get_local_pc_obj();
    if (critter_is_dead(pc_obj)) {
        return;
    }

    if (combat_critter_is_combat_mode_active(pc_obj)) {
        if (combat_can_exit_combat_mode(pc_obj)) {
            combat_critter_deactivate_combat_mode(pc_obj);
        } else {
            mes_file_entry.num = 24; // "You cannot exit combat-mode when under attack."
            mes_get_msg(intgame_mes_file, &mes_file_entry);

            ui_message.type = UI_MSG_TYPE_FEEDBACK;
            ui_message.str = mes_file_entry.str;
            intgame_message_window_display_msg(&ui_message);
        }
    } else {
        combat_critter_activate_combat_mode(pc_obj);
    }

    sub_5517F0();

    obj = qword_64C690;
    if (obj == OBJ_HANDLE_NULL) {
        obj = object_hover_obj_get();
    }

    if (obj != OBJ_HANDLE_NULL) {
        object_hover_obj_set(OBJ_HANDLE_NULL);
        object_hover_obj_set(obj);
        if (obj != OBJ_HANDLE_NULL) {
            sub_57CCF0(pc_obj, obj);
        }
    }
}

// 0x54ECD0
void sub_54ECD0(void)
{
    if (qword_64C690 == OBJ_HANDLE_NULL) {
        return;
    }

    if (object_hover_obj_get() != OBJ_HANDLE_NULL) {
        return;
    }

    object_hover_obj_set(qword_64C690);
    object_hover_obj_set(OBJ_HANDLE_NULL);
    sub_57CCF0(player_get_local_pc_obj(), qword_64C690);
}

// TODO: Lots of jumps, check.
//
// 0x54ED30
void sub_54ED30(TargetDescriptor* td)
{
    int64_t pc_obj;
    int64_t item_obj = OBJ_HANDLE_NULL;
    int64_t target_loc = 0;
    int target_type;
    unsigned int spell_flags;
    unsigned int critter_flags;
    AnimGoalData goal_data;
    AnimID anim_id;
    int anim;
    bool v26 = false;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return;
    }

    if (!critter_is_active(pc_obj)) {
        return;
    }

    target_type = obj_field_int32_get(td->obj, OBJ_F_TYPE);
    if (!td->is_loc && td->obj == pc_obj) {
        return;
    }

    spell_flags = obj_field_int32_get(pc_obj, OBJ_F_SPELL_FLAGS);
    critter_flags = obj_field_int32_get(pc_obj, OBJ_F_CRITTER_FLAGS);

    if ((spell_flags & OSF_STONED) != 0
        && (critter_flags & (OCF_PARALYZED | OCF_STUNNED)) != 0) {
        return;
    }

    if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
        if ((spell_flags & OSF_POLYMORPHED) != 0) {
            return;
        }

        item_obj = inven_ui_drag_item_obj_get();
        if (hotkey_ui_is_dragging()) {
            hotkey_ui_cancel_drag();
            sub_573740(item_obj, false);
        }

        if (hotkey_ui_is_dragging()) {
            return;
        }

        target_loc = obj_field_int64_get(td->obj, OBJ_F_LOCATION);
        if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
            item_obj = inven_ui_drag_item_obj_get();
        }

        sub_573840();
        intgame_refresh_cursor();

        anim = AG_THROW_ITEM;
    } else {
        if (!combat_critter_is_combat_mode_active(pc_obj)) {
            if (anim_is_attacking(td->obj, 0, pc_obj)) {
                combat_critter_activate_combat_mode(pc_obj);
            }
        }

        if (combat_critter_is_combat_mode_active(pc_obj)) {
            switch (target_type) {
            case OBJ_TYPE_WALL:
            case OBJ_TYPE_PORTAL:
                if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        anim = AG_ATTEMPT_ATTACK;
                    } else {
                        anim = AG_ATTACK;
                    }
                } else {
                    if ((spell_flags & OSF_POLYMORPHED) != 0) {
                        return;
                    }

                    anim = AG_USE_OBJECT;
                    v26 = true;
                }
                break;
            case OBJ_TYPE_CONTAINER:
                if ((spell_flags & OSF_POLYMORPHED) != 0) {
                    return;
                }

                if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        anim = AG_ATTEMPT_ATTACK;
                    } else {
                        anim = AG_ATTACK;
                    }
                } else {
                    anim = AG_USE_CONTAINER;
                    v26 = true;
                }
                break;
            case OBJ_TYPE_SCENERY:
                if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        anim = AG_ATTEMPT_ATTACK;
                    } else {
                        anim = AG_ATTACK;
                    }
                } else {
                    if ((spell_flags & OSF_POLYMORPHED) != 0) {
                        return;
                    }

                    anim = AG_USE_OBJECT;
                    v26 = true;
                }
                break;
            case OBJ_TYPE_WEAPON:
            case OBJ_TYPE_AMMO:
            case OBJ_TYPE_ARMOR:
            case OBJ_TYPE_GOLD:
            case OBJ_TYPE_FOOD:
            case OBJ_TYPE_SCROLL:
            case OBJ_TYPE_KEY:
            case OBJ_TYPE_KEY_RING:
            case OBJ_TYPE_WRITTEN:
            case OBJ_TYPE_GENERIC:
                if ((spell_flags & OSF_POLYMORPHED) != 0) {
                    return;
                }

                anim = AG_PICKUP_ITEM;
                break;
            case OBJ_TYPE_PC:
            case OBJ_TYPE_NPC:
                if (critter_is_dead(td->obj)) {
                    if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                        anim = AG_USE_CONTAINER;
                        v26 = true;
                    }
                } else {
                    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        anim = AG_ATTEMPT_ATTACK;
                    } else {
                        anim = AG_ATTACK;
                    }

                    if (!td->is_loc
                        && player_is_local_pc_obj(critter_pc_leader_get(td->obj)
                            && !tig_kb_is_key_pressed(SDL_SCANCODE_LALT))) {
                        return;
                    }
                }
                break;
            case OBJ_TYPE_TRAP:
                anim = AG_MOVE_TO_TILE;
                target_loc = obj_field_int64_get(td->obj, OBJ_F_LOCATION);
                break;
            default:
                return;
            }

            if (anim == AG_ATTACK || anim == AG_ATTEMPT_ATTACK) {
                if ((spell_flags & OSF_BODY_OF_AIR) != 0) {
                    MesFileEntry mes_file_entry;
                    UiMessage ui_message;

                    mes_file_entry.num = 25; // "You cannot attack in this form."
                    mes_get_msg(intgame_mes_file, &mes_file_entry);
                    ui_message.type = UI_MSG_TYPE_FEEDBACK;
                    ui_message.str = mes_file_entry.str;
                    intgame_message_window_display_msg(&ui_message);
                    return;
                }
            }
        } else {
            switch (target_type) {
            case OBJ_TYPE_PORTAL:
                if ((spell_flags & OSF_POLYMORPHED) != 0) {
                    return;
                }

                anim = AG_USE_OBJECT;
                v26 = true;
                break;
            case OBJ_TYPE_CONTAINER:
                if ((spell_flags & OSF_POLYMORPHED) != 0) {
                    return;
                }

                anim = AG_USE_CONTAINER;
                v26 = true;
                break;
            case OBJ_TYPE_SCENERY:
                anim = AG_USE_OBJECT;
                v26 = true;
                break;
            case OBJ_TYPE_WEAPON:
            case OBJ_TYPE_AMMO:
            case OBJ_TYPE_ARMOR:
            case OBJ_TYPE_GOLD:
            case OBJ_TYPE_FOOD:
            case OBJ_TYPE_SCROLL:
            case OBJ_TYPE_KEY:
            case OBJ_TYPE_KEY_RING:
            case OBJ_TYPE_WRITTEN:
            case OBJ_TYPE_GENERIC:
                if ((spell_flags & OSF_POLYMORPHED) != 0) {
                    return;
                }

                if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                    sub_4445A0(pc_obj, td->obj);
                    return;
                }

                anim = AG_PICKUP_ITEM;
                break;
            case OBJ_TYPE_PC:
            case OBJ_TYPE_NPC:
                if (!critter_is_dead(td->obj)
                    || (obj_field_int32_get(td->obj, OBJ_F_SPELL_FLAGS) & OSF_SPOKEN_WITH_DEAD) != 0) {
                    // FIXME: Useless.
                    obj_field_int32_get(td->obj, OBJ_F_CURRENT_AID);

                    if (player_is_pc_obj(td->obj)) {
                        return;
                    }

                    anim = AG_TALK;
                } else {
                    if ((spell_flags & OSF_POLYMORPHED) != 0) {
                        return;
                    }

                    if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                        sub_4445A0(pc_obj, td->obj);
                        return;
                    }

                    anim = AG_USE_CONTAINER;
                    v26 = true;
                }
                break;
            case OBJ_TYPE_TRAP:
                anim = AG_MOVE_TO_TILE;
                target_loc = obj_field_int64_get(td->obj, OBJ_F_LOCATION);
                break;
            default:
                return;
            }
        }
    }

    // 0x54F25B
    if (!sub_44D500(&goal_data, pc_obj, anim)) {
        return;
    }

    goal_data.params[AGDATA_TARGET_OBJ].obj = td->obj;

    if (target_loc != 0) {
        goal_data.params[AGDATA_TARGET_TILE].loc = target_loc;
    }

    if (item_obj != OBJ_HANDLE_NULL) {
        goal_data.params[AGDATA_SCRATCH_OBJ].obj = item_obj;
    }

    if (anim == AG_ATTACK || anim == AG_ATTEMPT_ATTACK) {
        if (tig_kb_is_key_pressed(SDL_SCANCODE_COMMA)) {
            goal_data.params[AGDATA_SCRATCH_VAL3].data = 1;
        } else if (tig_kb_is_key_pressed(SDL_SCANCODE_PERIOD)) {
            goal_data.params[AGDATA_SCRATCH_VAL3].data = 2;
        } else if (tig_kb_is_key_pressed(SDL_SCANCODE_SLASH)) {
            goal_data.params[AGDATA_SCRATCH_VAL3].data = 3;
        } else {
            goal_data.params[AGDATA_SCRATCH_VAL3].data = 0;
        }

        int64_t weapon_obj = item_wield_get(pc_obj, ITEM_INV_LOC_WEAPON);
        if (weapon_obj != OBJ_HANDLE_NULL
            && obj_field_int32_get(weapon_obj, OBJ_F_TYPE) == OBJ_TYPE_WEAPON
            && (obj_field_int32_get(weapon_obj, OBJ_F_WEAPON_FLAGS) & OWF_DEFAULT_THROWS) != 0
            && !item_check_remove(weapon_obj)) {
            int64_t throwable_instance_obj = item_find_first_matching_prototype(pc_obj, weapon_obj);
            if (throwable_instance_obj == OBJ_HANDLE_NULL) {
                throwable_instance_obj = weapon_obj;
            }

            item_remove(throwable_instance_obj);

            goal_data.params[AGDATA_TARGET_TILE].loc = obj_field_int64_get(td->obj, OBJ_F_LOCATION);
            goal_data.params[AGDATA_SCRATCH_OBJ].obj = throwable_instance_obj;

            anim_goal_throw_item(pc_obj, throwable_instance_obj, obj_field_int64_get(td->obj, OBJ_F_LOCATION));
            return;
        }
    }

    if (combat_auto_attack_get(pc_obj)) {
        if (sub_44E6F0(pc_obj, &goal_data)
            || !sub_424070(pc_obj, 3, 0, 0)) {
            return;
        }

        unsigned int flags = 0;
        if (tig_net_is_active()) {
            if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                flags |= 0x100;
            } else if (tig_kb_get_modifier(SDL_KMOD_CTRL)
                || ((!tig_kb_get_modifier(SDL_KMOD_NUM) || get_always_run(pc_obj))
                    && !tig_kb_get_modifier(SDL_KMOD_CTRL))) {
                flags |= 0x40;
            }

            if (v26) {
                flags |= 0x4000;
            }
        }

        if (!anim_goal_add_ex(&goal_data, &anim_id, flags)) {
            return;
        }

        if (tig_net_is_active()) {
            return;
        }
    } else if (sub_423300(pc_obj, &anim_id)) {
        // 0x54F68E
        if (combat_turn_based_is_active()) {
            AnimID fidget_anim_id;
            if (anim_is_current_goal_type(pc_obj, AG_ANIM_FIDGET, &fidget_anim_id)
                && anim_id_is_equal(&anim_id, &fidget_anim_id)
                && num_goal_subslots_in_use(&anim_id) < 4) {
                if (is_anim_forever(&anim_id)) {
                    if (sub_424070(pc_obj, 3, false, false)) {
                        if (!anim_goal_add(&goal_data, &anim_id)) {
                            return;
                        }
                    }
                } else {
                    // __FILE__: "C:\Troika\Code\Game\gameuilib\Intgame.c"
                    // __LINE__: 5088
                    if (!anim_subgoal_add(anim_id, &goal_data, __FILE__, __LINE__)) {
                        return;
                    }
                }
            }
        } else if (sub_44E6F0(pc_obj, &goal_data)) {
            if (anim == AG_ATTACK || anim == AG_ATTEMPT_ATTACK) {
                if (num_goal_subslots_in_use(&anim_id) < 4) {
                    if (is_anim_forever(&anim_id)) {
                        if (sub_424070(pc_obj, 3, 0, 0)
                            && !anim_goal_add(&goal_data, &anim_id)) {
                            return;
                        }
                    } else {
                        // __FILE__: "C:\Troika\Code\Game\gameuilib\Intgame.c"
                        // __LINE__: 5026
                        if (!anim_subgoal_add(anim_id, &goal_data, __FILE__, __LINE__)) {
                            return;
                        }
                    }
                }
            } else {
                if (sub_424070(pc_obj, 3, false, false)
                    && anim_goal_add(&goal_data, &anim_id)) {
                    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                        sub_436C50(anim_id);
                    } else if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                        turn_on_running(anim_id);
                    } else {
                        if (tig_kb_get_modifier(SDL_KMOD_NUM)) {
                            if (get_always_run(pc_obj)
                                && !tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                turn_on_running(anim_id);
                            }
                        } else {
                            if (!tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                turn_on_running(anim_id);
                            }
                        }
                    }

                    if (v26) {
                        sub_436ED0(anim_id);
                    }
                }
            }
        } else if (sub_424070(pc_obj, 3, false, false)) {
            if (tig_net_is_active()
                && !tig_kb_get_modifier(SDL_KMOD_SHIFT)
                && !tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                // NOTE: Some useless checks.
            }

            if (anim_goal_add(&goal_data, &anim_id)
                && !tig_net_is_active()) {
                if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                    sub_436C50(anim_id);
                } else if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                    turn_on_running(anim_id);
                } else {
                    if (tig_kb_get_modifier(SDL_KMOD_NUM)) {
                        if (get_always_run(pc_obj)
                            && !tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                            turn_on_running(anim_id);
                        }
                    } else {
                        if (!tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                            turn_on_running(anim_id);
                        }
                    }
                }

                if (v26) {
                    sub_436ED0(anim_id);
                }
            }
        }
    } else {
        if (!anim_goal_add(&goal_data, &anim_id)) {
            return;
        }
    }

    // 0x54FB19
    if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
        sub_436C50(anim_id);
    } else {
        if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
            turn_on_running(anim_id);
        } else {
            if (tig_kb_get_modifier(SDL_KMOD_NUM)) {
                if (get_always_run(pc_obj)
                    && !tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                    turn_on_running(anim_id);
                }
            } else {
                if (!tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                    turn_on_running(anim_id);
                }
            }
        }
    }

    if (v26) {
        sub_436ED0(anim_id);
    }
}

// 0x54FCF0
void intgame_hotkey_activate(Hotkey* hotkey)
{
    int64_t pc_obj;
    int64_t weapon_obj;
    int64_t v1;
    int64_t v2;

    if ((hotkey->flags & HOTKEY_DRAGGED) != 0) {
        return;
    }

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return;
    }

    if (critter_is_dead(pc_obj)) {
        return;
    }

    if (critter_is_unconscious(pc_obj)) {
        return;
    }

    if ((obj_field_int32_get(pc_obj, OBJ_F_CRITTER_FLAGS) & (OCF_PARALYZED | OCF_STUNNED)) != 0) {
        return;
    }

    switch (hotkey->type) {
    case HOTKEY_NONE:
        // Should be unreachable.
        break;
    case HOTKEY_ITEM:
        intgame_mode_set(INTGAME_MODE_MAIN);
        sub_444130(&(hotkey->item_obj));
        if (obj_field_handle_get(hotkey->item_obj.obj, OBJ_F_ITEM_PARENT) == pc_obj) {
            v2 = hotkey->item_obj.obj;
            if (obj_field_int32_get(v2, OBJ_F_TYPE) != OBJ_TYPE_WRITTEN
                || (sub_462C30(pc_obj, v2)
                    && (obj_field_int32_get(v2, OBJ_F_ITEM_FLAGS) & OIF_USE_IS_THROW) == 0)) {
                v1 = item_find_first_matching_prototype(pc_obj, v2);
                if (v1 != OBJ_HANDLE_NULL) {
                    v2 = v1;
                }
            }

            if (sub_462C30(pc_obj, v2)
                || (obj_field_int32_get(v2, OBJ_F_ITEM_FLAGS) & OIF_USE_IS_THROW) != 0) {
                switch (obj_field_int32_get(v2, OBJ_F_TYPE)) {
                case OBJ_TYPE_WEAPON:
                    sub_550000(pc_obj, hotkey, ITEM_INV_LOC_WEAPON);
                    break;
                case OBJ_TYPE_AMMO:
                case OBJ_TYPE_GOLD:
                    break;
                case OBJ_TYPE_ARMOR:
                    sub_550000(pc_obj, hotkey, item_location_get(v2));
                    break;
                default:
                    v1 = inven_ui_drag_item_obj_get();
                    if (v1 != v2) {
                        if (v1 != OBJ_HANDLE_NULL) {
                            sub_575770();
                        }
                        inven_ui_destroy();
                        sub_573740(v2, true);
                    }
                    break;
                }
            } else if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                item_use_on_obj(pc_obj, v2, pc_obj);
            } else {
                item_use_on_obj(pc_obj, v2, OBJ_HANDLE_NULL);
            }
        }
        break;
    case HOTKEY_SKILL:
        sub_579FA0(pc_obj, hotkey->data);
        break;
    case HOTKEY_SPELL:
        spell_ui_activate(pc_obj, hotkey->data);
        break;
    case HOTKEY_ITEM_SPELL:
        sub_444130(&(hotkey->item_obj));

        weapon_obj = item_wield_get(pc_obj, ITEM_INV_LOC_WEAPON);
        if (obj_field_handle_get(hotkey->item_obj.obj, OBJ_F_ITEM_PARENT) == pc_obj) {
            if (weapon_obj == hotkey->item_obj.obj) {
                sub_57C080(hotkey->item_obj.obj, hotkey->data);
            } else if (weapon_obj == OBJ_HANDLE_NULL || sub_464C80(weapon_obj)) {
                if (item_wield_set(hotkey->item_obj.obj, ITEM_INV_LOC_WEAPON)) {
                    sub_57C080(hotkey->item_obj.obj, hotkey->data);
                }
            }
        }
        break;
    }
}

// 0x550000
void sub_550000(int64_t critter_obj, Hotkey* hotkey, int inventory_location)
{
    int64_t item_obj;
    int v1;
    int sound_id;

    item_obj = item_wield_get(critter_obj, inventory_location);
    if (item_obj == hotkey->item_obj.obj) {
        return;
    }

    if (item_obj != OBJ_HANDLE_NULL) {
        v1 = sub_464D20(hotkey->item_obj.obj, inventory_location, critter_obj);
        if (v1 != 0 && v1 != 4) {
            item_error_msg(critter_obj, v1);
            return;
        }

        if (!sub_464C80(item_obj)) {
            return;
        }
    }

    v1 = sub_464D20(hotkey->item_obj.obj, inventory_location, critter_obj);
    if (v1 != 0) {
        item_error_msg(critter_obj, v1);
        if (item_obj != OBJ_HANDLE_NULL) {
            item_wield_set(item_obj, inventory_location);
        }
        return;
    }

    if (!item_wield_set(hotkey->item_obj.obj, inventory_location)) {
        item_error_msg(critter_obj, 0);
        if (item_obj != OBJ_HANDLE_NULL) {
            item_wield_set(item_obj, inventory_location);
        }
        return;
    }

    if (item_obj != OBJ_HANDLE_NULL) {
        sound_id = sfx_item_sound(item_obj, critter_obj, OBJ_HANDLE_NULL, ITEM_SOUND_DROP);
    } else {
        // FIXME: Looks wrong.
        sound_id = inventory_location;
    }

    if (sound_id != -1) {
        gsound_play_sfx(sound_id, 1);
    }

    sub_57E5A0(hotkey);
}

// 0x550150
void intgame_hotkey_highlight(Hotkey* hotkey)
{
    if ((hotkey->flags & HOTKEY_DRAGGED) != 0) {
        return;
    }

    switch (hotkey->type) {
    case HOTKEY_NONE:
        // Should be unreachable.
        break;
    case HOTKEY_ITEM:
        sub_57CCF0(player_get_local_pc_obj(), hotkey->item_obj.obj);
        break;
    case HOTKEY_SKILL:
        intgame_message_window_display_skill(hotkey->data);
        break;
    case HOTKEY_SPELL:
    case HOTKEY_ITEM_SPELL:
        intgame_message_window_display_spell(hotkey->data);
        break;
    }
}

// 0x5501C0
bool sub_5501C0(void)
{
    int index;
    tig_button_handle_t college_radio_group[COLLEGE_COUNT];
    int college_radio_group_size = 0;
    int selected_college_index = 0;
    tig_button_handle_t group[3];

    for (index = 0; index < 2; index++) {
        button_create_no_art(&(stru_5C65F8[index]), 382, 41);
    }

    for (index = 0; index < COLLEGE_COUNT; index++) {
        intgame_college_buttons[index].art_num = spell_college_small_icon(index);
        if (intgame_college_buttons[index].art_num != -1) {
            intgame_button_create(&(intgame_college_buttons[index]));
            college_radio_group[college_radio_group_size] = intgame_college_buttons[index].button_handle;

            if (index == dword_64C530) {
                selected_college_index = college_radio_group_size;
            }

            college_radio_group_size++;
        }
    }

    for (index = 0; index < 4; index++) {
        stru_5C6C68[index].art_num = sub_579F50(index);
        if (stru_5C6C68[index].art_num != -1) {
            intgame_button_create(&(stru_5C6C68[index]));
        }
    }

    intgame_spells_init();
    intgame_mt_spells_init();

    for (index = 0; index < 5; index++) {
        stru_64C4A8[index].x = intgame_rotwin_text_frame[5].rect.x;
        stru_64C4A8[index].y = intgame_rotwin_text_frame[5].rect.y + intgame_rotwin_text_frame[5].rect.y / 5;
        stru_64C4A8[index].art_num = -1;
        stru_64C4A8[index].button_handle = TIG_BUTTON_HANDLE_INVALID;
        button_create_no_art(&(stru_64C4A8[index]), intgame_rotwin_text_frame[5].rect.width, intgame_rotwin_text_frame[5].rect.y / 5);
    }

    for (index = 0; index < 6; index++) {
        intgame_button_create(&(stru_5C6CA8[index]));
    }

    for (index = 3; index < 6; index++) {
        group[index - 3] = stru_5C6CA8[index].button_handle;
    }
    tig_button_radio_group_create(3, group, 0);

    for (index = 0; index < INTGAME_QUANTITY_BUTTON_COUNT; index++) {
        intgame_button_create(&(intgame_quantity_buttons[index]));
    }

    for (index = 1; index < 11; index++) {
        iso_interface_window_disable(index);
    }

    sub_5503F0(ROTWIN_TYPE_MSG, 100);

    tig_button_radio_group_create(college_radio_group_size,
        college_radio_group,
        selected_college_index);

    intgame_iso_window_type = ROTWIN_TYPE_MSG;

    dword_5C6D58 = ROTWIN_TYPE_INVALID;

    return true;
}

// 0x5503F0
bool sub_5503F0(RotatingWindowType window_type, int progress)
{
    int iwid;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;

    iwid = find_interface_window_index(intgame_rotwin_button_info[window_type].x, intgame_rotwin_button_info[window_type].y);
    if (iwid == -1) {
        return false;
    }

    tig_art_interface_id_create(intgame_rotwin_button_info[window_type].art_num, 0, 0, 0, &(art_blit_info.art_id));
    tig_art_frame_data(art_blit_info.art_id, &art_frame_data);

    src_rect.width = art_frame_data.width;
    src_rect.height = progress * art_frame_data.height / 100;
    src_rect.x = 0;
    src_rect.y = art_frame_data.height - src_rect.height;

    dst_rect.width = src_rect.width;
    dst_rect.height = src_rect.height;
    dst_rect.x = intgame_rotwin_button_info[window_type].x - intgame_interface_window_frames[iwid].x;
    dst_rect.y = intgame_rotwin_button_info[window_type].y - intgame_interface_window_frames[iwid].y;

    art_blit_info.flags = 0;
    art_blit_info.src_rect = &src_rect;
    art_blit_info.dst_rect = &dst_rect;

    return tig_window_blit_art(dword_64C4F8[iwid], &art_blit_info);
}

// 0x5504F0
void iso_interface_window_disable(RotatingWindowType window_type)
{
    int index;

    switch (window_type) {
    case ROTWIN_TYPE_MSG:
        for (index = 0; index < 2; index++) {
            tig_button_hide(stru_5C65F8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_SPELLS:
        for (index = 0; index < COLLEGE_COUNT; index++) {
            if (spell_college_small_icon(index) != -1) {
                tig_button_hide(intgame_college_buttons[index].button_handle);
            }
        }
        intgame_spells_hide_college_spells(dword_64C530);
        break;
    case ROTWIN_TYPE_SKILLS:
        for (index = 0; index < 4; index++) {
            if (sub_579F50(index) != -1) {
                tig_button_hide(stru_5C6C68[index].button_handle);
            }
        }
        break;
    case ROTWIN_TYPE_CHAT:
        break;
    case ROTWIN_TYPE_TRAPS:
        break;
    case ROTWIN_TYPE_DIALOGUE:
        for (index = 0; index < 5; index++) {
            tig_button_hide(stru_64C4A8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_MAP_NOTE:
        for (index = 0; index < 6; index++) {
            tig_button_hide(stru_5C6CA8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_BROADCAST:
        break;
    case ROTWIN_TYPE_MAGICTECH:
        intgame_mt_spells_disable();
        break;
    case ROTWIN_TYPE_QUANTITY:
        for (index = 0; index < INTGAME_QUANTITY_BUTTON_COUNT; index++) {
            tig_button_hide(intgame_quantity_buttons[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_MP_KICKBAN:
        break;
    default:
        tig_debug_printf("iso_interface_window_disable: ERROR: window type out of range!\n");
        break;
    }

    dword_64C6E0 = false;
}

// 0x5506C0
void iso_interface_window_set(RotatingWindowType window_type)
{
    broadcast_ui_close();

    if (intgame_iso_window_type == ROTWIN_TYPE_QUANTITY) {
        intgame_mode_set(INTGAME_MODE_MAIN);
    }

    // CE: capture the effective result before the swap, since the swap
    // updates intgame_iso_window_type. Same-type re-press is the user's
    // dismiss gesture (e.g. K → SKILLS → K → MSG).
    bool toggled_off = (intgame_iso_window_type == window_type);
    RotatingWindowType effective = toggled_off ? ROTWIN_TYPE_MSG : window_type;

    // CE: MINI-peek special case. See intgame_hud_handle_mini_peek_press
    // for the full state machine. If it handles the press, we return —
    // the rotwin type stays as-is, only the crop stage flips.
    if (toggled_off && intgame_hud_handle_mini_peek_press()) {
        return;
    }

    intgame_rotwin_step = MAX_INTERFACE_WINDOW_ROTATION_STEPS;
    if (toggled_off) {
        dword_64C6AC = ROTWIN_TYPE_MSG;
        iso_interface_window_swap(ROTWIN_TYPE_MSG);
    } else {
        dword_64C6AC = window_type;
        iso_interface_window_swap(window_type);
    }

    // CE: HUD-crop coupling.
    // - Real rotwin (skills/spells/magictech-weapon/etc.) invoked from
    //   MINI/HIDDEN: pop to MEDIUM and stash the prior stage.
    // - Result is MSG (toggle-off or explicit dismiss): restore the
    //   stashed stage so the user returns to where they were.
    // Auto-reverts that route through iso_interface_window_swap directly
    // (cursor leaving the bar, mode exits) bypass this entirely.
    if (effective == ROTWIN_TYPE_MSG) {
        intgame_hud_restore_after_rotwin();
    } else if (effective != ROTWIN_TYPE_INVALID) {
        intgame_hud_auto_pop_for_rotwin();
    }
}

// 0x550720
void intgame_message_window_clear(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        return;
    }

    if (dword_64C6D4 != NULL) {
        dword_64C6D4(0);
    } else {
        intgame_message_window_clear_internal();
    }
}

// 0x550750
void intgame_message_window_display_msg(UiMessage* ui_message)
{
    sub_552770(ui_message);
    if (dword_64C6D4 != NULL) {
        dword_64C6D4(ui_message);
    }
}

// 0x550770
void intgame_message_window_display_str(int a1, char* str)
{
    UiMessage ui_message;

    (void)a1;

    if (dword_64C6D4 != NULL) {
        ui_message.type = UI_MSG_TYPE_FEEDBACK;
        ui_message.str = str;
        dword_64C6D4(&ui_message);
    } else if (intgame_iso_interface_created) {
        intgame_message_window_write_text_centered(str, &(intgame_rotwin_text_frame[intgame_iso_window_type].rect));
    }
}

// 0x5507D0
void sub_5507D0(void (*func)(UiMessage* ui_message))
{
    dword_64C6D4 = func;
}

// 0x5507E0
void intgame_message_window_display_spell(int spl)
{
    UiMessage ui_message;

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        intgame_message_window_write_text_centered(spell_name(spl), &(intgame_rotwin_text_frame[intgame_iso_window_type].rect));
    } else {
        ui_message.type = UI_MSG_TYPE_SPELL;
        ui_message.field_8 = spl;
        ui_message.field_C = 0;
        ui_message.field_10 = player_get_local_pc_obj();
        intgame_message_window_display_msg(&ui_message);
        sub_552770(&ui_message);
    }
}

// 0x550860
void intgame_message_window_display_college(int college)
{
    UiMessage ui_message;

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        intgame_message_window_write_text_centered(spell_college_name(college), &(intgame_rotwin_text_frame[intgame_iso_window_type].rect));
    } else {
        ui_message.type = UI_MSG_TYPE_COLLEGE;
        ui_message.field_8 = college;
        intgame_message_window_display_msg(&ui_message);
        sub_552770(&ui_message);
    }
}

// 0x5508C0
void intgame_message_window_display_skill(int value)
{
    UiMessage ui_message;

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        intgame_message_window_write_text_centered(sub_57A700(value), &(intgame_rotwin_text_frame[intgame_iso_window_type].rect));
    } else {
        ui_message.type = UI_MSG_TYPE_SKILL;
        ui_message.field_8 = sub_57A6A0(value);
        ui_message.field_C = 0;
        intgame_message_window_display_msg(&ui_message);
    }
}

// 0x550930
void intgame_message_window_clear_internal(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    if (intgame_is_compact_interface()) {
        compact_ui_message_window_acquire();
        compact_ui_message_window_release();
    } else {
        tig_window_fill(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            &(intgame_rotwin_text_frame[intgame_iso_window_type].rect),
            tig_color_make(0, 0, 0));
    }
}

// 0x5509C0
void intgame_message_window_write_text_centered(char* str, TigRect* rect)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    if (intgame_rotwin_text_frame[intgame_iso_window_type].rect.width == 0) {
        return;
    }

    intgame_message_window_clear_internal();
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        rect,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_CENTER | MSG_TEXT_VALIGN_CENTER);
}

// 0x550A10
bool intgame_message_window_write_text(tig_window_handle_t window_handle, char* str, TigRect* rect, tig_font_handle_t font, unsigned int flags)
{
    TigFont font_desc;
    TigRect text_rect;
    int width;
    int rc;

    if (!intgame_iso_interface_created) {
        return false;
    }

    tig_font_push(font);

    font_desc.str = str;
    font_desc.width = 0;
    tig_font_measure(&font_desc);

    text_rect.x = rect->x;
    text_rect.y = rect->y;

    if (intgame_is_compact_interface()) {
        text_rect.x -= 210;
        text_rect.y -= 59;
    }

    width = rect->width;
    if ((flags & MSG_TEXT_SECONDARY) != 0) {
        width /= 2;
        text_rect.x += width;
    }

    if (font_desc.width >= width) {
        font_desc.width = width;
        tig_font_measure(&font_desc);
        if (font_desc.height > rect->height) {
            font_desc.height = rect->height;
        }
    } else {
        if ((flags & MSG_TEXT_HALIGN_CENTER) != 0) {
            text_rect.x += (width - font_desc.width) / 2;
        } else if ((flags & MSG_TEXT_HALIGN_RIGHT) != 0) {
            text_rect.x += width - font_desc.width;
        }
    }

    if ((flags & MSG_TEXT_VALIGN_CENTER) != 0) {
        text_rect.y = rect->y + (rect->height - font_desc.height) / 2;

        if (intgame_is_compact_interface()) {
            text_rect.y -= 59;
        }
    }

    text_rect.width = font_desc.width;
    text_rect.height = font_desc.height;

    if (intgame_is_compact_interface()) {
        if (str[0] == '\0') {
            // FIXME: Leaking font!
            return true;
        }

        window_handle = compact_ui_message_window_acquire();
    }

    rc = tig_window_text_write(window_handle, str, &text_rect);
    if (rc != TIG_OK) {
        if ((flags & MSG_TEXT_TRUNCATE) != 0) {
            size_t pos;
            char ch;

            pos = strlen(str);
            while (rc != TIG_OK && pos > 0) {
                ch = str[pos - 1];
                str[pos - 1] = '\0';
                rc = tig_window_text_write(window_handle, str, &text_rect);
                str[pos - 1] = ch;
                pos--;
            }
        }
    }

    tig_font_pop();

    return rc == TIG_OK;
}

// 0x550BD0
bool intgame_spells_init(void)
{
    int clg;
    int spl;
    int lvl;

    for (clg = 0; clg < COLLEGE_COUNT; clg++) {
        for (lvl = 0; lvl < 5; lvl++) {
            spl = clg * 5 + lvl;
            intgame_spell_buttons[spl].art_num = spell_icon(spl);
            if (intgame_spell_buttons[spl].art_num != -1
                && !intgame_button_create(&(intgame_spell_buttons[spl]))) {
                return false;
            }
        }

        intgame_spells_hide_college_spells(clg);
    }

    return true;
}

// 0x550C60
void intgame_spells_show_college_spells(int clg)
{
    int64_t pc_obj;
    int spl;
    int lvl;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj != OBJ_HANDLE_NULL) {
        for (lvl = 0; lvl < 5; lvl++) {
            spl = clg * 5 + lvl;
            if (!spell_is_known(pc_obj, spl)) {
                break;
            }

            if (intgame_spell_buttons[spl].button_handle != TIG_BUTTON_HANDLE_INVALID) {
                tig_button_show(intgame_spell_buttons[spl].button_handle);
            }
        }
    }
}

// 0x550CD0
void intgame_spells_hide_college_spells(int clg)
{
    int spl;
    int lvl;

    for (lvl = 0; lvl < 5; lvl++) {
        spl = clg * 5 + lvl;
        if (intgame_spell_buttons[spl].button_handle != TIG_BUTTON_HANDLE_INVALID) {
            tig_button_hide(intgame_spell_buttons[spl].button_handle);
        }
    }

    sub_5503F0(intgame_iso_window_type, 100);
}

// 0x550D20
bool intgame_mt_spells_init(void)
{
    int index;

    for (index = 0; index < 5; index++) {
        intgame_mt_spell_buttons[index].art_num = spell_icon(0);
        if (intgame_mt_spell_buttons[index].art_num != -1) {
            if (!intgame_button_create(&(intgame_mt_spell_buttons[index]))) {
                return false;
            }
        }
    }

    intgame_mt_spells_disable();

    return true;
}

// 0x550D60
void intgame_mt_spells_disable(void)
{
    int index;

    for (index = 0; index < 5; index++) {
        if (intgame_mt_spell_buttons[index].button_handle != TIG_BUTTON_HANDLE_INVALID) {
            tig_button_hide(intgame_mt_spell_buttons[index].button_handle);
        }
    }

    sub_5503F0(intgame_iso_window_type, 100);
}

// 0x550DA0
void intgame_pc_lens_do(PcLensMode mode, PcLens* pc_lens)
{
    TigVideoBufferCreateInfo vb_create_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigArtBlitInfo blit_info;

    if (intgame_pc_lens_mode == mode) {
        return;
    }

    switch (intgame_pc_lens_mode) {
    case PC_LENS_MODE_NONE:
        break;
    case PC_LENS_MODE_PASSTHROUGH:
        if (tig_video_buffer_destroy(intgame_pc_lens_video_buffer) == TIG_OK) {
            intgame_pc_lens_video_buffer = NULL;
        }
        break;
    case PC_LENS_MODE_BLACKOUT:
        light_toggle();
        break;
    }

    switch (mode) {
    case PC_LENS_MODE_NONE:
        intgame_pc_lens.window_handle = TIG_WINDOW_HANDLE_INVALID;
        sub_558130(NULL);
        iso_redraw();

        if (dword_5C72B0 < 1) {
            gamelib_renderlock_release();
            dword_5C72B0++;
        }
        break;
    case PC_LENS_MODE_PASSTHROUGH:
        // CE: SNAP the iso viewport back to the PC so the lens shows the
        // player's surroundings. Must be a snap, NOT a tween: the lens
        // samples the live iso buffer at screen centre, so the camera has
        // to be centred on the PC by the time the lens redraws — a tween
        // would leave the lens looking at the wrong spot until it settled.
        // (The overlay-EXIT recenter animates instead; there's no lens to
        // keep in sync once the overlay closes.) Gated on
        // RECENTER_CAMERA_ON_OVERLAY_KEY — default off, the lens just
        // shows whatever's currently centered on screen so the player
        // can pan and still open overlays without losing their scroll.
        if (gamelib_recenter_camera_on_overlay()) {
            sub_551A10(player_get_local_pc_obj());
        }

        intgame_pc_lens.window_handle = pc_lens->window_handle;
        intgame_pc_lens.art_id = pc_lens->art_id;
        intgame_pc_lens.rect = &intgame_pc_lens_src_rect;
        intgame_pc_lens_src_rect = *pc_lens->rect;

        vb_create_info.width = intgame_pc_lens_dst_rect.width;
        vb_create_info.height = intgame_pc_lens_dst_rect.height;
        vb_create_info.flags = 0;
        vb_create_info.background_color = 0;

        if (tig_video_buffer_create(&vb_create_info, &intgame_pc_lens_video_buffer) == TIG_OK) {
            intgame_pc_lens_redraw();
        }

        sub_558130(&intgame_pc_lens_dst_rect);

        if (dword_5C72B0 < 1) {
            gamelib_renderlock_release();
            dword_5C72B0++;
        }
        break;
    case PC_LENS_MODE_BLACKOUT:
        light_toggle();

        intgame_pc_lens.window_handle = pc_lens->window_handle;
        intgame_pc_lens.art_id = pc_lens->art_id;
        intgame_pc_lens.rect = &intgame_pc_lens_src_rect;
        intgame_pc_lens_src_rect = *pc_lens->rect;

        tig_window_fill(intgame_pc_lens.window_handle,
            &intgame_pc_lens_src_rect,
            tig_color_make(0, 0, 0));

        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = intgame_pc_lens_src_rect.width;
        src_rect.height = intgame_pc_lens_src_rect.height;

        dst_rect.x = intgame_pc_lens.rect->x;
        dst_rect.y = intgame_pc_lens.rect->y;
        dst_rect.width = intgame_pc_lens_src_rect.width;
        dst_rect.height = intgame_pc_lens_src_rect.height;

        blit_info.flags = 0;
        blit_info.art_id = intgame_pc_lens.art_id;
        blit_info.src_rect = &src_rect;
        blit_info.dst_rect = &dst_rect;
        tig_window_blit_art(intgame_pc_lens.window_handle, &blit_info);

        sub_558130(&stru_64C698);

        gamelib_renderlock_acquire();
        dword_5C72B0--;
        break;
    }

    intgame_pc_lens_mode = mode;
}

// 0x551000
bool intgame_pc_lens_check_pt(int x, int y)
{
    TigWindowData window_data;

    if (intgame_pc_lens.window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return false;
    }

    if (tig_window_data(intgame_pc_lens.window_handle, &window_data) != TIG_OK) {
        return false;
    }

    if (intgame_pc_lens.rect == NULL) {
        return false;
    }

    return x >= window_data.rect.x + intgame_pc_lens.rect->x
        && x < window_data.rect.x + intgame_pc_lens.rect->x + intgame_pc_lens.rect->width
        && y >= window_data.rect.y + intgame_pc_lens.rect->y
        && y < window_data.rect.y + intgame_pc_lens.rect->y + intgame_pc_lens.rect->height;
}

// CE: Super ugly wrapper around `intgame_pc_lens_check_pt` that converts
// specified coordinates from old "fullscreen" 800x600 window to screen
// coordinates before delegating actual work to `intgame_pc_lens_check_pt`.
bool intgame_pc_lens_check_pt_unscale(int x, int y)
{
    hrp_center(&x, &y);
    return intgame_pc_lens_check_pt(x, y);
}

// True if a mouse click should dismiss an overlay menu: the click is
// outside the given menu rect AND not on either of the iso-interface HUD
// strips. All coordinates are in screen space.
//
// The 800x600 HUD strips are 800px wide and centered horizontally at hi-res,
// so empty screen area to either side of the strip (in the surrounding world
// view) is correctly treated as "outside HUD" and triggers dismissal.
// CE: one-shot guard against a stray click-outside dismiss. Overlays
// opened from a DIALOGUE option (worldmap / barter / charedit /
// schematic / identify) are selected on LEFT_BUTTON_DOWN —
// tc_handle_message only acts on mouse-down, dialogue options are
// mouse-only — so the paired LEFT_BUTTON_UP lands on the freshly
// opened overlay, outside its window, and would trip
// intgame_should_dismiss_overlay_click → instant close. The dialogue
// option handler arms this flag at the open; the next dismiss check
// (the paired mouse-up, the only event that calls the helper) clears
// it and returns false instead of dismissing. Because dialogue
// selection is mouse-only the paired up is guaranteed, so the flag is
// never left dangling for a keyboard path.
static bool intgame_overlay_dismiss_suppress_once = false;

void intgame_suppress_overlay_dismiss_once(void)
{
    intgame_overlay_dismiss_suppress_once = true;
}

bool intgame_should_dismiss_overlay_click(int screen_x, int screen_y, const TigRect* menu_rect)
{
    TigRect strip;

    // Consume the one stray mouse-up that pairs with the dialogue
    // option's mouse-down (see intgame_suppress_overlay_dismiss_once).
    // The helper is only ever called on LEFT_BUTTON_UP, so the first
    // call after a dialogue-driven open is exactly that stray up.
    if (intgame_overlay_dismiss_suppress_once) {
        intgame_overlay_dismiss_suppress_once = false;
        return false;
    }

    if (menu_rect == NULL) {
        return false;
    }

    if (screen_x >= menu_rect->x
        && screen_x < menu_rect->x + menu_rect->width
        && screen_y >= menu_rect->y
        && screen_y < menu_rect->y + menu_rect->height) {
        return false;
    }

    strip = intgame_interface_window_frames[0];
    hrp_apply(&strip, GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);
    if (screen_x >= strip.x
        && screen_x < strip.x + strip.width
        && screen_y >= strip.y
        && screen_y < strip.y + strip.height) {
        return false;
    }

    strip = intgame_interface_window_frames[1];
    hrp_apply(&strip, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
    if (screen_x >= strip.x
        && screen_x < strip.x + strip.width
        && screen_y >= strip.y
        && screen_y < strip.y + strip.height) {
        return false;
    }

    return true;
}

// 0x551080
void intgame_pc_lens_redraw(void)
{
    TigArtBlitInfo blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigRect lens_src_rect;

    if (intgame_pc_lens_video_buffer != NULL) {
        bool rendered_to_vb = false;

        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = intgame_pc_lens.rect->width;
        src_rect.height = intgame_pc_lens.rect->height;

        // CE: PC_LENS_FOLLOWS_PLAYER_KEY. The cheap path below just samples
        // the already-rendered main view, which can only show the PC while
        // the PC is on-screen. When the player has scrolled or zoomed the
        // PC off the view, sampling has nothing to copy — so give the lens
        // its OWN little 1:1 render centred on the PC instead (works at any
        // zoom/scroll, and renders only the lens-sized region). See
        // gamelib_render_lens_view.
        if (gamelib_pc_lens_follows_player()) {
            int64_t pc_obj = player_get_local_pc_obj();
            if (pc_obj != OBJ_HANDLE_NULL) {
                rendered_to_vb = gamelib_render_lens_view(intgame_pc_lens_video_buffer,
                    pc_obj, src_rect.width, src_rect.height);
            }
        }

        if (rendered_to_vb) {
            // The lens VB already holds the PC-centred world; show it in
            // the lens window, then the frame art on top.
            tig_window_copy_from_vbuffer(intgame_pc_lens.window_handle,
                intgame_pc_lens.rect, intgame_pc_lens_video_buffer, &src_rect);

            dst_rect = src_rect;
            dst_rect.x = intgame_pc_lens.rect->x;
            dst_rect.y = intgame_pc_lens.rect->y;

            blit_info.art_id = intgame_pc_lens.art_id;
            blit_info.flags = 0;
            blit_info.src_rect = &src_rect;
            blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(intgame_pc_lens.window_handle, &blit_info);
            return;
        }

        // Cheap path: copy whatever is at screen center from the rendered
        // main view — vanilla behavior, correct when the camera is centred
        // on the PC (e.g. recenter-camera-on-overlay).
        lens_src_rect = intgame_pc_lens_dst_rect;

        tig_window_copy(intgame_pc_lens.window_handle,
            intgame_pc_lens.rect,
            dword_64C52C,
            &lens_src_rect);

        if (tig_window_copy_to_vbuffer(dword_64C52C, &lens_src_rect, intgame_pc_lens_video_buffer, &src_rect) == TIG_OK) {
            dst_rect = src_rect;
            dst_rect.x = intgame_pc_lens.rect->x;
            dst_rect.y = intgame_pc_lens.rect->y;

            blit_info.art_id = intgame_pc_lens.art_id;
            blit_info.flags = 0;
            blit_info.src_rect = &src_rect;
            blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(intgame_pc_lens.window_handle, &blit_info);
        }
    }
}

// 0x551160
void iso_interface_refresh(void)
{
    int64_t pc_obj;
    int64_t obj;

    if (!intgame_iso_interface_created) {
        return;
    }

    pc_obj = player_get_local_pc_obj();

    iso_interface_window_disable(intgame_iso_window_type);
    iso_interface_window_enable(intgame_iso_window_type);

    if (intgame_iso_window_type == ROTWIN_TYPE_MSG) {
        if (pc_obj != OBJ_HANDLE_NULL) {
            if (qword_64C690 != OBJ_HANDLE_NULL) {
                sub_57CCF0(pc_obj, qword_64C690);
            } else if ((obj = object_hover_obj_get()) != OBJ_HANDLE_NULL) {
                sub_57CCF0(pc_obj, obj);
            } else {
                intgame_message_refresh(false);
            }
        } else {
            intgame_message_refresh(false);
        }
    }

    if (pc_obj != OBJ_HANDLE_NULL) {
        intgame_draw_counter(INTGAME_COUNTER_FATE,
            stat_level_get(pc_obj, STAT_FATE_POINTS),
            2);
        intgame_refresh_experience_gauges(pc_obj);
    }
}

// 0x551210
void iso_interface_window_enable(RotatingWindowType window_type)
{
    int64_t pc_obj;
    int64_t obj;
    int fld;
    int qty_fld;
    int index;
    TigArtBlitInfo blit_info;
    TigArtFrameData art_frame_data;
    TigRect src_rect;
    TigRect dst_rect;

    if (dword_64C6E0) {
        iso_interface_window_disable(intgame_iso_window_type);
    }

    sub_5503F0(window_type, 100);
    intgame_iso_window_type = window_type;

    pc_obj = player_get_local_pc_obj();

    switch (window_type) {
    case ROTWIN_TYPE_MSG:
        for (index = 0; index < 2; index++) {
            tig_button_show(stru_5C65F8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_SPELLS:
        for (index = 0; index < COLLEGE_COUNT; index++) {
            if (spell_college_small_icon(index) != -1
                && spell_college_is_known(pc_obj, index)) {
                tig_button_show(intgame_college_buttons[index].button_handle);
            }
        }
        tig_button_state_change(intgame_college_buttons[dword_64C530].button_handle, TIG_BUTTON_STATE_PRESSED);
        intgame_spells_show_college_spells(dword_64C530);
        break;
    case ROTWIN_TYPE_SKILLS:
        for (index = 0; index < 4; index++) {
            if (sub_579F50(index) != -1) {
                tig_button_show(stru_5C6C68[index].button_handle);
            }
        }
        break;
    case ROTWIN_TYPE_CHAT:
        break;
    case ROTWIN_TYPE_TRAPS:
        break;
    case ROTWIN_TYPE_DIALOGUE:
        for (index = 0; index < 5; index++) {
            tig_button_show(stru_64C4A8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_MAP_NOTE:
        for (index = 0; index < 6; index++) {
            tig_button_show(stru_5C6CA8[index].button_handle);
        }
        break;
    case ROTWIN_TYPE_BROADCAST:
        break;
    case ROTWIN_TYPE_MAGICTECH:
        intgame_mt_spells_enable();
        break;
    case ROTWIN_TYPE_QUANTITY:
        for (index = 0; index < INTGAME_QUANTITY_BUTTON_COUNT; index++) {
            tig_button_show(intgame_quantity_buttons[index].button_handle);
        }

        intgame_quantity = 0;

        obj = sub_579760();
        fld = sub_462410(obj, &qty_fld);
        if (fld != -1) {
            intgame_max_quantity = obj_field_int32_get(obj, qty_fld);
            switch (fld) {
            case OBJ_F_CRITTER_GOLD:
                tig_art_item_id_create(0, 2, 0, 0, 0, 3, 0, 0, &(blit_info.art_id));
                break;
            case OBJ_F_CRITTER_ARROWS:
                tig_art_item_id_create(0, 2, 0, 0, 0, 1, 0, 0, &(blit_info.art_id));
                break;
            case OBJ_F_CRITTER_BULLETS:
                tig_art_item_id_create(1, 2, 0, 0, 0, 1, 0, 0, &(blit_info.art_id));
                break;
            case OBJ_F_CRITTER_POWER_CELLS:
                tig_art_item_id_create(2, 2, 0, 0, 0, 1, 0, 0, &(blit_info.art_id));
                break;
            case OBJ_F_CRITTER_FUEL:
                tig_art_item_id_create(3, 2, 0, 0, 0, 1, 0, 0, &(blit_info.art_id));
                break;
            }

            tig_art_frame_data(blit_info.art_id, &art_frame_data);

            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = 269;
            dst_rect.y = 93;
            dst_rect.width = art_frame_data.width;
            dst_rect.height = art_frame_data.height;

            blit_info.flags = 0;
            blit_info.src_rect = &src_rect;
            blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(dword_64C4F8[1], &blit_info);
        } else {
            intgame_max_quantity = 1;
        }
        intgame_refresh_quantity();
        break;
    case ROTWIN_TYPE_MP_KICKBAN:
        break;
    default:
        tig_debug_printf("iso_interface_window_enable: ERROR: window type out of range!");
        break;
    }

    dword_64C6E0 = true;
}

// 0x551660
void intgame_mt_spells_enable(void)
{
    int index;
    int spl;
    tig_art_id_t art_id;

    if (player_get_local_pc_obj() == OBJ_HANDLE_NULL
        || qword_64C688 == OBJ_HANDLE_NULL) {
        return;
    }

    if ((obj_field_int32_get(qword_64C688, OBJ_F_ITEM_FLAGS) & OIF_IDENTIFIED) != 0
        && obj_field_int32_get(qword_64C688, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY) > 0) {
        for (index = 0; index < 5; index++) {
            spl = mt_item_spell(qword_64C688, index);
            if (spl != -1
                && !magictech_is_tech(spl)
                && intgame_mt_spell_buttons[index].art_num != -1) {
                intgame_mt_spell_buttons[index].art_num = spell_icon(spl);
                tig_art_interface_id_create(intgame_mt_spell_buttons[index].art_num, 0, 0, 0, &art_id);
                tig_button_set_art(intgame_mt_spell_buttons[index].button_handle, art_id);
                tig_button_show(intgame_mt_spell_buttons[index].button_handle);
            }
        }
    }

    intgame_counters_refresh();
}

// 0x551740
int find_interface_window_index(int x, int y)
{
    int index;

    for (index = 0; index < 2; index++) {
        if (x >= intgame_interface_window_frames[index].x
            && y >= intgame_interface_window_frames[index].y
            && x < intgame_interface_window_frames[index].x + intgame_interface_window_frames[index].width
            && y < intgame_interface_window_frames[index].y + intgame_interface_window_frames[index].height) {
            return index;
        }
    }

    return -1;
}

// 0x5517A0
bool sub_5517A0(TigMessage* msg)
{
    tig_window_handle_t window_handle;
    TigWindowData window_data;

    if (tig_window_get_at_position(msg->data.mouse.x, msg->data.mouse.y, &window_handle) != TIG_OK) {
        return false;
    }

    if (window_handle != dword_64C52C) {
        // FIXME: Meaningless.
        tig_window_data(window_handle, &window_data);
        return false;
    }

    return true;
}

// 0x5517F0
void sub_5517F0(void)
{
    TigMouseState mouse_state;
    TigMessage msg;

    if (tig_mouse_get_state(&mouse_state) == TIG_OK) {
        // NOTE: Fake TigMessage object, albeit incomplete - underlying code
        // is only interested in mouse coordinates.
        msg.data.mouse.x = mouse_state.x;
        msg.data.mouse.y = mouse_state.y;
        sub_551910(&msg);
    }
}

// 0x551830
bool intgame_get_location_under_cursor(int64_t* loc_ptr)
{
    TigMouseState mouse_state;
    TargetDescriptor td;
    int x, y;

    if (tig_mouse_get_state(&mouse_state) == TIG_OK
        && sub_5518C0(mouse_state.x, mouse_state.y)) {
        intgame_adjust_mouse_for_zoom(mouse_state.x, mouse_state.y, &x, &y);
        if (target_pick_at_screen_xy_ex(x, y, &td, TGT_TILE, intgame_fullscreen)
            && td.is_loc) {
            *loc_ptr = td.loc;
            return true;
        }
    }

    *loc_ptr = 0;
    return false;
}

// 0x5518C0
bool sub_5518C0(int x, int y)
{
    tig_window_handle_t window_handle;
    TigWindowData window_data;

    if (tig_window_get_at_position(x, y, &window_handle) != TIG_OK) {
        return false;
    }

    if (window_handle != dword_64C52C) {
        // FIXME: Meaningless.
        tig_window_data(window_handle, &window_data);
        return false;
    }

    return true;
}

static bool intgame_adjust_mouse_for_zoom(int x, int y, int* adj_x, int* adj_y)
{
    float z = iso_zoom_current();

    if (z != 1.0f) {
        int64_t ax;
        int64_t ay;

        location_zoom_adjust_screen_xy(x, y, z, &ax, &ay);
        *adj_x = (int)ax;
        *adj_y = (int)ay;
        return true;
    }

    *adj_x = x;
    *adj_y = y;
    return false;
}

// 0x551910
void sub_551910(TigMessage* msg)
{
    TargetDescriptor td;
    int x;
    int y;

    if (sub_5517A0(msg)) {
        sub_551F80();

        intgame_adjust_mouse_for_zoom(msg->data.mouse.x, msg->data.mouse.y, &x, &y);

        if (!map_is_clearing_objects()) {
            if (target_pick_at_screen_xy_ex(x, y, &td, qword_5C7280, intgame_fullscreen)) {
                if (!td.is_loc) {
                    sub_57CCF0(player_get_local_pc_obj(), td.obj);
                    object_hover_obj_set(td.obj);
                }
            } else if (combat_turn_based_is_active()
                && target_pick_at_screen_xy_ex(x, y, &td, TGT_TILE, intgame_fullscreen)
                && td.is_loc
                && intgame_mode_get() == INTGAME_MODE_MAIN) {
                combat_check_move_to(player_get_local_pc_obj(), td.loc);
            }
        }
    }
}

// 0x551A00
IntgameMode intgame_mode_get(void)
{
    return intgame_mode_stack[intgame_mode_stack_size];
}

// CE: Public wrapper for overlay screens that close on a PC-lens click.
// The lens widget is a "back to the player" button, so clicking it always
// GLIDES the iso camera to the PC (was a hard snap) — even when the
// recenter-camera-on-overlay opt-in is off. The animated recenter is the
// lens's whole point: it returns you to the player as the overlay closes.
void intgame_recenter_on_pc(void)
{
    intgame_recenter_on_pc_tween(0);
}

// CE: GLIDE the iso camera so `location` lands at screen center over
// duration_ms (0 = the camera_tween default) instead of snapping. Same
// center-on-location math as location_origin_set; the resulting delta is
// handed to camera_tween_by so the move is a smooth pan. Also clears any
// stale camera-follow cooldown first so follow picks up cleanly when the
// tween lands (the tween itself is yielded-to by follow while in flight;
// this handles a cooldown armed BEFORE the recenter). No-op when already
// centred or when `location` is invalid.
void intgame_recenter_on_location_tween(int64_t location, unsigned int duration_ms)
{
    int64_t dx;
    int64_t dy;

    if (location == 0) {
        return;
    }
    camera_follow_note_recenter();
    location_calc_dist_from_screen_center(location, &dx, &dy);
    camera_tween_by(dx, dy, duration_ms);
}

// CE: convenience — tween-recenter on the local PC. See
// intgame_recenter_on_location_tween.
void intgame_recenter_on_pc_tween(unsigned int duration_ms)
{
    int64_t obj;

    obj = player_get_local_pc_obj();
    if (obj == OBJ_HANDLE_NULL) {
        return;
    }
    intgame_recenter_on_location_tween(obj_field_int64_get(obj, OBJ_F_LOCATION), duration_ms);
}

// 0x551A10
void sub_551A10(int64_t obj)
{
    int64_t location;
    int64_t x;
    int64_t y;

    if (obj != OBJ_HANDLE_NULL) {
        location = obj_field_int64_get(obj, OBJ_F_LOCATION);
        location_calc_dist_from_screen_center(location, &x, &y);
        if (x != 0 || y != 0) {
            location_origin_set(location);
            iso_redraw();
        }
    }
}

// 0x551A80
bool intgame_mode_set(IntgameMode mode)
{
    int64_t pc_obj;
    int64_t obj;
    IntgameMode prev_mode;
    bool v1 = false;
    bool v2 = false;
    bool v17 = false;
    bool v18 = false;

    if (dword_64C6E8) {
        return true;
    }

    while (1) {
        dword_64C6E8 = true;
        pc_obj = player_get_local_pc_obj();
        prev_mode = intgame_mode_stack[intgame_mode_stack_size];

        if (mode != INTGAME_MODE_MAIN) {
            intgame_mode_stack_size++;
            v18 = true;
        } else {
            if (intgame_mode_stack_size > 0) {
                mode = intgame_mode_stack[--intgame_mode_stack_size];
                v2 = true;
            }
            if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
                intgame_refresh_cursor();
            }
        }

        switch (prev_mode) {
        case INTGAME_MODE_MAIN:
        case INTGAME_MODE_FOLLOWER:
            v1 = true;
            target_flags_set(TGT_OBJECT | TGT_OBJ_NO_T_WALL | TGT_TILE);
            break;
        case INTGAME_MODE_SPELL:
            v1 = true;
            v17 = true;
            spell_ui_cancel();
            combat_check_cast_spell(player_get_local_pc_obj());
            break;
        case INTGAME_MODE_SKILL:
            v1 = true;
            v17 = true;
            skill_ui_cancel();
            combat_check_use_skill(player_get_local_pc_obj());
            break;
        case INTGAME_MODE_DIALOG:
            v1 = 1;
            if (v2) {
                dialog_ui_end_dialog(player_get_local_pc_obj(), 0);
            } else {
                dialog_ui_notify_dialog_ended(player_get_local_pc_obj());
            }
            break;
        case INTGAME_MODE_BARTER:
            if (mode != INTGAME_MODE_QUANTITY) {
                inven_ui_destroy();
            }
            v1 = true;
            if (mode != INTGAME_MODE_QUANTITY) {
                intgame_unforce_fullscreen();
            }
            break;
        case INTGAME_MODE_WMAP:
            v1 = true;
            wmap_ui_close();
            if (intgame_iso_window_type == ROTWIN_TYPE_MAP_NOTE) {
                iso_interface_window_set(ROTWIN_TYPE_MSG);
            }
            scroll_set_scroll_func(NULL);
            intgame_unforce_fullscreen();
            break;
        case INTGAME_MODE_SLEEP:
            v1 = true;
            sleep_ui_close();
            break;
        case INTGAME_MODE_LOGBOOK:
            v1 = true;
            logbook_ui_close();
            intgame_unforce_fullscreen();
            break;
        case INTGAME_MODE_INVEN:
            switch (mode) {
            case INTGAME_MODE_SPELL:
            case INTGAME_MODE_SKILL:
            case INTGAME_MODE_QUANTITY:
            case INTGAME_MODE_ITEM:
                v1 = true;
                break;
            default:
                v1 = false;
                inven_ui_destroy();
                break;
            }

            if (mode != INTGAME_MODE_QUANTITY) {
                intgame_unforce_fullscreen();
            }
            break;
        case INTGAME_MODE_CHAREDIT:
            v1 = true;
            charedit_close();
            intgame_unforce_fullscreen();
            break;
        case INTGAME_MODE_LOOT:
            if (mode == INTGAME_MODE_SPELL || mode == INTGAME_MODE_SKILL) {
                if (mode != INTGAME_MODE_QUANTITY) {
                    intgame_unforce_fullscreen();
                }
            } else if (mode != INTGAME_MODE_QUANTITY) {
                inven_ui_destroy();
            }
            break;
        case INTGAME_MODE_STEAL:
            if (mode != INTGAME_MODE_QUANTITY) {
                inven_ui_destroy();
                intgame_unforce_fullscreen();
            }
            break;
        case INTGAME_MODE_QUANTITY:
            iso_interface_window_set(ROTWIN_TYPE_MSG);
            obj = sub_579760();
            item_flags_unset(obj, OIF_NO_DISPLAY);

            inven_ui_update(OBJ_HANDLE_NULL);
            break;
        case INTGAME_MODE_SCHEMATIC:
            v1 = true;
            schematic_ui_close();
            intgame_unforce_fullscreen();
            break;
        case INTGAME_MODE_WRITTEN:
            v1 = true;
            written_ui_close();
            intgame_unforce_fullscreen();
            break;
        case INTGAME_MODE_ITEM:
            item_ui_deactivate();
            if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                sub_575770();
                intgame_refresh_cursor();
            }
            break;
        case INTGAME_MODE_NPC_IDENTIFY:
        case INTGAME_MODE_NPC_REPAIR:
            if (mode != INTGAME_MODE_QUANTITY) {
                inven_ui_destroy();
            }
            v1 = true;
            intgame_unforce_fullscreen();
            break;
        default:
            break;
        }

        switch (mode) {
        case INTGAME_MODE_MAIN:
        case INTGAME_MODE_SPELL:
        case INTGAME_MODE_SKILL:
            sub_5517F0();
            break;
        case INTGAME_MODE_DIALOG:
            compact_ui_message_window_hide();
            if (!v18) {
                dialog_ui_notify_dialog_started(player_get_local_pc_obj());
            }
            break;
        case INTGAME_MODE_BARTER:
        case INTGAME_MODE_LOGBOOK:
        case INTGAME_MODE_INVEN:
        case INTGAME_MODE_CHAREDIT:
        case INTGAME_MODE_LOOT:
        case INTGAME_MODE_STEAL:
        case INTGAME_MODE_SCHEMATIC:
        case INTGAME_MODE_WRITTEN:
        case INTGAME_MODE_NPC_IDENTIFY:
        case INTGAME_MODE_NPC_REPAIR:
            intgame_force_fullscreen();
            break;
        case INTGAME_MODE_WMAP:
            intgame_force_fullscreen();
            scroll_set_scroll_func(wmap_ui_scroll);
            break;
        case INTGAME_MODE_QUANTITY:
            obj = sub_579760();
            item_flags_set(obj, OIF_NO_DISPLAY);
            inven_ui_update(OBJ_HANDLE_NULL);
            break;
        default:
            break;
        }

        if (v1) {
            if (inven_ui_drag_item_obj_get() != OBJ_HANDLE_NULL) {
                sub_575770();
            }
        }

        // CE: a non-MAIN mode is being pushed (a window, dialog, or
        // targeting mode laid on top of MAIN). Dismiss the fate overlay if
        // it's showing — fate is a pure overlay that never pushes an
        // intgame mode, so without this it lingers behind the new
        // fullscreen window and reappears when that window closes. This is
        // the reverse of fate_ui_toggle, which pops back to MAIN (closing
        // other windows) before opening fate. No-op when fate isn't open.
        // The animated slide-up is kept; fate_ui_toggle's dismiss-reversal
        // branch re-dismisses any open window if the user re-activates fate
        // mid-slide, so fate no longer gets stuck half-dismissed.
        if (v18) {
            fate_ui_close();
        }

        intgame_mode_stack[intgame_mode_stack_size] = mode;
        intgame_refresh_cursor();

        v1 = false;
        dword_64C6E8 = false;

        if (!v17) {
            return true;
        }

        mode = 0;
        v18 = false;
        v2 = false;
        v17 = false;
    }
}

// 0x551F20
void intgame_force_fullscreen(void)
{
    if (intgame_is_compact_interface()) {
        intgame_toggle_interface();
        intgame_fullscreen_forced = true;
    }
}

// 0x551F40
void intgame_unforce_fullscreen(void)
{
    if (intgame_fullscreen_forced) {
        if (!intgame_is_compact_interface()) {
            intgame_toggle_interface();
            intgame_fullscreen_forced = false;
        }
    }
}

// 0x551F70
bool intgame_mode_supports_scrolling(IntgameMode mode)
{
    return intgame_mode_scrolling[mode];
}

// 0x551F80
void sub_551F80(void)
{
    int64_t pc_obj;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj != OBJ_HANDLE_NULL) {
        if (intgame_mode_get() != INTGAME_MODE_MAIN) {
            qword_5C7280 = TGT_OBJECT;
            return;
        }

        if (combat_critter_is_combat_mode_active(pc_obj)) {
            if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                target_flags_set(TGT_OBJECT | TGT_OBJ_NO_SELF | TGT_OBJ_NO_T_WALL | TGT_TILE);
                qword_5C7280 = TGT_OBJECT | TGT_OBJ_NO_SELF | TGT_OBJ_NO_T_WALL;
            } else {
                target_flags_set(TGT_OBJECT | TGT_OBJ_NO_SELF | TGT_OBJ_NO_ST_CRITTER_DEAD | TGT_OBJ_NO_T_WALL | TGT_TILE | TGT_NON_PARTY_CRITTERS);
                qword_5C7280 = TGT_OBJECT | TGT_OBJ_NO_SELF | TGT_OBJ_NO_ST_CRITTER_DEAD | TGT_OBJ_NO_T_WALL | TGT_NON_PARTY_CRITTERS;
            }
            return;
        }
    }

    target_flags_set(TGT_OBJECT | TGT_OBJ_NO_T_WALL | TGT_TILE);
    qword_5C7280 = TGT_OBJECT;
}

// 0x552050
bool sub_552050(int x, int y, TargetDescriptor* td)
{
    if (intgame_adjust_mouse_for_zoom(x, y, &x, &y)) {
        sub_551F80();
        return target_pick_at_virtual_xy(x, y, td, intgame_fullscreen);
    }
    sub_551F80();
    return target_pick_at_screen_xy(x, y, td, intgame_fullscreen);
}

// 0x552070
RotatingWindowType iso_interface_window_get(void)
{
    return intgame_iso_window_type;
}

// 0x552080
void iso_interface_window_set_animated(RotatingWindowType window_type)
{
    if (window_type != ROTWIN_TYPE_INVALID) {
        sub_5520D0(window_type, intgame_rotwin_step);

        if (intgame_rotwin_step < MAX_INTERFACE_WINDOW_ROTATION_STEPS) {
            intgame_rotwin_step++;
            anim_ui_event_add(ANIM_UI_EVENT_TYPE_ROTATE_INTERFACE, window_type);
        } else {
            dword_5C6D58 = intgame_iso_window_type;
        }
    }
}

// 0x5520D0
void sub_5520D0(RotatingWindowType window_type, int step)
{
    if (intgame_iso_window_type != window_type) {
        if (step == 0) {
            iso_interface_window_disable(intgame_iso_window_type);
        } else if (step == MAX_INTERFACE_WINDOW_ROTATION_STEPS) {
            iso_interface_window_enable(window_type);
        } else {
            sub_5503F0(window_type, 100 * step / MAX_INTERFACE_WINDOW_ROTATION_STEPS);
        }
    }
}

// 0x552130
void iso_interface_window_swap(RotatingWindowType window_type)
{
    // CE: MINI's slim-row hack assumes SKILLS rotwin is the active
    // chrome (skill_rot art has a text well at art-local +71 that
    // exactly matches the MINI band). The engine internally swaps
    // the rotwin on world-hover (MSG / examine / etc.) — when stage
    // is MINI, ignore those swaps so the slim row keeps showing
    // SKILLS content instead of jittering between chrome layouts.
    // Allow re-swaps to SKILLS itself (used by the MINI-restore path).
    if (intgame_hud_in_mini_stage()
        && intgame_iso_window_type == ROTWIN_TYPE_SKILLS
        && window_type != ROTWIN_TYPE_SKILLS) {
        return;
    }
    iso_interface_window_disable(intgame_iso_window_type);
    intgame_rotwin_step = MAX_INTERFACE_WINDOW_ROTATION_STEPS;
    iso_interface_window_enable(window_type);
    dword_5C6D58 = intgame_iso_window_type;
    // CE: the bottom-strip clip band is anchored to the active chrome's
    // strip-local y. SKILLS/SPELLS chrome lives 1px higher than other
    // rotwin arts, so a type swap (e.g. MSG -> SKILLS) shifts the band
    // by that delta. Re-apply so the crop stays flush with the new
    // chrome. No-op in FULL stage (clears to NULL clip both times).
    intgame_hud_apply_clips();
}

// 0x552160
void intgame_text_edit_refresh(const char* str, tig_font_handle_t font)
{
    intgame_text_edit_refresh_color(str,
        font,
        tig_color_make(0, 0, 0),
        0);
}

// 0x5521B0
void intgame_text_edit_refresh_color(const char* str, tig_font_handle_t font, tig_color_t color, bool a4)
{
    tig_window_handle_t window_handle;
    TigRect rect;
    TigFont font_desc;

    window_handle = intgame_rotwin_text_frame[intgame_iso_window_type].window_handle;
    rect = intgame_rotwin_text_frame[intgame_iso_window_type].rect;

    tig_window_fill(window_handle, &rect, color);

    if (str != NULL && *str != '\0') {
        tig_font_push(font);
        font_desc.width = 0;
        font_desc.str = str;
        tig_font_measure(&font_desc);

        // NOTE: Many jumps, check.
        if (font_desc.width > rect.width && a4) {
            while (font_desc.width > rect.width && *str != '\0') {
                font_desc.width = 0;
                font_desc.str = str++;
                tig_font_measure(&font_desc);
            }

            if (*str == '\0') {
                tig_font_pop();
                return;
            }
        }

        rect.width = font_desc.width;

        tig_window_text_write(window_handle, str, &rect);
        tig_font_pop();
    } else {
        rect.width = 0;
    }

    tig_font_push(font);
    font_desc.width = 0;
    font_desc.str = "*";
    tig_font_measure(&font_desc);
    rect.x += rect.width + 3;
    rect.width = font_desc.width;
    if (tig_window_text_write(window_handle, "*", &rect) != TIG_OK) {
        tig_debug_printf("intgame_text_edit_refresh_color: ERROR: tig_window_text_write failed!\n");
    }
    tig_font_pop();
}

// 0x5522F0
void intgame_clock_refresh(void)
{
    char str[32];
    TigRect rect;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    TigFont font_desc;
    DateTime datetime;
    int v1;
    int cycle;
    int num_cycles;
    int width;
    int dst_x;
    int dst_width;

    if (!intgame_iso_interface_created) {
        return;
    }

    if (dword_64C6D0) {
        snprintf(str, sizeof(str), "%d Hours", datetime_current_hour());

        rect.x = intgame_interface_window_frames[0].x + 650;
        rect.y = intgame_interface_window_frames[0].y + 5;
        rect.width = 50;
        rect.height = 25;

        font_desc.str = str;
        font_desc.width = 0;
        tig_font_measure(&font_desc);

        if (tig_window_fill(dword_64C4F8[0], &rect, tig_color_make(0, 0, 0)) == TIG_OK) {
            tig_window_text_write(dword_64C4F8[0], str, &rect);
        }
        return;
    }

    datetime = sub_45A7C0();
    v1 = (dword_64C47C[1] + dword_64C47C[0]
             + (datetime_seconds_since_reference_date(&datetime) + 73800) % 86400 * (dword_64C47C[1] + dword_64C47C[0]) / 86400
             - intgame_clock_frame.width / 2)
        % (dword_64C47C[1] + dword_64C47C[0]);
    if (dword_5C7308 == v1) {
        return;
    }

    dword_5C7308 = v1;
    tig_window_fill(dword_64C4F8[0], &intgame_clock_frame, tig_color_make(0, 0, 0));

    dst_x = intgame_clock_frame.x;
    dst_width = intgame_clock_frame.width;

    cycle = 0;
    num_cycles = 0;
    while (dst_width > 0 && num_cycles < 3) {
        width = dword_64C47C[cycle];
        if (cycle == 0) {
            // 207: "clk_timestrip.art"
            if (tig_art_interface_id_create(207, 0, 0, 0, &(art_blit_info.art_id)) != TIG_OK) {
                tig_debug_printf("intgame_clock_refresh: ERROR: tig_art_interface_id_create failed!\n");
                return;
            }
        } else {
            int v2;
            int idx;

            v2 = ((datetime_seconds_since_reference_date(&datetime) + 43200) / 84600) % 28;

            for (idx = 0; idx < 8; idx++) {
                if (v2 <= dword_5C730C[idx]) {
                    break;
                }
            }

            // 208 - 215: "clk_moon#.art"
            if (tig_art_interface_id_create(208 + idx, 0, 0, 0, &(art_blit_info.art_id)) != TIG_OK) {
                tig_debug_printf("intgame_clock_refresh: ERROR: tig_art_interface_id_create2 failed!\n");
                return;
            }
        }

        if (v1 < width) {
            src_rect.x = v1;
            src_rect.y = 0;
            src_rect.width = SDL_min(width - v1, dst_width);
            src_rect.height = intgame_clock_frame.height;

            dst_rect.x = dst_x;
            dst_rect.y = intgame_clock_frame.y;
            dst_rect.width = src_rect.width;
            dst_rect.height = intgame_clock_frame.height;

            art_blit_info.flags = 0;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(dword_64C4F8[0], &art_blit_info);

            dst_x += src_rect.width;
            dst_width -= src_rect.width;

            v1 = 0;
        } else {
            v1 -= width;
        }

        cycle = 1 - cycle;
        num_cycles++;
    }

    // 216: "timepnts.art"
    if (tig_art_interface_id_create(216, 0, 0, 0, &(art_blit_info.art_id)) != TIG_OK) {
        tig_debug_printf("intgame_clock_refresh: ERROR: tig_art_interface_id_create3 failed!\n");
        return;
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = 100;
    src_rect.height = 100;

    dst_rect.x = 708;
    dst_rect.y = 6;
    dst_rect.width = 100;
    dst_rect.height = 100;

    art_blit_info.flags = 0;
    art_blit_info.src_rect = &src_rect;
    art_blit_info.dst_rect = &dst_rect;
    tig_window_blit_art(dword_64C4F8[0], &art_blit_info);
}

// 0x5526F0
bool intgame_clock_process_callback(TimeEvent* timeevent)
{
    DateTime datetime;
    TimeEvent next_timeevent;

    (void)timeevent;

    if (intgame_iso_interface_created) {
        intgame_clock_refresh();
        next_timeevent.type = TIMEEVENT_TYPE_CLOCK;
        sub_45A950(&datetime, 3600000);
        timeevent_clear_one_typed(TIMEEVENT_TYPE_CLOCK);
        timeevent_add_delay(&next_timeevent, &datetime);
    }

    return true;
}

// 0x552740
void sub_552740(int64_t obj, ChareditMode mode)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    if (obj == OBJ_HANDLE_NULL) {
        return;
    }

    charedit_open(obj, mode);
}

// 0x552770
void sub_552770(UiMessage* ui_message)
{
    // 0x64C6EC
    static tig_timestamp_t dword_64C6EC;

    int prev_idx;

    if (ui_message->type >= 6 && ui_message->type <= 12) {
        if (tig_timer_elapsed(dword_64C6EC) > 3000
            && intgame_iso_window_type == ROTWIN_TYPE_MSG) {
            intgame_message_window_clear_internal();
            intgame_message_draw(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
                ui_message,
                true);
        }
    } else {
        tig_timer_now(&dword_64C6EC);

        prev_idx = intgame_message_history_end;
        if (intgame_message_history_size < MAX_MESSAGE_HISTORY_ITEMS) {
            intgame_message_history_end = intgame_message_history_size;
        } else {
            intgame_message_history_end = (intgame_message_history_end + 1) % MAX_MESSAGE_HISTORY_ITEMS;
        }

        if (intgame_message_history_size != 0
            && (ui_message->type == UI_MSG_TYPE_EXCLAMATION
                || ui_message->type == UI_MSG_TYPE_QUESTION)
            && SDL_strcasecmp(ui_message->str, intgame_message_history[prev_idx].str) == 0) {
            intgame_message_history_end = prev_idx;
            intgame_message_history_curr = prev_idx;
            intgame_message_refresh(true);
            return;
        }

        if (intgame_message_history_size < MAX_MESSAGE_HISTORY_ITEMS) {
            intgame_message_history_size += 1;
        }

        intgame_message_history[intgame_message_history_end].type = ui_message->type;
        strncpy(intgame_message_history[intgame_message_history_end].str, ui_message->str, MAX_MESSAGE_HISTORY_STRING_SIZE);
        intgame_message_history[intgame_message_history_end].str[MAX_MESSAGE_HISTORY_STRING_SIZE - 1] = '\0';
        intgame_message_history[intgame_message_history_end].field_8 = ui_message->field_8;
        intgame_message_history[intgame_message_history_end].field_C = ui_message->field_C;
        intgame_message_history_curr = intgame_message_history_end;
        intgame_message_refresh(true);
    }
}

// 0x5528E0
void intgame_message_history_scroll_up(void)
{
    int idx;

    if (intgame_iso_window_type == ROTWIN_TYPE_MSG) {
        idx = (intgame_message_history_curr + MAX_MESSAGE_HISTORY_ITEMS - 1) % MAX_MESSAGE_HISTORY_ITEMS;
        if (idx != intgame_message_history_end
            && (intgame_message_history_end - idx + MAX_MESSAGE_HISTORY_ITEMS) % MAX_MESSAGE_HISTORY_ITEMS < intgame_message_history_size) {
            intgame_message_history_curr = idx;
        }
    }

    intgame_message_refresh(false);
}

// 0x552930
void intgame_message_history_scroll_down(void)
{
    if (intgame_iso_window_type == ROTWIN_TYPE_MSG) {
        if (intgame_message_history_curr != intgame_message_history_end) {
            intgame_message_history_curr = (intgame_message_history_curr + 1) % MAX_MESSAGE_HISTORY_ITEMS;
        }
    }

    intgame_message_refresh(false);
}

// 0x552960
void intgame_message_refresh(bool play_sound)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    iso_interface_window_set(ROTWIN_TYPE_MSG);

    if (intgame_iso_window_type == ROTWIN_TYPE_MSG) {
        if (intgame_message_history_size > 0) {
            intgame_message_window_clear_internal();
            intgame_message_draw(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
                &(intgame_message_history[intgame_message_history_curr]),
                play_sound);
        }
    }
}

// 0x5529C0
void intgame_message_draw(tig_window_handle_t window_handle, UiMessage* ui_message, bool play_sound)
{
    MesFileEntry mes_file_entry1;
    MesFileEntry mes_file_entry2;
    char str[MAX_STRING];

    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
    }

    switch (ui_message->type) {
    case UI_MSG_TYPE_LEVEL:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[ui_message->type]);

        mes_file_entry1.num = 21; // "Level Up"
        mes_get_msg(intgame_mes_file, &mes_file_entry1);
        intgame_message_window_write_text(window_handle,
            mes_file_entry1.str,
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        mes_file_entry1.num = 11; // "Congratulations! You are now level %d."
        mes_get_msg(intgame_mes_file, &mes_file_entry1);
        snprintf(str, sizeof(str), mes_file_entry1.str, ui_message->field_8);
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C70D8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);

        if (ui_message->field_C != -1) {
            mes_file_entry1.num = 12; // "You now have %d character point(s) to spend."
            mes_get_msg(intgame_mes_file, &mes_file_entry1);
            snprintf(str, sizeof(str), mes_file_entry1.str, ui_message->field_C);
            intgame_message_window_write_text(window_handle,
                str,
                &stru_5C70E8,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
        } else {
            intgame_message_window_write_text(window_handle,
                ui_message->str,
                &stru_5C7128,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
        }

        if (play_sound) {
            gsound_play_sfx(SND_INTERFACE_LEVEL_UP, 1);
        }
        break;
    case UI_MSG_TYPE_POISON:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[ui_message->type]);
        intgame_message_window_write_text(window_handle,
            ui_message->str,
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        mes_file_entry1.num = 13; // "You have absorbed %d unit(s) of poison."
        mes_get_msg(intgame_mes_file, &mes_file_entry1);
        snprintf(str, sizeof(str), mes_file_entry1.str, ui_message->field_8);
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C70D8,
            intgame_flare12_red_font,
            MSG_TEXT_HALIGN_LEFT);

        if (play_sound) {
            gsound_play_sfx(SND_INTERFACE_POISONED, 1);
        }
        break;
    case UI_MSG_TYPE_CURSE:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[ui_message->type]);
        intgame_message_window_write_text(window_handle,
            ui_message->str,
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        curse_copy_description(ui_message->field_8, str, sizeof(str));
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);

        if (play_sound) {
            gsound_play_sfx(SND_INTERFACE_CURSED, 1);
        }
        break;
    case UI_MSG_TYPE_BLESS:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[ui_message->type]);
        intgame_message_window_write_text(window_handle,
            ui_message->str,
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        bless_copy_description(ui_message->field_8, str, sizeof(str));
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);

        if (play_sound) {
            gsound_play_sfx(SND_INTERFACE_BLESS, 1);
        }
        break;
    case UI_MSG_TYPE_EXCLAMATION:
        if (play_sound) {
            gsound_play_sfx(SND_INTERFACE_EXCLAMATION, 1);
        }
        // FALLTHROUGH
    case UI_MSG_TYPE_QUESTION:
    case UI_MSG_TYPE_FEEDBACK: {
        size_t pos = 0;
        bool rc;

        intgame_message_window_draw_image(window_handle, intgame_message_icons[ui_message->type]);

        while (ui_message->str[pos] != '\0' && ui_message->str[pos] != '\n') {
            pos++;
        }

        if (ui_message->str[pos] == '\n') {
            ui_message->str[pos] = '\0';
            intgame_message_window_write_text(window_handle,
                ui_message->str,
                &stru_5C70C8,
                intgame_morph15_white_font,
                MSG_TEXT_HALIGN_LEFT);
            rc = intgame_message_window_write_text(window_handle,
                ui_message->str + pos + 1,
                &stru_5C7138,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
            ui_message->str[pos] = '\n';
        } else {
            rc = intgame_message_window_write_text(window_handle, ui_message->str, &stru_5C7108, intgame_flare12_white_font, 1);
        }

        if (!rc) {
            intgame_message_window_clear_internal();
            if (ui_message->str[pos] == '\n') {
                ui_message->str[pos] = '\0';
                intgame_message_window_write_text(window_handle, ui_message->str,
                    &stru_5C7148,
                    intgame_morph15_white_font,
                    MSG_TEXT_HALIGN_LEFT | MSG_TEXT_TRUNCATE);
                intgame_message_window_write_text(window_handle,
                    ui_message->str + pos + 1,
                    &stru_5C7168,
                    intgame_flare12_white_font,
                    MSG_TEXT_HALIGN_LEFT | MSG_TEXT_TRUNCATE);
                ui_message->str[pos] = '\n';
            } else {
                intgame_message_window_write_text(window_handle,
                    ui_message->str,
                    &stru_5C7158,
                    intgame_flare12_white_font,
                    MSG_TEXT_HALIGN_LEFT | MSG_TEXT_TRUNCATE);
            }
        }
        break;
    }
    case UI_MSG_TYPE_SKILL:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[UI_MSG_TYPE_FEEDBACK]);
        intgame_message_window_write_text(window_handle,
            IS_TECH_SKILL(ui_message->field_8)
                ? tech_skill_name(GET_TECH_SKILL(ui_message->field_8))
                : basic_skill_name(GET_BASIC_SKILL(ui_message->field_8)),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);
        intgame_message_window_write_text(window_handle,
            IS_TECH_SKILL(ui_message->field_8)
                ? tech_skill_description(GET_TECH_SKILL(ui_message->field_8))
                : basic_skill_description(GET_BASIC_SKILL(ui_message->field_8)),
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);

        if (ui_message->field_C != 0) {
            mes_file_entry1.num = 28 + (IS_TECH_SKILL(ui_message->field_8) ? tech_skill_stat(GET_TECH_SKILL(ui_message->field_8)) : basic_skill_stat(GET_BASIC_SKILL(ui_message->field_8)));
            mes_get_msg(intgame_mes_file, &mes_file_entry1);
            snprintf(str, sizeof(str),
                "%s: %d",
                mes_file_entry1.str,
                ui_message->field_C);
            intgame_message_window_write_text(window_handle,
                str,
                &stru_5C70C8,
                intgame_flare12_red_font,
                MSG_TEXT_HALIGN_RIGHT);
        }
        break;
    case UI_MSG_TYPE_SPELL: {
        intgame_message_window_draw_image(window_handle, spell_college_large_icon(ui_message->field_8 / 5));
        intgame_message_window_write_text(window_handle,
            spell_name(ui_message->field_8),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        mes_file_entry1.num = 73; // "Bonus to Heal skill"
        mes_get_msg(intgame_mes_file, &mes_file_entry1);
        snprintf(str, sizeof(str),
            "%s: %d",
            mes_file_entry1.str,
            spell_min_willpower(ui_message->field_8));
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C70D8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);

        mes_file_entry1.num = 59; // "Damage"
        mes_get_msg(intgame_mes_file, &mes_file_entry1);

        int cast_cost;
        int maintain_cost;
        int maintain_period;

        cast_cost = spell_cast_cost(ui_message->field_8, ui_message->field_10);
        maintain_cost = spell_maintain_cost(ui_message->field_8, ui_message->field_10, &maintain_period);
        if (maintain_period == 1) {
            mes_file_entry2.num = 74; // "second"
            mes_get_msg(intgame_mes_file, &mes_file_entry2);
            snprintf(str, sizeof(str),
                "%s: %d  (%d / %s)",
                mes_file_entry1.str,
                cast_cost,
                maintain_cost,
                mes_file_entry2.str);
        } else if (maintain_period > 1) {
            mes_file_entry2.num = 75; // "seconds"
            mes_get_msg(intgame_mes_file, &mes_file_entry2);
            snprintf(str, sizeof(str),
                "%s: %d  (%d / %d %s)",
                mes_file_entry1.str,
                cast_cost,
                maintain_cost,
                maintain_period,
                mes_file_entry2.str);
        } else {
            snprintf(str, sizeof(str),
                "%s: %d",
                mes_file_entry1.str,
                cast_cost);
        }
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C70D8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_RIGHT);

        intgame_message_window_write_text(window_handle,
            spell_description(ui_message->field_8),
            &stru_5C7128,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
        break;
    }
    case UI_MSG_TYPE_COLLEGE:
        intgame_message_window_draw_image(window_handle, spell_college_large_icon(ui_message->field_8));
        intgame_message_window_write_text(window_handle,
            spell_college_name(ui_message->field_8),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);
        intgame_message_window_write_text(window_handle,
            spell_college_description(ui_message->field_8),
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
        break;
    case UI_MSG_TYPE_TECH:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[UI_MSG_TYPE_FEEDBACK]);
        intgame_message_window_write_text(window_handle,
            tech_discipline_name_get(ui_message->field_8),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);
        intgame_message_window_write_text(window_handle,
            tech_discipline_description_get(ui_message->field_8),
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
        break;
    case UI_MSG_TYPE_DEGREE:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[UI_MSG_TYPE_FEEDBACK]);
        intgame_message_window_write_text(window_handle,
            tech_degree_name_get(ui_message->field_8 % 8),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);

        mes_file_entry1.num = 17;
        mes_get_msg(intgame_mes_file, &mes_file_entry1);
        snprintf(str, sizeof(str),
            "%s: %d",
            mes_file_entry1.str,
            tech_degree_min_intelligence_get(ui_message->field_8 % 8));
        intgame_message_window_write_text(window_handle,
            str,
            &stru_5C70C8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_RIGHT);

        intgame_message_window_write_text(window_handle,
            tech_degree_description_get(ui_message->field_8 % 8, ui_message->field_8 / 8),
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
        break;
    case UI_MSG_TYPE_STAT: {
        size_t pos = 0;

        intgame_message_window_draw_image(window_handle, intgame_message_icons[UI_MSG_TYPE_FEEDBACK]);

        while (ui_message->str[pos] != '\0' && ui_message->str[pos] != '\n') {
            pos++;
        }

        if (ui_message->str[pos] == '\n') {
            ui_message->str[pos] = '\0';
            intgame_message_window_write_text(window_handle,
                ui_message->str,
                &stru_5C70C8,
                intgame_morph15_white_font,
                MSG_TEXT_HALIGN_LEFT);
            intgame_message_window_write_text(window_handle,
                ui_message->str + pos + 1,
                &stru_5C7138,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
            ui_message->str[pos] = '\n';
        } else {
            intgame_message_window_write_text(window_handle,
                ui_message->str,
                &stru_5C7108,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
        }
        break;
    }
    case UI_MSG_TYPE_SCHEMATIC:
        intgame_message_window_draw_image(window_handle, intgame_message_icons[UI_MSG_TYPE_FEEDBACK]);
        intgame_message_window_write_text(window_handle,
            schematic_ui_product_name(ui_message->field_8),
            &stru_5C70C8,
            intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);
        intgame_message_window_write_text(window_handle,
            ui_message->str,
            &stru_5C7138,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
        break;
    }
}

// 0x553320
bool intgame_dialog_begin(bool (*func)(TigMessage* msg))
{
    intgame_dialog_process_event_func = func;
    tc_show();
    intgame_mode_set(INTGAME_MODE_MAIN);
    intgame_mode_set(INTGAME_MODE_DIALOG);

    return true;
}

// 0x553350
void intgame_dialog_end(void)
{
    intgame_dialog_process_event_func = NULL;
    tc_hide();
    dialog_camera_end(player_get_local_pc_obj());
    intgame_mode_set(INTGAME_MODE_MAIN);
}

// 0x553370
void intgame_dialog_clear(void)
{
    tc_clear(intgame_compact_interface);
}

// 0x553380
void intgame_dialog_set_option(int index, const char* str)
{
    tc_set_option(index, str);
}

// 0x5533A0
int intgame_dialog_get_option(TigMessage* msg)
{
    return tc_handle_message(msg);
}

// TODO: Reuse `iso_interface_window_get`.
//
// 0x5533B0
RotatingWindowType iso_interface_window_get_2(void)
{
    return intgame_iso_window_type;
}

// 0x5533C0
void intgame_spell_maintain_art_set_func(UiButtonInfo* button, int slot, tig_art_id_t art_id, tig_window_handle_t window_handle)
{
    int64_t pc_obj;
    int num_slots;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;

    if (!intgame_iso_interface_created) {
        return;
    }

    if (art_id == TIG_ART_ID_INVALID) {
        pc_obj = player_get_local_pc_obj();
        if (pc_obj != OBJ_HANDLE_NULL) {
            num_slots = stat_level_get(pc_obj, STAT_INTELLIGENCE) / 4;
        } else {
            num_slots = 5;
        }

        if (tig_art_interface_id_create(188 + slot, 0, 0, 0, &art_id) == TIG_OK) {
            tig_button_set_art(button->button_handle, art_id);
            tig_button_hide(button->button_handle);
        }

        if (slot < num_slots) {
            if (tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
                src_rect.x = 0;
                src_rect.y = 0;
                src_rect.width = art_frame_data.width;
                src_rect.height = art_frame_data.height;

                dst_rect.x = 0;
                dst_rect.y = 0;
                dst_rect.width = art_frame_data.width;
                dst_rect.height = art_frame_data.height;

                art_blit_info.flags = 0;
                art_blit_info.art_id = art_id;
                art_blit_info.src_rect = &src_rect;
                art_blit_info.dst_rect = &dst_rect;

                if (window_handle == dword_64C4F8[0]) {
                    dst_rect.x = button->x;
                    dst_rect.y = button->y;
                } else {
                    dst_rect.x = button->x - intgame_maintain_window_rects[slot].x;
                    dst_rect.y = button->y - intgame_maintain_window_rects[slot].y;
                }

                tig_window_blit_art(window_handle, &art_blit_info);
            }
        } else {
            if (tig_art_interface_id_create(628 + slot, 0, 0, 0, &art_id) == TIG_OK
                && tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
                src_rect.x = 0;
                src_rect.y = 0;
                src_rect.width = art_frame_data.width;
                src_rect.height = art_frame_data.height;

                dst_rect.x = button->x - 1;
                dst_rect.y = button->y - 1;
                dst_rect.width = art_frame_data.width;
                dst_rect.height = art_frame_data.height;

                art_blit_info.flags = 0;
                art_blit_info.art_id = art_id;
                art_blit_info.src_rect = &src_rect;
                art_blit_info.dst_rect = &dst_rect;

                tig_window_blit_art(window_handle, &art_blit_info);
            }
        }

        if (window_handle != dword_64C4F8[0]) {
            tig_window_hide(intgame_maintain_fs_windows[slot]);
        }
    } else {
        if (window_handle != dword_64C4F8[0]) {
            tig_window_show(intgame_maintain_fs_windows[slot]);
        }
        tig_button_set_art(button->button_handle, art_id);
        tig_button_show(button->button_handle);
    }
}

// 0x553620
void intgame_spell_maintain_art_set(int slot, tig_art_id_t art_id)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    intgame_spell_maintain_art_set_func(&(intgame_maintain_buttons[slot]),
        slot,
        art_id,
        dword_64C4F8[0]);
    intgame_spell_maintain_art_set_func(&(intgame_maintain_fs_buttons[slot]),
        slot,
        art_id,
        intgame_maintain_fs_windows[slot]);
}

// 0x553670
void intgame_spell_maintain_refresh_func(tig_button_handle_t button_handle, UiButtonInfo* info, int slot, bool active, tig_window_handle_t window_handle)
{
    TigButtonData button_data;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    int current_num;
    bool hidden;
    tig_art_id_t art_id;

    if (tig_button_data(button_handle, &button_data) != TIG_OK) {
        return;
    }

    current_num = tig_art_num_get(button_data.art_id);
    if (active) {
        if (current_num == slot + 188 && tig_button_is_hidden(button_handle, &hidden) == TIG_OK) {
            tig_button_show(button_handle);
            if (tig_art_interface_id_create(slot + 188, 0, 0, 0, &art_id) != TIG_OK) {
                tig_debug_printf("intgame_spell_maintain_refresh_func: ERROR: tig_art_interface_id_create failed!\n");
                return;
            }

            tig_button_set_art(button_handle, art_id);
            if (tig_art_interface_id_create(slot + 628, 0, 0, 0, &art_id) != TIG_OK) {
                return;
            }

            if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
                return;
            }

            if (tig_art_interface_id_create(185, 0, 0, 0, &art_id) != TIG_OK) {
                return;
            }

            src_rect.x = info->x - 1;
            src_rect.y = info->y - 1;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = src_rect.x;
            dst_rect.y = info->y - 1;
            dst_rect.width = art_frame_data.width;
            dst_rect.height = art_frame_data.height;

            art_blit_info.flags = 0;
            art_blit_info.art_id = art_id;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(window_handle, &art_blit_info);
        }
    } else if (current_num != slot + 628) {
        if (current_num == slot + 188) {
            if (tig_art_interface_id_create(slot + 188, 0, 0, 0, &art_id)) {
                tig_button_set_art(button_handle, art_id);
                tig_button_hide(button_handle);
            }

            if (tig_art_interface_id_create(slot + 628, 0, 0, 0, &art_id) != TIG_OK) {
                return;
            }

            if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
                return;
            }

            src_rect.x = info->x - 1;
            src_rect.y = info->y - 1;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.width = art_frame_data.width;
            dst_rect.height = art_frame_data.height;

            art_blit_info.flags = 0;
            art_blit_info.art_id = art_id;
            art_blit_info.src_rect = &dst_rect;
            art_blit_info.dst_rect = &src_rect;
            tig_window_blit_art(window_handle, &art_blit_info);
        } else {
            spell_ui_maintain_click(slot);
        }
    }
}

// 0x553910
void intgame_spell_maintain_refresh(int slot, bool active)
{
    intgame_spell_maintain_refresh_func(intgame_maintain_buttons[slot].button_handle,
        &(intgame_maintain_buttons[slot]),
        slot,
        active,
        dword_64C4F8[0]);
    intgame_spell_maintain_refresh_func(intgame_maintain_fs_buttons[slot].button_handle,
        &(intgame_maintain_fs_buttons[slot]),
        slot,
        active,
        intgame_maintain_fs_windows[slot]);
}

// 0x553960
void intgame_refresh_quantity(void)
{
    roller_ui_draw(intgame_quantity, dword_64C4F8[1], 404, 104, 6, 0);
}

// 0x553990
void intgame_refresh_cursor(void)
{
    bool have_weapon = false;
    int64_t pc_obj;
    tig_art_id_t art_id;
    int art_num;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj != OBJ_HANDLE_NULL) {
        art_id = obj_field_int32_get(pc_obj, OBJ_F_CURRENT_AID);
        if (tig_art_critter_id_weapon_get(art_id) != TIG_ART_WEAPON_TYPE_NO_WEAPON) {
            have_weapon = true;
        }
    }

    if (!hotkey_ui_is_dragging()) {
        // FIXME: Meaningless.
        tig_mouse_cursor_get_art_id();

        art_num = intgame_mode_cursors[intgame_mode_get()];
        if (art_num == -1) {
            if (have_weapon) {
                if (tig_kb_is_key_pressed(SDL_SCANCODE_COMMA)) {
                    art_num = 818; // "cursor-called-head.art"
                } else if (tig_kb_is_key_pressed(SDL_SCANCODE_PERIOD)) {
                    art_num = 819; // "cursor-called-leg.art"
                } else if (tig_kb_is_key_pressed(SDL_SCANCODE_SLASH)) {
                    art_num = 820; // "cursor-called-arm.art"
                } else {
                    art_num = 353; // "battlecur.art"
                }
            } else {
                art_num = 0; // "cursor.art"
            }
        }

        tig_art_interface_id_create(art_num, 0, 0, 0, &art_id);
        tig_mouse_cursor_set_art_id(art_id);

        sub_5736E0();
    }
}

// 0x553A60
void intgame_item_mode_cursor_set(int art_num)
{
    intgame_mode_cursors[INTGAME_MODE_ITEM] = art_num;
}

// 0x553A70
void sub_553A70(TigMessage* msg)
{
    int64_t obj;
    TargetDescriptor td;
    int x;
    int y;

    if (!sub_5517A0(msg)) {
        return;
    }

    obj = object_hover_obj_get();
    if (obj == OBJ_HANDLE_NULL) {
        return;
    }

    intgame_adjust_mouse_for_zoom(msg->data.mouse.x, msg->data.mouse.y, &x, &y);

    if (target_pick_at_screen_xy_ex(x, y, &td, qword_5C7280, intgame_fullscreen)) {
        if (obj != td.obj) {
            sub_57CCF0(player_get_local_pc_obj(), td.obj);
            object_hover_obj_set(td.obj);
        }
    } else {
        if (qword_64C690 != OBJ_HANDLE_NULL || object_hover_obj_get() == OBJ_HANDLE_NULL) {
            if (intgame_iso_window_type != ROTWIN_TYPE_CHAT) {
                if (obj_handle_is_valid(qword_64C690)) {
                    object_hover_obj_set(qword_64C690);
                    object_hover_obj_set(OBJ_HANDLE_NULL);
                    sub_57CCF0(player_get_local_pc_obj(), qword_64C690);
                }
                qword_64C690 = OBJ_HANDLE_NULL;
            }
        } else {
            if (intgame_iso_window_type != ROTWIN_TYPE_CHAT) {
                intgame_message_window_display_str(-1, "");
                compact_ui_message_window_release();
            }
            object_hover_obj_set(OBJ_HANDLE_NULL);
        }
    }
}

// 0x553BE0
void intgame_examine_object(int64_t pc_obj, int64_t target_obj, char* str)
{
    int type;

    if (intgame_iso_window_type != ROTWIN_TYPE_CHAT) {
        if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
            intgame_message_window_display_str(-1, str);
        } else {
            type = obj_field_int32_get(target_obj, OBJ_F_TYPE);
            switch (type) {
            case OBJ_TYPE_WALL:
                break;
            case OBJ_TYPE_PORTAL:
                intgame_examine_portal(pc_obj, target_obj, str);
                break;
            case OBJ_TYPE_CONTAINER:
                intgame_examine_container(pc_obj, target_obj, str);
                break;
            case OBJ_TYPE_SCENERY:
                intgame_examine_scenery(pc_obj, target_obj, str);
                break;
            case OBJ_TYPE_PROJECTILE:
                break;
            case OBJ_TYPE_WEAPON:
            case OBJ_TYPE_AMMO:
            case OBJ_TYPE_ARMOR:
            case OBJ_TYPE_GOLD:
            case OBJ_TYPE_FOOD:
            case OBJ_TYPE_SCROLL:
            case OBJ_TYPE_KEY:
            case OBJ_TYPE_KEY_RING:
            case OBJ_TYPE_WRITTEN:
            case OBJ_TYPE_GENERIC:
                intgame_examine_item(pc_obj, target_obj, str);
                break;
            case OBJ_TYPE_PC:
            case OBJ_TYPE_NPC:
                intgame_examine_critter(pc_obj, target_obj, str);
                break;
            default:
                intgame_message_window_display_str(-1, str);
                break;
            }
        }
    }
}

// 0x553D10
bool intgame_examine_portrait(int64_t pc_obj, int64_t target_obj, int* portrait_ptr)
{
    unsigned int critter_flags;

    *portrait_ptr = 432; // levelupicon.art

    switch (obj_field_int32_get(target_obj, OBJ_F_TYPE)) {
    case OBJ_TYPE_PORTAL:
        *portrait_ptr = tig_art_portal_id_type_get(obj_field_int32_get(target_obj, OBJ_F_CURRENT_AID)) == TIG_ART_PORTAL_TYPE_WINDOW
            ? 786 // iconwindow.art
            : 436; // door_icon.art
        return false;
    case OBJ_TYPE_CONTAINER:
        switch (sub_49B290(target_obj)) {
        case BP_JUNK_PILE:
            *portrait_ptr = 832; // cont_junk.art
            break;
        case BP_SAFE_1:
        case BP_SAFE_2:
            *portrait_ptr = 834; // cont_safe.art
            break;
        case BP_RUBBISH_BIN:
            *portrait_ptr = 835; // cont_trash.art
            break;
        case BP_BODY:
            *portrait_ptr = 833; // cont_body.art
            break;
        case BP_ALTAR_GOOD:
            *portrait_ptr = 837; // cont_altar_good.art
            break;
        case BP_ALTAR_NEUTRAL:
            *portrait_ptr = 838; // cont_altar_neutral.art
            break;
        case BP_ALTAR_EVIL:
            *portrait_ptr = 839; // cont_altar_evil.art
            break;
        case BP_PLANT_CONTAINER:
            *portrait_ptr = 836; // cont_trash.art
            break;
        default:
            *portrait_ptr = 435; // containericon.art
            break;
        }
        return false;
    case OBJ_TYPE_SCENERY:
        *portrait_ptr = 437; // levelupicon.art
        return false;
    case OBJ_TYPE_WEAPON:
    case OBJ_TYPE_AMMO:
    case OBJ_TYPE_ARMOR:
    case OBJ_TYPE_GOLD:
    case OBJ_TYPE_FOOD:
    case OBJ_TYPE_SCROLL:
    case OBJ_TYPE_KEY:
    case OBJ_TYPE_KEY_RING:
    case OBJ_TYPE_WRITTEN:
    case OBJ_TYPE_GENERIC:
        *portrait_ptr = intgame_item_icon_get(target_obj);
        return false;
    case OBJ_TYPE_PC:
        *portrait_ptr = portrait_get(target_obj);
        return true;
    case OBJ_TYPE_NPC:
        if (critter_pc_leader_get(target_obj) == pc_obj) {
            int portrait = portrait_get(target_obj);
            if (portrait != 0) {
                *portrait_ptr = portrait;
                return true;
            }
        }

        critter_flags = obj_field_int32_get(target_obj, OBJ_F_CRITTER_FLAGS);
        if ((critter_flags & OCF_UNDEAD) != 0) {
            *portrait_ptr = 384; // undead.art
            return false;
        }

        if ((critter_flags & OCF_MONSTER) != 0) {
            *portrait_ptr = 383; // monstericon.art
            return false;
        }

        if ((critter_flags & OCF_ANIMAL) != 0) {
            *portrait_ptr = 385; // animalicon.art
            return false;
        }

        if ((critter_flags & OCF_MECHANICAL) != 0) {
            *portrait_ptr = 434; // generaltechicon.art
            return false;
        }

        // Generic race-specific icon.
        *portrait_ptr = intgame_race_icons[stat_level_get(target_obj, STAT_RACE)];
        return false;
    default:
        return false;
    }
}

// 0x553F70
void intgame_examine_critter(int64_t pc_obj, int64_t critter_obj, char* str)
{
    int obj_type;
    bool is_detecting_alignment;
    int alignment;
    MesFileEntry mes_file_entry;
    char buffer[80];
    int64_t leader_obj;

    obj_type = obj_field_int32_get(critter_obj, OBJ_F_TYPE);
    intgame_message_window_clear_internal();

    leader_obj = critter_pc_leader_get(critter_obj);

    is_detecting_alignment = (obj_field_int32_get(pc_obj, OBJ_F_SPELL_FLAGS) & OSF_DETECTING_ALIGNMENT) != 0;
    if (is_detecting_alignment) {
        alignment = stat_level_get(critter_obj, STAT_ALIGNMENT);
    }

    if (pc_obj != critter_obj
        && combat_critter_is_combat_mode_active(pc_obj)
        && !critter_is_dead(critter_obj)) {
        sub_554830(pc_obj, critter_obj);
    } else {
        if (is_detecting_alignment) {
            int step = (stat_level_max(critter_obj, STAT_ALIGNMENT) - stat_level_min(critter_obj, STAT_ALIGNMENT)) / 6;
            int alignment_type = (alignment - stat_level_min(critter_obj, STAT_ALIGNMENT)) / step;
            if (alignment_type > 5) {
                alignment_type = 5;
            }
            intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, intgame_alignment_icons[alignment_type]);
        } else {
            int portrait;

            if (intgame_examine_portrait(pc_obj, critter_obj, &portrait)) {
                intgame_draw_portrait(critter_obj, portrait, intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 217, 69);
            } else {
                intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, portrait);
            }
        }
    }

    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70C8,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    if (critter_is_dead(critter_obj)) {
        mes_file_entry.num = 16; // "Dead"
        mes_get_msg(intgame_mes_file, &mes_file_entry);
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            mes_file_entry.str,
            &stru_5C70D8,
            intgame_flare12_red_font,
            MSG_TEXT_HALIGN_LEFT);
        return;
    }

    if (obj_type == OBJ_TYPE_NPC) {
        if (critter_is_concealed(pc_obj)) {
            mes_file_entry.num = 37; // "Prowling state"
            mes_get_msg(intgame_mes_file, &mes_file_entry);

            int awareness_dist;
            int see_extra_dist = ai_can_see(critter_obj, pc_obj);
            if (see_extra_dist != 0) {
                awareness_dist = ai_can_hear(critter_obj, pc_obj, LOUDNESS_SILENT);
                if (awareness_dist >= see_extra_dist) {
                    awareness_dist = see_extra_dist;
                }
            } else {
                awareness_dist = 0;
            }

            if (critter_is_sleeping(critter_obj) && awareness_dist == 0) {
                awareness_dist = 1;
            }

            MesFileEntry suffix;
            switch (awareness_dist) {
            case 0:
                suffix.num = 42; // "Aware!!!"
                break;
            case 1:
                suffix.num = 41; // "Perilous!"
                break;
            case 2:
                suffix.num = 40; // "Dangerous"
                break;
            case 3:
                suffix.num = 39; // "Risky"
                break;
            default:
                suffix.num = 38; // "Safe"
                break;
            }

            mes_get_msg(intgame_mes_file, &suffix);
            snprintf(buffer, sizeof(buffer),
                "%s: %s",
                mes_file_entry.str,
                suffix.str);
        } else {
            if (is_detecting_alignment) {
                mes_file_entry.num = 36; // "Alignment"
                mes_get_msg(intgame_mes_file, &mes_file_entry);
                snprintf(buffer, sizeof(buffer),
                    "%s: %d",
                    mes_file_entry.str,
                    alignment / 10);
            } else {
                int reaction_value = reaction_get(critter_obj, pc_obj);
                int reaction_level = reaction_translate(reaction_value);
                const char* reaction_name = reaction_get_name(reaction_level);

                mes_file_entry.num = 1; // "Reaction"
                mes_get_msg(intgame_mes_file, &mes_file_entry);

                snprintf(buffer, sizeof(buffer),
                    "%s: %d (%s)",
                    mes_file_entry.str,
                    reaction_value,
                    reaction_name);
            }
        }

        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70D8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
    }

    mes_file_entry.num = 0; // "Level"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    snprintf(buffer, sizeof(buffer),
        "%s: %d",
        mes_file_entry.str,
        stat_level_get(critter_obj, STAT_LEVEL));
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70D8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_RIGHT);

    int cur_hp = object_hp_current(critter_obj);
    int max_hp = object_hp_max(critter_obj);
    int hp_ratio = 100 * cur_hp / max_hp;
    if (stat_level_get(critter_obj, STAT_POISON_LEVEL) > 0) {
        sub_554640(665, 666, &stru_5C70E8, hp_ratio);
    } else {
        sub_554640(463, 464, &stru_5C70E8, hp_ratio);
    }

    if (pc_obj == critter_obj || leader_obj == pc_obj) {
        snprintf(buffer, sizeof(buffer), "%d/%d", cur_hp, max_hp);
    } else {
        snprintf(buffer, sizeof(buffer), "%d%%", hp_ratio);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70E8,
        intgame_flare12_red_font,
        MSG_TEXT_HALIGN_RIGHT);

    int cur_fatigue = critter_fatigue_current(critter_obj);
    int max_fatigue = critter_fatigue_max(critter_obj);
    int fatigue_ratio = 100 * cur_fatigue / max_fatigue;
    sub_554640(465, 466, &stru_5C70F8, fatigue_ratio);

    if (pc_obj == critter_obj || leader_obj == pc_obj) {
        snprintf(buffer, sizeof(buffer), "%d/%d", cur_fatigue, max_fatigue);
    } else {
        snprintf(buffer, sizeof(buffer), "%d%%", fatigue_ratio);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70F8,
        intgame_flare12_blue_font,
        MSG_TEXT_HALIGN_RIGHT);
}

// 0x554560
void intgame_message_window_draw_image(tig_window_handle_t window_handle, int num)
{
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo blit_info;
    TigRect src_rect;
    TigRect dst_rect;

    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
    }

    tig_art_interface_id_create(num, 0, 0, 0, &art_id);
    tig_art_frame_data(art_id, &art_frame_data);

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = art_frame_data.width;
    src_rect.height = art_frame_data.height;

    dst_rect.x = 217;
    dst_rect.y = 69;
    dst_rect.width = art_frame_data.width;
    dst_rect.height = art_frame_data.height;

    if (intgame_is_compact_interface()) {
        dst_rect.x -= 210;
        dst_rect.y -= 59;
    }

    blit_info.art_id = art_id;
    blit_info.flags = 0;
    blit_info.src_rect = &src_rect;
    blit_info.dst_rect = &dst_rect;
    tig_window_blit_art(window_handle, &blit_info);
}

// 0x554640
void sub_554640(int a1, int a2, TigRect* rect, int value)
{
    tig_window_handle_t window_handle;
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;

    window_handle = intgame_rotwin_text_frame[intgame_iso_window_type].window_handle;
    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
    }

    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    if (value != 0) {
        tig_art_interface_id_create(a1, 0, 0, 0, &art_id);
        tig_art_frame_data(art_id, &art_frame_data);

        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = value * art_frame_data.width / 100;
        if (src_rect.width < art_frame_data.width) {
            src_rect.width++;
        }
        src_rect.height = art_frame_data.height;

        dst_rect.x = rect->x;
        dst_rect.y = rect->y;
        dst_rect.width = src_rect.width;
        dst_rect.height = art_frame_data.height;

        if (intgame_is_compact_interface()) {
            dst_rect.x -= 210;
            dst_rect.y -= 59;
        }

        art_blit_info.flags = 0;
        art_blit_info.art_id = art_id;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &dst_rect;

        tig_window_blit_art(window_handle, &art_blit_info);
    }

    if (value != 100) {
        tig_art_interface_id_create(a2, 0, 0, 0, &art_id);
        tig_art_frame_data(art_id, &art_frame_data);

        src_rect.x = art_frame_data.width - art_frame_data.width * (100 - value) / 100;
        src_rect.y = 0;
        src_rect.height = art_frame_data.height;
        src_rect.width = art_frame_data.width * (100 - value) / 100;

        dst_rect.x = src_rect.x + rect->x;
        dst_rect.y = rect->y;
        dst_rect.width = src_rect.width;
        dst_rect.height = art_frame_data.height;

        if (intgame_is_compact_interface()) {
            dst_rect.x -= 210;
            dst_rect.y -= 59;
        }

        art_blit_info.flags = 0;
        art_blit_info.art_id = art_id;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &dst_rect;

        tig_window_blit_art(window_handle, &art_blit_info);
    }
}

// 0x554830
void sub_554830(int64_t a1, int64_t a2)
{
    tig_window_handle_t window_handle;
    int64_t weapon_obj;
    int skill;
    int effectiveness;
    SkillInvocation skill_invocation;
    int v3;
    TigRect rect;
    char str[20];
    int penalty;
    int slot;
    MesFileEntry mes_file_entry;

    window_handle = intgame_rotwin_text_frame[intgame_iso_window_type].window_handle;
    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
    }

    sub_554B00(window_handle, 582, 207, 57);

    weapon_obj = item_wield_get(a1, ITEM_INV_LOC_WEAPON);
    skill = item_weapon_skill(weapon_obj);
    if (IS_TECH_SKILL(skill)) {
        effectiveness = tech_skill_effectiveness(a1, GET_TECH_SKILL(skill), a2);
    } else {
        effectiveness = basic_skill_effectiveness(a1, GET_BASIC_SKILL(skill), a2);
    }

    skill_invocation_init(&skill_invocation);
    sub_4440E0(a1, &(skill_invocation.source));
    sub_4440E0(a2, &(skill_invocation.target));
    sub_4440E0(weapon_obj, &(skill_invocation.item));
    skill_invocation.skill = skill;

    effectiveness -= skill_invocation_difficulty(&skill_invocation);

    if (weapon_obj != OBJ_HANDLE_NULL
        && skill != SKILL_THROWING) {
        v3 = sub_461620(weapon_obj, a1, a2);
        if (v3 > 0) {
            if (v3 > 20) {
                skill_invocation.flags |= SKILL_INVOCATION_MAGIC_TECH_PENALTY;
            }
            effectiveness -= effectiveness * v3 / 100;
        }
    }

    if (effectiveness < 0 || (skill_invocation.flags & (SKILL_INVOCATION_BLOCKED_SHOT | SKILL_INVOCATION_PENALTY_RANGE)) != 0) {
        effectiveness = 0;
    }

    if ((skill_invocation.flags & SKILL_INVOCATION_PENALTY_MASK) != 0) {
        rect.x = 215;
        rect.y = 66;
        rect.width = 64;
        rect.height = 64;

        snprintf(str, sizeof(str), "%d%%", effectiveness);
        intgame_message_window_write_text(window_handle,
            str,
            &rect,
            intgame_flare14_white_font,
            MSG_TEXT_HALIGN_CENTER);

        slot = 0;
        for (penalty = 0; penalty < INTGAME_PENALTY_COUNT; penalty++) {
            if (slot >= INTGAME_PENALTY_SLOTS) {
                break;
            }

            if ((intgame_penalty_flags[penalty] & skill_invocation.flags) != 0) {
                sub_554B00(window_handle,
                    intgame_penalty_icons[penalty],
                    intgame_penalty_slot_x[slot],
                    intgame_penalty_slot_y[slot]);
                slot++;
            }
        }
    } else {
        mes_file_entry.num = 20;
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        rect.x = 215;
        rect.y = 85;
        rect.width = 64;
        rect.height = 64;

        intgame_message_window_write_text(window_handle,
            mes_file_entry.str,
            &rect,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_CENTER);

        snprintf(str, sizeof(str), "%d%%", effectiveness);
        rect.y += 18;
        intgame_message_window_write_text(window_handle,
            str,
            &rect,
            intgame_flare14_white_font,
            MSG_TEXT_HALIGN_CENTER);
    }
}

// 0x554B00
void sub_554B00(tig_window_handle_t window_handle, int art_num, int x, int y)
{
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo blit_info;
    TigRect src_rect;
    TigRect dst_rect;

    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
        x -= 210;
        y -= 59;
    }

    tig_art_interface_id_create(art_num, 0, 0, 0, &art_id);
    tig_art_frame_data(art_id, &art_frame_data);

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = art_frame_data.width;
    src_rect.height = art_frame_data.height;

    dst_rect.x = x;
    dst_rect.y = y;
    dst_rect.width = art_frame_data.width;
    dst_rect.height = art_frame_data.height;

    blit_info.art_id = art_id;
    blit_info.flags = 0;
    blit_info.src_rect = &src_rect;
    blit_info.dst_rect = &dst_rect;
    tig_window_blit_art(window_handle, &blit_info);

    compact_ui_message_window_box();
}

// 0x554BE0
tig_art_id_t sub_554BE0(int64_t obj)
{
    tig_art_id_t art_id = TIG_ART_ID_INVALID;
    int art_num;

    if (obj != OBJ_HANDLE_NULL) {
        art_num = intgame_item_icon_get(obj);
        tig_art_interface_id_create(art_num, 0, 0, 0, &art_id);
    }

    return art_id;
}

// 0x554C20
int intgame_item_icon_get(int64_t item_obj)
{
    int obj_type;
    int complexity;
    int num;
    tig_art_id_t art_id;
    int armor_coverage;

    obj_type = obj_field_int32_get(item_obj, OBJ_F_TYPE);
    complexity = obj_field_int32_get(item_obj, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY);

    if (item_is_identified(item_obj)
        && (obj_field_int32_get(item_obj, OBJ_F_ITEM_FLAGS) & OIF_HEXED) != 0) {
        num = 440;

        switch (obj_type) {
        case OBJ_TYPE_WEAPON:
            art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_USE_AID_FRAGMENT);
            switch (tig_art_item_id_subtype_get(art_id)) {
            case TIG_ART_WEAPON_TYPE_BOW:
                num = 792;
                break;
            case TIG_ART_WEAPON_TYPE_DAGGER:
            case TIG_ART_WEAPON_TYPE_SWORD:
            case TIG_ART_WEAPON_TYPE_AXE:
            case TIG_ART_WEAPON_TYPE_MACE:
            case TIG_ART_WEAPON_TYPE_TWO_HANDED_SWORD:
            case TIG_ART_WEAPON_TYPE_STAFF:
                num = 791;
                break;
            }
            break;
        case OBJ_TYPE_ARMOR:
            art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_INV_AID);
            armor_coverage = tig_art_item_id_armor_coverage_get(art_id);
            switch (armor_coverage) {
            case TIG_ART_ARMOR_COVERAGE_TORSO:
                art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_USE_AID_FRAGMENT);
                switch (tig_art_item_id_subtype_get(art_id)) {
                case TIG_ART_ARMOR_TYPE_VILLAGER:
                case TIG_ART_ARMOR_TYPE_CITY_DWELLER:
                    num = 794;
                    break;
                case TIG_ART_ARMOR_TYPE_ROBE:
                    num = 797;
                    break;
                default:
                    num = 793;
                    break;
                }
                break;
            case TIG_ART_ARMOR_COVERAGE_RING:
                num = 796;
                break;
            case TIG_ART_ARMOR_COVERAGE_MEDALLION:
                num = 795;
                break;
            default:
                num = 793;
                break;
            }
            break;
        }
        return num;
    }

    switch (obj_type) {
    case OBJ_TYPE_WEAPON:
        art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_USE_AID_FRAGMENT);
        num = intgame_weapon_icons[tig_art_item_id_subtype_get(art_id)];
        if (complexity > 0) {
            num += 1;
        } else if (complexity < 0) {
            num += 2;
        }
        break;
    case OBJ_TYPE_AMMO:
        num = intgame_ammo_icons[obj_field_int32_get(item_obj, OBJ_F_AMMO_TYPE)];
        break;
    case OBJ_TYPE_ARMOR:
        art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_INV_AID);
        armor_coverage = tig_art_item_id_armor_coverage_get(art_id);
        switch (armor_coverage) {
        case TIG_ART_ARMOR_COVERAGE_TORSO:
            art_id = obj_field_int32_get(item_obj, OBJ_F_ITEM_USE_AID_FRAGMENT);
            num = intgame_armor_type_icons[tig_art_item_id_subtype_get(art_id)];
            break;
        default:
            num = intgame_armor_coverage_icons[armor_coverage];
            break;
        }
        if (complexity > 0) {
            num += 1;
        } else if (complexity < 0) {
            num += 2;
        }
        break;
    case OBJ_TYPE_GOLD:
        num = 417;
        break;
    case OBJ_TYPE_FOOD:
        num = 423;
        if (complexity > 0) {
            num = 418;
        } else if (complexity < 0) {
            switch (obj_field_int32_get(item_obj, OBJ_F_ITEM_DISCIPLINE)) {
            case TECH_HERBOLOGY:
                num = 420;
                break;
            case TECH_CHEMISTRY:
                num = 421;
                break;
            case TECH_THERAPEUTICS:
                num = 419;
                break;
            }
        }
        break;
    case OBJ_TYPE_SCROLL:
        num = 425;
        break;
    case OBJ_TYPE_KEY:
        num = 426;
        break;
    case OBJ_TYPE_KEY_RING:
        num = 427;
        break;
    case OBJ_TYPE_WRITTEN:
        num = intgame_written_icons[obj_field_int32_get(item_obj, OBJ_F_WRITTEN_SUBTYPE)];
        break;
    case OBJ_TYPE_GENERIC:
        num = 432;
        if (complexity > 0) {
            num = 433;
        } else if (complexity < 0) {
            num = 434;
        }
        break;
    }

    return num;
}

// 0x554F10
void intgame_examine_item(int64_t pc_obj, int64_t item_obj, char* str)
{
    int obj_type;
    int64_t parent_obj;
    bool is_identified;
    int complexity;
    MesFileEntry mes_file_entry;
    MesFileEntry suffix;
    char buffer[MAX_STRING];
    int quantity_fld;
    int value;

    obj_type = obj_field_int32_get(item_obj, OBJ_F_TYPE);

    intgame_message_window_clear_internal();

    if (item_parent(item_obj, &parent_obj)
        && parent_obj != OBJ_HANDLE_NULL
        && IS_WEAR_INV_LOC(item_inventory_location_get(item_obj))) {
        pc_obj = parent_obj;
    }

    intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        intgame_item_icon_get(item_obj));

    is_identified = item_is_identified(item_obj);
    complexity = obj_field_int32_get(item_obj, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY);

    if ((obj_field_int32_get(item_obj, OBJ_F_ITEM_FLAGS) & OIF_HEXED) != 0
        && is_identified) {
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            str,
            &stru_5C70C8,
            intgame_morph15_orange_font,
            MSG_TEXT_HALIGN_LEFT);
    } else {
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            str,
            &stru_5C70C8,
            complexity > 0 ? intgame_morph15_blue_font : intgame_morph15_white_font,
            MSG_TEXT_HALIGN_LEFT);
    }

    if (obj_type == OBJ_TYPE_WEAPON) {
        value = item_weapon_magic_speed(item_obj, pc_obj);

        mes_file_entry.num = 8; // "Speed"
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        snprintf(buffer, sizeof(buffer),
            "%s: %d",
            mes_file_entry.str,
            value);
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70C8,
            intgame_flare12_red_font,
            MSG_TEXT_HALIGN_RIGHT);
    }

    switch (obj_type) {
    case OBJ_TYPE_WEAPON:
        mes_file_entry.num = obj_field_int32_get(item_obj, OBJ_F_ITEM_DESCRIPTION_EFFECTS);
        mes_file_entry.str = item_effect_get(mes_file_entry.num);
        if (mes_file_entry.str != NULL
            && *mes_file_entry.str != '\0'
            && (complexity <= 0 || is_identified)) {
            strcpy(buffer, mes_file_entry.str);
        } else {
            format_weapon_stats(item_obj, buffer, sizeof(buffer));
        }
        break;
    case OBJ_TYPE_AMMO:
    case OBJ_TYPE_GOLD:
        sub_462410(item_obj, &quantity_fld);
        value = obj_field_int32_get(item_obj, quantity_fld);

        mes_file_entry.num = 6; // "Quantity"
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        snprintf(buffer, sizeof(buffer),
            "%s: %d",
            mes_file_entry.str,
            value);
        break;
    case OBJ_TYPE_ARMOR:
        mes_file_entry.num = obj_field_int32_get(item_obj, OBJ_F_ITEM_DESCRIPTION_EFFECTS);
        mes_file_entry.str = item_effect_get(mes_file_entry.num);
        if (mes_file_entry.str != NULL
            && *mes_file_entry.str != '\0'
            && (complexity <= 0 || is_identified)) {
            strcpy(buffer, mes_file_entry.str);
        } else {
            format_armor_stats(item_obj, buffer, sizeof(buffer));
        }
        break;
    case OBJ_TYPE_FOOD:
    case OBJ_TYPE_SCROLL:
    case OBJ_TYPE_WRITTEN:
    case OBJ_TYPE_GENERIC:
        buffer[0] = '\0';
        if (complexity <= 0 || is_identified) {
            mes_file_entry.num = obj_field_int32_get(item_obj, OBJ_F_ITEM_DESCRIPTION_EFFECTS);
            mes_file_entry.str = item_effect_get(mes_file_entry.num);
            if (mes_file_entry.str != NULL) {
                strcpy(buffer, mes_file_entry.str);
            }
        }
        if (obj_type == OBJ_TYPE_GENERIC) {
            unsigned int flags = obj_field_int32_get(item_obj, OBJ_F_GENERIC_FLAGS);
            if ((flags & OGF_IS_LOCKPICK) != 0) {
                mes_file_entry.num = 71; // "Bonus to Pick Locks skill"
            } else if ((flags & OGF_IS_HEALING_ITEM) != 0) {
                mes_file_entry.num = 72; // "Bonus to Heal skill"
            } else {
                mes_file_entry.num = -1;
            }

            if (mes_file_entry.num != -1) {
                mes_get_msg(intgame_mes_file, &mes_file_entry);
                snprintf(buffer, sizeof(buffer),
                    "%s: %+d%%",
                    mes_file_entry.str,
                    obj_field_int32_get(item_obj, OBJ_F_GENERIC_USAGE_BONUS));
            }
        }
        break;
    case OBJ_TYPE_KEY:
        buffer[0] = '\0';
        break;
    case OBJ_TYPE_KEY_RING:
        value = item_get_keys(item_obj, NULL);

        mes_file_entry.num = 7; // "Keys"
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        snprintf(buffer, sizeof(buffer),
            "%s: %d",
            mes_file_entry.str,
            value);
        break;
    }

    if (buffer[0] != '\0' || !intgame_is_compact_interface()) {
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70D8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_LEFT);
    }

    if (is_identified
        && (obj_type == OBJ_TYPE_WEAPON
            || obj_type == OBJ_TYPE_ARMOR
            || obj_type == OBJ_TYPE_SCROLL)) {
        if (complexity > 0) {
            value = item_effective_power(item_obj, pc_obj);

            mes_file_entry.num = 2; // "Magic power available"
            mes_get_msg(intgame_mes_file, &mes_file_entry);

            snprintf(buffer, sizeof(buffer),
                "%s: %d%%",
                mes_file_entry.str,
                100 * value / complexity);
            intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
                buffer,
                &stru_5C70E8,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
        } else if (complexity < 0) {
            value = item_aptitude_crit_failure_chance(item_obj, pc_obj);

            mes_file_entry.num = 3; // "Aptitude adj to chance of critical failure"
            mes_get_msg(intgame_mes_file, &mes_file_entry);

            snprintf(buffer, sizeof(buffer),
                "%s: %+d%%",
                mes_file_entry.str,
                value);
            intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
                buffer,
                &stru_5C70E8,
                intgame_flare12_white_font,
                MSG_TEXT_HALIGN_LEFT);
        }
    }

    buffer[0] = '\0';
    value = obj_field_int32_get(item_obj, OBJ_F_ITEM_SPELL_MANA_STORE);

    if (complexity > 0 && value != 0) {
        mes_file_entry.num = 19; // "Uses"
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        if (is_identified) {
            if (value > 0) {
                snprintf(buffer, sizeof(buffer), "%s: +", mes_file_entry.str);
            } else {
                snprintf(buffer, sizeof(buffer), "%s: %d", mes_file_entry.str, value);
            }
        } else {
            snprintf(buffer, sizeof(buffer), "%s: ??", mes_file_entry.str);
        }
    } else {
        switch (obj_type) {
        case OBJ_TYPE_GENERIC:
            if ((obj_field_int32_get(item_obj, OBJ_F_GENERIC_FLAGS) & OGF_IS_HEALING_ITEM) != 0) {
                mes_file_entry.num = 19; // "Uses"
                mes_get_msg(intgame_mes_file, &mes_file_entry);

                snprintf(buffer, sizeof(buffer),
                    "%s: %d",
                    mes_file_entry.str,
                    obj_field_int32_get(item_obj, OBJ_F_GENERIC_USAGE_BONUS));
            }
            break;
        case OBJ_TYPE_WEAPON: {
            int ammo_type = item_weapon_ammo_type(item_obj);
            if (ammo_type != 10000) {
                int consumption = obj_field_int32_get(item_obj, OBJ_F_WEAPON_AMMO_CONSUMPTION);
                if (consumption > 0) {
                    snprintf(buffer, sizeof(buffer),
                        "%s: %d",
                        ammunition_type_get_name(ammo_type),
                        consumption);
                }
            }
            break;
        }
        }
    }

    if (buffer[0] != '\0' || !intgame_is_compact_interface()) {
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70E8,
            intgame_flare12_white_font,
            MSG_TEXT_HALIGN_RIGHT);
    }

    value = item_weight(item_obj, pc_obj);
    mes_file_entry.num = 4; // "Weight"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    suffix.num = 5; // "stone"
    mes_get_msg(intgame_mes_file, &suffix);
    snprintf(buffer, sizeof(buffer),
        "%s: %d %s",
        mes_file_entry.str,
        value,
        suffix.str);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70F8,
        intgame_flare12_red_font,
        MSG_TEXT_HALIGN_LEFT);

    if (tig_art_item_id_destroyed_get(obj_field_int32_get(item_obj, OBJ_F_CURRENT_AID)) == 0) {
        if (obj_type == OBJ_TYPE_WEAPON
            || obj_type == OBJ_TYPE_ARMOR
            || (obj_type == OBJ_TYPE_GENERIC
                && (obj_field_int32_get(item_obj, OBJ_F_GENERIC_FLAGS) & 0x20) != 0)) {
            snprintf(buffer, sizeof(buffer),
                "%d/%d",
                object_hp_current(item_obj),
                object_hp_max(item_obj));
            intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
                buffer,
                &stru_5C70F8,
                intgame_flare12_red_font,
                MSG_TEXT_HALIGN_RIGHT);
        }
    }
}

// 0x555780
void append_stat(char* buffer, size_t maxlen, int num, int min, int max, int adj, bool is_modifier)
{
    MesFileEntry mes_file_entry;
    char tmp[80];

    if (min == 0 && max == 0 && adj == 0) {
        return;
    }

    mes_file_entry.num = num;
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    SDL_strlcat(buffer, mes_file_entry.str, maxlen);
    SDL_strlcat(buffer, ":", maxlen);

    if (max != 0) {
        snprintf(tmp, sizeof(tmp), "%d-%d", min, max);
        strcat(buffer, tmp);
    } else if (min != 0) {
        if (is_modifier) {
            snprintf(tmp, sizeof(tmp), "%+d", min);
        } else {
            snprintf(tmp, sizeof(tmp), "%d", min);
        }
        SDL_strlcat(buffer, tmp, maxlen);
    }

    if (adj != 0) {
        snprintf(tmp, sizeof(tmp), "(%+d)", adj);
        SDL_strlcat(buffer, tmp, maxlen);
    }

    SDL_strlcat(buffer, "  ", maxlen);
}

// 0x555910
void format_weapon_stats(int64_t weapon_obj, char* buffer, size_t maxlen)
{
    bool identified;
    int min;
    int max;
    int adj;

    identified = obj_field_int32_get(weapon_obj, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY) > 0
        && item_is_identified(weapon_obj);
    buffer[0] = '\0';

    // D
    min = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_LOWER_IDX, 0);
    max = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_UPPER_IDX, 0);
    if (identified) {
        adj = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_DAMAGE_ADJ_IDX, 0);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 43, min, max, adj, false);

    // FT
    min = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_LOWER_IDX, 4);
    max = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_UPPER_IDX, 4);
    if (identified) {
        adj = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_DAMAGE_ADJ_IDX, 4);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 44, min, max, adj, false);

    // TH
    min = obj_field_int32_get(weapon_obj, OBJ_F_WEAPON_BONUS_TO_HIT);
    if (identified) {
        adj = obj_field_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_HIT_ADJ);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 45, min, 0, adj, true);

    // RNG
    min = obj_field_int32_get(weapon_obj, OBJ_F_WEAPON_RANGE);
    if (min == 1) {
        min = 0;
    }
    if (identified) {
        adj = obj_field_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_RANGE_ADJ);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 46, min, 0, adj, false);

    // PD
    min = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_LOWER_IDX, 1);
    max = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_UPPER_IDX, 1);
    if (identified) {
        adj = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_DAMAGE_ADJ_IDX, 1);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 47, min, max, adj, false);

    // FD
    min = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_LOWER_IDX, 3);
    max = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_UPPER_IDX, 3);
    if (identified) {
        adj = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_DAMAGE_ADJ_IDX, 3);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 48, min, max, adj, false);

    // ED
    min = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_LOWER_IDX, 2);
    max = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_DAMAGE_UPPER_IDX, 2);
    if (identified) {
        adj = obj_arrayfield_int32_get(weapon_obj, OBJ_F_WEAPON_MAGIC_DAMAGE_ADJ_IDX, 2);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 49, min, max, adj, false);
}

// 0x555B50
void format_armor_stats(int64_t armor_obj, char* buffer, size_t maxlen)
{
    bool identified;
    int value;
    int adj;

    identified = obj_field_int32_get(armor_obj, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY) > 0
        && item_is_identified(armor_obj);
    buffer[0] = '\0';

    // AC
    value = obj_field_int32_get(armor_obj, OBJ_F_ARMOR_AC_ADJ);
    if (identified) {
        adj = obj_field_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_AC_ADJ);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 50, value, 0, adj, false);

    // DR
    value = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_NORMAL);
    if (identified) {
        adj = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_NORMAL);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 51, value, 0, adj, true);

    // PR
    value = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_POISON);
    if (identified) {
        adj = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_POISON);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 52, value, 0, adj, true);

    // FR
    value = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_FIRE);
    if (identified) {
        adj = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_FIRE);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 53, value, 0, adj, true);

    // ER
    value = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_ELECTRICAL);
    if (identified) {
        adj = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_ELECTRICAL);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 54, value, 0, adj, true);

    // MR
    value = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_MAGIC);
    if (identified) {
        adj = obj_arrayfield_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_RESISTANCE_ADJ_IDX, RESISTANCE_TYPE_MAGIC);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 55, value, 0, adj, true);

    // NP
    value = obj_field_int32_get(armor_obj, OBJ_F_ARMOR_SILENT_MOVE_ADJ);
    if (identified) {
        adj = obj_field_int32_get(armor_obj, OBJ_F_ARMOR_MAGIC_SILENT_MOVE_ADJ);
    } else {
        adj = 0;
    }
    append_stat(buffer, maxlen, 56, value, 0, adj, true);

    // D
    if (item_armor_coverage(armor_obj) == TIG_ART_ARMOR_COVERAGE_GAUNTLETS) {
        value = obj_field_int32_get(armor_obj, OBJ_F_ARMOR_UNARMED_BONUS_DAMAGE);
    } else {
        value = 0;
    }
    append_stat(buffer, maxlen, 43, value, 0, 0, true);
}

// 0x555D80
void intgame_examine_scenery(int64_t pc_obj, int64_t scenery_obj, char* str)
{
    int portrait;
    char buffer[2000];

    if (str[0] == '\0') {
        return;
    }

    intgame_message_window_clear_internal();

    if (intgame_examine_portrait(pc_obj, scenery_obj, &portrait)) {
        intgame_draw_portrait(scenery_obj, portrait, intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 217, 69);
    } else {
        intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, portrait);
    }

    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C7118,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    if ((obj_field_int32_get(scenery_obj, OBJ_F_FLAGS) & OF_INVULNERABLE) == 0) {
        snprintf(buffer, sizeof(buffer),
            "%d/%d",
            object_hp_current(scenery_obj),
            object_hp_max(scenery_obj));
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70F8,
            intgame_flare12_red_font,
            MSG_TEXT_HALIGN_RIGHT);
    }

    if ((obj_field_int32_get(scenery_obj, OBJ_F_SCENERY_FLAGS) & OSCF_MARKS_TOWNMAP) != 0) {
        wmap_ui_mark_townmap(scenery_obj);
    }
}

// 0x555EC0
void intgame_examine_portal(int64_t pc_obj, int64_t portal_obj, char* str)
{
    int portrait;
    ObjectPortalFlags portal_flags;
    MesFileEntry mes_file_entry;
    char buffer[MAX_STRING];

    intgame_message_window_clear_internal();

    if (intgame_examine_portrait(pc_obj, portal_obj, &portrait)) {
        intgame_draw_portrait(portal_obj, portrait, intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 217, 69);
    } else {
        intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, portrait);
    }

    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70C8,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    portal_flags = obj_field_int32_get(portal_obj, OBJ_F_PORTAL_FLAGS);
    if ((portal_flags & OPF_JAMMED) != 0) {
        mes_file_entry.num = 26; // "Jammed"
    } else if ((portal_flags & OPF_MAGICALLY_HELD) != 0) {
        mes_file_entry.num = 27; // "Magically held"
    } else if (object_locked_get(portal_obj)) {
        mes_file_entry.num = 9; // "Locked"
    } else {
        mes_file_entry.num = 10; // "Unlocked"
    }
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        mes_file_entry.str,
        &stru_5C70F8,
        intgame_flare12_red_font,
        MSG_TEXT_HALIGN_LEFT);

    snprintf(buffer, sizeof(buffer),
        "%d/%d",
        object_hp_current(portal_obj),
        object_hp_max(portal_obj));
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70F8,
        intgame_flare12_red_font,
        MSG_TEXT_HALIGN_RIGHT);
}

// 0x556040
void intgame_examine_container(int64_t pc_obj, int64_t container_obj, char* str)
{
    int portrait;
    unsigned int container_flags;
    MesFileEntry mes_file_entry;
    char buffer[MAX_STRING];

    intgame_message_window_clear_internal();

    if (intgame_examine_portrait(pc_obj, container_obj, &portrait)) {
        intgame_draw_portrait(container_obj, portrait, intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 217, 69);
    } else {
        intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, portrait);
    }

    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70C8,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    container_flags = obj_field_int32_get(container_obj, OBJ_F_CONTAINER_FLAGS);
    if ((container_flags & OCOF_JAMMED)) {
        mes_file_entry.num = 26; // "Jammed"
    } else if ((container_flags & OCOF_MAGICALLY_HELD) != 0) {
        mes_file_entry.num = 27; // "Magically held"
    } else if (object_locked_get(container_obj)) {
        mes_file_entry.num = 9; // "Locked"
    } else {
        mes_file_entry.num = 10; // "Unlocked"
    }
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        mes_file_entry.str,
        &stru_5C70F8,
        intgame_flare12_red_font,
        MSG_TEXT_HALIGN_LEFT);

    if ((obj_field_int32_get(container_obj, OBJ_F_FLAGS) & OF_INVULNERABLE) == 0) {
        snprintf(buffer, sizeof(buffer),
            "%d/%d",
            object_hp_current(container_obj),
            object_hp_max(container_obj));
        intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
            buffer,
            &stru_5C70F8,
            intgame_flare12_red_font,
            MSG_TEXT_HALIGN_RIGHT);
    }
}

// 0x5561D0
void intgame_draw_portrait(int64_t obj, int portrait, tig_window_handle_t window_handle, int x, int y)
{
    if (intgame_is_compact_interface()) {
        window_handle = compact_ui_message_window_acquire();
        x -= 211;
        y -= 59;
    }

    portrait_draw_native(obj, portrait, window_handle, x, y);
}

// 0x556220
void intgame_message_window_display_attack(int64_t obj)
{
    MesFileEntry mes_file_entry;
    char str[MAX_STRING];
    int64_t weapon_obj;
    int skill;
    int effectiveness;
    int min_dam;
    int max_dam;

    mes_file_entry.num = 57; // "Total Attack"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    snprintf(str, sizeof(str),
        "%s: %d",
        mes_file_entry.str,
        item_total_attack(obj));

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        intgame_message_window_display_str(-1, str);
        return;
    }

    intgame_message_window_clear_internal();

    intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 675);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70C8,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    weapon_obj = item_wield_get(obj, ITEM_INV_LOC_WEAPON);
    skill = item_weapon_skill(weapon_obj);

    mes_file_entry.num = 69; // "Base To Hit"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    if (IS_TECH_SKILL(skill)) {
        effectiveness = tech_skill_effectiveness(obj, GET_TECH_SKILL(skill), OBJ_HANDLE_NULL);
    } else {
        effectiveness = basic_skill_effectiveness(obj, GET_BASIC_SKILL(skill), OBJ_HANDLE_NULL);
    }

    snprintf(str, sizeof(str), "%s: %d%%", mes_file_entry.str, effectiveness);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70D8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 59; // "Fatigue"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    item_weapon_damage(weapon_obj, obj, DAMAGE_TYPE_FATIGUE, skill, 0, &min_dam, &max_dam);
    if (max_dam != 0) {
        snprintf(str, sizeof(str),
            "%s: %d-%d",
            mes_file_entry.str,
            min_dam,
            max_dam);
    } else {
        snprintf(str, sizeof(str), "%s: 0", mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70E8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 61; // "Fire Damage"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    item_weapon_damage(weapon_obj, obj, DAMAGE_TYPE_FIRE, skill, 0, &min_dam, &max_dam);
    if (max_dam != 0) {
        snprintf(str, sizeof(str),
            "%s: %d-%d",
            mes_file_entry.str,
            min_dam,
            max_dam);
    } else {
        snprintf(str, sizeof(str), "%s: 0", mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70F8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 58; // "Damage"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    item_weapon_damage(weapon_obj, obj, DAMAGE_TYPE_NORMAL, skill, 0, &min_dam, &max_dam);
    if (max_dam != 0) {
        snprintf(str, sizeof(str),
            "%s: %d-%d",
            mes_file_entry.str,
            min_dam,
            max_dam);
    } else {
        snprintf(str, sizeof(str), "%s: 0", mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70D8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);

    mes_file_entry.num = 60; // "Electrical Damage"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    item_weapon_damage(weapon_obj, obj, DAMAGE_TYPE_ELECTRICAL, skill, 0, &min_dam, &max_dam);
    if (max_dam != 0) {
        snprintf(str, sizeof(str),
            "%s: %d-%d",
            mes_file_entry.str,
            min_dam,
            max_dam);
    } else {
        snprintf(str, sizeof(str), "%s: 0", mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70E8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);

    mes_file_entry.num = 62; // "Poison"
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    item_weapon_damage(weapon_obj, obj, DAMAGE_TYPE_POISON, skill, 0, &min_dam, &max_dam);
    if (max_dam != 0) {
        snprintf(str, sizeof(str),
            "%s: %d-%d",
            mes_file_entry.str,
            min_dam,
            max_dam);
    } else {
        snprintf(str, sizeof(str), "%s: 0", mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        str,
        &stru_5C70F8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);
}

// 0x5566B0
void intgame_message_window_display_defense(int64_t obj)
{
    MesFileEntry mes_file_entry;
    char buffer[MAX_STRING];
    int value;

    mes_file_entry.num = 63; // "Total Defense"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = item_total_defence(obj);
    snprintf(buffer, sizeof(buffer),
        "%s: %d",
        mes_file_entry.str,
        value);

    if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
        intgame_message_window_display_str(-1, buffer);
        return;
    }

    intgame_message_window_clear_internal();

    intgame_message_window_draw_image(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle, 674);
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70C8,
        intgame_morph15_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 70; // "Total AC"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_ac(obj, 1);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70D8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 65; // "Magic Resistance"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_resistance(obj, RESISTANCE_TYPE_MAGIC, true);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d%%",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70E8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 67; // "Fire Resistance"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_resistance(obj, RESISTANCE_TYPE_FIRE, true);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d%%",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70F8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT);

    mes_file_entry.num = 64; // "Damage Resistance"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_resistance(obj, RESISTANCE_TYPE_NORMAL, true);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d%%",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70D8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);

    mes_file_entry.num = 66; // "Electrical Resistance"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_resistance(obj, RESISTANCE_TYPE_ELECTRICAL, true);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d%%",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70E8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);

    mes_file_entry.num = 68; // "Poison Resistance"
    mes_get_msg(intgame_mes_file, &mes_file_entry);
    value = object_get_resistance(obj, RESISTANCE_TYPE_POISON, true);
    if (value != 0) {
        snprintf(buffer, sizeof(buffer),
            "%s: %d%%",
            mes_file_entry.str,
            value);
    } else {
        snprintf(buffer, sizeof(buffer),
            "%s: 0",
            mes_file_entry.str);
    }
    intgame_message_window_write_text(intgame_rotwin_text_frame[intgame_iso_window_type].window_handle,
        buffer,
        &stru_5C70F8,
        intgame_flare12_white_font,
        MSG_TEXT_HALIGN_LEFT | MSG_TEXT_SECONDARY);
}

// 0x556A90
void intgame_toggle_primary_button(UiPrimaryButton btn, bool on)
{
    if (on) {
        switch (btn) {
        case UI_PRIMARY_BUTTON_CHAR:
            if (charedit_is_created()) {
                return;
            }
            break;
        case UI_PRIMARY_BUTTON_LOGBOOK:
            if (logbook_ui_is_created()) {
                return;
            }
            break;
        case UI_PRIMARY_BUTTON_TOWNMAP:
        case UI_PRIMARY_BUTTON_WORLDMAP:
            if (wmap_ui_is_created()) {
                return;
            }
            break;
        case UI_PRIMARY_BUTTON_INVENTORY:
            if (inven_ui_is_created()) {
                return;
            }
            break;
        case UI_PRIMARY_BUTTON_COUNT:
            // Should be unreachable.
            break;
        }

        intgame_ui_primary_button_icons[btn] = intgame_ui_primary_button_highlighted_icons[btn];
        intgame_refresh_primary_button(btn);
    } else {
        intgame_ui_primary_button_icons[btn] = intgame_ui_primary_button_normal_icons[btn];
        intgame_refresh_primary_button(btn);
    }
}

// 0x556B70
void intgame_set_map_button(UiPrimaryButton btn)
{
    intgame_map_button = btn;
    intgame_refresh_primary_button(btn);
}

// 0x556B90
void intgame_refresh_primary_button(UiPrimaryButton btn)
{
    tig_button_handle_t button_handle;
    tig_art_id_t art_id;

    switch (btn) {
    case UI_PRIMARY_BUTTON_CHAR:
        button_handle = intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_CHAR].button_handle;
        break;
    case UI_PRIMARY_BUTTON_LOGBOOK:
        button_handle = intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_LOGBOOK].button_handle;
        break;
    case UI_PRIMARY_BUTTON_TOWNMAP:
        if (intgame_map_button != UI_PRIMARY_BUTTON_TOWNMAP) {
            return;
        }

        button_handle = intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_MAP].button_handle;
        break;
    case UI_PRIMARY_BUTTON_WORLDMAP:
        if (intgame_map_button != UI_PRIMARY_BUTTON_WORLDMAP) {
            return;
        }

        button_handle = intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_MAP].button_handle;
        break;
    case UI_PRIMARY_BUTTON_INVENTORY:
        button_handle = intgame_primary_buttons[INTGAME_PRIMARY_BUTTON_INVENTORY].button_handle;
        break;
    case UI_PRIMARY_BUTTON_COUNT:
        // Should be unreachable.
        break;
    }

    tig_art_interface_id_create(intgame_ui_primary_button_icons[btn], 0, 0, 0, &art_id);
    tig_button_set_art(button_handle, art_id);
}

// 0x556C20
void intgame_refresh_experience_gauges(int64_t obj)
{
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    tig_art_id_t art_id;
    int cnt;
    int index;

    if (tig_art_interface_id_create(184, 0, 0, 0, &art_id) == TIG_OK) {
        src_rect.x = 211;
        src_rect.y = 37;
        src_rect.width = 384;
        src_rect.height = 14;

        art_blit_info.flags = 0;
        art_blit_info.art_id = art_id;
        art_blit_info.src_rect = &src_rect;
        art_blit_info.dst_rect = &src_rect;
        tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
    }

    cnt = 11 * level_progress_towards_next_level(obj) / 10;
    for (index = 0; index < cnt / 100; index++) {
        if (tig_art_interface_id_create(stru_5C71D0[index].art_num, 0, 0, 0, &art_id) == TIG_OK
            && tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = art_frame_data.width;
            src_rect.height = art_frame_data.height;

            dst_rect.x = stru_5C71D0[index].x;
            dst_rect.y = stru_5C71D0[index].y;
            dst_rect.width = src_rect.width;
            dst_rect.height = src_rect.height;

            art_blit_info.flags = 0;
            art_blit_info.art_id = art_id;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
        }
    }

    if (cnt % 100 != 0) {
        if (tig_art_interface_id_create(stru_5C7270.art_num, 0, 0, 0, &art_id) == TIG_OK
            && tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
            src_rect.x = 0;
            src_rect.y = 0;
            src_rect.width = art_frame_data.width * (cnt % 100) / 100;
            src_rect.height = art_frame_data.height;

            dst_rect.x = stru_5C7270.x;
            dst_rect.y = stru_5C7270.y;
            dst_rect.width = src_rect.width;
            dst_rect.height = src_rect.height;

            art_blit_info.flags = 0;
            art_blit_info.art_id = art_id;
            art_blit_info.src_rect = &src_rect;
            art_blit_info.dst_rect = &dst_rect;
            tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
        }
    }
}

// 0x556E60
void sub_556E60(void)
{
    int64_t parent_obj;

    if (qword_64C688 != OBJ_HANDLE_NULL) {
        parent_obj = obj_field_handle_get(qword_64C688, OBJ_F_ITEM_PARENT);
        if (parent_obj == OBJ_HANDLE_NULL || !player_is_local_pc_obj(parent_obj)) {
            qword_64C688 = OBJ_HANDLE_NULL;
        }
    }
}

// 0x556EA0
void sub_556EA0(int64_t item_obj)
{
    if (item_obj != OBJ_HANDLE_NULL) {
        qword_64C688 = item_obj;
    } else {
        qword_64C688 = item_wield_get(player_get_local_pc_obj(), ITEM_INV_LOC_WEAPON);
    }
    iso_interface_window_set(ROTWIN_TYPE_MAGICTECH);
}

// 0x556EF0
void intgame_mt_button_enable(void)
{
    bool hidden;
    int64_t obj;
    int64_t item_obj;
    int mana_store;
    unsigned int flags;

    if (tig_button_is_hidden(intgame_mt_button_info.button_handle, &hidden) == TIG_OK && hidden) {
        obj = player_get_local_pc_obj();
        item_obj = item_wield_get(obj, ITEM_INV_LOC_WEAPON);
        if (item_obj != OBJ_HANDLE_NULL) {
            mana_store = obj_field_int32_get(item_obj, OBJ_F_ITEM_SPELL_MANA_STORE);
            flags = obj_field_int32_get(item_obj, OBJ_F_ITEM_FLAGS);
            if ((mana_store != 0 || (flags & OIF_IS_MAGICAL) != 0)
                && obj_field_int32_get(item_obj, OBJ_F_ITEM_MAGIC_TECH_COMPLEXITY) > 0) {
                tig_button_show(intgame_mt_button_info.button_handle);
            }
        }
    }
}

// 0x556F80
void intgame_mt_button_disable(void)
{
    bool hidden;
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;
    TigRect rect;

    if (tig_button_is_hidden(intgame_mt_button_info.button_handle, &hidden) == TIG_OK && !hidden) {
        tig_button_hide(intgame_mt_button_info.button_handle);

        if (tig_art_interface_id_create(intgame_mt_button_info.art_num, 0, 0, 0, &art_id) != TIG_OK) {
            tig_debug_printf("Intgame: intgame_mt_button_disable: ERROR: Can't find Interface Art: %d!\n", intgame_mt_button_info.art_num);
            return;
        }

        tig_art_frame_data(art_id, &art_frame_data);

        if (tig_art_interface_id_create(184, 0, 0, 0, &art_id) != TIG_OK) {
            tig_debug_printf("Intgame: intgame_mt_button_disable: ERROR: Can't find Interface Art: %d!\n", 184);
        }

        rect.x = intgame_mt_button_info.x - intgame_interface_window_frames[1].x;
        rect.y = intgame_mt_button_info.y - intgame_interface_window_frames[1].y;
        rect.width = art_frame_data.width;
        rect.height = art_frame_data.height;

        art_blit_info.flags = 0;
        art_blit_info.art_id = art_id;
        art_blit_info.src_rect = &rect;
        art_blit_info.dst_rect = &rect;
        tig_window_blit_art(dword_64C4F8[1], &art_blit_info);
    }
}

// 0x5570A0
void sub_5570A0(int64_t obj)
{
    qword_64C690 = obj;
    if (qword_64C690 != OBJ_HANDLE_NULL) {
        sub_57CCF0(player_get_local_pc_obj(), qword_64C690);
    }
}

// 0x5570D0
void intgame_notify_item_inserted_or_removed(int64_t item_obj, bool removed, int inventory_location)
{
    int index;
    Hotkey* hotkey;

    for (index = 0; index < 10; index++) {
        hotkey = sub_57F240(index);
        if (removed && hotkey->item_obj.obj == item_obj) {
            sub_57EF90(index);
        }

        if (hotkey->type == HOTKEY_ITEM
            && hotkey->item_obj.obj != OBJ_HANDLE_NULL) {
            hotkey->count = item_count_items_matching_prototype(player_get_local_pc_obj(), hotkey->item_obj.obj);
            intgame_hotkey_refresh(index);
        }
    }

    if (removed) {
        hotkey_ui_notify_item_inserted_or_removed(item_obj, removed);
        if (qword_64C688 == item_obj
            && intgame_iso_window_type == ROTWIN_TYPE_MAGICTECH) {
            iso_interface_window_set(ROTWIN_TYPE_MSG);
            qword_64C688 = OBJ_HANDLE_NULL;
        }

        if (inventory_location == ITEM_INV_LOC_WEAPON) {
            intgame_mt_button_disable();
        }
    } else {
        intgame_mt_button_enable();
    }

    if (hotkey_ui_is_dragging()) {
        intgame_refresh_cursor();
        hotkey_ui_dragging = false;
        hotkey_ui_dragging_index = -1;
    }
}

// 0x5571C0
void intgame_refresh_health_bar(int64_t obj)
{
    if (player_is_local_pc_obj(obj)) {
        anim_ui_event_add(ANIM_UI_EVENT_TYPE_UPDATE_HEALTH_BAR, -1);
    }

    if (object_hover_obj_get() == obj || qword_64C690 == obj) {
        sub_57CCF0(player_get_local_pc_obj(), obj);
    }

    follower_ui_update_obj(obj);
}

// 0x557230
bool intgame_big_window_create(void)
{
    TigWindowData window_data;

    window_data.flags = TIG_WINDOW_MESSAGE_FILTER;
    window_data.rect.x = 0;
    window_data.rect.y = 41;
    window_data.rect.width = 800;
    window_data.rect.height = 400;
    window_data.background_color = 0;
    window_data.message_filter = intgame_big_window_message_filter;
    hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

    if (tig_window_create(&window_data, &intgame_big_window_handle) != TIG_OK) {
        return false;
    }

    tig_window_hide(intgame_big_window_handle);
    intgame_big_window_locked = false;

    return true;
}

// 0x5572B0
void intgame_big_window_destroy(void)
{
    tig_window_destroy(intgame_big_window_handle);
}

// 0x5572C0
bool intgame_big_window_message_filter(TigMessage* msg)
{
    return false;
}

// 0x5572D0
bool intgame_big_window_lock(TigWindowMessageFilterFunc func, tig_window_handle_t* window_handle_ptr)
{
    if (intgame_big_window_locked) {
        return false;
    }

    // CE: reopening while a previous close is still animating out (e.g.
    // switching inventory → character → worldmap, which all share this
    // window). Cancel the deferred teardown, drop the stale buttons, and
    // CANCEL the in-flight exit tween so the new window's entrance plays
    // FRESH (from its scaled-down/transparent start) rather than
    // retargeting the near-full hide value — otherwise the switch reads
    // as an instant in-place swap with no animation.
    if (intgame_big_window_exit_pending) {
        intgame_big_window_exit_pending = false;
        tig_window_button_destroy(intgame_big_window_handle);
        ui_anim_cancel_for_window(intgame_big_window_handle);
    }

    intgame_big_window_locked = true;
    tig_window_message_filter_set(intgame_big_window_handle, func);
    tig_window_show(intgame_big_window_handle);
    sub_51E850(intgame_big_window_handle);
    *window_handle_ptr = intgame_big_window_handle;

    return true;
}

// CE: exit-animation bookkeeping for the shared big window. When a
// window closes via intgame_big_window_close_animated, the lock is
// released immediately (so a reopen can re-acquire) but the teardown
// (button destroy, filter reset, hide) is deferred to the hide tween's
// on_complete. intgame_big_window_exit_pending (declared above the lock
// function) guards that finalize so a reopen mid-exit cancels it.

// Exit feel: subtle scale (0.96) + fade, faster than the entrance.
static const ui_anim_profile_t INTGAME_BIG_WINDOW_EXIT_PROFILE = { 110, 1.2f };

// The actual teardown — shared by the synchronous unlock and the
// deferred animated-exit finalize.
static void intgame_big_window_teardown(void)
{
    tig_window_button_destroy(intgame_big_window_handle);
    tig_window_message_filter_set(intgame_big_window_handle, intgame_big_window_message_filter);
    tig_window_hide(intgame_big_window_handle);
}

// 0x557330
void intgame_big_window_unlock(void)
{
    // Synchronous teardown — used by creation error/abort paths (the
    // window was never presented, so it must NOT animate out).
    intgame_big_window_locked = false;
    intgame_big_window_exit_pending = false;
    intgame_big_window_teardown();
}

// CE: hide tween on_complete — runs the deferred teardown once the exit
// animation settles. No-op if the window was re-locked mid-exit (lock
// clears exit_pending).
static void intgame_big_window_finalize_exit(void* ctx)
{
    (void)ctx;
    if (!intgame_big_window_exit_pending) {
        return;
    }
    intgame_big_window_exit_pending = false;
    intgame_big_window_teardown();
}

// CE: animated close for the shared big window. Releases the lock
// immediately (a reopen can re-acquire mid-exit), then scales+fades the
// window out and runs the teardown on settle. cfg-disabled / pool-full:
// ui_anim_window_hide fires the on_complete synchronously, so this
// degrades to the old instant close. Re-locking mid-exit retargets this
// tween gracefully back to full (see intgame_big_window_lock).
void intgame_big_window_close_animated(void)
{
    if (!intgame_big_window_locked) {
        return;
    }
    intgame_big_window_locked = false;
    intgame_big_window_exit_pending = true;
    ui_anim_window_hide(intgame_big_window_handle, UI_ANIM_ANCHOR_CENTER,
        0.96f, &INTGAME_BIG_WINDOW_EXIT_PROFILE,
        intgame_big_window_finalize_exit, NULL);
}

bool intgame_big_window_screen_rect(TigRect* rect)
{
    TigWindowData wd;

    if (intgame_big_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return false;
    }
    if (tig_window_data(intgame_big_window_handle, &wd) != TIG_OK) {
        return false;
    }
    *rect = wd.rect;
    return true;
}

// Re-push the big overlay window to the top of its z-class so it sits above
// any subsequently created MIDDLE-class siblings (e.g. the main-menu backdrop
// for the charedit step of character creation).
void intgame_big_window_promote(void)
{
    if (intgame_big_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    tig_window_move_on_top(intgame_big_window_handle);
}

// Re-push both iso-interface HUD strips to the top of their z-class.
// Used after the custom-UI backdrop window is created so the strips
// (which were created earlier and are therefore older in MIDDLE class)
// don't get covered by the backdrop. Lets the original mainmenu art's
// chromakey knockouts reveal the strip content (rotwin / info bar)
// underneath, the way upstream's z-compositing intended.
void intgame_iso_strips_promote(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (dword_64C4F8[i] != TIG_WINDOW_HANDLE_INVALID) {
            tig_window_move_on_top(dword_64C4F8[i]);
        }
    }
}

// CE: chrome-less shell menus (Save / Load, hi-res Options) hide
// the bars via slide. Idempotent with intgame_hud_slide_hide — if
// the path already ran intgame_hide (which slides), this just
// re-asserts the same slide.
void intgame_iso_strips_hide_full(void)
{
    intgame_hud_slide_hide();
}

// Force the iso (game-world) window visible.  intgame_hide() hides the
// iso window — so when a pause-menu chain (ESC → Save / Load) hands off
// into a chrome-less menu, the world would otherwise stay hidden behind
// the panel as a black flood.  Pair with intgame_iso_strips_hide_full()
// to get a clean "panel over live game" composite.
void intgame_iso_world_show(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (dword_64C52C != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_show(dword_64C52C);
    }
}

// Re-apply the moved-and-shown state for the bottom iso strip, so
// chrome-bearing vanilla menus (pause menu, etc.) get their band
// back. The top strip stays hidden.
//
// CE: cancels any in-flight slide so the vanilla band positioning
// owns the bar's screen y without the slide system re-driving it.
void intgame_iso_strips_show_as_band(void)
{
    TigRect rect;
    if (!intgame_iso_interface_created) {
        return;
    }
    intgame_hud_slide_reset_to_rest();
    intgame_hud_band_mode = true;
    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_hide(dword_64C4F8[0]);
    }
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
        rect = intgame_interface_window_frames[1];
        hrp_apply(&rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);
        tig_window_move(dword_64C4F8[1], rect.x, rect.y);
        // CE: re-evaluate the translucent-black underlay BEFORE
        // the bar becomes visible as a band. Otherwise the bar
        // composites for one frame with whatever underlay was set
        // last (iso world, from iso_interface_create), which the
        // user sees as the bar punching through to the pregame
        // world for a moment before snapping to the menu backdrop.
        // Doing it here means the very first frame the bar shows
        // already samples the correct underlay (mainmenu backdrop
        // when one is up, none otherwise).
        intgame_refresh_hud_bar_tint();
        if (!intgame_is_compact_interface()) {
            tig_window_show(dword_64C4F8[1]);
        }
    }
}

// 0x557370
void sub_557370(int64_t source_obj, int64_t target_obj)
{
    unsigned int spell_flags;
    unsigned int critter_flags;
    int target_obj_type;

    if (source_obj == OBJ_HANDLE_NULL) {
        return;
    }

    if (!critter_is_active(source_obj)) {
        return;
    }

    if (target_obj == source_obj) {
        return;
    }

    spell_flags = obj_field_int32_get(source_obj, OBJ_F_SPELL_FLAGS);
    critter_flags = obj_field_int32_get(source_obj, OBJ_F_CRITTER_FLAGS);

    if ((spell_flags & OSF_STONED) != 0
        || (critter_flags & (OCF_PARALYZED | OCF_STUNNED)) != 0) {
        return;
    }

    target_obj_type = obj_field_int32_get(target_obj, OBJ_F_TYPE);

    // NOTE: The code below omit some unused checks from the original code. I'm
    // not sure if it's a bug or not.
    switch (intgame_mode_get()) {
    case INTGAME_MODE_MAIN:
        if (inven_ui_drag_item_obj_get() == OBJ_HANDLE_NULL) {
            switch (target_obj_type) {
            case OBJ_TYPE_WALL:
            case OBJ_TYPE_PORTAL:
            case OBJ_TYPE_SCENERY:
                if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                    combat_check_attack(source_obj, target_obj);
                } else {
                    if ((spell_flags & OSF_POLYMORPHED) == 0) {
                        combat_check_use_obj(source_obj, target_obj);
                    }
                }
                break;
            case OBJ_TYPE_CONTAINER:
                if ((spell_flags & OSF_POLYMORPHED) == 0) {
                    if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                        combat_check_attack(source_obj, target_obj);
                    } else {
                        combat_check_use_obj(source_obj, target_obj);
                    }
                }
                break;
            case OBJ_TYPE_WEAPON:
            case OBJ_TYPE_AMMO:
            case OBJ_TYPE_ARMOR:
            case OBJ_TYPE_GOLD:
            case OBJ_TYPE_FOOD:
            case OBJ_TYPE_SCROLL:
            case OBJ_TYPE_KEY:
            case OBJ_TYPE_KEY_RING:
            case OBJ_TYPE_WRITTEN:
            case OBJ_TYPE_GENERIC:
                if ((spell_flags & OSF_POLYMORPHED) == 0) {
                    combat_check_pick_item(source_obj, target_obj);
                }
                break;
            case OBJ_TYPE_PC:
            case OBJ_TYPE_NPC:
                if (critter_is_dead(target_obj)) {
                    if (tig_kb_get_modifier(SDL_KMOD_ALT)) {
                        combat_check_use_obj(source_obj, target_obj);
                    }
                } else {
                    if (!player_is_local_pc_obj(critter_pc_leader_get(target_obj)) || tig_kb_get_modifier(SDL_KMOD_LALT)) {
                        combat_check_attack(source_obj, target_obj);
                    }
                }
                break;
            case OBJ_TYPE_TRAP:
                combat_check_move_to(source_obj, obj_field_int64_get(target_obj, OBJ_F_LOCATION));
                break;
            }
        }
        break;
    case INTGAME_MODE_SPELL:
        combat_check_cast_spell(source_obj);
        break;
    case INTGAME_MODE_SKILL:
        combat_check_use_skill(source_obj);
        break;
    default:
        break;
    }
}

// 0x557670
void intgame_there_is_nothing_to_loot(void)
{
    MesFileEntry mes_file_entry;
    UiMessage ui_message;

    mes_file_entry.num = 2000;
    mes_get_msg(intgame_mes_file, &mes_file_entry);

    ui_message.type = UI_MSG_TYPE_FEEDBACK;
    ui_message.str = mes_file_entry.str;
    intgame_message_window_display_msg(&ui_message);
}

// 0x5576B0
void sub_5576B0(void)
{
    TigRect rect;
    TigArtFrameData art_frame_data;
    TigArtBlitInfo art_blit_info;

    art_blit_info.flags = 0;
    art_blit_info.src_rect = &rect;
    art_blit_info.dst_rect = &rect;
    tig_art_interface_id_create(185, 0, 0, 0, &(art_blit_info.art_id));

    tig_art_frame_data(art_blit_info.art_id, &art_frame_data);

    rect.x = 0;
    rect.y = 0;
    rect.width = art_frame_data.width;
    rect.height = art_frame_data.height;
    tig_window_blit_art(dword_64C4F8[0], &art_blit_info);
}

// 0x557730
void sub_557730(int index)
{
    MesFileEntry mes_file_entry;
    UiMessage ui_message;

    mes_file_entry.num = index + 3000;
    if (mes_search(intgame_mes_file, &mes_file_entry)) {
        mes_get_msg(intgame_mes_file, &mes_file_entry);

        ui_message.type = UI_MSG_TYPE_FEEDBACK;
        ui_message.str = mes_file_entry.str;
        intgame_message_window_display_msg(&ui_message);
    }
}

// 0x557790
void sub_557790(int64_t obj)
{
    if (obj != OBJ_HANDLE_NULL
        && obj == object_hover_obj_get()) {
        sub_57CCF0(player_get_local_pc_obj(), obj);
    }
}

// 0x5577D0
unsigned int intgame_get_iso_window_flags(void)
{
    return intgame_iso_window_flags;
}

// 0x5577E0
void intgame_set_iso_window_flags(unsigned int flags)
{
    intgame_iso_window_flags = flags;
}

// 0x5577F0
void intgame_set_iso_window_width(int width)
{
    intgame_iso_window_width = width;
}

// 0x557800
void intgame_set_iso_window_height(int height)
{
    intgame_iso_window_height = height;
}

// 0x557810
bool intgame_create_iso_window(tig_window_handle_t* window_handle_ptr)
{
    TigWindowData window_data;

    window_data.flags = intgame_iso_window_flags | TIG_WINDOW_ALWAYS_ON_BOTTOM | TIG_WINDOW_VIDEO_MEMORY;
    window_data.rect.x = 0;
    window_data.rect.y = 0;
    window_data.rect.width = intgame_iso_window_width;
    window_data.rect.height = intgame_iso_window_height;
    window_data.background_color = 0;

    if (tig_window_create(&window_data, window_handle_ptr) != TIG_OK) {
        tig_debug_printf("intgame_create_iso_window: ERROR: window create failed!\n");
        tig_exit();
        return false;
    }

    intgame_iso_window = *window_handle_ptr;
    return true;
}

// 0x5578C0
bool intgame_is_compact_interface(void)
{
    return intgame_compact_interface;
}

bool intgame_iso_interface_is_created(void)
{
    return intgame_iso_interface_created;
}

// 0x5578D0
void intgame_set_fullscreen(void)
{
    intgame_fullscreen = true;
}

// 0x5578E0
void intgame_toggle_interface(void)
{
    TigWindowData window_data;
    GameResizeInfo resize_info;
    int index;

    if (!intgame_fullscreen) {
        return;
    }

    tig_debug_printf("Resizing Iso View...");

    resize_info.window_handle = dword_64C52C;
    intgame_compact_interface = !intgame_compact_interface;

    if (intgame_iso_window != TIG_WINDOW_HANDLE_INVALID) {
        if (tig_window_data(dword_64C52C, &window_data) != TIG_OK) {
            tig_debug_printf("intgame_toggle_interface: ERROR: tig_window_data failed!\n");
            exit(EXIT_FAILURE);
        }

        resize_info.window_rect = window_data.rect;
        resize_info.content_rect = window_data.rect;

        if (intgame_compact_interface) {
            intgame_pc_lens_dst_rect = intgame_pc_lens_fullscreen_dst_frame;
            intgame_pc_lens_dst_rect.x = (800 - intgame_pc_lens_dst_rect.width) / 2;
            intgame_pc_lens_dst_rect.y = (600 - intgame_pc_lens_dst_rect.height) / 2;
            hrp_apply(&intgame_pc_lens_dst_rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

            for (index = 0; index < 2; index++) {
                tig_window_hide(dword_64C4F8[index]);
            }

            gamelib_resize(&resize_info);
            gameuilib_resize(&resize_info);

            compact_ui_create();
        } else {
            intgame_pc_lens_dst_rect = intgame_pc_lens_normal_dst_frame;
            intgame_pc_lens_dst_rect.x = (800 - intgame_pc_lens_dst_rect.width) / 2;
            intgame_pc_lens_dst_rect.y = (600 - intgame_pc_lens_dst_rect.height) / 2;
            hrp_apply(&intgame_pc_lens_dst_rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

            gamelib_resize(&resize_info);
            gameuilib_resize(&resize_info);

            for (index = 0; index < 2; index++) {
                tig_window_show(dword_64C4F8[index]);
            }

            compact_ui_destroy();
        }
    }

    if (tig_window_is_hidden(dword_64C52C)) {
        intgame_hide();
    }

    tig_debug_printf("completed.\n");
}

// TODO: Reuse `iso_interface_window_get`.
//
// 0x557AA0
RotatingWindowType iso_interface_window_get_3(void)
{
    return intgame_iso_window_type;
}

// 0x557AB0
int sub_557AB0(void)
{
    return dword_64C530;
}

// 0x557AC0
void sub_557AC0(int clg, int lvl, UiButtonInfo* button_info)
{
    if (button_info != NULL) {
        *button_info = intgame_spell_buttons[clg * 5 + lvl];
    }
}

// 0x557B00
int64_t sub_557B00(void)
{
    return qword_64C688;
}

// 0x557B10
mes_file_handle_t intgame_hotkey_mes_file(void)
{
    return intgame_mes_file;
}

// 0x557B20
UiButtonInfo* intgame_recent_action_button_get(int index)
{
    return &(intgame_recent_action_buttons[index]);
}

// 0x557B30
void intgame_recent_action_button_position_set(int index, int x, int y)
{
    intgame_recent_action_buttons[index].x = x;
    intgame_recent_action_buttons[index].y = y;
}

// 0x557B50
int sub_557B50(int index)
{
    return dword_5C6F60[index];
}

// 0x557B60
int sub_557B60(void)
{
    TigMouseState mouse_state;
    TigWindowData window_data;
    TigButtonData button_data;
    int x;
    int y;
    int index;

    if (intgame_iso_window_type != ROTWIN_TYPE_SKILLS) {
        return 4;
    }

    tig_mouse_get_state(&mouse_state);

    // Check if mouse position is within rotating window bounds.
    if (tig_window_data(dword_64C4F8[1], &window_data) != TIG_OK
        || mouse_state.x < window_data.rect.x
        || mouse_state.y < window_data.rect.y
        || mouse_state.y >= window_data.rect.x + window_data.rect.width
        || mouse_state.y >= window_data.rect.y + window_data.rect.height) {
        return 4;
    }

    // Convert mouse position from screen coordinate system to rotating
    // window coordinate system.
    x = mouse_state.x - window_data.rect.x;
    y = mouse_state.y - window_data.rect.y;

    for (index = 0; index < 4; index++) {
        if (tig_button_data(stru_5C6C68[index].button_handle, &button_data) != TIG_OK) {
            break;
        }

        if (x >= button_data.x
            && y >= button_data.y
            && x < button_data.x + button_data.width
            && y < button_data.y + button_data.height) {
            return index;
        }
    }

    return 4;
}

// 0x557C00
int sub_557C00(void)
{
    TigMouseState mouse_state;
    TigWindowData window_data;
    TigButtonData button_data;
    int x;
    int y;
    int index;
    int64_t pc_obj;

    if (intgame_iso_window_type != ROTWIN_TYPE_MAGICTECH) {
        return 5;
    }

    tig_mouse_get_state(&mouse_state);

    // Check if mouse position is within rotating window bounds.
    if (tig_window_data(dword_64C4F8[1], &window_data) != TIG_OK
        || mouse_state.x < window_data.rect.x
        || mouse_state.y < window_data.rect.y
        || mouse_state.y >= window_data.rect.x + window_data.rect.width
        || mouse_state.y >= window_data.rect.y + window_data.rect.height) {
        return 5;
    }

    // Convert mouse position from screen coordinate system to rotating window
    // coordinate system.
    x = mouse_state.x - window_data.rect.x;
    y = mouse_state.y - window_data.rect.y;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return 5;
    }

    for (index = 0; index < 5; index++) {
        if (tig_button_data(intgame_mt_spell_buttons[index].button_handle, &button_data) != TIG_OK) {
            return 6;
        }

        if (x >= button_data.x
            && y >= button_data.y
            && x < button_data.x + button_data.width
            && y < button_data.y + button_data.height) {
            if (!sub_45A030(mt_item_spell(qword_64C688, index))) {
                return index;
            } else {
                return 5;
            }
        }
    }

    return 5;
}

// 0x557CF0
int sub_557CF0(void)
{
    TigMouseState mouse_state;
    TigWindowData window_data;
    TigButtonData button_data;
    int x;
    int y;
    int index;
    int64_t pc_obj;

    if (iso_interface_window_get_3() != ROTWIN_TYPE_SPELLS) {
        return 5;
    }

    tig_mouse_get_state(&mouse_state);

    // Check if mouse position is within rotating window bounds.
    if (tig_window_data(dword_64C4F8[1], &window_data) != TIG_OK
        || mouse_state.x < window_data.rect.x
        || mouse_state.y < window_data.rect.y
        || mouse_state.y >= window_data.rect.x + window_data.rect.width
        || mouse_state.y >= window_data.rect.y + window_data.rect.height) {
        return 5;
    }

    // Convert mouse position from screen coordinate system to rotating window
    // coordinate system.
    x = mouse_state.x - window_data.rect.x;
    y = mouse_state.y - window_data.rect.y;

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return 5;
    }

    for (index = 0; index < 5; index++) {
        if (tig_button_data(intgame_spell_buttons[5 * dword_64C530 + index].button_handle, &button_data) != TIG_OK) {
            return 6;
        }

        if (x >= button_data.x
            && y >= button_data.y
            && x < button_data.x + button_data.width
            && y < button_data.y + button_data.height) {
            if (spell_is_known(pc_obj, 5 * dword_64C530 + index)) {
                return index;
            } else {
                return 5;
            }
        }
    }

    return 5;
}

void intgame_hide(void)
{
    int index;

    if (!intgame_iso_interface_created) {
        return;
    }

    tig_window_hide(dword_64C52C);

    // CE: slide bars off-screen instead of tig_window_hide + move
    // to mid-screen "band" position. Shell-menu callers that want
    // the band (vanilla chrome-bearing menus) cancel this via
    // intgame_iso_strips_show_as_band → intgame_hud_slide_reset_to_rest.
    intgame_hud_slide_hide();

    if (intgame_fs_hotkey_window != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_hide(intgame_fs_hotkey_window);
    }

    for (index = 0; index < 5; index++) {
        tig_window_hide(intgame_maintain_fs_windows[index]);
    }

    follower_ui_hide();
}

// CE: TAB-cycle HUD-crop state. Bound to the TAB key in main.c.
//
// Four stages cycle FULL → MEDIUM → MINI → HIDDEN → FULL. Instead
// of hiding/moving the strip windows, we use tig's per-window clip
// rect (tig_window_clip_rect_set) to composite only a chosen band
// of the bottom strip — the rest of the bar's VB stays intact, so
// when the user cycles back to FULL the original bar reappears with
// zero rebuilding. Buttons, rotwin chrome, text writes etc. all
// continue working in their natural positions; the only thing that
// changes is which pixels make it to screen.
//
// The top strip is always either fully visible (FULL) or fully
// clipped (everything else).
//
// Stage-relative bands within the BOTTOM strip's VB (strip-relative
// design coords; the strip is 800x159 anchored at y=441 in 800x600
// reference space):
//   FULL    → no clip; whole strip composites
//   MEDIUM  → rotwin region: (196, 51, 410, 107) — the full
//             skill_rot / dialoguewindow frame area
//   MINI    → slim row: (205, 122, 394, 25) — the cropped 18px
//             rollover row (skill_rot region at art-local (9,71))
//   HIDDEN  → clip to a 0-sized rect; nothing composites
//
// When the user presses K/M (skills/spells) and the current stage
// is HIDDEN or MINI, the cycle auto-pops to MEDIUM so the rotwin
// becomes interactable. Cycling TAB or pressing K again reverts.

typedef enum IntgameHudStage {
    INTGAME_HUD_STAGE_FULL = 0,
    INTGAME_HUD_STAGE_MEDIUM = 1,
    INTGAME_HUD_STAGE_MINI = 2,
    INTGAME_HUD_STAGE_HIDDEN = 3,
    INTGAME_HUD_STAGE_COUNT,
} IntgameHudStage;

static IntgameHudStage intgame_hud_stage = INTGAME_HUD_STAGE_FULL;
// CE: pre-rotwin stage snapshot. When the user is in MINI/HIDDEN and
// invokes a rotwin (K/M, magictech weapon, etc.), we auto-pop to MEDIUM
// and remember where to return when the rotwin is dismissed. Cleared
// by user-driven TAB cycling (manual override).
static IntgameHudStage intgame_hud_saved_stage = INTGAME_HUD_STAGE_FULL;
static bool intgame_hud_has_saved_stage = false;
// CE: MINI is a "hack" stage — it silently keeps SKILLS active so the
// slim text row shows real hover content. Pressing K (or whichever key
// matches the active rotwin) while MINI is up should NOT dismiss the
// rotwin (that would swap to MSG and visually break the row); instead
// it peek-expands to MEDIUM. A second press returns to MINI without
// dismissing. This flag tracks "we're MEDIUM but only because MINI
// peeked," so we know to return-to-MINI instead of toggling-off.
static bool intgame_hud_peek_from_mini = false;
// Mirror bool kept for the existing intgame_hud_is_user_hidden()
// consumers (camera-follow margin math, fate/sleep top-bar dock).
static bool intgame_hud_user_hidden;

// CE: spring-tweened vertical slide offset (design-coord pixels) for
// the top HUD bar. 0 = bar at its rest screen position (y=0); -41 =
// bar slid fully off the top of the screen. Animated by ui_anim_int_to
// on TAB-HUD stage transitions between visible-top (FULL) and hidden-
// top (MEDIUM, MINI, HIDDEN) stages. Read by intgame_hud_top_offset
// every frame, so fate / sleep panels that dock under the bar "ride
// along" without any explicit parent-child wiring.
static int intgame_hud_top_slide_offset = 0;

// CE: bottom HUD bar slide-from-bottom offset (design coords).
// Positive = bar moved DOWN below its natural rest y (off-screen
// when slide_offset >= INTGAME_HUD_BOTTOM_H). 0 = at rest.
//
// Used for the return-from-HIDDEN transition only: instead of the
// scale-down/fade-out's inverse scale-up/fade-in, the bar slides
// up from below the screen back into its rest position. Snapped
// to INTGAME_HUD_BOTTOM_H when the transition fires, then
// ui_anim_int_to springs it back to 0 over the slide profile.
// intgame_hud_ping applies it via tig_window_move every frame.
static int intgame_hud_bottom_slide_offset = 0;

// CE: returns the screen-coord y of the bottom bar at rest. Computed
// on-demand from the design-coord frame + the same gravity used at
// create-time (BOTTOM, see iso_interface_create). Tracks screen
// resizes via hrp_apply.
//
// Previously this was captured from wd.rect.y when offset==0, but
// the vanilla band-mode path moves the bar mid-screen with offset
// still 0, which would corrupt the capture and break later slides.
static int intgame_hud_bottom_rest_y_compute(void)
{
    TigRect rest = intgame_interface_window_frames[1];
    hrp_apply(&rest, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
    return rest.y;
}

// CE: the last visible stage the bar was in before going HIDDEN.
// During HIDDEN, the bar still renders this stage's band so the
// slide-down carries the band off-screen as a unit rather than
// flashing empty. Switches to FULL on initial cold start so the
// very first show-after-HIDDEN (if it somehow happens) doesn't
// render garbage.
static IntgameHudStage intgame_hud_last_visible_stage = INTGAME_HUD_STAGE_FULL;

// CE: shared tween handles for the top + bottom bar slide offsets.
// File-scope so the apply_clips TAB-driven path AND the public
// slide_hide / slide_show shell-menu paths can cancel each other's
// in-flight tween before retargeting — without this, two callers
// fighting over the same offset variable would produce visible
// jitter.
static ui_anim_handle_t intgame_hud_top_slide_handle = UI_ANIM_HANDLE_INVALID;
static ui_anim_handle_t intgame_hud_bottom_slide_handle = UI_ANIM_HANDLE_INVALID;

// CE: ping's "last value applied to tig_window_move" memo, file-scope
// so reset_to_rest (band mode handoff) can sync them against the new
// offset — otherwise ping fires on the next frame and snaps the bar
// back to rest_y, fighting whatever band-mode positioning the caller
// just installed via tig_window_move.
static int intgame_hud_top_last_applied = INT_MIN;
static int intgame_hud_bottom_last_applied = INT_MIN;

// CE: set true while a shell menu (Options / Save / Load / ESC
// pause) is currently slide-hiding the bars. apply_clips skips its
// usual TAB-driven slide retargets while this is set so the menu's
// off-screen state isn't fought by a stray rotwin-driven reapply.
// Cleared by intgame_hud_slide_show.
// (Forward-declared at file top — defining decl is up there.)

// CE: set true while vanilla band mode is active (chargen windows
// repurpose the bottom bar as their chrome band via
// intgame_iso_strips_show_as_band). The bar is parked mid-screen
// via tig_window_move with offset==0; if we then transition to a
// chrome-less menu, a normal slide-hide would jump the bar to its
// natural BOTTOM rest before sliding off — visible as a flash.
// slide_hide snaps to off-screen instead when this is set so the
// chargen panel's own exit animation owns the visual.
// (Forward-declared at file top — defining decl is up there.)

// CE: top HUD bar height (design coords). Hardcoded — matches the
// chrome art (top_bar.art = 800x41).
#define INTGAME_HUD_TOP_HEIGHT 41

// Stage-band design coords inside the bottom strip's 800x159 VB.
#define INTGAME_HUD_BOTTOM_W 800
#define INTGAME_HUD_BOTTOM_H 159
#define INTGAME_HUD_MEDIUM_BAND_X 196
#define INTGAME_HUD_MEDIUM_BAND_Y 51
#define INTGAME_HUD_MEDIUM_BAND_W 410
#define INTGAME_HUD_MEDIUM_BAND_H 107
#define INTGAME_HUD_MINI_BAND_X 205
#define INTGAME_HUD_MINI_BAND_Y 122
#define INTGAME_HUD_MINI_BAND_W 394
#define INTGAME_HUD_MINI_BAND_H 25

void intgame_show(void)
{
    int index;

    if (!intgame_iso_interface_created) {
        return;
    }

    tig_window_show(dword_64C52C);

    // CE: slide bars back to TAB-stage rest.
    //   - shell_hidden (slide-hide path used): animate from
    //     off-screen.
    //   - band_mode (chargen / vanilla Options/Save/Load was
    //     using the bar as panel chrome at mid-screen): snap to
    //     off-screen first, then animate in. The chargen exit is
    //     a level-load entrance from the player's POV — slide-in
    //     is the right entrance.
    //   - neither (last seen at TAB rest, no menu): legacy
    //     tig_window_show + snap to BOTTOM rest.
    if (intgame_hud_shell_hidden || intgame_hud_band_mode) {
        if (intgame_hud_band_mode) {
            intgame_hud_slide_prepare_offscreen();
        }
        // Make sure the bar windows are tig-shown — band mode
        // tig_window_hide'd the top, and slide-show animates the
        // position only.
        if (!intgame_is_compact_interface()) {
            for (index = 0; index < 2; index++) {
                tig_window_show(dword_64C4F8[index]);
            }
        }
        intgame_hud_slide_show();
    } else {
        if (!intgame_is_compact_interface()) {
            for (index = 0; index < 2; index++) {
                tig_window_show(dword_64C4F8[index]);
            }
        }
        TigRect rect = intgame_interface_window_frames[1];
        hrp_apply(&rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_BOTTOM);
        tig_window_move(dword_64C4F8[1], rect.x, rect.y);
    }

    if (intgame_fs_hotkey_window != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_show(intgame_fs_hotkey_window);
    }

    for (index = 0; index < 5; index++) {
        if (spell_ui_maintain_has(index)) {
            tig_window_show(intgame_maintain_fs_windows[index]);
        }
    }

    follower_ui_show();

    // CE: Re-apply the current TAB stage's clip after a modal menu
    // cycle (Esc main menu, Options, Save/Load) — intgame_show is
    // called on close and would otherwise leave the strips fully
    // visible regardless of the user's TAB stage.
    intgame_hud_apply_clips();
}

// CE: Set the bottom bar's clip rect to the band for `render_stage`,
// positioned in screen coords relative to (bar_x, bar_y) — the bar
// window's current frame origin. Both apply_clips (on stage change)
// and intgame_hud_ping (every frame during a slide) call this so
// the crop tracks the bar's y as it animates between rest and off-
// screen. chrome_strip_y is recomputed inside so it picks up the
// active rotwin's per-type anchor offset.
static void intgame_hud_bottom_set_clip(int bar_x,
    int bar_y,
    IntgameHudStage render_stage)
{
    if (dword_64C4F8[1] == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    int chrome_strip_y = INTGAME_HUD_MEDIUM_BAND_Y;
    if (intgame_iso_window_type >= 0
        && intgame_iso_window_type < ROTWIN_TYPE_COUNT) {
        int btn_y = intgame_rotwin_button_info[intgame_iso_window_type].y;
        if (btn_y > intgame_interface_window_frames[1].y) {
            chrome_strip_y = btn_y - intgame_interface_window_frames[1].y;
        }
    }
    TigRect band;
    switch (render_stage) {
    case INTGAME_HUD_STAGE_FULL:
        tig_window_clip_rect_set(dword_64C4F8[1], NULL);
        break;
    case INTGAME_HUD_STAGE_MEDIUM:
        band.x = bar_x + INTGAME_HUD_MEDIUM_BAND_X;
        band.y = bar_y + chrome_strip_y;
        band.width = INTGAME_HUD_MEDIUM_BAND_W;
        band.height = INTGAME_HUD_MEDIUM_BAND_H;
        tig_window_clip_rect_set(dword_64C4F8[1], &band);
        break;
    case INTGAME_HUD_STAGE_MINI:
        band.x = bar_x + INTGAME_HUD_MINI_BAND_X;
        band.y = bar_y + chrome_strip_y
            + (INTGAME_HUD_MINI_BAND_Y - INTGAME_HUD_MEDIUM_BAND_Y);
        band.width = INTGAME_HUD_MINI_BAND_W;
        band.height = INTGAME_HUD_MINI_BAND_H;
        tig_window_clip_rect_set(dword_64C4F8[1], &band);
        break;
    case INTGAME_HUD_STAGE_HIDDEN:
    default: {
        TigRect empty = { 0, 0, 0, 0 };
        tig_window_clip_rect_set(dword_64C4F8[1], &empty);
        break;
    }
    }
}

// Compute and install the per-strip clip rects for the current
// stage. Idempotent — safe to call from any path that may have
// reset the strips (intgame_show, mode exits, etc.).
static void intgame_hud_apply_clips(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    intgame_hud_user_hidden = (intgame_hud_stage != INTGAME_HUD_STAGE_FULL);

    // CE: invalidate the world (iso VB) under both bar strips on every
    // stage change. The iso layer only paints into its VB for regions
    // gamelib has been asked to redraw; while a strip is opaque on top,
    // iso leaves that band stale. When the clip uncovers part of the
    // strip, the compositor blits iso's stale pixels through the hole
    // and the user sees an edge smear. Invalidating here forces the
    // world to repaint into iso's VB so the newly-exposed band has
    // fresh content by the next composite.
    for (int strip_idx = 0; strip_idx < 2; strip_idx++) {
        if (dword_64C4F8[strip_idx] == TIG_WINDOW_HANDLE_INVALID) {
            continue;
        }
        TigWindowData strip_wd;
        if (tig_window_data(dword_64C4F8[strip_idx], &strip_wd) == TIG_OK) {
            // CE: for the top strip, the slide tween may have left
            // the window's current frame offscreen (y=-41). Iso
            // needs to repaint the bar's REST screen area (y=0..41)
            // so the slide-in animation reveals fresh pixels. We
            // compose a rect at y=0 using the strip's width/x but
            // ignoring its current y.
            TigRect inv = strip_wd.rect;
            if (strip_idx == 0) {
                inv.y = 0;
                inv.height = INTGAME_HUD_TOP_HEIGHT;
            }
            iso_invalidate_rect(&inv);
        }
    }

    // Top strip: visibility is now driven by a tweened slide rather
    // than a clip-rect on/off. The strip's window y position is
    // springed toward 0 (visible) when FULL, -INTGAME_HUD_TOP_HEIGHT
    // (off the top edge) for any cropped stage. intgame_hud_ping
    // pumps tig_window_move every frame from the integrated offset.
    // We always leave the clip-rect cleared so the moving frame
    // composites normally at whatever screen position the tween has
    // it at this instant.
    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID
        && !intgame_hud_shell_hidden) {
        tig_window_clip_rect_set(dword_64C4F8[0], NULL);
        int target = (intgame_hud_stage == INTGAME_HUD_STAGE_FULL)
            ? 0
            : -INTGAME_HUD_TOP_HEIGHT;
        ui_anim_cancel(intgame_hud_top_slide_handle);
        intgame_hud_top_slide_handle = ui_anim_int_to(
            &intgame_hud_top_slide_offset, target,
            &UI_ANIM_PROFILE_DEFAULT_SLIDE);
    }

    // Bottom strip: stage-specific band.
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
        TigWindowData wd;
        if (tig_window_data(dword_64C4F8[1], &wd) != TIG_OK) {
            return;
        }
        // CE: keep "last visible stage" current so the slide-down
        // into HIDDEN can keep rendering whatever band the user
        // last saw — the whole band moves off-screen as a unit
        // along with the bar.
        if (intgame_hud_stage != INTGAME_HUD_STAGE_HIDDEN) {
            intgame_hud_last_visible_stage = intgame_hud_stage;
        }
        IntgameHudStage render_stage =
            (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN)
            ? intgame_hud_last_visible_stage
            : intgame_hud_stage;

        // Install the band clip at the bar's current screen y.
        // intgame_hud_ping re-installs it every frame the slide
        // changes position so the crop follows the bar.
        intgame_hud_bottom_set_clip(wd.rect.x, wd.rect.y, render_stage);

        // CE: HIDDEN ↔ visible transition for the bottom bar.
        //   - Entering HIDDEN: slide the bar DOWN with its current
        //     band clip in tow (render_stage above is the previous
        //     visible stage) — the whole band moves off-screen as
        //     a unit.
        //   - Leaving HIDDEN: snap to below-screen (in case ping
        //     hasn't run yet), then slide UP to rest in sync with
        //     the top bar's slide-down. render_stage is the new
        //     stage, so the slide reveals the destination band.
        // MEDIUM/MINI cycling (no HIDDEN involved) skips this —
        // those are just clip-rect swaps, no slide.
        static IntgameHudStage s_prev_stage = INTGAME_HUD_STAGE_FULL;
        bool was_hidden = (s_prev_stage == INTGAME_HUD_STAGE_HIDDEN);
        bool now_hidden = (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN);

        if (was_hidden != now_hidden
            && !intgame_hud_shell_hidden) {
            int rest_y = intgame_hud_bottom_rest_y_compute();
            // Stop any in-flight slide so the previous direction's
            // motion doesn't fight us.
            ui_anim_cancel(intgame_hud_bottom_slide_handle);
            intgame_hud_bottom_slide_handle = UI_ANIM_HANDLE_INVALID;

            if (now_hidden) {
                // Slide DOWN from rest to off-screen below. Ping
                // moves the band clip along with the bar each
                // frame so the whole band rides off-screen.
                intgame_hud_bottom_slide_offset = 0;
                intgame_hud_bottom_slide_handle = ui_anim_int_to(
                    &intgame_hud_bottom_slide_offset,
                    INTGAME_HUD_BOTTOM_H,
                    &UI_ANIM_PROFILE_DEFAULT_SLIDE);
            } else {
                // Snap below-screen first (in case ping hadn't
                // moved it yet), then slide UP to rest.
                intgame_hud_bottom_slide_offset = INTGAME_HUD_BOTTOM_H;
                tig_window_move(dword_64C4F8[1], wd.rect.x,
                    rest_y + INTGAME_HUD_BOTTOM_H);
                intgame_hud_bottom_set_clip(wd.rect.x,
                    rest_y + INTGAME_HUD_BOTTOM_H,
                    render_stage);
                intgame_hud_bottom_slide_handle = ui_anim_int_to(
                    &intgame_hud_bottom_slide_offset, 0,
                    &UI_ANIM_PROFILE_DEFAULT_SLIDE);
            }
        }
        s_prev_stage = intgame_hud_stage;
    }

    // Reposition top-bar-docked panels so they sit flush against the
    // screen top when the bar is hidden, or below the bar when it's
    // shown. No-op if the respective panel isn't currently open.
    fate_ui_reposition();
    sleep_ui_reposition();

    // Notify the dialog options backdrop of the new bar-gap so it can
    // drop down into the freed space. No-op when no dialog is active.
    tc_set_bottom_gap_offset(intgame_hud_bottom_gap_offset());
}

void intgame_hud_user_toggle(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    // Manual TAB cycle = user override; drop any auto-pop snapshot so
    // a later rotwin dismiss won't snap them back to a stage they've
    // since moved past.
    intgame_hud_has_saved_stage = false;
    intgame_hud_peek_from_mini = false;
    IntgameHudStage prev_stage = intgame_hud_stage;
    intgame_hud_stage = (intgame_hud_stage + 1) % INTGAME_HUD_STAGE_COUNT;

    // CE: MINI's only purpose is the slim rollover row — and that row
    // is driven by whichever rotwin is currently active. On the way
    // INTO MINI, silently force the SKILLS rotwin so the row shows
    // skill names / hover text. On the way OUT of MINI (cycling to
    // HIDDEN or wrapping back to FULL), drop the rotwin back to MSG
    // so we don't leave SKILLS arbitrarily active afterwards. Use
    // iso_interface_window_swap directly (not _set) to skip the
    // K/M-style auto-pop-to-MEDIUM hook — we want the type change
    // without the stage override. The swap itself re-applies clips,
    // so we skip the manual apply below in those branches.
    bool entering_mini = (intgame_hud_stage == INTGAME_HUD_STAGE_MINI
        && prev_stage != INTGAME_HUD_STAGE_MINI);
    bool leaving_mini = (prev_stage == INTGAME_HUD_STAGE_MINI
        && intgame_hud_stage != INTGAME_HUD_STAGE_MINI);

    // CE: collapse-transition "wings slide down" ghosts. Each step where
    // the bar shrinks — FULL -> MEDIUM here, MEDIUM -> MINI just below —
    // spawns a snapshot of the departing (larger) bar that slides + fades
    // down behind the new, smaller band, so the chrome that's lost reads
    // as sliding off rather than blinking away. Spawned BEFORE apply_clips
    // / the rotwin swap below so the snapshot captures the bar while it's
    // still at the larger stage. The expanding transitions (MEDIUM -> FULL,
    // MINI -> MEDIUM) and HIDDEN just re-show their chrome directly.
    if (prev_stage == INTGAME_HUD_STAGE_FULL
        && intgame_hud_stage == INTGAME_HUD_STAGE_MEDIUM) {
        intgame_hud_ghost_slide_down();
    }
    // CE: MEDIUM -> MINI. Snapshot the MEDIUM band BEFORE the
    // entering_mini block below swaps the rotwin to SKILLS (which
    // redraws the bar VB) — so the ghost captures the departing
    // MEDIUM content, then slides+fades it down behind the new MINI
    // slim row.
    if (prev_stage == INTGAME_HUD_STAGE_MEDIUM
        && intgame_hud_stage == INTGAME_HUD_STAGE_MINI) {
        intgame_hud_ghost_med_to_mini();
    }

    if (entering_mini) {
        intgame_hud_enter_mini_with_skills();
        // Fall through to the populate hook below so MINI's slim row
        // gets the current context immediately (during dialogue, the
        // speaker NPC's name; out of dialogue, the natural hover/
        // target info) instead of waiting for the next rollover.
    } else if (leaving_mini) {
        // Sync the secondary buttons back to RELEASED so the next K
        // / M press behaves as an activation, not a dismiss-of-stale.
        tig_button_state_change(
            intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle,
            TIG_BUTTON_STATE_RELEASED);
        tig_button_state_change(
            intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle,
            TIG_BUTTON_STATE_RELEASED);
        if (intgame_iso_window_type != ROTWIN_TYPE_MSG) {
            iso_interface_window_swap(ROTWIN_TYPE_MSG);
        }
    }
    intgame_hud_apply_clips();

    // CE: after any TAB transition that leaves the bar in a visible
    // stage (FULL, MEDIUM, or MINI — anything except HIDDEN), re-
    // populate the rotwin content. Without this, switching stages
    // leaves the rotwin showing whatever was last written (or blank
    // if nothing) until the next mouse rollover or speaker focus
    // event fires the natural refresh. During dialogue, push the
    // active speaker NPC explicitly so "talking to X" reappears
    // immediately; the examine path routes correctly for whatever
    // rotwin type is active (MSG → portrait+text, SKILLS/etc → str
    // into the slim row via display_str). Out of dialogue, fall
    // back to the natural target/hover refresh.
    if (intgame_hud_stage != INTGAME_HUD_STAGE_HIDDEN) {
        int64_t dialog_npc = dialog_ui_get_local_pc_npc_obj();
        if (dialog_npc != OBJ_HANDLE_NULL) {
            int64_t pc_obj = player_get_local_pc_obj();
            if (pc_obj != OBJ_HANDLE_NULL) {
                char buffer[2000];
                object_examine(dialog_npc, pc_obj, buffer);
                intgame_examine_object(pc_obj, dialog_npc, buffer);
                object_hover_obj_set(dialog_npc);
            }
        } else if (intgame_iso_window_type == ROTWIN_TYPE_MSG) {
            iso_interface_refresh();
        }
    }
}

// Auto-pop to MEDIUM when the user invokes a rotwin (K/M/etc.)
// while the HUD is in MINI or HIDDEN. Called from the existing
// iso_interface_window_set hook.
void intgame_hud_auto_pop_for_rotwin(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (intgame_hud_stage == INTGAME_HUD_STAGE_MINI
        || intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN) {
        // Stash the user's pre-rotwin stage so dismissing the rotwin
        // can return to it. Don't overwrite an existing snapshot — a
        // rotwin-to-rotwin switch (SKILLS->SPELLS) is still part of
        // the same pop, and we want to restore the *original* state.
        if (!intgame_hud_has_saved_stage) {
            intgame_hud_saved_stage = intgame_hud_stage;
            intgame_hud_has_saved_stage = true;
        }
        intgame_hud_stage = INTGAME_HUD_STAGE_MEDIUM;
        intgame_hud_apply_clips();
    }
}

// Restore the pre-rotwin stage stashed by intgame_hud_auto_pop_for_rotwin.
// Called when the rotwin returns to MSG (user re-pressed the dismiss key,
// hovered off, magictech weapon cleared, etc.). No-op if nothing was
// stashed or the interface isn't up.
void intgame_hud_restore_after_rotwin(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (!intgame_hud_has_saved_stage) {
        return;
    }
    IntgameHudStage restored = intgame_hud_saved_stage;
    intgame_hud_has_saved_stage = false;
    if (restored == INTGAME_HUD_STAGE_MINI) {
        // Use the centralized MINI-invariant helper so type, stage,
        // and button state all line up.
        intgame_hud_enter_mini_with_skills();
    } else {
        intgame_hud_stage = restored;
        intgame_hud_apply_clips();
    }
}

// CE: enforce MINI invariants. MINI's whole point is "SKILLS rotwin
// silently active so the slim row shows skill hover content". Any
// path that lands us in MINI must call this so the rotwin and the
// secondary button state stay in sync — TAB-into-MINI, MEDIUM-peek
// return, dismiss-while-peeked, etc.
static bool intgame_hud_in_mini_stage(void)
{
    return intgame_iso_interface_created
        && intgame_hud_stage == INTGAME_HUD_STAGE_MINI;
}

static void intgame_hud_enter_mini_with_skills(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    intgame_hud_stage = INTGAME_HUD_STAGE_MINI;
    if (intgame_iso_window_type != ROTWIN_TYPE_SKILLS) {
        tig_button_state_change(
            intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SPELLS].button_handle,
            TIG_BUTTON_STATE_RELEASED);
        tig_button_state_change(
            intgame_secondary_buttons[INTGAME_SECONDARY_BUTTON_SKILLS].button_handle,
            TIG_BUTTON_STATE_PRESSED);
        // swap re-applies clips for us.
        iso_interface_window_swap(ROTWIN_TYPE_SKILLS);
    } else {
        intgame_hud_apply_clips();
    }
}

static bool intgame_hud_handle_mini_peek_press(void)
{
    if (!intgame_iso_interface_created) {
        return false;
    }
    // MINI -> MEDIUM peek: rotwin stays active, only the crop expands.
    if (intgame_hud_stage == INTGAME_HUD_STAGE_MINI) {
        intgame_hud_peek_from_mini = true;
        intgame_hud_stage = INTGAME_HUD_STAGE_MEDIUM;
        intgame_hud_apply_clips();
        return true;
    }
    // MEDIUM (peeked) -> MINI return: same condition, opposite direction.
    // Re-enter MINI invariants so SKILLS is active even if the user
    // switched the rotwin to SPELLS while peeking.
    if (intgame_hud_peek_from_mini
        && intgame_hud_stage == INTGAME_HUD_STAGE_MEDIUM) {
        intgame_hud_peek_from_mini = false;
        intgame_hud_enter_mini_with_skills();
        return true;
    }
    return false;
}

bool intgame_hud_is_user_hidden(void)
{
    return intgame_hud_user_hidden;
}

void intgame_hud_promote_top_strip(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (dword_64C4F8[0] == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    tig_window_move_on_top(dword_64C4F8[0]);
}

void intgame_hud_promote_bottom_strip(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (dword_64C4F8[1] == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    tig_window_move_on_top(dword_64C4F8[1]);
}

tig_window_handle_t intgame_get_band_bar_handle(void)
{
    if (!intgame_iso_interface_created) {
        return TIG_WINDOW_HANDLE_INVALID;
    }
    return dword_64C4F8[1];
}

bool intgame_hud_is_band_mode(void)
{
    return intgame_hud_band_mode;
}

// ===========================================================================
// CE: FULL -> MEDIUM "wings slide down" ghost.
//
// MEDIUM crops the bottom bar to a centered band (rotwin + message
// area) via a clip rect — the left/right chrome "wings" just vanish.
// To make that read as the wings sliding DOWN off the bottom, we can't
// slide the real bar window: the rotwin text is drawn into that same
// window, so it would slide too. Instead, on the FULL->MEDIUM step we:
//
//   1. snapshot the live FULL bar (chrome + current rotwin) into a
//      throwaway "ghost" window at the bar's rest position,
//   2. promote the REAL bar above the ghost, then switch the real bar
//      to its MEDIUM band clip (band + rotwin, static, in front),
//   3. slide the ghost DOWN behind the real bar and destroy it on
//      settle.
//
// Because the real band sits in FRONT, it occludes the ghost's center
// — the viewer only sees the ghost's WINGS sliding down past the
// persistent band. No inverse-clip needed, and the rotwin never moves.
// One ghost at a time; a new spawn replaces any in-flight one (rapid
// TAB cycling). Skipped entirely when UI animations are disabled.
// ===========================================================================
static tig_window_handle_t intgame_hud_ghost_window = TIG_WINDOW_HANDLE_INVALID;
static int intgame_hud_ghost_slide_offset = 0;
static int intgame_hud_ghost_last_applied = INT_MIN;
static int intgame_hud_ghost_rest_x = 0;
static int intgame_hud_ghost_rest_y = 0;
static int intgame_hud_ghost_slide_distance = 0;
static ui_anim_handle_t intgame_hud_ghost_handle = UI_ANIM_HANDLE_INVALID;
static bool intgame_hud_ghost_pending_destroy = false;
// CE: optional animated "morph" on the ghost (MED->MINI). Each frame
// the visible footprint is lerped (by slide progress) from the MEDIUM
// band toward the MINI band: bottom rises to the MINI crop's bottom,
// width narrows MED->MINI, top rides the sliding window. The crop is
// done at the VB LEVEL (clear ghost VB to the transparent key, then
// re-blit just the morphing sub-region from a pristine snapshot)
// rather than via a tig clip rect — that keeps the window clip-free so
// the transform/fade path stays usable (the compositor disables
// transform on any clipped window). The ghost composites in FRONT (the
// transform/deferred path always paints last); for MED->MINI that's
// intentional — it fades out over the real MINI row. Screen-coord
// endpoints.
static bool intgame_hud_ghost_clip_morph = false;
static int intgame_hud_ghost_clip_x0 = 0;
static int intgame_hud_ghost_clip_x1 = 0;
static int intgame_hud_ghost_clip_w0 = 0;
static int intgame_hud_ghost_clip_w1 = 0;
static int intgame_hud_ghost_clip_bottom0 = 0;
static int intgame_hud_ghost_clip_bottom1 = 0;
// Top inset (ghost-local px shaved off the band's top), lerped over
// the slide. A few px of top recession makes the band read as
// collapsing from both edges rather than just sliding off the bottom.
static int intgame_hud_ghost_clip_top0 = 0;
static int intgame_hud_ghost_clip_top1 = 0;
// Pristine band snapshot (morph source), the bar's transparent key,
// and the ghost dimensions — used to rebuild the morphing crop each
// frame.
static TigVideoBuffer* intgame_hud_ghost_src_vb = NULL;
static tig_color_t intgame_hud_ghost_key_color = 0;
static int intgame_hud_ghost_width = 0;
static int intgame_hud_ghost_height = 0;

static void intgame_hud_ghost_destroy(void)
{
    if (intgame_hud_ghost_handle != UI_ANIM_HANDLE_INVALID) {
        ui_anim_cancel(intgame_hud_ghost_handle);
        intgame_hud_ghost_handle = UI_ANIM_HANDLE_INVALID;
    }
    if (intgame_hud_ghost_window != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_destroy(intgame_hud_ghost_window);
        intgame_hud_ghost_window = TIG_WINDOW_HANDLE_INVALID;
    }
    if (intgame_hud_ghost_src_vb != NULL) {
        tig_video_buffer_destroy(intgame_hud_ghost_src_vb);
        intgame_hud_ghost_src_vb = NULL;
    }
    intgame_hud_ghost_pending_destroy = false;
    intgame_hud_ghost_last_applied = INT_MIN;
    intgame_hud_ghost_slide_offset = 0;
    intgame_hud_ghost_clip_morph = false;
}

// ui_anim on_complete: defer the actual window destroy to the next
// ghost ping (don't tear down a tig window from inside the ui_anim
// integration loop).
static void intgame_hud_ghost_on_settle(void* ctx)
{
    (void)ctx;
    intgame_hud_ghost_pending_destroy = true;
}

// Core spawn: snapshot a sub-region of the bottom bar (bar-local
// coords) into a same-sized ghost window at that region's rest screen
// position, promote the real bar above it, then slide the ghost DOWN
// by slide_distance. Destroyed on settle. One ghost at a time.
// (Clip-morph, if any, is configured by the caller after this returns.)
// transparent: create the ghost with TIG_WINDOW_TRANSPARENT so its VB
// gets an SDL surface color key. Required when the ghost will FADE
// (transform path) AND have key-filled regions (the morph crop): the
// fade blit (SDL_BlitSurfaceScaled) only keys via the surface key, so
// without this the key-filled margins composite opaque (a coloured /
// "white" fill). The plain-slide ghost (FULL->MEDIUM) doesn't fade and
// keys via the normal manual-key blit, so it passes false.
static void intgame_hud_ghost_spawn(TigRect region_local, int slide_distance,
    bool transparent)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    if (dword_64C4F8[1] == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    // Animations off → no ghost; the stage just snaps as before.
    if (!settings_get_value(&settings, UI_ANIMATIONS_KEY)) {
        return;
    }

    // Replace any in-flight ghost (rapid TAB cycling).
    intgame_hud_ghost_destroy();

    TigWindowData bar_wd;
    if (tig_window_data(dword_64C4F8[1], &bar_wd) != TIG_OK) {
        return;
    }

    int rest_y = intgame_hud_bottom_rest_y_compute();

    TigWindowData ghost_wd;
    ghost_wd.flags = transparent ? TIG_WINDOW_TRANSPARENT : 0;
    ghost_wd.rect.x = bar_wd.rect.x + region_local.x;
    ghost_wd.rect.y = rest_y + region_local.y; // region's rest screen y
    ghost_wd.rect.width = region_local.width;
    ghost_wd.rect.height = region_local.height;
    ghost_wd.background_color = bar_wd.background_color;
    ghost_wd.color_key = bar_wd.color_key;
    ghost_wd.message_filter = NULL;
    if (tig_window_create(&ghost_wd, &intgame_hud_ghost_window) != TIG_OK) {
        intgame_hud_ghost_window = TIG_WINDOW_HANDLE_INVALID;
        return;
    }

    // Snapshot the region from the live bar VB into the ghost
    // (dst is ghost-local {0,0,w,h}; src is the bar-local region).
    TigRect dst = { 0, 0, region_local.width, region_local.height };
    tig_window_copy(intgame_hud_ghost_window, &dst, dword_64C4F8[1], &region_local);

    // For the plain-slide ghost (FULL->MEDIUM): promote the REAL bar
    // above it so the ghost sits BEHIND (real band occludes the
    // ghost's center, only the departing chrome shows). The fading
    // morph ghost (MED->MINI) intentionally composites in FRONT via
    // the transform path; the promote is a harmless no-op there.
    tig_window_move_on_top(dword_64C4F8[1]);

    intgame_hud_ghost_rest_x = ghost_wd.rect.x;
    intgame_hud_ghost_rest_y = ghost_wd.rect.y;
    intgame_hud_ghost_width = region_local.width;
    intgame_hud_ghost_height = region_local.height;
    intgame_hud_ghost_key_color = bar_wd.color_key;
    intgame_hud_ghost_slide_offset = 0;
    intgame_hud_ghost_last_applied = INT_MIN;
    intgame_hud_ghost_pending_destroy = false;
    intgame_hud_ghost_clip_morph = false;
    intgame_hud_ghost_slide_distance = slide_distance;
    intgame_hud_ghost_handle = ui_anim_int_to_with_complete(
        &intgame_hud_ghost_slide_offset,
        slide_distance,
        &UI_ANIM_PROFILE_DEFAULT_SLIDE,
        intgame_hud_ghost_on_settle,
        NULL);
}

// FULL -> MEDIUM: slide the whole full-bar snapshot down (no clip
// morph); the wings clear the bottom edge while the real MEDIUM band
// holds in front.
static void intgame_hud_ghost_slide_down(void)
{
    TigRect full = { 0, 0, INTGAME_HUD_BOTTOM_W, INTGAME_HUD_BOTTOM_H };
    intgame_hud_ghost_spawn(full, INTGAME_HUD_BOTTOM_H, /*transparent=*/false);
}

// MEDIUM -> MINI: snapshot the MEDIUM band and slide it DOWN over the
// real MINI slim row while morphing toward the MINI footprint (bottom
// rises to the MINI crop's bottom, width narrows MED->MINI) and fading
// out. The crop is done at the VB level so the window stays clip-free
// and the transform fade is usable; the ghost is created TRANSPARENT so
// its VB has a surface key (the fade blit only keys via that). Composites
// in front and fades out over the real MINI row.
static void intgame_hud_ghost_med_to_mini(void)
{
    // Match intgame_hud_bottom_set_clip's MEDIUM band y: default
    // INTGAME_HUD_MEDIUM_BAND_Y, shifted to the active rotwin button's
    // row when that sits below the bar's top edge.
    int chrome_strip_y = INTGAME_HUD_MEDIUM_BAND_Y;
    if (intgame_iso_window_type >= 0
        && intgame_iso_window_type < ROTWIN_TYPE_COUNT) {
        int btn_y = intgame_rotwin_button_info[intgame_iso_window_type].y;
        if (btn_y > intgame_interface_window_frames[1].y) {
            chrome_strip_y = btn_y - intgame_interface_window_frames[1].y;
        }
    }
    TigRect band = {
        INTGAME_HUD_MEDIUM_BAND_X,
        chrome_strip_y,
        INTGAME_HUD_MEDIUM_BAND_W,
        INTGAME_HUD_MEDIUM_BAND_H,
    };

    // Slide the band-top down to the MINI band's top, so at settle the
    // visible region (top=window, bottom=MINI crop bottom) equals the
    // MINI band footprint. MINI top is MEDIUM-relative, matching
    // intgame_hud_bottom_set_clip's MINI offset.
    int slide_distance = INTGAME_HUD_MINI_BAND_Y - INTGAME_HUD_MEDIUM_BAND_Y;
    if (slide_distance < 1) slide_distance = 1;

    intgame_hud_ghost_spawn(band, slide_distance, /*transparent=*/true);

    if (intgame_hud_ghost_window == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }

    // Pristine band snapshot to rebuild the morphing crop from each
    // frame (the ghost VB itself gets overwritten by the per-frame
    // fill+reblit).
    TigVideoBufferCreateInfo vb_info;
    vb_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY
        | TIG_VIDEO_BUFFER_CREATE_COLOR_KEY;
    vb_info.width = INTGAME_HUD_MEDIUM_BAND_W;
    vb_info.height = INTGAME_HUD_MEDIUM_BAND_H;
    vb_info.background_color = intgame_hud_ghost_key_color;
    vb_info.color_key = intgame_hud_ghost_key_color;
    if (tig_video_buffer_create(&vb_info, &intgame_hud_ghost_src_vb) != TIG_OK) {
        intgame_hud_ghost_src_vb = NULL;
        return; // falls back to plain slide (no morph/fade)
    }
    TigRect band_dst = { 0, 0, INTGAME_HUD_MEDIUM_BAND_W, INTGAME_HUD_MEDIUM_BAND_H };
    tig_window_copy_to_vbuffer(intgame_hud_ghost_window, &band_dst,
        intgame_hud_ghost_src_vb, &band_dst);

    // Configure the screen-space morph endpoints. bar_x / bar_rest_y are
    // recovered from the ghost's rest position and the band offsets.
    int bar_x = intgame_hud_ghost_rest_x - INTGAME_HUD_MEDIUM_BAND_X;
    int bar_rest_y = intgame_hud_ghost_rest_y - chrome_strip_y;
    // MINI crop bottom in screen coords — matches set_clip exactly:
    // chrome_strip_y + (MINI_Y - MEDIUM_Y) + MINI_H.
    int mini_bottom_screen = bar_rest_y + chrome_strip_y
        + (INTGAME_HUD_MINI_BAND_Y - INTGAME_HUD_MEDIUM_BAND_Y)
        + INTGAME_HUD_MINI_BAND_H;
    intgame_hud_ghost_clip_morph = true;
    // Width: MED band width -> MINI band width.
    intgame_hud_ghost_clip_w0 = INTGAME_HUD_MEDIUM_BAND_W;
    intgame_hud_ghost_clip_w1 = INTGAME_HUD_MINI_BAND_W;
    // X (screen): MED band left -> MINI band left (MINI sits inward).
    intgame_hud_ghost_clip_x0 = bar_x + INTGAME_HUD_MEDIUM_BAND_X;
    intgame_hud_ghost_clip_x1 = bar_x + INTGAME_HUD_MINI_BAND_X;
    // Bottom (screen): ghost's own bottom (at rest) -> MINI crop bottom.
    intgame_hud_ghost_clip_bottom0 =
        intgame_hud_ghost_rest_y + INTGAME_HUD_MEDIUM_BAND_H;
    intgame_hud_ghost_clip_bottom1 = mini_bottom_screen;
    // Top inset (ghost-local): shave a few px off the band top over the
    // slide so the top edge recedes too.
    intgame_hud_ghost_clip_top0 = 0;
    intgame_hud_ghost_clip_top1 = 4;
}

// Per-frame: move the ghost to its tweened y, drive its clip morph (if
// any), and reap it once the slide has settled. Called from
// intgame_hud_ping.
static void intgame_hud_ghost_ping(void)
{
    if (intgame_hud_ghost_window == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    if (intgame_hud_ghost_pending_destroy) {
        intgame_hud_ghost_destroy();
        return;
    }
    if (intgame_hud_ghost_slide_offset != intgame_hud_ghost_last_applied) {
        intgame_hud_ghost_last_applied = intgame_hud_ghost_slide_offset;
        int cur_y = intgame_hud_ghost_rest_y + intgame_hud_ghost_slide_offset;
        tig_window_move(intgame_hud_ghost_window,
            intgame_hud_ghost_rest_x, cur_y);

        if (intgame_hud_ghost_clip_morph
            && intgame_hud_ghost_slide_distance > 0
            && intgame_hud_ghost_src_vb != NULL) {
            float t = (float)intgame_hud_ghost_slide_offset
                / (float)intgame_hud_ghost_slide_distance;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            // Morph rect, SCREEN coords: top rides the sliding window;
            // bottom lerps toward the MINI crop bottom; width narrows
            // MED->MINI; x drifts MED->MINI.
            int x_screen = intgame_hud_ghost_clip_x0
                + (int)((intgame_hud_ghost_clip_x1
                    - intgame_hud_ghost_clip_x0) * t + 0.5f);
            int w = intgame_hud_ghost_clip_w0
                + (int)((intgame_hud_ghost_clip_w1
                    - intgame_hud_ghost_clip_w0) * t + 0.5f);
            int bottom_screen = intgame_hud_ghost_clip_bottom0
                + (int)((intgame_hud_ghost_clip_bottom1
                    - intgame_hud_ghost_clip_bottom0) * t + 0.5f);
            int top_inset = intgame_hud_ghost_clip_top0
                + (int)((intgame_hud_ghost_clip_top1
                    - intgame_hud_ghost_clip_top0) * t + 0.5f);

            // Convert to ghost-local (window x is fixed). top_inset
            // shaves the band top; crop bottom tracks the MINI crop.
            int crop_x = x_screen - intgame_hud_ghost_rest_x;
            int crop_y = top_inset;
            int crop_h = bottom_screen - (cur_y + top_inset);
            if (crop_x < 0) crop_x = 0;
            if (crop_y < 0) crop_y = 0;
            if (w > intgame_hud_ghost_width - crop_x) {
                w = intgame_hud_ghost_width - crop_x;
            }
            if (crop_h < 0) crop_h = 0;
            if (crop_h > intgame_hud_ghost_height - crop_y) {
                crop_h = intgame_hud_ghost_height - crop_y;
            }

            // Rebuild the ghost VB: clear to the transparent key, then
            // re-blit only the morphing sub-region from the pristine
            // snapshot. The VB has a surface key (TRANSPARENT window),
            // so the fade blit below keys the cleared margins out.
            TigRect full_local = {
                0, 0, intgame_hud_ghost_width, intgame_hud_ghost_height
            };
            tig_window_fill(intgame_hud_ghost_window, &full_local,
                intgame_hud_ghost_key_color);
            if (w > 0 && crop_h > 0) {
                TigRect crop = { crop_x, crop_y, w, crop_h };
                tig_window_copy_from_vbuffer(intgame_hud_ghost_window, &crop,
                    intgame_hud_ghost_src_vb, &crop);
            }

            // Fade out over the slide.
            float alpha = 1.0f - t;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            tig_window_transform_set(intgame_hud_ghost_window,
                1.0f, 1.0f, alpha, 0.5f, 0.5f);
        }

        // Safety: once the slide has reached its target, reap the ghost
        // even if the settle callback is delayed.
        if (intgame_hud_ghost_slide_offset >= intgame_hud_ghost_slide_distance) {
            intgame_hud_ghost_pending_destroy = true;
        }
    }
}

// CE: slide both HUD bars off-screen, ignoring TAB stage. Used by
// shell menus (Options / Save / Load / ESC pause) that previously
// hid the bars instantly via tig_window_hide — sliding looks more
// "physical" and matches the bg's own entrance/exit timing.
//
// The bars stay tig-shown (not tig_window_hide) so the slide is
// actually visible. The captured rest_y + bottom_slide_offset
// mechanism in intgame_hud_ping handles the per-frame move; setting
// the shell_hidden flag pauses the TAB-driven slide retargets that
// would otherwise fight us.
//
// Idempotent — safe to call from multiple paths converging on the
// same menu open. Also resets the bottom bar's render_stage anchor
// to "last visible" so the band crop slides off with the bar.
void intgame_hud_slide_hide(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }

    // CE: if we're coming out of band mode (bar parked mid-screen
    // as chargen panel chrome), a slide would JUMP from mid-screen
    // to bottom rest before animating off — visible flash. Snap
    // to off-screen instead and let the chargen window's own exit
    // animation handle the visual transition.
    bool was_band = intgame_hud_band_mode;
    intgame_hud_band_mode = false;
    intgame_hud_shell_hidden = true;

    ui_anim_cancel(intgame_hud_top_slide_handle);
    ui_anim_cancel(intgame_hud_bottom_slide_handle);
    intgame_hud_top_slide_handle = UI_ANIM_HANDLE_INVALID;
    intgame_hud_bottom_slide_handle = UI_ANIM_HANDLE_INVALID;

    if (was_band) {
        // Snap straight to off-screen, no animation. Sync
        // last_applied so ping doesn't re-fire on the next frame.
        intgame_hud_top_slide_offset = -INTGAME_HUD_TOP_HEIGHT;
        intgame_hud_bottom_slide_offset = INTGAME_HUD_BOTTOM_H;
        intgame_hud_top_last_applied = intgame_hud_top_slide_offset;
        intgame_hud_bottom_last_applied = intgame_hud_bottom_slide_offset;
        if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID) {
            TigWindowData wd;
            if (tig_window_data(dword_64C4F8[0], &wd) == TIG_OK) {
                tig_window_move(dword_64C4F8[0], wd.rect.x,
                    -INTGAME_HUD_TOP_HEIGHT);
            }
            // band mode tig_window_hide'd the top earlier; keep
            // it consistent — re-show so the slide-show later
            // doesn't reveal an invisible window at rest_y.
            tig_window_show(dword_64C4F8[0]);
        }
        if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
            TigWindowData wd;
            if (tig_window_data(dword_64C4F8[1], &wd) == TIG_OK) {
                tig_window_move(dword_64C4F8[1], wd.rect.x,
                    intgame_hud_bottom_rest_y_compute()
                        + INTGAME_HUD_BOTTOM_H);
            }
        }
        return;
    }

    // Normal slide path — bar was at TAB-rest or already mid-slide.
    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID) {
        intgame_hud_top_slide_handle = ui_anim_int_to(
            &intgame_hud_top_slide_offset,
            -INTGAME_HUD_TOP_HEIGHT,
            &UI_ANIM_PROFILE_DEFAULT_SLIDE);
    }
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
        intgame_hud_bottom_slide_handle = ui_anim_int_to(
            &intgame_hud_bottom_slide_offset,
            INTGAME_HUD_BOTTOM_H,
            &UI_ANIM_PROFILE_DEFAULT_SLIDE);
    }
}

// CE: complementary slide-show — animates both bars from wherever
// they currently are (typically off-screen after slide_hide) back
// to their TAB-stage rest positions. The TAB stage may itself have
// the bars partially hidden (MEDIUM/MINI/HIDDEN) — we honor that
// so the menu dismiss restores exactly what the user had before.
void intgame_hud_slide_show(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    intgame_hud_shell_hidden = false;
    intgame_hud_band_mode = false;

    // Top bar → slide back to TAB target (0 for FULL, -41 for any
    // cropped stage).
    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID) {
        int top_target = (intgame_hud_stage == INTGAME_HUD_STAGE_FULL)
            ? 0
            : -INTGAME_HUD_TOP_HEIGHT;
        ui_anim_cancel(intgame_hud_top_slide_handle);
        intgame_hud_top_slide_handle = ui_anim_int_to(
            &intgame_hud_top_slide_offset, top_target,
            &UI_ANIM_PROFILE_DEFAULT_SLIDE);
    }

    // Bottom bar → 0 (rest) when visible, BOTTOM_H (off-screen
    // below) when the TAB stage is HIDDEN. Re-apply clips so the
    // render_stage clip rect is current.
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
        int bottom_target =
            (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN)
            ? INTGAME_HUD_BOTTOM_H
            : 0;
        ui_anim_cancel(intgame_hud_bottom_slide_handle);
        intgame_hud_bottom_slide_handle = ui_anim_int_to(
            &intgame_hud_bottom_slide_offset, bottom_target,
            &UI_ANIM_PROFILE_DEFAULT_SLIDE);
    }
}

// CE: snap-mode reset for both bar slide offsets. Used by the
// vanilla band-mode path (intgame_iso_strips_show_as_band) and by
// callers that need to know the bars are at rest immediately
// (level-load setup, save). Cancels any in-flight tween.
void intgame_hud_slide_reset_to_rest(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    ui_anim_cancel(intgame_hud_top_slide_handle);
    ui_anim_cancel(intgame_hud_bottom_slide_handle);
    intgame_hud_top_slide_handle = UI_ANIM_HANDLE_INVALID;
    intgame_hud_bottom_slide_handle = UI_ANIM_HANDLE_INVALID;
    intgame_hud_top_slide_offset =
        (intgame_hud_stage == INTGAME_HUD_STAGE_FULL)
        ? 0
        : -INTGAME_HUD_TOP_HEIGHT;
    intgame_hud_bottom_slide_offset =
        (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN)
        ? INTGAME_HUD_BOTTOM_H
        : 0;
    intgame_hud_shell_hidden = false;
    // Sync last_applied to the new offsets so ping doesn't fire on
    // the next frame and snap the bar back to rest_y, overriding
    // whatever band-mode positioning the caller is about to install
    // via tig_window_move.
    intgame_hud_top_last_applied = intgame_hud_top_slide_offset;
    intgame_hud_bottom_last_applied = intgame_hud_bottom_slide_offset;
}

// CE: seed both bar slide offsets to off-screen WITHOUT animating
// — used for level-load entrance, where the bars should already
// be off-screen when the iso world first renders. A subsequent
// intgame_hud_slide_show animates them in from there.
//
// Actively moves the bar windows to off-screen via tig_window_move
// (in addition to seeding the offset slots) so there's no one-
// frame flash of bars-at-rest before the next ping cycle picks
// up the new offset. rest_y is captured from the current bottom
// bar y if not already known.
void intgame_hud_slide_prepare_offscreen(void)
{
    if (!intgame_iso_interface_created) {
        return;
    }
    ui_anim_cancel(intgame_hud_top_slide_handle);
    ui_anim_cancel(intgame_hud_bottom_slide_handle);
    intgame_hud_top_slide_handle = UI_ANIM_HANDLE_INVALID;
    intgame_hud_bottom_slide_handle = UI_ANIM_HANDLE_INVALID;
    intgame_hud_top_slide_offset = -INTGAME_HUD_TOP_HEIGHT;
    intgame_hud_bottom_slide_offset = INTGAME_HUD_BOTTOM_H;
    intgame_hud_shell_hidden = true;
    intgame_hud_band_mode = false;

    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID) {
        // Cancel any in-flight ui_anim tween targeting this window
        // (e.g. the band-mode exit hide that mainmenu_ui's
        // sub_546DD0 fires alongside the panel hide) AND clear
        // the transform tig left behind. Without this, the slide
        // animates into place but the band-exit tween either
        // re-asserts scale 0.92 / alpha 0 on the next ui_anim_ping
        // (rendering the bar invisible) or leaves a stale
        // transform in tig's compositor (same outcome).
        ui_anim_cancel_for_window(dword_64C4F8[0]);
        TigWindowData wd;
        if (tig_window_data(dword_64C4F8[0], &wd) == TIG_OK) {
            tig_window_move(dword_64C4F8[0], wd.rect.x,
                -INTGAME_HUD_TOP_HEIGHT);
        }
    }
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID) {
        ui_anim_cancel_for_window(dword_64C4F8[1]);
        TigWindowData wd;
        if (tig_window_data(dword_64C4F8[1], &wd) == TIG_OK) {
            tig_window_move(dword_64C4F8[1], wd.rect.x,
                intgame_hud_bottom_rest_y_compute() + INTGAME_HUD_BOTTOM_H);
        }
    }
}

// CE: per-frame integrator — applies the spring-tweened top-bar
// slide offset to the top HUD strip's tig window via tig_window_move.
// Cheap fast-path when the offset hasn't changed since the last
// invocation (early return on identity). Called from gamelib_draw
// after ui_anim_ping so it sees the just-integrated offset.
// CE: vial animation state — one per bar so the two liquids bubble
// independently (out of sync) rather than in lockstep. A vial spends most
// of its time idle (settled at frame 0) and occasionally plays a short
// "bubble" burst — a few loops through the liquid art — then waits a
// randomized gap before the next one. A change in the underlying value
// (taking/healing damage, draining/restoring fatigue) triggers a burst
// immediately so the vial reacts when disturbed or replenished.
typedef struct {
    int frame;          // current displayed frame (0..num_frames-1)
    int num_frames;     // resolved from the liquid art (0 = not yet resolved)
    int base_frame_ms;  // frame duration from the art's authored fps
    int cur_frame_ms;   // this burst's frame duration (randomized per burst)
    int timer_ms;       // countdown to the next frame (playing) / next burst (idle)
    int cycles_left;    // loops remaining in the current burst
    bool playing;       // true: mid-burst; false: idle gap
    bool disturb_pending; // set by intgame_vial_disturb (value changed); the
                          // ping consumes it to kick a burst
} IntgameVialAnim;
static IntgameVialAnim intgame_vial[INTGAME_BAR_COUNT];

// CE: tiny self-contained LCG for the vial timing jitter. Deliberately
// NOT the game RNG — this is cosmetic and must not perturb gameplay/MP
// random state. Seeded once from the wall clock.
static unsigned int intgame_vial_rng = 0;

static int intgame_vial_rand(int lo, int hi)
{
    if (hi <= lo) return lo;
    intgame_vial_rng = intgame_vial_rng * 1103515245u + 12345u;
    return lo + (int)((intgame_vial_rng >> 16) % (unsigned int)(hi - lo + 1));
}

// Kick a vial into a burst of `cycles` loops at a randomized speed,
// starting from a settled frame.
static void intgame_vial_start_burst(IntgameVialAnim* v, int cycles)
{
    v->playing = true;
    v->cycles_left = cycles;
    // ±25% speed jitter so bursts don't all feel identical.
    v->cur_frame_ms = v->base_frame_ms * intgame_vial_rand(80, 130) / 100;
    if (v->cur_frame_ms < 1) v->cur_frame_ms = 1;
    v->frame = 0;
    v->timer_ms = v->cur_frame_ms;
}

// Advance one vial's state machine by dt ms; returns true if its frame
// changed and the bar needs a redraw.
static bool intgame_vial_update(int bar, int dt)
{
    IntgameVialAnim* v = &intgame_vial[bar];
    bool changed = false;

    // Resolve the liquid art metadata once (health redvial #18 /
    // fatigue bluvial #19; both 15-frame). Poisoned health (#17) is
    // single-frame and handled by the num_frames guard in the draw.
    if (v->num_frames == 0) {
        tig_art_id_t art_id;
        TigArtAnimData anim;
        int art_num = (bar == INTGAME_BAR_HEALTH) ? 18 : 19;
        if (tig_art_interface_id_create(art_num, 0, 0, 0, &art_id) == TIG_OK
            && tig_art_anim_data(art_id, &anim) == TIG_OK
            && anim.num_frames > 1 && anim.fps > 0) {
            v->num_frames = anim.num_frames;
            v->base_frame_ms = 1000 / anim.fps;
        } else {
            v->num_frames = 1;
            v->base_frame_ms = 80;
        }
        if (v->base_frame_ms < 1) v->base_frame_ms = 1;
        v->frame = 0;
        v->playing = false;
        // Stagger the first spontaneous burst per vial so they start
        // desynced.
        v->timer_ms = intgame_vial_rand(1200, 5000) + bar * 700;
    }
    if (v->num_frames <= 1) {
        v->disturb_pending = false;
        return false; // single-frame liquid — nothing to animate
    }

    // Disturb/replenish trigger: the value changed (event-driven via
    // intgame_vial_disturb, so no per-frame stat polling here). Bubble the
    // vial in reaction.
    if (v->disturb_pending) {
        v->disturb_pending = false;
        if (!v->playing) {
            intgame_vial_start_burst(v, intgame_vial_rand(2, 3));
            changed = true;
        } else if (v->cycles_left < 2) {
            // already bubbling — keep it going a little longer
            v->cycles_left = 2;
        }
    }

    // Advance the burst / idle-gap countdown.
    v->timer_ms -= dt;
    if (v->playing) {
        while (v->timer_ms <= 0) {
            v->frame++;
            v->timer_ms += v->cur_frame_ms;
            changed = true;
            if (v->frame >= v->num_frames) {
                v->frame = 0;
                if (--v->cycles_left <= 0) {
                    // settle and wait a randomized gap before the next
                    // spontaneous burst.
                    v->playing = false;
                    v->timer_ms = intgame_vial_rand(1500, 6000);
                    break;
                }
            }
        }
    } else if (v->timer_ms <= 0) {
        // spontaneous ambient burst (1-2 loops)
        intgame_vial_start_burst(v, intgame_vial_rand(1, 2));
        changed = true;
    }

    intgame_vial_frame[bar] = v->frame;
    return changed;
}

// CE: drive the bubbling-liquid vial animations. Each vial runs an
// independent burst/idle state machine (see intgame_vial_update) so they
// bubble irregularly and out of sync, with gaps between cycles, and react
// when their value is disturbed or replenished. Gated to the cases where
// the bottom HUD vials are actually on screen so it doesn't force redraws
// (and a composite) under a fullscreen window, in a shell menu, or while
// the bar is slid out — keeping the cost to the small vial rects only.
static void intgame_vial_anim_ping(void)
{
    static tig_timestamp_t last_ms;
    static bool have_last = false;

    if (dword_64C4F8[1] == TIG_WINDOW_HANDLE_INVALID) return;
    if (intgame_hud_shell_hidden) return;
    if (intgame_fullscreen_forced) return;
    if (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN) return;
    if (player_get_local_pc_obj() == OBJ_HANDLE_NULL) return;

    tig_timestamp_t now;
    tig_timer_now(&now);
    if (!have_last) {
        last_ms = now;
        have_last = true;
        intgame_vial_rng = now ^ 0x9E3779B9u;
        return;
    }
    int dt = (int)tig_timer_between(last_ms, now);
    last_ms = now;
    if (dt <= 0) return;
    if (dt > 250) dt = 250; // clamp after a hitch / pause so we don't lurch

    for (int bar = 0; bar < INTGAME_BAR_COUNT; bar++) {
        if (intgame_vial_update(bar, dt)) {
            intgame_draw_bar(bar);
        }
    }
}

// CE: react to a health/fatigue change by bubbling the matching vial.
// Called from the bar-refresh events (UPDATE_HEALTH_BAR / UPDATE_FATIGUE_BAR)
// so disturbance is event-driven — no per-frame stat polling. The ping
// picks up the pending flag on its next tick and kicks the burst.
void intgame_vial_disturb(int bar)
{
    if (bar < 0 || bar >= INTGAME_BAR_COUNT) return;
    intgame_vial[bar].disturb_pending = true;
}

void intgame_hud_ping(void)
{
    if (!intgame_iso_interface_created) {
        // Iso interface gone — reset so a fresh open re-applies on
        // first ping.
        intgame_hud_top_last_applied = INT_MIN;
        intgame_hud_bottom_last_applied = INT_MIN;
        return;
    }

    intgame_vial_anim_ping();

    // CE: scroll the day/night clock strip smoothly. intgame_clock_refresh
    // self-caches its pixel offset (no-op until the strip would actually
    // move by >=1px), replacing the original's once-per-game-hour stepwise
    // jump with continuous motion. Throttled to ~8/sec rather than every
    // frame — the strip creeps at most a pixel every few game-minutes, so
    // recomputing the time-of-day offset 60×/sec is wasted work; checking
    // 8×/sec is visually identical. Gated like the vials so a hidden /
    // covered top bar doesn't force redraws.
    {
        static tig_timestamp_t clk_last_ms;
        static bool clk_have_last = false;
        tig_timestamp_t clk_now;
        tig_timer_now(&clk_now);
        if (!clk_have_last
            || (int)tig_timer_between(clk_last_ms, clk_now) >= 125) {
            clk_have_last = true;
            clk_last_ms = clk_now;
            if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID
                && !intgame_hud_shell_hidden
                && !intgame_fullscreen_forced
                && intgame_hud_stage != INTGAME_HUD_STAGE_HIDDEN) {
                intgame_clock_refresh();
            }
        }
    }

    // CE: advance / reap the FULL->MEDIUM wings-slide ghost.
    intgame_hud_ghost_ping();

    // Top bar slide.
    if (dword_64C4F8[0] != TIG_WINDOW_HANDLE_INVALID
        && intgame_hud_top_slide_offset != intgame_hud_top_last_applied) {
        intgame_hud_top_last_applied = intgame_hud_top_slide_offset;
        TigWindowData wd;
        if (tig_window_data(dword_64C4F8[0], &wd) == TIG_OK) {
            // The bar's canonical rest y is 0 (top of screen), so
            // we move directly to slide_offset (range -41..0).
            tig_window_move(dword_64C4F8[0], wd.rect.x,
                intgame_hud_top_slide_offset);
        }
    }

    // Bottom bar slide (used both for entering HIDDEN — slide
    // DOWN — and leaving HIDDEN — slide UP). rest_y is computed
    // on-demand from the design frame + GRAVITY_BOTTOM, the same
    // gravity used at create-time — so band-mode tig_window_move
    // calls don't corrupt our baseline.
    //
    // Re-install the band clip at the new screen y so the crop
    // tracks the bar — without this, the clip stays at the old
    // screen rect and the bar's content slides INSIDE a static
    // crop instead of moving as a unit with its band.
    if (dword_64C4F8[1] != TIG_WINDOW_HANDLE_INVALID
        && intgame_hud_bottom_slide_offset != intgame_hud_bottom_last_applied) {
        intgame_hud_bottom_last_applied = intgame_hud_bottom_slide_offset;
        TigWindowData wd;
        if (tig_window_data(dword_64C4F8[1], &wd) == TIG_OK) {
            int new_y = intgame_hud_bottom_rest_y_compute()
                + intgame_hud_bottom_slide_offset;
            tig_window_move(dword_64C4F8[1], wd.rect.x, new_y);
            IntgameHudStage render_stage =
                (intgame_hud_stage == INTGAME_HUD_STAGE_HIDDEN)
                ? intgame_hud_last_visible_stage
                : intgame_hud_stage;
            intgame_hud_bottom_set_clip(wd.rect.x, new_y, render_stage);
        }
    }
}

int intgame_hud_top_offset(void)
{
    // CE: instead of the binary 0-or-41 snap based on stage, return
    // the smoothly-animated bar y. Fate / sleep panels read this
    // every frame via their own pings and "ride" the bar's slide as
    // it tweens between rest (offset=0 → returns 41) and fully-off
    // (offset=-41 → returns 0). Mid-tween values produce the
    // intermediate dock positions that compose with each panel's
    // own slide animation.
    int top = INTGAME_HUD_TOP_HEIGHT + intgame_hud_top_slide_offset;
    if (top < 0) top = 0;
    if (top > INTGAME_HUD_TOP_HEIGHT) top = INTGAME_HUD_TOP_HEIGHT;
    return top;
}

// Half of the bottom strip's currently hidden height (in design coords).
// The bottom strip is 159px tall; in MEDIUM/MINI/HIDDEN stages portions
// of it are clipped out. tc.c (dialog options backdrop) shifts down by
// this amount when shown so it takes half of the visual gap that the
// cropped bar leaves behind.
int intgame_hud_bottom_gap_offset(void)
{
    switch (intgame_hud_stage) {
    case INTGAME_HUD_STAGE_FULL:
        return 0;
    case INTGAME_HUD_STAGE_MEDIUM:
        // Chrome 107px visible; ~52 hidden. Half = 26.
        return 26;
    case INTGAME_HUD_STAGE_MINI:
        // Only the 25px slim row visible; 134 hidden. Half = 67.
        return 67;
    case INTGAME_HUD_STAGE_HIDDEN:
        // Entire 159 hidden. Half = 79.
        return 79;
    default:
        return 0;
    }
}

int intgame_hud_bottom_top_crop(void)
{
    switch (intgame_hud_stage) {
    case INTGAME_HUD_STAGE_FULL:
        return 0;
    case INTGAME_HUD_STAGE_MEDIUM:
        // Visible band starts at strip-local MEDIUM_BAND_Y (51).
        return INTGAME_HUD_MEDIUM_BAND_Y;
    case INTGAME_HUD_STAGE_MINI:
        // Visible band starts at strip-local MINI_BAND_Y (122).
        return INTGAME_HUD_MINI_BAND_Y;
    case INTGAME_HUD_STAGE_HIDDEN:
        // No visible bar — top edge has fallen all the way to
        // INTGAME_HUD_BOTTOM_H (= bottom of the strip rect).
        return INTGAME_HUD_BOTTOM_H;
    default:
        return 0;
    }
}

void intgame_hud_bottom_band_design_x(int* out_x, int* out_w)
{
    int x = 0;
    int w = 0;
    switch (intgame_hud_stage) {
    case INTGAME_HUD_STAGE_FULL:
        x = 0;
        w = INTGAME_HUD_BOTTOM_W;
        break;
    case INTGAME_HUD_STAGE_MEDIUM:
        x = INTGAME_HUD_MEDIUM_BAND_X;
        w = INTGAME_HUD_MEDIUM_BAND_W;
        break;
    case INTGAME_HUD_STAGE_MINI:
        x = INTGAME_HUD_MINI_BAND_X;
        w = INTGAME_HUD_MINI_BAND_W;
        break;
    case INTGAME_HUD_STAGE_HIDDEN:
    default:
        x = 0;
        w = 0;
        break;
    }
    if (out_x != NULL) *out_x = x;
    if (out_w != NULL) *out_w = w;
}

bool intgame_hud_is_settling(void)
{
    return ui_anim_is_active(intgame_hud_top_slide_handle)
        || ui_anim_is_active(intgame_hud_bottom_slide_handle);
}

int intgame_hud_bottom_slide_offset_get(void)
{
    return intgame_hud_bottom_slide_offset;
}

// CE: per-tick iso-invalidate hook. In the current direct-paint
// tint architecture (tig_video_blit_near_black_tinted reads iso's
// VB directly each composite), no extra invalidation is needed —
// iso_redraw's natural dirty-rect system keeps the VB fresh wherever
// the world has changed, and the compositor reads it as-is. Kept as
// a no-op stub so the main-loop call site stays linkable.
void intgame_hud_tick_invalidate_alpha_strips(void)
{
}

// CE: per-tick hook called from main.c AFTER iso_redraw. For each
// window opted into the translucent-black tint pathway, darken the
// iso VB pixels under that window's rect so the pre-baked color-key
// holes in the chrome show "tinted iso world" through them. The
// compositor then does a plain hardware color-key blit — no per-
// pixel CPU work in its inner loop. Same architectural pattern as
// the dialog options backdrop tint in tc.c.
// CE: also a no-op now. The tint happens at composite time inside
// tig_video_blit_near_black_tinted (reads iso VB directly, writes
// subtract-tinted result to screen). No pre-composite iso-VB
// mutation is needed, which also avoids conflicting with tc.c's
// dialog-options backdrop tint (the chamfered corners come back).
void intgame_hud_tick_apply_tint(void)
{
}

// CE: Tint parameters chosen for the current UI context — both the
// underlay window and the subtract amount applied to its pixels
// before they replace the panel's near-black source. Returned by
// intgame_translucent_black_pick().
typedef struct {
    tig_window_handle_t underlay;
    uint8_t threshold;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} IntgameTintParams;

// CE: pick the right underlay + tint amount for the translucent-
// black pathway based on current UI context.
//
// Priority:
//   1. If the mainmenu is up AND has a hi-res backdrop window
//      (mainmenu_bg / per-screen *_bg.bmp baked in), use that
//      backdrop's VB. Menu backdrops are high-luminance and the
//      user wants the chrome to stay the dominant surface, with
//      only a faint hint of bg bleeding through. The mainmenu
//      backdrop is preferred over the iso world because the
//      mainmenu's full-screen backdrop hides iso entirely; dark
//      panel pixels should reveal what the user can actually see
//      (the menu bg), not stale or unloaded iso content beneath.
//   2. Else if the iso interface exists (active gameplay, no
//      mainmenu chrome), use the iso world VB. World content is
//      darker than menu bg, so a lighter darken keeps world detail
//      visible through the chrome's near-black areas.
//   3. Else (early-init-only, or any state without a sensible
//      underlay), underlay=INVALID — caller disables tint.
//
// The tint blit (tig_video_blit_near_black_tinted) uses a per-
// channel MULTIPLY: output = underlay * (255 - tint) / 256. So
// r/g/b are "darken by N out of 255" values — 0 preserves a
// channel, 255 zeroes it. Multiply preserves the underlay's hue
// (unlike a saturating subtract, which clips channels independently
// and visibly burns colors).
//
// At native 800x600 the mainmenu has no separate backdrop window;
// the in-game shortcut path also skips the backdrop in hi-res.
// Both fall through to iso as the underlay, which is correct since
// the panel doesn't fully cover the world in those cases.
static IntgameTintParams intgame_translucent_black_pick(void)
{
    IntgameTintParams params = {
        .underlay = TIG_WINDOW_HANDLE_INVALID,
        .threshold = 8,
        .r = 0, .g = 0, .b = 0,
    };
    // CE: backdrop check first, regardless of mainmenu_ui_active.
    // When the strip-mgmt block fires show_as_band (which triggers
    // a tint refresh) inside mainmenu_ui_create_window_func, the
    // backdrop is already created but mainmenu_ui_active hasn't
    // been set to true yet — that happens later in the same
    // function. Checking the backdrop window directly catches that
    // window of time and prevents the bar from picking up the iso
    // world (pregame "world") as its tint underlay for a frame.
    // CE: while the backdrop is animating OUT into gameplay (just
    // started/loaded a game), its handle is still valid + has-custom-art
    // still true, but the view is becoming the live iso world. Don't
    // pick the fading menu backdrop as the underlay or a window opened
    // right after load shows the menu / black through its dark areas.
    // Fall through to the iso world instead.
    bool backdrop_exiting = mainmenu_ui_backdrop_is_exiting();
    tig_window_handle_t backdrop = mainmenu_ui_get_backdrop_handle();
    if (!backdrop_exiting
        && backdrop != TIG_WINDOW_HANDLE_INVALID
        && mainmenu_ui_has_custom_backdrop_art()) {
        params.underlay = backdrop;
        // ~80% darken
        params.r = 204;
        params.g = 204;
        params.b = 204;
        return params;
    }
    if (mainmenu_ui_is_active() && !backdrop_exiting) {
        // Legacy / no-custom-bg mainmenu: don't use the panel as
        // the underlay (panel-as-its-own-underlay creates feedback:
        // a panel near-black pixel reading from its same near-black
        // pixel produces an even darker result, blacking out the
        // panel chrome). And don't fall through to iso (in-game
        // pause case) — the user explicitly didn't want modals
        // punching through to the live game world, and pre-game
        // there is no game world anyway. Disable the tint so
        // near-black panel pixels render as their native near-
        // black color, matching vanilla appearance.
        return params;
    }
    if (intgame_iso_interface_is_created()) {
        params.underlay = intgame_iso_window;
        // ~50% darken — matches the dialog options backdrop's
        // MUL(128) for visual parity.
        params.r = 128;
        params.g = 128;
        params.b = 128;
    }
    return params;
}

// CE: Apply the translucent-black tint pathway to a window. Used
// by inventory / paperdoll / loot / barter / world map / charedit /
// in-play Options-Save-Load / mainmenu sub-windows to opt in/out
// at open / close time. The compositor runs
// tig_video_blit_near_black_tinted for opted-in windows, replacing
// near-black source pixels with subtract-tinted underlay pixels.
//
// The underlay + tint amount are context-aware (see
// intgame_translucent_black_pick): mainmenu backdrop with a heavier
// subtract when the mainmenu is up, iso world with the standard
// subtract during active gameplay, neither when no sensible
// underlay exists.
//
// When disabling (enable=false) the gates don't matter — we just
// turn it off unconditionally so a previously-opted-in window
// doesn't keep the effect when its UI closes.
void intgame_apply_translucent_black(tig_window_handle_t window_handle, bool enable)
{
    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    if (!enable) {
        tig_window_tint_enable(window_handle, false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    if (!settings_get_value(&settings, TRANSLUCENT_BLACK_UI_KEY)) {
        // Cfg off — clear any leftover tint from a previous session.
        tig_window_tint_enable(window_handle, false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    IntgameTintParams p = intgame_translucent_black_pick();
    if (p.underlay == TIG_WINDOW_HANDLE_INVALID) {
        // No sensible underlay (pre-game title screen on 800x600, or
        // mainmenu-not-up + iso-not-up — the latter only at very
        // early init). User explicitly doesn't want dark panel
        // pixels punching through to mainmenu_bg or the unloaded
        // game world.
        tig_window_tint_enable(window_handle, false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    tig_window_tint_enable(window_handle, true,
        p.underlay, p.threshold, p.r, p.g, p.b);
}

// CE: world-knockout key colour — pure magenta. A window opted into
// knockout shows the raw iso world wherever its pixels are this colour, so
// custom-shaped panels can punch clean holes to the world. Magenta is
// distinct from the green chromakey the custom-UI BMP loader consumes, so
// the two never collide: a knockout-mode window pre-fills magenta, the
// green chromakey reveals it, and this turns it into the world.
#define INTGAME_WORLD_KNOCKOUT_KEY tig_color_make(255, 0, 255)

// CE: opt a window into the world-knockout composite (see tig_window_
// knockout_enable). Reuses the translucent-black underlay picker for the
// world source; a no-op (disabled) when there's no sensible underlay
// (e.g. no iso world up). Independent of the TRANSLUCENT_BLACK_UI cfg —
// custom window shapes aren't the near-black see-through.
void intgame_apply_world_knockout(tig_window_handle_t window_handle, bool enable)
{
    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    if (!enable) {
        tig_window_knockout_enable(window_handle, false,
            TIG_WINDOW_HANDLE_INVALID, 0);
        return;
    }
    IntgameTintParams p = intgame_translucent_black_pick();
    if (p.underlay == TIG_WINDOW_HANDLE_INVALID) {
        tig_window_knockout_enable(window_handle, false,
            TIG_WINDOW_HANDLE_INVALID, 0);
        return;
    }
    tig_window_knockout_enable(window_handle, true, p.underlay,
        INTGAME_WORLD_KNOCKOUT_KEY);
}

tig_color_t intgame_world_knockout_key(void)
{
    return INTGAME_WORLD_KNOCKOUT_KEY;
}

// CE: re-pick the modal-dialog auto-tint params based on current
// UI context. Called whenever the relevant state flips: iso
// interface create/destroy, mainmenu open/close. Without this hook
// the underlay was locked in at iso_interface_create time and
// stayed iso even after the mainmenu opened over the world — modals
// raised from the pause menu (Save overwrite confirms, Quit confirm,
// etc.) would punch through to the iso world they were supposed to
// hide.
void intgame_refresh_modal_tint(void)
{
    if (!settings_get_value(&settings, TRANSLUCENT_BLACK_UI_KEY)) {
        tig_window_modal_tint_set(false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    IntgameTintParams p = intgame_translucent_black_pick();
    if (p.underlay == TIG_WINDOW_HANDLE_INVALID) {
        tig_window_modal_tint_set(false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    tig_window_modal_tint_set(true,
        p.underlay, p.threshold, p.r, p.g, p.b);
}

// CE: re-pick the HUD bar's tint params for the current context.
// The bar is opted into the tint pathway at iso_interface_create
// time with whatever the picker returns; whenever UI context flips
// (mainmenu opens/closes), this gets called again to refresh —
// otherwise the bar's tint underlay stays locked to iso even when
// the bar is visible over a mainmenu (notably the pre-game new-
// char / pregen / charedit flow, where the iso strips are shown
// as a band and the bar's near-black pixels would knock through
// to an unloaded iso world). The bar window itself is destroyed
// in iso_interface_destroy and the tint state with it, so we don't
// need a dedicated "disable" path here.
void intgame_refresh_hud_bar_tint(void)
{
    if (dword_64C4F8[1] == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    if (!intgame_hud_bar_uses_tint) {
        return;
    }
    if (!settings_get_value(&settings, TRANSLUCENT_BLACK_UI_KEY)) {
        tig_window_tint_enable(dword_64C4F8[1], false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    IntgameTintParams p = intgame_translucent_black_pick();
    if (p.underlay == TIG_WINDOW_HANDLE_INVALID) {
        tig_window_tint_enable(dword_64C4F8[1], false,
            TIG_WINDOW_HANDLE_INVALID, 0, 0, 0, 0);
        return;
    }
    tig_window_tint_enable(dword_64C4F8[1], true,
        p.underlay, p.threshold, p.r, p.g, p.b);
}
