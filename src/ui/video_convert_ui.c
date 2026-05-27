/*
 * video_convert_ui.c -- main-menu modal that drives video_convert.c.
 *
 * The implementation reuses tig_window_modal_dialog (TIG's existing
 * one-shot OK/Cancel widget) in a small state machine:
 *
 *     PROMPT_INITIAL ──OK──► RUNNING ──complete──► DONE
 *           │                  │
 *           ├──Cancel──► DISMISSED
 *           │                  │
 *           └─ no ffmpeg ─► PROMPT_NO_FFMPEG ──OK──► (redetect) ──► PROMPT_INITIAL
 *                                                                       │
 *                                                                       └─ Cancel ─► DISMISSED
 *
 * The progress modal is CANCEL-only and intentionally static-text -- the
 * tig_window_modal_dialog API renders text once at open time, so we
 * keep the message generic ("Converting videos. Please wait. Click
 * Cancel to abort.") and let the user gauge progress by elapsed wall
 * time. A future polish PR can swap this for a custom window with a
 * live progress bar.
 */

#include "ui/video_convert_ui.h"

#include <stdio.h>
#include <string.h>

#include <tig/tig.h>

#include "game/gamelib.h"
#include "game/gmovie.h"
#include "game/hrp.h"
#include "game/settings.h"
#include "game/video_convert.h"

#define SETTING_NEVER_ASK "video convert never ask"

static bool g_shown_this_session;
static bool g_settings_registered;

static void ensure_settings_registered(void)
{
    if (g_settings_registered) return;
    settings_register(&settings, SETTING_NEVER_ASK, "0", NULL);
    g_settings_registered = true;
}

bool video_convert_ui_get_never_ask(void)
{
    ensure_settings_registered();
    return settings_get_value(&settings, SETTING_NEVER_ASK) != 0;
}

void video_convert_ui_set_never_ask(bool value)
{
    ensure_settings_registered();
    settings_set_value(&settings, SETTING_NEVER_ASK, value ? 1 : 0);
}

/* -- helpers ------------------------------------------------------- */

static TigWindowModalDialogChoice show_modal(int type, const char* text,
    TigWindowDialogProcess process)
{
    TigWindowModalDialogInfo info;
    TigWindowModalDialogChoice choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
    memset(&info, 0, sizeof(info));
    info.type = type;
    info.x = 237;
    info.y = 232;
    info.text = text;
    info.keys[TIG_WINDOW_MODAL_DIALOG_CHOICE_OK] = 'y';
    info.keys[TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL] = 'n';
    info.process = process;
    info.redraw = gamelib_redraw;
    hrp_center(&info.x, &info.y);
    tig_window_modal_dialog(&info, &choice);
    return choice;
}

/* Modal layout constants -- mirrors tig/src/window.c so the overlay
 * lands exactly on the modal's text rect. Kept local rather than
 * exported to avoid widening the TIG public surface. */
#define VC_MODAL_WIDTH      325
#define VC_MODAL_HEIGHT     136
#define VC_MODAL_TEXT_X     30
#define VC_MODAL_TEXT_Y     14
#define VC_MODAL_TEXT_W     265
#define VC_MODAL_TEXT_H     65
#define VC_MODAL_BG_ART     822  /* MODAL_DIALOG_BACKGROUND_ART_NUM */

/* Screen-space rect of the open progress modal; used to look up its
 * window handle in the process callback. Populated by run_flow just
 * before the progress modal is shown. */
static TigRect g_progress_modal_rect;
static tig_window_handle_t g_progress_modal_win = TIG_WINDOW_HANDLE_INVALID;

static void progress_render(const VideoConvertProgress* p)
{
    /* Lazily locate the modal window by probing the centre of where
     * we asked tig_window_modal_dialog to open it. tig_window_modal_dialog
     * doesn't expose its handle; tig_window_get_at_position does. */
    if (g_progress_modal_win == TIG_WINDOW_HANDLE_INVALID) {
        int cx = g_progress_modal_rect.x + g_progress_modal_rect.width / 2;
        int cy = g_progress_modal_rect.y + g_progress_modal_rect.height / 2;
        tig_window_get_at_position(cx, cy, &g_progress_modal_win);
        if (g_progress_modal_win == TIG_WINDOW_HANDLE_INVALID) {
            return;
        }
    }

    /* Re-blit the background art over the text rect to wipe the prior
     * frame's progress text, then draw the fresh string. Restricting
     * the blit to the text rect leaves the modal's Cancel button at
     * the bottom untouched. */
    TigRect text_rect;
    text_rect.x = VC_MODAL_TEXT_X;
    text_rect.y = VC_MODAL_TEXT_Y;
    text_rect.width = VC_MODAL_TEXT_W;
    text_rect.height = VC_MODAL_TEXT_H;

    TigArtBlitInfo blit_info;
    memset(&blit_info, 0, sizeof(blit_info));
    blit_info.flags = 0;
    blit_info.src_rect = &text_rect;
    blit_info.dst_rect = &text_rect;
    if (tig_art_interface_id_create(VC_MODAL_BG_ART, 0, 0, 0, &blit_info.art_id) != TIG_OK) {
        return;
    }
    tig_window_blit_art(g_progress_modal_win, &blit_info);

    /* Build the progress string. ASCII bar so it always renders in
     * the bitmap font; #/- chosen because they share the same glyph
     * width in monospace and look clean even with proportional
     * fallback fonts. */
    char bar[33];
    const int bar_width = 32;
    int filled = (int)(p->overall_progress * (float)bar_width + 0.5f);
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    for (int i = 0; i < bar_width; ++i) bar[i] = i < filled ? '#' : '.';
    bar[bar_width] = '\0';

    int pct = (int)(p->overall_progress * 100.0f + 0.5f);
    if (pct > 100) pct = 100;

    char text[512];
    if (p->total_files > 0) {
        snprintf(text, sizeof(text),
            "Converting video %u of %u\n"
            "%s\n"
            "[%s] %d%%",
            p->completed_files + (p->state == VIDEO_CONVERT_STATE_RUNNING ? 1 : 0),
            p->total_files,
            p->current_filename[0] ? p->current_filename : "...",
            bar, pct);
    } else {
        snprintf(text, sizeof(text), "Converting Arcanum's videos...");
    }

    tig_window_text_write(g_progress_modal_win, text, &text_rect);
}

