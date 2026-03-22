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
// pad_top and pad_side are computed per-frame in compute_target because
// the head-gap scales with zoom (adjust_text_bubble_rect multiplies it by z).
//
//   pad_top  = UI_TOP  + HEAD_GAP*z + BUBBLE_H + AESTHETIC
//   pad_bot  = UI_BOT  + AESTHETIC
//   pad_side = BUBBLE_HALF_W + AESTHETIC
#define DIALOGUE_CAM_UI_TOP         41   // top chrome bar height
#define DIALOGUE_CAM_UI_BOT        159   // bottom chrome bar height
#define DIALOGUE_CAM_HEAD_GAP      100   // gap above NPC head (×zoom in tb.c)
#define DIALOGUE_CAM_BUBBLE_H      120   // typical bubble height (fixed px)
#define DIALOGUE_CAM_BUBBLE_HALF_W 100   // half bubble width   (fixed px)
#define DIALOGUE_CAM_AESTHETIC      60   // breathing room on all sides

// Tween duration in milliseconds.
#define DIALOGUE_CAM_TWEEN_MS 400.0f

static bool cam_tween_active;
static int64_t cam_start_ox;
static int64_t cam_start_oy;
static int64_t cam_target_ox;
static int64_t cam_target_oy;
static unsigned int cam_tween_start_time;

void dialog_camera_init(void)
{
    settings_register(&settings, DIALOGUE_CAMERA_MODE_KEY,       "0", NULL);
    settings_register(&settings, DIALOGUE_CAMERA_TWEEN_BACK_KEY, "0", NULL);
    cam_tween_active = false;
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

    // Compute zoom-dependent padding (all values in screen pixels).
    // The head-gap scales with zoom because adjust_text_bubble_rect in tb.c
    // multiplies all sprite-relative distances by z.
    float z = iso_zoom_current();
    int pad_top  = DIALOGUE_CAM_UI_TOP
                 + (int)((float)DIALOGUE_CAM_HEAD_GAP * z)
                 + DIALOGUE_CAM_BUBBLE_H
                 + DIALOGUE_CAM_AESTHETIC;
    int pad_bot  = DIALOGUE_CAM_UI_BOT + DIALOGUE_CAM_AESTHETIC;
    int pad_side = DIALOGUE_CAM_BUBBLE_HALF_W + DIALOGUE_CAM_AESTHETIC;

    int64_t cx = cr.width  / 2;
    int64_t cy = cr.height / 2;

    *out_dx = 0;
    *out_dy = 0;

    // location_xy returns unzoomed world-pixel screen coords.  Apply the zoom
    // transform to get the NPC's actual screen position before comparing
    // against the viewport.  Deltas are in world pixels, so divide by z.
    int64_t npc_zsx = cx + (int64_t)((float)(npc_sx - cx) * z);
    int64_t npc_zsy = cy + (int64_t)((float)(npc_sy - cy) * z);

    switch (mode) {
    case 0:
        // Minimal pan: only move if NPC's zoomed position is outside the
        // padding zone.  Convert the required screen delta back to world
        // pixels by dividing by z.
        if (npc_zsx < pad_side) {
            *out_dx = (int64_t)((float)(pad_side - npc_zsx) / z);
        } else if (npc_zsx > cr.width - pad_side) {
            *out_dx = (int64_t)((float)((cr.width - pad_side) - npc_zsx) / z);
        }
        if (npc_zsy < pad_top) {
            *out_dy = (int64_t)((float)(pad_top - npc_zsy) / z);
        } else if (npc_zsy > cr.height - pad_bot) {
            *out_dy = (int64_t)((float)((cr.height - pad_bot) - npc_zsy) / z);
        }
        break;
    case 1:
        // Center on NPC.  Centering is correct in world space.  The vertical
        // bias is divided by z so it represents the same screen-pixel offset
        // regardless of zoom.
        *out_dx = cx - npc_sx;
        *out_dy = (cy + (int64_t)((float)(pad_top - pad_bot) / (2.0f * z))) - npc_sy;
        break;
    case 2:
        // Center on midpoint between player and NPC, same vertical bias.
        *out_dx = cx - (pc_sx + npc_sx) / 2;
        *out_dy = (cy + (int64_t)((float)(pad_top - pad_bot) / (2.0f * z))) - (pc_sy + npc_sy) / 2;
        break;
    default:
        break;
    }

    // Check whether both characters fit on screen after panning.
    float sp_x = fabsf((float)(npc_sx - pc_sx));
    float sp_y = fabsf((float)(npc_sy - pc_sy));

    bool need_zoom_x = sp_x > 0.0f
        && (sp_x * z + (float)(pad_side * 2) > (float)cr.width);
    bool need_zoom_y = sp_y > 0.0f
        && (sp_y * z + (float)(pad_top + pad_bot) > (float)cr.height);

    if (need_zoom_x || need_zoom_y) {
        float zx = need_zoom_x
            ? ((float)cr.width  - (float)(pad_side * 2)) / sp_x
            : ISO_ZOOM_MAX;
        float zy = need_zoom_y
            ? ((float)cr.height - (float)(pad_top + pad_bot)) / sp_y
            : ISO_ZOOM_MAX;
        float z_needed = (zx < zy) ? zx : zy;
        if (z_needed < z) {
            iso_zoom_set_target(z_needed);  // clamped to ISO_ZOOM_MIN inside
        }
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
    int64_t pc_loc  = obj_field_int64_get(pc_obj,  OBJ_F_LOCATION);
    int64_t npc_loc = obj_field_int64_get(npc_obj, OBJ_F_LOCATION);
    int mode = settings_get_value(&settings, DIALOGUE_CAMERA_MODE_KEY);

    int64_t dx, dy;
    compute_target(pc_loc, npc_loc, mode, &dx, &dy);
    start_tween(dx, dy);
}

void dialog_camera_end(int64_t pc_obj)
{
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
