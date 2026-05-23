#ifndef ARCANUM_GAME_CAMERA_FOLLOW_H_
#define ARCANUM_GAME_CAMERA_FOLLOW_H_

#include <stdbool.h>
#include <stdint.h>

// Auto-follow the player character with a velocity-driven camera that
// eases in when tracking starts and eases out when tracking stops.
// Opt-in via the arcanum.cfg key below.
//
// Mental model: every tick we compute a TARGET camera velocity (zero
// when PC is inside the safe zone or idle; gap-to-safe-zone-edge
// otherwise, capped). The current camera velocity is low-pass filtered
// toward that target with an asymmetric alpha: snappy when target is
// non-zero (responsive ramp-up to PC's speed), gentle when target is
// zero (soft glide to rest). One primitive handles four behaviors:
//
//   - PC inside safe zone, idle   → camera at rest (target=0, no work)
//   - PC inside safe zone, moving → camera at rest (deadband: target=0)
//   - PC crosses safe-zone edge   → target ramps up, camera eases in
//   - PC re-enters safe zone OR
//     stops while outside         → target → 0 immediately, camera
//                                    glides to rest (does NOT wait for
//                                    PC to reach destination)
//
// User-override handoff:
//  - User scrolls / portrait click / UI recenter → arm override:
//    cooldown for ~3s, current velocity zeroed so we don't fight the
//    user. External origin jumps are detected passively by comparing
//    the camera origin each tick — so any code path that moves the
//    camera (not just our wrapped scroll) engages the cooldown.
//  - Resume gate: (cooldown expired) AND (PC currently moving). This
//    keeps the camera where the user put it whenever the PC is idle,
//    AND resumes tracking smoothly when the PC is walking continuously
//    after the cooldown ticks out (the prior "PC just transitioned to
//    moving" gate failed this case — if PC never stopped, tracking
//    never resumed).
//
// Safe-zone math accounts for: HUD top/bottom bar heights, asymmetric
// iso sprite head/foot extents, current zoom, and a screen-ratio-
// derived horizontal margin (wider screens get proportionally larger
// margins so the deadband feels balanced).
#define CAMERA_FOLLOWS_PLAYER_KEY "camera follows player"
// 0 (default) = no auto-follow (vanilla behavior preserved)
// 1          = auto-follow PC with safe zone + velocity ease

void camera_follow_init(void);

// Per-tick driver. Cheap when the setting is off or when nothing needs
// to happen. Called from gamelib_draw alongside the existing
// iso_zoom_ping / dialog_camera_ping / camera_tween_ping.
void camera_follow_ping(void);

// Explicit handoff: signal that the user just manually scrolled (mouse
// edge, keyboard arrow). External camera jumps from UI code paths are
// also detected passively in camera_follow_ping, so this call is only
// strictly needed for the scroll path (where the origin move is
// distributed across ticks and the per-tick passive check would also
// see it — calling here just arms the cooldown immediately on the
// first scroll input, before any origin change is observed).
void camera_follow_note_user_camera_move(void);

#endif /* ARCANUM_GAME_CAMERA_FOLLOW_H_ */
