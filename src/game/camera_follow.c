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

// === Tunables ===========================================================
//
// Unified velocity-based driver. Each tick:
//
//   1. TARGET velocity (origin-px/tick, per-axis):
//        - PC moving AND outside safe zone → gap-to-edge / zoom, capped
//          at MAX_VEL_ORIGIN per axis.
//        - PC inside safe zone OR idle     → 0.
//
//   2. CURRENT velocity is low-pass filtered toward target with
//      asymmetric alpha:
//        - target != 0 → RAMP_UP  (responsive: PC is leading)
//        - target == 0 → RAMP_DOWN (gentle: settle / drift)
//
//   3. Current velocity is applied to camera origin (rounded to int px).
//
// This one primitive replaces the prior trio of (1) 1:1 edge pin, (2)
// blended catch-up for off-camera starts, and (3) drift settle on stop.
// All transitions flow through the same low-pass:
//
//   - Crossing the safe-zone edge: target jumps from 0 to non-zero,
//     low-pass smoothly ramps current velocity up to match.
//   - PC stops or re-enters safe zone: target jumps to 0, low-pass
//     smoothly ramps current down — camera glides to rest WITHOUT
//     waiting for PC to reach its destination.
//   - Big gap (off-camera start after a manual scroll): target hits
//     MAX_VEL cap, current ramps up to capped speed, then naturally
//     decelerates as gap shrinks (target shrinks proportionally).
//
// Tunings (60Hz tick rate assumed; higher refresh = proportionally
// shorter wall-time durations, an acceptable simplification vs full
// frame-rate-independent decay math).
//
//   RAMP_UP=0.20  → ~80ms time const, ~190ms to 90% of target.
//                   Snappy enough that tracking onset feels responsive,
//                   soft enough that there's a visible ease.
//   RAMP_DOWN=0.07 → ~210ms time const, ~700ms to 5% of initial.
//                   Subtle continued glide after PC stops or enters the
//                   safe zone, dampening as it goes. Total drift
//                   distance at typical walking speed (~5 origin
//                   px/tick) ≈ v0 / alpha ≈ 70 origin-px — comfortably
//                   inside the safe zone, never pushes PC past it.
//   MAX_VEL=14    → caps catch-up speed when gap is huge (user scrolled
//                   far). ~3× PC walking speed; covers a full-screen
//                   gap in ~1 sec including ramp.
//   CUTOFF=0.25   → below this magnitude (origin px/tick) we zero
//                   velocity; avoids endless sub-pixel invalidations.
#define FOLLOW_VEL_RAMP_UP    0.20f
#define FOLLOW_VEL_RAMP_DOWN  0.07f
#define FOLLOW_MAX_VEL_ORIGIN 14.0f
#define FOLLOW_VEL_CUTOFF     0.25f

// PC animation updates OBJ_F_OFFSET_X/Y in chunks at the anim-frame
// rate (~3–4 render frames per anim step), so the raw "PC past
// safe-zone edge by N pixels" gap jumps in matching staircases. Track
// that raw gap directly and the camera target velocity inherits the
// staircase — visible as a pulsing motion synchronized with PC's anim
// frames.
//
// Fix: low-pass the gap itself before deriving target velocity. After
// smoothing, the target sees a continuous curve regardless of how the
// underlying anim stepped. Combined with the existing velocity
// low-pass below, the camera moves at PC's AVERAGE speed continuously
// between anim frames instead of jolting in sync with them.
//
// alpha=0.30 → tau ~50ms (3 frames @ 60Hz) — fast enough to track
// real velocity changes (PC stops, direction reversal) without
// noticeable lag, slow enough to smear across PC's 3–4 frame stepping
// cadence.
#define FOLLOW_GAP_SMOOTH_ALPHA 0.30f

// Safe-zone fractions of the usable viewport. The "no-camera-move"
// zone is the centered rect of (usable_w * (1 - 2*FRAC_X)) by
// (usable_h * (1 - top_frac - bot_frac)).
//
// Horizontal margin is screen-ratio-aware: a reference 4:3 layout gets
// FRAC_X_BASE; wider screens get a slightly larger fraction so the
// deadband stays visually balanced (wide screens have proportionally
// more sideways "look-ahead" room).
#define FOLLOW_SAFE_FRAC_X_BASE   0.22f
#define FOLLOW_SAFE_FRAC_X_WIDE   0.30f
#define FOLLOW_REF_ASPECT         (4.0f / 3.0f)
#define FOLLOW_WIDE_ASPECT        (16.0f / 9.0f)

