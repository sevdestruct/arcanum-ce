#include "game/dialog_camera.h"

#include <math.h>

#include "game/gamelib.h"
#include "game/iso_zoom.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/settings.h"
#include "game/tb.h"
#include "game/tc.h"
#include "tig/timer.h"

// Padding component constants (screen pixels).
// npc_pad_top and pad_side are computed per-frame in compute_target because
// the head-gap scales with zoom (matches adjust_text_bubble_rect in tb.c).
//
// Head gap formula: HEAD_SCALE*z + HEAD_FIXED  (matches tb.c TB_POS_TOP offset)
//   HEAD_SCALE*z  clears the NPC sprite which scales with zoom
//   HEAD_FIXED    is a constant screen-px gap above the head; doesn't balloon
//
//   npc_pad_top = UI_TOP  + HEAD_SCALE*z + HEAD_FIXED + BUBBLE_H + AESTHETIC_Y
//   pc_pad_top  = UI_TOP  + AESTHETIC_Y   (no bubble above player)
//   pad_bot     = UI_BOT  + AESTHETIC_Y
//   pad_side    = BUBBLE_HALF_W + AESTHETIC_X
#define DIALOGUE_CAM_UI_TOP  GAME_UI_BAR_TOP
#define DIALOGUE_CAM_UI_BOT  GAME_UI_BAR_BOTTOM
#define DIALOGUE_CAM_HEAD_SCALE     75   // NPC sprite height above tile centre (world px, ×zoom)
#define DIALOGUE_CAM_HEAD_FIXED     25   // constant screen-px gap above NPC head (matches tb.c)
#define DIALOGUE_CAM_BUBBLE_H      100   // typical bubble height for pan positioning
                                         // visible headroom = (this - actual_h) + AESTHETIC_Y
                                         // lower = tighter fit; raise if bubbles clip top
#define DIALOGUE_CAM_ZOOM_BUBBLE_H  80   // bubble height used only for zoom-to-fit threshold
#define DIALOGUE_CAM_BUBBLE_HALF_W 100   // half bubble width   (fixed px)
#define DIALOGUE_CAM_AESTHETIC_Y  20.0f  // breathing room top/bottom base (×zoom)
#define DIALOGUE_CAM_AESTHETIC_X   120   // breathing room left/right (more screen equity)

// Tween duration in milliseconds.
#define DIALOGUE_CAM_TWEEN_MS 400.0f

static bool cam_tween_active;
static int64_t cam_start_ox;
static int64_t cam_start_oy;
static int64_t cam_target_ox;
static int64_t cam_target_oy;
static unsigned int cam_tween_start_time;
static float cam_pre_dialog_zoom;
static float cam_dialog_zoom_floor; // lowest zoom target reached during this session
static bool cam_zoom_saved;
static bool cam_zoom_restore_pending;
static float cam_zoom_restore_target;

static bool dialog_camera_npc_fits_after_pan(int64_t dx,
    int64_t dy,
    int64_t npc_zsx,
    int64_t npc_zsy,
    float z,
    TigRect* cr,
    int pad_side,
    int npc_pad_top,
    int pad_bot)
{
    float final_npc_x = (float)npc_zsx + (float)dx * z;
    float final_npc_y = (float)npc_zsy + (float)dy * z;

    return final_npc_x >= (float)pad_side
        && final_npc_x <= (float)(cr->width - pad_side)
        && final_npc_y >= (float)npc_pad_top
        && final_npc_y <= (float)(cr->height - pad_bot);
}

void dialog_camera_init(void)
{
    settings_register(&settings, DIALOGUE_CAMERA_MODE_KEY,       "0", NULL);
    settings_register(&settings, DIALOGUE_CAMERA_TWEEN_BACK_KEY, "0", NULL);
    settings_register(&settings, DIALOGUE_CAMERA_ZOOM_TO_FIT_KEY, "1", NULL);
    cam_tween_active = false;
    cam_zoom_saved = false;
    cam_dialog_zoom_floor = ISO_ZOOM_MAX;
    cam_zoom_restore_pending = false;
    cam_zoom_restore_target = 1.0f;
}

bool dialog_camera_is_animating(void)
{
    return cam_tween_active;
}