/* Process callback for the RUNNING progress modal. Returns true (and
 * sets choice) when video_convert finishes one way or another. */
static bool progress_process(TigWindowModalDialogChoice* choice_ptr)
{
    /* Drain worker state transitions. */
    video_convert_poll();
    VideoConvertProgress p;
    video_convert_get_progress(&p);

    progress_render(&p);

    if (p.state == VIDEO_CONVERT_STATE_RUNNING) {
        return false;
    }
    /* Map terminal states to the modal's binary choice. We use OK for
     * "completed normally" (whether successfully or with an error
     * surfaced afterward by us) and CANCEL for explicit user cancel. */
    *choice_ptr = (p.state == VIDEO_CONVERT_STATE_CANCELED)
        ? TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL
        : TIG_WINDOW_MODAL_DIALOG_CHOICE_OK;
    return true;
}

/* Build the body text for the prompt modal. Kept intentionally vague
 * about the underlying mechanism -- users don't need to think about
 * codecs or containers, only that a one-time setup step is needed
 * before Arcanum's videos play. */
static const char* build_prompt_text(void)
{
    static char buf[512];
    unsigned int n = gmovie_unconverted_count();
    snprintf(buf, sizeof(buf),
        "To play Arcanum's videos on this system, %u file%s need%s to "
        "be converted. This is a one-time step that takes a few "
        "minutes.\n\n"
        "[OK] Convert now\n"
        "[Cancel] Skip -- the prompt will return next launch.",
        n,
        n == 1 ? "" : "s",
        n == 1 ? "s" : "");
    return buf;
}

static const char* build_no_ffmpeg_text(void)
{
    static char buf[768];
    /* Choose platform-specific install line via predefined macros.
     * The install step requires naming the tool -- there's no way to
     * tell a user how to install something while keeping it abstract
     * -- but the body text is framed around "converting videos"
     * rather than "we depend on ffmpeg". */
#if defined(__APPLE__)
    const char* hint = "macOS: brew install ffmpeg";
#elif defined(__linux__)
    const char* hint = "Linux: sudo apt install ffmpeg (or dnf / pacman)";
#elif defined(_WIN32)
    const char* hint = "Windows: https://ffmpeg.org/download.html";
#else
    const char* hint = "Install ffmpeg from https://ffmpeg.org/download.html";
#endif
    snprintf(buf, sizeof(buf),
        "Converting Arcanum's videos needs a free media tool that is "
        "not yet installed on this machine.\n\n%s\n\n"
        "[OK] I've installed it -- try again\n"
        "[Cancel] Skip this session.",
        hint);
    return buf;
}

static const char* build_mobile_text(void)
{
    return "To play Arcanum's videos on this device, convert them on a "
           "desktop first using scripts/convert_videos.py, then copy "
           "the resulting files alongside the rest of your game data.";
}

static const char* build_done_text(unsigned int converted)
{
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "Done -- %u video%s converted. They will play normally from now on.",
        converted, converted == 1 ? "" : "s");
    return buf;
}

static const char* build_error_text(const char* err)
{
    static char buf[512];
    /* Strip the leading "ffmpeg (NNN): " prefix our spawn code adds
     * so the user-facing message doesn't lead with the tool name --
     * the underlying error text is still surfaced. */
    const char* clean = err && err[0] ? err : "unknown error";
    if (strncmp(clean, "ffmpeg (", 8) == 0) {
        const char* colon = strstr(clean, "): ");
        if (colon) clean = colon + 3;
    }
    snprintf(buf, sizeof(buf),
        "Video conversion didn't finish: %s\n\n"
        "Some files may have been converted. The prompt will return "
        "next launch so you can retry.",
        clean);
    return buf;
}

