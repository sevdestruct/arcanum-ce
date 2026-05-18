# Perception Fog

A Perception-gated awareness boundary, rendered as a soft elliptical fog
around the player.  Replaces the original "ScrollDist" camera leash that
broke when zoom was added.

## Background — Cain's original design

In the shipping game, the Perception stat fed a single value: `ScrollDist`,
the maximum distance the camera could pan from the player.  The derivation
([scroll.c:667](../src/game/scroll.c) — upstream, untouched):

```c
distance_tiles = Perception / 2 + 3
```

| Perception | Distance (tiles) |
|------------|------------------|
| 0          | 3                |
| 10         | 8                |
| 20         | 13 (max)         |

A low-Perception character could not pan the view far from themselves —
they literally couldn't look around the map.  Cain's design intent: the
Perception stat controls **situational awareness**, expressed mechanically
as "what you are allowed to look at."  The `+3` floor ensures even a
Perception-0 character isn't effectively blinded.

## What zoom broke

When the isometric zoom feature was added, the leash became trivially
circumventable: a low-Perception character could just zoom out and see the
entire map.  The leash is measured in **world pixels** from the player,
but zoom changes how many world pixels fit on the screen.  A 0.5× zoom
fits 4× the world area inside the same leash radius.

Hard-clamping the camera in world-pixel space alone is no longer
sufficient to honor Cain's intent.

## The replacement — two interlocking systems

Both are driven by the same `P/2+3 → 80×40 px` derivation, so a single
Perception stat governs the player's awareness consistently across
camera, zoom, and fog.

### 1. Zoom floor — structural

[`iso_zoom_update_perception_floor()`](../src/game/iso_zoom.c) runs every
frame and derives a **minimum allowable zoom** such that the visible
half-span on each axis cannot exceed the leash.  Low-Perception PCs can't
zoom out far enough to bypass the awareness boundary.

```
z_floor = max(
    (viewport_w/2) / hor_limit,
    (viewport_h/2) / vert_limit
)
```

Toggle: `zoom floor` in arcanum.cfg (default `1` = on).  Off restores the
user's configured `min zoom` and lets any character zoom anywhere.

### 2. Fog overlay — visual

When perception fog is enabled, the hard camera leash in
[`scroll_start`](../src/game/scroll.c) and
[`scroll_by`](../src/game/scroll.c) is dropped.  The player can pan the
camera anywhere on the map, but past the awareness boundary the world is
fogged.

**The fog *is* the leash, expressed in-world rather than as a UI
constraint.**  Mechanically equivalent to Cain's original; visually
diegetic instead of UI-locked.

## Derivation chain — Perception stat to ellipse on screen

```
Perception stat
   │  scroll_distance_get()             scroll.c:667    (upstream, unchanged)
   ▼
distance_tiles  =  Perception / 2 + 3
   │  scroll_perception_pixel_limits()  scroll.c:689    (added for fog)
   ▼
hor_limit  =  80 × distance_tiles       (world pixels)
vert_limit =  40 × distance_tiles       (world pixels)
   │  pfog_advance_tween()              perception_fog.c
   ▼  (per-frame lerp toward target)
pfog_displayed_hor, pfog_displayed_vert (smoothly interpolated)
   │  pfog_regenerate_mask()
   ▼
ha  =  pfog_displayed_hor  × iso_zoom_current()
va  =  pfog_displayed_vert × iso_zoom_current()    (screen pixels)
   │  per-pixel smoothstep with inner_r, outer_r
   ▼
uint8_t alpha mask, viewport-sized
```

The `80×40` multiplier matches the Arcanum tile footprint in screen
pixels (the standard 2:1 dimetric projection: 80 wide, 40 tall).  One
"Perception tile" of awareness equals one tile-quad on screen at 1.0×
zoom.

## Configuration

All keys live in `arcanum.cfg` and are loaded/saved by the standard
settings system.

| Key                          | Default | Range  | Meaning |
|------------------------------|---------|--------|---------|
| `perception fog`             | `0`     | 0/1    | Enable the fog overlay.  When on, the hard camera leash is also disabled. |
| `perception fog inner`       | `90`    | 0–200  | Inner ellipse radius as % of `ha,va`.  Pixels closer than this are fully clear. |
| `perception fog outer`       | `130`   | 0–200  | Outer ellipse radius as % of `ha,va`.  Pixels farther than this are fully fogged. |
| `perception fog alpha`       | `75`    | 0–100  | Max fog opacity (% of 255).  100 = solid wall, 50 = light tint. |
| `perception fog blur`        | `1`     | 0/1    | Apply a box blur to fog pixels.  Off = solid colour fog (cheaper but harder-edged). |
| `perception fog blur radius` | `8`     | 0–64   | Box blur kernel radius in pixels. |
| `perception fog dim`         | `50`    | 0–100  | Fog pixels are dimmed to this % of world brightness before blending. |
| `zoom floor`                 | `1`     | 0/1    | Enable the Perception-based minimum zoom (the structural half of the system). |

### What the fog *looks like* per pixel

Inside the inner ellipse: world pixel, untouched.

Beyond the outer ellipse: a sampled, blurred, dimmed copy of the world,
alpha-blended at `alpha` opacity.  Default settings give a "you can tell
something is there but can't make out detail" look — fits *Perception as
noticing things* rather than *Perception as vision radius*.