// Vertical fractions are asymmetric: the iso PC sprite extends UP from
// its tile (head) more than DOWN (foot/shadow), so the top margin is
// larger to keep the head away from the HUD bar AND the top edge
// during diagonal movement.
#define FOLLOW_SAFE_FRAC_Y_TOP    0.32f
#define FOLLOW_SAFE_FRAC_Y_BOT    0.20f

// Cooldown after a manual user camera move (mouse-edge scroll,
// keyboard arrow, portrait click, UI recenter). Auto-follow stays off
// until (cooldown expired) AND (PC currently moving). The AND keeps
// the camera where the user put it whenever PC is idle — you can
// manually pan and read a sign without the camera snapping back the
// instant the cooldown ticks out. The `!pc_idle` check (instead of the
// prior "PC just transitioned to moving" gate) makes resume work even
// when the PC has been walking continuously the whole time.
#define FOLLOW_USER_COOLDOWN_MS   3000u

// Anything bigger than this (origin px between consecutive ticks)
// counts as an external camera jump (UI recenter, portrait click,
// dialog tween conclusion, etc.) and engages the cooldown. Bigger than
// any reasonable per-tick auto-follow apply (capped at MAX_VEL_ORIGIN
// ≈ 14), comfortably less than a "click portrait, camera jumps across
// the map" sort of move.
#define FOLLOW_JUMP_THRESHOLD_PX  64

// === State ==============================================================

static bool s_follow_enabled;

// User-override state.
static unsigned int s_user_override_until_ts;
static bool s_user_override_armed;

// Current camera velocity, in CAMERA-ORIGIN pixels per tick. Low-pass
// filtered toward a per-tick target inside camera_follow_ping.
static float s_cam_vx_origin;
static float s_cam_vy_origin;

// Low-passed safe-zone gap (zoomed-screen px). Smooths out PC's
// anim-frame stepping so the derived target velocity is continuous.
static float s_smoothed_gap_x;
static float s_smoothed_gap_y;

// Sub-pixel accumulator for applying velocity to camera origin. Camera
// origin can only be set in integer pixels (int64_t), but our velocity
// is fractional. Adding velocity to this accumulator each tick and
// extracting the integer part guarantees that, over time, the camera
// moves at exactly cam_v origin-px/tick — no rounding bias, no
// per-frame "every other tick we apply N+1 instead of N" oscillation
// that shows up as visible micro-jitter at small velocities.
static float s_subpixel_x;
static float s_subpixel_y;

// Origin after the last tick we drove. Used to detect EXTERNAL camera
// jumps: anything that moved the origin between ticks without going
// through our apply path — portrait click, inventory open, dialog
// tween final position, map load, etc. We engage the cooldown when we
// see one so we don't immediately snap back over the new framing.
static int64_t s_last_origin_x;
static int64_t s_last_origin_y;
static bool s_last_origin_valid;

void camera_follow_init(void)
{
    settings_register(&settings, CAMERA_FOLLOWS_PLAYER_KEY, "0", NULL);
    s_follow_enabled = settings_get_value(&settings, CAMERA_FOLLOWS_PLAYER_KEY) != 0;
    s_user_override_until_ts = 0;
    s_user_override_armed = false;
    s_cam_vx_origin = 0.0f;
    s_cam_vy_origin = 0.0f;
    s_smoothed_gap_x = 0.0f;
    s_smoothed_gap_y = 0.0f;
    s_subpixel_x = 0.0f;
    s_subpixel_y = 0.0f;
    s_last_origin_valid = false;
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
    // Cancel any auto-follow tween already in flight so it doesn't
    // fight the user's scroll. Dialog camera tweens are modal — we
    // leave those alone. (We can't distinguish here, but the only
    // auto-follow caller of camera_tween is dialog_camera anyway.)
    if (camera_tween_is_active() && !dialog_camera_is_animating()) {
        camera_tween_cancel();
    }
    // Kill momentum — user is steering, we don't get to coast.
    s_cam_vx_origin = 0.0f;
    s_cam_vy_origin = 0.0f;
    s_smoothed_gap_x = 0.0f;
    s_smoothed_gap_y = 0.0f;
    s_subpixel_x = 0.0f;
    s_subpixel_y = 0.0f;
}

