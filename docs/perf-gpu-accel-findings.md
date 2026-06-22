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

## Update — resumed; visual correctness done, steps 1–4 complete

The migration was resumed and the GPU world path is now **visually correct** for
tiles, objects, AND roofs (validated by an autonomous software-vs-GPU A/B harness,
`tools/gpu_test/run.sh <Slot>` + `diff_bmp.py`). The three visual bugs that had
blocked the object pass are fixed (see commit `f1148fed` and memory
`gpu_three_bug_root_causes`):

- **Object lighting did NOT require the feared multi-round color disaster.** The
  fix: blits arriving with no color intent synthesize the working-palette ambient
  tint as a runtime `COLOR_CONST` (`light_default_tint_for`, light.c) over the
  original-palette cache; per-column wall light ships as a real `COLOR_ARRAY`
  (new `TIG_VIDEO_BUFFER_BLIT_BLEND_COLOR_ARRAY` flag, blit_gpu samples
  `color_array[col]` over a fine grid). GPU matches software within ~1–3 lsb.
- **Shadows:** `PALETTE_OVERRIDE` with the dim shadow palette + the Metal SUB
  blend-op fix (REV_SUBTRACT for both color AND alpha — Metal silently rejects
  mismatched ops; see memory `gpu_metal_sub_blend_op_match`).
- **Z-order:** plain/COLOR_ARRAY blits now draw in-pass on the GPU target instead
  of deferring to the post-readback CPU replay. **Deferrals are now ~0/frame**
  (only true STIPPLE_S/ALPHA_AVG remain), which matters for step 5 below.

So original steps **1–4 are DONE**. Remaining: **5 (drop upload)** and
**6 (drop readback — the step that wins)**.

### Current bridge (exact code)
- Upload: `tile_gpu_begin_pass` (tile.c:221) → `SDL_UpdateTexture(gpu_tex, full, dst)` (tile.c:245), full-frame every frame.
- Readback: `tile_gpu_end_pass` (tile.c:267) → `SDL_RenderReadPixels` (tile.c:278) → blit into `dword_602DF0`, full-frame every frame.
- Bracketed by `tile_gpu_world_begin`/`tile_gpu_world_end` (tile.c:1267/1285).

### Readback consumers (what must be handled to drop it — step 6)
1. **Deferred-blit replay** (tile.c:1309) — replays leftover blits via `tig_art_blit` onto the CPU surface AFTER readback. Now ~0/frame; the few remaining could draw on the GPU target or be accepted.
2. **Void-edge facade scan** (`tile_void_edge_scan`, tile.c:1207) — locks + reads `dword_602DF0` to detect pure-black facades. Would need to read the GPU target (region `RenderReadPixels`) instead, or run on a cached subset.
3. **Present / UI compositing** — `dword_602DF0` is blitted to the window framebuffer; translucent-black UI panels read the world beneath them. The hard part: with the world GPU-resident, those UI reads need a partial readback of UI-covered regions OR a GPU-side blend at present.

### Step 5 (drop upload) — ATTEMPTED, reverted; has a prerequisite
Implemented persistent-target upload-skip; static A/B passed but it **broke roofs
under zoom/scroll** (roofs disappear, tear/repeat in the void area, flicker while
scrolling; ground/walls fine). Root cause found in `gamelib_draw_game`
(gamelib.c:3398-3399):

```
tile_gpu_world_end();   // readback GPU target -> dword_602DF0
roof_draw(draw_info);   // roofs draw AFTER, software, onto the CPU surface
```

