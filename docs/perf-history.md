# Arcanum-CE — full historical performance comparison

Cross-branch perf progression from upstream baseline through the current
`feature/perf-gpu-accel` head. Hardware: 2023 MacBook Pro M3 Max, 120 Hz
ProMotion (8.3 ms vsync budget). Deployed variants live under
`~/Applications/Arcanum/`.

> **Reading caveat (honesty first).** Stages 0→1 are *frame-avg / per-pass*
> numbers on the "busy-city scroll/walk/zoom" scenario (the F9 perf log, see
> `perf-status.md`). Stages 2→3 are *tile-pass / world-pass* numbers from the
> GPU-branch harness on Slot0013 day-town. The two eras use different headline
> metrics and slightly different scenarios, so the **per-stage Δ is measured and
> trustworthy**, but you cannot subtract a Stage-3 number from a Stage-0 number
> and call it a single continuous metric. Where a clean cross-stage absolute
> would require it, the older un-instrumented builds (Vanilla/Upstream/Zoom-Perf)
> predate the gpucmd+perf harness, so a fresh same-scenario sweep across them
> isn't possible without back-porting the harness.

## The progression at a glance

| Stage | Branch / build | Path | What it added | Measured effect |
| --- | --- | --- | --- | --- |
| **0 — Baseline** | `upstream/main` · Vanilla | software | Original engine | Full re-render every frame at zoom≠1; 1 MB / 20-file sound cache; hard vsync; scalar blits. Subjective: "scrolling stutters." Frame avg **13.7 ms**, tile_draw **1.01 ms**, stddev **17.6 ms**, object_max worst **42 ms**, **49 %** of intervals over the 8.3 ms budget. |
| **1 — Software opts** | `zoom-perf-experiments` → `ui-improvements` | software | Partial-render at zoom-out (9× idle), shrink-render-area (6× full-redraw), AABB fast-reject (tile + object), adaptive vsync, 64 MB sound cache, async sound loader, O(1) message queue, bilinear stipple fix | Frame avg **−24 %** (13.7→**10.4 ms**); tile_draw **−30 %** (1.01→**0.71 ms**); stddev **−68 %** (17.6→**5.7 ms**); object_max **−81 %** (42→**7.96 ms**); megahitches **→ 0**; **0 %** of passes over budget (was 49 %). Software pipeline declared "fits in one vsync." |
| **2 — GPU present path** | `perf-gpu-accel` (steps 1-6 + gpu-ui) | hardware | World + roofs + zoom + UI overlays composited on the GPU; the per-frame CPU upload+readback "bridge" eliminated; full gpu-ui window-stack composite | World render pass **~10 ms → <1 ms**; the residual heavy bilinear-LERP tile blits (~1000 tiles/frame) that the software AABB skip *couldn't* reduce moved off the CPU entirely. |
| **3 — Recent software wins** | `perf-gpu-accel` (this push) | both | Art cache 12→96 MB; wmap void-fade half-res; terrain-LERP NEON; idle present-skip | wmap void-fade **−53 %** (5.9→**2.7 ms**/scroll step, spikes gone, ≤6/255 identical); terrain tile-pass **−4.5 %** (byte-identical); present-skip ≈ **static-screen CPU/GPU/battery** win (~290 frames skipped at menu, ~0 in active play). Heavy 5-level sweep: **~2.8 ms HW / ~4.3 ms SW** tile pass, **0** pass-spikes, **0** crashes. |

## Decisions banked along the way (what we did NOT ship, and why)

| Candidate | Verdict | Why |
| --- | --- | --- |
| `tig_video_flip` partial-upload (Stage 1) | shipped, neutral | Hypothesis (full 8 MB upload expensive) was wrong — present/vsync is the floor, upload is 0.5-1.4 ms. Kept as harmless GPU-compositor scaffolding. |
| `video.c` COLOR_LERP SIMD blit | KEEP (as kernel) | Order-controlled A/B = neutral on its own path (only 5 % of lit pixels), but it's the **proven byte-identical kernel** the Stage-3 terrain-NEON reuses. Don't rip out the foundation. |
| Terrain **gradient**-vectorization | byte-identical but **reverted** | The hoped "double the terrain win." Proven 0-px diff on terrain, but perf measured neutral (both A/B orderings disagreed in sign = order/warmup; the sub-ms pass leaves it below the noise floor). The gradient isn't the bottleneck — gather+multiply+memory dominate. Engine is simply too optimized for it to register. |
| Table-free `/255` | not pursued | Same memory-bound-neutral pattern as the SIMD (the mul table is L2-resident). |
| **Metal** renderer | parked (keep OpenGL) | GL does adaptive vsync via SDL (the best mode, −28 % software frame vs hard). Metal's world pass currently renders *broken*, and SDL's 2D renderer doesn't expose Metal's native VRR. (Metal/macOS *does* support adaptive vsync natively — that earlier "can't" was an SDL-layer limit; a Metal path is a real future project once its render is fixed.) |

## Where each render path stands today (`perf-gpu-accel` head)

