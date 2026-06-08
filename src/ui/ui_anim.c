#include "ui/ui_anim.h"

#include <math.h>

#include <SDL3/SDL.h>

#include "game/gamelib.h"
#include "game/settings.h"
#include "tig/debug.h"
#include "tig/timer.h"
#include "tig/window.h"

// CE: ping perf instrumentation. Enabled by the gamelib F9 zoom-perf
// toggle alongside the existing flip-perf / tint-blit counters so all
// UI-related cost shows up in one dump.
static bool s_perf_enabled = false;
static TigUiAnimPerf s_perf;

void ui_anim_perf_set_enabled(bool enabled)
{
    s_perf_enabled = enabled;
    if (enabled) {
        s_perf.ping_total_ns = 0;
        s_perf.ping_max_ns = 0;
        s_perf.ping_samples = 0;
        s_perf.apply_total_ns = 0;
        s_perf.active_slots_max = 0;
        s_perf.active_slots_total = 0;
    }
}

void ui_anim_perf_get(TigUiAnimPerf* out)
{
    if (out != NULL) *out = s_perf;
}

void ui_anim_perf_reset(void)
{
    s_perf.ping_total_ns = 0;
    s_perf.ping_max_ns = 0;
    s_perf.ping_samples = 0;
    s_perf.apply_total_ns = 0;
    s_perf.active_slots_max = 0;
    s_perf.active_slots_total = 0;
}

// CE: see ui_anim.h. Fixed pool of concurrent tweens; per-frame ping
// advances every active slot's spring state. Handles encode (slot,
// generation) so stale handles fail-safe.

// Pool size. 32 is plenty: even a busy session has at most ~5-10
// concurrent tweens (a window entering, the dialog Y, fate/sleep
// sliding, the top HUD slide, maybe a couple of stragglers). The slot
// pool is static so the module never allocates per-tween.
#define UI_ANIM_POOL_SIZE 32

// Cap dt to keep one bad frame (e.g. a hitch, alt-tab) from launching
// values across the screen via a giant Euler step. 33ms = ~30fps frame
// budget — anything longer gets clamped.
#define UI_ANIM_DT_MAX_MS 33

// Sub-step within ui_anim_ping. Semi-implicit Euler is stable for an
// overdamped MSD when dt * omega_n is bounded; with our stiffness
// (~450 for the default 180ms entrance) dt > ~5ms can produce
// spurious oscillation that visually shows up as a scale-wobble
// battle. Sub-stepping the per-frame dt into 4ms chunks keeps the
// integrator in its stable regime regardless of frame timing.
#define UI_ANIM_SUBSTEP_MS 4

// Settle epsilons. Tightened progressively to eliminate end-frame
// pixel pops + slow-tail stepping jitter on the menu scale animation:
//   - 0.002 (original) → ~4px pop on 2000-wide backdrop at clear
//   - 0.0002 → still a ~2px pop because the parity-matched dst_w
//     rounded to 1998 at value 0.9997 (frame.width=2000 case)
//   - 0.0001 → end pop gone, but the last 2-3 spring frames could
//     still cross a parity boundary (1922 → 1920) leaving a
//     visible 2-pixel step at the very end of the recede/return
//   - 0.00003 → the spring's last ~4 frames all land in the same
//     dst_w bucket (parity-stable around target), so the visual
//     end-state arrives well before the spring "officially"
//     settles. No final-frame step. Costs ~2x extra spring frames
//     past visual quiescence, but they're invisible.
//
// VELOCITY_EPS scaled to match. At value within 0.00003 of target,
// an overdamped spring's velocity is essentially zero anyway, so
// this rarely gates settle, but a tighter bound ensures we don't
// declare settle while still drifting.
#define UI_ANIM_VALUE_EPS 0.00003f
#define UI_ANIM_VELOCITY_EPS 0.003f

// Max scalars per slot. Window transform tween uses 3 (scale, alpha,
// padding); int/float var tweens use 1. Bumping this is cheap.
#define UI_ANIM_MAX_SCALARS 4

// CE: animate tint-enabled windows in REALTIME instead of from a frozen
// snapshot. When 1, transform animations on tinted windows DON'T capture
// a pre-baked snapshot, so the compositor uses its integrated
// transform-tinted blit each frame — the near-black see-through tracks
// the LIVE underlay during the scale/alpha phase and is identical to the
// settled 1:1 realtime tint, so there's nothing to snap to (no frozen
// underlay, no post-settle cross-fade needed). The snapshot path was a
// perf optimization (HW scaled blit) that froze the underlay at anim
// start and caused the snap. Set to 0 to restore snapshot + cross-fade.
#define UI_ANIM_TINT_ANIMATE_REALTIME 1

typedef enum {
    UI_ANIM_KIND_NONE,
    UI_ANIM_KIND_WINDOW_TRANSFORM,
    UI_ANIM_KIND_WINDOW_TINT_REVEAL,
    // CE: post-entrance dissolve of the tint snapshot over the realtime
    // tint (value[0] = snapshot fade, 1→0; releases the snapshot at end).
    UI_ANIM_KIND_WINDOW_TINT_XFADE,
    UI_ANIM_KIND_INT_VAR,
    UI_ANIM_KIND_FLOAT_VAR,
} ui_anim_kind_t;

typedef struct {
    bool active;
    int generation;
    ui_anim_kind_t kind;

    // Spring scalars. value[i] is the current animated position;
    // velocity[i] is its derivative; target[i] is where it's heading.
    int n_scalars;
    float value[UI_ANIM_MAX_SCALARS];
    float velocity[UI_ANIM_MAX_SCALARS];
    float target[UI_ANIM_MAX_SCALARS];

    // Spring physics. stiffness k (units: 1/s^2), damping c (units: 1/s).
    // Derived once at start_* from the profile and held constant for the
    // tween's lifetime so retarget snaps without re-deriving.
    float stiffness;
    float damping;

    // Per-kind state.
    union {
        struct {
            tig_window_handle_t window;
            float anchor_rel_x;
            float anchor_rel_y;
        } transform;
        struct {
            tig_window_handle_t window;
        } tint_reveal;
        struct {
            tig_window_handle_t window;
        } tint_xfade;
        struct {
            int* slot;
        } int_var;
        struct {
            float* slot;
        } float_var;
    } u;

    void (*on_complete)(void* ctx);
    void* on_complete_ctx;
} ui_anim_slot_t;

