# Arcanum-CE Enhancement Ideas & Engine-Readiness Roadmap

Working notes for future feature work — visual/lighting/FX enhancements and the
engine state behind each. Compiled from a multi-agent code-audit of the
`feature/perf-gpu-accel` branch (8 agents, static analysis + adversarial critique).

> **Confidence caveat:** the audit was **read-only static analysis** — line/symbol
> references are real, but the perf cost estimates are *not* yet measured. Before
> committing engineering effort, profile the relevant path (see "Profile-first
> mandate" at the end). Treat effort/risk as planning estimates, not gospel.

---

## TL;DR — what already exists vs what's net-new

| Feature | State | Effort | One-line |
|---|---|---|---|
| Object/critter cast shadows | **implemented, OFF** | S–M | Works (point-light driven); needs a *sun/moon* source + default-on |
| Sun/moon azimuth model | **absent** | M | Hard prerequisite for any global/directional shadow |
| Wall/building shadows | absent | L (2–4 wk) | Hardest; projection geometry + tile z-order + roof-fade interaction |
| Sprite ambient occlusion | absent | S–M | No depth buffer → approximate with contact-shadow disks |
| Flickering organic lights | **partial** | M | Light infra exists; needs an intensity/color *modulation* layer |
| Magic casting light | **exists, static** | S | Already attaches `OBJ_F_OVERLAY_LIGHT`; make it pulse |
| Weather (rain/snow/fog) | absent | M–L | Two layers: ambient color bias (cheap) + particle post-pass |
| Bloom / glow | absent | S (fake) / M–L (real) | Fake halos cheap; real bloom needs a render-target chain |
| Water/glass/mirror reflections | absent | M | Mirrored-flipped-alpha draw + a reflective-region data layer |
| TIG palette color-cycling | absent (feasible!) | S–M | **Doable without new sprites** — engine is palette-indexed end-to-end |

---

## Recommended build order

Dependency- and risk-aware sequence (from the synthesis + critique):

1. **Color-cycling** — lowest engine risk, no new art, immediate visual payoff (water/lava/glow). Builds the *palette-band annotation layer* that other features reuse.
2. **Flickering lights** — reuses the ~20Hz ambient-relight throttle + change-detect gate.
3. **Object shadows ON** — flip the existing system on (delivers *lamp* shadows immediately).
4. **Sun/moon model** — the missing prerequisite. Build the astronomical source.
5. **Sun-driven directional shadows** — feed the sun into the existing `shadow_apply` pipeline.
6. **Sprite AO** — contact-disk approximation (option 1; reuses the shadow path).
7. **Fake bloom** — per-light additive halos (no post pipeline).
8. **Reflections** — mirrored draw pass + region data.
9. **Wall/building shadows** — the hard one; projection + multi-tile clipping.
10. **Real post-process bloom** — introduces the render-target chain (also unblocks future shaders).

> **Cross-cutting rule:** every per-present-frame feature (flicker re-tint, reflection
> double-draw, bloom blur, weather post-pass) must be **budgeted against the present/
> vsync window** before shipping. The perf work just spent effort deleting per-frame
> full-screen costs; an unbudgeted feature can erase that win. Gate each behind a cfg key.

---

## Shadows & AO

### Object / critter directional cast shadows — IMPLEMENTED (dormant)
- **Where:** `light.c:shadow_apply` (0x4D9B20, ~line 833); drawn in `object.c:object_draw` (~820–882); shadow art bound at proto level; `shadowmap.bmp` for falloff.
- **State:** Fully working **on CPU and GPU**, but **OFF by default** (`SHADOWS_KEY="0"`, `light.c:216`). The 32-direction critter shadow sprite sets (`med_critter_s*`, `PlayerShadow*`) ship in the assets. GPU already handles `SUB + PALETTE_OVERRIDE` one-shot (`tile.c:725–791`). **So: Alex ported it; it's just turned off.**
- **The catch (per critique):** it's **point-light driven**, not sun/moon. It enumerates nearby `Light` objects and derives a rotation frame per light. Outdoors with no nearby lamp you get only the flat ambient silhouette (frame 2), gated at `light.c:875`. So "just turn it on" = **lamp shadows only**, not the global sun shadows you want.
- **Build the sun version (S–M, ~1–2 days *engine plumbing*; +M for the sun model):** synthesize ONE extra `Shadow` in `shadow_apply` whose rotation = quantized sun azimuth (`sub_504730` already supports 32 rotations) and whose darkness/length = f(sun elevation). Scale Y of the silhouette `dst_rect` for long low-sun shadows. No new critter art needed.

### Sun / moon azimuth model — ABSENT (prerequisite)
- The day/night system (`light_scheme.c`) stores only **24 hourly ambient RGB triples** — no azimuth/elevation anywhere. Any global/directional shadow, god-rays, or directional AO needs a **new sun-position model** (time → azimuth+elevation; optionally season/latitude). This is the real cost the "reuse the pipeline" framing hides. Build it once; shadows + future directional lighting reuse it.

### Wall / building cast shadows — ABSENT (hard)
- No mechanism projects a wall footprint as a shadow. Hard obstacle: walls participate in the roof-fade / see-through occlusion model (`roof_is_covered_loc`, `object.c:668`) and tile z-order (both historically fragile — see `deferred_wall_fixes`). A cast shadow must draw on the **ground tiles it falls on**, not the wall's tile — which the per-object enqueue model doesn't express.
- **Two approaches:** (a) **asset-driven** — author per-wall directional shadow sprites, emit at ground z-order (`shadow_order` group exists, `object.c:564`); ~1–2 wk incl. art. (b) **projection-based** — compute the projected ground quad per sun vector, SUB-blend a darkened fill clipped to floor tiles; truer, 3+ wk. Both need a **new ground-anchored shadow pass**. Reuse `sub_4DC210`'s indoor/outdoor classification (lines 1124–1142) to suppress interior walls.

### Sprite ambient occlusion — ABSENT (approximate)
- No depth/normal buffers → **true SSAO infeasible**. Approximate:
  - **(1) Contact-shadow disks (S–M):** extend the always-on base critter silhouette into a configurable soft AO disk under `OBJ_TYPE_SCENERY/PORTAL` too, darkness from local-light grayscale (`sub_4D9240`). This is "shadow feature (a)" minus directionality.
  - **(2) Edge-AO via corner colors (closer to L per critique):** multiply a darker `COLOR_LERP` into sprite edges adjacent to occluders — but the per-object enqueue model has **no neighbor-sprite query at blit time**, so adjacency detection is the real work. Lower priority.

---

## Lighting / FX / Weather

### Flickering organic lights — PARTIAL
- **State:** the light model is "light = glow sprite": samples its own art bitmap, multiplies by `tint_color`, add/sub-blends per vertex. The **only** built-in animation is art-frame cycling (`light_inc_frame`, `light.c:1617`). `light->r/g/b` and `tint_color` are **static** — no time-varying intensity/color, no phase/seed/waveform.
- **Magic light exists but static:** `animfx.c:1174` parses `Light:`/`Light Color:`, attaches `OBJ_F_OVERLAY_LIGHT` via `object_set_overlay_light` (`animfx.c:580`). It's a static-color overlay sprite, not a flickering emitter.
- **Build (M):** add an optional **modulation descriptor** to `Light` (waveform=flicker/pulse/none, amplitude, frequency, per-light random phase/seed; persist via `light_read/write_dif`). Drive from a per-frame ping reusing `anim_ui.c:ambient_lighting_ping`'s ~20Hz throttle. **Gate the relight with change-detection** (round to 8-bit, compare to last-pushed, only invalidate the light's rect when it actually changes) — this is the key to staying cheap. Author flicker params in a `.mes` keyed by light art num so vanilla torches/lamps/braziers flicker with no per-instance edits. No new art (runtime multiply on existing palettes). Same waveform fields added to the animfx `Light:` parse let spell glows pulse.

