#include "game/harness.h"

// Entire body gated -- when ARCANUM_HARNESS is undefined this is an empty translation
// unit, so the file can stay in the always-compiled source list with zero ship cost.
#if defined(ARCANUM_HARNESS)

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "game/gamelib.h" // gamelib_zoom_perf_is_enabled() — emitter dedupe

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

#endif // ARCANUM_HARNESS