static ui_anim_slot_t s_slots[UI_ANIM_POOL_SIZE];

// CE: per-call context bag for ui_anim_window_show_with_tint_reveal —
// captures the window + reveal profile so the entrance's on_complete
// can chain the tint-reveal tween. Static pool sized to typical concurrent
// uses (one in-game overlay + a couple mainmenu transitions); if
// exhausted the helper degrades to a plain show without tint fade.
typedef struct {
    tig_window_handle_t window;
    ui_anim_profile_t reveal_profile;
} ui_anim_tint_reveal_ctx_t;
#define UI_ANIM_REVEAL_CTX_POOL 8
static ui_anim_tint_reveal_ctx_t s_reveal_ctx_pool[UI_ANIM_REVEAL_CTX_POOL];

// Generation counter — increments per slot reuse so old handles fail
// the active check. Wraparound is fine in practice (2^16 reuses per
// slot is enough for a session).
static int s_next_generation = 1;

static bool s_initialized = false;
static tig_timestamp_t s_last_ping_ms;
static bool s_have_last_ping = false;

// Cfg cache. Refreshed lazily; cheap to read on each start.
static bool ui_anim_cfg_enabled(void)
{
    return settings_get_value(&settings, UI_ANIMATIONS_KEY) != 0;
}

// CE: profiles. Slightly overdamped (damping ratio 1.2) for a settle
// without overshoot but with smoother motion than critically damped.
// Settle-time numbers are approximate — actual settle depends on initial
// offset, but for typical 0..1 normalized scalars these land in the
// ballpark of the stated ms. Entrance is intentionally shorter than
// exit; rapid entrance reads as "responsive", slow exit reads as
// "intentional dismissal" (matches typical OS/app UI conventions).
// CE: spring settle times (ms) + damping ratios. settle_ms is the
// approximate time the spring takes to land within the value epsilon
// (UI_ANIM_VALUE_EPS = 0.002, ~0.2% of normalized range). Damping
// ratio 1.2 = slightly overdamped, no overshoot, smooth tail.
//
//   ENTRANCE = 150ms — slightly slower than the original 130ms; user
//     feedback said the entrance still felt slightly snappy at 130,
//     and 150 makes the panel arrive with a bit more weight.
//   EXIT     = 260ms — slowed from 200ms. Exits at 200 felt rushed,
//     particularly the slide-up dismiss of fate/sleep; the slower
//     tail gives the user time to register the panel leaving.
//   VAR      = 200ms — for int/float slot tweens (TC dialog Y,
//     fate/sleep slide offsets); a hair slower than the old 180 to
//     match the new EXIT feel.
//   SLIDE    = 200ms — for the TAB-HUD top bar slide; matches VAR.
const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_ENTRANCE = { 150, 1.2f };
const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_EXIT     = { 260, 1.2f };
const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_VAR      = { 200, 1.2f };
const ui_anim_profile_t UI_ANIM_PROFILE_DEFAULT_SLIDE    = { 200, 1.2f };

// CE: derive spring constants (k, c) from the user-facing profile. We
// solve from the time-to-1%-settle formula for an overdamped spring:
// settle_t ~= -ln(0.01) / (zeta * omega_n) ~= 4.6 / (zeta * omega_n),
// so omega_n = 4.6 / (zeta * settle_t). k = omega_n^2; c = 2 * zeta *
// omega_n (unit mass). Clamping omega_n to a positive value so
// settle_ms=0 (caller error) yields a snappy-but-finite spring instead
// of NaN.
static void ui_anim_profile_to_spring(const ui_anim_profile_t* profile,
    float* out_k,
    float* out_c)
{
    float zeta = profile->damping_ratio;
    if (zeta < 1.0f) zeta = 1.0f; // clamp — no bounciness
    float settle_s = (float)profile->settle_ms / 1000.0f;
    if (settle_s < 0.01f) settle_s = 0.01f;
    float omega_n = 4.6f / (zeta * settle_s);
    *out_k = omega_n * omega_n;
    *out_c = 2.0f * zeta * omega_n;
}

void ui_anim_anchor_to_rel(ui_anim_anchor_t anchor,
    float* rel_x,
    float* rel_y)
{
    float x = 0.5f;
    float y = 0.5f;
    switch (anchor) {
    case UI_ANIM_ANCHOR_CENTER:        x = 0.5f; y = 0.5f; break;
    case UI_ANIM_ANCHOR_TOP_LEFT:      x = 0.0f; y = 0.0f; break;
    case UI_ANIM_ANCHOR_TOP_CENTER:    x = 0.5f; y = 0.0f; break;
    case UI_ANIM_ANCHOR_TOP_RIGHT:     x = 1.0f; y = 0.0f; break;
    case UI_ANIM_ANCHOR_MIDDLE_LEFT:   x = 0.0f; y = 0.5f; break;
    case UI_ANIM_ANCHOR_MIDDLE_RIGHT:  x = 1.0f; y = 0.5f; break;
    case UI_ANIM_ANCHOR_BOTTOM_LEFT:   x = 0.0f; y = 1.0f; break;
    case UI_ANIM_ANCHOR_BOTTOM_CENTER: x = 0.5f; y = 1.0f; break;
    case UI_ANIM_ANCHOR_BOTTOM_RIGHT:  x = 1.0f; y = 1.0f; break;
    case UI_ANIM_ANCHOR_CUSTOM:        x = 0.5f; y = 0.5f; break;
    }
    if (rel_x) *rel_x = x;
    if (rel_y) *rel_y = y;
}

// Pack (slot_index, generation) into an opaque handle. Slot index is in
// low byte, generation in high bits. Handle 0 is reserved as INVALID.
static ui_anim_handle_t ui_anim_make_handle(int slot_index, int generation)
{
    return (ui_anim_handle_t)((generation << 8) | (slot_index & 0xFF)) + 1;
}

