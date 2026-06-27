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
- `tele <map> <x> <y>` — teleport the PC in-game (async; the channel does NOT survive this on
  pre-rework branches — prefer `newgameat`).
- `gotomap <name>` — teleport to a named map's start location (`map_list_info_find`).
- `wherepc` — log the PC's current map + tile coords (read a scene's coords from a save).
- `scrollto <x> <y>` / `scrollby <dx> <dy>` — absolute / relative camera move.
- `walkby <dx> <dy>` — walk the PC dx,dy tiles (camera follows).
- `setzoom <f>` — drive the iso zoom toward f (1.0 in, 0.5 out).

**UI**
- `inven`/`invenclose`, `logbook`/`logbookclose`, `wmap`/`wmapclose`/`wmapcap`/`wmaphalf`/`wmapscroll`.

**Render path + perf toggles** (same setters the cfg keys use — for same-launch A/B)
- `setpath <software|gpu>`, `resolveonce 0|1`, `halfreslerp 0|1`, `tilethreads 0|1`,
  `gpucachememo 0|1`, `presentskip 0|1`, `simd 0|1`.

**Instrumentation / control**
- `perf` — toggle the rich zoom-perf log (render/blit/OTHER/frame avg-max-stddev).
- `zoomlog`, `trace` — zoom + GPU-dispatch tracing.
- `capture <abs_path>` — dump the iso world buffer to a BMP (for pixel diffs).
- `wait <N>`, `quit` (clean exit).

## Launch recipe (macOS)

```sh
caffeinate -dis &                                   # display sleep wedges the GL swap
ARCANUM_GPU_CMD=/path/to/script.txt \
  "…/arcanum-ce" -window -ApplePersistenceIgnoreState YES   # avoid the window-restore hang
```

- After a `SIGKILL`, macOS window-state restoration hangs the *next* launch
  (`_reopenWindowsAsNecessaryIncludingRestorableState`). `-ApplePersistenceIgnoreState YES`
  (and `defaults write <bundle-id> ApplePersistenceIgnoreState -bool YES`) avoids it.
- Run perf measurements serially with no other load; the universal frame-timer dumps the
  `[zoom-perf]` format to `/tmp/arcanum-zoom-perf.log` (parser in the scratchpad / `tools/`).

## Roadmap

Prioritized; #1 is the highest-leverage capability, #3 turns it into a CI tool.

1. **Generalize the channel-survives-transition fix.** `newgameat` works by letting
   `sub_5412E0` pump the game loop until the spawn teleport completes. Promote that to a
   `harness_settle()` primitive (pump `teleport_ping`/load until `!teleport_is_teleporting()`)
   so in-game `tele`, map changes, and any mid-game transition keep the channel alive — this
   is what blocked the clean cross-branch *town* workout on the old engines.
2. **Determinism.** A `seed <N>` command (fix the RNG) + premade char + fixed workout =
   byte-reproducible runs = real regression detection via `capture` diffs.
3. **Bake in A/B + a perf gate.** A `bench-ab <toggle>` command that runs the same workout
   with an option off then on (same-launch, ambient-robust) and reports the delta; an
   `assert-render-under <ms>` that exit-codes non-zero on regression — now it is CI-able.
4. **Scenarios as first-class.** Promote the ad-hoc workout `.txt` files to named, versioned
   `scenario <name>` scripts under `tools/` (`town-stress`, `zoom-sweep`).
5. **Introspection + capture-diff.** Make `maplist`/`wherepc` permanent; add `capture-diff
   <a> <b>` (pixel delta) and auto-capture-on-spike.
6. **Headless software-only mode** for CI perf gates (no window/vsync, run + dump) — the
   biggest force-multiplier, gated on a GL-context-free software render path.

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
