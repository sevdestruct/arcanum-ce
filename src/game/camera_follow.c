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
#include "tig/timer.h"

// === Tunables ===
//
// Tween durations: active follow needs to feel responsive (PC walks
// off the safe-zone edge, camera catches up quickly); the drift settle
// after PC stops should feel calm.
#define FOLLOW_TWEEN_ACTIVE_MS    220u
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

// Re-target threshold (screen px). If the new desired camera origin is
// within this distance of the active tween's target, don't restart the
// tween — let the existing one finish. Avoids micro-jitter when the PC
// moves a few pixels and the math would re-fire every frame.
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
static bool compute_pc_screen_pos(int64_t pc_obj, int* out_sx, int* out_sy)
{
    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t unzoomed_sx, unzoomed_sy;
    location_xy(pc_loc, &unzoomed_sx, &unzoomed_sy);
    unzoomed_sx += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X) + 40;  // tile center
    unzoomed_sy += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y) + 20;

    // Translate to current screen coords via camera origin.
    int64_t cam_ox, cam_oy;
    location_origin_get(&cam_ox, &cam_oy);
    int64_t screen_x = unzoomed_sx + cam_ox;
    int64_t screen_y = unzoomed_sy + cam_oy;

    // Apply zoom (same math as dialog_camera).
    TigRect cr;
    gamelib_get_iso_content_rect(&cr);
    float z = iso_zoom_current();
    float cx = (float)cr.width  * 0.5f;
    float cy = (float)cr.height * 0.5f;
    float zsx = cx + ((float)screen_x - cx) * z;
    float zsy = cy + ((float)screen_y - cy) * z;

    *out_sx = (int)zsx;
    *out_sy = (int)zsy;
    return true;
}

// Compute the desired camera ORIGIN (world-pixel coords) that would put
// the PC at the screen center, accounting for zoom. Mirrors the math
// used by dialog_camera_end's "tween back to PC" path.
static void compute_recenter_origin(int64_t pc_obj, int64_t* out_ox, int64_t* out_oy)
{
    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t pc_sx, pc_sy;
    location_xy(pc_loc, &pc_sx, &pc_sy);
    pc_sx += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X) + 40;
    pc_sy += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y) + 20;

    TigRect cr;
    gamelib_get_iso_content_rect(&cr);

    // Bias the recenter slightly UPWARD to account for the HUD bottom
    // bar being thicker than the top — without this the PC sits visually
    // below center on the unobscured iso area.
    int usable_h = cr.height - GAME_UI_BAR_TOP - GAME_UI_BAR_BOTTOM;
    int target_screen_y = GAME_UI_BAR_TOP + usable_h / 2;
    int target_screen_x = cr.width / 2;

    // Inverse of (pc_sx + cam_ox) = target_screen_x  →  cam_ox = target_screen_x - pc_sx.
    *out_ox = (int64_t)target_screen_x - pc_sx;
    *out_oy = (int64_t)target_screen_y - pc_sy;
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

    if (pc_in_safe_zone && !pc_idle) {
        // Moving but still within the deadband — let it ride.
        return;
    }

    // Active follow OR drift settle on stop.
    bool want_settle = false;
    if (pc_idle) {
        if (pc_in_safe_zone) {
            // Idle inside safe zone: nothing to do until the player moves.
            return;
        }
        // Idle outside safe zone — wait for the settle delay before
        // tweening (avoids tweening for tiny pauses mid-path).
        tig_timestamp_t now;
        tig_timer_now(&now);
        if (s_pc_stopped_ts == 0
            || (unsigned int)now - s_pc_stopped_ts < FOLLOW_SETTLE_DELAY_MS) {
            return;
        }
        want_settle = true;
    }

    // Target: recenter the PC. Same math as dialog_camera_end's tween-
    // back path so the feel is consistent.
    int64_t target_ox, target_oy;
    compute_recenter_origin(pc_obj, &target_ox, &target_oy);

    // Re-target damping: if a tween is already aimed within
    // FOLLOW_RETARGET_THRESHOLD of the new target, let it finish. Without
    // this, micro-movements would restart the tween every frame and the
    // camera would "stick" to PC instead of smoothly catching up.
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

    unsigned int duration = want_settle
        ? FOLLOW_TWEEN_SETTLE_MS
        : FOLLOW_TWEEN_ACTIVE_MS;
    camera_tween_to(target_ox, target_oy, duration);
}