static bool ui_anim_handle_decode(ui_anim_handle_t handle,
    int* out_slot_index,
    int* out_generation)
{
    if (handle == UI_ANIM_HANDLE_INVALID) return false;
    int raw = (int)handle - 1;
    int slot_index = raw & 0xFF;
    int generation = raw >> 8;
    if (slot_index < 0 || slot_index >= UI_ANIM_POOL_SIZE) return false;
    *out_slot_index = slot_index;
    *out_generation = generation;
    return true;
}

static int ui_anim_alloc_slot(void)
{
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (!s_slots[i].active) {
            s_slots[i].active = true;
            s_slots[i].generation = s_next_generation++;
            if (s_next_generation > (1 << 23)) s_next_generation = 1;
            return i;
        }
    }
    tig_debug_printf("ui_anim: ERROR: pool exhausted (%d slots)\n",
        UI_ANIM_POOL_SIZE);
    return -1;
}

static ui_anim_slot_t* ui_anim_resolve(ui_anim_handle_t handle)
{
    int slot_index;
    int generation;
    if (!ui_anim_handle_decode(handle, &slot_index, &generation)) return NULL;
    ui_anim_slot_t* s = &s_slots[slot_index];
    if (!s->active || s->generation != generation) return NULL;
    return s;
}

// Find any active slot animating a given window (transform kind only).
// Used by show/hide retarget — preserves velocity continuity.
static ui_anim_slot_t* ui_anim_find_window(tig_window_handle_t window)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return NULL;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && s_slots[i].kind == UI_ANIM_KIND_WINDOW_TRANSFORM
            && s_slots[i].u.transform.window == window) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static void ui_anim_clear_slot(ui_anim_slot_t* s)
{
    s->active = false;
    s->kind = UI_ANIM_KIND_NONE;
    s->on_complete = NULL;
    s->on_complete_ctx = NULL;
}

// Apply the slot's current scalars to its target. Called after each
// integration step and once more on settle so the final value lands
// exactly on target (without spring-residual drift).
static void ui_anim_apply(ui_anim_slot_t* s)
{
    switch (s->kind) {
    case UI_ANIM_KIND_WINDOW_TRANSFORM: {
        // scalars: [0]=scale, [1]=alpha, [2]=unused
        float scale = s->value[0];
        float alpha = s->value[1];
        if (scale < 0.0f) scale = 0.0f;
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        tig_window_transform_set(s->u.transform.window,
            scale, scale, alpha,
            s->u.transform.anchor_rel_x,
            s->u.transform.anchor_rel_y);
        break;
    }
    case UI_ANIM_KIND_WINDOW_TINT_REVEAL: {
        float reveal = s->value[0];
        if (reveal < 0.0f) reveal = 0.0f;
        if (reveal > 1.0f) reveal = 1.0f;
        tig_window_tint_reveal_set(s->u.tint_reveal.window, reveal);
        break;
    }
    case UI_ANIM_KIND_WINDOW_TINT_XFADE: {
        float fade = s->value[0];
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        tig_window_tint_snapshot_fade_set(s->u.tint_xfade.window, fade);
        break;
    }
    case UI_ANIM_KIND_INT_VAR:
        if (s->u.int_var.slot != NULL) {
            *s->u.int_var.slot = (int)(s->value[0] + (s->value[0] >= 0 ? 0.5f : -0.5f));
        }
        break;
    case UI_ANIM_KIND_FLOAT_VAR:
        if (s->u.float_var.slot != NULL) {
            *s->u.float_var.slot = s->value[0];
        }
        break;
    case UI_ANIM_KIND_NONE:
        break;
    }
}

// CE: approximate dissolve time for the post-entrance snapshot→realtime
// tint cross-fade.
#define UI_ANIM_TINT_XFADE_MS 170

// CE: drop any in-flight tint cross-fade for `window` WITHOUT releasing
// the snapshot — called when a new transform animation starts on the
// same window (the new anim re-captures and owns the snapshot). Without
// this, the stale xfade's delayed release would destroy the snapshot
// mid-animation.
static void ui_anim_cancel_tint_xfade(tig_window_handle_t window)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && s_slots[i].kind == UI_ANIM_KIND_WINDOW_TINT_XFADE
            && s_slots[i].u.tint_xfade.window == window) {
            ui_anim_clear_slot(&s_slots[i]);
        }
    }
}

// CE: kick off the post-entrance tint cross-fade for `window`. The
// transform has just cleared, so the compositor is on the realtime tint
// path; the snapshot is still alive and fully covering (fade=1). Ramp
// the snapshot fade 1→0 over UI_ANIM_TINT_XFADE_MS, then release it
// (handled in finalize). Falls back to an immediate release whenever
// there's nothing to dissolve or no slot/animation is available.
static void ui_anim_start_tint_xfade(tig_window_handle_t window)
{
    if (window == TIG_WINDOW_HANDLE_INVALID) return;

    // No snapshot (untinted window) → nothing to dissolve.
    if (tig_window_tint_snapshot_get(window) == NULL) {
        tig_window_tint_snapshot_release(window);  // no-op
        return;
    }

    // Start fully covering so the first cross-fade frame matches the
    // last animation frame exactly (no seam).
    tig_window_tint_snapshot_fade_set(window, 1.0f);

    if (!ui_anim_cfg_enabled()) {
        tig_window_tint_snapshot_release(window);
        return;
    }

    int slot_index = ui_anim_alloc_slot();
    if (slot_index < 0) {
        tig_window_tint_snapshot_release(window);
        return;
    }
    ui_anim_slot_t* s = &s_slots[slot_index];
    s->kind = UI_ANIM_KIND_WINDOW_TINT_XFADE;
    s->n_scalars = 1;
    s->value[0] = 1.0f;
    s->velocity[0] = 0.0f;
    s->target[0] = 0.0f;
    s->u.tint_xfade.window = window;
    ui_anim_profile_t prof;
    prof.settle_ms = UI_ANIM_TINT_XFADE_MS;
    prof.damping_ratio = 1.2f;
    ui_anim_profile_to_spring(&prof, &s->stiffness, &s->damping);
    s->on_complete = NULL;
    s->on_complete_ctx = NULL;
    ui_anim_apply(s);
}