// Compute the PC's current on-screen pixel coords (accounting for
// sub-tile OFFSET_X/Y during movement animation) and the zoom-scaled
// position the user perceives.
//
// Coordinate convention (verified against location.c:140-141):
// `location_xy(loc, &sx, &sy)` returns SCREEN pixel coords — it
// already includes `location_origin_x/y` in the result. So `pc_sx`
// here IS the PC's current on-screen unzoomed position; we must NOT
// add cam_ox a second time (early version of this code did, and the
// resulting alternating-target loop caused the camera to spiral
// infinitely).
static bool compute_pc_screen_pos(int64_t pc_obj, int* out_sx, int* out_sy)
{
    int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    int64_t pc_sx, pc_sy;
    location_xy(pc_loc, &pc_sx, &pc_sy);
    pc_sx += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X) + 40;  // tile center
    pc_sy += obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y) + 20;

    // Zoom pivots around screen center; apply transform for the
    // perceived position.
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
// All returned values are in SCREEN pixels (post-zoom). Inside this
// rect the camera does NOT auto-follow.
static void compute_safe_zone(int* x1, int* y1, int* x2, int* y2)
{
    TigRect cr;
    gamelib_get_iso_content_rect(&cr);

    int usable_top = GAME_UI_BAR_TOP;
    int usable_bot = cr.height - GAME_UI_BAR_BOTTOM;
    int usable_w   = cr.width;
    int usable_h   = usable_bot - usable_top;

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

    int margin_y_top = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_TOP);
    int margin_y_bot = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_BOT);

    *x1 = margin_x;
    *x2 = usable_w - margin_x;
    *y1 = usable_top + margin_y_top;
    *y2 = usable_bot - margin_y_bot;
}

// Detect a camera origin jump caused by code outside this module
// (UI recenter, portrait click, dialog tween final position, map
// load). Always zero our residual velocity — the prior tick's velocity
// was computed against the pre-jump origin and is meaningless after
// the warp. Then decide whether to engage the cooldown:
//
//   - If the jump LEFT PC inside the safe zone, the jumper wanted the
//     camera framed on PC (portrait click, "recenter on PC" UI button,
//     map load placing PC at screen center). Engaging the cooldown
//     would just delay follow from resuming — instead accept the new
//     framing as our baseline and let follow tick normally.
//
//   - If the jump moved the view AWAY from PC (PC now outside safe
//     zone), the jumper was framing something else (a spell effect, a
//     conversation NPC, mouse-edge scroll). Engage the cooldown so we
//     don't snap back over their framing.
//
// Returns true if the cooldown was engaged.
static bool handle_external_jump(int64_t cam_ox, int64_t cam_oy)
{
    if (!s_last_origin_valid) {
        // First observation; nothing to compare against.
        s_last_origin_x = cam_ox;
        s_last_origin_y = cam_oy;
        s_last_origin_valid = true;
        return false;
    }
    int64_t ddx = cam_ox - s_last_origin_x;
    int64_t ddy = cam_oy - s_last_origin_y;
    if (ddx < 0) ddx = -ddx;
    if (ddy < 0) ddy = -ddy;
    if (ddx <= FOLLOW_JUMP_THRESHOLD_PX && ddy <= FOLLOW_JUMP_THRESHOLD_PX) {
        return false;
    }

    // Stale velocity + filter state: zero everything. (note_user_camera_move
    // would also do this if we call it, but we zero unconditionally so the
    // PC-in-safe-zone branch below doesn't carry stale momentum, stale gap
    // smoothing, or stale sub-pixel residual past the jump either.)
    s_cam_vx_origin = 0.0f;
    s_cam_vy_origin = 0.0f;
    s_smoothed_gap_x = 0.0f;
    s_smoothed_gap_y = 0.0f;
    s_subpixel_x = 0.0f;
    s_subpixel_y = 0.0f;

    int64_t pc_obj = player_get_local_pc_obj();
    if (pc_obj != OBJ_HANDLE_NULL) {
        int pc_screen_x, pc_screen_y;
        if (compute_pc_screen_pos(pc_obj, &pc_screen_x, &pc_screen_y)) {
            int sz_x1, sz_y1, sz_x2, sz_y2;
            compute_safe_zone(&sz_x1, &sz_y1, &sz_x2, &sz_y2);
            if (pc_screen_x >= sz_x1 && pc_screen_x <= sz_x2
                && pc_screen_y >= sz_y1 && pc_screen_y <= sz_y2) {
                // PC framed in safe zone post-jump — accept new
                // baseline, no cooldown.
                return false;
            }
        }
    }

    // PC framed outside safe zone post-jump — user / system is looking
    // elsewhere. Engage cooldown.
    camera_follow_note_user_camera_move();
    return true;
}

