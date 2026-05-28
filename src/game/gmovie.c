#include "game/gmovie.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#include "game/gsound.h"
#include "game/mes.h"

#include "bink_compat.h"

/**
 * "movies.mes"
 *
 * 0x59F02C
 */
static mes_file_handle_t movies_mes_file = MES_FILE_HANDLE_INVALID;

/* Cached unconverted-asset snapshot. The UI surfaces this verbatim via
 * gmovie_unconverted_*; the conversion driver reads source paths from
 * here when the user clicks "Convert Now". The list grows in chunks of
 * 16 to keep allocations bounded for the typical 8-cutscene case. */
typedef struct UnconvertedEntry {
    /* Source-side path -- DAT-style "movies\foo.bik" for cutscenes,
     * cwd-relative "data/art/ui/foo.mp4" for loose-file replacements.
     * The conversion driver passes this to tig_file_extract() to
     * resolve to an absolute disk path before invoking ffmpeg. */
    char source_path[TIG_MAX_PATH];
    /* Destination -- where the .avi sidecar gets written. cwd-relative
     * so the path resolves the same way at every launch. For .bik
     * sources we route to "data/videos/<basename>.avi" (the canonical
     * override location gmovie_play_path probes first); for loose
     * replacement videos we keep the .avi next to the original so the
     * mainmenu / slide override system finds it via its own probes. */
    char dest_path[TIG_MAX_PATH];
    char basename[64];               /* "foo" */
} UnconvertedEntry;

static UnconvertedEntry* unconverted_entries;
static unsigned int unconverted_count;
static unsigned int unconverted_capacity;

static bool skip_intros;

static void gmovie_unconverted_clear(void)
{
    free(unconverted_entries);
    unconverted_entries = NULL;
    unconverted_count = 0;
    unconverted_capacity = 0;
}

static bool gmovie_unconverted_push(const char* source_path,
    const char* dest_path, const char* basename)
{
    if (unconverted_count == unconverted_capacity) {
        unsigned int new_cap = unconverted_capacity ? unconverted_capacity * 2 : 16;
        UnconvertedEntry* nb = (UnconvertedEntry*)realloc(unconverted_entries,
            new_cap * sizeof(UnconvertedEntry));
        if (!nb) return false;
        unconverted_entries = nb;
        unconverted_capacity = new_cap;
    }
    UnconvertedEntry* e = &unconverted_entries[unconverted_count++];
    strncpy(e->source_path, source_path, sizeof(e->source_path) - 1);
    e->source_path[sizeof(e->source_path) - 1] = '\0';
    strncpy(e->dest_path, dest_path, sizeof(e->dest_path) - 1);
    e->dest_path[sizeof(e->dest_path) - 1] = '\0';
    strncpy(e->basename, basename, sizeof(e->basename) - 1);
    e->basename[sizeof(e->basename) - 1] = '\0';
    return true;
}

/* Strip the final ".ext" from `name`, write the bare stem into `out`.
 * Returns false if `name` has no extension or out_size is too small. */
static bool strip_ext(const char* name, char* out, size_t out_size)
{
    const char* dot = strrchr(name, '.');
    if (!dot || dot == name) return false;
    size_t n = (size_t)(dot - name);
    if (n + 1 > out_size) return false;
    memcpy(out, name, n);
    out[n] = '\0';
    return true;
}

/**
 * Called when a module is being loaded.
 *
 * 0x40DE20
 */
bool gmovie_mod_load(void)
{
    // Load "movies.mes" (might be absent).
    if (!mes_load("movies\\movies.mes", &movies_mes_file)) {
        movies_mes_file = MES_FILE_HANDLE_INVALID;
    }

    /* Do NOT scan video assets here — the scan needs every file
     * repository registered, and at module-load time we only see the
     * 2 DAT-internal logo files. The main-menu modal calls
     * gmovie_scan_video_assets() directly once everything is
     * mounted. */
    return true;
}

/**
 * Called when a module is being unloaded.
 *
 * 0x40DE50
 */
