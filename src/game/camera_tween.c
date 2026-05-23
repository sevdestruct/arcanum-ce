#include "game/camera_tween.h"

#include "game/gamelib.h"
#include "game/location.h"
#include "game/tc.h"
#include "tig/timer.h"

// Default tween duration if caller passes 0. Matches the previous
// dialog_camera DIALOGUE_CAM_TWEEN_MS exactly so dialog_camera's
// behavior is unchanged after the refactor.
#define CAMERA_TWEEN_DEFAULT_MS 400u

static bool s_active;
static int64_t s_start_ox;
static int64_t s_start_oy;
static int64_t s_target_ox;
static int64_t s_target_oy;
static unsigned int s_start_time;
static unsigned int s_duration_ms;
static bool s_just_finished;

void camera_tween_init(void)
{
    s_active = false;
    s_just_finished = false;
}

void camera_tween_to(int64_t target_ox, int64_t target_oy, unsigned int duration_ms)
{
    int64_t cur_ox, cur_oy;
    location_origin_get(&cur_ox, &cur_oy);

    // Already there — nothing to do. Don't reset state; leave any
    // prior tween in its (presumably also-completed) state.
    if (target_ox == cur_ox && target_oy == cur_oy) {
        return;
    }

    if (duration_ms == 0) {
        duration_ms = CAMERA_TWEEN_DEFAULT_MS;
    }

    // Snap mode: caller asked for an instantaneous move. Update origin
    // directly + sync floating-text overlays.
    if (duration_ms == 1) {
        int64_t ddx = target_ox - cur_ox;
        int64_t ddy = target_oy - cur_oy;
        location_origin_pixel_set(target_ox, target_oy);
        tc_scroll((int)ddx, (int)ddy);
        s_active = false;
        return;
    }

    // Smoothly retarget — start from CURRENT origin, not where the
    // previous tween thought it was starting. Avoids jerk if a second
    // tween is requested mid-flight.
    s_start_ox = cur_ox;
    s_start_oy = cur_oy;
    s_target_ox = target_ox;
    s_target_oy = target_oy;
    s_duration_ms = duration_ms;
    tig_timer_now(&s_start_time);
    s_active = true;
}

void camera_tween_by(int64_t dx, int64_t dy, unsigned int duration_ms)
{
    if (dx == 0 && dy == 0) {
        return;
    }
    int64_t cur_ox, cur_oy;
    location_origin_get(&cur_ox, &cur_oy);
    camera_tween_to(cur_ox + dx, cur_oy + dy, duration_ms);
}

void camera_tween_cancel(void)
{
    s_active = false;
}

bool camera_tween_ping(void)
{
    if (!s_active) {
        return false;
    }

    int elapsed = tig_timer_elapsed(s_start_time);
    float t = (float)elapsed / (float)s_duration_ms;
    bool finishing = false;
    if (t >= 1.0f) {
        t = 1.0f;
        finishing = true;
    }

    // Smoothstep: s = t² (3 − 2t). Same curve dialog_camera used
    // (preserving feel post-refactor); generic enough for follow too.
    float s = t * t * (3.0f - 2.0f * t);

    int64_t desired_ox = s_start_ox
        + (int64_t)((float)(s_target_ox - s_start_ox) * s);
    int64_t desired_oy = s_start_oy
        + (int64_t)((float)(s_target_oy - s_start_oy) * s);

    int64_t cur_ox, cur_oy;
    location_origin_get(&cur_ox, &cur_oy);
    int64_t ddx = desired_ox - cur_ox;
    int64_t ddy = desired_oy - cur_oy;

    bool updated = false;
    if (ddx != 0 || ddy != 0) {
        location_origin_pixel_set(desired_ox, desired_oy);
        // tc_scroll keeps floating text in dialogue/conversation overlays
        // in sync with the camera move. No-op when no conversation is
        // active, so it's safe to always call.
        tc_scroll((int)ddx, (int)ddy);
        // Keep the draw loop running while we're mid-tween.
        gamelib_invalidate_rect(NULL);
        updated = true;
    }

    if (finishing) {
        s_active = false;
        s_just_finished = true;
    }
    return updated;
}

bool camera_tween_is_active(void)
{
    return s_active;
}

bool camera_tween_just_finished(void)
{
    if (s_just_finished) {
        s_just_finished = false;
        return true;
    }
    return false;
}

void camera_tween_get_target(int64_t* target_ox, int64_t* target_oy)
{
    if (target_ox) *target_ox = s_target_ox;
    if (target_oy) *target_oy = s_target_oy;
}