// Final-frame apply: snap values to target and clear the transform on
// windows that ended at scale=1 alpha=1 (so the compositor's transform
// short-circuit kicks back in for free).
static void ui_anim_finalize(ui_anim_slot_t* s)
{
    for (int i = 0; i < s->n_scalars; i++) {
        s->value[i] = s->target[i];
        s->velocity[i] = 0.0f;
    }
    if (s->kind == UI_ANIM_KIND_WINDOW_TRANSFORM) {
        float scale = s->target[0];
        float alpha = s->target[1];
        // Clean default: window settled fully visible at scale 1.0 —
        // drop the transform so the compositor takes the fast path.
        // Otherwise hold the transform (e.g. hidden state, scale != 1).
        if (scale >= 1.0f - UI_ANIM_VALUE_EPS && alpha >= 1.0f - UI_ANIM_VALUE_EPS) {
            tig_window_transform_clear(s->u.transform.window);
            // Settled fully visible: dissolve the snapshot out over the
            // realtime tint instead of dropping it instantly (avoids the
            // snapshot→realtime pop). No-op for untinted windows.
            ui_anim_start_tint_xfade(s->u.transform.window);
        } else {
            tig_window_transform_set(s->u.transform.window,
                scale, scale, alpha,
                s->u.transform.anchor_rel_x,
                s->u.transform.anchor_rel_y);
            // Hidden / non-unit end state — nothing on screen to
            // dissolve; drop the snapshot immediately.
            tig_window_tint_snapshot_release(s->u.transform.window);
        }
    } else if (s->kind == UI_ANIM_KIND_WINDOW_TINT_XFADE) {
        // Dissolve complete — realtime tint stands alone; drop snapshot.
        tig_window_tint_snapshot_fade_set(s->u.tint_xfade.window, 0.0f);
        tig_window_tint_snapshot_release(s->u.tint_xfade.window);
    } else {
        ui_anim_apply(s);
    }
}

// Forward decl — defined below; registered with tig at init.
void ui_anim_notify_window_destroyed(tig_window_handle_t window);

bool ui_anim_init(void)
{
    if (s_initialized) return true;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        ui_anim_clear_slot(&s_slots[i]);
    }
    for (int i = 0; i < UI_ANIM_REVEAL_CTX_POOL; i++) {
        s_reveal_ctx_pool[i].window = TIG_WINDOW_HANDLE_INVALID;
    }
    s_have_last_ping = false;
    s_initialized = true;
    // Register destroy-notify so tig calls us when any window is
    // destroyed — we cancel any tween targeting that handle so the
    // next ping doesn't write to a dead window.
    tig_window_destroy_notify_set(ui_anim_notify_window_destroyed);
    return true;
}

void ui_anim_exit(void)
{
    if (!s_initialized) return;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        ui_anim_clear_slot(&s_slots[i]);
    }
    s_initialized = false;
}

void ui_anim_reset(void)
{
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        ui_anim_clear_slot(&s_slots[i]);
    }
    for (int i = 0; i < UI_ANIM_REVEAL_CTX_POOL; i++) {
        s_reveal_ctx_pool[i].window = TIG_WINDOW_HANDLE_INVALID;
    }
    s_have_last_ping = false;
}

void ui_anim_ping(void)
{
    if (!s_initialized) return;

    tig_timestamp_t now;
    tig_timer_now(&now);

    if (!s_have_last_ping) {
        s_last_ping_ms = now;
        s_have_last_ping = true;
        return;
    }

    int dt_ms = (int)tig_timer_between(s_last_ping_ms, now);
    s_last_ping_ms = now;
    if (dt_ms <= 0) return;
    if (dt_ms > UI_ANIM_DT_MAX_MS) dt_ms = UI_ANIM_DT_MAX_MS;
    float dt = (float)dt_ms / 1000.0f;

    uint64_t perf_t0 = s_perf_enabled ? SDL_GetPerformanceCounter() : 0;
    int active_count = 0;

    // Sub-step the frame dt into smaller chunks so the integrator
    // stays stable at any frame timing. Per-frame total work scales
    // with frame time but the per-step math is identical, just
    // repeated.
    int n_substeps = (dt_ms + UI_ANIM_SUBSTEP_MS - 1) / UI_ANIM_SUBSTEP_MS;
    if (n_substeps < 1) n_substeps = 1;
    float sub_dt = dt / (float)n_substeps;

    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        ui_anim_slot_t* s = &s_slots[i];
        if (!s->active) continue;
        active_count++;

        // Spring integration. Semi-implicit Euler per substep: stable
        // for overdamped systems at substep-bounded dts.
        bool settled = true;
        for (int step = 0; step < n_substeps; step++) {
            for (int k = 0; k < s->n_scalars; k++) {
                float force = -s->stiffness * (s->value[k] - s->target[k])
                             - s->damping * s->velocity[k];
                s->velocity[k] += force * sub_dt;
                s->value[k] += s->velocity[k] * sub_dt;
            }
        }
        for (int k = 0; k < s->n_scalars; k++) {
            float dv = s->value[k] - s->target[k];
            if (dv < 0.0f) dv = -dv;
            float vv = s->velocity[k];
            if (vv < 0.0f) vv = -vv;
            if (dv >= UI_ANIM_VALUE_EPS || vv >= UI_ANIM_VELOCITY_EPS) {
                settled = false;
            }
        }

        uint64_t apply_t0 = s_perf_enabled ? SDL_GetPerformanceCounter() : 0;
        if (settled) {
            ui_anim_finalize(s);
            void (*cb)(void*) = s->on_complete;
            void* ctx = s->on_complete_ctx;
            ui_anim_clear_slot(s);
            if (cb != NULL) cb(ctx);
        } else {
            ui_anim_apply(s);
        }
        if (s_perf_enabled) {
            uint64_t apply_t1 = SDL_GetPerformanceCounter();
            uint64_t apply_ns = (uint64_t)((double)(apply_t1 - apply_t0)
                * 1e9 / (double)SDL_GetPerformanceFrequency());
            s_perf.apply_total_ns += apply_ns;
        }
    }

    if (s_perf_enabled) {
        uint64_t perf_t1 = SDL_GetPerformanceCounter();
        uint64_t ping_ns = (uint64_t)((double)(perf_t1 - perf_t0)
            * 1e9 / (double)SDL_GetPerformanceFrequency());
        s_perf.ping_total_ns += ping_ns;
        if (ping_ns > s_perf.ping_max_ns) s_perf.ping_max_ns = ping_ns;
        s_perf.ping_samples++;
        if (active_count > s_perf.active_slots_max) s_perf.active_slots_max = active_count;
        s_perf.active_slots_total += active_count;
    }
}

