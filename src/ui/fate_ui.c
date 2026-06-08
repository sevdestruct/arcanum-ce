#include "ui/fate_ui.h"

#include <limits.h>

#include "game/critter.h"
#include "game/fate.h"
#include "game/hrp.h"
#include "game/mes.h"
#include "ui/charedit_ui.h"
#include "ui/intgame.h"
#include "ui/types.h"
#include "ui/ui_anim.h"

static void fate_ui_create(void);
static void fate_ui_destroy(void);
static bool fate_ui_message_filter(TigMessage* msg);
static void fate_ui_handle_fate_resolved(int64_t obj, int fate);

/**
 * Handle to the parent window.
 *
 * 0x5CAAE0
 */
static tig_window_handle_t fate_ui_window = TIG_WINDOW_HANDLE_INVALID;

/**
 * Fate button configurations.
 *
 * 0x5CAAE8
 */
static UiButtonInfo fate_ui_buttons[FATE_COUNT] = {
    /*                 FATE_FULL_HEAL */ { 223, 4, 293, TIG_BUTTON_HANDLE_INVALID },
    /*       FATE_FORCE_GOOD_REACTION */ { 223, 22, 293, TIG_BUTTON_HANDLE_INVALID },
    /*                  FATE_CRIT_HIT */ { 223, 40, 293, TIG_BUTTON_HANDLE_INVALID },
    /*                 FATE_CRIT_MISS */ { 223, 58, 293, TIG_BUTTON_HANDLE_INVALID },
    /*       FATE_SAVE_AGAINST_MAGICK */ { 223, 76, 293, TIG_BUTTON_HANDLE_INVALID },
    /*          FATE_SPELL_AT_MAXIMUM */ { 223, 94, 293, TIG_BUTTON_HANDLE_INVALID },
    /*     FATE_CRIT_SUCCESS_GAMBLING */ { 223, 112, 293, TIG_BUTTON_HANDLE_INVALID },
    /*         FATE_CRIT_SUCCESS_HEAL */ { 223, 130, 293, TIG_BUTTON_HANDLE_INVALID },
    /*  FATE_CRIT_SUCCESS_PICK_POCKET */ { 223, 148, 293, TIG_BUTTON_HANDLE_INVALID },
    /*       FATE_CRIT_SUCCESS_REPAIR */ { 223, 166, 293, TIG_BUTTON_HANDLE_INVALID },
    /*   FATE_CRIT_SUCCESS_PICK_LOCKS */ { 223, 184, 293, TIG_BUTTON_HANDLE_INVALID },
    /* FATE_CRIT_SUCCESS_DISARM_TRAPS */ { 223, 202, 293, TIG_BUTTON_HANDLE_INVALID },
};

/**
 * Font for rendering fate labels.
 *
 * 0x680ED0
 */
static tig_font_handle_t fate_ui_font;

/**
 * 0x680ED8
 */
static int64_t fate_ui_obj;

/**
 * "fate_ui.mes"
 *
 * 0x680EE0
 */
static mes_file_handle_t fate_ui_mes;

/**
 * Flag indicating whether the fate UI system is initialized.
 *
 * 0x680EE4
 */
static bool fate_ui_initialized;

/**
 * Flag indicating whether the fate UI window is created.
 *
 * 0x680EE8
 */
static bool fate_ui_created;

// CE: spring-tweened vertical slide offset (design-coord pixels). 0
// means the panel sits at its rest position (just below the top
// HUD bar); negative means it's shifted up — at -window_height the
// panel is fully off the top of the screen. Driven by ui_anim_int_to.
// Read each frame by fate_ui_ping which moves the tig window to
// rest_y + fate_ui_slide_offset.
static int fate_ui_slide_offset = 0;

// CE: cached window-art height so fate_ui_close can compute the
// "fully off-screen" target without re-querying art metadata.
static int fate_ui_window_height = 0;

// CE: true between fate_ui_close and the slide-up tween's settle
// callback. Suppresses input on the still-alive panel during dismiss
// and tells fate_ui_finalize_close to destroy when the tween settles.
static bool fate_ui_dismiss_pending;

