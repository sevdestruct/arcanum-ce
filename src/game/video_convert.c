/*
 * video_convert.c — in-game .bik → .avi conversion driver.
 *
 * Spawns the system `ffmpeg` binary as a child process per file,
 * parses its stderr (merged into stdout via SDL_CreateProcess
 * properties) for `frame=N` progress markers, and atomically renames
 * the output `.tmp` to `.avi` on success.
 *
 * Threading model:
 *
 *   UI thread                Worker thread (SDL)
 *   --------------------     ----------------------------
 *   start() ─────────────┐   for each unconverted file:
 *                        ├─► spawn ffmpeg
 *   poll() (every frame) │   read merged stdout in chunks
 *      └─ reads progress │   parse "frame=N" lines
 *      └─ scans for done │   atomic rename on success
 *                        │   yield mutex-protected progress
 *   cancel() ───────────►│   detect cancel flag; SDL_KillProcess
 *                        ▼
 *                        thread exits, state -> DONE/CANCELED/ERROR
 *
 * No FFmpeg headers are included in this translation unit. We invoke
 * the binary purely by argv; the produced file format (MJPEG-in-AVI,
 * 16-bit PCM stereo) matches what the bink_compat backend decodes.
 */

#include "game/video_convert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/types.h>
#endif

#include <SDL3/SDL.h>
#include <tig/tig.h>

#include "game/gmovie.h"

/* Convert-command argv template. -y overwrites the .tmp; -nostdin keeps
 * ffmpeg from blocking on terminal control sequences (it occasionally
 * reads stdin even when not in a TTY). */
static const char* kFfmpegProgramName = "ffmpeg";

/* Cached absolute path of the working `ffmpeg` binary, picked at probe
 * time. Empty string means "use the bare name `ffmpeg` and rely on
 * PATH" (the case on Linux + terminal-launched macOS); a non-empty
 * value is the resolved full path found in one of the common
 * Finder-invisible install locations like /opt/homebrew/bin. */
static char g_ffmpeg_resolved_path[512];

/* Common install locations to probe when the bare-name PATH lookup
 * fails. macOS .app bundles launched from Finder inherit launchd's
 * PATH which does NOT include Homebrew paths, so we fall back to
 * checking the well-known install dirs directly. Linux containers /
 * AppImages can hit the same issue with /usr/local/bin. */
static const char* const kFfmpegFallbackPaths[] = {
#ifdef __APPLE__
    "/opt/homebrew/bin/ffmpeg",  /* Homebrew on Apple Silicon */
    "/usr/local/bin/ffmpeg",     /* Homebrew on Intel macs    */
    "/opt/local/bin/ffmpeg",     /* MacPorts                  */
#endif
#ifdef __linux__
    "/usr/bin/ffmpeg",
    "/usr/local/bin/ffmpeg",
    "/snap/bin/ffmpeg",
    "/var/lib/flatpak/exports/bin/org.freedesktop.Platform.ffmpeg-full",
#endif
    NULL,
};

#define VC_PROGRESS_MUTEX_NAME "video_convert_progress"

typedef struct VideoConvertCtx {
    SDL_Mutex* mutex;
    SDL_Thread* thread;

    VideoConvertProgress progress; /* mutex-protected */
    bool cancel_requested;          /* mutex-protected */
    bool state_changed;             /* mutex-protected; cleared by poll() */

    VideoConvertFfmpegStatus ffmpeg_status;

    /* Current child process (worker-thread-only access). */
    SDL_Process* current_process;
} VideoConvertCtx;

static VideoConvertCtx g_ctx;

/* ------------------------------------------------------------------ */
/* ffmpeg detection                                                    */
/* ------------------------------------------------------------------ */

static bool platform_supports_subprocess(void)
{
#if defined(__IPHONEOS__) || defined(__ANDROID__) \
    || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
    return false;
#else
    return true;
#endif
}

/* Try to spawn `ffmpeg_path -version` and return true if it ran with
 * exit code 0. Used both to validate the bare-name PATH lookup and the
 * full-path fallbacks. */
static bool probe_ffmpeg_candidate(const char* ffmpeg_path)
{
    const char* args[] = { ffmpeg_path, "-version", NULL };
    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) return false;
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) return false;

    int exit_code = -1;
    SDL_WaitProcess(p, true, &exit_code);
    SDL_DestroyProcess(p);
    return exit_code == 0;
}