### Weather systems — ABSENT
- Zero weather code. Two cleanly separable layers:
  - **(1) Ambient weather (cheap, high impact):** a weather state (clear/overcast/rain/snow/fog) that **biases the day/night target colors** — hook into `light_scheme_set_time` before the ease step; overcast desaturates+dims and the existing change-detect relight handles it for free.
  - **(2) Visual precipitation:** a screen-space particle layer as a **full-screen post-pass in `tig_video_flip`** right after `SDL_RenderTexture` (same seam as `tig_fade_state`). Nearly free on GPU; composite into the surface on software (watch software/GPU parity — historically where bugs hide; cf. the XRGB→ARGB blend trap). Lightning = transient full-screen additive flash reusing `tig_fade_state` + a one-shot bright ambient spike. Per-map weather data-driven via sector flags or a `.mes`.

### Bloom / glow — ABSENT
- No shader pipeline; engine composites on CPU and presents one texture. Two tiers:
  - **(a) Fake bloom (S):** lights/magic are already additive glow sprites — author/generate a soft blurred halo and add under bright emitters. No post pipeline.
  - **(b) Real bloom (M–L):** add intermediate SDL render targets, bright-pass + separable box-blur (the **box-blur machinery already exists** from `void_edge_fade` and perception-fog) + additive recombine, at the `tig_video_flip` seam. GPU-only, perf-gated. This is the canonical reason to introduce a small render-target chain — which also unblocks future shader effects.