In the inner-to-outer transition band: alpha interpolates via smoothstep
(`t² × (3 − 2t)`), which prevents the boundary from looking like a hard
circle.

## Tuning guidance

- **`80×40` multiplier** (in `scroll_perception_pixel_limits`) is the
  single number that controls how much each tier of Perception "matters."
  Halving to `40×20` would double the granularity of the stat.  Not a cfg
  key — it's a deliberate design constant tied to tile geometry.
- **`inner_pct` and `outer_pct`** shape the *feel* of the boundary.
  Narrow band (e.g. 100/110) feels harsh and gamey.  Wide band (e.g.
  60/160) feels dreamlike.  Default 90/130 is a half-tile-wide fade.
- **`alpha_pct`** controls how much you can still see through the fog.
  Default 75 is "barely make out shapes."  Lower for a light tint;
  higher for a hard wall.
- **`dim_pct`** is independent of `alpha_pct`: alpha is the *blend
  amount*, dim is the *brightness of the fog pixel itself*.  100/100
  would composite the world over itself — invisible.  50/75 (default)
  composites a half-dim blur at 75% opacity.

## Animation behavior

Three sources of motion feed the fog ellipse, and all of them are
smoothed before reaching the mask:

1. **Player position** — `OBJ_F_OFFSET_X/Y` on the PC object advances
   per animation frame during walks.  `scroll_get_player_screen_pos`
   reads it directly so the fog centre glides instead of stepping.
2. **Zoom level** — `iso_zoom_current()` lerps internally at 0.25/frame
   toward `zoom_target` (`ISO_ZOOM_LERP`).  Fog reads the live value,
   so radius scales smoothly with zoom animation.
3. **Perception leash limits** — `pfog_advance_tween()` lerps
   `pfog_displayed_hor/vert` at 0.25/frame toward the live
   `scroll_perception_pixel_limits()`.  Perception changes (level-up,
   equipment, magic) produce a smooth radius growth/shrink instead of
   a snap.

All three converge over ~150 ms at 60 fps.  Per the user's intent that
"drift is acceptable," none is throttled — the regen is cheap enough
(see Performance below) to run every frame.

## Performance

Per-frame cost on a 1080p viewport at default config:

| Stage             | Cost (approx) | Notes |
|-------------------|---------------|-------|
| Mask regenerate   | 0.5–3 ms      | Squared-distance fast path skips `sqrtf` for ~90% of pixels.  Outer-bbox-limited iteration with `memset` bulk-fill outside. |
| Horizontal blur   | 3–5 ms        | Separable, sliding-window — O(W·H) regardless of kernel radius. |
| Vertical blur+dim | 3–5 ms        | Same algorithm, row-major write order for cache. |
| Composite         | 2–4 ms        | Per-pixel alpha mask lookup + LERP, or `tig_video_buffer_blit_alpha_mask` in the masked variant. |

A **solid-dark fast path** (blur disabled) folds the composite and dim
into one multiplier (`out = game × (25500 − α·(100−dim)) / 25500`),
skipping the intermediate fog buffer entirely.  Useful for low-spec
preset.

## Implementation notes

- [`src/game/perception_fog.c`](../src/game/perception_fog.c) and
  [`.h`](../src/game/perception_fog.h) — the module.  Registered as a
  `GameLibModule` in [`gamelib.c`](../src/game/gamelib.c) so it gets
  init/exit/resize lifecycle.
- [`src/game/scroll.c`](../src/game/scroll.c) —
  `scroll_perception_pixel_limits` and `scroll_get_player_screen_pos`
  are the perception-fog-specific additions; the rest of scroll.c is
  Cain's original camera-leash logic, gated on
  `perception_fog_is_enabled()` so it's bypassed when fog is on.
- [`src/game/iso_zoom.c`](../src/game/iso_zoom.c) —
  `iso_zoom_update_perception_floor` is the structural half of the
  replacement (the zoom-out exploit defence).
- Mask is composited onto the world frame **after** scaling but
  **before** HUD ([`gamelib_draw`](../src/game/gamelib.c)), so floating
  text and turn-based UI overlays stay sharp and unfogged.
- A full-viewport invalidate is forced every frame the fog is active.
  Without it, the incremental dirty-rect system would reapply fog over
  already-fogged pixels and produce progressive darkening artefacts.

## Future possibilities

- **Diegetic light sources** that locally extend the awareness ellipse
  (torches, fires, magic).  Would need to switch from a single
  player-centred ellipse to a union-of-ellipses mask.
- **Tile memory** ("fog of war" sense): tiles you've previously visited
  stay dim but not blacked-out.  Would need persistent per-tile state.
- **Per-PC awareness** in party play: if the party splits, mask becomes
  the union of each member's ellipse.  Trivial to add — just iterate
  the party in `pfog_regenerate_mask`.
- **A `tig_video_buffer_blit_alpha_mask` upstream contribution** — the
  function already exists on the `feature/perception-fog-tig-masked`
  branch, CE-tagged.  Worth proposing to Alex if other features (light
  cones, vision spells, dialogue dim) end up wanting per-pixel-alpha
  composites.