static VideoConvertFfmpegStatus probe_ffmpeg_once(void)
{
    g_ffmpeg_resolved_path[0] = '\0';

    if (!platform_supports_subprocess()) {
        return VIDEO_CONVERT_FFMPEG_UNSUPPORTED_PLATFORM;
    }

    /* Try the bare name first — works in terminal-launched processes
     * and on Windows where PATH is sane. */
    if (probe_ffmpeg_candidate(kFfmpegProgramName)) {
        return VIDEO_CONVERT_FFMPEG_PRESENT;
    }

    /* Fall back to common install locations. macOS .app bundles
     * launched from Finder don't inherit /opt/homebrew/bin etc., so the
     * bare-name probe fails there even when the user has ffmpeg
     * installed via Homebrew. */
    for (int i = 0; kFfmpegFallbackPaths[i] != NULL; ++i) {
        const char* path = kFfmpegFallbackPaths[i];
        /* Cheap existence check before paying the fork+exec cost. */
        FILE* fp = fopen(path, "rb");
        if (fp == NULL) continue;
        fclose(fp);
        if (probe_ffmpeg_candidate(path)) {
            size_t n = strlen(path);
            if (n + 1 <= sizeof(g_ffmpeg_resolved_path)) {
                memcpy(g_ffmpeg_resolved_path, path, n + 1);
            }
            fprintf(stderr, "video_convert: using ffmpeg at %s\n", path);
            return VIDEO_CONVERT_FFMPEG_PRESENT;
        }
    }

    return VIDEO_CONVERT_FFMPEG_ABSENT;
}

/* Return the resolved ffmpeg binary path for spawn calls. Empty
 * resolved_path means "use PATH lookup with the bare name". */
static const char* ffmpeg_program(void)
{
    return g_ffmpeg_resolved_path[0] ? g_ffmpeg_resolved_path
                                      : kFfmpegProgramName;
}

VideoConvertFfmpegStatus video_convert_detect_ffmpeg(void)
{
    if (g_ctx.ffmpeg_status == VIDEO_CONVERT_FFMPEG_UNKNOWN) {
        g_ctx.ffmpeg_status = probe_ffmpeg_once();
    }
    return g_ctx.ffmpeg_status;
}

VideoConvertFfmpegStatus video_convert_redetect_ffmpeg(void)
{
    g_ctx.ffmpeg_status = probe_ffmpeg_once();
    return g_ctx.ffmpeg_status;
}

/* ------------------------------------------------------------------ */
/* Init / exit                                                         */
/* ------------------------------------------------------------------ */

bool video_convert_init(GameInitInfo* info)
{
    (void)info;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.mutex = SDL_CreateMutex();
    g_ctx.ffmpeg_status = VIDEO_CONVERT_FFMPEG_UNKNOWN;
    g_ctx.progress.state = VIDEO_CONVERT_STATE_IDLE;
    return g_ctx.mutex != NULL;
}