**Roofs are NOT on the GPU target** — they're software-drawn onto `dword_602DF0`
*after* the readback (the existing comment there literally says "after roof_draw
once roof is on GPU too"). The per-frame upload was silently carrying last frame's
roofs back into the GPU target; skipping it lets the readback wipe roofs from the
target, and only the dirty roof rects get repainted -> flicker/tear. Ground/walls
survive because they genuinely live on the target.

**So the corrected order is:**
- **5a. Move `roof_draw` onto the GPU target** (inside the pass, before
  `tile_gpu_world_end`; route roof blits through `tile_gpu_dispatch`). This is the
  unfinished half of original step 3, and it's the prerequisite for BOTH the upload
  and readback elimination (you can't drop the readback while roofs depend on the
  post-readback CPU surface). Validatable with the static A/B harness (roof
  correctness) + scroll-test.
- **5b. Drop the upload** (the persistent-target skip — code is in git history;
  re-seed on create/resize/resume/deferred). With roofs on the target it works.
- **6. Drop the readback** via present-time GPU composite. The win.

Saves ~3 ms (upload) at 5b, ~3.6 ms (readback) at 6.

### Measured again (F9, user's M3 Max, GPU mode, both Town day + night)
```
gpu-bridge: upload avg 3.5-4.9 ms | blit avg 0.02-0.10 ms | readback avg 3.3-4.9 ms
passes:     tile avg 3.5-5.0 ms | object avg 0.1-0.2 ms | roof avg 0.01 ms
```
Blit is **0.02-0.10 ms** (30-100x faster than software's ~3 ms). The bridge
(~8 ms upload+readback) is the entire GPU cost. The transfer size is the iso
window buffer (constant with zoom), so the bridge doesn't scale with zoom.

### 5a attempt (roofs on GPU target) — done + REVERTED
Routed `roof_draw` blits through `tile_gpu_dispatch` and moved it inside the pass
(before the readback). **Static rendering was correct** (roofs match software).
But two MOTION regressions appeared and it was reverted:
- **Tearing through FADED (transparent) roofs.** When the player/an NPC is under a
  roof (roof alpha-faded so you see through it), the interior behind the roof
  doesn't refresh consistently outside the moving sprite's dirty rect — you see a
  dirty-rect-shaped tear and stale see-through content. The alpha-fade roof
  composited on the GPU target does not survive the partial-redraw + upload/
  readback round-trip the way the persistent CPU surface does. (roof_draw DOES
  clip to dirty rects, roof.c:263 — so it's not overdraw; it's the alpha compose.)
- **Stutter when zoomed far out** — per-roof-piece dispatch + the ALPHA_LERP grid
  subdivision (the smooth fade) add CPU/geometry overhead that bites with many
  roof pieces on screen.

**Insight for the resume:** the faded roof is the crux. It probably should NOT
live in the world pass at all — composite it as a **present-time layer** in step 6
(GPU world texture, then the faded roof over it, then UI), where there's no
partial-redraw/readback interaction. That folds 5a into the step-6 present rework
rather than fighting the bridge. Opaque (fully-shown, outside) roofs could go on
the target fine; only the faded case is hard.

## Step 6 design — investigated (the core present path)

Present chain today: `sub_51D050` (window.c, the compositor) blits every window —
iso world (`dword_602DF0`) + all UI — into `tig_video_state.surface` (a single
**XRGB8888** CPU framebuffer, no alpha); `tig_video_flip` (video.c:1041) uploads
that surface to one streaming texture and `SDL_RenderTexture(NULL,NULL)` blits it
to the screen. So EVERYTHING goes through one CPU framebuffer. The readback exists
to get the GPU world into that framebuffer.

**Existing infrastructure to leverage — the "knockout" compositor** (window.c:81-89):
a window can set `knockout_enabled` + `knockout_key` (RGB) + `knockout_underlay`
(a world source window); `sub_51D050` routes that window's blit through
`tig_video_blit_knockout`, replacing key-colored pixels with the underlay. This is
already the world-through-translucent-UI path. (It's a CPU blit, so today it still
needs the world as CPU pixels — i.e. the readback. Understanding/repurposing this
is central to dropping the readback cleanly.)

**Why there's no trivial increment:** the framebuffer is XRGB (no alpha), so you
can't just leave the iso region transparent and draw the GPU world behind it. The
clean options all touch the core path: (a) make the iso region a colorkey and have
`tig_video_flip` present GPU-world-then-framebuffer-with-colorkey-transparent (needs
CreateTextureFromSurface-with-colorkey or a shader each frame); (b) move the whole
final composite onto the renderer (draw GPU world tex at the iso screen rect, then
the UI on top), repurposing the knockout path so translucent UI samples the GPU
world; (c) make the framebuffer ARGB and clear the iso region to alpha 0. All are
broad and **harness-blind** (the harness captures `dword_602DF0`, not the screen
present — only in-game eyes + F9 validate step 6).

**Increment order for a focused session:**
1. Make `tile_gpu_world_buffer` persistent + expose its SDL texture + the iso
   screen rect (`tile_iso_window_handle`).
2. In `tig_video_flip`, behind a flag, present the GPU world tex at the iso rect,
   and make the framebuffer's iso region show it through (colorkey or knockout).
3. Faded roof as a present-time layer over the GPU world tex (fixes 5a).
4. Translucent UI that samples the world → via the knockout underlay = GPU world
   (or a small partial readback only of UI-covered regions).
5. Rework `tile_void_edge_scan` (reads `dword_602DF0`) to sample the GPU target
   region or a cached subset.
6. Drop the readback in `tile_gpu_world_end` + the per-frame upload in
   `tile_gpu_begin_pass`. Verify `gpu-bridge: upload→0 readback→0`.

Net target: world pass ~0.4 ms (blit only) vs software ~3 ms — the real win.
