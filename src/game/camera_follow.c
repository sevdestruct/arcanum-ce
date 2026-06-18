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
#include "ui/intgame.h"

// === Tunables ===========================================================
//
// Target velocity is a distance-proportional curve that asymptotes to
// MAX_VEL at long distances:
//
//   target_v = sign(gap) * MAX_VEL * tanh(|gap_origin| / MAX_VEL)
//
// Why tanh:
//   - For small gaps (|gap| << MAX_VEL), tanh(x) ≈ x → target ≈ gap
//     ≈ near-1:1 tracking (only ~5 origin-px of equilibrium lag at
//     PC's typical walking speed — barely visible).
//   - As |gap| grows, the curve smoothly saturates: at gap = MAX_VEL,
//     target = 0.76·MAX_VEL; at 2·MAX_VEL, 0.96·MAX_VEL; asymptotic.
//   - Distance-proportional in the middle range, capped at extremes.
//     "Far PC" doesn't race; "close PC" doesn't whip; both are eased.
//
// Layered on top: a per-tick accel cap smooths the moment-to-moment
// velocity changes (especially on transition into catch-up and out of
// it). Low MAX_ACCEL = longer ease; high = snappier. We pick 2.0,
// which means it takes ~7 ticks (~120ms) for cam_v to ramp from rest
// to MAX_VEL, and a similar interval to decelerate back to steady
// tracking speed — comfortable to watch, never jolting.
//
// Drift damping kicks in only when PC is truly idle (real stop, not
// inter-anim-frame pause). Subtle inertia continuation.
//
// All sized for a 60 Hz tick rate. Higher refresh = proportionally
// snappier wall-time behavior (a known limitation; acceptable for
// the smoothness vs. complexity trade-off in this module).
//
// Reference: dialog_camera tweens any distance in 400ms with a
// smoothstep ease (DIALOGUE_CAM_TWEEN_MS in dialog_camera.c). We're
// targeting a similar cinematic feel for off-camera catch-up — slow,
// deliberate, eased — while keeping zero lag at steady-state tracking.
//
//   MAX_VEL = 8    → ceiling on cam_v. Sets the tanh saturation
//                    point. With the tanh curve, mid-range gaps
//                    (40–100 origin-px) take ~250–400ms to close,
//                    roughly matching the dialog tween cadence.
//                    Lower → slower / more cinematic. Higher →
//                    snappier.
//
//   MAX_ACCEL = 1.0 → per-tick |cam_v| change cap. Smooths velocity
//                     transitions, especially ramp-up from rest. At
//                     1.0, ramp 0→MAX_VEL takes ~8 ticks (~130ms) —
//                     visibly eased like dialog camera's smoothstep
//                     start. Lower → more ease. Higher → more snap.
//
//   DRIFT_DAMPING = 0.85 → applied when pc_idle. cam_v *= 0.85/tick
//                    until below CUTOFF. From v0=5: total drift ~33
//                    origin-px over ~20 ticks (~330ms).
//
//   CUTOFF = 0.25  → below this |cam_v| (origin px/tick) we zero out
//                    and bail. Avoids endless sub-pixel invalidations.
//
// Continuous-tracking smoothness: feed-forward on PC's smoothed
// world-space velocity, so the camera moves at PC's AVERAGE speed
// continuously between anim steps (not just when a gap appears).
// PC's OBJ_F_OFFSET updates in chunks every 3-4 render frames; without
// FF, cam_v drops to zero between those chunks → world scroll has the
// same staircase as the PC anim. With FF, cam_v stays at the smoothed
// avg speed → world scrolls continuously.
//
//   PC_V_SMOOTH_ALPHA = 0.15 → per-tick low-pass on PC velocity. Tau
//                              ~100ms; converges to PC's avg speed
//                              over ~30 ticks of walking. Also acts as
//                              "drift on stop" — when PC stops,
//                              smoothed velocity decays naturally over
//                              the same time constant. Higher → more
//                              responsive but more jittery FF; lower
//                              → smoother but more onset lag.
//
//   GAP_GAIN = 0.25         → spring gain on position error.
//                              cam_v_target = FF + GAP_GAIN * gap.
//                              Higher = tighter pin to edge; lower =
//                              more PC sway during walking. 0.25 keeps
//                              PC within ~1-2 origin-px of the edge
//                              at steady state.
#define FOLLOW_PC_V_SMOOTH_ALPHA 0.10f
#define FOLLOW_GAP_GAIN          0.25f

