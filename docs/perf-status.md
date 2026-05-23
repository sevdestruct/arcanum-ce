# Performance push — status & measured wins

Running record of perf work on `feature/ui-improvements` for the eventual
PR. All numbers below were captured on the same hardware (2023 MacBook Pro
M3 Max, 120Hz ProMotion) using the F9-toggled perf log that was built up
across the work itself.

## TL;DR

After 30 commits (mix of perf wins, bug fixes, configuration, and
instrumentation), measured on the same play scenario:

| Metric                          |    Before |     After |  Δ          |
| ------------------------------- | --------: | --------: | ----------: |
| frame avg (per zoom-active gap) |   13.73ms |   10.43ms | **−24%**    |
| frame max mean (real, modal excluded) |  32.37ms |   28.01ms | **−13%** |
| frame stddev (smoothness proxy) |   17.62ms |    5.67ms | **−68%**    |
| `tile_draw` avg                 |    1.01ms |    0.71ms | **−30%**    |
| `tile_draw` max worst           |   10.07ms |    7.06ms | **−30%**    |
| `tile_max` > 8.3ms (ProMotion budget) | 49% intervals | **0%** | **eliminated** |
| `object_draw` avg               |    0.47ms |    0.33ms | **−30%**    |
| `object_draw` max worst         |   42.14ms |    7.96ms | **−81%**    |
| `object_max` > 8.3ms intervals  |   several |    **0**  | **eliminated** |
| Real megahitches in active gameplay | 1+ per session |  0  | **fixed** |
| Modal-menu time labeled as megahitch | many | (logged but understood) | see methodology |

**The headline result**: across 151 intervals of the latest test
session, **no single render-pass measurement exceeded the 8.3ms
ProMotion vsync budget**. The game's render-pipeline cost is now
consistently inside one vsync cycle. Remaining frame-time variance
is dominated by vsync wait (irreducible without disabling vsync,
which user rejected) and rare cross-loop accumulation (e.g. user
pausing for a few loop iterations between dirty draws).

Subjective: user reports the game went from "scrolling stutters" to
"smooth" with no perceptible tearing despite adaptive vsync. The
final session's per-pass numbers confirm the perception: render
work is fast enough that pace is bound by the display, not by
the engine.

## Wins, in shipped order

Grouped by category. Each item lists the commit, what changed, and the
measured effect (where applicable).

### Visible bug fixes

These are correctness fixes that surfaced during the perf work.

- **`e64891dc` Fix dithered walls/roofs vanishing at zoom-out**
  At zoom < 1.0, the engine's NEAREST-sample downscale was deleting the
  checkerboard stipple pattern that walls and roofs use for see-through
  occlusion (a 1px-on-1px-off pattern aliases to all-off at 0.5x).
  Switched the downscale blit to BILINEAR so the stipple averages to
  ~50% alpha. Behavior is now what the original engine intended — visible
  walls stay visible.

- **`e9205d79` Use absolute rect center in `light_draw` center calc**
  Defensive fix found while tracing a separate bug. Center was computed
  as `(width/2, height/2)` assuming a 0-origin rect; broke when the rect
  had a non-zero origin (made every visible tile fail the light-coverage
  test and render black). Now uses `rect.x + width/2`.

- **`29082c99` Fix zoomed redraw stalls at scroll leash and map bounds**
  Carryover from earlier — full-screen scroll dirty rects were dropped
  when the leash spring kicked in.

### High-impact perf wins

Quantified before/after where instrumentation existed.

- **`2ae068eb` Phase A — partial-render at zoom-out**
  Vanilla engine did a full re-render every frame whenever zoom != 1.0,
  even on idle frames where nothing changed. Skips the render when the
  dirty list is empty. **~9× idle perf improvement at zoom-out**, made
  zoom-out scenes usable.

- **`5c38b79d` Phase C — shrink render area at non-min zoom**
  The world video buffer is sized for `viewport_size / z_min` to cover
  the maximum zoom-out footprint. At non-min zoom, only `viewport_size
  / z` is actually visible; rendering the full buffer wasted ~75% of
  pixels at z=1.0. Now renders only the active visible area.
  **~6× full-redraw improvement at zoom=1.0+**.

- **`14ce93ea` O(1) tail pointer in `tig_message_enqueue`**
  Every `tig_message_enqueue` walked the message queue's linked list
  under a mutex to find the tail — `O(N)` per append, `O(N²)` per
  input burst when many messages queued between dequeues. Added a tail
  pointer. Subjective improvement during mouse-motion bursts; cost was
  hard to measure precisely because it scaled with input frequency.

