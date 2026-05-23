#include "game/camera_follow.h"

#include <math.h>

#include "game/anim.h"
#include "game/camera_tween.h"
#include "game/dialog_camera.h"
#include "game/gamelib.h"
#include "game/iso_zoom.h"
#include "game/location.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/settings.h"
#include "game/tc.h"
#include "tig/timer.h"

// === Tunables ===
//
// Active follow is now real-time edge-tracking: when the PC moves past
// the safe-zone edge we move the camera by the same delta per tick to
// keep PC pinned at the edge. No tween — matches the PC's sprite
// motion exactly. The previous tween-based active path felt like a
// "catch-up lurch" because the camera was always a half-tween-duration
// behind the PC.
//
// The settle tween still fires when PC has been stopped for a moment,
// to recenter on PC at rest. Keep this calm; the player has already
// settled, so the camera should too.
#define FOLLOW_TWEEN_SETTLE_MS    600u

// Safe-zone fractions of the usable viewport. The "no-camera-move" zone
// is the centered rect of (usable_w * (1 - 2*FRAC_X)) by
// (usable_h * (1 - top_frac - bot_frac)).
//
// Horizontal margin is screen-ratio-aware. A reference 4:3 layout gets
// FRAC_X_BASE; wider screens get a slightly larger fraction so the
// deadband stays visually balanced (wide screens have proportionally
// more sideways "look-ahead" room).
#define FOLLOW_SAFE_FRAC_X_BASE   0.22f
#define FOLLOW_SAFE_FRAC_X_WIDE   0.30f  // applied at aspect >= 16:9
#define FOLLOW_REF_ASPECT         (4.0f / 3.0f)
#define FOLLOW_WIDE_ASPECT        (16.0f / 9.0f)

// Vertical safe-zone fractions are asymmetric because the iso PC sprite
// extends UP from its tile (head) more than DOWN (foot/shadow). Giving
// the top a larger fraction keeps the head away from the HUD bar AND
// the top edge during diagonal movement.
#define FOLLOW_SAFE_FRAC_Y_TOP    0.32f
#define FOLLOW_SAFE_FRAC_Y_BOT    0.20f

// Cooldown after a manual user camera move. Auto-follow stays off until
// (cooldown expired) AND (PC has started motion since the override). The
// AND keeps the camera where the user put it when the user isn't moving
// the PC — you can manually pan and read a sign without the camera
// snapping back the moment the cooldown ticks out.
#define FOLLOW_USER_COOLDOWN_MS   3000u

// Drift settle delay: when PC stops, wait this long before the recenter
// tween fires. Avoids tweening for tiny pauses between steps in a longer
// path; the player should settle, then the camera should settle.
#define FOLLOW_SETTLE_DELAY_MS    250u

// Re-target threshold (screen px) for the settle tween. If a new
// desired settle origin is within this distance of the active tween's
// target, let the existing one finish. Avoids micro-jitter from tiny
// drifts in OFFSET_X/Y between ticks.
#define FOLLOW_RETARGET_THRESHOLD 6

// === State ===

// Cached cfg flag — checked once per init (could be hot-reloaded by
// re-registering but vsync_mode/etc don't either; consistent with the
// rest of the project's settings pattern).
static bool s_follow_enabled;

// User-override state
static unsigned int s_user_override_until_ts;   // tig_timer ms timestamp
static bool s_user_override_armed;              // override active AND PC hasn't moved since
static bool s_pc_was_idle_last_tick;            // for detecting "PC just started moving"

// Drift settle state
static unsigned int s_pc_stopped_ts;            // ms timestamp PC went idle (0 = not pending)

void camera_follow_init(void)
{
    settings_register(&settings, CAMERA_FOLLOWS_PLAYER_KEY, "0", NULL);
    s_follow_enabled = settings_get_value(&settings, CAMERA_FOLLOWS_PLAYER_KEY) != 0;
    s_user_override_until_ts = 0;
    s_user_override_armed = false;
    s_pc_was_idle_last_tick = true;
    s_pc_stopped_ts = 0;
}