/**
 * Called when the game is initialized.
 *
 * 0x56FAE0
 */
bool fate_ui_init(GameInitInfo* init_info)
{
    TigFont font_info;

    (void)init_info;

    // Load fate button labels (required).
    if (!mes_load("mes\\fate_ui.mes", &fate_ui_mes)) {
        return false;
    }

    // Create white font for rendering labels.
    font_info.flags = 0;
    tig_art_interface_id_create(229, 0, 0, 0, &(font_info.art_id));
    font_info.str = NULL;
    font_info.color = tig_color_make(255, 255, 255);
    tig_font_create(&font_info, &fate_ui_font);

    // Bind fate change handler.
    fate_set_callback(fate_ui_handle_fate_resolved);

    fate_ui_initialized = true;

    return true;
}

/**
 * Called when the game is being reset.
 *
 * 0x56FBB0
 */
void fate_ui_reset(void)
{
    if (fate_ui_created) {
        // CE: bypass the slide-out animation — game is resetting,
        // ui_anim may not get pinged again before the panel needs
        // to be gone. Synchronous destroy + state cleanup. Any
        // in-flight slide tween targeting fate_ui_slide_offset
        // continues to settle quietly; its on_complete returns
        // early because fate_ui_dismiss_pending is cleared here.
        fate_ui_dismiss_pending = false;
        fate_ui_destroy();
        fate_ui_obj = OBJ_HANDLE_NULL;
        fate_ui_slide_offset = 0;
    }
}

/**
 * Called when the game shuts down.
 *
 * 0x56FBC0
 */
void fate_ui_exit(void)
{
    mes_unload(fate_ui_mes);
    tig_font_destroy(fate_ui_font);
    fate_ui_initialized = false;
}

/**
 * Toggles the fate UI.
 *
 * This function shows or hides the fate UI when the fate button in the top bar
 * is clicked or the `W` key is pressed.
 *
 * The `obj` is mandatory for presenting fate UI and should be the local PC.
 *
 * 0x56FBF0
 */
void fate_ui_toggle(int64_t obj)
{
    if (fate_ui_created) {
        if (fate_ui_dismiss_pending) {
            // CE: mid-dismiss reversal — user re-pressed the
            // toggle before the slide-up tween settled. Clear the
            // dismiss flag and retarget the spring back to 0 from
            // wherever it currently is. The retarget fires the
            // previous on_complete (fate_ui_finalize_close) before
            // swapping in the new (NULL) callback — finalize_close
            // is guarded on dismiss_pending and no-ops because we
            // just cleared it. Result: smooth slide-back from
            // current position, no completion-then-reopen pop.
            fate_ui_dismiss_pending = false;
            // CE: fate is being re-opened (it was mid-slide-out). If it
            // was dismissed because another window took over the screen
            // (intgame_mode_set's reverse hook calls fate_ui_close when a
            // window opens), that window is still up — pop back to MAIN to
            // dismiss it, same as the fresh-open path below. Without this,
            // re-activating fate mid-dismiss slid the panel back but left
            // the other window open, so fate appeared to "stop dismissing
            // windows after the first time." No-op when nothing else is open.
            intgame_mode_set(INTGAME_MODE_MAIN);
            int slide_ms = (int)((float)fate_ui_window_height / 0.85f + 0.5f);
            if (slide_ms < 140) slide_ms = 140;
            if (slide_ms > 350) slide_ms = 350;
            ui_anim_profile_t slide_profile = { slide_ms, 1.2f };
            ui_anim_int_to(&fate_ui_slide_offset, 0, &slide_profile);
            return;
        }
        fate_ui_close();
        return;
    }

    // Make sure PC object is specified and it's not dead.
    if (obj == OBJ_HANDLE_NULL
        || critter_is_dead(obj)) {
        return;
    }

    fate_ui_obj = obj;

    // CE: dismiss any other open interface window before showing fate, so
    // the fate panel doesn't end up stacked on top of (e.g.) the open
    // inventory or character window. This mirrors the sleep button, which
    // resets to MAIN mode (closing inventory / charedit / logbook / wmap /
    // etc.) before opening. Fate is a pure overlay and never pushes an
    // intgame mode of its own, so popping back to MAIN is all that's
    // needed here; it's a no-op when nothing else is open. The reverse
    // case — another window opening while fate is up — is handled in
    // intgame_mode_set, which closes the fate overlay on any non-MAIN
    // mode push.
    intgame_mode_set(INTGAME_MODE_MAIN);

    // Proceed to create the UI.
    fate_ui_create();
}