// CE: cfg-disabled fast path — write the end state immediately, fire
// on_complete synchronously, return INVALID (caller's ui_anim_is_active
// will return false). Used by show/hide/var when the cfg key is off.
static ui_anim_handle_t ui_anim_apply_end_state_now(ui_anim_kind_t kind,
    void* target,
    const float* end_values,
    int n,
    float anchor_rel_x,
    float anchor_rel_y,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    switch (kind) {
    case UI_ANIM_KIND_WINDOW_TRANSFORM: {
        tig_window_handle_t window = (tig_window_handle_t)(uintptr_t)target;
        float scale = end_values[0];
        float alpha = end_values[1];
        if (scale >= 1.0f - UI_ANIM_VALUE_EPS && alpha >= 1.0f - UI_ANIM_VALUE_EPS) {
            tig_window_transform_clear(window);
        } else {
            tig_window_transform_set(window, scale, scale, alpha,
                anchor_rel_x, anchor_rel_y);
        }
        break;
    }
    case UI_ANIM_KIND_INT_VAR: {
        int* slot = (int*)target;
        if (slot != NULL) {
            *slot = (int)(end_values[0] + (end_values[0] >= 0 ? 0.5f : -0.5f));
        }
        break;
    }
    case UI_ANIM_KIND_FLOAT_VAR: {
        float* slot = (float*)target;
        if (slot != NULL) {
            *slot = end_values[0];
        }
        break;
    }
    case UI_ANIM_KIND_NONE:
        break;
    }
    (void)n;
    if (on_complete != NULL) on_complete(ctx);
    return UI_ANIM_HANDLE_INVALID;
}

// Forward decl — defined a few functions down.
static ui_anim_handle_t ui_anim_window_show_ex_with_complete(
    tig_window_handle_t window,
    float rel_x,
    float rel_y,
    float scale_from,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx);

ui_anim_handle_t ui_anim_window_show(tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* profile)
{
    float rel_x, rel_y;
    ui_anim_anchor_to_rel(anchor, &rel_x, &rel_y);
    return ui_anim_window_show_ex(window, rel_x, rel_y, scale_from, profile);
}

ui_anim_handle_t ui_anim_window_show_ex(tig_window_handle_t window,
    float rel_x,
    float rel_y,
    float scale_from,
    const ui_anim_profile_t* profile)
{
    return ui_anim_window_show_ex_with_complete(window, rel_x, rel_y,
        scale_from, profile, NULL, NULL);
}

ui_anim_handle_t ui_anim_window_show_with_complete(
    tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    float rel_x, rel_y;
    ui_anim_anchor_to_rel(anchor, &rel_x, &rel_y);
    return ui_anim_window_show_ex_with_complete(window, rel_x, rel_y,
        scale_from, profile, on_complete, ctx);
}

// CE: shared impl for show / show_ex / show_with_complete /
// show_ex_with_complete. Exposed locally to ui_anim.c.
static ui_anim_handle_t ui_anim_window_show_ex_with_complete(
    tig_window_handle_t window,
    float rel_x,
    float rel_y,
    float scale_from,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    if (!s_initialized) ui_anim_init();
    if (window == TIG_WINDOW_HANDLE_INVALID) {
        if (on_complete != NULL) on_complete(ctx);
        return UI_ANIM_HANDLE_INVALID;
    }
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_ENTRANCE;

    float end[2] = { 1.0f, 1.0f };
    if (!ui_anim_cfg_enabled()) {
        return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
            (void*)(uintptr_t)window, end, 2, rel_x, rel_y,
            on_complete, ctx);
    }

    // Retarget if already animating: preserve velocity, just update
    // start values + target. New profile replaces old. Fire any prev
    // on_complete first so it isn't silently dropped.
    ui_anim_slot_t* s = ui_anim_find_window(window);
    int slot_index;
    if (s != NULL) {
        slot_index = (int)(s - s_slots);
        if (s->on_complete != NULL) {
            void (*prev_cb)(void*) = s->on_complete;
            void* prev_ctx = s->on_complete_ctx;
            s->on_complete = NULL;
            prev_cb(prev_ctx);
        }
    } else {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            // pool full → apply end state to keep UI consistent
            return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
                (void*)(uintptr_t)window, end, 2, rel_x, rel_y,
                on_complete, ctx);
        }
        s = &s_slots[slot_index];
        // Fresh slot: seed values from scale_from / alpha 0.
        s->value[0] = scale_from;
        s->value[1] = 0.0f;
        s->velocity[0] = 0.0f;
        s->velocity[1] = 0.0f;
    }
    s->kind = UI_ANIM_KIND_WINDOW_TRANSFORM;
    s->n_scalars = 2;
    s->target[0] = 1.0f;
    s->target[1] = 1.0f;
    s->u.transform.window = window;
    s->u.transform.anchor_rel_x = rel_x;
    s->u.transform.anchor_rel_y = rel_y;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = on_complete;
    s->on_complete_ctx = ctx;

    // CE: capture a pre-tinted snapshot of the window's VB so the
    // compositor's anim path can use SDL_BlitSurfaceScaled (HW
    // accelerated, ~10× cheaper than the per-pixel integrated
    // transform-tint-alpha blit). No-op for non-tint windows. The
    // snapshot is released on settle (in finalize via the
    // transform-clear branch) or on cancel.
#if !UI_ANIM_TINT_ANIMATE_REALTIME
    ui_anim_cancel_tint_xfade(window);
    tig_window_tint_snapshot_capture(window);
#endif

    // Prime the compositor with the starting transform so the very
    // first frame shows the scaled-down state, not full-size.
    ui_anim_apply(s);
    return ui_anim_make_handle(slot_index, s->generation);
}

ui_anim_handle_t ui_anim_window_hide(tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_to,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    float rel_x, rel_y;
    ui_anim_anchor_to_rel(anchor, &rel_x, &rel_y);
    return ui_anim_window_hide_ex(window, rel_x, rel_y, scale_to, profile,
        on_complete, ctx);
}