// Quick tuning guide for taste:
//   Camera too fast / abrupt?       lower MAX_VEL and/or MAX_ACCEL
//   Camera too slow / lethargic?    raise MAX_VEL and/or MAX_ACCEL
//   Catch-up ramp not eased enough? lower MAX_ACCEL alone
//   Far-gap top speed too racy?     lower MAX_VEL alone
//   World scroll feels chunky?      lower PC_V_SMOOTH_ALPHA (more FF
//                                    smoothing, longer drift)
//   PC visibly lags behind edge?    raise GAP_GAIN (tighter pin)
// Note: spring saturates at GAP_GAIN * MAX_VEL_ORIGIN. Spring max
// MUST exceed PC's max walking/running speed for catch-up to work
// when PC is past the edge and moving away from the camera. PC
// walks ~1.7 origin-px/tick and runs ~5. With GAP_GAIN=0.25 and
// MAX_VEL=10, saturation is 2.5/tick — exceeds walking. Running
// catch-up is handled by FF (no longer capped); spring just adds
// a small position-correction on top.
//
// Lowering MAX_VEL below ~7 (=walking_speed/GAP_GAIN) makes catch-up
// fail for "PC walked off-screen, user clicked further away".
#define FOLLOW_MAX_VEL_ORIGIN  10.0f
#define FOLLOW_MAX_ACCEL       0.5f
#define FOLLOW_DRIFT_DAMPING   0.92f
#define FOLLOW_VEL_CUTOFF      0.25f

// Catch-up inset: during off-camera resume (large gap), aim for PC
// to land INSIDE the safe zone by this many screen pixels instead of
// exactly at the edge. Avoids the "camera arrives, target=0, mode
// flip, then PC's next step re-engages tracking" pause: PC arrives
// with momentum just inside the zone, continues walking through, and
// exits the far edge naturally where tracking takes over without a
// stop-at-edge beat.
//
// Only applied when |gap_raw| > CATCHUP_THRESHOLD. For small gaps the
// inset would oscillate (camera over-corrects on every PC step out
// of zone). Inside the threshold we use the raw gap — pin at edge.
#define FOLLOW_CATCHUP_INSET      20  // screen px past edge into zone
#define FOLLOW_CATCHUP_THRESHOLD  60  // |raw gap| screen px to trigger

// Scroll-accumulator cooldown: a single arrow-tap or brief edge-scroll
// shouldn't impose the full 3-second user-override cooldown. Each
// note() call increments a counter; the cooldown only arms once the
// user has scrolled SUBSTANTIALLY. Below threshold, cam_v is still
// zeroed (we yield instantly so we don't fight) but the override
// stays disarmed → tracking resumes on the next tick.
//
//   THRESHOLD = 10 notes → ~167ms of mouse-edge-scroll at 60Hz, or
//                          ~333ms of held-arrow-key auto-repeat.
//   RESET_MS = 400       → if no scroll input for this long, the
//                          accumulator clears.
#define FOLLOW_SCROLL_NOTE_THRESHOLD 10
#define FOLLOW_SCROLL_RESET_MS       400u