// CE: ui_anim on_complete — fires when the slide-up dismiss tween
// settles at -fate_ui_window_height. Destroys the now-fully-offscreen
// panel and clears the dismiss-pending flag. Guarded against being
// called for a non-dismiss settle (e.g. reposition retarget): only
// destroys when fate_ui_dismiss_pending is still set.
static void fate_ui_finalize_close(void* ctx_v)
{
    (void)ctx_v;
    if (!fate_ui_dismiss_pending) {
        return;
    }
    fate_ui_dismiss_pending = false;
    if (fate_ui_created) {
        fate_ui_destroy();
        fate_ui_obj = OBJ_HANDLE_NULL;
    }
}

/**
 * Closes the fate UI.
 *
 * 0x56FC40
 */
void fate_ui_close(void)
{
    if (!fate_ui_created) {
        return;
    }
    if (fate_ui_dismiss_pending) {
        // Already animating out — let the in-flight tween finish.
        return;
    }
    fate_ui_dismiss_pending = true;
    // Slide up by the panel's height so the entire window goes off
    // the top of the screen, then destroy from the on_complete
    // callback. tig_window_move runs each frame from fate_ui_ping
    // using the tweened slide_offset value.
    //
    // CE: pixel-velocity-based settle (~0.85 px/ms) so the slide
    // looks the same speed regardless of panel height. Equal
    // settle_ms would make the taller fate panel slide visibly
    // faster than the shorter sleep panel; computing ms from
    // height keeps the px/ms rate constant. Clamp [140, 350] so
    // edge-case heights still feel intentional.
    int slide_ms = (int)((float)fate_ui_window_height / 0.85f + 0.5f);
    if (slide_ms < 140) slide_ms = 140;
    if (slide_ms > 350) slide_ms = 350;
    ui_anim_profile_t slide_profile = { slide_ms, 1.2f };
    ui_anim_int_to_with_complete(&fate_ui_slide_offset,
        -fate_ui_window_height, &slide_profile,
        fate_ui_finalize_close, NULL);
}

// CE: Re-snap the fate window to its docked-below-top-bar position.
// Called by the TAB HUD-crop toggle so the panel sits flush against
// screen-top when the bar is hidden, or back below it when shown.
// No-op when the panel isn't currently open.
//
// Honors fate_ui_slide_offset so a TAB toggle mid-slide doesn't snap
// the panel back to its slide_offset=0 rest position — the per-frame
// fate_ui_ping repositions with the offset re-applied next tick.
void fate_ui_reposition(void)
{
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigRect rect;

    if (!fate_ui_created || fate_ui_window == TIG_WINDOW_HANDLE_INVALID) {
        return;
    }
    if (tig_art_interface_id_create(292, 0, 0, 0, &art_id) != TIG_OK) {
        return;
    }
    if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
        return;
    }

    rect.x = 0;
    rect.y = intgame_hud_top_offset();
    rect.width = art_frame_data.width;
    rect.height = art_frame_data.height;
    hrp_apply(&rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);
    tig_window_move(fate_ui_window, rect.x, rect.y + fate_ui_slide_offset);
}

