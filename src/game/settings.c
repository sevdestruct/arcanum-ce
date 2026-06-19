#include "game/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/obj_private.h"

static SettingsEntry* settings_find(Settings* settings, const char* key);
static void settings_trim(char* str);

/**
 * Initializes a settings structure with a file path.
 *
 * 0x438AE0
 */
void settings_init(Settings* settings, const char* path)
{
    settings->path = path;
    settings->entries = NULL;
}

/**
 * Frees all resources associated with a settings structure.
 *
 * 0x438B00
 */
void settings_exit(Settings* settings)
{
    SettingsEntry* curr;
    SettingsEntry* next;

    curr = settings->entries;
    while (curr != NULL) {
        next = curr->next;
        FREE(curr->key);
        FREE(curr->value);
        FREE(curr);
        curr = next;
    }

    settings->path = NULL;
    settings->entries = NULL;
}

// CE: remove a setting by key, if present. Used to drop deprecated keys
// loaded from an old .cfg so they don't linger in the file forever. Marks
// the settings changed so the cleaned file is rewritten on save.
void settings_remove(Settings* settings, const char* key)
{
    SettingsEntry* curr;
    SettingsEntry* prev;

    prev = NULL;
    curr = settings->entries;
    while (curr != NULL) {
        if (SDL_strcasecmp(curr->key, key) == 0) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                settings->entries = curr->next;
            }
            FREE(curr->key);
            FREE(curr->value);
            FREE(curr);
            settings->flags |= SETTINGS_CHANGED;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * Loads settings from the associated file.
 *
 * File path is specified at creation time with `settings_init` and should
 * not be changed.
 *
 * 0x438B50
 */
void settings_load(Settings* settings)
{
    bool exists;
    TigFindFileData find_file_data;
    FILE* stream;
    char buffer[256];
    char* sep;

    // Check if the settings file exists.
    exists = tig_find_first_file(settings->path, &find_file_data);
    tig_find_close(&find_file_data); // FIX: Memory leak.

    if (!exists) {
        return;
    }

    stream = fopen(settings->path, "rt");
    if (stream == NULL) {
        return;
    }

    while (fgets(buffer, sizeof(buffer), stream) != NULL) {
        // CE: skip comment lines and blanks. Comments use "//" and often
        // contain an '=' in their prose (e.g. "0=off, 1=on"), which the
        // parser below would otherwise mistake for a key=value pair and
        // store as a junk entry that corrupts the file on the next save.
        char* line = buffer;
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r'
            || (line[0] == '/' && line[1] == '/')) {
            continue;
        }

        // Find the key-value separator.
        sep = strchr(buffer, '=');
        if (sep != NULL) {
            // Split the string into key and value.
            *sep++ = '\0';

            // Trim whitespace from both key and value.
            settings_trim(buffer);
            settings_trim(sep);

            // Only store non-empty key-value pairs.
            if (sep[0] != '\0' && buffer[0] != '\0') {
                settings_set_str_value(settings, buffer, sep);
            }
        }
    }

    fclose(stream);
}

/**
 * Saves settings to the associated file.
 *
 * 0x438C20
 */
void settings_save(Settings* settings)
{
    FILE* stream;
    SettingsEntry* curr;

    // Ensure there is something worth saving.
    if (settings->entries == NULL) {
        return;
    }

    // Only save settings have been changed.
    if ((settings->flags & SETTINGS_CHANGED) == 0) {
        return;
    }

    stream = fopen(settings->path, "wt");
    if (stream == NULL) {
        // Something's wrong, this should not normally happen.
        return;
    }

    curr = settings->entries;
    while (curr != NULL) {
        fprintf(stream, "%s=%s\n", curr->key, curr->value);
        curr = curr->next;
    }

    fclose(stream);

    // FIX: Settings synchronized to disk, unset `SETTINGS_CHANGED`.
    settings->flags &= ~SETTINGS_CHANGED;
}

// CE: true if `key` appears anywhere in the doc table. A doc row with a
// NULL key is a SECTION HEADER (its comment is the section name); the
// array is terminated by a {NULL, NULL} row. So iterate until both are
// NULL, and only match rows that actually carry a key.
static bool settings_doc_has_key(const SettingsDoc* docs, const char* key)
{
    const SettingsDoc* doc;

    if (docs == NULL) {
        return false;
    }
    for (doc = docs; doc->key != NULL || doc->comment != NULL; doc++) {
        if (doc->key != NULL && SDL_strcasecmp(doc->key, key) == 0) {
            return true;
        }
    }
    return false;
}

