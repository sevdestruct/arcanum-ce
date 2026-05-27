/*
 * video_convert_ui.h — main-menu conversion prompt + progress UI.
 *
 * The UI is intentionally cobbled from existing tig_window_modal_dialog
 * primitives. It is functional polish; the user has flagged it as
 * placeholder until a properly styled modal lands.
 *
 * Flow:
 *   1. video_convert_ui_show_if_needed() is called from the main-menu
 *      window init. It returns without doing anything if all assets are
 *      already converted, the "Don't ask again" preference is set, or
 *      the modal has already been shown this session.
 *   2. Otherwise it shows an OK/Cancel prompt describing the conversion.
 *      - OK: kick off video_convert_start(), then show a CANCEL-only
 *        progress modal that closes when the worker reports DONE /
 *        CANCELED / ERROR.
 *      - Cancel: dismiss for this session.
 *   3. After completion, a short OK modal reports the result, then
 *      gmovie_scan_video_assets() re-runs so the count snaps to zero.
 *
 * Reset / re-invocation: video_convert_ui_force() flips the
 * "already shown this session" flag back off and re-runs the flow.
 * The Options menu's "Re-convert videos…" action uses this.
 */

#ifndef ARCANUM_UI_VIDEO_CONVERT_UI_H_
#define ARCANUM_UI_VIDEO_CONVERT_UI_H_

#include <stdbool.h>

void video_convert_ui_show_if_needed(void);
void video_convert_ui_force(void);

/* "Don't ask again" persistence. The setting is registered in
 * mainmenu_ui_init alongside other UI prefs. */
bool video_convert_ui_get_never_ask(void);
void video_convert_ui_set_never_ask(bool value);

#endif /* ARCANUM_UI_VIDEO_CONVERT_UI_H_ */
