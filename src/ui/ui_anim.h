#ifndef ARCANUM_UI_UI_ANIM_H_
#define ARCANUM_UI_UI_ANIM_H_

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// CE: Unified UI animation framework. Per-frame tween integrator that
// drives:
//   - tig window transforms (scale + alpha + anchor at composite time)
//   - tig window position (frame.x/y via tig_window_move)
//   - arbitrary int / float variables (for things like the dialog options
//     window's Y gap-offset that's read each frame by another module)
//
// The motion model is an overdamped mass-spring-damper, so values settle
// toward their target without overshoot — gives a "dampened spring (not
// bouncy)" feel without the brittleness of hand-tuned cubic easing curves.
// Each animating scalar carries its own position + velocity; per-frame
// ping advances all active scalars by the elapsed dt.
//
// Same-target retarget snaps the new start values to current in-flight
// values (and preserves velocity), so reversing direction mid-flight
// stays continuous — the camera_tween pattern, generalized to per-slot
// per-scalar state.

// CE: Named anchor positions for window scale animation. Wraps a 0..1
// frame-relative (rel_x, rel_y) pair so call sites don't repeat
// fractions inline. Naming follows CSS/typography conventions: vertical
// "top/middle/bottom" first, horizontal "left/center/right" second.
// CENTER alone is the geometric center (middle + center).
typedef enum {
    UI_ANIM_ANCHOR_CENTER,         // (0.5, 0.5) — geometric center
    UI_ANIM_ANCHOR_TOP_LEFT,       // (0.0, 0.0)
    UI_ANIM_ANCHOR_TOP_CENTER,     // (0.5, 0.0)
    UI_ANIM_ANCHOR_TOP_RIGHT,      // (1.0, 0.0)
    UI_ANIM_ANCHOR_MIDDLE_LEFT,    // (0.0, 0.5)
    UI_ANIM_ANCHOR_MIDDLE_RIGHT,   // (1.0, 0.5)
    UI_ANIM_ANCHOR_BOTTOM_LEFT,    // (0.0, 1.0)
    UI_ANIM_ANCHOR_BOTTOM_CENTER,  // (0.5, 1.0)
    UI_ANIM_ANCHOR_BOTTOM_RIGHT,   // (1.0, 1.0)
    UI_ANIM_ANCHOR_CUSTOM,         // use the (rel_x, rel_y) fields raw
} ui_anim_anchor_t;

// CE: convert a named anchor to its 0..1 (rel_x, rel_y) pair. For
// CUSTOM, returns 0.5/0.5 — callers using CUSTOM should provide their
// own (rel_x, rel_y) via the *_ex variants below.
void ui_anim_anchor_to_rel(ui_anim_anchor_t anchor,
    float* rel_x,
    float* rel_y);

// CE: Profile = animation feel knobs. settle_ms is the approximate time
// the spring takes to come to rest at the target (within epsilon), and
// damping_ratio controls overshoot:
//   1.0  = critically damped (no overshoot, fastest settle).
//   >1.0 = overdamped (smoother settle, slightly slower).
// damping_ratio < 1.0 would oscillate (bouncy) — keep it >= 1.0 for
// the "dampened, not bouncy" feel the user wants.
typedef struct {
    int settle_ms;
    float damping_ratio;
} ui_anim_profile_t;

// CE: bundled default profiles. Slightly overdamped (ratio 1.2) so the
// motion feels like silicone, not a hard stop. Tune these centrally
// instead of at each call site. Pass NULL to ui_anim_* start functions
// to use UI_ANIM_PROFILE_DEFAULT_ENTRANCE for show / DEFAULT_EXIT for
// hide / DEFAULT_VAR for generic var tweens.
extern const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_ENTRANCE; // ~180ms
extern const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_EXIT;     // ~240ms
extern const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_VAR;      // ~220ms
extern const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_SLIDE;    // ~200ms

// CE: opaque handle to an in-flight tween. Survives reuse of the
// underlying slot via a generation counter — passing a stale handle to
// ui_anim_cancel / ui_anim_is_active is safe (no-op / false).
typedef int ui_anim_handle_t;
#define UI_ANIM_HANDLE_INVALID ((ui_anim_handle_t)0)

// CE: module lifecycle. ui_anim_init must run after tig is up (so the
// window destroy-notify hook can be registered). ui_anim_reset cancels
// every active tween without firing on_complete callbacks — call on
// session boundaries (quit to title, game load) so callbacks bound to
// torn-down state don't fire.
bool ui_anim_init(void);
void ui_anim_exit(void);
void ui_anim_reset(void);