// Safe-zone fractions of the usable viewport. The "no-camera-move"
// zone is the centered rect of (usable_w * (1 - 2*FRAC_X)) by
// (usable_h * (1 - top_frac - bot_frac)).
//
// Horizontal margin is screen-ratio-aware: a reference 4:3 layout gets
// FRAC_X_BASE; wider screens get a slightly larger fraction so the
// deadband stays visually balanced (wide screens have proportionally
// more sideways "look-ahead" room).
#define FOLLOW_SAFE_FRAC_X_BASE   0.17f
#define FOLLOW_SAFE_FRAC_X_WIDE   0.24f
#define FOLLOW_REF_ASPECT         (4.0f / 3.0f)
#define FOLLOW_WIDE_ASPECT        (16.0f / 9.0f)

// Vertical fractions are asymmetric: the iso PC sprite extends UP from
// its tile (head) more than DOWN (foot/shadow), so the top margin is
// larger to keep the head away from the HUD bar AND the top edge
// during diagonal movement.
#define FOLLOW_SAFE_FRAC_Y_TOP    0.32f
#define FOLLOW_SAFE_FRAC_Y_BOT    0.20f

// Zoom-aware shrink of the safe zone at high zoom.
//
// Safe-zone WIDTH = usable_w - 2 * (frac_x * usable_w) = (1 - 2*frac_x)
// * usable_w. Look-ahead margin on each side = frac_x * usable_w.
//
// At higher zoom you see less of the world (each screen-px = 1/z world
// units). With fixed frac_x, the world-space safe zone stays the same
// FRACTION of the visible viewport, but the actual world-units it
// represents shrinks with z. Meanwhile look-ahead (also in world-units)
// shrinks too. PC has less room to walk to the edge before tracking
// kicks in, AND less is visible past the edge in world terms.
//
// Fix: GROW the look-ahead margin (frac_x) inversely with zoom — at
// z=2, frac_x is 1.5x default, so the look-ahead margin grows and the
// safe-zone width shrinks (in screen-px). World-space look-ahead now
// stays roughly constant; the camera engages tracking sooner so PC
// approaches the visible-world edge less often.
//
// Clamped to a max scale so safe zone doesn't collapse entirely at
// extreme zoom (would lose the deadband benefit).
#define FOLLOW_ZOOM_MARGIN_SCALE_MAX 1.80f

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

// Current camera velocity, in CAMERA-ORIGIN pixels per tick. Updated
// per tick via accel-cap toward target (active tracking) or via
// exponential damping (PC idle drift).
static float s_cam_vx_origin;
static float s_cam_vy_origin;

// Sub-pixel accumulator for applying velocity to camera origin. Camera
// origin can only be set in integer pixels (int64_t), but our velocity
// is fractional. Adding velocity to this accumulator each tick and
// extracting the integer part guarantees that, over time, the camera
// moves at exactly cam_v origin-px/tick — no rounding bias.
static float s_subpixel_x;
static float s_subpixel_y;

// Smoothed PC velocity (camera-independent world-space, origin-px/tick).
// Low-passed from the per-tick delta of PC's world position. Used as
// feed-forward in cam_v target so the camera coasts at PC's average
// speed between anim-frame steps → continuous world scroll.
static float s_pc_v_smooth_x;
static float s_pc_v_smooth_y;

// Previous tick's PC world position (camera-independent) for computing
// raw per-tick PC velocity. The s_pc_world_prev_valid flag handles
// first-tick / post-reset (where we have no baseline to diff against).
static int64_t s_pc_world_prev_x;
static int64_t s_pc_world_prev_y;
static bool s_pc_world_prev_valid;

// Scroll accumulator: counts note_user_camera_move calls within a
// recent time window. Only triggers the user-override cooldown once
// the count exceeds FOLLOW_SCROLL_NOTE_THRESHOLD; otherwise the
// gesture is treated as a quick nudge — velocity is yielded but
// tracking resumes on the next tick.
static int s_scroll_note_count;
static unsigned int s_last_scroll_note_ts;