### Water/glass/mirror reflections — ABSENT
- No reflective-region concept. (Note: `OSF_FULL_REFLECTION` etc. in `obj_flags.c` are *spell mechanics*, unrelated.)
- **Build (M):** **Data** — a reflective-region marker (sector/tile flag or a per-map overlay `.mes` your patch authors). **Render** — for sprites above a reflective region, draw a second pass flipped vertically below the surface line with a downward alpha falloff (`tig_art_blit` already supports top→bottom alpha gradients, `art.h:352`) + optional blue tint (`tig_palette_modify TINT`), clipped to the region rect. Ripple = perturb the mirror offset over time via the day/night ping. Stays entirely within existing blit + palette-tint + rect-clip facilities.

### TIG palette color-cycling (no new sprites) — FEASIBLE
- **Directly answers your color-cycling thread: yes, doable without painting new sprites.** The renderer is **palette-indexed end-to-end** — each art stores a mutable 256-entry working palette per palette-slot. Cycling a **sub-range** of an existing sprite's palette indices recolors every pixel using those indices with zero new art.
- **Build (S–M engine):** (a) a small data table (`.mes` keyed by art type+num, or sidecar) naming start index, count, rate for the band to rotate; (b) each tick (reuse the ~20Hz `ambient_lighting_ping` throttle) rotate those entries in the source palette and re-run the existing lit-palette rebuild (`tig_palette_modify` + `sub_505000`); (c) invalidate just the affected art's on-screen rects.
- **The real work is asset *survey*, not code:** find which vanilla palette index bands are worth cycling (water, lava, torch glow). That's why your thread stalled on "no purpose-built assets" — but the answer is you don't need new sprites, you need to **identify good existing palette ranges**.
- **Watch the cost (per critique):** many simultaneously-cycling arts each forcing a palette rebuild + rect invalidation at 20Hz could regress the software path. Bound the number of concurrent cycling arts.

---

## My additions (cross-cutting ideas)

1. **One shared per-art annotation layer.** Color-cycling (which indices cycle), bloom (which indices are *emissive*), reflections (which regions reflect), and flicker-tinting all want metadata keyed by art/sector. Build **one** annotation system (a `.mes` your patches author) and let every feature read it — instead of four bespoke data layers.
2. **God-rays / light shafts** through windows & doorways — a cheap fake using the existing additive light-sprite blend plus the reflective-region data layer (mark the opening, project a soft additive gradient quad). Pairs naturally with the sun model.
3. **Day/night-tinted shadows** — once the sun model exists, color the shadow (warm at dawn/dusk, cool/blue at night) by sampling the day/night RGB triples that already drive ambient. Near-free, big mood payoff.
4. **Ambient particle layer reuse** — dust motes indoors, embers near fire, fireflies at night — all ride the *same* particle system built for weather precipitation. Build the particle layer once.
5. **Puddle/wet reflections after rain** — couples the weather state to the reflection system: rain → temporary reflective ground regions. Two systems, one emergent effect.
6. **Emissive-aware bloom from the annotation layer** — instead of bright-pass thresholding the whole frame, only bloom pixels whose palette index is tagged emissive. Cheaper *and* art-directed (only torches/magic/windows glow, not bright white walls).

---

## Optimization roadmap (separate but related)

> **The default cfg is `render path=software`. Confirm which path actually ships before
> ranking these — the software set and hardware set are two different deployments.**

### SIMD / NEON lighting — RESOLVED (superseded by `perf-history.md` + `perf-next-steps.md`)
- **Profile-first mandate: DONE.** The lit-terrain-vs-lit-object pixel-count profile (instrumented in `art_blit`) settled it — terrain LERP-gradient = **95%** of lit pixels, objects = 5%. The world hot path is `art.c`'s `COLOR_LERP` case (`art.c:5279`), not `tig_video_buffer_blit`.
- **`video.c` SIMD: KEEP, do NOT revert.** It's neutral on its own 5%-of-pixels path, but it's the *proven byte-identical kernel* the terrain-NEON reuses.
- **Terrain-LERP NEON: SHIPPED** (`c4b9255d`, byte-identical, **−4.5%** tile pass). The gradient-vectorization follow-up was built, proven byte-identical, then **reverted as perf-neutral** — the gradient isn't the bottleneck (gather + multiply + memory dominate; the sub-ms pass sits below the measurement noise). Per-pixel software SIMD headroom is **tapped out**.

