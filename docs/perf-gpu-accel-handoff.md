# GPU acceleration session — handoff

You are picking up GPU-acceleration work on a sibling branch of an ongoing
software-perf push. The other branch is being actively worked in a separate
session. Stay on your branch and don't touch theirs.

## Read this first

Before doing anything else, read **`docs/perf-gpu-accel-plan.md`** in the
repo. It contains:
- Why this work exists (software perf push hit the limit, tile_draw_iso
  doing ~3M pixels/frame of CPU LERP-blits is the wall)
- Audit findings (existing `tig_video_3d_*` API is hollow stubs, SDL_Renderer
  is underutilized as a passive upload target, existing flags like
  `TIG_VIDEO_BUFFER_CREATE_TEXTURE` are reserved-but-inert)
- The 5-phase migration plan and architecture
- Open questions and risks

The plan is your spec. Don't deviate without thinking through why.

## Branch state

- You are on `feature/perf-gpu-accel`
- It was branched off `feature/ui-improvements` at commit `3719da2a`
  (the megahitch-logger commit)
- HEAD on this branch is currently `0eedd0b6` (the plan-doc commit) — just
  the plan, no GPU code yet

Verify before starting:
```bash
git branch --show-current   # → feature/perf-gpu-accel
git log --oneline -5        # → top entry is the perf-gpu-accel-plan commit
```

If those are wrong, stop and ask before proceeding.

## What's running in the other session

The other session is working `feature/ui-improvements` (the software perf
path). It has already shipped (in addition to commits on this branch):
- adaptive vsync as default (`SDL_RENDERER_VSYNC_ADAPTIVE`)
- AABB pre-skip in `tile_draw_iso` (the per-tile fast-reject before the
  roof check)
- sound file cache bumped 20→256 items, 1MB→64MB (fixed a 400ms hitch)
- megahitch logger that emits `[megahitch] <bucket> took Xms` to the perf
  log when any per-bucket measurement exceeds 100ms

The user may land more commits there while you work. **Don't rebase or
merge from `feature/ui-improvements` yourself.** When the user wants to
sync the branches they'll do it (`git rebase feature/ui-improvements`
from your branch).

## Environment

- Machine: 2023 MacBook Pro, M3 Max
- Display: 120Hz ProMotion (vsync cycle ~8.3ms)
- Build preset: `macos-arm64-release` (defined in CMakeUserPresets.json)
- Build output: `out/build/macos-arm64/Release/Arcanum Community Edition.app`

Build command:
```bash
cmake --build --preset macos-arm64-release -j
```

After every successful build, copy the .app to the user's installed-
variant slot. Naming convention: branch name → `(Title Cased Descriptor)`,
acronyms stay upper-case.

For this branch: `feature/perf-gpu-accel` → **`(Perf GPU Accel)`**

```bash
rm -rf "$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app"
cp -R "out/build/macos-arm64/Release/Arcanum Community Edition.app" \
      "$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app"
```

**Do NOT** copy as the bare `Arcanum Community Edition.app` — the user has
many variants installed side-by-side and an unparenthesized name collides
with whichever was there before.

Do NOT auto-launch the app. The user opens it manually when they want to
test.

## Perf measurement (preserve what's already there)

The user collects perf data via:
1. Launch the variant app
2. Press F9 in-game to toggle perf on/off
3. Play normally (scroll, zoom, walk, fight)
4. Press F9 again to stop
5. Read `/tmp/arcanum-zoom-perf.log`

When you A/B-test the GPU path (Phase 3), the existing harness gives you:
- `passes: light avg X max X | tile avg X max X | object avg X max X | roof avg X max X`
  — per-render-pass timing (tile is what we expect to crater with GPU)
- `loop: tig_ping ... | iso_redraw ... | win_display ... | flip: update ... present ... partial N%`
  — total frame breakdown
- `frame avg X max X stddev X` — perceived smoothness metric
- `[megahitch] <bucket> took Xms` — any single iteration >100ms

Run a before/after comparison on the same play scenario (busy area + scroll
at zoom-out) and report deltas in the same table style the other session
uses.