// CE: per-frame integrator hook — pumps the spring-tweened slide
// offset into a tig_window_move so the panel slides down on appear /
// slides up on dismiss. Cheap fast-path when no slide motion is
// active (early return on rect-unchanged-since-last-frame).
void fate_ui_ping(void)
{
    static int s_last_applied_offset = 0;
    static int s_last_applied_top = 0;

    if (!fate_ui_created || fate_ui_window == TIG_WINDOW_HANDLE_INVALID) {
        // Reset so a fresh open re-applies the move on first ping.
        s_last_applied_offset = INT_MIN;
        s_last_applied_top = INT_MIN;
        return;
    }

    int top = intgame_hud_top_offset();
    if (fate_ui_slide_offset == s_last_applied_offset
        && top == s_last_applied_top) {
        return;
    }
    s_last_applied_offset = fate_ui_slide_offset;
    s_last_applied_top = top;

    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigRect rect;
    if (tig_art_interface_id_create(292, 0, 0, 0, &art_id) != TIG_OK) {
        return;
    }
    if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
        return;
    }
    rect.x = 0;
    rect.y = top;
    rect.width = art_frame_data.width;
    rect.height = art_frame_data.height;
    hrp_apply(&rect, GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);
    tig_window_move(fate_ui_window, rect.x, rect.y + fate_ui_slide_offset);
}

/**
 * Creates the fate UI window.
 *
 * 0x56FC70
 */
void fate_ui_create(void)
{
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    TigRect window_rect;
    TigRect label_rect;
    TigWindowData window_data;
    TigArtBlitInfo blit_info;
    MesFileEntry mes_file_entry;
    int fate;

    // Skip if UI is already created.
    if (fate_ui_created) {
        return;
    }

    if (tig_art_interface_id_create(292, 0, 0, 0, &art_id) != TIG_OK) {
        return;
    }

    if (tig_art_frame_data(art_id, &art_frame_data) != TIG_OK) {
        return;
    }

    // Set up window properties. y-offset tracks the top HUD strip:
    // 41 when the bar is visible, 0 when the user has TAB-hidden it
    // (panel sits flush at screen top instead of leaving a gap).
    window_rect.x = 0;
    window_rect.y = intgame_hud_top_offset();
    window_rect.width = art_frame_data.width;
    window_rect.height = art_frame_data.height;

    window_data.flags = TIG_WINDOW_MESSAGE_FILTER;
    window_data.rect = window_rect;
    window_data.message_filter = fate_ui_message_filter;
    hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);

    if (tig_window_create(&window_data, &fate_ui_window) != TIG_OK) {
        tig_debug_printf("fate_ui_create: ERROR: window create failed!\n");
        exit(EXIT_FAILURE); // FIX: Proper exit code.
    }

    // CE: tig stacks newer windows above older in the same z-class, so
    // the freshly-created fate panel lands ABOVE the top HUD bar by
    // default. Re-promote the top bar to put it back on top so the
    // slide-down animation emerges from BEHIND the bar (rather than
    // being visible above it from frame 1, defeating the slide effect).
    intgame_hud_promote_top_strip();

    // Draw background.
    window_rect.x = 0;
    window_rect.y = 0;

    blit_info.flags = 0;
    blit_info.art_id = art_id;
    blit_info.src_rect = &window_rect;
    blit_info.dst_rect = &window_rect;
    tig_window_blit_art(fate_ui_window, &blit_info);

    tig_font_push(fate_ui_font);

    // Set up initial label rect.
    label_rect.x = 11;
    label_rect.y = 3;
    label_rect.width = window_rect.width - 11;
    label_rect.height = 18;

    // Write fate button label from the message file.
    for (fate = 0; fate < FATE_COUNT; fate++) {
        mes_file_entry.num = fate;
        mes_get_msg(fate_ui_mes, &mes_file_entry);
        tig_window_text_write(fate_ui_window, mes_file_entry.str, &label_rect);
        label_rect.y += label_rect.height;
    }

    tig_font_pop();

    // Create fate buttons.
    for (fate = 0; fate < FATE_COUNT; fate++) {
        intgame_button_create_ex(fate_ui_window,
            &window_rect,
            &(fate_ui_buttons[fate]),
            fate_is_activated(fate_ui_obj, fate)
                ? TIG_BUTTON_ON
                : TIG_BUTTON_TOGGLE);
    }

    fate_ui_created = true;

    // CE: cache height for the slide-up dismiss target.
    fate_ui_window_height = art_frame_data.height;

    // CE: spring-driven slide-in entrance. Start the panel at
    // -window_height (fully above the top edge of the screen) so it
    // visibly descends from off-screen to its rest position. The
    // window has already been created at its rest screen rect by
    // tig_window_create; we shift it up immediately via
    // tig_window_move, then ui_anim_int_to springs the offset back
    // to 0. fate_ui_ping pumps the per-frame tig_window_move.
    fate_ui_slide_offset = -fate_ui_window_height;
    fate_ui_dismiss_pending = false;
    {
        TigRect rest;
        rest.x = 0;
        rest.y = intgame_hud_top_offset();
        rest.width = art_frame_data.width;
        rest.height = art_frame_data.height;
        hrp_apply(&rest, GRAVITY_CENTER_HORIZONTAL | GRAVITY_TOP);
        tig_window_move(fate_ui_window, rest.x, rest.y + fate_ui_slide_offset);
    }
    // CE: pixel-velocity-based settle — same rate as the dismiss
    // slide-up in fate_ui_close, so entrance and exit feel
    // symmetric and the fate panel matches the visual speed of the
    // (shorter) sleep panel.
    int slide_ms = (int)((float)fate_ui_window_height / 0.85f + 0.5f);
    if (slide_ms < 140) slide_ms = 140;
    if (slide_ms > 350) slide_ms = 350;
    ui_anim_profile_t slide_profile = { slide_ms, 1.2f };
    ui_anim_int_to(&fate_ui_slide_offset, 0, &slide_profile);
}