// Returns the scroll delta (world-pixel space) needed to achieve the desired
// camera position for the given mode.  Also calls iso_zoom_set_target if the
// two characters are too far apart to both fit on screen after panning.
static void compute_target(int64_t pc_loc, int64_t npc_loc, int mode,
                            int64_t* out_dx, int64_t* out_dy)
{
    int64_t pc_sx, pc_sy, npc_sx, npc_sy, tmp;
    TigRect cr;

    location_xy(pc_loc,  &pc_sx,  &pc_sy);
    location_xy(npc_loc, &npc_sx, &npc_sy);

    // Use tile centre (+40, +20) as the reference point for each character.
    pc_sx  += 40;  pc_sy  += 20;
    npc_sx += 40;  npc_sy += 20;

    gamelib_get_iso_content_rect(&cr);

    int64_t cx = cr.width  / 2;
    int64_t cy = cr.height / 2;
    bool zoom_fit_blocked = false;
    bool zoom_to_fit_enabled = settings_get_value(&settings, DIALOGUE_CAMERA_ZOOM_TO_FIT_KEY) != 0;
    int64_t npc_center_dx;
    int64_t npc_center_dy;
    int64_t fallback_dx = 0;
    int64_t fallback_dy = 0;
    int64_t candidate_dx = 0;
    int64_t candidate_dy = 0;
    float npc_min_dx = 0.0f;
    float npc_max_dx = 0.0f;
    float npc_min_dy = 0.0f;
    float npc_max_dy = 0.0f;

    *out_dx = 0;
    *out_dy = 0;

    // Use the settled zoom target (not the mid-lerp current value) so the pan
    // is computed for the zoom level that will be stable when the tween lands.
    float z = iso_zoom_target();

    // Zoom-to-fit check: resolve this FIRST so that if a zoom-out is needed,
    // the updated target is used for all subsequent padding and pan math.
    float sp_x = fabsf((float)(npc_sx - pc_sx));
    float sp_y = fabsf((float)(npc_sy - pc_sy));

    // Compute padding once at the current target z to evaluate fit.
    // npc_pad_top accounts for the speech bubble above the NPC's head.
    // pc_pad_top is smaller — the player has no bubble above them.
    int aesthetic_y = (int)(DIALOGUE_CAM_AESTHETIC_Y * z);
    int npc_pad_top = DIALOGUE_CAM_UI_TOP
                    + (int)((float)DIALOGUE_CAM_HEAD_SCALE * z) + DIALOGUE_CAM_HEAD_FIXED
                    + DIALOGUE_CAM_BUBBLE_H
                    + aesthetic_y;
    int pc_pad_top  = DIALOGUE_CAM_UI_TOP + aesthetic_y;
    int pad_bot     = DIALOGUE_CAM_UI_BOT + aesthetic_y;
    int pad_side    = DIALOGUE_CAM_BUBBLE_HALF_W + DIALOGUE_CAM_AESTHETIC_X;

    // Zoom-to-fit: determine which character is north (smaller screen y = higher
    // on screen) because that controls the effective top padding.
    // When NPC is north its bubble must clear the chrome, so the top pad has
    // a z-dependent component (HEAD_GAP * z).  When PC is north, PC has no
    // bubble, so the top pad is z-independent.
    //
    // Head gap = HEAD_SCALE*z + HEAD_FIXED (matches tb.c TB_POS_TOP).
    // Rewrite the zoom-to-fit inequality:
    //   z * sp_y + HEAD_SCALE*z + HEAD_FIXED > y_avail_raw
    //   z * (sp_y + HEAD_SCALE) > y_avail_raw - HEAD_FIXED
    // So fold HEAD_FIXED into y_fixed_top and use HEAD_SCALE as the z-coefficient.
    // z_needed = (y_avail_raw - HEAD_FIXED) / (sp_y + HEAD_SCALE)  — exact, no approximation.
    bool npc_is_north = (npc_sy <= pc_sy);
    float y_head_coeff = npc_is_north ? (float)DIALOGUE_CAM_HEAD_SCALE : 0.0f;
    // Use ZOOM_BUBBLE_H (typical line height) here, not the max BUBBLE_H.
    // Pan positioning uses the full max so there's always room for long lines;
    // zoom-to-fit only engages when characters are genuinely too far apart for
    // a typical exchange — not as a precaution against rare worst-case bubbles.
    float y_fixed_top  = npc_is_north
        ? (float)(DIALOGUE_CAM_UI_TOP + DIALOGUE_CAM_HEAD_FIXED + DIALOGUE_CAM_ZOOM_BUBBLE_H + DIALOGUE_CAM_AESTHETIC_Y)
        : (float)(DIALOGUE_CAM_UI_TOP + DIALOGUE_CAM_AESTHETIC_Y);
    float y_avail = (float)cr.height - y_fixed_top - (float)pad_bot;

    bool need_zoom_x = sp_x > 0.0f
        && (sp_x * z + (float)(pad_side * 2) > (float)cr.width);
    bool need_zoom_y = sp_y > 0.0f
        && (z * (sp_y + y_head_coeff) > y_avail);

    if (need_zoom_x || need_zoom_y) {
        if (!zoom_to_fit_enabled || !iso_zoom_is_available()) {
            zoom_fit_blocked = true;
        } else {
        float zx = need_zoom_x
            ? ((float)cr.width - (float)(pad_side * 2)) / sp_x
            : 1e9f;  // sentinel: x axis doesn't constrain zoom
        float zy = need_zoom_y
            ? y_avail / (sp_y + y_head_coeff)
            : 1e9f;  // sentinel: y axis doesn't constrain zoom
        float z_needed = (zx < zy) ? zx : zy;
        // Only zoom out if z_needed is smaller than the session floor — never
        // zoom back in during a dialogue.  The floor accumulates outward and
        // resets only when the full dialogue session ends.
        if (z_needed < cam_dialog_zoom_floor) {
            iso_zoom_set_target(z_needed);  // clamped to ISO_ZOOM_MIN inside
            // Re-read the clamped target and update the session floor.
            z = iso_zoom_target();
            cam_dialog_zoom_floor = z;
            aesthetic_y = (int)(DIALOGUE_CAM_AESTHETIC_Y * z);
            npc_pad_top = DIALOGUE_CAM_UI_TOP
                        + (int)((float)DIALOGUE_CAM_HEAD_SCALE * z) + DIALOGUE_CAM_HEAD_FIXED
                        + DIALOGUE_CAM_BUBBLE_H
                        + aesthetic_y;
            pc_pad_top  = DIALOGUE_CAM_UI_TOP + aesthetic_y;
            pad_bot     = DIALOGUE_CAM_UI_BOT + aesthetic_y;
        }
        }
    }

    npc_center_dx = cx - npc_sx;
    npc_center_dy = (cy + (int64_t)((float)(npc_pad_top - pad_bot) / (2.0f * z))) - npc_sy;

    // Compute zoomed screen positions for the final settled z.
    // location_xy returns unzoomed world-pixel screen coords; apply the zoom
    // transform to get actual screen positions, then divide by z for world-px
    // deltas.
    int64_t npc_zsx = cx + (int64_t)((float)(npc_sx - cx) * z);
    int64_t npc_zsy = cy + (int64_t)((float)(npc_sy - cy) * z);
    int64_t pc_zsx  = cx + (int64_t)((float)(pc_sx  - cx) * z);
    int64_t pc_zsy  = cy + (int64_t)((float)(pc_sy  - cy) * z);

    // Shared NPC-safe pan bounds, plus the minimal NPC-priority fallback used
    // whenever a framing mode cannot fully honor its preferred target.
    npc_min_dx = (float)(pad_side - npc_zsx) / z;
    npc_max_dx = (float)((cr.width - pad_side) - npc_zsx) / z;
    {
        float pc_min_dx  = (float)(pad_side - pc_zsx) / z;
        float pc_max_dx  = (float)((cr.width - pad_side) - pc_zsx) / z;
        float lo_dx = npc_min_dx > pc_min_dx ? npc_min_dx : pc_min_dx;
        float hi_dx = npc_max_dx < pc_max_dx ? npc_max_dx : pc_max_dx;
        if (lo_dx <= hi_dx) {
            if      (0.0f < lo_dx) fallback_dx = (int64_t)lo_dx;
            else if (0.0f > hi_dx) fallback_dx = (int64_t)hi_dx;
        } else {
            if      (npc_zsx < pad_side)            fallback_dx = (int64_t)npc_min_dx;
            else if (npc_zsx > cr.width - pad_side) fallback_dx = (int64_t)npc_max_dx;
        }
    }

    npc_min_dy = (float)(npc_pad_top - npc_zsy) / z;
    npc_max_dy = (float)((cr.height - pad_bot) - npc_zsy) / z;
    {
        float pc_min_dy  = (float)(pc_pad_top  - pc_zsy)  / z;
        float pc_max_dy  = (float)((cr.height - pad_bot) - pc_zsy)  / z;
        float lo_dy = npc_min_dy > pc_min_dy ? npc_min_dy : pc_min_dy;
        float hi_dy = npc_max_dy < pc_max_dy ? npc_max_dy : pc_max_dy;
        if (lo_dy <= hi_dy) {
            if      (0.0f < lo_dy) fallback_dy = (int64_t)lo_dy;
            else if (0.0f > hi_dy) fallback_dy = (int64_t)hi_dy;
        } else {
            if      (npc_zsy < npc_pad_top)         fallback_dy = (int64_t)npc_min_dy;
            else if (npc_zsy > cr.height - pad_bot) fallback_dy = (int64_t)npc_max_dy;
        }
    }

    switch (mode) {
    case 0:
        break;
    case 1:
        // Center on midpoint between player and NPC, same vertical bias.
        candidate_dx = cx - (pc_sx + npc_sx) / 2;
        candidate_dy = (cy + (int64_t)((float)(npc_pad_top - pad_bot) / (2.0f * z))) - (pc_sy + npc_sy) / 2;
        break;
    case 2:
        // Center on NPC.  The vertical bias is divided by z so it represents
        // the same screen-pixel offset regardless of zoom.
        candidate_dx = npc_center_dx;
        candidate_dy = npc_center_dy;
        break;
    case 3:
        // Center on player, matching the legacy-feeling framing mode, but
        // clamp toward that target so the NPC speaker remains properly framed.
        candidate_dx = cx - pc_sx;
        candidate_dy = cy - pc_sy;
        if ((float)candidate_dx < npc_min_dx) {
            candidate_dx = (int64_t)npc_min_dx;
        } else if ((float)candidate_dx > npc_max_dx) {
            candidate_dx = (int64_t)npc_max_dx;
        }
        if ((float)candidate_dy < npc_min_dy) {
            candidate_dy = (int64_t)npc_min_dy;
        } else if ((float)candidate_dy > npc_max_dy) {
            candidate_dy = (int64_t)npc_max_dy;
        }
        break;
    default:
        break;
    }

    if (mode == 0) {
        *out_dx = fallback_dx;
        *out_dy = fallback_dy;
    } else if (mode == 3) {
        // Player-center mode should always move as far toward centering the
        // player as the NPC-safe bounds allow, rather than collapsing to the
        // minimal fallback just because exact centering is impossible.
        *out_dx = candidate_dx;
        *out_dy = candidate_dy;
    } else if (zoom_fit_blocked
        || !dialog_camera_npc_fits_after_pan(candidate_dx,
               candidate_dy,
               npc_zsx,
               npc_zsy,
               z,
               &cr,
               pad_side,
               npc_pad_top,
               pad_bot)) {
        // Shared fallback for non-minimal modes: keep the NPC speaker and
        // bubble cleanly framed with the same NPC-priority logic as mode 0.
        *out_dx = fallback_dx;
        *out_dy = fallback_dy;
    } else {
        *out_dx = candidate_dx;
        *out_dy = candidate_dy;
    }

    // Suppress unused warning — tmp used by location_xy via pc_loc/npc_loc
    // through the pointer-to-local pattern in some compilers.
    (void)tmp;
}