void camera_follow_note_user_camera_move(void)
{
    if (!s_follow_enabled) {
        return;
    }
    tig_timestamp_t now;
    tig_timer_now(&now);
    s_user_override_until_ts = (unsigned int)now + FOLLOW_USER_COOLDOWN_MS;
    s_user_override_armed = true;
    // Cancel any auto-follow tween already in flight so it doesn't fight
    // the user's scroll. dialog_camera tweens are explicit/modal — we
    // leave those alone. (We can't distinguish here, but the only auto-
    // follow callers are us anyway; dialog_camera tweens during a
    // dialog/cinematic when the user isn't free-scrolling.)
    if (camera_tween_is_active() && !dialog_camera_is_animating()) {
        camera_tween_cancel();
    }
}

// Compute the PC's CURRENT on-screen pixel coords (accounting for sub-
// tile OFFSET_X/Y during movement animation) and the zoom-scaled
// position the user perceives.
//
// Coordinate convention (verified against location.c:140-141):
// `location_xy(loc, &sx, &sy)` returns SCREEN pixel coords — it already
// includes `location_origin_x/y` in the result. So `pc_sx` here IS the
// PC's current on-screen unzoomed position; we must NOT add cam_ox a
// second time (early version of this code did, and the resulting
// alternating-target loop caused the camera to spiral infinitely).
static bool compute_pc_screen_pos(int64_t pc_obj, int* out_sx, int* out_sy)
{
    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t pc_sx, pc_sy;
    location_xy(pc_loc, &pc_sx, &pc_sy);
    pc_sx += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X) + 40;  // tile center
    pc_sy += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y) + 20;

    // Apply zoom to get what the user actually perceives. Zoom pivots
    // around the screen center; pre-zoom screen position is just pc_sx.
    TigRect cr;
    gamelib_get_iso_content_rect(&cr);
    float z = iso_zoom_current();
    float cx = (float)cr.width  * 0.5f;
    float cy = (float)cr.height * 0.5f;
    float zsx = cx + ((float)pc_sx - cx) * z;
    float zsy = cy + ((float)pc_sy - cy) * z;

    *out_sx = (int)zsx;
    *out_sy = (int)zsy;
    return true;
}

// Compute the desired camera ORIGIN that would put the PC at the
// screen target position. Mirrors dialog_camera_end's recenter math
// but expressed as an absolute origin rather than a delta.
//
// Math: location_xy returns SCREEN pixel position (it includes
// cam_origin_x), so pc_sx == pc_world_pixel + cam_ox. Rearranging:
//     pc_world_pixel = pc_sx - cam_ox
// And the new origin we want satisfies:
//     target_screen == pc_world_pixel + new_cam_ox
//     new_cam_ox    == target_screen - pc_world_pixel
//                   == target_screen - pc_sx + cam_ox
static void compute_recenter_origin(int64_t pc_obj, int64_t* out_ox, int64_t* out_oy)
{
    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t pc_sx, pc_sy;
    location_xy(pc_loc, &pc_sx, &pc_sy);
    pc_sx += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X) + 40;
    pc_sy += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y) + 20;

    int64_t cam_ox, cam_oy;
    location_origin_get(&cam_ox, &cam_oy);

    TigRect cr;
    gamelib_get_iso_content_rect(&cr);

    // Bias the recenter slightly UPWARD to account for the HUD bottom
    // bar being thicker than the top — without this the PC sits visually
    // below center on the unobscured iso area.
    int usable_h = cr.height - GAME_UI_BAR_TOP - GAME_UI_BAR_BOTTOM;
    int target_screen_y = GAME_UI_BAR_TOP + usable_h / 2;
    int target_screen_x = cr.width / 2;

    *out_ox = (int64_t)target_screen_x - pc_sx + cam_ox;
    *out_oy = (int64_t)target_screen_y - pc_sy + cam_oy;
}