## What to do, in order

1. **Read the plan doc.** Verify you understand the 5 phases and the
   "keep world on GPU between passes" architecture.

2. **Phase 1: GPU-backed `TigVideoBuffer`.**
   - The existing `TIG_VIDEO_BUFFER_CREATE_TEXTURE` flag in
     `first_party/tig/include/tig/video.h` is reserved but inert
     (`tig_video_buffer_create` ignores it). Repurpose it to actually
     mean "create as SDL_Texture for GPU operations".
   - Add an `SDL_Texture* texture` field to the `TigVideoBuffer` struct.
     Mutually exclusive with `surface` — caller picks one at create time.
   - Lock/unlock semantics: a GPU buffer cannot be CPU-mapped; lock should
     either error or be a no-op that records a warning. Existing callers
     that lock should not be allowed to pass a GPU buffer.
   - Write a small unit test or a manual sanity check (create/destroy in
     `gamelib_init` behind a debug flag, log success). No real callers yet.
   - Commit Phase 1 separately.

3. **Phase 2: GPU blit primitives.**
   - `tig_video_buffer_blit_gpu` covering the two blend modes that
     `tile_draw_iso` uses: `TIG_ART_BLT_BLEND_COLOR_LERP` (use
     `SDL_RenderGeometry` with per-vertex colors) and
     `TIG_ART_BLT_BLEND_COLOR_CONST` (use `SDL_SetTextureColorMod`).
   - Lazy art-texture cache keyed by art_id. LRU eviction. Don't preload
     the world — upload on first use. ~12MB budget for tile arts.
   - Commit Phase 2 separately.

4. **Phase 3: tile_draw_iso GPU path behind cfg flag.**
   - Add `tile_render_path` to arcanum.cfg (`software` default, `gpu` opt-in)
   - When `gpu`: render tiles to a world `SDL_Texture` render target via
     the new primitives.
   - **Bridge step**: download the texture back to the existing surface
     after the pass, so the rest of the pipeline (object/roof/light/UI)
     continues to see expected pixels. This is temporary — gets us to an
     A/B-testable state without rewriting the whole pipeline.
   - Have the user A/B test with F9. Expect `tile_max` to drop dramatically
     (from ~10ms to ~1-2ms).
   - Commit Phase 3 separately.

5. **STOP after Phase 3 and surface to the user.** Phases 4 and 5
   (remaining world passes + GPU composite in present) only make sense if
   Phase 3's win is big enough to justify continuing.

## What NOT to do

- Don't touch `feature/ui-improvements` (separate session is working it).
- Don't rebase `feature/perf-gpu-accel` onto a newer base unless the user
  asks. They coordinate sync.
- Don't move the renderer's vsync mode setting; that's user-controlled.
- Don't change the perf-log format or remove the existing F9 instrumentation
  — the user relies on it for A/B testing across branches.
- Don't auto-launch the app after build. User-initiated launches only.

## When to surface things to the user

Interrupt the user (don't just continue silently) when:
- A design call needs their input (e.g., "GPU palette art makes the
  texture cache 5× bigger — accept or constrain?")
- Phase 3 lands and is ready to A/B test
- Visual regressions appear (tile seams, color shift, missing sprites)
  that you can't trivially explain — they'll want to debug with you
- You finish Phases 1-3 and want to decide on Phase 4-5

Otherwise just push commits and keep momentum. Smaller incremental commits
are better than one giant Phase-1-through-3 commit — easier to bisect if
something breaks.

## Useful files to know

- `first_party/tig/src/video.c` — current `TigVideoBuffer` impl,
  `tig_video_flip`, `tig_video_state` (renderer + surface + texture)
- `first_party/tig/include/tig/video.h` — public API + flags
- `src/game/tile.c` — `tile_draw_iso` (target for Phase 3)
- `src/game/gamelib.c` — `gamelib_draw_game` (pipeline orchestration),
  per-pass timing hooks (`gamelib_zoom_perf_pass_*_max_ns`)
- `docs/perf-gpu-accel-plan.md` — the spec

Good luck.