### Software-path fixed-overhead wins (cheap, safe, no byte-identity risk)
- **Resolve-once tile blit:** the 4-quadrant LERP tile split re-resolves the same art cache entry 4× (path-build + strcmp + re-lock). Hoist resolution out of the quadrant loop. (`tile.c:2109–2188`)
- **Table-free /255:** replace the 64KB `tig_color_mul` table with the integer identity `(a*b + 0x80); (t+(t>>8))>>8`. Frees 64KB of L2 pressure. *(But measure — the cache-bound-vs-L2-resident claim is unverified; this swap is cheap enough to just A/B.)* (`color.h:58–71`)
- **Software present-skip: IMPLEMENTED (gated, `45af12fd`).** `tig_video_flip` now skips the whole flip on a static frame (no present-dirty hint, no fade/fps, software path) and `SDL_Delay(8ms)` replaces the vsync wait. Validated: ~290 skips at the static menu, ~0 in active play → a menu/dialog/battery win, inert during gameplay. Gated `ARCANUM_OPT_PRESENT_SKIP=1` / `presentskip` gpucmd; default-on pending a visual playtest. GPU-path twin is the follow-up below.
- **Threading the tile pass** (the structural lever the audit under-explored): the per-pixel scalar fill is embarrassingly parallel across dirty-rect rows; `SDL_CreateThread` infra is proven (`sound.c:129`). Needs a feasibility pass on the `art_blit` lock + `light_buffers_lock` thread-safety. Plausibly larger than any SIMD micro-opt.

### Hardware-path wins (if GPU is the deployment)
- **Metal switch: keep OpenGL FOR NOW (but the "Metal can't adaptive" claim was wrong).** GL accepts adaptive vsync via SDL (`actual-vsync=-1`, the best mode). **Correction:** Metal/macOS *does* support adaptive vsync natively (`presentAfterMinimumDuration` / `CAMetalDisplayLink` / built-in-ProMotion auto-VRR, since Monterey) — the `SDL_SetRenderVSync(-1) → "not supported"` is an **SDL-2D-renderer** limit, not a Metal one. Metal stays parked only because (a) its world pass currently renders *broken* (terrain black/torn) under the GPU present path and (b) SDL doesn't expose its native VRR. GL/Metal present is ~7ms hardware (normal vsync idle, ~120Hz day-town), not a defect; the "73fps floor" was harness frame-avg noise. A Metal path is a real **future project**: fix the world render, then plumb native Metal present-pacing (or full-screen ProMotion).
- **Present-skip dirty-gate** + scope the `RenderClear` to the letterbox — both fold together (skip the flip → skip the clear).
- Draw-call batching / art atlasing — real but **not the current bottleneck** (render is already under budget); defer.

### Profile-first mandate
Before any optimization code, get three real numbers (none were measured by the audit):
1. **F9 flip-perf split** (update vs present, `video.c:1397–1408`) in **both** render paths during a zoomed scroll → settles present-bound vs render-bound.
2. **`SDL_GetRenderVSync`** after renderer creation → validates/kills the Metal thesis.
3. **Lit-terrain vs lit-object pixel count** per frame → settles which `art.c` loop to attack.

---

## Perception-fog rebase (will need attention)
The `feature/perception-fog` branch (a Perception-stat-gated soft elliptical awareness fog — *not* classic fog-of-war) **will not survive a naive rebase onto this branch.** `perception_fog_draw` CPU-locks `gamelib_iso_window_vb`, reads back every world pixel, and writes the fogged result back — **exactly the readback the GPU present path deleted.** After rebase it would render *invisibly* (writes a bypassed buffer) while burning ~8–17ms CPU. **Re-home it as a GPU present-time composite** using the `tb/tf/tc_gpu_composite()` / `gamelib_iso_overlay_composite()` pattern this branch already established for speech bubbles. The `feature/perception-fog-tig-masked` variant (composite behind `tig_video_buffer_blit_alpha_mask`) is the cleaner seam to redirect; drop the CPU box-blur for a GPU downsample.
