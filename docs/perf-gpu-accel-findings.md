# GPU acceleration — findings & status (paused after Phase 3)

Status as of the post-Phase-3 work on `feature/perf-gpu-accel` (rebased onto
`feature/ui-improvements`). The GPU tile path is **visually correct and
opt-in** (`tile render path = gpu` in arcanum.cfg; default `software`). The
migration is **paused before the object pass** — see "The wall" and "Why we
stopped" below.

## What shipped on this branch

On top of Phases 1–3 (GPU buffer, blit primitive + art cache, flag-gated
`tile_draw_iso` GPU dispatch):

- **Original-palette GPU art cache** (`tig_art_render_original_palette`). The
  cache now renders each tile through its immutable `hdr.palette_tbl` instead
  of the engine's mutating working palette. Fixes the base-color mismatch and
  makes textures tween-proof (no per-frame invalidation). Also bakes the
  flippable-tile mirror, fixing un-mirrored tiles.
- **Deferred cache-miss replay.** Misses are recorded and replayed with
  `tig_art_blit` *after* the readback, instead of drawing to the CPU surface
  mid-pass (which the readback would clobber → black tile holes).
- **Bilinear LERP via subdivision.** `tig_video_buffer_blit_gpu` subdivides
  the LERP quad into a grid with exact bilinear vertex colors, matching the
  software bilinear blitter (SDL_RenderGeometry alone is Gouraud → diagonal
  shading seams, and it skewed void_edge_fade's dark probe).
- **Plain-tile ambient.** Plain (flags=0) tiles emit a CONST modulate by the
  ambient color so the GPU path isn't stuck at daytime brightness at night.
- **Render-target thrash fix.** `blit_gpu` no longer does a per-blit
  SetRenderTarget save/restore when the target is already bound (the tile
  pass binds once). This is what made the actual blit cheap.
- **Bridge instrumentation.** F9 log emits `gpu-bridge: upload | blit |
  readback` under each `passes:` line.

## The measured wall

F9 perf, busy zoom-out scroll, M3 Max / 120Hz, 1280×804, GPU mode:

```
passes:     tile avg ~6.5–7.5 ms
gpu-bridge: upload avg ~3.0 ms | blit avg ~0.2 ms | readback avg ~3.6 ms
```

Software baseline tile pass: **~2.6–4.3 ms**.

The takeaways:

- **The GPU blit is ~0.2 ms — ~15× faster than software's ~3 ms** for the same
  pixels. The actual drawing is a huge win.
- **The entire GPU-path cost is the two bridge transfers** (~3 ms upload +
  ~3.6 ms readback ≈ 6.6 ms), which is why GPU is *net slower* than software
  today. The transfers are dominated by CPU↔GPU synchronization, not
  bandwidth, so making them partial/smaller doesn't help much — they have to
  be **removed**, not shrunk.

Arithmetic of the prize: remove both transfers and the whole world pass drops
to **~0.4 ms** (tile+object+roof blits) vs software's ~3.5 ms. Real and large.

## What the upload actually is (key finding)

The begin_pass upload is **not** carrying the light pass. `light_draw` only
fills small grid-sized aux buffers (`darker_vb`/`lighter_vb`, content/40 ×
content/20) that `tile_draw` *samples* for its LERP corner colors — it never
paints the world buffer. The upload exists to seed the GPU target with the
**previous frame's content for partial redraw** (the engine has no
scroll-blit; it invalidates + redraws dirty rects and relies on the window
buffer retaining the rest).

So removing the upload is about making the **GPU world target persistent**
(retain its own content frame-to-frame), not about porting light.

## The path to the win, and why we stopped

To remove the readback (the bigger half), the whole world must be GPU-resident
so the present step can composite it — which means **object and roof must draw
on the GPU target too**. Object is the linchpin: its SUB shadows / ADD / MUL
blends *read* the destination, so objects can't live on a separate layer.

The blocker surfaced when speccing `object_draw`:

- **Object main sprites get their day/night ambient + local lighting from a
  per-object *working palette*** — not from LERP corner colors like tiles
  (objects don't use `field_14`). That palette changes continuously with the
  time-of-day tween **and** varies per object.
- A GPU art-texture cache keyed by `art_id` is therefore **wrong for objects**
  (same art, many palettes) and would re-upload every frame (cache thrash).
- The viable approach is to cache objects in their original palette and
  reapply lighting as an approximate per-object `SetTextureColorMod` tint
  (like the tile trick). But Arcanum's palette lighting isn't a pure linear
  multiply, so it won't be bit-exact — expect a multi-round color-correctness
  effort, on top of the palette-aware cache, the combined ADD/SUB/MUL blends,
  and the present/UI compositing rework.

Decision: **bank the correct, opt-in GPU tile path and stop here.** The
remaining win is real but gated on a hard, multi-step object-lighting
migration with meaningful regression risk. Software stays the shipping path.

## If someone resumes this

Remaining steps to the win, in order (all required; the win only lands at the
end):

1. Generalize `blit_gpu`: ADD / SUB / MUL (via `SDL_ComposeCustomBlendMode`,
   all read-modify-write) + ALPHA_CONST + combinable with COLOR_CONST/LERP.
2. Palette-aware GPU art cache: key by `(art_id, palette)`; cache objects in
   original palette and modulate by a per-object light tint.
3. `object_draw` + `roof_draw` dispatch onto the GPU target (choke point:
   `object_flush_pending_blits` → `tig_window_blit_art`, ~object.c:4908).
4. Single begin/end around all world passes in `gamelib_draw_game`.
5. Persistent GPU world target → drop the per-frame upload (seed on
   create/resize/area-change/zoom-toggle).
6. Present-time composite (world tex + UI) in `tig_video_flip` → drop the
   readback. Handle translucent-black UI (reads world): partial readback of
   UI-covered regions, or GPU-side blend. **This is the step that wins.**

The `gpu-bridge:` instrumentation is in place to verify each step.
