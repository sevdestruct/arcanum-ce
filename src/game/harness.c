#include "game/harness.h"

// Entire body gated -- when ARCANUM_HARNESS is undefined this is an empty translation
// unit, so the file can stay in the always-compiled source list with zero ship cost.
#if defined(ARCANUM_HARNESS)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <tig/core.h>     // tig_ping()
#include <tig/timer.h>    // tig_timer_now / tig_timer_elapsed
#include <tig/window.h>   // tig_window_display()

#include "game/gamelib.h"  // gamelib_zoom_perf_is_enabled() — emitter dedupe; gamelib_ping()
#include "game/teleport.h" // teleport_is_pending / teleport_is_teleporting
#include "game/tile.h"     // tile_gpu_test_capture() — spike capture
#include "ui/iso.h"        // iso_redraw()

// Auto-capture-on-spike state (see harness_set_spike_capture).
static double harness_spike_ms = 0.0;
static int harness_spike_max = 0;
static int harness_spike_count = 0;

void harness_set_spike_capture(double ms, int max)
{
    harness_spike_ms = ms;
    harness_spike_max = max;
    harness_spike_count = 0;
}

// Frame-timer accumulators (moved verbatim from main.c). bench_on latches once from
// ARCANUM_GPU_CMD so the timer is inert outside a harness run.
static int bench_init = 0, bench_on = 0, bench_n = 0;
static unsigned long long bench_rsum = 0, bench_rmax = 0, bench_wsum = 0,
    bench_fsum = 0, bench_fmax = 0, bench_prev = 0;
static double bench_fsq = 0.0;
static unsigned long long bench_r0 = 0, bench_r1 = 0, bench_r2 = 0;

static unsigned long long harness_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void harness_lazy_init(void)
{
    if (!bench_init) {
        bench_init = 1;
        const char* be = getenv("ARCANUM_GPU_CMD");
        bench_on = (be != NULL && be[0] != '\0') ? 1 : 0;
    }
}

void harness_frame_render_begin(void)
{
    harness_lazy_init();
    if (bench_on) {
        bench_r0 = harness_now_ns();
    }
}

void harness_frame_render_end(void)
{
    if (bench_on) {
        bench_r1 = harness_now_ns();
    }
}

void harness_frame_present_end(void)
{
    if (!bench_on) {
        return;
    }
    // CE: the rich gamelib zoom-perf instrument writes the same [zoom-perf]
    // log. When it is toggled on (F9 / `perf` gpu-cmd) it is authoritative —
    // stand this universal timer down so the two never double-write the log.
    // Clear prev so the resumed frame-delta is clean, not a gap-sized spike.
    if (gamelib_zoom_perf_is_enabled()) {
        bench_prev = 0;
        return;
    }
    bench_r2 = harness_now_ns();
    unsigned long long rdr = bench_r1 - bench_r0;
    unsigned long long win = bench_r2 - bench_r1;

    // Auto-capture-on-spike: dump the just-rendered iso buffer when this frame's
    // render time crosses the armed threshold (capped at harness_spike_max).
    if (harness_spike_ms > 0.0
        && harness_spike_count < harness_spike_max
        && (double)rdr / 1e6 >= harness_spike_ms) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/arcanum-spike-%d.bmp", harness_spike_count);
        tile_gpu_test_capture(path);
        fprintf(stderr, "[harness] spike capture %s: render %.2fms >= %.2fms\n",
            path, (double)rdr / 1e6, harness_spike_ms);
        harness_spike_count++;
    }

    bench_rsum += rdr;
    if (rdr > bench_rmax) {
        bench_rmax = rdr;
    }
    bench_wsum += win;
    if (bench_prev != 0) {
        unsigned long long fd = bench_r2 - bench_prev;
        bench_fsum += fd;
        if (fd > bench_fmax) {
            bench_fmax = fd;
        }
        double fm = (double)fd / 1e6;
        bench_fsq += fm * fm;
    }
    bench_prev = bench_r2;
    if (++bench_n >= 60) {
        double favg = (double)bench_fsum / 60.0 / 1e6;
        double var = bench_fsq / 60.0 - favg * favg;
        if (var < 0) {
            var = 0;
        }
        double sd = var;
        if (sd > 0) {
            double g = sd;
            int i;
            for (i = 0; i < 24; i++) {
                g = 0.5 * (g + sd / g);
            }
            sd = g;
        }
        FILE* bf = fopen("/tmp/arcanum-zoom-perf.log", "a");
        if (bf != NULL) {
            fprintf(bf, "[zoom-perf] z=1.00 over 60 frames: render %.2fms, blit 0.00ms, OTHER 0.00ms (max 0.00ms), zoom-total 0.00ms (max 0.00ms), dirty 100%%, full-redraws 100%% | frame avg %.2fms max %.2fms stddev %.2fms\n",
                (double)bench_rsum / 60.0 / 1e6, favg, (double)bench_fmax / 1e6, sd);
            fclose(bf);
        }
        bench_n = 0;
        bench_rsum = 0;
        bench_rmax = 0;
        bench_wsum = 0;
        bench_fsum = 0;
        bench_fmax = 0;
        bench_fsq = 0.0;
    }
}