ui_anim_handle_t ui_anim_window_hide_ex(tig_window_handle_t window,
    float rel_x,
    float rel_y,
    float scale_to,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    if (!s_initialized) ui_anim_init();
    if (window == TIG_WINDOW_HANDLE_INVALID) {
        if (on_complete != NULL) on_complete(ctx);
        return UI_ANIM_HANDLE_INVALID;
    }
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_EXIT;

    float end[2] = { scale_to, 0.0f };
    if (!ui_anim_cfg_enabled()) {
        return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
            (void*)(uintptr_t)window, end, 2, rel_x, rel_y, on_complete, ctx);
    }

    ui_anim_slot_t* s = ui_anim_find_window(window);
    int slot_index;
    if (s != NULL) {
        slot_index = (int)(s - s_slots);
        // Retarget — keep current value + velocity. If a previous
        // on_complete is pending, fire it now (we're replacing the
        // animation; the prior on_complete's expectation that its
        // tween reached "settled" is closest-approximated by firing
        // it once we replace the tween).
        if (s->on_complete != NULL) {
            void (*prev_cb)(void*) = s->on_complete;
            void* prev_ctx = s->on_complete_ctx;
            s->on_complete = NULL;
            prev_cb(prev_ctx);
        }
    } else {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
                (void*)(uintptr_t)window, end, 2, rel_x, rel_y, on_complete, ctx);
        }
        s = &s_slots[slot_index];
        // Fresh slot: assume window is currently fully visible.
        s->value[0] = 1.0f;
        s->value[1] = 1.0f;
        s->velocity[0] = 0.0f;
        s->velocity[1] = 0.0f;
    }
    s->kind = UI_ANIM_KIND_WINDOW_TRANSFORM;
    s->n_scalars = 2;
    s->target[0] = scale_to;
    s->target[1] = 0.0f;
    s->u.transform.window = window;
    s->u.transform.anchor_rel_x = rel_x;
    s->u.transform.anchor_rel_y = rel_y;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = on_complete;
    s->on_complete_ctx = ctx;

    // CE: capture a pre-tinted snapshot for tint-enabled windows so
    // the compositor uses the fast scaled-blit path during the
    // animation instead of the per-pixel integrated tint blit. The
    // exit (hide) path was previously falling back to the slow path
    // because the snapshot was only captured on show — visible as
    // stuttering frame drops on the bottom-HUD FULL→HIDDEN scale.
    // No-op on non-tint windows. Released on settle/cancel.
#if !UI_ANIM_TINT_ANIMATE_REALTIME
    ui_anim_cancel_tint_xfade(window);
    tig_window_tint_snapshot_capture(window);
#endif

    ui_anim_apply(s);
    return ui_anim_make_handle(slot_index, s->generation);
}

ui_anim_handle_t ui_anim_window_transform_to(
    tig_window_handle_t window,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile)
{
    return ui_anim_window_transform_from_to_with_complete(window,
        1.0f, 1.0f, scale_to, alpha_to, anchor, profile, NULL, NULL);
}

ui_anim_handle_t ui_anim_window_transform_from_to(
    tig_window_handle_t window,
    float scale_from,
    float alpha_from,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile)
{
    return ui_anim_window_transform_from_to_with_complete(window,
        scale_from, alpha_from, scale_to, alpha_to, anchor, profile,
        NULL, NULL);
}

ui_anim_handle_t ui_anim_window_transform_from_to_with_complete(
    tig_window_handle_t window,
    float scale_from,
    float alpha_from,
    float scale_to,
    float alpha_to,
    ui_anim_anchor_t anchor,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    if (!s_initialized) ui_anim_init();
    if (window == TIG_WINDOW_HANDLE_INVALID) {
        if (on_complete != NULL) on_complete(ctx);
        return UI_ANIM_HANDLE_INVALID;
    }
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_VAR;

    float rel_x, rel_y;
    ui_anim_anchor_to_rel(anchor, &rel_x, &rel_y);

    float end[2] = { scale_to, alpha_to };
    if (!ui_anim_cfg_enabled()) {
        return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
            (void*)(uintptr_t)window, end, 2, rel_x, rel_y, on_complete, ctx);
    }

    ui_anim_slot_t* s = ui_anim_find_window(window);
    int slot_index;
    if (s != NULL) {
        slot_index = (int)(s - s_slots);
        // Retarget — keep current value + velocity. Fire previous
        // on_complete first (don't silently drop callers' callbacks).
        // The from_* values are ignored on retarget: a tween already
        // in flight has authoritative state.
        if (s->on_complete != NULL) {
            void (*prev_cb)(void*) = s->on_complete;
            void* prev_ctx = s->on_complete_ctx;
            s->on_complete = NULL;
            prev_cb(prev_ctx);
        }
    } else {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            return ui_anim_apply_end_state_now(UI_ANIM_KIND_WINDOW_TRANSFORM,
                (void*)(uintptr_t)window, end, 2, rel_x, rel_y, on_complete, ctx);
        }
        s = &s_slots[slot_index];
        // Fresh slot: seed at caller-specified (scale_from, alpha_from).
        s->value[0] = scale_from;
        s->value[1] = alpha_from;
        s->velocity[0] = 0.0f;
        s->velocity[1] = 0.0f;
    }
    s->kind = UI_ANIM_KIND_WINDOW_TRANSFORM;
    s->n_scalars = 2;
    s->target[0] = scale_to;
    s->target[1] = alpha_to;
    s->u.transform.window = window;
    s->u.transform.anchor_rel_x = rel_x;
    s->u.transform.anchor_rel_y = rel_y;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = on_complete;
    s->on_complete_ctx = ctx;

    // CE: capture tint snapshot for the same reason as show/hide —
    // tint-enabled windows otherwise use the slow per-pixel blit
    // during the animation. No-op on non-tint windows (mainmenu
    // backdrop doesn't use tint anymore, so this is defensive).
#if !UI_ANIM_TINT_ANIMATE_REALTIME
    ui_anim_cancel_tint_xfade(window);
    tig_window_tint_snapshot_capture(window);
#endif

    ui_anim_apply(s);
    return ui_anim_make_handle(slot_index, s->generation);
}

