# Handoff — visual + asset features (fresh worktree)

Paste this to start a new session. `feature/perf-gpu-accel` stays focused on **perf +
harness**; the **visual/asset feature** work below runs in its **own worktree** off that
branch so the two don't tangle. Cold-start brief — assumes no memory of the session that
wrote it.

## Setup (do this first)

```bash
# from the main repo root. Base on feature/perf-gpu-accel (it has the GPU present path you'll
# composite fx into). If perf has already merged into feature/ui-improvements by now, base on
# ui-improvements instead.
git worktree add ../arc-features -b feature/visual-fx feature/perf-gpu-accel
cd ../arc-features

# Build WITH the harness (you need the capture/A-B channel for pixel diffs). The flag is not
# in any preset — pass it explicitly. ALWAYS Release (Debug is 5-10× slower).
cmake -S . -B out/build/macos-arm64 -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64 -DARCANUM_HARNESS=ON
cmake --build out/build/macos-arm64 --config Release --target arcanum-ce

# Deploy to a branch-named app so it doesn't clobber other variants. Shared data/modules/custom
# live at ~/Applications/Arcanum/ beside the .apps.
rsync -a --delete "out/build/macos-arm64/Release/Arcanum Community Edition.app/Contents/" \
  "$HOME/Applications/Arcanum/Arcanum Community Edition (Visual FX).app/Contents/"
```

## Run / test harness

```bash
caffeinate -dis &                              # display sleep wedges the GL vsync swap
cd "$HOME/Applications/Arcanum"                # so the binary finds ./data, ./modules
ARCANUM_GPU_CMD=/abs/path/cmd.txt \
  "./Arcanum Community Edition (Visual FX).app/Contents/MacOS/arcanum-ce" \
  -window -ApplePersistenceIgnoreState YES
# one-time, avoids the post-SIGKILL window-restore hang:
#   defaults write com.alexbatalov.arcanum-ce ApplePersistenceIgnoreState -bool YES
#   rm -rf "$HOME/Library/Saved Application State/com.alexbatalov.arcanum-ce.savedState"
```

- The gpucmd channel (read every frame, one cmd/line) lives in `gpu_test_channel_tick`
  (`src/game/gamelib.c`); add your own commands there (it is `#if defined(ARCANUM_HARNESS)`'d).
  Useful ones: `loadsave <Slot>`, `newgameat <map> <x> <y>`, `wait <frames>`,
  `setpath <software|hardware>`, `scrollto <x> <y>`, `setzoom <f>`, `capture <abs.bmp>`,
  `perf`, `quit`. Full list + launch gotchas: `docs/arbiter-harness.md`.
- **Lit test scenes** (saves in `~/Applications/Arcanum/data/Save/`, shared across variants;
  load on this post-rework base): `Slot0013` Town (day, outdoor), `Slot0014` Indoor-Daytime,
  `Slot0015` Night, `Slot0016` Under-Roof, `Slot0012` Blimp. No save? `newgameat 1 90234 84162`
  (Shrouded Hills, daytime town) spawns a premade PC at a heavy outdoor scene.
- **Visual A/B:** `capture` two BMPs at the *same* scripted position → diff with
  `tools/gpu_test/diff_bmp.py` (max/mean delta + heatmap PNG). Use a sprite-free terrain
  rectangle to isolate a static effect from NPC animation.

## The master roadmap

`docs/enhancement-ideas.md` is the authoritative feature roadmap — every item below has a
fuller write-up there with `IMPLEMENTED (dormant)` / `ABSENT` / `FEASIBLE` / `PARTIAL` status
and the engine-readiness analysis. **Read it first. Don't duplicate it; extend it.**

## Features, in proposed build order (pick per the user)

### 1. Directional shadows — the marquee (≈1-2 day)
- **Object/critter cast shadows are IMPLEMENTED but DORMANT.** Infra in `src/game/light.c`
  (`shadow_init`/`shadow_exit`/`shadow_apply`/`shadow_node_*`, `shadow_node_head`, the
  `shadows_changed` hook) + `light.h`. Point-light driven, gated off (`SHADOWS_KEY`).
- **Prerequisite (ABSENT): a sun/moon azimuth model** to drive *directional* outdoor shadows
  (enhancement-ideas §"Sun / moon azimuth model"). Without it you only get point-light shadows.
- **GPU gotcha (already learned):** if you blend shadows on the GPU, Metal/SDL silently drops a
  custom SUB blend mode whose alpha op ≠ color op → solid-black shadows. SUB must use
  `REV_SUBTRACT` for *both* channels. (memory `gpu_metal_sub_blend_op_match`.)
- Start: enable the dormant path, get point-light shadows rendering, then add the sun model.

### 2. Palette color-cycling — FEASIBLE, no new sprites
- Animate water / lava / fire / magic-glow by cycling palette indices on the existing static
  art — zero new assets. enhancement-ideas §"TIG palette color-cycling".
- Entry points: TIG palette handling + `src/game/light.h`; the `.pal` cycle ranges. A deployed
  `Arcanum Community Edition (Palette Cycling)` variant exists — check it for prior spike work
  before starting cold.

### 3. Organic light flicker — PARTIAL, smallest (good warm-up)
- Make torch/fire/candle light *breathe* (modulate intensity over time). Animation scaffolding
  exists: `LF_ANIMATING` in `src/game/light.c:415-437`. enhancement-ideas §"Flickering organic
  lights".

### 4. Sprite compositing subsystem — ABSENT, infra-level (memory `sprite_hacking_system_plan`)
- An engine-side API to slice / flip / scale / mask / layer `.dat` art into named runtime
  assets. The blit primitives exist; there is **no runtime composite-art API** yet. This is the
  foundation the coin-slot work (below) and future custom UI art want. Keep the `.dat`
  engine/art separation intact (memory `asset_dat_separation_installer` — original replacement
  art will likely need an installer; deferred).

### 5. Coin-slot resize — depends on #4 (memory `coin_slot_resize_plan`)
- Quantity-based gold slot sizing (`<100` = 1 slot, `<500` = 2, `≥500` = 4) via variant coin
  sprites. Inventory slots are pixel-size-driven by `item_inv_icon_size`. Needs the sprite
  compositing API (#4) to generate the variant coin sprites at runtime.

## Don't do these here (they belong on the perf branch)
Resolve-once tile blit, GPU present-skip dirty-gate, the GPU object pass / palette lighting —
all tracked in `docs/handoff-2026-06-27.md` + `docs/perf-next-steps.md`. Idle present-skip is
already shipped default-on, so don't re-spike it.

## Pointers
- `docs/enhancement-ideas.md` — master feature roadmap (with build order + readiness status).
- `docs/arbiter-harness.md` — harness command reference + the `capture` A/B flow.
- `docs/handoff-2026-06-27.md` — the perf/harness branch's own forward brief.
- `docs/perf-gpu-accel-findings.md` / `-plan.md` — how the GPU present path works (you'll
  composite shadows/fx into it).
- Perception-fog is a *separate* tracked item — enhancement-ideas §"Perception-fog rebase"
  (must re-home as a GPU present-time composite; memory `perception_fog_rebase_trap`).