// CE: emit a comment as one or more "// <line>" lines (splitting on '\n').
static void settings_write_comment(FILE* stream, const char* comment)
{
    const char* start = comment;
    const char* nl;

    while ((nl = strchr(start, '\n')) != NULL) {
        fprintf(stream, "// %.*s\n", (int)(nl - start), start);
        start = nl + 1;
    }
    if (*start != '\0') {
        fprintf(stream, "// %s\n", start);
    }
}

void settings_save_documented(Settings* settings, const SettingsDoc* docs)
{
    FILE* stream;
    const SettingsDoc* doc;
    const char* pending_header;
    SettingsEntry* entry;
    SettingsEntry* curr;
    bool first;
    bool wrote_other;

    if (settings->entries == NULL) {
        return;
    }

    if ((settings->flags & SETTINGS_CHANGED) == 0) {
        return;
    }

    stream = fopen(settings->path, "wt");
    if (stream == NULL) {
        return;
    }

    // The doc table defines a canonical, grouped layout: rows are written
    // in table order, a NULL-key row is a "// ===== <name> =====" section
    // header, and each setting is preceded by its comment. A blank line
    // separates every block. A header is DEFERRED until the section's first
    // present setting, so empty sections don't print a stray header. Then
    // any settings NOT in the table (unknown / legacy / other-branch keys)
    // are appended under "Other" so nothing is ever dropped.
    first = true;
    pending_header = NULL;

    for (doc = docs; doc->key != NULL || doc->comment != NULL; doc++) {
        if (doc->key == NULL) {
            // Section header — hold it until a present setting needs it.
            pending_header = doc->comment;
            continue;
        }

        entry = settings_find(settings, doc->key);
        if (entry == NULL) {
            // Documented but not present this session — skip.
            continue;
        }

        if (pending_header != NULL) {
            if (!first) {
                fprintf(stream, "\n");
            }
            first = false;
            fprintf(stream, "// ===== %s =====\n", pending_header);
            pending_header = NULL;
        }

        if (!first) {
            fprintf(stream, "\n");
        }
        first = false;
        if (doc->comment != NULL) {
            settings_write_comment(stream, doc->comment);
        }
        fprintf(stream, "%s=%s\n", entry->key, entry->value);
    }

    // Undocumented entries, preserved verbatim under their own section.
    wrote_other = false;
    for (curr = settings->entries; curr != NULL; curr = curr->next) {
        if (settings_doc_has_key(docs, curr->key)) {
            continue;
        }
        if (!wrote_other) {
            if (!first) {
                fprintf(stream, "\n");
            }
            first = false;
            fprintf(stream, "// ===== Other =====\n");
            wrote_other = true;
        }
        fprintf(stream, "\n%s=%s\n", curr->key, curr->value);
    }

    fclose(stream);

    settings->flags &= ~SETTINGS_CHANGED;
}

/**
 * Registers a new setting with a given key and default value.
 *
 * An optional callback `value_changed_func` can be specified as part of
 * setting. This callback will be triggered whenever the value associated with
 * a given key is changed. It will not be fired upon registration. Care should
 * be taken when changing the settings from within this callback to avoid
 * creating infinite loops.
 *
 * 0x438C80
 */
void settings_register(Settings* settings, const char* key, const char* default_value, SettingsValueChangedFunc value_changed_func)
{
    SettingsEntry* entry;

    entry = settings_find(settings, key);
    if (entry != NULL) {
        entry->value_changed_func = value_changed_func;
    } else {
        entry = (SettingsEntry*)MALLOC(sizeof(*entry));
        entry->key = STRDUP(key);
        entry->value = STRDUP(default_value);
        entry->value_changed_func = value_changed_func;
        entry->next = NULL;

        // CE: APPEND, not head-insert. The original head-insert reversed
        // the list relative to insertion order; since settings_load runs
        // before the settings_register calls, the file was loaded into the
        // list reversed, then written back head-first — so the .cfg key
        // order flipped on every save. Appending makes list order ==
        // insertion order (== file order on load, then any new defaults at
        // the end), so saves are stable and the file order is preserved.
        if (settings->entries == NULL) {
            settings->entries = entry;
        } else {
            SettingsEntry* tail = settings->entries;
            while (tail->next != NULL) {
                tail = tail->next;
            }
            tail->next = entry;
        }
    }
}