ui_anim_handle_t ui_anim_int_to(int* slot, int target,
    const ui_anim_profile_t* profile)
{
    return ui_anim_int_to_with_complete(slot, target, profile, NULL, NULL);
}

ui_anim_handle_t ui_anim_int_to_with_complete(int* slot, int target,
    const ui_anim_profile_t* profile,
    void (*on_complete)(void* ctx),
    void* ctx)
{
    if (!s_initialized) ui_anim_init();
    if (slot == NULL) {
        if (on_complete != NULL) on_complete(ctx);
        return UI_ANIM_HANDLE_INVALID;
    }
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_VAR;

    float end[1] = { (float)target };
    if (!ui_anim_cfg_enabled()) {
        return ui_anim_apply_end_state_now(UI_ANIM_KIND_INT_VAR,
            (void*)slot, end, 1, 0.0f, 0.0f, on_complete, ctx);
    }

    // Retarget if an existing int tween targets the same slot pointer
    // (preserves velocity for smooth re-aim mid-flight).
    int slot_index = -1;
    ui_anim_slot_t* s = NULL;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && s_slots[i].kind == UI_ANIM_KIND_INT_VAR
            && s_slots[i].u.int_var.slot == slot) {
            slot_index = i;
            s = &s_slots[i];
            break;
        }
    }
    if (s != NULL) {
        // Retarget — fire any previous on_complete first so callers
        // who relied on theirs running don't see it silently dropped.
        if (s->on_complete != NULL) {
            void (*prev_cb)(void*) = s->on_complete;
            void* prev_ctx = s->on_complete_ctx;
            s->on_complete = NULL;
            prev_cb(prev_ctx);
        }
    } else {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            return ui_anim_apply_end_state_now(UI_ANIM_KIND_INT_VAR,
                (void*)slot, end, 1, 0.0f, 0.0f, on_complete, ctx);
        }
        s = &s_slots[slot_index];
        s->value[0] = (float)(*slot);
        s->velocity[0] = 0.0f;
    }
    // No-op early return if already at target with no velocity (avoids
    // burning ping cycles on dead tweens). Fire on_complete sync so
    // callers' expectation that completion happens is preserved.
    if (s->value[0] == (float)target && s->velocity[0] == 0.0f) {
        ui_anim_clear_slot(s);
        if (on_complete != NULL) on_complete(ctx);
        return UI_ANIM_HANDLE_INVALID;
    }
    s->kind = UI_ANIM_KIND_INT_VAR;
    s->n_scalars = 1;
    s->target[0] = (float)target;
    s->u.int_var.slot = slot;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = on_complete;
    s->on_complete_ctx = ctx;
    return ui_anim_make_handle(slot_index, s->generation);
}

ui_anim_handle_t ui_anim_float_to(float* slot, float target,
    const ui_anim_profile_t* profile)
{
    if (!s_initialized) ui_anim_init();
    if (slot == NULL) return UI_ANIM_HANDLE_INVALID;
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_VAR;

    float end[1] = { target };
    if (!ui_anim_cfg_enabled()) {
        return ui_anim_apply_end_state_now(UI_ANIM_KIND_FLOAT_VAR,
            (void*)slot, end, 1, 0.0f, 0.0f, NULL, NULL);
    }

    int slot_index = -1;
    ui_anim_slot_t* s = NULL;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && s_slots[i].kind == UI_ANIM_KIND_FLOAT_VAR
            && s_slots[i].u.float_var.slot == slot) {
            slot_index = i;
            s = &s_slots[i];
            break;
        }
    }
    if (s == NULL) {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            return ui_anim_apply_end_state_now(UI_ANIM_KIND_FLOAT_VAR,
                (void*)slot, end, 1, 0.0f, 0.0f, NULL, NULL);
        }
        s = &s_slots[slot_index];
        s->value[0] = *slot;
        s->velocity[0] = 0.0f;
    }
    if (s->value[0] == target && s->velocity[0] == 0.0f) {
        ui_anim_clear_slot(s);
        return UI_ANIM_HANDLE_INVALID;
    }
    s->kind = UI_ANIM_KIND_FLOAT_VAR;
    s->n_scalars = 1;
    s->target[0] = target;
    s->u.float_var.slot = slot;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = NULL;
    s->on_complete_ctx = NULL;
    return ui_anim_make_handle(slot_index, s->generation);
}

void ui_anim_cancel(ui_anim_handle_t handle)
{
    ui_anim_slot_t* s = ui_anim_resolve(handle);
    if (s == NULL) return;
    ui_anim_clear_slot(s);
}

void ui_anim_cancel_for_window(tig_window_handle_t window)
{
    if (!s_initialized) return;
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (!s_slots[i].active) continue;
        bool match =
            (s_slots[i].kind == UI_ANIM_KIND_WINDOW_TRANSFORM
                && s_slots[i].u.transform.window == window)
            || (s_slots[i].kind == UI_ANIM_KIND_WINDOW_TINT_REVEAL
                && s_slots[i].u.tint_reveal.window == window);
        if (match) {
            ui_anim_clear_slot(&s_slots[i]);
        }
    }
    // Reset transform / tint_reveal so the window's compositor path
    // is back to natural 1:1 opaque + full-strength tint. Caller
    // (e.g. an inventory close) gets a clean slate for the next
    // open instead of inheriting whatever state the cancelled tween
    // last wrote.
    tig_window_transform_clear(window);
    tig_window_tint_reveal_set(window, 1.0f);
    tig_window_tint_snapshot_release(window);
}