/* -- main flow ----------------------------------------------------- */

static void run_flow(void)
{
    /* Always re-scan before showing -- covers the case where the user
     * dropped files into movies/ between sessions. */
    gmovie_scan_video_assets();
    if (gmovie_unconverted_count() == 0) {
        return;
    }

    /* mainmenu_ui_start() hides the mouse cursor unconditionally on
     * the default startup path so the cutscene play-out can run
     * full-screen-clean. When our modal opens before the menu's
     * first input event un-hides it, the user sees the dialog but
     * can't aim at the buttons. Show the cursor around the modal
     * sequence; the menu will re-show it on its own once the user
     * starts interacting normally. */
    tig_mouse_show();

    VideoConvertFfmpegStatus st = video_convert_detect_ffmpeg();

    if (st == VIDEO_CONVERT_FFMPEG_UNSUPPORTED_PLATFORM) {
        show_modal(TIG_WINDOW_MODAL_DIALOG_TYPE_OK, build_mobile_text(), NULL);
        return;
    }

    /* Loop here so the "no ffmpeg" → install → recheck path can lead
     * straight into the prompt without dropping back to the menu. */
    for (;;) {
        if (st == VIDEO_CONVERT_FFMPEG_ABSENT) {
            TigWindowModalDialogChoice c = show_modal(
                TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL,
                build_no_ffmpeg_text(), NULL);
            if (c != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
                return;
            }
            st = video_convert_redetect_ffmpeg();
            continue;
        }

        if (st != VIDEO_CONVERT_FFMPEG_PRESENT) return;

        TigWindowModalDialogChoice c = show_modal(
            TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL,
            build_prompt_text(), NULL);
        if (c != TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
            return;
        }
        break;
    }

    /* Kick off conversion. */
    if (!video_convert_start()) {
        show_modal(TIG_WINDOW_MODAL_DIALOG_TYPE_OK,
            "Could not start conversion (no work to do, or a previous "
            "run is still in flight).", NULL);
        return;
    }

    /* Capture the modal's screen-space rect so progress_render() can
     * locate the modal window in the process callback. tig_window_
     * modal_dialog doesn't expose its window handle; we look it up
     * via tig_window_get_at_position(centre-of-modal). The x/y/dims
     * here mirror what show_modal() passes through. */
    g_progress_modal_rect.x = 237;
    g_progress_modal_rect.y = 232;
    g_progress_modal_rect.width = VC_MODAL_WIDTH;
    g_progress_modal_rect.height = VC_MODAL_HEIGHT;
    hrp_center(&g_progress_modal_rect.x, &g_progress_modal_rect.y);
    g_progress_modal_win = TIG_WINDOW_HANDLE_INVALID;

    /* Progress modal -- CANCEL only. process() closes it when the
     * worker reaches DONE / CANCELED / ERROR. The body text below
     * is what gets painted on modal-open; progress_render() then
     * overwrites it every tick with a live progress bar. */
    TigWindowModalDialogChoice c = show_modal(
        TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL,
        "Converting Arcanum's videos...",
        progress_process);
    if (c == TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL) {
        video_convert_cancel();
        /* Wait for the worker to actually stop and clean up. */
        while (video_convert_is_running()) {
            video_convert_poll();
            tig_ping();
        }
    }

    /* Drain final state for the done/error report. */
    VideoConvertProgress p;
    video_convert_get_progress(&p);
    if (p.state == VIDEO_CONVERT_STATE_ERROR) {
        show_modal(TIG_WINDOW_MODAL_DIALOG_TYPE_OK,
            build_error_text(p.error_message), NULL);
    } else if (p.state == VIDEO_CONVERT_STATE_DONE) {
        show_modal(TIG_WINDOW_MODAL_DIALOG_TYPE_OK,
            build_done_text(p.completed_files), NULL);

        /* Some video files only become visible to TIG's file
         * enumeration after additional repositories mount during init.
         * Re-scan in case more .bik files turned up after we built the
         * conversion list; if so, ask the user one more time. */
        gmovie_scan_video_assets();
        if (gmovie_unconverted_count() > 0) {
            g_shown_this_session = false;
            run_flow();
        }
    }
    /* CANCELED falls through silently -- the user already knows. */
}

void video_convert_ui_show_if_needed(void)
{
    ensure_settings_registered();
    if (g_shown_this_session) return;
    if (video_convert_ui_get_never_ask()) return;

    /* The scan must run here, not in gmovie_mod_load: the main menu
     * paints before any game module is loaded (modules load when the
     * user picks "New Game" / "Load"). Without this call the count is
     * always zero on first menu paint and the modal silently no-ops.
     *
     * tig_file_list_create works against the global file system mount
     * which is already set up by tig_file_init at app startup, so
     * scanning here is safe even before a module loads. */
    gmovie_scan_video_assets();
    if (gmovie_unconverted_count() == 0) {
        g_shown_this_session = true;
        return;
    }
    g_shown_this_session = true;
    run_flow();
}

void video_convert_ui_force(void)
{
    g_shown_this_session = false;
    run_flow();
}