/**
 * Sets an integer value for a setting.
 *
 * 0x438CE0
 */
void settings_set_value(Settings* settings, const char* key, int value)
{
    char buffer[48];

    SDL_itoa(value, buffer, 10);
    settings_set_str_value(settings, key, buffer);
}

/**
 * Retrieves an integer value for a setting.
 *
 * Returns `0` if the key is not found.
 *
 * 0x438D10
 */
int settings_get_value(Settings* settings, const char* key)
{
    const char* str;

    str = settings_get_str_value(settings, key);
    if (str != NULL) {
        return atoi(str);
    } else {
        return 0;
    }
}

/**
 * Sets an ObjectID value for a setting.
 *
 * 0x438D40
 */
void settings_set_obj_value(Settings* settings, const char* key, ObjectID oid)
{
    char str[40];

    objid_id_to_str(str, oid);
    settings_set_str_value(settings, key, str);
}

/**
 * Retrieves an ObjectID value for a setting.
 *
 * Returns an ObjectID with type `OID_TYPE_NULL` if the key is not found.
 *
 * 0x438DA0
 */
ObjectID settings_get_obj_value(Settings* settings, const char* key)
{
    const char* str;
    ObjectID oid;

    str = settings_get_str_value(settings, key);
    if (str != NULL) {
        objid_id_from_str(&oid, str);
    } else {
        oid.type = OID_TYPE_NULL;
    }

    return oid;
}

/**
 * Sets a string value for a setting.
 *
 * 0x438DF0
 */
void settings_set_str_value(Settings* settings, const char* key, const char* value)
{
    SettingsEntry* entry;

    // Mark settings as changed to trigger saving.
    settings->flags |= SETTINGS_CHANGED;

    // Check if the key already exists.
    entry = settings_find(settings, key);
    if (entry == NULL) {
        // Add a new entry if the key does not exist.
        settings_register(settings, key, value, NULL);
    } else {
        // Update the existing entry's value.
        if (strlen(value) > strlen(entry->value)) {
            // The new string is longer than existing one - free existing value
            // and make a copy of the new string.
            FREE(entry->value);
            entry->value = STRDUP(value);
        } else {
            // Value fits into already existing buffer.
            memcpy(entry->value, value, strlen(value) + 1);
        }

        // Trigger the callback.
        if (entry->value_changed_func != NULL) {
            entry->value_changed_func();
        }
    }
}

/**
 * Retrieves a string value for a setting.
 *
 * Returns `NULL` if the key is not found.
 *
 * 0x438E90
 */
const char* settings_get_str_value(Settings* settings, const char* key)
{
    SettingsEntry* entry;

    entry = settings_find(settings, key);
    if (entry != NULL) {
        return entry->value;
    } else {
        return NULL;
    }
}

/**
 * Internal helper function to find a settings entry by key.
 *
 * 0x438EB0
 */
SettingsEntry* settings_find(Settings* settings, const char* key)
{
    SettingsEntry* curr;

    curr = settings->entries;
    while (curr != NULL) {
        if (SDL_strcasecmp(curr->key, key) == 0) {
            return curr;
        }
        curr = curr->next;
    }

    return NULL;
}

/**
 * Internal helper function to trim leading and trailing whitespace from a
 * string (in place).
 *
 * 0x438EF0
 */
void settings_trim(char* str)
{
    char* curr;
    size_t len;

    // Skip leading whitespace.
    curr = str;
    while (SDL_isspace(*curr)) {
        curr++;
    }

    // Move the trimmed string to the start.
    len = strlen(curr);
    memmove(str, curr, len + 1); // FIX: Instead of `memcpy`.

    // Remove trailing whitespace.
    //
    // CE FIX: start at the LAST character (str + len - 1), not the '\0'
    // terminator (str + len). The original started on the '\0', so
    // SDL_isspace('\0') was false and the loop never ran — trailing
    // whitespace (notably the '\n' from fgets) was left on every value.
    // That stored values as e.g. "1\n", which on save wrote "key=1\n\n"
    // (a stray blank line after every setting) and broke string-value
    // comparisons like "software\n" != "software".
    if (len > 0) {
        curr = str + len - 1;
        while (curr >= str && SDL_isspace((unsigned char)*curr)) {
            *curr-- = '\0';
        }
    }
}