void gmovie_mod_unload(void)
{
    // Unload "movies.mes".
    if (movies_mes_file != MES_FILE_HANDLE_INVALID) {
        mes_unload(movies_mes_file);
        movies_mes_file = MES_FILE_HANDLE_INVALID;
    }
    gmovie_unconverted_clear();
}

/* Build the .avi sibling path for a given .bik path. Caller-supplied
 * buffer must hold at least TIG_MAX_PATH bytes. */
static void build_avi_sidecar(const char* bik_path, char* out, size_t out_size)
{
    strncpy(out, bik_path, out_size - 1);
    out[out_size - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) {
        if ((size_t)(dot - out) + 5 < out_size) {
            memcpy(dot, ".avi", 5);
        }
    }
}

/* True if a converted .avi has already been produced for this stem.
 *
 * Lookup strategy: try TIG's repository search first (matches the
 * runtime playback path used by gmovie_play_path), then fall back to
 * a direct stdio check on the same path interpreted relative to the
 * process working directory. The stdio fallback catches the case
 * where the conversion script has just written a .avi but TIG's
 * repository cache doesn't reflect it yet (TIG only rescans loose
 * directories at specific lifecycle points).
 *
 * We check both `data/videos/<stem>.avi` (the gmovie override
 * location the conversion script writes to) and the legacy sidecar
 * `movies\<stem>.avi`. */
