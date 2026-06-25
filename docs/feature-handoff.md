# Handoff — visual features (new isolated session/worktree)

This branch (`feature/perf-gpu-accel`) is staying focused on **optimization**. The
**visual-feature** work below should run in its **own worktree** off this branch so
the two don't tangle. This doc is the cold-start brief — it assumes no memory of
the session that wrote it.

## Setup (do this first)

```bash
# from the main repo root
git worktree add ../arc-features -b feature/visual-fx feature/perf-gpu-accel
cd ../arc-features
cmake --preset macos-arm64-release          # configure once
cmake --build --preset macos-arm64-release  # ALWAYS the Release preset (Debug is 5-10× slower)
# deploy to a branch-named app so it doesn't clobber other variants:
rsync -a --delete "out/build/macos-arm64/Release/Arcanum Community Edition.app/" \
  "$HOME/Applications/Arcanum/Arcanum Community Edition (Visual FX).app/"
```

**Run/test harness** (proven this session):
- Display sleep blocks the GL vsync swap → always launch under `caffeinate`:
  `caffeinate -u -t 2` (wake) then `open -g "<app>" --args -window -gpucmd:/tmp/cmd.txt`
  with `ARCANUM_GPU_CMD=/tmp/cmd.txt` set.
- The gpucmd channel (read every frame): `loadsave <Slot>`, `wait <frames>`,
  `setpath <software|gpu>`, `scrollto <x> <y>`, `setzoom <f>`, `capture <abs.bmp>`,
  `simd 0|1`, `presentskip 0|1`, `perf`, `quit`. Add your own in `gamelib.c`'s
  `gpu_test_channel_tick` (near `src/game/gamelib.c:1182`).
- Saves live in `~/Applications/Arcanum/data/Save/` (shared across variants):
  Slot0013 Town (day, outdoor), Slot0014 Indoor-Daytime, Slot0015 Night,
  Slot0016 Under-Roof, Slot0012 Blimp.
- Visual A/B: `capture` two BMPs at the same scripted position → diff with
  `tools/gpu_test/diff_bmp.py` (gives max/mean delta + heatmap PNG). Use a
  sprite-free terrain rectangle to isolate a static effect from NPC animation.

## The master roadmap

`docs/enhancement-ideas.md` is the authoritative feature roadmap — every item below
has a fuller write-up there with `IMPLEMENTED (dormant)` / `ABSENT` / `FEASIBLE` /
`PARTIAL` status and the engine-readiness analysis. Read it first. Don't duplicate
it; extend it.

## Features, in the order proposed (pick per the user)

### 1. Directional shadows — the marquee (≈1-2 day)
- **Status: object/critter cast shadows are IMPLEMENTED but DORMANT.** Infra is in
  `src/game/light.c` (`shadow_init` / `shadow_exit` / `shadow_apply` / `shadow_node_*`,
  `shadow_node_head`, the `shadows_changed` hook) + `light.h`. It's point-light driven
  and gated off (`SHADOWS_KEY`).
- **Prerequisite (ABSENT): a sun/moon azimuth model** to drive *directional* outdoor
  shadows (see enhancement-ideas §"Sun / moon azimuth model"). Without it you only get
  point-light shadows.
- **GPU gotcha (already learned):** if you blend shadows on the GPU, Metal/SDL silently
  drops a custom SUB blend mode whose alpha op ≠ color op → solid-black shadows. SUB must
  use `REV_SUBTRACT` for *both* channels. (memory: `gpu_metal_sub_blend_op_match`.)
- Start: enable the dormant path, get point-light shadows rendering, then add the sun model.

### 2. Palette color-cycling — FEASIBLE, no new sprites
- Animate water / lava / fire / magic-glow by cycling palette indices on the existing
  static art — zero new assets. See enhancement-ideas §"TIG palette color-cycling".
- Entry points: TIG palette handling + `src/game/light.h`; the `.pal` cycle ranges.
  There's a deployed `Arcanum Community Edition (Palette Cycling)` variant — check whether
  it has prior spike work to crib from before starting cold.

### 3. Organic light flicker — PARTIAL, smallest
- Make torch/fire/candle light *breathe* (modulate intensity over time). The animation
  scaffolding already exists: `LF_ANIMATING` in `src/game/light.c:415-437`. Good warm-up
  before the shadow work. See enhancement-ideas §"Flickering organic lights".

## Deferred perf follow-ups (can stay on `feature/perf-gpu-accel`, NOT the features worktree)
- **Present-skip default-on:** the idle present-skip shipped gated (`45af12fd`). Once the
  user confirms the visual playtest (no stale/black on static screens), flip it default-on
  and consider an `arcanum.cfg` key. (memory: `present_skip_state`.)
- **GPU-path present-skip:** the hardware twin of the above — gate the GPU composite/present
  on dirty (`docs/perf-next-steps.md` step 4).
- **Resolve-once tile blit:** hoist the 4× art-cache re-resolution out of the LERP quadrant
  loop (`src/game/tile.c:2109-2188`). Byte-identical, modest (~0.1ms), invasive — low priority.

## Pointers
- `docs/enhancement-ideas.md` — master feature roadmap (with build order).
- `docs/perf-history.md` — full cross-branch perf comparison (context for what's already fast).
- `docs/perf-next-steps.md` — ranked perf next steps + the banked decisions.
- `docs/perf-gpu-accel-findings.md` / `-plan.md` — how the GPU present path works (you'll
  composite shadows/fx into it).
- Perception-fog rebase is a *separate* tracked item — see enhancement-ideas §"Perception-fog
  rebase" (must re-home as a GPU present-time composite; memory `perception_fog_rebase_trap`).