static void start_tween(int64_t dx, int64_t dy)
{
    if (dx == 0 && dy == 0) {
        return;
    }

    location_origin_get(&cam_start_ox, &cam_start_oy);
    cam_target_ox = cam_start_ox + dx;
    cam_target_oy = cam_start_oy + dy;
    tig_timer_now(&cam_tween_start_time);
    cam_tween_active = true;
}

void dialog_camera_start(int64_t pc_obj, int64_t npc_obj)
{
    // Save the player's zoom level on first call only (not on mid-dialogue rechecks).
    if (!cam_zoom_saved) {
        cam_pre_dialog_zoom = iso_zoom_target();
        cam_dialog_zoom_floor = cam_pre_dialog_zoom;
        cam_zoom_saved = true;
    }

    int64_t pc_loc  = obj_field_int64_get(pc_obj,  OBJ_F_LOCATION);
    int64_t npc_loc = obj_field_int64_get(npc_obj, OBJ_F_LOCATION);
    int mode = settings_get_value(&settings, DIALOGUE_CAMERA_MODE_KEY);

    int64_t dx, dy;
    compute_target(pc_loc, npc_loc, mode, &dx, &dy);
    start_tween(dx, dy);
}

void dialog_camera_end(int64_t pc_obj)
{
    // Schedule zoom restore.  dialog_camera_start cancels it if called in the
    // same game-logic tick (mid-session NPC handoff).  dialog_camera_ping fires
    // it on the first draw tick where start hasn't cancelled it (true end).
    if (cam_zoom_saved && cam_pre_dialog_zoom != iso_zoom_target()) {
        cam_zoom_restore_pending = true;
        cam_zoom_restore_target = cam_pre_dialog_zoom;
    }
    cam_zoom_saved = false;
    cam_dialog_zoom_floor = ISO_ZOOM_MAX;

    if (!settings_get_value(&settings, DIALOGUE_CAMERA_TWEEN_BACK_KEY)) {
        return;
    }

    if (pc_obj == OBJ_HANDLE_NULL) {
        return;
    }

    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t dx, dy;

    // Compute delta to center on player (same logic as mode 1 but for PC).
    int64_t pc_sx, pc_sy;
    TigRect cr;
    location_xy(pc_loc, &pc_sx, &pc_sy);
    pc_sx += 40;
    pc_sy += 20;
    gamelib_get_iso_content_rect(&cr);
    dx = cr.width  / 2 - pc_sx;
    dy = cr.height / 2 - pc_sy;

    start_tween(dx, dy);
}

