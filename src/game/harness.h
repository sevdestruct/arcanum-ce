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

#include <stdbool.h>

// Universal frame-timer. The main loop calls these around the per-frame draw so every
// branch is measured with one instrument (dumps the shared [zoom-perf] format to
// /tmp/arcanum-zoom-perf.log every 60 frames). Self-gating no-op unless ARCANUM_GPU_CMD
// names a command file (i.e. a harness run is active).
void harness_frame_render_begin(void); // before iso_redraw()
void harness_frame_render_end(void);   // after iso_redraw()
void harness_frame_present_end(void);  // after tig_window_display()

// Pump real game frames until the in-flight PC teleport / map transition has been
// carried out (teleport_is_pending() clears), or timeout_ms elapses. The command
// channel reads its commands at the top of gamelib_ping, BEFORE the per-module
// teleport_ping that actually performs a requested teleport -- so without settling,
// commands issued right after an in-game `tele`/`gotomap` run against the
// pre-teleport map. Calling this after teleport_do() lets the transition complete
// first, so the channel survives mid-game transitions (not just the menu->game one,
// which the synchronous mainmenu pump already covered). No-op if nothing is pending.
void harness_settle(int timeout_ms);

// True while harness_settle() is pumping frames. gpu_test_channel_tick checks this
// and returns early so the nested gamelib_ping inside the settle pump does not
// re-entrantly consume further commands.
bool harness_is_settling(void);

// The `quit` channel command enqueues TIG_MESSAGE_QUIT and calls this. The main
// loop's quit handler would otherwise open the blocking "Are you sure?" confirm
// modal -- which runs its own pump (no gamelib_ping), so the channel can't drive
// its OK and the run hangs. harness_request_quit() records that the OK outcome is
// pre-chosen; harness_wants_quit() lets the main loop skip the modal and exit. A
// scripted run has no progress to protect, so the confirm is pure friction here.
void harness_request_quit(void);
bool harness_wants_quit(void);

#endif // ARCANUM_HARNESS

#endif /* ARCANUM_GAME_HARNESS_H_ */
