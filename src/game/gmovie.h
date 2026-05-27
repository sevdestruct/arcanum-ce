#ifndef ARCANUM_GAME_GMOVIE_H_
#define ARCANUM_GAME_GMOVIE_H_

#include "game/context.h"

// NOTE: Gmovie flags are the same as TIG but have slightly different values.
typedef unsigned int GameMovieFlags;

#define GAME_MOVIE_FADE_IN 0x1u
#define GAME_MOVIE_FADE_OUT 0x2u
#define GAME_MOVIE_IGNORE_KEYBOARD 0x4u
#define GAME_MOVIE_NO_FINAL_FLIP 0x8u
#define GAME_MOVIE_NO_SCALE 0x10u

bool gmovie_mod_load(void);
void gmovie_mod_unload(void);
void gmovie_play(int movie, GameMovieFlags flags, int sound_track);
void gmovie_play_path(const char* path, GameMovieFlags flags, int sound_track);

// Cutscene-asset scan + conversion-aware QoL helpers.
//
// The cross-platform video backend (see first_party/bink_compat) plays
// .avi sidecars produced from the original .bik cutscenes by either the
// in-game conversion modal (src/ui/video_convert_ui) or the optional CLI
// script in scripts/convert_videos.py. These helpers expose the
// before/after state so the UI layer can prompt the user, show a banner,
// or hide the conversion machinery entirely once everything is converted.
//
// Call gmovie_scan_video_assets() once after the data DAT and movies.mes
// have loaded; subsequent gmovie_unconverted_count() / iteration calls
// reflect that snapshot. Re-run the scan after a conversion completes or
// when the user toggles modules.
void gmovie_scan_video_assets(void);
unsigned int gmovie_unconverted_count(void);
// 0-based index into the unconverted list; copies the basename (no path,
// no extension) into out_basename. Returns false if index is out of range.
// out_basename must be at least 64 bytes.
bool gmovie_unconverted_basename(unsigned int index, char* out_basename, size_t out_size);
// Copy the source-side path (DAT-style "movies\foo.bik" for cutscenes,
// or a cwd-relative "data/art/ui/foo.mp4" for user replacements) so the
// conversion driver can resolve the file with tig_file_extract.
bool gmovie_unconverted_source_path(unsigned int index, char* out_path, size_t out_size);
// Copy the cwd-relative destination path where the converted .avi
// should be written ("data/videos/foo.avi" for cutscene .bik entries,
// or a sibling-of-source "data/art/ui/foo.avi" for replacements).
bool gmovie_unconverted_dest_path(unsigned int index, char* out_path, size_t out_size);

// "Skip intro cutscenes" toggle (Options menu). When true, gmovie_play
// becomes a no-op for the intro movie IDs (1 = company logos, 7 = main
// intro) and for direct paths matching the SierraLogo / TroikaLogo
// startup calls in gamelib.c.
void gmovie_set_skip_intros(bool skip);
bool gmovie_get_skip_intros(void);

#endif /* ARCANUM_GAME_GMOVIE_H_ */
