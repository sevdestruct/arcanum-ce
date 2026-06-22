#ifndef ARCANUM_GAME_TILE_H_
#define ARCANUM_GAME_TILE_H_

#include <stdint.h>

#include "game/context.h"

bool tile_init(GameInitInfo* init_info);
void tile_exit(void);
void tile_resize(GameResizeInfo* resize_info);
void tile_update_view(ViewOptions* view_options);
void tile_toggle_visibility(void);
void tile_draw(GameDrawInfo* draw_info);
int tile_id_from_loc(int64_t loc);
tig_art_id_t tile_art_id_at(int64_t loc);
bool tile_is_blocking(int64_t loc, bool a2);
bool tile_is_soundproof(int64_t loc);
bool tile_is_sinkable(int64_t loc);
bool tile_is_slippery(int64_t loc);
void sub_4D7430(int64_t loc);
tig_art_id_t sub_4D7480(tig_art_id_t art_id, int num2, bool flippable2, int a4);
void sub_4D7590(tig_art_id_t art_id, TigVideoBuffer* video_buffer);
void tile_set_render_target(TigVideoBuffer* vb);

// CE (feature/perf-gpu-accel): read + reset the accumulated GPU bridge
// transfer timings (CPU->GPU upload, GPU->CPU readback) in nanoseconds.
// Used by the F9 perf log to break down the GPU tile pass cost.
void tile_gpu_perf_read_reset(uint64_t* upload_ns, uint64_t* readback_ns);

// CE (feature/perf-gpu-accel): dump the current iso world buffer to an absolute
// BMP path (self-test harness; compares gpu vs software world renders).
void tile_gpu_test_capture(const char* abs_path);

// CE (feature/perf-gpu-accel): arm the GPU dispatch trace for the next world
// pass. Self-disables after one frame with real content (re-arms across empty
// frames). The harness `trace` command calls this directly so it doesn't have
// to race a file marker against the tile pass timing.
void tile_gpu_trace_arm(void);

// CE (feature/perf-gpu-accel): shared GPU blit dispatch for the world passes
// (tile/object/roof). Returns true if the blit was handled -- drawn on the GPU
// world target, or queued for CPU replay at tile_gpu_world_end -- and false if
// the GPU world pass is inactive and the caller should perform its own blit.
bool tile_gpu_dispatch(TigArtBlitInfo* art_info);

// CE (feature/perf-gpu-accel): open/close the GPU world render pass.
// gamelib_draw_game calls begin before the tile pass and end after the last GPU
// world pass (object/roof); between them tile/object/roof blits route to the
// shared GPU world target. No-op in software mode.
void tile_gpu_world_begin(void);
void tile_gpu_world_end(void);

// CE (step 6): roof present-layer pass (gpu-present only). begin binds a cleared
// transparent roof texture and returns true if roofs should draw through
// tile_gpu_dispatch (the caller renders ALL visible roofs); end registers the roof
// texture as the flip-time layer. Returns false outside gpu-present -- the caller
// then draws roofs the normal (software) way.
bool tile_gpu_world_roof_begin(void);
void tile_gpu_world_roof_end(void);

// CE (feature/perf-gpu-accel): true when the GPU world path is selected, so
// object_draw emits COLOR_CONST lighting (the hardware path) for GPU tinting.
bool tile_gpu_world_lighting(void);

#define TILE_X(tile) ((tile) & 0x3F)
#define TILE_Y(tile) (((tile) >> 6) & 0x3F)
#define TILE_MAKE(x, y) ((x) | ((y) << 6))

#endif /* ARCANUM_GAME_TILE_H_ */