void video_convert_exit(void)
{
    /* If a conversion is mid-flight, cancel and join. */
    if (g_ctx.thread) {
        video_convert_cancel();
        SDL_WaitThread(g_ctx.thread, NULL);
        g_ctx.thread = NULL;
    }
    if (g_ctx.mutex) {
        SDL_DestroyMutex(g_ctx.mutex);
        g_ctx.mutex = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                       */
/* ------------------------------------------------------------------ */

/* Parse a chunk of merged ffmpeg stderr looking for the most recent
 * "frame=N" marker, updating per-file progress in `progress_out`. The
 * `expected_total_frames` is taken from ffmpeg's "Duration: ..." /
 * "fps" line earlier in the same stream; if zero we report fractional
 * progress as 0.5 once any frame= line appears (better than a stuck
 * bar). */
static void parse_progress_chunk(const char* text, size_t len,
    unsigned int expected_total_frames,
    float* out_progress)
{
    if (!text || len == 0 || !out_progress) return;
    /* Walk lines backwards from the end so we land on the most recent
     * progress marker fast. The buffer ffmpeg produces is small
     * (< 4 KB per second of output). */
    for (size_t i = len; i > 8; --i) {
        if (text[i - 1] != '=' && text[i - 1] != ' ') continue;
        if (i >= 7 && memcmp(text + i - 7, "frame=", 6) == 0) {
            const char* p = text + i - 1;
            while (*p == '=' || *p == ' ') ++p;
            unsigned long frame = 0;
            while (*p >= '0' && *p <= '9') {
                frame = frame * 10 + (unsigned long)(*p - '0');
                ++p;
            }
            if (expected_total_frames > 0 && frame > 0) {
                float f = (float)frame / (float)expected_total_frames;
                if (f > 1.0f) f = 1.0f;
                *out_progress = f;
            } else if (frame > 0) {
                *out_progress = 0.5f;
            }
            return;
        }
    }
}

static void set_state(VideoConvertState s, const char* err)
{
    SDL_LockMutex(g_ctx.mutex);
    g_ctx.progress.state = s;
    if (err) {
        strncpy(g_ctx.progress.error_message, err,
            sizeof(g_ctx.progress.error_message) - 1);
        g_ctx.progress.error_message[sizeof(g_ctx.progress.error_message) - 1] = '\0';
    } else {
        g_ctx.progress.error_message[0] = '\0';
    }
    g_ctx.state_changed = true;
    SDL_UnlockMutex(g_ctx.mutex);
}

static void set_overall_progress(unsigned int completed, unsigned int total,
    float current_file_progress, const char* current_name)
{
    SDL_LockMutex(g_ctx.mutex);
    g_ctx.progress.total_files = total;
    g_ctx.progress.completed_files = completed;
    g_ctx.progress.current_file_progress = current_file_progress;
    if (current_name) {
        strncpy(g_ctx.progress.current_filename, current_name,
            sizeof(g_ctx.progress.current_filename) - 1);
        g_ctx.progress.current_filename[sizeof(g_ctx.progress.current_filename) - 1] = '\0';
    }
    if (total > 0) {
        float per_file = 1.0f / (float)total;
        g_ctx.progress.overall_progress
            = (float)completed * per_file + current_file_progress * per_file;
        if (g_ctx.progress.overall_progress > 1.0f) g_ctx.progress.overall_progress = 1.0f;
    } else {
        g_ctx.progress.overall_progress = 0.0f;
    }
    SDL_UnlockMutex(g_ctx.mutex);
}

static bool cancel_requested(void)
{
    bool c;
    SDL_LockMutex(g_ctx.mutex);
    c = g_ctx.cancel_requested;
    SDL_UnlockMutex(g_ctx.mutex);
    return c;
}

/* Make sure the parent directory of `path` exists, creating it (and
 * any intermediates) if not. Used to guarantee "data/videos/..." is
 * writable before we ask ffmpeg to land an output there. */
static void ensure_parent_dir(const char* path)
{
    char buf[1024];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);

    char* slash = strrchr(buf, '/');
    char* bslash = strrchr(buf, '\\');
    if (bslash > slash) slash = bslash;
    if (!slash || slash == buf) return;
    *slash = '\0';

    /* Walk forward creating each intermediate directory. */
    for (char* p = buf + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
#ifdef _WIN32
            _mkdir(buf);
#else
            mkdir(buf, 0755);
#endif
            *p = saved;
        }
    }
#ifdef _WIN32
    _mkdir(buf);
#else
    mkdir(buf, 0755);
#endif
}

/* Convert a single file. Returns true on success, false on
 * error/cancel. On error, *err_out is filled. */
