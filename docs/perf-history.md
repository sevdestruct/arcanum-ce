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

## Sources
- Stage 0-1: `docs/perf-status.md` (the F9 perf-log before/after record).
- Stage 2: `docs/perf-gpu-accel-findings.md`, `docs/perf-gpu-accel-plan.md`.
- Stage 3 + decisions + next steps: `docs/perf-next-steps.md`.