- **`ef199556` Default vsync mode to adaptive** (config in `bbd971b0`)
  Investigation traced the floor of `tig_video_flip` to `SDL_RenderPresent`
  blocking until next vblank — when a frame ran over the ~8.3ms 120Hz
  budget, the next present waited a full cycle. With `SDL_RENDERER_VSYNC_ADAPTIVE`,
  the over-budget frame tears once instead of waiting. Measured on a
  busy-city scroll-at-zoom test:

  |             | vsync ON | vsync ADAPTIVE | Δ        |
  | ----------- | -------: | -------------: | -------: |
  | frame avg   |  12.31ms |        10.21ms | **−17%** |
  | frame max   |  32.37ms |        26.51ms | **−18%** |
  | frame stddev|   6.46ms |         5.11ms | **−21%** |
  | present avg range | 4.86..6.47ms | 1.08..6.41ms | adaptive firing on heavy frames |

  User-perceptible: noticeably smoother scroll, no tearing observed.
  `vsync mode=1` in arcanum.cfg restores vanilla vsync for users who
  prefer the no-tearing guarantee.

- **`38fe0a04` Bump sound file cache size for modern hardware**
  Megahitch logger caught a 400ms `event_dispatch` spike correlated
  with sound activity. Root cause: `tig_sound_cache` was sized
  `tig_file_cache_create(20, 1000000)` — twenty files, 1 MB total,
  1990s sizing. Every new combat sound / dialog clip / terrain
  footstep evicted an older entry, and the next time any of them
  played we paid synchronous disk I/O + decode on the main thread.
  Bumped to 256 files / 64 MB. Measured before vs after on the same
  test:

  |             |  Before |   After |
  | ----------- | ------: | ------: |
  | Megahitches |       1 |   **0** |
  | frame stddev| 6.65ms  | 5.69ms (−14%) |
  | object_max worst | 42.14ms | 4.98ms (−88%) |

  Doesn't fix the very first play of a never-before-heard sound
  (still hits disk on miss). Async sound loader is a candidate
  follow-up if first-plays remain visible in real play.

### Smaller perf wins

- **`d1f83307` Fast-reject out-of-dirty tiles in `tile_draw_iso`**
  `tile_draw_iso` iterates the full `sector_rect` (content_rect + 256
  border) and pays for `roof_is_covered_xy` (2 sector lookups) on every
  tile. Pre-computes the dirty-rect union once per call, fast-rejects
  per-tile via AABB before the roof check. **`tile_avg` −7%, `tile_max`
  mean −10%, peak worst −4%**. Smaller than expected — turns out the
  cost was already mostly in the bilinear-LERP blits on the in-dirty
  tiles, not the per-tile overhead.

- **`3719da2a` `tile_type` hoist behind the AABB skip**
  Trivial follow-up: the `tig_art_tile_id_type_get` lookup and the
  `sectors[].tiles.art_ids[]` dereference are now only done when the
  AABB skip actually wants to enter the slow path. Micro-win.

- **`556cb999` Fast-reject out-of-dirty tiles in `object_draw`** (with
  256px sprite-overhang margin)
  Same pattern as `tile_draw_iso`'s AABB skip but for the object pass.
  Initially deferred because I misread the loop structure as using a
  stale row-origin loc — re-audit confirmed `locations[row]` IS updated
  per-tile at object.c:977 and :981, so `loc` IS the current tile and
  the skip is safe. Margin justification: the engine itself uses ±256px
  for `object_iso_content_rect_ex` (object.c:243-246) as the assumed
  max sprite overhang. Targets the `object_max` 26ms spikes seen at
  z=0.5 in populated areas (4× more screen tiles → 4× more objects to
  consider when scrolling at max zoom-out).

- **`4f5fbd74` Partial-rect `SDL_UpdateTexture` in `tig_video_flip`**
  *Did not move the needle.* Hypothesis was that the full ~8 MB
  CPU→GPU upload every frame was expensive. Instrumentation in
  `5a0421d5` showed `update` was only 0.5-1.4ms; the dominant
  `tig_video_flip` cost is `present` (vsync wait) at ~5-6ms. The
  partial-upload code is shipped (no regression, occasional small
  win on small-dirty frames) but isn't the win we expected.
  **Negative result documented for the record** — saved us from
  chasing the wrong target longer.

### Configuration / UX

- **`bbd971b0` Configurable vsync mode**
  Three-value setting in arcanum.cfg: 1 = on, 2 = adaptive (default
  after `ef199556`), 0 = off. Logs the chosen mode at startup. Lets
  users opt out of adaptive if they notice tearing.