// Compute safe-zone bounds for the PC's tile-center on-screen position.
// All returned values are in SCREEN pixels (post-zoom). Inside this rect
// the camera does NOT auto-follow.
static void compute_safe_zone(int* x1, int* y1, int* x2, int* y2)
{
    TigRect cr;
    gamelib_get_iso_content_rect(&cr);

    // Usable area excludes the HUD chrome.
    int usable_top = GAME_UI_BAR_TOP;
    int usable_bot = cr.height - GAME_UI_BAR_BOTTOM;
    int usable_w   = cr.width;
    int usable_h   = usable_bot - usable_top;

    // Horizontal margin: lerp between BASE (4:3) and WIDE (16:9) by
    // current aspect. Beyond 16:9 we clamp to WIDE so ultrawides don't
    // end up with the safe zone hugging the PC.
    float aspect = (cr.height > 0)
        ? (float)cr.width / (float)cr.height
        : FOLLOW_REF_ASPECT;
    float t_aspect = (aspect - FOLLOW_REF_ASPECT)
        / (FOLLOW_WIDE_ASPECT - FOLLOW_REF_ASPECT);
    if (t_aspect < 0.0f) t_aspect = 0.0f;
    if (t_aspect > 1.0f) t_aspect = 1.0f;
    float frac_x = FOLLOW_SAFE_FRAC_X_BASE
        + (FOLLOW_SAFE_FRAC_X_WIDE - FOLLOW_SAFE_FRAC_X_BASE) * t_aspect;
    int margin_x = (int)((float)usable_w * frac_x);

    // Vertical: asymmetric — more top margin (head extends up in iso),
    // less bottom margin (foot is near tile origin).
    int margin_y_top = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_TOP);
    int margin_y_bot = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_BOT);

    *x1 = margin_x;
    *x2 = usable_w - margin_x;
    *y1 = usable_top + margin_y_top;
    *y2 = usable_bot - margin_y_bot;
}