void camera_follow_ping(void)
{
    if (!s_follow_enabled) {
        // Keep the origin tracker dormant when feature is off so toggling
        // the cfg flag mid-session doesn't spuriously fire on the first
        // tick after enable.
        s_last_origin_valid = false;
        return;
    }
    // Dialogue camera owns the view during a dialog session. Reset
    // the origin tracker each tick we yield so the first post-dialog
    // tick treats the dialog-tween's final origin as the new baseline
    // — without this, the post-dialog jump (potentially large, since
    // dialog camera frames the conversation) would engage the
    // cooldown, and the user would have to wait 3s for follow to
    // resume after a normal conversation. That's the wrong UX; the
    // safe-zone check inside handle_external_jump still arms cooldown
    // for any subsequent jump that leaves PC outside the safe zone.
    if (dialog_camera_is_animating()) {
        s_last_origin_valid = false;
        s_cam_vx_origin = 0.0f;
        s_cam_vy_origin = 0.0f;
        s_smoothed_gap_x = 0.0f;
        s_smoothed_gap_y = 0.0f;
        s_subpixel_x = 0.0f;
        s_subpixel_y = 0.0f;
        return;
    }

    int64_t pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        s_last_origin_valid = false;
        return;
    }

    // === Detect external camera jumps ====================================
    // Any code outside this module that moved the origin between ticks
    // (portrait click, UI recenter, dialog tween conclusion, map load)
    // → engage cooldown so we don't slam back to PC over their framing.
    int64_t cam_ox, cam_oy;
    location_origin_get(&cam_ox, &cam_oy);
    handle_external_jump(cam_ox, cam_oy);

    bool pc_idle = anim_is_idle(pc_obj);

    // === User-override gate ==============================================
    // Disarm only when BOTH (cooldown expired) AND (PC currently
    // moving). The "currently moving" check (vs the prior
    // "just-transitioned" check) makes resume work even when PC is
    // walking continuously the whole time — old gate would stay armed
    // forever in that case because the idle→moving transition never
    // happened during the cooldown window.
    if (s_user_override_armed) {
        tig_timestamp_t now;
        tig_timer_now(&now);
        bool cooldown_expired = (unsigned int)now >= s_user_override_until_ts;
        if (cooldown_expired && !pc_idle) {
            s_user_override_armed = false;
            // Fall through to drive logic below.
        } else {
            // Drift is camera motion too — don't let it survive the
            // user holding the wheel.
            s_cam_vx_origin = 0.0f;
            s_cam_vy_origin = 0.0f;
            s_smoothed_gap_x = 0.0f;
            s_smoothed_gap_y = 0.0f;
            s_subpixel_x = 0.0f;
            s_subpixel_y = 0.0f;
            s_last_origin_x = cam_ox;
            s_last_origin_y = cam_oy;
            return;
        }
    }

    // === Compute RAW gap (zoomed screen pixels) ==========================
    // When PC is idle, raw gap is forced to 0 so the gap smoother below
    // unwinds any prior tracking gap → camera target naturally decays
    // toward zero, no special-case "drift settle" branch needed.
    float raw_gap_x = 0.0f;
    float raw_gap_y = 0.0f;
    float z = iso_zoom_current();
    if (z <= 0.0f) z = 1.0f;
    if (!pc_idle) {
        int pc_screen_x, pc_screen_y;
        if (compute_pc_screen_pos(pc_obj, &pc_screen_x, &pc_screen_y)) {
            int sz_x1, sz_y1, sz_x2, sz_y2;
            compute_safe_zone(&sz_x1, &sz_y1, &sz_x2, &sz_y2);

            if (pc_screen_x > sz_x2) {
                raw_gap_x = (float)(sz_x2 - pc_screen_x);
            } else if (pc_screen_x < sz_x1) {
                raw_gap_x = (float)(sz_x1 - pc_screen_x);
            }
            if (pc_screen_y > sz_y2) {
                raw_gap_y = (float)(sz_y2 - pc_screen_y);
            } else if (pc_screen_y < sz_y1) {
                raw_gap_y = (float)(sz_y1 - pc_screen_y);
            }
        }
    }

    // === Smooth the gap to filter PC anim-frame stepping =================
    // Raw gap jumps in 5–10 px increments synchronized with PC's anim
    // frames; smoothing it before the velocity stage gives the camera a
    // continuous target so it moves at PC's average speed between anim
    // frames instead of pulsing in sync with them.
    s_smoothed_gap_x += (raw_gap_x - s_smoothed_gap_x) * FOLLOW_GAP_SMOOTH_ALPHA;
    s_smoothed_gap_y += (raw_gap_y - s_smoothed_gap_y) * FOLLOW_GAP_SMOOTH_ALPHA;

    // Convert smoothed gap → target velocity (origin px/tick).
    // The zoom transform pivots at screen center, so an unzoomed
    // delta D produces a zoomed delta D*z; invert to get the
    // origin-px move we need.
    float target_vx = s_smoothed_gap_x / z;
    float target_vy = s_smoothed_gap_y / z;

    // Per-axis cap so huge gaps (large user scroll, off-camera start)
    // don't fling the camera.
    if (target_vx >  FOLLOW_MAX_VEL_ORIGIN) target_vx =  FOLLOW_MAX_VEL_ORIGIN;
    if (target_vx < -FOLLOW_MAX_VEL_ORIGIN) target_vx = -FOLLOW_MAX_VEL_ORIGIN;
    if (target_vy >  FOLLOW_MAX_VEL_ORIGIN) target_vy =  FOLLOW_MAX_VEL_ORIGIN;
    if (target_vy < -FOLLOW_MAX_VEL_ORIGIN) target_vy = -FOLLOW_MAX_VEL_ORIGIN;

    // === Drive CURRENT velocity toward target ============================
    // Asymmetric low-pass: snappy ramp-up while tracking is active
    // (target != 0), gentle ramp-down when target is zero (PC stopped
    // or re-entered safe zone — camera glides to rest, doesn't snap).
    // Second smoothing layer on top of gap smoothing; the cascade gives
    // a critically-damped feel without explicit spring math.
    float alpha_x = (fabsf(target_vx) > 0.01f) ? FOLLOW_VEL_RAMP_UP : FOLLOW_VEL_RAMP_DOWN;
    float alpha_y = (fabsf(target_vy) > 0.01f) ? FOLLOW_VEL_RAMP_UP : FOLLOW_VEL_RAMP_DOWN;
    s_cam_vx_origin += (target_vx - s_cam_vx_origin) * alpha_x;
    s_cam_vy_origin += (target_vy - s_cam_vy_origin) * alpha_y;

    // === Apply with sub-pixel accumulator ================================
    // Settled? Zero everything (incl. residuals) and bail.
    if (fabsf(s_cam_vx_origin) < FOLLOW_VEL_CUTOFF
        && fabsf(s_cam_vy_origin) < FOLLOW_VEL_CUTOFF
        && fabsf(s_smoothed_gap_x) < FOLLOW_VEL_CUTOFF
        && fabsf(s_smoothed_gap_y) < FOLLOW_VEL_CUTOFF) {
        s_cam_vx_origin = 0.0f;
        s_cam_vy_origin = 0.0f;
        s_smoothed_gap_x = 0.0f;
        s_smoothed_gap_y = 0.0f;
        s_subpixel_x = 0.0f;
        s_subpixel_y = 0.0f;
        s_last_origin_x = cam_ox;
        s_last_origin_y = cam_oy;
        return;
    }

    // Accumulate fractional velocity, extract integer part to apply.
    // (int) truncates toward zero in C; residual = remainder. Over
    // time, sum of applied dx exactly matches integral of velocity —
    // no rounding bias, no per-frame "round up / round down"
    // oscillation that shows up as micro-jitter at low speeds.
    s_subpixel_x += s_cam_vx_origin;
    s_subpixel_y += s_cam_vy_origin;
    int dx = (int)s_subpixel_x;
    int dy = (int)s_subpixel_y;
    s_subpixel_x -= (float)dx;
    s_subpixel_y -= (float)dy;

    if (dx == 0 && dy == 0) {
        s_last_origin_x = cam_ox;
        s_last_origin_y = cam_oy;
        return;
    }

    int64_t new_ox = cam_ox + dx;
    int64_t new_oy = cam_oy + dy;
    location_origin_pixel_set(new_ox, new_oy);
    // Keep floating text in conversation overlays synced, same as
    // camera_tween_ping does. No-op when no conversation is active.
    tc_scroll(dx, dy);
    gamelib_invalidate_rect(NULL);

    // Save the origin we just drove to as our baseline for next-tick
    // external-jump detection.
    s_last_origin_x = new_ox;
    s_last_origin_y = new_oy;
}