static bool path_exists_stdio(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

static bool avi_already_exists(const char* stem)
{
    TigFileInfo info;
    char path[TIG_MAX_PATH];
    char resolved[TIG_MAX_PATH];

    /* gmovie's override-search location — what convert_videos.py uses. */
    snprintf(path, sizeof(path), "data/videos/%s.avi", stem);
    if (tig_file_exists(path, &info)) return true;
    if (tig_file_extract(path, resolved)) return true;
    if (path_exists_stdio(path)) return true;

    snprintf(path, sizeof(path), "data/videos/%s_native.avi", stem);
    if (tig_file_exists(path, &info)) return true;
    if (tig_file_extract(path, resolved)) return true;
    if (path_exists_stdio(path)) return true;

    /* Legacy sidecar location — same folder as the original .bik. */
    snprintf(path, sizeof(path), "movies\\%s.avi", stem);
    if (tig_file_exists(path, &info)) return true;
    if (tig_file_extract(path, resolved)) return true;
    /* stdio uses POSIX separators, so try the forward-slash form too. */
    {
        char alt[TIG_MAX_PATH];
        snprintf(alt, sizeof(alt), "movies/%s.avi", stem);
        if (path_exists_stdio(alt)) return true;
    }

    return false;
}

/* Case-insensitive endswith for path filtering. */
static bool path_ends_with_ci(const char* path, const char* suffix)
{
    size_t pn = strlen(path);
    size_t sn = strlen(suffix);
    if (sn > pn) return false;
    const char* tail = path + (pn - sn);
    for (size_t i = 0; i < sn; ++i) {
        char a = tail[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return true;
}

/* True if `name` carries a video extension we know how to convert to
 * MJPEG/AVI. .avi is intentionally excluded — those are already in
 * our target format. .bik is excluded here because the existing scan
 * handles it through tig_file_list_create. */
static bool is_convertible_video_filename(const char* name)
{
    static const char* const exts[] = {
        ".mp4", ".m4v", ".mov", ".mkv", ".webm", ".flv", ".wmv", NULL
    };
    for (int i = 0; exts[i]; ++i) {
        if (path_ends_with_ci(name, exts[i])) return true;
    }
    return false;
}

/* Recursive directory walker (POSIX dirent). Walks `dir_path` (forward
 * slashes, relative to cwd or absolute), descends every subdirectory
 * except TIGCache (the engine's own .bik extract cache — we don't
 * want to re-scan those), and adds every convertible video file that
 * lacks a sibling .avi to the unconverted list. */
static void scan_data_subtree_for_videos(const char* dir_path, int depth)
{
    if (depth > 16) return;  /* safety net */

#ifdef _WIN32
    /* Windows _findfirst path: out of scope for the initial v1 — the
     * native Win32 build still uses binkw32.dll so the conversion
     * flow isn't the primary path there. Stub for now. */
    (void)dir_path; (void)depth;
    return;
#else
    DIR* d = opendir(dir_path);
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;  /* skip "." ".." dotfiles */
        if (strcasecmp(entry->d_name, "TIGCache") == 0) continue;
        if (strcasecmp(entry->d_name, "Players") == 0) continue;
        if (strcasecmp(entry->d_name, "Save") == 0) continue;

        char full[TIG_MAX_PATH];
        if ((int)snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name)
                >= (int)sizeof(full)) {
            continue;  /* path too long; skip */
        }

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_data_subtree_for_videos(full, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        if (!is_convertible_video_filename(entry->d_name)) continue;

        /* Sibling .avi check. We do BOTH a stdio fopen test (catches
         * files just dropped by the user) and a tig_file_extract test
         * (catches anything visible through the engine's repository
         * but missing from the loose filesystem). */
        char sibling[TIG_MAX_PATH];
        const char* dot = strrchr(full, '.');
        if (!dot) continue;
        size_t n = (size_t)(dot - full);
        if (n + 5 >= sizeof(sibling)) continue;
        memcpy(sibling, full, n);
        memcpy(sibling + n, ".avi", 5);

        FILE* fp = fopen(sibling, "rb");
        if (fp) { fclose(fp); continue; }

        /* Basename without extension, for the modal display. */
        const char* leaf = strrchr(full, '/');
        leaf = leaf ? leaf + 1 : full;
        char basename[64];
        if (!strip_ext(leaf, basename, sizeof(basename))) continue;

        /* Replacement videos: write the .avi as a sibling of the
         * source so the mainmenu / slide override probes find it
         * through the same parent directory the user dropped the
         * source into. `sibling` was already computed above. */
        gmovie_unconverted_push(full, sibling, basename);
    }
    closedir(d);
#endif
}

void gmovie_scan_video_assets(void)
{
    gmovie_unconverted_clear();

    /* Pass 1 -- canonical cutscene .bik files via the engine's file
     * enumeration. This sees both loose-disk and DAT-internal .bik
     * entries that match "movies\*.bik". */
    TigFileList list;
    tig_file_list_create(&list, "movies\\*.bik");
    unsigned int total_bik = list.count;
    for (unsigned int i = 0; i < list.count; ++i) {
        const char* leaf = list.entries[i].path;

        char basename[64];
        if (!strip_ext(leaf, basename, sizeof(basename))) {
            continue;
        }
        if (avi_already_exists(basename)) {
            continue;
        }

        char source[TIG_MAX_PATH];
        snprintf(source, sizeof(source), "movies\\%s", leaf);
        /* All .bik sources -- DAT-internal or loose -- get their .avi
         * written to the canonical override directory. This is the
         * first path gmovie_play_path probes at runtime, so the
         * converted file wins regardless of which form the original
         * .bik took. */
        char dest[TIG_MAX_PATH];
        snprintf(dest, sizeof(dest), "data/videos/%s.avi", basename);
        gmovie_unconverted_push(source, dest, basename);
    }
    tig_file_list_destroy(&list);
    unsigned int after_bik = unconverted_count;

    /* Pass 2 -- recursively walk loose files under data/ and art/ for
     * user-supplied replacement videos (.mp4/.mov/.mkv/.webm). The
     * decoder only plays our AVI/MJPEG container, so any non-.avi
     * video file in the asset tree needs to be converted the same
     * way as the cutscene .bik files. Path discovery is stdio-based:
     * user replacements are loose files by definition, never DAT
     * entries. */
    static const char* const data_subtrees[] = {
        "data", "art", "modules", NULL,
    };
    for (int i = 0; data_subtrees[i]; ++i) {
        scan_data_subtree_for_videos(data_subtrees[i], 0);
    }

    fprintf(stderr,
        "gmovie: scan found %u .bik file(s), %u loose video file(s); "
        "%u need conversion total\n",
        total_bik, unconverted_count - after_bik, unconverted_count);
}

unsigned int gmovie_unconverted_count(void)
{
    return unconverted_count;
}

bool gmovie_unconverted_basename(unsigned int index, char* out_basename, size_t out_size)
{
    if (!out_basename || index >= unconverted_count) return false;
    strncpy(out_basename, unconverted_entries[index].basename, out_size - 1);
    out_basename[out_size - 1] = '\0';
    return true;
}

bool gmovie_unconverted_source_path(unsigned int index, char* out_path, size_t out_size)
{
    if (!out_path || index >= unconverted_count) return false;
    strncpy(out_path, unconverted_entries[index].source_path, out_size - 1);
    out_path[out_size - 1] = '\0';
    return true;
}

bool gmovie_unconverted_dest_path(unsigned int index, char* out_path, size_t out_size)
{
    if (!out_path || index >= unconverted_count) return false;
    strncpy(out_path, unconverted_entries[index].dest_path, out_size - 1);
    out_path[out_size - 1] = '\0';
    return true;
}

void gmovie_set_skip_intros(bool skip)
{
    skip_intros = skip;
}

bool gmovie_get_skip_intros(void)
{
    return skip_intros;
}

/* The hard-coded path checks that classify a cutscene as an "intro" the
 * user opted out of via Options. We special-case the two startup logo
 * movies (called directly by gamelib.c) plus the menu intro IDs played
 * from mainmenu_ui.c (IDs 1 and 7). */
static bool path_is_intro(const char* path)
{
    if (!path) return false;
    /* Case-insensitive endswith for the two known startup logo files. */
    const char* known[] = { "SierraLogo.bik", "TroikaLogo.bik",
                            "SierraLogo.avi", "TroikaLogo.avi" };
    size_t n = strlen(path);
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        size_t kn = strlen(known[i]);
        if (n >= kn) {
            const char* tail = path + (n - kn);
            int match = 1;
            for (size_t j = 0; j < kn; ++j) {
                char a = tail[j], b = known[i][j];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { match = 0; break; }
            }
            if (match) return true;
        }
    }
    return false;
}

/**
 * Plays a game movie specified by ID (from "movies.mes" file).
 *
 * 0x40DE70
 */
void gmovie_play(int movie, GameMovieFlags flags, int sound_track)
{
    MesFileEntry mes_file_entry;
    char path[TIG_MAX_PATH];

    // Check if "movies.mes" was successfully initialized.
    if (movies_mes_file == MES_FILE_HANDLE_INVALID) {
        return;
    }

    if (skip_intros && (movie == 1 || movie == 7)) {
        return;
    }

    // Search for file name in "movies.mes".
    mes_file_entry.num = movie;
    if (!mes_search(movies_mes_file, &mes_file_entry)) {
        return;
    }

    // Build a full movie path and play it.
    snprintf(path, sizeof(path), "movies\\%s", mes_file_entry.str);
    gmovie_play_path(path, flags, sound_track);
}

/**
 * Plays a game movie specified by its file path.
 *
 * 0x40DEE0
 */
void gmovie_play_path(const char* path, GameMovieFlags flags, int sound_track)
{
    char temp_path[TIG_MAX_PATH];
    unsigned int movie_flags;
    const char* sep;
    const char* basename;
    char base_noext[TIG_MAX_PATH];
    char* dot;
    bool found;

    if (skip_intros && path_is_intro(path)) {
        return;
    }

    // Check art/videos/<name>.avi (then .mp4, then .bik) for a
    // user-supplied replacement video matching the original file's base
    // name. This allows swapping any game movie without modifying the
    // DAT archives. .avi is preferred so converted assets win over
    // legacy .bik or older .mp4 replacements when both are present.
    sep = strrchr(path, '\\');
    if (!sep) sep = strrchr(path, '/');
    basename = sep ? sep + 1 : path;

    strncpy(base_noext, basename, sizeof(base_noext) - 1);
    base_noext[sizeof(base_noext) - 1] = '\0';
    dot = strrchr(base_noext, '.');
    if (dot) *dot = '\0';

    found = false;
    {
        // Extension priority depends on the active backend: with the
        // native Bink decoder enabled (ARCANUM_BINK_DIRECT=1) a .bik is
        // the directly-playable asset and must win over a leftover
        // .avi/.mp4 of the same base name; otherwise .avi (converted
        // MJPEG) is preferred.
        static const char* const exts_avi_first[] = { ".avi", ".mp4", ".bik" };
        static const char* const exts_bik_first[] = { ".bik", ".avi", ".mp4" };
        const char* const* video_exts = bink_compat_native_bink_enabled()
            ? exts_bik_first
            : exts_avi_first;
        const int video_ext_count = 3;
        int i;
        char candidate[TIG_MAX_PATH];
        // Check for a `_native` suffixed file first. If found, native
        // resolution is used (no scale-to-fit).
        for (i = 0; i < video_ext_count; i++) {
            snprintf(candidate, sizeof(candidate), "data/videos/%s_native%s", base_noext, video_exts[i]);
            if (tig_file_extract(candidate, temp_path)) {
                flags |= GAME_MOVIE_NO_SCALE;
                found = true;
                break;
            }
        }
        if (!found) {
            for (i = 0; i < video_ext_count; i++) {
                snprintf(candidate, sizeof(candidate), "data/videos/%s%s", base_noext, video_exts[i]);
                if (tig_file_extract(candidate, temp_path)) {
                    found = true;
                    break;
                }
            }
        }
    }

    // Fall back to the original DAT path. With the native Bink decoder
    // enabled, prefer the original .bik directly (it plays natively, no
    // conversion); otherwise try the .avi sidecar first so cross-platform
    // builds use the converted MJPEG asset before the .bik.
    if (!found && bink_compat_native_bink_enabled()) {
        if (tig_file_extract(path, temp_path)) {
            found = true;
        }
    }
    if (!found) {
        char alt_path[TIG_MAX_PATH];
        build_avi_sidecar(path, alt_path, sizeof(alt_path));
        if (strcmp(alt_path, path) != 0 && tig_file_extract(alt_path, temp_path)) {
            found = true;
        }
    }
    if (!found && !tig_file_extract(path, temp_path)) {
        /* Soft-skip: neither the .avi sidecar nor the original .bik
         * could be located. Log once and return so the engine continues
         * past the cutscene instead of hanging on a black screen. */
        fprintf(stderr, "gmovie: skipping missing movie '%s' (no .avi sidecar found; "
                        "run video conversion from the main menu or "
                        "scripts/convert_videos.py)\n", path);
        return;
    }

    // Lock game sound system to ensure exclusive audio access during movie
    // playback.
    gsound_lock();

    // Convert game movie flags to TIG movie flags.
    movie_flags = 0;

    if ((flags & GAME_MOVIE_FADE_IN) != 0) {
        movie_flags |= TIG_MOVIE_FADE_IN;
    }

    if ((flags & GAME_MOVIE_FADE_OUT) != 0) {
        movie_flags |= TIG_MOVIE_FADE_OUT;
    }

    if ((flags & GAME_MOVIE_IGNORE_KEYBOARD) != 0) {
        movie_flags |= TIG_MOVIE_IGNORE_KEYBOARD;
    }

    if ((flags & GAME_MOVIE_NO_FINAL_FLIP) != 0) {
        movie_flags |= TIG_MOVIE_NO_FINAL_FLIP;
    }

    if ((flags & GAME_MOVIE_NO_SCALE) != 0) {
        movie_flags |= TIG_MOVIE_NO_SCALE;
    }

    // Play the movie. This is a blocking call, which will return when movie
    // playback ends.
    tig_movie_play(temp_path, movie_flags, sound_track);

    // Resume sound system.
    gsound_unlock();
}