// CE: per-frame integrator. Call once per main-loop tick from
// gamelib_draw, next to camera_tween_ping / dialog_camera_ping. Advances
// every active tween by the elapsed dt and applies the result to its
// target (window transform / window move / variable write).
void ui_anim_ping(void);

// CE: F9-toggle-able perf counters for the ui_anim ping itself
// (spring integration + per-slot apply). Counters increment only
// while ui_anim_perf_set_enabled(true) is in effect. Get/reset are
// expected to be called from the gamelib F9 zoom-perf log dump.
typedef struct {
    uint64_t ping_total_ns;
    uint64_t ping_max_ns;
    int ping_samples;             // number of pings (frames) measured
    uint64_t apply_total_ns;      // time inside per-slot apply (subset of ping)
    int active_slots_max;         // peak concurrent active slots in window
    int active_slots_total;       // sum across pings — divide by samples for avg
} TigUiAnimPerf;
void ui_anim_perf_set_enabled(bool enabled);
void ui_anim_perf_get(TigUiAnimPerf* out);
void ui_anim_perf_reset(void);

// CE: start a "show" animation on a window — scales from scale_from to
// 1.0 and alpha from 0.0 to 1.0, both with the same spring profile.
// The window's transform_active flag stays set until the tween reaches
// its end state; after that the compositor short-circuits the transform
// path. anchor controls the scaling center (typically CENTER for modal-
// style entrances, TOP_CENTER for dropdowns sliding from above, etc.).
//
// If a tween is already in flight on the same window, the new tween
// snaps its start value + velocity to the current in-flight value so
// retargeting (e.g. quick show -> hide) is continuous.
//
// Returns UI_ANIM_HANDLE_INVALID on full pool / invalid window. If the
// UI_ANIMATIONS cfg is disabled, applies the end state immediately and
// returns a handle that reads as not-active.
ui_anim_handle_t ui_anim_window_show(tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* profile);

// CE: like ui_anim_window_show but takes raw frame-relative anchor
// coords (0..1 each axis) instead of the named ui_anim_anchor_t
// enum. Used for cases where the anchor isn't a fixed point on the
// frame — e.g. the bottom HUD bar's transform anchor needs to track
// the currently-visible band's center, which varies per stage.
ui_anim_handle_t ui_anim_window_show_ex(tig_window_handle_t window,
    float anchor_rel_x,
    float anchor_rel_y,
    float scale_from,
    const ui_anim_profile_t* profile);

