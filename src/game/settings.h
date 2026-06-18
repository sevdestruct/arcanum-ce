#ifndef ARCANUM_GAME_SETTINGS_H_
#define ARCANUM_GAME_SETTINGS_H_

#include "game/obj.h"

typedef unsigned int SettingsFlags;

#define SETTINGS_CHANGED 0x1

typedef void (*SettingsValueChangedFunc)(void);

typedef struct SettingsEntry {
    /* 0000 */ char* key;
    /* 0004 */ char* value;
    /* 0008 */ SettingsValueChangedFunc value_changed_func;
    /* 000C */ struct SettingsEntry* next;
} SettingsEntry;

typedef struct Settings {
    /* 0000 */ const char* path;
    /* 0004 */ SettingsEntry* entries;
    /* 0008 */ SettingsFlags flags;
} Settings;

// CE: one row of documentation for the settings file. `comment` is a
// succinct description written as a "// ..." line above the setting on
// save (may contain '\n' for multiple lines). The array is terminated by
// a row whose `key` is NULL. Keys not in the table are still saved, just
// without a comment.
typedef struct SettingsDoc {
    const char* key;
    const char* comment;
} SettingsDoc;

void settings_init(Settings* settings, const char* path);
void settings_exit(Settings* settings);
void settings_load(Settings* settings);
void settings_save(Settings* settings);
// CE: like settings_save, but writes a "// <comment>" line above each
// setting (looked up by key in `docs`) and a blank line between entries.
// Order is unchanged (list/insertion order); `docs` only supplies text.
void settings_save_documented(Settings* settings, const SettingsDoc* docs);
void settings_register(Settings* settings, const char* key, const char* default_value, SettingsValueChangedFunc value_changed_func);
void settings_set_value(Settings* settings, const char* key, int value);
int settings_get_value(Settings* settings, const char* key);
void settings_set_obj_value(Settings* settings, const char* key, ObjectID oid);
ObjectID settings_get_obj_value(Settings* settings, const char* key);
void settings_set_str_value(Settings* settings, const char* key, const char* value);
const char* settings_get_str_value(Settings* settings, const char* key);

#endif /* ARCANUM_GAME_SETTINGS_H_ */
