/*
 * video_convert.h — in-game .bik → .avi conversion driver.
 *
 * Drives the user-side "Convert Now" experience surfaced by
 * src/ui/video_convert_ui. The driver does not embed any video codec;
 * it shells out to the `ffmpeg` binary present on the user's machine
 * (detected at startup via SDL_CreateProcess). The game executable
 * never links FFmpeg, and FFmpeg is never bundled in the build or
 * distribution — it is a per-user runtime tool, the same category as
 * a system text editor or git client.
 *
 * Lifecycle:
 *   video_convert_init() once at engine startup.
 *   video_convert_detect_ffmpeg() cheap synchronous probe; cached.
 *   video_convert_start() kicks off the background worker.
 *   video_convert_get_progress() polled by the UI per-frame.
 *   video_convert_poll() returns true when a state transition occurred
 *     (so the UI can rescan / dismiss the modal).
 *   video_convert_cancel() asks the worker to terminate ASAP.
 *   video_convert_exit() once at engine shutdown.
 */

#ifndef ARCANUM_GAME_VIDEO_CONVERT_H_
#define ARCANUM_GAME_VIDEO_CONVERT_H_

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    VIDEO_CONVERT_STATE_IDLE,
    VIDEO_CONVERT_STATE_RUNNING,
    VIDEO_CONVERT_STATE_DONE,
    VIDEO_CONVERT_STATE_CANCELED,
    VIDEO_CONVERT_STATE_ERROR
} VideoConvertState;

typedef enum {
    VIDEO_CONVERT_FFMPEG_UNKNOWN,
    VIDEO_CONVERT_FFMPEG_PRESENT,
    VIDEO_CONVERT_FFMPEG_ABSENT,
    /* iOS / Android: subprocess spawning is not viable from the
     * application sandbox; the UI surfaces a "convert on desktop and
     * transfer" message in this state. */
    VIDEO_CONVERT_FFMPEG_UNSUPPORTED_PLATFORM
} VideoConvertFfmpegStatus;

typedef struct {
    VideoConvertState state;
    unsigned int total_files;
    unsigned int completed_files;
    char current_filename[64];
    float current_file_progress;   /* 0..1 within the current file */
    float overall_progress;        /* 0..1 across the whole batch  */
    char error_message[256];       /* populated when state == ERROR */
} VideoConvertProgress;

/* Forward-declare to avoid pulling the whole game/context.h tree into
 * UI headers that only need the conversion control surface. The init
 * adapter in gamelib.c invokes us through the GameLibModule table. */
struct GameInitInfo;
typedef struct GameInitInfo GameInitInfo;

bool video_convert_init(GameInitInfo* info);
void video_convert_exit(void);

VideoConvertFfmpegStatus video_convert_detect_ffmpeg(void);
/* Force a re-detect (e.g. after the user installs ffmpeg and clicks
 * "Recheck" in the modal). */
VideoConvertFfmpegStatus video_convert_redetect_ffmpeg(void);

/* Returns false if no files need conversion, ffmpeg is unavailable, or
 * a conversion is already running. */
bool video_convert_start(void);
void video_convert_cancel(void);
bool video_convert_is_running(void);

void video_convert_get_progress(VideoConvertProgress* out);

/* Called by the UI thread once per frame. Drains any worker results
 * and returns true if the state changed since the last call (UI uses
 * this to rescan gmovie assets and dismiss the modal on completion). */
bool video_convert_poll(void);

#endif /* ARCANUM_GAME_VIDEO_CONVERT_H_ */
