#ifndef ARCANUM_GAME_CAMERA_FOLLOW_H_
#define ARCANUM_GAME_CAMERA_FOLLOW_H_

#include <stdbool.h>
#include <stdint.h>

// Auto-follow the player character with a safe-zone deadband + drift
// settle when PC stops. Opt-in via the arcanum.cfg key below.
//
// Behavior summary:
//  - PC inside safe zone, idle      → camera at rest (no tween)
//  - PC inside safe zone, moving    → camera at rest (PC has breathing room)
//  - PC outside safe zone           → tween camera toward centering PC
//  - PC just stopped after motion   → subtle drift to recenter
//  - User scrolls / portrait click  → suppress auto-follow for a cooldown;
//                                     resume only when (cooldown expired)
//                                     AND (PC starts new motion)
//
// Safe-zone math accounts for: HUD top/bottom bar heights, asymmetric
// iso sprite head/foot extents, current zoom, and a screen-ratio-derived
// horizontal margin (wider screens get proportionally larger margins so
// the deadband feels balanced).
//
// Tween durations: short (~220ms) during active follow for responsive
// feel; longer (~600ms) for the drift-settle on stop for a calm finish.
#define CAMERA_FOLLOWS_PLAYER_KEY "camera follows player"
// 0 (default) = no auto-follow (vanilla behavior preserved)
// 1          = auto-follow PC with safe zone + drift settle

void camera_follow_init(void);

// Per-tick driver. Cheap when the setting is off or when nothing needs to
// happen. Called from gamelib_draw alongside the existing iso_zoom_ping /
// dialog_camera_ping / camera_tween_ping.
void camera_follow_ping(void);

// Signal that the user just manually scrolled (mouse edge, keyboard arrow)
// or otherwise repositioned the camera. Starts the manual-override
// cooldown — auto-follow stays suppressed until BOTH the cooldown expires
// AND the PC has started a new motion since the override. Without the
// "new motion" gate, the camera would snap back the instant the cooldown
// expired even if the user was still looking at a specific spot.
void camera_follow_note_user_camera_move(void);

#endif /* ARCANUM_GAME_CAMERA_FOLLOW_H_ */