// CE: internal — starts a tint_reveal tween toward `target` on the
// given window. Used by ui_anim_window_show_with_tint_reveal's
// on_complete to fade tint in after entrance settles, and exposable
// from intgame later if useful for other contexts.
static ui_anim_handle_t ui_anim_tint_reveal_to(tig_window_handle_t window,
    float target,
    const ui_anim_profile_t* profile)
{
    if (!s_initialized) ui_anim_init();
    if (window == TIG_WINDOW_HANDLE_INVALID) return UI_ANIM_HANDLE_INVALID;
    if (profile == NULL) profile = &UI_ANIM_PROFILE_DEFAULT_VAR;

    if (!ui_anim_cfg_enabled()) {
        tig_window_tint_reveal_set(window, target);
        return UI_ANIM_HANDLE_INVALID;
    }

    // Retarget if a tint_reveal tween already exists for this window.
    int slot_index = -1;
    ui_anim_slot_t* s = NULL;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && s_slots[i].kind == UI_ANIM_KIND_WINDOW_TINT_REVEAL
            && s_slots[i].u.tint_reveal.window == window) {
            slot_index = i;
            s = &s_slots[i];
            break;
        }
    }
    if (s == NULL) {
        slot_index = ui_anim_alloc_slot();
        if (slot_index < 0) {
            tig_window_tint_reveal_set(window, target);
            return UI_ANIM_HANDLE_INVALID;
        }
        s = &s_slots[slot_index];
        // Fresh slot: seed from the window's current tint_reveal
        // (assumed 0 if caller pre-set it before entrance, 1 if not).
        // No tig accessor for tint_reveal — pick 0 as the safe default
        // for "fade in" semantics; callers that want a different start
        // can pre-set via tig_window_tint_reveal_set first.
        s->value[0] = 0.0f;
        s->velocity[0] = 0.0f;
    }
    s->kind = UI_ANIM_KIND_WINDOW_TINT_REVEAL;
    s->n_scalars = 1;
    s->target[0] = target;
    s->u.tint_reveal.window = window;
    ui_anim_profile_to_spring(profile, &s->stiffness, &s->damping);
    s->on_complete = NULL;
    s->on_complete_ctx = NULL;
    ui_anim_apply(s);
    return ui_anim_make_handle(slot_index, s->generation);
}

static ui_anim_tint_reveal_ctx_t* ui_anim_alloc_reveal_ctx(void)
{
    for (int i = 0; i < UI_ANIM_REVEAL_CTX_POOL; i++) {
        if (s_reveal_ctx_pool[i].window == TIG_WINDOW_HANDLE_INVALID) {
            return &s_reveal_ctx_pool[i];
        }
    }
    return NULL;
}

static void ui_anim_show_tint_reveal_complete(void* ctx)
{
    ui_anim_tint_reveal_ctx_t* rctx = (ui_anim_tint_reveal_ctx_t*)ctx;
    if (rctx == NULL || rctx->window == TIG_WINDOW_HANDLE_INVALID) return;
    tig_window_handle_t window = rctx->window;
    ui_anim_profile_t profile = rctx->reveal_profile;
    rctx->window = TIG_WINDOW_HANDLE_INVALID;  // free slot
    ui_anim_tint_reveal_to(window, 1.0f, &profile);
}

ui_anim_handle_t ui_anim_window_show_with_tint_reveal(
    tig_window_handle_t window,
    ui_anim_anchor_t anchor,
    float scale_from,
    const ui_anim_profile_t* show_profile,
    int reveal_settle_ms)
{
    if (!s_initialized) ui_anim_init();
    if (window == TIG_WINDOW_HANDLE_INVALID) return UI_ANIM_HANDLE_INVALID;

    // Hide the tint immediately so the post-entrance transition to
    // "tint enabled" doesn't pop. Even if cfg disables animations,
    // we still set 1.0 below in the same code path (via the show's
    // synchronous on_complete fire).
    tig_window_tint_reveal_set(window, 0.0f);

    if (reveal_settle_ms <= 0) reveal_settle_ms = 150;

    ui_anim_tint_reveal_ctx_t* rctx = ui_anim_alloc_reveal_ctx();
    if (rctx == NULL) {
        // Pool full — degrade to a plain show without the chained
        // reveal fade. Tint will snap on at entrance settle.
        return ui_anim_window_show(window, anchor, scale_from, show_profile);
    }
    rctx->window = window;
    rctx->reveal_profile.settle_ms = reveal_settle_ms;
    rctx->reveal_profile.damping_ratio = 1.2f;

    // Reuse the standard show, but pass our on_complete chain so the
    // reveal fade fires after entrance settles. The show currently
    // doesn't take an on_complete in its public signature — use the
    // hide API's pattern indirectly by setting the on_complete on the
    // slot we just created. To do that cleanly, allocate the slot
    // via show first, then patch the on_complete fields.
    ui_anim_handle_t handle = ui_anim_window_show(window, anchor, scale_from,
        show_profile);
    if (handle == UI_ANIM_HANDLE_INVALID) {
        // cfg disabled or pool full — fire reveal sync.
        rctx->window = TIG_WINDOW_HANDLE_INVALID;
        tig_window_tint_reveal_set(window, 1.0f);
        return UI_ANIM_HANDLE_INVALID;
    }
    ui_anim_slot_t* s = ui_anim_resolve(handle);
    if (s != NULL) {
        s->on_complete = ui_anim_show_tint_reveal_complete;
        s->on_complete_ctx = rctx;
    }
    return handle;
}

void ui_anim_finish_now(ui_anim_handle_t handle)
{
    ui_anim_slot_t* s = ui_anim_resolve(handle);
    if (s == NULL) return;
    ui_anim_finalize(s);
    void (*cb)(void*) = s->on_complete;
    void* ctx = s->on_complete_ctx;
    ui_anim_clear_slot(s);
    if (cb != NULL) cb(ctx);
}

bool ui_anim_is_active(ui_anim_handle_t handle)
{
    return ui_anim_resolve(handle) != NULL;
}

// CE: called from tig_window_destroy via tig_window_destroy_notify_set
// so any tween targeting this window cancels silently (no on_complete
// fires — the caller is destroying the window directly, not via the
// tween's intended path).
void ui_anim_notify_window_destroyed(tig_window_handle_t window)
{
    if (!s_initialized) return;
    if (window == TIG_WINDOW_HANDLE_INVALID) return;
    for (int i = 0; i < UI_ANIM_POOL_SIZE; i++) {
        if (s_slots[i].active
            && (s_slots[i].kind == UI_ANIM_KIND_WINDOW_TRANSFORM
                || s_slots[i].kind == UI_ANIM_KIND_WINDOW_TINT_REVEAL
                || s_slots[i].kind == UI_ANIM_KIND_WINDOW_TINT_XFADE)
            && s_slots[i].u.transform.window == window) {
            ui_anim_clear_slot(&s_slots[i]);
        }
    }
    // tig_window_destroy already destroys the snapshot VB itself —
    // we just need to drop slot references here.
}
