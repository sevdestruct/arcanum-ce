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
// Edge tracking — two regimes blended smoothly:
//
//   1. STEADY-STATE PIN (small gap): when the PC is within
//      CATCH_UP_BLEND_MIN_GAP of the safe-zone edge, we apply the full
//      screen delta each tick. PC stays glued to the edge. Matches the
//      sprite motion 1:1, no lag.
//
//   2. EASED CATCH-UP (large gap): when the PC is much further out —
//      typical case being "user manually scrolled away from PC, then PC
//      starts moving again, gap to safe zone is hundreds of px" — a
//      one-tick 1:1 pin would lurch the whole way in a single frame.
//      Instead apply CATCH_UP_RATE × gap (with a max-per-tick cap so
//      truly enormous gaps don't go faster than ~720px/sec). This is
//      mathematically an exponential approach, which reads as ease-out:
//      fast at first, decelerating as we close.
//
//   3. BLEND BAND between the two: linearly interpolates so there's no
//      visible velocity discontinuity at the threshold.
//
// At rate 0.20, equilibrium gap for a 5px/tick PC velocity is ~25px, so
// MIN_GAP=10 / MAX_GAP=40 puts the blend window straddling that
// equilibrium — the eased path naturally lands the PC inside the 1:1
// regime, no oscillation.
#define FOLLOW_CATCH_UP_RATE          0.20f
#define FOLLOW_CATCH_UP_BLEND_MIN_GAP 10    // gap ≤ this: pure 1:1 pin
#define FOLLOW_CATCH_UP_BLEND_MAX_GAP 40    // gap ≥ this: pure eased
#define FOLLOW_CATCH_UP_MAX_PER_TICK  60    // hard velocity cap

// When the PC stops, we don't snap-stop the camera. The camera was
// moving (because PC was moving and we were tracking), and momentum is
// preserved with an exponential damp: the camera continues in the same
// direction for a moment, decelerating smoothly. Subtle continuation,
// not a long coast. Per-tick damping factor; at typical 60Hz ticks this
// works out to ~250ms total drift from a ~5px/tick starting velocity.
// (Higher refresh rates → proportionally shorter wall-time drift; an
// acceptable simplification vs full frame-rate-independent decay math.)
#define FOLLOW_DRIFT_DAMPING      0.88f
// Below this magnitude (camera-origin pixels per tick) we treat the
// drift as finished and stop applying it. 0.5 means "less than a pixel
// per tick" — invisible motion.
#define FOLLOW_DRIFT_CUTOFF       0.5f

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

// === State ===

// Cached cfg flag — checked once per init (could be hot-reloaded by
// re-registering but vsync_mode/etc don't either; consistent with the
// rest of the project's settings pattern).
static bool s_follow_enabled;

// User-override state
static unsigned int s_user_override_until_ts;   // tig_timer ms timestamp
static bool s_user_override_armed;              // override active AND PC hasn't moved since
static bool s_pc_was_idle_last_tick;            // for detecting "PC just started moving"

// Momentum drift state. Camera-origin pixels per tick. Updated each
// tick of active edge-tracking; bled off via exponential damp once PC
// goes idle.
static float s_drift_vx_origin;
static float s_drift_vy_origin;

