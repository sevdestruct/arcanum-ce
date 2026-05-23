#ifndef ARCANUM_GAME_CAMERA_TWEEN_H_
#define ARCANUM_GAME_CAMERA_TWEEN_H_

#include <stdbool.h>
#include <stdint.h>

// Shared single-target camera-origin tween engine. Smoothstep easing.
// Multiple callers (dialog_camera, camera_follow) drive it; only one
// tween is active at a time. Starting a new tween while one is in
// flight smoothly retargets from the current origin — no jerk.
//
// The engine drives camera origin via location_origin_pixel_set and
// also calls tc_scroll() so floating-text overlays stay synced (tc_scroll
// is a no-op when no text-conversation is active, so it's safe to always
// call). Callers that need on-complete behavior (e.g. dialog_camera
// invalidating speech-bubble positions) can poll camera_tween_just_finished
// for one-shot detection.
void camera_tween_init(void);

// Start a tween toward an ABSOLUTE camera origin (world-pixel coords —
// same units as location_origin_get / location_origin_pixel_set).
// duration_ms ≤ 0 snaps immediately.
void camera_tween_to(int64_t target_ox, int64_t target_oy, unsigned int duration_ms);

// Convenience: tween BY a delta from current origin. Used by dialog_camera
// where compute_target returns a delta.
void camera_tween_by(int64_t dx, int64_t dy, unsigned int duration_ms);

// Cancel any in-flight tween. Camera stays at its current origin.
void camera_tween_cancel(void);

// Step the tween. Call once per main-loop iteration. Returns true if
// the camera origin was updated this tick (useful for invalidate logic).
bool camera_tween_ping(void);

// True while a tween is mid-flight.
bool camera_tween_is_active(void);

// One-shot edge detector: returns true exactly once after a tween
// completes, then false until the next tween completes. Lets callers
// react to "tween just finished" without subscribing to a callback.
bool camera_tween_just_finished(void);

// Current tween target. Valid while camera_tween_is_active() is true;
// undefined otherwise. Used by camera_follow to decide whether to
// retarget or let the existing tween finish.
void camera_tween_get_target(int64_t* target_ox, int64_t* target_oy);

#endif /* ARCANUM_GAME_CAMERA_TWEEN_H_ */
