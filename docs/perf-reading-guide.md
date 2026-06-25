# Arcanum-CE performance: a reader's guide

*For non-engineers. How the game measures its own speed, and how to read a perf
report at any moment. Metric definitions verified against the instrumentation
code; the `[MEASURED]` blocks are fresh F9 captures on the current build
(Slot0013 day-town, M3 Max / 120Hz).*

---

## First: the one number that matters — 8.3 milliseconds

The game runs on a 120Hz display: the screen refreshes 120 times a second.
1000 ms ÷ 120 = **~8.3 ms** — the *budget* for one frame.

Think of a conveyor belt delivering a fresh picture to your eyes every 8.3 ms.
The game must finish drawing each picture *before* the belt arrives. If it does,
motion is buttery-smooth. If a frame takes longer than 8.3 ms it misses the belt
and waits for the next slot — a tiny stutter.

> **Under 8.3 ms per frame = smooth. Over 8.3 ms = a dropped frame you can feel.**

Alarm thresholds: **30 ms** trips a "megahitch" warning; **33 ms** means the
frame fell below 30 fps (a serious stall).

---

## Turning the report ON: the F9 key

Logging is **off by default** and costs nothing when off (the stopwatch code is
skipped entirely). **Press F9 to toggle it on.** Two things:

- **The first 2 frames after F9 are ignored** ("warmup") — cold-cache frames are
  artificially slow, so they're thrown out.
- The **zoom report only prints while you're actually zoomed out**, once every 60
  frames. Press F9 but never zoom and you won't see those lines — normal.

---

## How a frame is built

Each frame runs a handful of steps; the report times them as "buckets" that
roughly sum to the whole frame. The five timed buckets, in order:

1. **tig_ping** — housekeeping (mouse, keyboard, sound, timers)
2. **key_repeat** — handling a held-down zoom key
3. **iso_redraw** — drawing the game world *(usually the biggest)*
4. **win_display** — putting the UI on top and showing the finished frame
5. **event_dispatch** — clicks, menu opens, scrolling

There's also a sixth, *untimed* chunk — the game's "thinking" (AI, scripts, world
simulation). It isn't in any bucket. It matters later.

---

## (1) Overall / framerate

| Metric | Plain meaning | Good vs bad |
|---|---|---|
| **Frame avg** | Average time per frame. Inverse of FPS: 8.3 ms ≈ 120 fps, 16.6 ms ≈ 60 fps. | Lower is better. At/under 8.3 ms is full speed. |
| **Frame max** | The single *worst* frame — the biggest hitch. | Lower is better. A big gap above the average = occasional stutter. |
| **Frame stddev** | How *consistent* frames are. The **smoothness** number. | **Low = silky. High = jittery micro-stutter.** The single best smoothness indicator. |

**Why stddev is the star:** uniformly-a-bit-slow feels smoother than
fast-fast-LAG-fast. Stddev catches the unevenness; aim for low stddev even more
than low average.

> **[MEASURED — current build]** Frame avg **~8.3 ms (~120 fps)**, stddev
> **~1.8–2.3 ms** (smooth), frame max **~17–26 ms** (rare spikes). The game is
> *vsync-bound* — it finishes early and waits for the display.

---

## (2) Iso world render — drawing the world

The world draw is bucket #3, **iso_redraw**, normally the most expensive part.
It splits into four **sub-passes**:

| Pass | What it draws | Notes |
|---|---|---|
| **light** | Lighting / shadow layer | — |
| **tile** | Ground / terrain floor | Heaviest historically — ~1000 tiles/frame. The hot spot. |
| **object** | Sprites, characters, items | Per-object color/lighting work. |
| **roof** | Roof tiles and layers | — |

Whichever pass is biggest is to blame for a heavy frame. Each shows **avg** and
**max** (a high max with low avg = occasional spike). Each should sit under 8.3 ms.

> **[MEASURED — current build]** iso_redraw **~1.7 ms** total (~20% of the frame).
> Sub-passes: tile **0.30 ms**, object **0.16 ms**, light **0.08 ms**, roof
> **0.06 ms**. The four passes are ~0.5 ms; the rest of iso_redraw is the
> downscale blit + setup. All far under budget.

---

## (3) Zooming

Zooming out renders the world then *shrinks* it to fit. Its own report prints
once every 60 zoomed frames, only while zoomed:

| Metric | Plain meaning | Good vs bad |
|---|---|---|
| **render** | Redraw the world before shrinking | Lower better; creeping toward 8.3 ms is bad. |
| **blit** | Shrink the world to the zoomed window | Expected ~0.5–1 ms. Higher = the shrink is a bottleneck. |
| **OTHER** | Leftover bookkeeping | Should be small. |
| **zoom-total** | The whole zoom draw (render+blit+OTHER), with a max | "How expensive is zooming." Max reveals worst-frame stutter. |
| **dirty %** | How much of the screen actually changed | **Lower better** — redrawing only what moved. 100% = redrawing everything. |
| **full-redraws %** | How often the *entire* screen was redrawn | **Lower better.** 0% = the smart partial-redraw always wins. |

**Nuance:** the "frame"/"ping"/pass numbers in this report are actually collected
on **every** frame while logging is on — only the 60-frame counter and the
printout wait for you to be zoomed. Treat them as recent-activity averages.