static bool harness_pumping = false;

bool harness_is_pumping(void)
{
    return harness_pumping;
}

// Pump one real frame -- the same core steps the main loop runs -- and return the
// iso_redraw (render) duration in ns. When force_full is set, the whole iso surface
// is invalidated first so the frame does a full redraw (representative worst-case
// render cost) rather than a cheap partial/dirty-gated one.
static unsigned long long harness_pump_one_frame(bool force_full)
{
    tig_ping();
    gamelib_ping();
    if (force_full) {
        gamelib_invalidate_rect(NULL);
    }
    unsigned long long t0 = harness_now_ns();
    iso_redraw();
    unsigned long long t1 = harness_now_ns();
    tig_window_display();
    return t1 - t0;
}

void harness_settle(int timeout_ms)
{
    if (harness_pumping) {
        return; // defensive: never nest a pump
    }
    if (!teleport_is_pending() && !teleport_is_teleporting()) {
        return; // nothing in flight
    }

    // Pump real frames so the pending teleport advances to completion exactly as it
    // would in normal play. The teleport itself (map swap + the synchronous fade in
    // tig_video_fade) is carried out inside gamelib_ping's per-module teleport_ping.
    // harness_pumping keeps the nested gamelib_ping's channel tick from consuming
    // more commands.
    harness_pumping = true;

    tig_timestamp_t start;
    tig_timer_now(&start);
    while (teleport_is_pending() || teleport_is_teleporting()) {
        harness_pump_one_frame(false);
        if (timeout_ms > 0 && tig_timer_elapsed(start) >= timeout_ms) {
            fprintf(stderr, "[harness] settle: timed out after %dms with a "
                            "teleport still in flight\n", timeout_ms);
            break;
        }
    }

    harness_pumping = false;
}

double harness_measure_render_ms(int frames)
{
    if (frames <= 0) {
        return 0.0;
    }
    bool nested = harness_pumping;
    harness_pumping = true;

    unsigned long long sum = 0;
    int i;
    for (i = 0; i < frames; i++) {
        sum += harness_pump_one_frame(true);
    }

    if (!nested) {
        harness_pumping = false;
    }
    return (double)sum / (double)frames / 1e6;
}

static int harness_fixed_dt = 0;

void harness_set_fixed_dt(int ms)
{
    harness_fixed_dt = ms;
}

int harness_fixed_dt_ms(void)
{
    return harness_fixed_dt;
}

static bool harness_quit_requested = false;

void harness_request_quit(void)
{
    harness_quit_requested = true;
}

bool harness_wants_quit(void)
{
    return harness_quit_requested;
}

#endif // ARCANUM_HARNESS