// Per-axis catch-up state. Engages when |outer_gap| > CATCHUP_THRESHOLD;
// while engaged, the deadband uses the INNER zone (shrunk by INSET on
// each side) so the camera tween doesn't stop at the outer edge —
// it carries PC inside the zone. Disengages once PC is inside the
// inner zone on that axis, at which point normal (outer-zone)
// deadband applies for steady tracking.
static bool s_catchup_active_x;
static bool s_catchup_active_y;

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
    s_subpixel_x = 0.0f;
    s_subpixel_y = 0.0f;
    s_pc_v_smooth_x = 0.0f;
    s_pc_v_smooth_y = 0.0f;
    s_pc_world_prev_valid = false;
    s_scroll_note_count = 0;
    s_last_scroll_note_ts = 0;
    s_catchup_active_x = false;
    s_catchup_active_y = false;
    s_last_origin_valid = false;
}

bool camera_follow_is_enabled(void)
{
    return s_follow_enabled;
}

// Zero all motion state — used by every code path that yields camera
// motion to user input (scroll, external jump). Keeps "stop fighting
// the user" consistent. Also invalidates the PC-velocity baseline so
// the next tick starts fresh (no spurious velocity from comparing
// against a pre-yield position).
static void zero_motion_state(void)
{
    s_cam_vx_origin = 0.0f;
    s_cam_vy_origin = 0.0f;
    s_subpixel_x = 0.0f;
    s_subpixel_y = 0.0f;
    s_pc_v_smooth_x = 0.0f;
    s_pc_v_smooth_y = 0.0f;
    s_pc_world_prev_valid = false;
    s_catchup_active_x = false;
    s_catchup_active_y = false;
}

// Directly arm the user-override cooldown — used by handle_external_jump
// for single big jumps (UI recenter, magictech, etc.) where one
// observation is sufficient signal of user intent, vs. the scroll
// accumulator which needs sustained input. Also called from
// note_user_camera_move when the accumulator crosses the threshold.
static void arm_user_override_cooldown(void)
{
    tig_timestamp_t now;
    tig_timer_now(&now);
    s_user_override_until_ts = (unsigned int)now + FOLLOW_USER_COOLDOWN_MS;
    s_user_override_armed = true;
    if (camera_tween_is_active() && !dialog_camera_is_animating()) {
        camera_tween_cancel();
    }
    zero_motion_state();
}

void camera_follow_note_user_camera_move(void)
{
    if (!s_follow_enabled) {
        return;
    }
    tig_timestamp_t now;
    tig_timer_now(&now);

    // Refresh the scroll accumulator. If it's been a while since the
    // last scroll input, reset it — we treat this as a new gesture.
    if ((unsigned int)now - s_last_scroll_note_ts > FOLLOW_SCROLL_RESET_MS) {
        s_scroll_note_count = 0;
    }
    s_scroll_note_count++;
    s_last_scroll_note_ts = (unsigned int)now;

    // ALWAYS yield camera motion to the user — even small scrolls
    // shouldn't fight a tween that happens to be in flight. The
    // distinction between "small nudge" and "real reposition" only
    // affects whether we then arm the longer cooldown below.
    zero_motion_state();

    // Only arm the 3-second cooldown after the user has scrolled
    // SUBSTANTIALLY. Below threshold the gesture is a quick nudge:
    // we yield this tick but tracking resumes on the next.
    if (s_scroll_note_count >= FOLLOW_SCROLL_NOTE_THRESHOLD) {
        arm_user_override_cooldown();
    }
}