void camera_follow_ping(void)
{
    if (!s_follow_enabled) {
        return;
    }
    // Dialogue camera owns the view during a dialog session. Don't fight
    // it; the user expects the framed shot to stay framed.
    if (dialog_camera_is_animating()) {
        return;
    }

    int64_t pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return;
    }

    bool pc_idle = anim_is_idle(pc_obj);
    bool pc_just_started_moving = s_pc_was_idle_last_tick && !pc_idle;
    s_pc_was_idle_last_tick = pc_idle;

    // User-override gate: only disarm when both (cooldown expired) AND
    // (PC started a new motion since the override). Until then, leave
    // the camera where the user put it.
    if (s_user_override_armed) {
        tig_timestamp_t now;
        tig_timer_now(&now);
        bool cooldown_expired = (unsigned int)now >= s_user_override_until_ts;
        if (cooldown_expired && pc_just_started_moving) {
            s_user_override_armed = false;
            // Fall through to follow logic below.
        } else {
            return;
        }
    }

    // Drift-settle bookkeeping: remember when PC went idle so we can
    // start a calm recenter tween if they don't move for a moment.
    if (!pc_idle) {
        s_pc_stopped_ts = 0;
    } else if (s_pc_stopped_ts == 0) {
        tig_timestamp_t now;
        tig_timer_now(&now);
        s_pc_stopped_ts = (unsigned int)now;
    }

    int pc_screen_x, pc_screen_y;
    if (!compute_pc_screen_pos(pc_obj, &pc_screen_x, &pc_screen_y)) {
        return;
    }

    int sz_x1, sz_y1, sz_x2, sz_y2;
    compute_safe_zone(&sz_x1, &sz_y1, &sz_x2, &sz_y2);

    bool pc_in_safe_zone = pc_screen_x >= sz_x1 && pc_screen_x <= sz_x2
                        && pc_screen_y >= sz_y1 && pc_screen_y <= sz_y2;

    // --- ACTIVE FOLLOW: real-time edge tracking ------------------------
    // While PC is moving AND past the safe-zone edge, move the camera
    // by the exact amount needed to pin PC at the edge this frame. No
    // tween — matches PC sprite motion 1:1, no lurch.
    //
    // While PC is moving INSIDE the safe zone, the camera stays put
    // (the safe zone is the deadband the user asked for).
    if (!pc_idle) {
        if (pc_in_safe_zone) {
            return;
        }

        // Cancel any in-flight settle tween — we're back in motion.
        if (camera_tween_is_active()) {
            camera_tween_cancel();
        }

        // Smallest screen-px delta needed to push PC back to the edge.
        // Negative on the right/bottom side, positive on the left/top.
        int dx_screen = 0;
        int dy_screen = 0;
        if (pc_screen_x > sz_x2) {
            dx_screen = sz_x2 - pc_screen_x;
        } else if (pc_screen_x < sz_x1) {
            dx_screen = sz_x1 - pc_screen_x;
        }
        if (pc_screen_y > sz_y2) {
            dy_screen = sz_y2 - pc_screen_y;
        } else if (pc_screen_y < sz_y1) {
            dy_screen = sz_y1 - pc_screen_y;
        }
        if (dx_screen == 0 && dy_screen == 0) {
            return;
        }

        // Screen-pixel delta is in ZOOMED space (after the *z transform
        // in compute_pc_screen_pos). Camera origin is in unzoomed-
        // screen-pixel space (since location_xy adds it pre-zoom). The
        // zoom transform pivots at screen center, so an unzoomed delta
        // of D produces a zoomed delta of D*z. Invert: unzoomed delta
        // = zoomed delta / z.
        float z = iso_zoom_current();
        if (z <= 0.0f) z = 1.0f;
        int dx_origin = (int)((float)dx_screen / z);
        int dy_origin = (int)((float)dy_screen / z);
        if (dx_origin == 0 && dy_origin == 0) {
            return;
        }

        int64_t cam_ox, cam_oy;
        location_origin_get(&cam_ox, &cam_oy);
        location_origin_pixel_set(cam_ox + dx_origin, cam_oy + dy_origin);
        // Keep floating text in conversation overlays synced, same as
        // camera_tween_ping does. No-op when no conversation active.
        tc_scroll(dx_origin, dy_origin);
        gamelib_invalidate_rect(NULL);
        return;
    }

    // --- IDLE: drift settle on stop ------------------------------------
    if (pc_in_safe_zone) {
        // Idle inside safe zone: nothing to do until the player moves.
        return;
    }
    // Idle outside safe zone — wait for the settle delay before tweening
    // (avoids tweening for tiny pauses mid-path).
    {
        tig_timestamp_t now;
        tig_timer_now(&now);
        if (s_pc_stopped_ts == 0
            || (unsigned int)now - s_pc_stopped_ts < FOLLOW_SETTLE_DELAY_MS) {
            return;
        }
    }

    // Target: recenter PC. Same math as dialog_camera_end's tween-back
    // path so the feel is consistent.
    int64_t target_ox, target_oy;
    compute_recenter_origin(pc_obj, &target_ox, &target_oy);

    // Re-target damping: if a settle tween is already aimed within
    // FOLLOW_RETARGET_THRESHOLD of the new target, let it finish. Without
    // this, micro-movements in OFFSET_X/Y between ticks would restart
    // the tween every frame.
    if (camera_tween_is_active()) {
        int64_t cur_target_ox, cur_target_oy;
        camera_tween_get_target(&cur_target_ox, &cur_target_oy);
        int64_t ddx = cur_target_ox - target_ox;
        int64_t ddy = cur_target_oy - target_oy;
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        if (ddx <= FOLLOW_RETARGET_THRESHOLD
            && ddy <= FOLLOW_RETARGET_THRESHOLD) {
            return;
        }
    }

    camera_tween_to(target_ox, target_oy, FOLLOW_TWEEN_SETTLE_MS);
}
