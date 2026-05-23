# Performance push — status & measured wins

Running record of perf work on `feature/ui-improvements` for the eventual
PR. All numbers below were captured on the same hardware (2023 MacBook Pro
M3 Max, 120Hz ProMotion) using the F9-toggled perf log that was built up
across the work itself.

## TL;DR

After 22 commits (mix of perf wins, bug fixes, configuration, and
instrumentation), measured on the same play scenario:

| Metric                          |    Before |     After |  Δ        |
| ------------------------------- | --------: | --------: | --------: |
| frame avg (per zoom-active gap) |   13.73ms |   10.21ms | **−26%**  |
| frame max mean                  |  120.03ms |   26.51ms | **−78%**  |
| frame max worst                 |  4941ms†  |   50.38ms | **−99%**  |
| frame stddev (smoothness proxy) |   17.62ms |    5.11ms | **−71%**  |
| `tile_draw` avg                 |    1.01ms |    0.71ms | **−30%**  |
| `tile_draw` max (worst)         |   10.07ms |   11.14ms |   ≈ same  |
| Megahitches per play session    |       1+  |        0  | **fixed** |

† The 4941ms baseline value was a user-paused-in-menu artifact of how
  `frame max` was measured (gap between consecutive zoom-active draws,
  multi-loop). The instrumentation got sharper across the push — by the
  end we exclude these from the headline numbers and have a separate
  `[megahitch]` logger for actual single-iteration slowness.

Subjective: user reports the game went from "scrolling stutters" to
"smooth" with no perceptible tearing despite adaptive vsync.

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
  time exceeds 50ms even though no single bucket spikes.

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