static bool convert_one_file(const char* src_path,
    const char* dst_tmp_path,
    char* err_out, size_t err_size)
{
    /* Tell ffmpeg to overwrite (-y), suppress stdin interaction
     * (-nostdin), and produce baseline MJPEG with 16-bit PCM audio. */
    const char* args[] = {
        ffmpeg_program(),
        "-nostdin",
        "-loglevel", "error",
        "-stats",                /* still print "frame=" progress to stderr */
        "-y",
        "-i", src_path,
        "-c:v", "mjpeg",
        "-q:v", "4",
        "-pix_fmt", "yuvj420p",
        "-c:a", "pcm_s16le",
        /* Force the muxer — the destination ends in `.avi.tmp` which
         * ffmpeg can't auto-detect as AVI, so it errors out with
         * "Unable to choose an output format". We rename .tmp -> .avi
         * after the spawn succeeds. */
        "-f", "avi",
        dst_tmp_path,
        NULL,
    };

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        snprintf(err_out, err_size, "failed to allocate process properties");
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) {
        snprintf(err_out, err_size, "could not launch ffmpeg");
        return false;
    }

    g_ctx.current_process = p;

    /* Poll the merged stdout stream for progress lines until the
     * process exits or the user cancels. We deliberately use a small
     * carry buffer between reads so we always have at least the most
     * recent "frame=N" suffix available to the parser. */
    char buf[2048];
    char carry[256];
    size_t carry_n = 0;

    /* Tail of ffmpeg's combined output kept for error reporting if
     * the process exits non-zero — without this the user only sees
     * the exit code, which on macOS ffmpeg builds is usually 254
     * for any input-side problem. */
    char err_tail[512];
    size_t err_tail_n = 0;

    SDL_IOStream* out_io = (SDL_IOStream*)SDL_GetPointerProperty(
        SDL_GetProcessProperties(p),
        SDL_PROP_PROCESS_STDOUT_POINTER, NULL);

    /* Best-effort total-frame estimate. ffmpeg prints "fps=" and
     * "Duration:" in the early output; we just leave total at 0 and
     * report a 0.5 indeterminate state. The overall progress bar still
     * tracks file-count, which is the headline number users care
     * about. */
    unsigned int expected_total_frames = 0;
    float file_progress = 0.0f;

    while (!cancel_requested()) {
        int exit_code = 0;
        bool exited = SDL_WaitProcess(p, false, &exit_code);

        if (out_io) {
            size_t n = SDL_ReadIO(out_io, buf, sizeof(buf) - 1);
            if (n > 0) {
                /* Append to carry, then parse. */
                size_t take = n;
                if (carry_n + take > sizeof(carry) - 1) {
                    /* Drop oldest carry data; we only care about the
                     * trailing few hundred bytes for the parser. */
                    take = sizeof(carry) - 1;
                    carry_n = 0;
                }
                memcpy(carry + carry_n, buf, take);
                carry_n += take;
                parse_progress_chunk(carry, carry_n,
                    expected_total_frames, &file_progress);
                set_overall_progress(g_ctx.progress.completed_files,
                    g_ctx.progress.total_files, file_progress, NULL);

                /* Always remember the LAST chunk of ffmpeg's output
                 * (sliding window) for the error report. */
                if (n >= sizeof(err_tail)) {
                    memcpy(err_tail, buf + (n - (sizeof(err_tail) - 1)),
                        sizeof(err_tail) - 1);
                    err_tail_n = sizeof(err_tail) - 1;
                } else if (err_tail_n + n < sizeof(err_tail)) {
                    memcpy(err_tail + err_tail_n, buf, n);
                    err_tail_n += n;
                } else {
                    /* Sliding window: keep the most recent bytes. */
                    size_t keep = sizeof(err_tail) - 1 - n;
                    memmove(err_tail, err_tail + (err_tail_n - keep), keep);
                    memcpy(err_tail + keep, buf, n);
                    err_tail_n = keep + n;
                }
                err_tail[err_tail_n < sizeof(err_tail) ? err_tail_n
                                                       : sizeof(err_tail) - 1] = '\0';
            }
        }

        if (exited) {
            g_ctx.current_process = NULL;
            if (exit_code != 0) {
                SDL_DestroyProcess(p);
                /* Log the full ffmpeg tail to stderr (visible via the
                 * Console.app / terminal) so the user can see exactly
                 * what ffmpeg complained about. */
                fprintf(stderr,
                    "video_convert: ffmpeg exited %d. tail of output:\n%.*s\n",
                    exit_code, (int)err_tail_n, err_tail);
                /* Surface the last non-empty line of err_tail in the
                 * modal — usually the actual error message. */
                const char* last = err_tail;
                for (size_t k = 0; k < err_tail_n; ++k) {
                    if (err_tail[k] == '\n' && k + 1 < err_tail_n) {
                        last = err_tail + k + 1;
                    }
                }
                snprintf(err_out, err_size, "ffmpeg (%d): %s",
                    exit_code, last[0] ? last : "no stderr captured");
                return false;
            }
            SDL_DestroyProcess(p);
            return true;
        }
        SDL_Delay(50);
    }

    /* Cancel path: kill, drain, destroy. */
    SDL_KillProcess(p, true);
    SDL_WaitProcess(p, true, NULL);
    SDL_DestroyProcess(p);
    g_ctx.current_process = NULL;
    snprintf(err_out, err_size, "canceled");
    return false;
}