// CE: announce a deliberate "frame the PC" recenter (UI recenter button,
// PC lens, wmap travel-close). The game is explicitly choosing PC
// framing, so any armed user-override cooldown (a stale manual-framing
// suppression left over from earlier edge-scroll / UI interaction) must
// be cleared — otherwise follow would stay dead through the recenter and
// the walk that follows it. Re-baselines the origin tracker so the
// recenter's own motion isn't later counted as an external jump. No-op
// when the feature is disabled.
void camera_follow_note_recenter(void)
{
    if (!s_follow_enabled) {
        return;
    }
    s_user_override_armed = false;
    s_last_origin_valid = false;
    zero_motion_state();
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

    // When the user has TAB-hidden the HUD strips, the top and bottom
    // bar areas are part of the visible game world — don't subtract
    // them from the usable area. Otherwise the safe zone would
    // unnecessarily push PC toward screen center and waste the freshly
    // visible space.
    bool hud_hidden = intgame_hud_is_user_hidden();
    int usable_top = hud_hidden ? 0 : GAME_UI_BAR_TOP;
    int usable_bot = cr.height - (hud_hidden ? 0 : GAME_UI_BAR_BOTTOM);
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

    // Zoom-aware GROW of the look-ahead margin: at zoom > 1, scale
    // margin fractions UP by z (clamped). Wider margins → narrower
    // safe zone → more look-ahead. World-space deadband stays in a
    // comfortable range regardless of zoom.
    float z = iso_zoom_current();
    if (z <= 0.0f) z = 1.0f;
    float zoom_scale = 1.0f;
    if (z > 1.0f) {
        zoom_scale = z;
        if (zoom_scale > FOLLOW_ZOOM_MARGIN_SCALE_MAX) {
            zoom_scale = FOLLOW_ZOOM_MARGIN_SCALE_MAX;
        }
    }
    frac_x *= zoom_scale;

    int margin_x = (int)((float)usable_w * frac_x);

    int margin_y_top = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_TOP * zoom_scale);
    int margin_y_bot = (int)((float)usable_h * FOLLOW_SAFE_FRAC_Y_BOT * zoom_scale);

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

    // Stale motion state past the jump — zero it. Even if we don't
    // arm the cooldown below (PC framed in zone), the prior tick's
    // velocity / sub-pixel residual was computed against the old
    // origin and is meaningless now.
    zero_motion_state();

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
    // elsewhere. Arm the cooldown directly (bypass scroll accumulator;
    // a single >64-px jump IS substantial signal).
    arm_user_override_cooldown();
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
        zero_motion_state();
        return;
    }

    // CE: a deliberate camera-origin tween (PC-recenter on overlay/lens,
    // wmap travel-close, dialog conclusion) owns the view while it plays.
    // Yield exactly like the dialog case — reset our origin baseline and
    // motion residual each tick so the tween's per-tick origin shifts
    // aren't seen by handle_external_jump as a "user framed elsewhere"
    // jump. That misread is destructive: it would CANCEL the tween
    // mid-flight (arm_user_override_cooldown cancels any non-dialog
    // tween) AND arm the 3s cooldown, leaving follow dead for 3s right
    // as the post-recenter walk begins. When the tween finishes it has
    // framed the PC (inside the safe zone), so the next tick resumes
    // tracking cleanly with no cooldown.
    if (camera_tween_is_active()) {
        s_last_origin_valid = false;
        zero_motion_state();
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
            zero_motion_state();
            s_last_origin_x = cam_ox;
            s_last_origin_y = cam_oy;
            return;
        }
    }

    // === Update smoothed PC velocity (camera-independent FF) =============
    // Read PC's world-space position (unzoomed; cam_origin subtracted so
    // the value is invariant to camera moves). Per-tick delta gives PC's
    // raw velocity in origin-px/tick. Low-pass to filter PC's anim-frame
    // stepping; the smoothed value drives feed-forward in target_v
    // below, so the camera coasts at PC's average speed continuously
    // between anim chunks — world scrolls smoothly, not in jerks.
    {
        int64_t pc_loc = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
        int64_t pc_sx_unz, pc_sy_unz;
        location_xy(pc_loc, &pc_sx_unz, &pc_sy_unz);
        int pc_offset_x = obj_field_int32_get(pc_obj, OBJ_F_OFFSET_X);
        int pc_offset_y = obj_field_int32_get(pc_obj, OBJ_F_OFFSET_Y);
        // location_xy already includes cam_ox in its result; subtract
        // it to get a camera-independent world position (so the same
        // PC standing still gives the same value regardless of how the
        // camera has shifted).
        int64_t pc_world_x = pc_sx_unz + pc_offset_x + 40 - cam_ox;
        int64_t pc_world_y = pc_sy_unz + pc_offset_y + 20 - cam_oy;

        if (s_pc_world_prev_valid) {
            float raw_pc_vx = (float)(pc_world_x - s_pc_world_prev_x);
            float raw_pc_vy = (float)(pc_world_y - s_pc_world_prev_y);
            s_pc_v_smooth_x += (raw_pc_vx - s_pc_v_smooth_x) * FOLLOW_PC_V_SMOOTH_ALPHA;
            s_pc_v_smooth_y += (raw_pc_vy - s_pc_v_smooth_y) * FOLLOW_PC_V_SMOOTH_ALPHA;
        }
        s_pc_world_prev_x = pc_world_x;
        s_pc_world_prev_y = pc_world_y;
        s_pc_world_prev_valid = true;
    }

    // === Compute target velocity from raw gap ============================
    // Two regimes:
    //
    //  (a) Active tracking (PC moving, gap != 0): aim for PC to land
    //      at the safe-zone edge — UNLESS the gap is large enough to
    //      qualify as catch-up, in which case aim for PC to land
    //      slightly INSIDE the zone (CATCHUP_INSET) so the camera
    //      doesn't stop dead at the edge while PC is still walking.
    //      This avoids the "off-camera resume → camera arrives →
    //      mode flip → tracking re-engages" stutter.
    //
    //  (b) PC inside safe zone OR idle: target = 0 (no pull). Drift
    //      handling below decides whether to ramp cam_v down softly
    //      (idle) or snap (in-zone while moving).
    float target_vx = 0.0f;
    float target_vy = 0.0f;
    float z = iso_zoom_current();
    if (z <= 0.0f) z = 1.0f;
    if (!pc_idle) {
        int pc_screen_x, pc_screen_y;
        if (compute_pc_screen_pos(pc_obj, &pc_screen_x, &pc_screen_y)) {
            int sz_x1, sz_y1, sz_x2, sz_y2;
            compute_safe_zone(&sz_x1, &sz_y1, &sz_x2, &sz_y2);

            // Outer-zone gap (the raw "PC past the safe zone edge")
            // — used to detect catch-up engagement and as the gap for
            // steady-state tracking.
            int outer_gap_x = 0;
            int outer_gap_y = 0;
            if (pc_screen_x > sz_x2) {
                outer_gap_x = sz_x2 - pc_screen_x;
            } else if (pc_screen_x < sz_x1) {
                outer_gap_x = sz_x1 - pc_screen_x;
            }
            if (pc_screen_y > sz_y2) {
                outer_gap_y = sz_y2 - pc_screen_y;
            } else if (pc_screen_y < sz_y1) {
                outer_gap_y = sz_y1 - pc_screen_y;
            }

            // Engage catch-up on either axis when outer gap exceeds
            // threshold — sticky state so the inset actually lands
            // PC INSIDE the zone instead of the deadband canceling
            // it at the outer edge.
            if (outer_gap_x > FOLLOW_CATCHUP_THRESHOLD
                || outer_gap_x < -FOLLOW_CATCHUP_THRESHOLD) {
                s_catchup_active_x = true;
            }
            if (outer_gap_y > FOLLOW_CATCHUP_THRESHOLD
                || outer_gap_y < -FOLLOW_CATCHUP_THRESHOLD) {
                s_catchup_active_y = true;
            }

            // Effective gap per axis. If catch-up is active on an
            // axis, use the INNER zone (shrunk by INSET) — camera
            // keeps tweening until PC is genuinely inside the zone,
            // not just at the outer edge. Once PC reaches the inner
            // zone (effective gap = 0), the state clears and
            // steady-state tracking resumes against the outer zone.
            //
            // Per-axis state so a diagonal off-camera resume can
            // disengage one axis while still pulling the other.
            int gap_x_eff = 0;
            int gap_y_eff = 0;
            if (s_catchup_active_x) {
                int inner_x1 = sz_x1 + FOLLOW_CATCHUP_INSET;
                int inner_x2 = sz_x2 - FOLLOW_CATCHUP_INSET;
                if (pc_screen_x > inner_x2) {
                    gap_x_eff = inner_x2 - pc_screen_x;
                } else if (pc_screen_x < inner_x1) {
                    gap_x_eff = inner_x1 - pc_screen_x;
                } else {
                    // PC reached inner zone — catch-up done.
                    s_catchup_active_x = false;
                }
            } else {
                gap_x_eff = outer_gap_x;
            }
            if (s_catchup_active_y) {
                int inner_y1 = sz_y1 + FOLLOW_CATCHUP_INSET;
                int inner_y2 = sz_y2 - FOLLOW_CATCHUP_INSET;
                if (pc_screen_y > inner_y2) {
                    gap_y_eff = inner_y2 - pc_screen_y;
                } else if (pc_screen_y < inner_y1) {
                    gap_y_eff = inner_y1 - pc_screen_y;
                } else {
                    s_catchup_active_y = false;
                }
            } else {
                gap_y_eff = outer_gap_y;
            }

            // Below, the per-axis deadband gate uses gap_x_eff /
            // gap_y_eff (= 0 means deadband). That way catch-up
            // doesn't terminate at the outer edge; it goes until
            // PC is at the inner zone.
            int gap_x_raw = gap_x_eff;
            int gap_y_raw = gap_y_eff;

            // Convert effective gap (zoomed screen px) → origin px.
            // Zoom transform pivots at screen center; unzoomed delta D
            // produces zoomed delta D*z, so invert by dividing.
            float gap_x_orig = (float)gap_x_eff / z;
            float gap_y_orig = (float)gap_y_eff / z;

            // Target velocity = feed-forward + spring, gated PER AXIS
            // on the deadband:
            //
            //   target_axis = -smoothed_pc_v_axis          (feed-forward)
            //               + GAP_GAIN * tanh(gap_orig / MAX_VEL) * MAX_VEL
            //                                              (spring on gap)
            //   …but ONLY when gap_axis_raw != 0 (PC is past the edge
            //   on that axis). When gap_axis is 0 (PC inside the safe
            //   zone on that axis), target_axis stays 0 — deadband
            //   respected, FF doesn't pull the camera across the zone.
            //
            // Without this per-axis gate, FF would drive the camera
            // every tick that PC is moving, defeating the safe-zone
            // entirely (most visibly after map/save load when PC is
            // recentered inside the zone — camera would follow PC the
            // moment they took a step).
            //
            // The FF sign is negated: if PC moves east (pc_v positive),
            // cam_origin must DECREASE for PC to stay framed (see
            // location.c:140-141 where pc_screen = cam_origin + f(loc)),
            // so cam_v is the negative of PC's world velocity.
            //
            // The spring term closes any residual position error
            // (smoothed FF lags PC's instantaneous step by alpha
            // amount; spring catches it back up). tanh keeps the
            // spring distance-proportional with a comfortable ceiling,
            // so off-camera resume still feels eased rather than
            // sproingy at huge gaps.
            // FF + spring; no total cap applied here. The spring term
            // is naturally bounded by tanh * GAP_GAIN * MAX_VEL (~1.25
            // at current tunings — gentle catch-up). FF must be free
            // to match PC's actual movement speed, since clamping it
            // to MAX_VEL clipped the running case: PC running at e.g.
            // 7 origin-px/tick would get target = -7 (FF) clamped to
            // -MAX_VEL = -5, so the camera lagged PC by 2 px/tick
            // continuously — visible as running stutter that walking
            // (well below MAX_VEL) doesn't have.
            //
            // accel-cap still bounds the per-tick velocity change, so
            // ramp-up to a high target stays eased even with no total
            // cap. PC's max walking/running speed is implicitly the
            // bound here.
            if (gap_x_raw != 0) {
                target_vx = -s_pc_v_smooth_x
                    + FOLLOW_GAP_GAIN * FOLLOW_MAX_VEL_ORIGIN
                      * tanhf(gap_x_orig / FOLLOW_MAX_VEL_ORIGIN);
            }
            if (gap_y_raw != 0) {
                target_vy = -s_pc_v_smooth_y
                    + FOLLOW_GAP_GAIN * FOLLOW_MAX_VEL_ORIGIN
                      * tanhf(gap_y_orig / FOLLOW_MAX_VEL_ORIGIN);
            }
        }
    }

    // === Drive cam_v: drift on idle, accel-capped 1:1 on active ==========
    //
    // Drift: ONLY when PC is genuinely idle (real stop). cam_v decays
    // via exponential damping — subtle inertia continuation, ~330ms
    // at typical walking speed. NOT used for inter-anim-frame pauses
    // (PC is still moving overall): those want snappy tracking so the
    // accel-cap path keeps PC pinned at the safe-zone edge.
    //
    // Active tracking: cam_v moves toward target with per-tick accel
    // cap. At steady state (cam_v == target), diff is 0 and we get
    // pure 1:1 — PC stays pinned, world steps under PC, no visible PC
    // sprite oscillation. On transitions (onset, off-camera catch-up,
    // direction reversal, PC re-enters safe zone), the cap smooths
    // the velocity change over a few ticks.
    // Drift fix: when PC is idle, override the deadband-killed target
    // with -smoothed_pc_v. Smoothed PC velocity carries PC's actual
    // walking/running momentum (it's updated regardless of deadband),
    // so cam_v drifts from PC's real speed even if the deadband had
    // just zeroed cam_v during the walking oscillation. smoothed_pc_v
    // naturally decays each tick (raw_pc_v=0 → smoothed *= 1-alpha),
    // so the drift fades on its own without explicit damping.
    //
    // Drift distance ≈ smoothed_pc_v_initial / PC_V_SMOOTH_ALPHA.
    // For walking (1.67) at alpha 0.10: ~17 origin-px. Lower alpha
    // for longer drift; raise for snappier stops.
    if (pc_idle) {
        target_vx = -s_pc_v_smooth_x;
        target_vy = -s_pc_v_smooth_y;
    }
    float diff_x = target_vx - s_cam_vx_origin;
    float diff_y = target_vy - s_cam_vy_origin;
    if (diff_x >  FOLLOW_MAX_ACCEL) diff_x =  FOLLOW_MAX_ACCEL;
    if (diff_x < -FOLLOW_MAX_ACCEL) diff_x = -FOLLOW_MAX_ACCEL;
    if (diff_y >  FOLLOW_MAX_ACCEL) diff_y =  FOLLOW_MAX_ACCEL;
    if (diff_y < -FOLLOW_MAX_ACCEL) diff_y = -FOLLOW_MAX_ACCEL;
    s_cam_vx_origin += diff_x;
    s_cam_vy_origin += diff_y;

    // === Apply via sub-pixel accumulator =================================
    // Settled? Zero everything and bail.
    if (fabsf(s_cam_vx_origin) < FOLLOW_VEL_CUTOFF
        && fabsf(s_cam_vy_origin) < FOLLOW_VEL_CUTOFF) {
        s_cam_vx_origin = 0.0f;
        s_cam_vy_origin = 0.0f;
        s_subpixel_x = 0.0f;
        s_subpixel_y = 0.0f;
        s_last_origin_x = cam_ox;
        s_last_origin_y = cam_oy;
        return;
    }

    // Accumulate fractional velocity, extract integer part to apply.
    // Over time the sum of applied dx exactly matches the integral of
    // velocity — no rounding bias, no per-frame round-up/down jitter.
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