> **[MEASURED — current build]** zoom-total **~1.6–1.85 ms** avg (max ~11 ms on a
> camera-jump full re-render). dirty **1–17%**, full-redraws **5–6%**. Zoomed-out
> (z 0.5) ≈ zoomed-in (z 1.6): both land at ~8.3 ms — zooming is essentially free.

---

## (4) Scrolling

Scrolling has no headline bucket; its cost lands in the world render (§2) and
event handling (§5).

- **Busy-area scrolling is the stress test.** Zoomed far out in a populated city,
  ~4× the tiles/objects are on screen, so the tile and object passes work hardest
  — watch **tile**/**object** (§2) and **frame stddev** (§1).
- **World-map scrolling** has a "void-fade" edge effect, drawn at half resolution:
  **53% faster** (5.9 → 2.7 ms/step), visually identical.

Smooth motion + low stddev = healthy scrolling. A hitch each camera nudge → check
whether the tile or object pass is spiking.

> **[MEASURED — current build]** Day-town scroll: frame avg **~8.3 ms**, stddev
> **~2 ms**. The tile/object passes (§2) are the scroll's render cost.

---

## (5) UI / present — the interface and showing the frame

After the world is drawn, the UI goes on top and the picture is pushed to screen
(bucket #4, **win_display**, plus two low-level timers).

| Metric | Plain meaning | Good vs bad |
|---|---|---|
| **win_display** | Compositing UI over the world + presenting | A steady few ms is normal — **mostly waiting for the display's next refresh, not work.** Worry only if it grows beyond the vsync wait (the HUD-bar tint is the real CPU cost here). |
| **flip present** | The low-level "show it now" call — almost entirely the vsync wait | **~6–7 ms is normal idle waiting** = headroom. Bad only if it grows *with* scene complexity. |
| **flip update** | Copying the picture CPU→graphics card | Under 1 ms; ~0 on the GPU path. |
| **present-skip count** | Frames *skipped entirely* because nothing changed (static menu) | A tally, not a time. **High on a still screen = good** (saves battery). ~0 in gameplay is expected. |

**Key intuition:** a few ms here is the game *politely waiting* for the display,
not being slow. Idle waiting = spare capacity.

> **[MEASURED — current build]** win_display **~6.4 ms** — almost all of it the
> vsync wait (the actual UI/HUD-tint compositing is ~0.05 ms). present-skip
> (gated): **~290 frames skipped at the static main menu, ~0 in active play**.

---

## (6) General / idle — the "thinking" and the hitch alarms

**The untimed "thinking" time (gamelib_ping).** Every frame the game updates AI,
scripts, timed events, and the world. This is **not** in any of the five timed
buckets — it's in an "untimed" gap. If frames feel slow but every visible bucket
looks healthy, the cost is hiding here. Classic signature of **"lag that lets up
when you zoom"** — render is fine; the simulation is the culprit.

The zoom report exposes it as:
- **ping avg / max** — all game-logic subsystems together per frame (expected
  ~0.1 ms; a high max = a bursty subsystem like a periodic AI pass).
- **hot (top 3)** — the three most expensive subsystems, ranked. Your "which
  system to blame" list.

The alarm lines:

| Alarm | When it fires | What it tells you |
|---|---|---|
| **[megahitch]** | Any single bucket exceeds **30 ms** | Names the culprit (e.g. "iso_redraw 80 ms") + graphics-cache stats. 30 ms is ~3.6× budget, so only real hitches fire it. |
| **[slow-loop]** | A whole frame exceeds **33 ms** with no single bucket to blame | *Meant* to point at untimed thinking-time via an "UNTIMED" figure. |

> ⚠️ **Caveat about [slow-loop]:** in the current build this line **never prints —
> it is dead code.** The function that would emit it exists but is never called.
> So today, sustained-slowness frames are caught by the per-bucket **[megahitch]**
> alarm (30 ms) instead. Hunting "lag with a healthy render," you currently infer
> the untimed cost rather than read a [slow-loop] line. (Known gap, flagged for
> wiring up.)

> **[MEASURED — current build]** ping avg **~0.1–0.19 ms** (max ~1.65 ms), hottest
> subsystem **TimeEvent**; **0 megahitches** during normal play.

---

## Quick cheat-sheet

- **Press F9** to log (first 2 frames ignored).
- **8.3 ms** is the per-frame budget. Under it = smooth; over = a dropped frame.
- **Frame avg** = speed. **Frame max** = worst hitch. **Frame stddev** = smoothness (watch this one).
- **iso_redraw** splits into **light / tile / object / roof** — biggest is the bottleneck.
- A few ms in **win_display / present** is just *waiting for the display* — healthy headroom.
- **[megahitch]** = one step over 30 ms (names the culprit). **[slow-loop]** is currently **dead code — never prints**.
- Smooth render but laggy feel? Suspect the **untimed "thinking" time** (AI/scripts/sim) — check **ping** and the **hot** list.

---

**Source files (for engineers):**
- Main-loop buckets + recorders: `src/main.c` (calls at :560/574/581/594/1028) and `src/game/gamelib.c` (recorders :1685–1758; megahitch `#define` :1664; slow-loop recorder defined-but-unwired at :1781).
- Zoom-perf summary + passes: `src/game/gamelib.c` (accumulate/print :2526–2839; ping :1242–1264; passes :4081–4176).
- Flip/present timing + present-skip: `first_party/tig/src/video.c` (:1312–1477).
- Background: `docs/perf-status.md`, `docs/perf-history.md`, `docs/perf-next-steps.md`.
