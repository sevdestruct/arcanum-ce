# The arbiter harness

An autonomous, file-driven test/benchmark channel that drives the game from a command
script — load a scene, move/zoom/scroll, toggle a render option, capture a frame, dump
per-frame timings — so perf work and visual A/Bs are reproducible and scriptable, on any
branch, headless of a human at the keyboard.

It is the same *shape* as the multiplayer-restoration "arbiter" (a command channel that
arbitrates game state from outside the main loop); the two are intended to converge (see
[Merging with the multiplayer arbiter](#merging-with-the-multiplayer-arbiter)).

## Build flag — keep it, expand it, don't ship it

The harness is gated behind a CMake option, **`ARCANUM_HARNESS`, default `OFF`**:

```sh
cmake --preset macos-arm64 -DARCANUM_HARNESS=ON      # dev / CI build: harness compiled in
cmake --preset macos-arm64                            # ship build: harness compiled OUT entirely
```

When `OFF`, every harness surface is `#if defined(ARCANUM_HARNESS)`'d out — zero bytes in
the binary, no env-activatable test channel in a public release, nothing to strip. When
`ON`, the full channel + instrumentation is present. The code lives in the source tree
either way, so it is never stripped and is always editable.

This is why the harness can be **merged into `ui-improvements` along with the perf work**
and still be safe to release: it rides along in the source, off by default. When
multiplayer later rebases onto `ui-improvements`, the harness is already there for MP
testing too — just flip the flag.

> The perf *optimizations themselves* (present-skip, resolve-once, cache size, …) are NOT
> harness — they ship via their `arcanum.cfg` keys. Only the runtime gpu-cmd *toggles* for
> them live in the harness channel; the setter functions they call stay compiled in.

## Where the pieces live

| Piece | File | Gated how |
| --- | --- | --- |
| Frame-timer (universal per-frame perf dump) | `src/game/harness.c` / `harness.h` | whole file body `#if ARCANUM_HARNESS`; main loop calls 3 hooks, also gated |
| Command channel (`gpu_test_channel_tick` + all commands) | `src/game/gamelib.c` | function + its `gamelib_ping` call gated in place |
| New-game spawn override (`mainmenu_ui_harness_newgame[_at]`, `g_harness_ng_*`, the `sub_5412E0` redirect) | `src/ui/mainmenu_ui.c` / `mainmenu_ui.h` | functions, globals, and the override block gated in place |
| Rich zoom-perf instrument (`gamelib_zoom_perf_*`; F9 key + `perf` cmd toggle it) | `src/game/gamelib.c`, `src/main.c` | `gamelib_zoom_perf_toggle()` body + the F9 case gated; accumulators stay compiled but **inert** (the flag never goes true, so every `is_enabled()` consumer short-circuits) |

The frame-timer is extracted to `harness.c` (it is self-contained). The channel and the
new-game override are **gated in place** because they touch file-private statics (`settings`,
the mainmenu pregen state). Fully moving them to `harness.c` is a clean follow-up — it just
needs a handful of accessor functions exposed first (see [Roadmap](#roadmap), phase 2).

The **rich zoom-perf instrument** (`gamelib_zoom_perf_*` — the detailed render/blit/OTHER/
frame-stats dump, toggled by **F9** in-game and by the `perf` gpu-cmd) is gated at its
*activation*: with `ARCANUM_HARNESS` off, `gamelib_zoom_perf_toggle()` is a no-op and the F9
case is empty, so the flag never goes true and every `is_enabled()`-gated accumulator (the
draw path, the main loop, `map.c`, `wmap_ui.c`) stays dormant. The accumulator code itself
stays compiled — it is interleaved with the hot render path, so leaving it inert is cheaper
and safer than threading `#if`s through every loop; a full compile-out is a phase-2 item.
Because the rich instrument and the universal frame-timer both emit the same `[zoom-perf]`
log, the frame-timer **stands itself down whenever the rich instrument is active**, so the
two never double-write (this also fixes the mixed-emitter ambiguity in the cross-branch
sweeps, where current-branch runs had both live).

## Command reference

The channel reads `ARCANUM_GPU_CMD` (a file path), one command per line, pumped every frame
from `gamelib_ping` (menu AND in-game). `wait N` pauses N frames; `# …` is a comment.

**Scene entry**
- `newgame [idx]` — start a new game with premade char `idx` (default 1 = Merwin); lands on
  the module start map. Save-format independent.
- `newgameat <map> <x> <y>` — same, but spawn at `map`+`(x,y)` via the *synchronous*
  menu→game transition. Use this to reach a heavy scene (e.g. `newgameat 1 90234 84162` =
  Shrouded Hills town) — it is channel-safe on every branch, unlike an in-game `tele`.
- `loadsave <slot>` — load a save (auto-switches to the save's module). Slot is the bare
  slot name, e.g. `Slot0015`. New-format saves only load on post-rework branches.
- `setmodule <name>` — force the active module.
- `dlgclose` — dismiss the intro dialogue (base Arcanum opens in one).

**Navigation / camera**
- `tele <map> <x> <y>` — teleport the PC in-game. Calls `harness_settle()` after `teleport_do`,
  so the transition (incl. a map change) completes before the next command runs — the channel
  now survives an in-game teleport (see Roadmap #1). `newgameat` is still the way to reach a
  heavy scene with no prior in-game state.
- `gotomap <name>` — teleport to a named map's start location (`map_list_info_find`); also
  settles before continuing.
- `wherepc` — log the PC's current map + tile coords (read a scene's coords from a save).
- `scrollto <x> <y>` / `scrollby <dx> <dy>` — absolute / relative camera move.
- `walkby <dx> <dy>` — walk the PC dx,dy tiles (camera follows).
- `setzoom <f>` — drive the iso zoom toward f (1.0 in, 0.5 out).

**UI**
- `inven`/`invenclose`, `logbook`/`logbookclose`, `wmap`/`wmapclose`/`wmapcap`/`wmaphalf`/`wmapscroll`.

**Render path + perf toggles** (same setters the cfg keys use — for same-launch A/B)
- `setpath <software|gpu>`, `resolveonce 0|1`, `halfreslerp 0|1`, `tilethreads 0|1`,
  `gpucachememo 0|1`, `presentskip 0|1`, `simd 0|1`.

**Determinism**
- `seed <N>` — fix the engine RNG (`random_seed`, the only RNG source). Issue it *before*
  `newgame` so the scene spawn's RNG draws are fixed too.
- `fixeddt <ms>` — advance game/animation time by a fixed `ms` per frame instead of
  wall-clock (0 = off). Removes time-of-day lighting + animation drift between runs.
- Together these make a captured scene ~99.95% byte-reproducible. They do *not* yield a
  byte-identical capture: scene-entry transitions pump a wall-clock-bounded number of
  frames, so one in-place ambient animation's phase still drifts (measured floor ≈ 1.6 KB /
  ~550 px of a 1280×804 frame). Treat `capture` regression checks as **tolerance-based**,
  not exact-match — a real rendering regression dwarfs that floor.

**CI gate**
- `bench-ab <toggle> [frames]` — same-launch A/B of a perf toggle (`simd`, `presentskip`,
  `resolveonce`, `halfreslerp`, `tilethreads`, `gpucachememo`): measures mean full-redraw
  render time with the option OFF vs ON, interleaved over 3 rounds of `frames` (default 120)
  so thermal/ordering drift hits both arms equally, and logs the delta + %.
- `assert-render-under <ms> [frames]` — measure mean full-redraw render time over `frames`
  (default 120); if it is ≥ `ms`, log FAIL to stderr and `exit(1)`. Turns a workout into a
  headless perf gate (non-zero exit on regression).

**Instrumentation / control**
- `perf` — toggle the rich zoom-perf log (render/blit/OTHER/frame avg-max-stddev).
- `zoomlog`, `trace` — zoom + GPU-dispatch tracing.
- `capture <abs_path>` — dump the iso world buffer to a BMP (for pixel diffs).
- `spikecap <ms> [max]` — auto-capture-on-spike: dump the iso buffer to
  `/tmp/arcanum-spike-<n>.bmp` whenever a frame's render time reaches `ms`, up to `max`
  (default 8) captures, then stop. `spikecap 0` disables. Catches the offending frame in a
  long run without a human watching.
- `wait <N>`, `quit` — clean exit. `quit` pre-confirms the "Are you sure you want to quit?"
  modal (via `harness_request_quit()`), so the process exits on its own instead of hanging on
  a prompt the channel can't drive. No `SIGKILL` needed to end a scripted run.

## Launch recipe (macOS)

```sh
caffeinate -dis &                                   # display sleep wedges the GL swap
ARCANUM_GPU_CMD=/path/to/script.txt \
  "…/arcanum-ce" -window -ApplePersistenceIgnoreState YES   # avoid the window-restore hang
```

- End a run with the `quit` command — it exits cleanly (see Command reference), so the
  `SIGKILL` workaround below is only needed when a run is aborted mid-script.
- After a `SIGKILL`, macOS window-state restoration hangs the *next* launch
  (`_reopenWindowsAsNecessaryIncludingRestorableState`). `-ApplePersistenceIgnoreState YES`
  (and `defaults write <bundle-id> ApplePersistenceIgnoreState -bool YES`) avoids it.
- Run perf measurements serially with no other load; the universal frame-timer dumps the
  `[zoom-perf]` format to `/tmp/arcanum-zoom-perf.log` (parser in the scratchpad / `tools/`).

## Scenarios & tooling

Named, versioned scenarios live as command scripts under `tools/scenarios/*.txt` and run
through `tools/arbiter.sh`:

```sh
tools/arbiter.sh --list                 # list scenarios
tools/arbiter.sh town-stress            # run one; exit code is the scenario's
tools/arbiter.sh zoom-sweep --app "Arcanum Community Edition (Perf GPU Accel)"
```

`arbiter.sh` handles `caffeinate`, kills a stale instance, runs the build foreground from the
data root, and **propagates the scenario's exit code** — so an `assert-render-under` failure
(or a crash) fails the script, making it a drop-in headless CI gate. Needs a
`-DARCANUM_HARNESS=ON` build deployed (override the app with `--app`/`APP`).

- `town-stress` — heavy Shrouded Hills town: move + scroll + zoom-sweep, then
  `assert-render-under 16.0` as the gate. (Verified PASS at ~2.9 ms avg.)
- `zoom-sweep` — `bench-ab resolveonce` at z=1.0 and z=0.5 (the z=0.5 full-redraw path is
  ~3× heavier: ~10 ms vs ~3 ms). It A/Bs `resolveonce`, **not** `tilethreads` — the 2-thread
  tile pass ships default-OFF because it SIGSEGVs on a cold art cache
  (`tile_rows_thread_fn → art_blit`), which a heavy town + forced full redraws reliably trips.

Capture-diff is `tools/gpu_test/diff_bmp.py` (now tolerance-capable):
```sh
python3 tools/gpu_test/diff_bmp.py a.bmp b.bmp --tolerance-delta 16 --tolerance-px 2000
```
Defaults (0/0) keep the original strict pixel-identical check; the tolerance flags gate above
the determinism noise floor (the measured floor is a single 64×32 animated sprite,
max channel delta ≈13 — `--tolerance-delta 16` clears it to 0 differing px).

## Roadmap

Prioritized; #1 is the highest-leverage capability, #3 turns it into a CI tool.

1. **Generalize the channel-survives-transition fix. — DONE.** `harness_settle(int
   timeout_ms)` (`harness.c`) now pumps real game frames (`tig_ping` / `gamelib_ping` /
   `iso_redraw` / `tig_window_display`) until the in-flight PC teleport is carried out, then
   returns. The `tele` and `gotomap` channel commands call it right after `teleport_do`, so
   an in-game teleport — *including a map change* (`map_open_in_game`) — completes before the
   channel reads the next command. Two implementation notes on the original sketch:
   - The poll signal is **`teleport_is_pending()`** (a new accessor), NOT
     `teleport_is_teleporting()`. The latter only reflects the momentary `teleport_processing`
     flag (true *during* `teleport_process`, which runs inside `teleport_ping`), so an
     out-of-loop poller never observes it true. `teleport_pending` is the real "requested but
     not yet carried out" gate.
   - The bug it fixes is intra-frame **ordering**: the channel reads all its commands at the
     top of `gamelib_ping`, *before* the per-module `teleport_ping` that performs a requested
     teleport. Without settling, a `wherepc`/`capture` right after `tele` ran against the
     pre-teleport map. A `harness_is_pumping()` guard makes the nested `gamelib_ping` inside
     the pump skip the channel so it can't re-consume commands. Verified: post-`tele`
     `wherepc` reports the destination; `gotomap ShopMap` lands the channel on map 14.
2. **Determinism. — DONE (near-deterministic).** `seed <N>` fixes the engine LCG;
   `fixeddt <ms>` advances game/animation time by a fixed per-frame delta instead of
   wall-clock (hooked in `timeevent_ping`). Together they make a captured scene ~99.95%
   byte-reproducible. They do NOT give byte-identical captures: scene-entry transitions pump
   a wall-clock-bounded number of frames, so one in-place ambient animation's phase still
   drifts (measured floor ≈ 1.6 KB / ~550 px; `seed`+`fixeddt` cut the diff from 1908→1647
   bytes vs `seed` alone). The remaining gap would need frame-deterministic scene entry —
   not worth it. **Consequence for #5: `capture-diff` must be tolerance-based, not
   exact-match** — a real regression dwarfs the animation floor.
3. **Bake in A/B + a perf gate. — DONE.** `bench-ab <toggle> [frames]` measures mean
   full-redraw render time OFF vs ON, interleaved over 3 rounds so thermal/ordering drift
   hits both arms (a single off→on lies on sub-10% deltas — see `software_render_findings`).
   `assert-render-under <ms> [frames]` `exit(1)`s on regression. Both reuse a new
   `harness_measure_render_ms(frames)` pump (forces a full redraw per frame for a stable,
   representative number). Verified: assert PASS→exit 0, FAIL→exit 1 (before `quit`); bench-ab
   reports per-toggle deltas.
4. **Scenarios as first-class. — DONE.** Versioned `tools/scenarios/*.txt` (`town-stress`,
   `zoom-sweep`) run via `tools/arbiter.sh <name>`, which propagates the scenario's exit code.
   See [Scenarios & tooling](#scenarios--tooling).
5. **Introspection + capture-diff. — DONE (capture side).** `tools/gpu_test/diff_bmp.py` is
   now tolerance-capable (`--tolerance-delta`/`--tolerance-px`, default 0 = strict) and
   `spikecap <ms> [max]` does auto-capture-on-spike. `wherepc` already exists; a permanent
   `maplist` is still open.
6. **Headless software-only mode** for CI perf gates (no window/vsync, run + dump) — the
   biggest force-multiplier, gated on a GL-context-free software render path. **Next.**

### Phase 2 — finish the extraction
Move the command channel and the new-game override fully into `harness.c` by first exposing
small accessors for the statics they touch: a render-path setter (wraps the `settings`
static for `setpath`), and the pregen-spawn entry (already public via
`mainmenu_ui_harness_newgame_at`). Then `gamelib.c`/`mainmenu_ui.c` keep only the gated
one-line hooks, and all harness logic lives in one file — easier to expand and to diff.

## Merging with the multiplayer arbiter

The MP-restoration branch has its own command-channel "arbiter". Both are "drive the game
from a command file" pumps; unify them in `harness.c` as **one pump that registers two
command sets** (perf/test commands here, MP-state commands there). Do this *after* the perf
work lands and multiplayer rebases onto `ui-improvements`, so the two histories don't
collapse prematurely. Until then, both can coexist behind `ARCANUM_HARNESS` on their
respective branches; the merge is a command-set union, not a rewrite.