static int worker_main(void* userdata)
{
    (void)userdata;
    unsigned int total = gmovie_unconverted_count();
    if (total == 0) {
        set_state(VIDEO_CONVERT_STATE_DONE, NULL);
        return 0;
    }

    set_overall_progress(0, total, 0.0f, "");

    for (unsigned int i = 0; i < total; ++i) {
        if (cancel_requested()) {
            set_state(VIDEO_CONVERT_STATE_CANCELED, NULL);
            return 0;
        }

        char tig_src[1024];
        char basename[64];
        char dst[TIG_MAX_PATH];
        if (!gmovie_unconverted_source_path(i, tig_src, sizeof(tig_src))
            || !gmovie_unconverted_basename(i, basename, sizeof(basename))
            || !gmovie_unconverted_dest_path(i, dst, sizeof(dst))) {
            continue;
        }

        /* The gmovie scan returns DAT-relative paths like
         * "movies\foo.bik" (or cwd-relative paths for loose user
         * replacements). ffmpeg needs a real filesystem path; ask
         * TIG to resolve it (loose files return their actual disk
         * location, DAT-internal files get extracted to a temp dir). */
        char src_real[TIG_MAX_PATH];
        if (!tig_file_extract(tig_src, src_real)) {
            char err[256];
            snprintf(err, sizeof(err),
                "could not resolve filesystem path for %s", tig_src);
            set_state(VIDEO_CONVERT_STATE_ERROR, err);
            return 0;
        }

        /* Ensure the destination directory exists (e.g. data/videos
         * on a fresh install) before ffmpeg tries to write there. */
        ensure_parent_dir(dst);

        char dst_tmp[TIG_MAX_PATH + 8];
        snprintf(dst_tmp, sizeof(dst_tmp), "%s.tmp", dst);

        fprintf(stderr, "video_convert: %s -> %s\n", src_real, dst);
        set_overall_progress(i, total, 0.0f, basename);

        char err[256] = { 0 };
        bool ok = convert_one_file(src_real, dst_tmp, err, sizeof(err));
        if (!ok) {
            /* Tidy up half-written output. */
            remove(dst_tmp);
            if (cancel_requested()) {
                set_state(VIDEO_CONVERT_STATE_CANCELED, NULL);
                return 0;
            }
            set_state(VIDEO_CONVERT_STATE_ERROR, err);
            return 0;
        }

        /* Atomic-ish rename: remove existing .avi first so rename
         * succeeds across all our supported filesystems. */
        remove(dst);
        if (rename(dst_tmp, dst) != 0) {
            set_state(VIDEO_CONVERT_STATE_ERROR, "rename to .avi failed");
            return 0;
        }

        set_overall_progress(i + 1, total, 0.0f, basename);
    }

    set_state(VIDEO_CONVERT_STATE_DONE, NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public control                                                      */
/* ------------------------------------------------------------------ */

bool video_convert_start(void)
{
    if (video_convert_detect_ffmpeg() != VIDEO_CONVERT_FFMPEG_PRESENT) {
        return false;
    }
    if (g_ctx.thread != NULL) {
        return false;
    }
    if (gmovie_unconverted_count() == 0) {
        return false;
    }
    SDL_LockMutex(g_ctx.mutex);
    g_ctx.cancel_requested = false;
    g_ctx.state_changed = true;
    g_ctx.progress.state = VIDEO_CONVERT_STATE_RUNNING;
    g_ctx.progress.total_files = gmovie_unconverted_count();
    g_ctx.progress.completed_files = 0;
    g_ctx.progress.current_filename[0] = '\0';
    g_ctx.progress.current_file_progress = 0.0f;
    g_ctx.progress.overall_progress = 0.0f;
    g_ctx.progress.error_message[0] = '\0';
    SDL_UnlockMutex(g_ctx.mutex);

    g_ctx.thread = SDL_CreateThread(worker_main, "video_convert", NULL);
    if (!g_ctx.thread) {
        set_state(VIDEO_CONVERT_STATE_ERROR, "failed to spawn worker thread");
        return false;
    }
    return true;
}

void video_convert_cancel(void)
{
    SDL_LockMutex(g_ctx.mutex);
    g_ctx.cancel_requested = true;
    SDL_UnlockMutex(g_ctx.mutex);
}

bool video_convert_is_running(void)
{
    bool running;
    SDL_LockMutex(g_ctx.mutex);
    running = (g_ctx.progress.state == VIDEO_CONVERT_STATE_RUNNING);
    SDL_UnlockMutex(g_ctx.mutex);
    return running;
}

void video_convert_get_progress(VideoConvertProgress* out)
{
    if (!out) return;
    SDL_LockMutex(g_ctx.mutex);
    *out = g_ctx.progress;
    SDL_UnlockMutex(g_ctx.mutex);
}

bool video_convert_poll(void)
{
    bool changed;
    SDL_LockMutex(g_ctx.mutex);
    changed = g_ctx.state_changed;
    g_ctx.state_changed = false;
    VideoConvertState s = g_ctx.progress.state;
    SDL_UnlockMutex(g_ctx.mutex);

    /* If the worker just transitioned out of RUNNING, join the thread
     * so we don't leak it. */
    if (g_ctx.thread != NULL
        && s != VIDEO_CONVERT_STATE_RUNNING
        && s != VIDEO_CONVERT_STATE_IDLE) {
        SDL_WaitThread(g_ctx.thread, NULL);
        g_ctx.thread = NULL;
        /* Refresh the gmovie asset scan so the banner/modal hides for
         * any file that successfully converted before an error/cancel. */
        gmovie_scan_video_assets();
    }
    return changed;
}
