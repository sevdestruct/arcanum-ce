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

1. **Revert the SIMD `video.c` NEON path** (`8d4002bd`). Confirmed neutral; remove the NEON complexity from a core blit. (Keep the `ARCANUM_OPT_SIMD_BLIT` plumbing only if it stays useful; otherwise drop.)
2. **Software fixed-overhead wins** (byte-identity-free, hit the default deployment):
   - **Resolve-once tile blit** — hoist the 4-quadrant art-cache re-resolution out of the LERP-tile quadrant loop (`tile.c:2109-2188`). Pure overhead removal.
   - **Table-free /255** — replace the 64KB `tig_color_mul` table with the integer identity (`color.h:58-71`). **Measure first** (cache-bound claim unverified; risk of 1-LSB byte-identity drift).
   - **Software present-skip** — `tig_window_display` flips unconditionally; skip the flip when nothing's dirty. **Needs a replacement idle-sleep** or the loop busy-spins (skipping the present removes the vsync pacing). Battery/CPU win on idle/static frames.
3. **`art.c` NEON port — the big software win.** NEON-ize the *actual* world lighting blitter (100% scalar today). **Gated on a pixel-count profile**: terrain LERP-gradient loop (`art.c:5279`, byte-identity hazard) vs gradient-free object loop (`art.c:3540/3608/3817`, safer). Instrument per-COLOR-op pixel counts, decide which dominates, then port the dominant safe loop. Est. 30-50% off the tile/wall fill.
4. **GPU present-skip dirty-gate** — same idle-frame win on the hardware path; also folds in the per-flip `RenderClear` scope.
5. **Future features** (see `docs/enhancement-ideas.md`): shadows are *implemented-but-dormant* (sun model needed, ~1-2 day), color-cycling feasible without new sprites, flicker lights, weather, bloom, reflections. Score every per-present feature against the vsync budget.
6. **perception-fog rebase** — must re-home onto the GPU present-time compositor (it CPU-reads-back the framebuffer the GPU path deleted). See `[[perception_fog_rebase_trap]]`.

## Harness state (this branch)
Built this session, default-behavior-unchanged: gpucmds `wmap`/`wmapscroll`/`wmapcap`/`wmaphalf`/`inven`/`logbook`/`simd`/`setmodule`; instrumentation `[render-diag]` (driver + `SDL_GetRenderVSync`), `[flip-perf]` (present-vs-upload split), `[wmap-fade]`/`[wmap-fade-half]` timers, `ARCANUM_RENDER_DRIVER` override. Launch under `caffeinate -dimsu` (display-sleep blocks the GL swap, not a window-server wedge).