void camera_follow_init(void)
{
    settings_register(&settings, CAMERA_FOLLOWS_PLAYER_KEY, "0", NULL);
    s_follow_enabled = settings_get_value(&settings, CAMERA_FOLLOWS_PLAYER_KEY) != 0;
    s_user_override_until_ts = 0;
    s_user_override_armed = false;
    s_pc_was_idle_last_tick = true;
    s_drift_vx_origin = 0.0f;
    s_drift_vy_origin = 0.0f;
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
    // Kill any momentum drift too — user is steering, the camera should
    // do exactly what they say, not coast a bit further.
    s_drift_vx_origin = 0.0f;
    s_drift_vy_origin = 0.0f;
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

// Convert a raw per-axis gap (screen pixels needed to pin PC to the
// safe-zone edge) into the screen-pixel delta we actually apply this
// tick. Implements the two-regime blend described next to the tunables:
//
//   small gap (|d| <= MIN_GAP)      → return d unchanged (1:1 pin)
//   large gap (|d| >= MAX_GAP)      → return sign(d) * min(|d| * RATE,
//                                                          MAX_PER_TICK)
//   between                         → linear lerp of the two
//
// Sign is preserved throughout; only magnitude is shaped.
static int compute_blended_apply(int delta_screen)
{
    int abs_d = (delta_screen < 0) ? -delta_screen : delta_screen;
    if (abs_d <= FOLLOW_CATCH_UP_BLEND_MIN_GAP) {
        return delta_screen;
    }
    int eased_mag = (int)((float)abs_d * FOLLOW_CATCH_UP_RATE);
    if (eased_mag > FOLLOW_CATCH_UP_MAX_PER_TICK) {
        eased_mag = FOLLOW_CATCH_UP_MAX_PER_TICK;
    }
    int eased = (delta_screen > 0) ? eased_mag : -eased_mag;
    if (abs_d >= FOLLOW_CATCH_UP_BLEND_MAX_GAP) {
        return eased;
    }
    float t = (float)(abs_d - FOLLOW_CATCH_UP_BLEND_MIN_GAP)
            / (float)(FOLLOW_CATCH_UP_BLEND_MAX_GAP - FOLLOW_CATCH_UP_BLEND_MIN_GAP);
    return (int)((float)delta_screen * (1.0f - t) + (float)eased * t);
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
            // Drift is also a camera movement — don't let it survive
            // the user grabbing the wheel.
            s_drift_vx_origin = 0.0f;
            s_drift_vy_origin = 0.0f;
            return;
        }
    }

    // Fresh motion → drift is stale, clear it so we don't briefly
    // continue from a previous run.
    if (pc_just_started_moving) {
        s_drift_vx_origin = 0.0f;
        s_drift_vy_origin = 0.0f;
    }

    // === PC IDLE: apply residual drift, decay, return ==================
    // While the PC was tracking outside the safe zone, each tick stored
    // the camera-origin delta as a "velocity" sample. When PC stops, we
    // keep applying that velocity with exponential decay so the camera
    // glides to a stop in the direction it was already moving — natural
    // continuation, no snap-stop, no recenter.
    if (pc_idle) {
        if (fabsf(s_drift_vx_origin) < FOLLOW_DRIFT_CUTOFF
            && fabsf(s_drift_vy_origin) < FOLLOW_DRIFT_CUTOFF) {
            s_drift_vx_origin = 0.0f;
            s_drift_vy_origin = 0.0f;
            return;
        }
        int dx = (int)roundf(s_drift_vx_origin);
        int dy = (int)roundf(s_drift_vy_origin);
        if (dx != 0 || dy != 0) {
            int64_t cam_ox, cam_oy;
            location_origin_get(&cam_ox, &cam_oy);
            location_origin_pixel_set(cam_ox + dx, cam_oy + dy);
            tc_scroll(dx, dy);
            gamelib_invalidate_rect(NULL);
        }
        // Exponential damp — same factor every tick. At ~16ms ticks this
        // gives ~250ms of perceptible drift from a typical tracking
        // velocity, fading to nothing.
        s_drift_vx_origin *= FOLLOW_DRIFT_DAMPING;
        s_drift_vy_origin *= FOLLOW_DRIFT_DAMPING;
        return;
    }

    int pc_screen_x, pc_screen_y;
    if (!compute_pc_screen_pos(pc_obj, &pc_screen_x, &pc_screen_y)) {
        return;
    }

    int sz_x1, sz_y1, sz_x2, sz_y2;
    compute_safe_zone(&sz_x1, &sz_y1, &sz_x2, &sz_y2);

    // PC is moving. Inside the safe-zone deadband → camera holds.
    // Also bleed off any leftover drift velocity per tick (so if PC
    // walks back inside the safe zone after a tracking burst, the
    // momentum dies cleanly instead of triggering a drift on the next
    // stop).
    bool pc_in_safe_zone = pc_screen_x >= sz_x1 && pc_screen_x <= sz_x2
                        && pc_screen_y >= sz_y1 && pc_screen_y <= sz_y2;
    if (pc_in_safe_zone) {
        s_drift_vx_origin *= FOLLOW_DRIFT_DAMPING;
        s_drift_vy_origin *= FOLLOW_DRIFT_DAMPING;
        return;
    }

    // PC is past the safe-zone edge — close the gap. Small gaps get a
    // 1:1 pin (camera matches the sprite step-for-step, no lag). Large
    // gaps (typical when PC starts moving from far off-screen after a
    // manual scroll) get an eased catch-up via compute_blended_apply.
    // The blend band between them avoids any visible velocity
    // discontinuity at the threshold.

    // Smallest screen-px delta needed to push PC back to the edge.
    // Negative on the right/bottom side, positive on the left/top.
    int gap_x = 0;
    int gap_y = 0;
    if (pc_screen_x > sz_x2) {
        gap_x = sz_x2 - pc_screen_x;
    } else if (pc_screen_x < sz_x1) {
        gap_x = sz_x1 - pc_screen_x;
    }
    if (pc_screen_y > sz_y2) {
        gap_y = sz_y2 - pc_screen_y;
    } else if (pc_screen_y < sz_y1) {
        gap_y = sz_y1 - pc_screen_y;
    }
    if (gap_x == 0 && gap_y == 0) {
        return;
    }

    // Shape each axis independently so a large vertical gap doesn't
    // throttle a tight horizontal pin (and vice versa).
    int dx_screen = compute_blended_apply(gap_x);
    int dy_screen = compute_blended_apply(gap_y);
    if (dx_screen == 0 && dy_screen == 0) {
        return;
    }

    // Screen-pixel delta is in ZOOMED space (after the *z transform in
    // compute_pc_screen_pos). Camera origin is in unzoomed-screen-pixel
    // space (since location_xy adds it pre-zoom). The zoom transform
    // pivots at screen center, so an unzoomed delta of D produces a
    // zoomed delta of D*z. Invert: unzoomed delta = zoomed delta / z.
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

    // Stash this tick's velocity for the momentum-drift path that
    // fires when PC stops. We use the raw per-tick delta we just
    // applied — no smoothing window, no averaging. That keeps the
    // drift direction perfectly aligned with the last burst of
    // tracking (which is what the user perceives as "the direction
    // the camera was going").
    s_drift_vx_origin = (float)dx_origin;
    s_drift_vy_origin = (float)dy_origin;
}
