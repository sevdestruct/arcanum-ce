# Perf — validated progress & pinned next steps

Re-validated under clean(er) conditions after a concurrent Stable Diffusion run
muddied the first measurements. All A/Bs interleaved/order-controlled so they're
robust to background load (a `fileproviderd` cloud-sync was still active).

## Validated & shipped
- **wmap void-fade half-res** — **−53% faster** (5.9ms → 2.7ms per scroll step, max 7→4ms), visually identical (≤6/255). Shipped `4ad57212`. (The first "−40%" was SD-inflated.)
- **Module auto-switch on load** + the `gamelib_saveinfo_load` NUL-termination bug fix — shipped on `feature/ui-improvements` `48d361f6`.
- **Heavy sweep (5 levels × zoom/scroll/walk/menus/wmap, both render paths)** — 0 pass-spikes, 0 crashes, menus 5/5, wmap half-res 20/20, render ~2.8ms HW / ~4.3ms SW. No regressions.

## Validated decisions
- **SIMD/NEON `video.c` blit: REVERT.** Order-controlled = +2.8% (neutral-to-slightly-slower). Root cause (code audit): it's the *wrong target* — world tiles use `art.c`'s scalar paletted blitter, not this path.
- **Renderer: keep OpenGL + adaptive vsync.** Metal *cannot* do adaptive vsync (`SDL_SetRenderVSync(-1)` → "not supported"); GL/Metal present is identical (~6.5ms, both 120Hz on day-town). The present time is normal vsync idle, not a defect. The "Force OpenGL" hint is correct.

## Next steps (ranked, for more wins)
> The DEFAULT cfg is `render path=software`, so software-path wins hit the majority deployment; the user wants BOTH paths maximized.

1. ~~Revert the SIMD `video.c` NEON path~~ **KEEP IT.** It measured neutral only because it serves the 5% townmap/roof/composite path — but it's the *proven byte-identical kernel* the terrain-LERP NEON (#3, shipped) reuses. Don't rip out the foundation. (Metal screenshots showed its world pass is broken — so the "GL≈Metal present" timing was invalid. Keep GL **for now**: GL does adaptive vsync via SDL, the best mode, −28% software frame vs hard. **Correction:** Metal *does* support adaptive vsync natively — `presentAfterMinimumDuration` / `CAMetalDisplayLink` / built-in-ProMotion auto-VRR, since Monterey; the SDL "not supported" was an SDL-2D-renderer limit, not a Metal one. A **Metal path** is a real future option: fix the GPU world render, then plumb native Metal present-pacing (or run full-screen on the ProMotion panel for automatic VRR). It could ultimately present better than SDL/GL.)
2. **Software fixed-overhead wins** (byte-identity-free, hit the default deployment):
   - **Resolve-once tile blit** — hoist the 4-quadrant art-cache re-resolution out of the LERP-tile quadrant loop (`tile.c:2109-2188`). Pure overhead removal.
   - **Table-free /255** — replace the 64KB `tig_color_mul` table with the integer identity (`color.h:58-71`). **Measure first** (cache-bound claim unverified; risk of 1-LSB byte-identity drift).
   - **Software present-skip** — `tig_window_display` flips unconditionally; skip the flip when nothing's dirty. **Needs a replacement idle-sleep** or the loop busy-spins (skipping the present removes the vsync pacing). Battery/CPU win on idle/static frames.
3. **`art.c` terrain LERP NEON — SHIPPED.** Pixel-count profile confirmed terrain LERP = 95% of lit pixels (objects 5%), so the target was the terrain LERP loop (`art.c:5279`). Ported the proven video.c kernel + a 4-wide palette gather (gradient stays scalar → matches the float order). **Byte-identical (max 0/255 over a full frame) and −4.5% on the software tile-pass** (order-controlled, both orderings averaged; the forward-only −11.7% had the ~7% order bias). Default ON; `ARCANUM_OPT_TERRAIN_SIMD=0` = scalar; the `simd` gpucmd toggles it. **FURTHER WIN — ATTEMPTED, NEUTRAL → REVERTED.** Vectorized the gradient too (NEON float, base+lane·step, the `vmla`/`vcvt` pack). Verified **BYTE-IDENTICAL on terrain**: 0-px diff over a 321,600-px sprite-free ground rectangle (the ≤1-LSB FP-truncation drift never materializes on real lighting values — no terrain pixel lands within ε of an integer truncation boundary). But perf measured **NEUTRAL**: the two order-controlled A/B runs DISAGREED in sign (off-first → grad-on 5% faster; on-first → grad-on 1.5% slower) = the order/warmup effect, not the gradient. And the render pass is sub-millisecond (0.6–1.0ms at these zoom levels), so any real effect is sub-0.04ms — below the noise floor. The gradient build is **not** the bottleneck; the palette gather + the NEON multiply + memory dominate. This is the *same outcome as the video.c SIMD* — don't accrete neutral SIMD into the 95%-hot blit. Reverted (was gated `ARCANUM_OPT_TERRAIN_GRAD`, never committed); the byte-identical multiply above stays the win. Built a runtime `grad` gpucmd + capture/heatmap byte-diff harness to reach this — also reverted. If revisited: the gather is the thing to attack, not the gradient.
4. **GPU present-skip dirty-gate** — same idle-frame win on the hardware path; also folds in the per-flip `RenderClear` scope.
5. **Future features** (see `docs/enhancement-ideas.md`): shadows are *implemented-but-dormant* (sun model needed, ~1-2 day), color-cycling feasible without new sprites, flicker lights, weather, bloom, reflections. Score every per-present feature against the vsync budget.
6. **perception-fog rebase** — must re-home onto the GPU present-time compositor (it CPU-reads-back the framebuffer the GPU path deleted). See `[[perception_fog_rebase_trap]]`.

## Harness state (this branch)
Built this session, default-behavior-unchanged: gpucmds `wmap`/`wmapscroll`/`wmapcap`/`wmaphalf`/`inven`/`logbook`/`simd`/`setmodule`; instrumentation `[render-diag]` (driver + `SDL_GetRenderVSync`), `[flip-perf]` (present-vs-upload split), `[wmap-fade]`/`[wmap-fade-half]` timers, `ARCANUM_RENDER_DRIVER` override. Launch under `caffeinate -dimsu` (display-sleep blocks the GL swap, not a window-server wedge).