// CE: like ui_anim_window_show but fires on_complete(ctx) when the
// entrance spring settles. Used by callers that need to defer
// post-entrance state changes — e.g. charedit shows its skill/spell/
// tech/scheme subwindows after the parent settles so they don't
// "pop in" instantly above the animating parent.
ui_anim_handle_t ui_anim_window_show_with_complete(
    tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

// CE: spring-tween a window's transform to ANY (scale, alpha) target —
// unlike show (targets 1,1) and hide (targets ?,0), this lets the
// caller pick both. Used for state-driven backdrop animations like
// the mainmenu bg's recede (scale 1→0.96 with alpha kept at 1.0). If
// no existing transform tween for the window, seeds value from the
// current transform state (or natural 1,1 if no transform); retargets
// in place if a tween is already in flight (preserves velocity).
ui_anim_handle_t ui_anim_window_transform_to(
    tig_window_handle_t window,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile);

// CE: like ui_anim_window_transform_to but explicit (scale_from,
// alpha_from) seed for fresh slots — useful when the caller knows
// the window's current visible state (e.g. tracked externally) and
// wants the spring to start there exactly, not at the default
// (1.0, 1.0). If a tween is already active for this window, the
// from_* values are ignored (retarget preserves current velocity
// + value as usual).
ui_anim_handle_t ui_anim_window_transform_from_to(
    tig_window_handle_t window,
    float scale_from,
    float alpha_from,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile);

// CE: like ui_anim_window_transform_from_to but fires on_complete(ctx)
// when the spring settles. The callback may be cancelled mid-flight
// by a retarget (a subsequent transform_to / transform_from_to on
// the same window fires the previous on_complete BEFORE applying the
// new target). Callers that want to "abandon" the destroy on retarget
// should guard their on_complete with a pending flag (similar pattern
// to fate_ui's dismiss_pending).
ui_anim_handle_t ui_anim_window_transform_from_to_with_complete(
    tig_window_handle_t window,
    float scale_from,
    float alpha_from,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

// CE: start a "hide" animation on a window — scales from current to
// scale_to and alpha to 0.0. on_complete fires when the spring settles
// (with ctx); typical usage is to defer the actual tig_window_destroy /
// state cleanup until then. on_complete may be NULL if the caller
// doesn't need a notification (e.g. window stays alive after the fade
// for some other reason).
//
// If the cfg is disabled, on_complete fires immediately from the start
// call — callers must tolerate sync invocation (don't rely on a stack
// frame still being live).
ui_anim_handle_t ui_anim_window_hide(tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_to,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

// CE: like ui_anim_window_hide but takes raw frame-relative anchor
// coords (0..1 each axis). Same use case as ui_anim_window_show_ex —
// per-stage anchors that don't fit the named enum.
ui_anim_handle_t ui_anim_window_hide_ex(tig_window_handle_t window,
    float anchor_rel_x,
    float anchor_rel_y,
    float scale_to,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

// CE: tween an int variable from its current value to target. Caller
// retains the pointer's lifetime — make sure the slot stays valid for
// the duration of the tween (use ui_anim_cancel on the returned handle
// before tearing down the slot's owner if needed). Mostly useful for
// driving game-state ints that other modules read each frame (e.g. the
// dialog options window's bottom-gap-offset). The optional invalidate_*
// fields request a per-frame screen invalidation while the tween is
// active so the consumer's draw picks up each integer step.
ui_anim_handle_t ui_anim_int_to(int* slot,
    int target,
    const ui_anim_profile_t* profile);

// CE: like ui_anim_int_to but fires on_complete(ctx) when the spring
// settles (value within epsilon AND velocity within epsilon). Used by
// the fate / sleep UI slide-up dismissal: the tween moves the window
// off screen, and on_complete destroys the window from a known
// settled state. When the animation cfg is disabled, on_complete
// fires synchronously from this call.
//
// Retargeting an existing tween on the same slot pointer updates the
// callback to the new fn/ctx; the previous callback (if any) is
// fired before the swap so callers don't see their callback silently
// dropped.
ui_anim_handle_t ui_anim_int_to_with_complete(int* slot,
    int target,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

ui_anim_handle_t ui_anim_float_to(float* slot,
    float target,
    const ui_anim_profile_t* profile);

// CE: cancel an in-flight tween. The target value is left at whatever
// state the tween wrote last (NOT snapped to target — if you want that,
// call ui_anim_finish_now instead). on_complete is not fired.
void ui_anim_cancel(ui_anim_handle_t handle);

// CE: snap a tween to its end state and fire on_complete (if any).
// Used by the cfg-disabled fast-path and by callers that want to abort
// the motion but commit the destination (e.g. on TAB key spam).
void ui_anim_finish_now(ui_anim_handle_t handle);

// CE: stale-handle-safe predicate. Returns true only if the handle
// matches a still-active tween in the pool (generation match + active).
bool ui_anim_is_active(ui_anim_handle_t handle);

// CE: cancel every in-flight tween targeting the given window —
// transforms, tint reveals, and any future per-window kinds. Used
// when a window's owner closes/hides it externally (e.g. inventory
// destroy) before the tween's natural completion; without this the
// next opener finds the tween still in flight and the spring
// retargets from a stale value, producing a "stuck-mid-scale"
// effect. on_complete is NOT fired for cancelled tweens (caller is
// closing directly, not via the tween's intended path).
//
// Also calls tig_window_transform_clear on the window so any state
// the cancelled tween left in tig's compositor is reset to the
// natural 1:1 opaque path. Safe to call on a window that has no
// active tween (no-op).
void ui_anim_cancel_for_window(tig_window_handle_t window);

// CE: start a "show" animation with a chained tint-reveal phase
// after entrance settles. The standard tig_window_tint_enable
// (intgame_apply_translucent_black) sets r/g/b at full strength;
// during the entrance the compositor's transform pathway bypasses
// the tint blit entirely so the panel shows fully opaque. When the
// transform clears at entrance settle, the tint would normally snap
// in at full strength — visible as a hard "darken" pop. This helper
// instead seeds tint_reveal = 0 before the entrance starts, then on
// settle springs tint_reveal 0 → 1 over reveal_settle_ms. Smooth.
//
// reveal_settle_ms = 0 uses a default ~150ms.
ui_anim_handle_t ui_anim_window_show_with_tint_reveal(
    tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* show_profile,
    int reveal_settle_ms);

#ifdef __cplusplus
}
#endif

#endif /* ARCANUM_UI_UI_ANIM_H_ */