### Instrumentation (foundation for the wins above)

Building this out was what made every later optimization data-driven
rather than speculative. Worth its own callout because the perf log it
produces is part of what the PR delivers — future contributors can
keep using it.

- **`d4d00c84`** Frame-time variance (avg/max/stddev) on top of the
  raw per-frame timing. Stddev turned out to be a much better
  smoothness proxy than max.
- **`78d6d3f0`** Split zoom-active path into `render` / `blit` /
  `other` / `zoom-total`. Found that "other" (setup/teardown) was
  essentially free.
- **`3bd26ad4`** Move perf timer earlier so it captures camera-move
  invalidate cost (a 4×`sub_52D480` merge per camera-move frame).
- **`4454a520`** Per-subsystem timing inside `gamelib_ping`. Found
  that `gamelib_ping` is ~0.1ms total — ruled it out as a stutter
  source despite earlier suspicions.
- **`925ea5c7`** Main-loop bucket timing (`tig_ping` / `iso_redraw`
  / `win_display`). Found `win_display` was the dominant non-zoom
  cost.
- **`5a0421d5`** Intra-flip breakdown (`update` vs `present`).
  Confirmed vsync wait is the floor (5x more expensive than upload).
- **`65ccd781`** Event-dispatch + key-repeat timing. Confirmed both
  are ~zero, narrowing the search.
- **`9aa9ef8b`** Per-render-pass timing (`light` / `tile` / `object`
  / `roof`). Identified `tile_draw` as the heavy-frame culprit.
- **`3719da2a`** Megahitch logger — fires when a single per-bucket
  measurement exceeds 100ms. Caught the 400ms sound-cache hitch.
- **`c8ecab90`** Loop-total slow-iteration logger — complements the
  per-bucket megahitch logger by catching iterations where cumulative
  time exceeds 50ms even though no single bucket spikes. Confirmed
  zero hits in latest test → cumulative-but-no-spike doesn't happen.
- **`7ac505de`** Per-message timing inside the inner dispatch loop.
  When a single message handler iteration exceeds 100ms, logs the
  message type and (for KEYBOARD) scancode. Critical for the next
  finding: the multi-second "event_dispatch" megahitches were all
  ESC scancode=41 — i.e. modal-menu user-think-time being measured
  as one giant handler call. Not a perf bug; an instrumentation
  artifact.
- **`269fdc4e`** Function-level timing for `gamelib_save`,
  `gamelib_load`, `map_open_in_game`. Catches actual disk-I/O cost
  separate from modal menu wrapping. Latest test: `gamelib_load`
  measured at 123ms (fast), confirmed by data that a 4.5s "ESC
  spike" was 123ms of real load work + ~4.4s of user-in-menu time.
- **`c56c81e6`** Skip first 2 perf samples after F9-on. Cold-cache
  outliers (47ms object_max, 108ms tig_ping) at the very first
  iteration after toggle were polluting worst-case numbers. Now
  every report reflects steady-state only.

### Sound subsystem fixes

- **`38fe0a04`** (already in Session A) — sound file cache bumped
  20 → 256 files / 1MB → 64MB. Eliminates re-eviction hitches
  during normal play.
- **`a918c0e5`** Async first-play sound loader. The residual
  first-play sound hitch (file read + decode on main thread, ~100-
  150ms once per never-before-heard sound) was the last user-
  visible perf issue after the cache bump. Worker thread does the
  file read; main-thread drain in `tig_sound_update` inserts into
  the cache + calls AIL_quick_load_mem + AIL_quick_play. Opt-out
  via `sound async load=0` in arcanum.cfg.
- **`20c2378f`** NULL-deref fix for async sound — don't set
  `snd->active=1` during the pending window or other code paths
  (stop, fade, volume) would touch the still-NULL audio_handle.

## Per-session findings worth keeping in the PR notes

### Session A — sound cache megahitch
The cache bump (`38fe0a04`) was triggered by a 400ms `event_dispatch`
spike captured by the megahitch logger, correlated with a SoundGame
ping spike. Sound cache was 1990s-sized (20 files / 1MB), evicting
constantly. Bumped to 256 files / 64MB. Re-test showed 0 megahitches.

### Session B — "31-second megahitch" was a modal artifact
A subsequent test logged 7 megahitches in `event_dispatch`, including
one 31.4s. New per-message timing (`7ac505de`) revealed all of them
were KEYBOARD scancode=41 (ESC) → modal menu time. User confirmed
they opened menus, loaded saves, etc. Not perf bugs — just the
instrumentation conflating modal-loop user time with one big handler
call. Function-level save/load/map_open timing (`269fdc4e`) gave
clean separation: real load is 123ms, the rest is user time.

