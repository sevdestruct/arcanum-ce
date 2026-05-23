# GPU acceleration plan (feature/perf-gpu-accel)

Sibling branch to `feature/ui-improvements`. Starting point: current state of
that branch after the perf push (adaptive vsync, AABB skip, megahitch logger,
all the instrumentation, etc.).

## Why

After the perf push hit diminishing returns on the software path, the
remaining cost is **the actual pixel-pushing in `tile_draw_iso` and
`tig_video_buffer_blit`**. Concretely:

- ~1000 visible tiles × 3120 pixels × LERP-blit = ~3M pixels/frame of
  bilinear-LERP math, on the CPU, every frame during scroll
- `tile_max` peaks at 7-10ms — half the 8.3ms ProMotion budget on its own
- No per-tile overhead trick can help (already at minimum); only making the
  pixel work itself faster will move it

GPU is the obvious answer. SDL3's `SDL_Renderer` already supports color
modulation, blend modes, and offscreen render targets — everything the
existing software blit pipeline does. We just don't use any of it.

## Audit summary (see also: agent investigation in commit messages)

- `tig_video_3d_check_initialized()` and friends are hollow stubs left from
  the original D3D port. `tig_video_3d_initialized` is never set to true.
  No path to reactivate.
- `*_hardware_accelerated` booleans exist in tile/light/roof/object init but
  always evaluate false. They gate a few small flag differences (palette
  handling, stipple-vs-alpha-const for roof fade) — useful as architectural
  signposts but not enough scaffolding to ride.
- `TigArtBlitInfo` blend flags are rich (LERP / CONST / ARRAY for color,
  ALPHA_CONST / ALPHA_LERP_* / ALPHA_STIPPLE_D / ADD / SUB / MUL / AVG) — all
  implemented purely in software via `tig_video_buffer_blit`.
- `SDL_Renderer` is initialized but used only for: one `SDL_UpdateTexture`
  per frame + one `SDL_RenderTexture` + `SDL_RenderPresent`. Already
  underutilized. Plenty of headroom.

## Target architecture

The world render passes (light → tile → object → roof) currently
write to one shared `SDL_Surface` (`tig_video_state.surface`). After that the
UI passes (tb/tf/tc) stamp HUD elements onto the same surface.
`tig_video_flip` uploads the whole surface to a single `SDL_Texture` and
presents.

The migration goal:

```
[World passes]                      [UI passes]        [Present]
light → tile → object → roof        tb → tf → tc       UpdateTexture +
on a GPU-backed render target       in software, on    RenderTexture +
(SDL_Texture, RENDER_TARGET)        a CPU surface      RenderPresent
                                    that gets layered
                                    over the world
                                    texture in present
```

- World tex stays on GPU between passes (no readbacks).
- UI surface stays on CPU (much smaller per-frame change; current code is
  already efficient there).
- Final present: GPU composite of world tex + UI tex.

This isolates the migration to the four world-pass functions and the present
path. Game code calling `gamelib_invalidate_rect` etc. is untouched.

## Phasing

**Phase 0: scaffolding (this commit)**
- Plan doc (this file).
- `feature/perf-gpu-accel` branch created off `feature/ui-improvements`.
- No code changes yet.

**Phase 1: GPU-backed alternative TigVideoBuffer**
- Add `TIG_VIDEO_BUFFER_CREATE_GPU` flag and a parallel SDL_Texture-backed
  implementation alongside the existing `SDL_Surface`-backed one.
- Constructor/destructor, lock/unlock no-op for GPU buffers (lock/unlock
  semantics differ — GPU buffers can't be CPU-mapped efficiently).
- All ops error or fallback when called on a GPU buffer.
- Zero callers yet; the new path coexists with the old one.

**Phase 2: GPU-renderer blit primitives**
- Implement `tig_video_buffer_blit_gpu` covering the subset of
  `TigArtBlitInfo` flags actually used by tile_draw_iso:
    - `TIG_ART_BLT_BLEND_COLOR_LERP` (per-corner color via
      `SDL_RenderGeometry`)
    - `TIG_ART_BLT_BLEND_COLOR_CONST` (`SDL_SetTextureColorMod`)
    - `TIG_ART_BLT_PALETTE_ORIGINAL` (irrelevant for GPU; skip)
- Art-texture cache: lazy upload of tile-art SDL_Surfaces to SDL_Textures,
  keyed by art_id, LRU-evicted.

**Phase 3: tile_draw_iso GPU path (flag-gated)**
- Add `tile_render_path` setting in arcanum.cfg (`software` / `gpu`).
- When `gpu`: render to the world SDL_Texture render target via the new blit
  primitives.
- After the pass, download to the existing surface so subsequent passes
  (object/roof/light/UI) still see expected pixels. **This is a temporary
  bridge for testing — keeps the rest of the pipeline unchanged.**
- A/B test perf with the same F9 instrumentation; expect tile_max to drop
  to ~1ms.

**Phase 4: convert remaining world passes**
- object_draw, roof_draw, light_draw moved to the same GPU render target.
- Remove the download bridge from Phase 3.

**Phase 5: GPU composite in present**
- Keep UI surface in CPU. In `tig_video_flip`, composite world texture +
  UI texture via SDL_Renderer instead of doing CPU pass + single texture
  upload.

## Risks

- **Texture upload bandwidth.** Loading every tile art to GPU on first use
  costs upload time. Mitigated by lazy cache + LRU eviction.
- **Pixel-exact correctness.** Existing LERP path may not be bit-identical
  to `SDL_RenderGeometry`. Visual diffs likely; need side-by-side check.
- **Palette art.** `TIG_ART_BLT_PALETTE_ORIGINAL` means the source uses a
  remapped palette. GPU path will need texture re-upload on palette swap
  (rare — only happens for ambient lighting state changes).
- **Subpixel offsets / iso math.** Currently all blits are integer-aligned.
  SDL_Renderer is float-coord; must round consistently to avoid seam gaps
  between tiles.
- **Render-target permanence.** `RENDERER_TARGET_LOST` events on
  alt-tab/resize need handling. Current path doesn't have to.

## Open questions

- Should the GPU path entirely replace software, or always coexist?
  Probably coexist for at least one ship cycle so users can opt out if
  there's a regression.
- Texture format. Surface is `XRGB8888` (line 1472 video.c). Use the same
  for the GPU buffers to avoid format conversion at present time.
- Memory budget for the tile-art cache. Arcanum has ~1000 unique tile arts?
  At 78×40×4 bytes = ~12KB each → ~12MB total if we cache everything.
  Probably acceptable.

## Next concrete actions

1. Phase 1: `tig_video_buffer_create` flag + SDL_Texture-backed impl. ~1 day.
2. Phase 2: GPU blit primitive for the two blend modes tile_draw uses.
   ~1 day.
3. Phase 3: behind the `tile_render_path=gpu` cfg flag, prototype tile pass
   with the download bridge. ~1 day plus measurement / visual check.

Stop after Phase 3 to decide whether the win is big enough to continue to
Phase 4-5 (likely yes but worth confirming).