void dialog_camera_ping(void)
{
    // Fire zoom restore on the first draw tick after dialog_camera_end, provided
    // dialog_camera_start hasn't cancelled it (mid-session NPC handoff cancels
    // in the game-logic phase of the same tick, before draw runs).
    // Fire zoom restore only when we're no longer in a dialogue session.
    // cam_zoom_saved is true while inside a session (set by dialog_camera_start,
    // cleared by dialog_camera_end).  If start was called after end (mid-session
    // NPC handoff), cam_zoom_saved will be true here and we skip.
    if (cam_zoom_restore_pending && !cam_zoom_saved) {
        iso_zoom_set_target(cam_zoom_restore_target);
        cam_zoom_restore_pending = false;
        gamelib_invalidate_rect(NULL);
    }

    if (!cam_tween_active) {
        return;
    }

    int elapsed = tig_timer_elapsed(cam_tween_start_time);
    float t = (float)elapsed / DIALOGUE_CAM_TWEEN_MS;
    if (t > 1.0f) {
        t = 1.0f;
        cam_tween_active = false;
        // Tween done — recompute bubble placement against the settled viewport.
        tb_invalidate_positions();
    }

    // Smooth-step easing: s = t²(3 - 2t).
    float s = t * t * (3.0f - 2.0f * t);

    int64_t desired_ox = cam_start_ox
        + (int64_t)((float)(cam_target_ox - cam_start_ox) * s);
    int64_t desired_oy = cam_start_oy
        + (int64_t)((float)(cam_target_oy - cam_start_oy) * s);

    int64_t cur_ox, cur_oy;
    location_origin_get(&cur_ox, &cur_oy);

    int64_t ddx = desired_ox - cur_ox;
    int64_t ddy = desired_oy - cur_oy;

    if (ddx != 0 || ddy != 0) {
        location_origin_pixel_set(desired_ox, desired_oy);
        tc_scroll((int)ddx, (int)ddy);
    }

    // Keep the draw loop running while the tween is active.
    gamelib_invalidate_rect(NULL);
}