- **Software path (the cfg default — what most users run):** the Stage-1 pipeline
  (already inside one vsync) + Stage-3 wins (96 MB art cache, wmap half-res,
  terrain-NEON −4.5 %, idle present-skip gated). Per-pixel SIMD headroom is
  **tapped out** — the terrain multiply was the last clean win; finer SIMD is
  neutral because the blit is memory-bound.
- **Hardware path:** the Stage-2 GPU present path (world/roofs/zoom/UI on GPU,
  bridge gone) + adaptive vsync. The remaining identified win is the GPU-side
  present-skip dirty-gate (the software present-skip's hardware twin).

## Fresh apples-to-apples cross-branch sweep — 2026-06-27 (the arbiter harness)

The Stage 0-3 numbers above mix metrics/scenarios (the honesty caveat). This sweep
re-measured the whole lineage with ONE instrument on ONE scene, arm64-native:

- **Method:** a `newgame` harness command spawns a premade PC (Merwin) into a real start
  map — save-format independent, so every branch runs identically (the old saves the
  prior sweep used are gone post-rework). A universal frame-timer in `main.c` (wraps
  `iso_redraw` + `tig_window_display`, gated on `ARCANUM_GPU_CMD`) dumps the same
  `[zoom-perf]` format on every branch, old or new.
- **macOS gotchas:** after a SIGKILL, window-state restoration hangs the next launch
  (`_reopenWindowsAsNecessaryIncludingRestorableState`) — launch with
  `-ApplePersistenceIgnoreState YES` + clear the saved state. Display sleep wedges the GL
  swap — run under a persistent `caffeinate -dis`.

### The optimization arc (software iso render, identical scene)

| Branch | crash-site zoom | heavy town | note |
| --- | --- | --- | --- |
| `upstream/main` (vanilla) | — pre-zoom | 0.35 ms | light start-map static floor = ~0.08 ms (all branches equal) |
| pre-zoom-feature (`78250818`) | — pre-zoom | 0.34 ms | |
| pre-perf (`a8bcc2cd`) | **1.39 ms** | **1.24 ms** | zoom feature added, UN-optimized = the peak |
| `zoom-perf-experiments` (`162c47a3`) | 0.37 ms | 0.44 ms | software zoom-perf opts (−73% vs pre-perf) |
| `ui-improvements` (`4c213266`) | 0.39 ms | 0.37 ms | ≈ zpe (built on it; no render-path change) |
| `perf-gpu-accel` (current) | **0.19 ms** | **0.17 ms** | GPU present + resolve-once + 96 MB cache (−51% vs ui-i) |

**Net −86 % (pre-perf → current)**, corroborated on BOTH scenes. Two real jumps: the
software zoom-perf era (pre-perf→zpe), and this branch's GPU+recent work (ui-i→current).

### Honesty notations
- The **light start map** (crash dungeon) doesn't discriminate on non-zoom work — every
  branch renders it in ~0.08 ms. Optimizations only register under render load.
- The **heavy town** (Shrouded Hills, reached via `tele`/`newgameat` to the overworld town
  coords on map 1) discriminates, BUT the four pre-rework branches' OLD ENGINE stalls the
  harness channel during the dense-town sector load — so their town numbers are an
  **idle-frame proxy** (consistent ×2 runs); current is a full workout. A clean active town
  workout across the old branches needs the engine internals ported, not just the harness.

### Cross-module characterization (current, this session)
SW + HW, 6 real levels / 2 modules (Arcanum + Vormantown), full workout (zoom/scroll/walk/UI):
iso render **0.78-1.41 ms everywhere** (SW ≈ HW; the per-pass metric doesn't isolate GPU-
async cost — the GPU world pass is <1 ms in the dedicated zoom test). Only over-budget frames
are zoom-out worst-frame spikes **9-37 ms** (worst on Wall-Glitch, likely an anomaly). 0 crashes.

### REVERTED — 2-thread tile pass (briefly default-on, `ac2d3f3a`)
Capped the worst zoom-out full-redraw at ~11 ms (−42 %), byte-identical on a WARM cache — but
**SIGSEGVs on every save-load** (cold cache: the slow-path entry load races the lock-free
resolve read; `art_blit` derefs a torn pixel pointer). Reverted to default-OFF. Lesson:
"warm-cache byte-identical + no crash" does NOT prove thread-safety — the cold post-load draw
is the real adversary; safe default-on needs a pre-warm pass. Gated on for warm-cache experiments.

## Sources
- Stage 0-1: `docs/perf-status.md` (the F9 perf-log before/after record).
- Stage 2: `docs/perf-gpu-accel-findings.md`, `docs/perf-gpu-accel-plan.md`.
- Stage 3 + decisions + next steps: `docs/perf-next-steps.md`.
- 2026-06-27 sweep: this section; harness in `src/game/gamelib.c` (`gpu_test_channel_tick`),
  `src/main.c` (frame-timer), `src/ui/mainmenu_ui.c` (`mainmenu_ui_harness_newgame[_at]`).
