#ifndef ARCANUM_GAME_HARNESS_H_
#define ARCANUM_GAME_HARNESS_H_

// CE arbiter harness -- the autonomous test/benchmark command channel + frame-time
// instrumentation. Compiled in ONLY when ARCANUM_HARNESS is defined (CMake option
// -DARCANUM_HARNESS=ON, default OFF). Ship/release builds omit it entirely -- zero
// footprint, no env-activatable test channel in a public binary.
//
// This header holds the harness HOOK interface that core files call (gated). The
// frame-timer lives here in harness.c; the command channel (gpu_test_channel_tick,
// gamelib.c) and the new-game spawn override (sub_5412E0, mainmenu_ui.c) are gated
// in place because they touch file-private statics. See docs/arbiter-harness.md for
// the design + the phased full-extraction plan + the multiplayer-arbiter merge.

#if defined(ARCANUM_HARNESS)

// Universal frame-timer. The main loop calls these around the per-frame draw so every
// branch is measured with one instrument (dumps the shared [zoom-perf] format to
// /tmp/arcanum-zoom-perf.log every 60 frames). Self-gating no-op unless ARCANUM_GPU_CMD
// names a command file (i.e. a harness run is active).
void harness_frame_render_begin(void); // before iso_redraw()
void harness_frame_render_end(void);   // after iso_redraw()
void harness_frame_present_end(void);  // after tig_window_display()

#endif // ARCANUM_HARNESS

#endif /* ARCANUM_GAME_HARNESS_H_ */