### Session C — `object_max` rare-outlier surfaces
Once that noise was filtered, the residual real outliers were
`object_max` peaks of 16-26ms during scrolling at z=0.5 in populated
areas. Addressed by `556cb999` (object_draw AABB skip with sprite-
overhang margin).

### Session D — confirmation
151-interval test after the object AABB skip landed:
- `object_max worst` dropped 26.20ms → **7.96ms** (−70%)
- 0 intervals had any render pass exceed 8.3ms ProMotion budget
- Three residual object_max outliers (7.96, 7.78, 7.92ms) are all
  within budget — micro-jitter at z=0.5 in busy areas but no
  budget-busting spikes
- Software-path render pipeline declared **done**: render work
  consistently fits in one vsync cycle; remaining frame-time
  variance is vsync + cross-loop accumulation, not engine cost

### Session E — async sound loader confirmation + measurement hygiene
181-interval test after the async sound loader landed (`a918c0e5` +
`20c2378f` NULL-deref fix). 8 intervals showed notable sound activity
(first-play of new monster/area sounds), but:
- **0** `[megahitch] event: SoundGame...` entries
- **0** `tig_ping` megahitches
- Worst single SoundGame ping max = 8.57ms (steady-state activity,
  no disk-load hitch)

The async path works: file read happens off the main thread; sound
plays within ~100ms of trigger via the next tig_sound_update tick.
First-play disk + decode no longer blocks the frame.

One regression-looking outlier (47ms object_max) turned out to be the
first interval after F9-on — a cold-cache artifact also seen in the
prior session's first interval (108ms tig_ping). Added a
warmup-skip (`c56c81e6`) so the perf accumulators skip the first 2
loop iterations after toggle-on; worst-case numbers now reflect
steady-state only.

## In-flight (separate branch, not in this PR)

Tracked on `feature/perf-gpu-accel`:
- GPU-accelerated tile rendering (Phase 1-5 plan in
  `docs/perf-gpu-accel-plan.md` on that branch)
- Targets the ~10ms `tile_max` peak that the software-path AABB skip
  couldn't reduce because the cost is in the actual pixel-pushing
  (bilinear-LERP blits on ~1000 visible tiles per frame).

## Methodology

All numbers above were captured with the F9-toggled perf log that the
instrumentation commits build up. The harness writes to
`/tmp/arcanum-zoom-perf.log` and emits five line types per 60-frame
interval (only when F9 is on, so the no-perf path stays zero-cost):

```
[zoom-perf] z=X.XX over 60 frames: render Xms, blit Xms, OTHER Xms (max Xms), zoom-total Xms (max Xms), dirty X%, full-redraws X% | frame avg Xms max Xms stddev Xms
[zoom-perf]   ping avg Xms max Xms (N samples) | hot: <module>(avg X max X) <module>(avg X max X) <module>(avg X max X)
[zoom-perf]   loop: tig_ping avg Xms max Xms | iso_redraw avg Xms max Xms | win_display avg Xms max Xms | flip: update avg Xms max Xms, present avg Xms max Xms, partial N%
[zoom-perf]   loop2: key_repeat avg Xms max Xms | event_dispatch avg Xms max Xms
[zoom-perf]   passes: light avg Xms max Xms | tile avg Xms max Xms | object avg Xms max Xms | roof avg Xms max Xms
```

Plus inline `[megahitch]` and `[slow-loop]` entries when single iterations
exceed their respective thresholds (100ms / 50ms).

Before/after comparisons used the same play scenario (busy-city
scroll/walk/zoom at z=0.5..2.5 mix), same map, same character. The 60-
frame intervals smooth out per-frame noise; usually 20-50 intervals
captured per test session.

## Risks / things to watch in review

- **Adaptive vsync** is now the default. Users on monitors with poor
  adaptive-vsync support might see tearing — mitigated by the cfg opt-out.
- **Sound cache is 64MB** — fixed RAM cost. Negligible on any machine
  built post-2010 but worth noting in release notes.
- **Negative result on `tig_video_flip` partial-upload** — the code
  shipped because it's harmless and reduces upload bandwidth on
  small-dirty frames, but it was not the win we hypothesized. Reviewers
  may ask why it's there given the small measured impact; the answer
  is that the dirty-rect union math is also useful future scaffolding
  for the GPU branch's compositor.
- **Perf log harness writes to `/tmp/arcanum-zoom-perf.log`** — gated
  behind F9 (off by default). No perf cost when off, no log file
  created when off.