/**
 * Destroys the fate UI window.
 *
 * 0x56FE40
 */
void fate_ui_destroy(void)
{
    if (fate_ui_created) {
        if (tig_window_destroy(fate_ui_window) == TIG_OK) {
            fate_ui_window = TIG_WINDOW_HANDLE_INVALID;
        }
        fate_ui_created = false;
    }
}

/**
 * Called when an event occurred.
 *
 * 0x56FE70
 */
bool fate_ui_message_filter(TigMessage* msg)
{
    int fate;

    switch (msg->type) {
    case TIG_MESSAGE_BUTTON:
        for (fate = 0; fate < FATE_COUNT; fate++) {
            if (fate_ui_buttons[fate].button_handle == msg->data.button.button_handle) {
                if (msg->data.button.state == TIG_BUTTON_STATE_PRESSED) {
                    // Button is pressed - attempt to activate appropriate fate.
                    if (fate_activate(fate_ui_obj, fate)) {
                        // Refresh UI.
                        iso_interface_refresh();
                    } else {
                        // Fate was not activated - simulate button release by
                        // programatically changing state so that appropriate
                        // art is displayed.
                        tig_button_state_change(fate_ui_buttons[fate].button_handle, TIG_BUTTON_STATE_RELEASED);

                        // Since the user just pressed this button the mouse
                        // cursor is within button bounds - mark it as
                        // highlighted.
                        tig_button_state_change(fate_ui_buttons[fate].button_handle, TIG_BUTTON_STATE_MOUSE_INSIDE);
                    }
                } else if (msg->data.button.state == TIG_BUTTON_STATE_RELEASED) {
                    // Button is released - attempt to deactivate appropriate
                    // fate.
                    if (fate_deactivate(fate_ui_obj, fate)) {
                        // Refresh UI.
                        iso_interface_refresh();
                    }
                }

                return true;
            }
        }
        break;
    case TIG_MESSAGE_KEYBOARD:
        // Pressing space closes fate UI.
        if (msg->data.keyboard.scancode == SDL_SCANCODE_SPACE
            && msg->data.keyboard.pressed) {
            fate_ui_close();
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

/**
 * Called when fate has been resolved.
 *
 * 0x56FF40
 */
void fate_ui_handle_fate_resolved(int64_t obj, int fate)
{
    if (fate_ui_obj == obj) {
        tig_button_state_change(fate_ui_buttons[fate].button_handle, TIG_BUTTON_STATE_RELEASED);
    }

    charedit_refresh();
}
