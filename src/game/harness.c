#include "game/harness.h"

// Entire body gated -- when ARCANUM_HARNESS is undefined this is an empty translation
// unit, so the file can stay in the always-compiled source list with zero ship cost.
#if defined(ARCANUM_HARNESS)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tig/art.h>      // tig_art_{terrain_simd,resolve_once,gpu_cache_memo}_set
#include <tig/core.h>     // tig_ping()
#include <tig/debug.h>    // tig_debug_printf()
#include <tig/message.h>  // tig_message_enqueue, TIG_MESSAGE_QUIT
#include <tig/timer.h>    // tig_timer_now / tig_timer_elapsed
#include <tig/video.h>    // tig_video_{simd_blit,present_skip,flip_perf}_*
#include <tig/window.h>   // tig_window_display()

#include "game/anim.h"     // anim_goal_run_to_tile
#include "game/gamelib.h"  // channel deps: gamelib_load/mod_load/render_path_set/zoom_perf/...
#include "game/iso_zoom.h" // iso_zoom_set_target / current / available
#include "game/light.h"    // light_invalidate_rect (setpath)
#include "game/location.h" // LOCATION_MAKE / location_origin_set
#include "game/map.h"      // map_list_info_find / map_get_starting_location / map_current_map / dump
#include "game/obj.h"      // obj_field_int64_get, OBJ_F_LOCATION, OBJ_HANDLE_NULL
#include "game/object.h"   // object_invalidate_rect (setpath)
#include "game/player.h"   // player_get_local_pc_obj
#include "game/random.h"   // random_seed (seed)
#include "game/scroll.h"   // scroll_by
#include "game/teleport.h" // teleport_do / teleport_is_pending / teleport_is_teleporting
#include "game/tile.h"     // tile_{halfres_lerp,threads}_set, tile_gpu_test_capture, tile_gpu_trace_arm
#include "game/ui.h"       // ui_gameuilib_mod_load (loadsave module switch)
#include "ui/dialog_ui.h"  // dialog_ui_is_local_pc_in_dialog / end_dialog
#include "ui/inven_ui.h"   // inven_ui_open / destroy / is_created
#include "ui/iso.h"        // iso_redraw()
#include "ui/logbook_ui.h" // logbook_ui_open / close
#include "ui/mainmenu_ui.h" // mainmenu_ui_is_active, harness_newgame[_at], sub_5412D0
#include "ui/wmap_ui.h"    // wmap_ui_* (travel map commands)

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

// Map a perf-toggle name (the same names as the standalone gpu-cmd toggles) to its
// setter, for bench-ab. Returns false for an unknown name. Keep in sync with the
// individual toggle commands below.
static bool harness_apply_toggle(const char* name, int on)
{
    if (strcmp(name, "simd") == 0) {
        tig_video_simd_blit_set(on);
        tig_art_terrain_simd_set(on);
    } else if (strcmp(name, "presentskip") == 0) {
        tig_video_present_skip_set(on);
    } else if (strcmp(name, "resolveonce") == 0) {
        tig_art_resolve_once_set(on);
    } else if (strcmp(name, "halfreslerp") == 0) {
        tile_halfres_lerp_set(on);
    } else if (strcmp(name, "tilethreads") == 0) {
        tile_threads_set(on);
    } else if (strcmp(name, "gpucachememo") == 0) {
        tig_art_gpu_cache_memo_set(on);
    } else {
        return false;
    }
    return true;
}

// The autonomous arbiter test command channel. Pumped from gamelib_ping every
// frame (menu AND in-game). Reads ARCANUM_GPU_CMD line by line; no-op unless that
// env names a readable file. Command reference: docs/arbiter-harness.md.
void harness_channel_tick(void)
{
    static const char* cmd_path = NULL;
    static long cmd_offset = 0;
    static int wait_frames = 0;
    static bool inited = false;
    if (!inited) {
        inited = true;
        cmd_path = getenv("ARCANUM_GPU_CMD");
    }
    if (cmd_path == NULL || cmd_path[0] == '\0') {
        return;
    }
    // Re-entrancy guard: harness_settle()/harness_measure_render_ms() pump
    // gamelib_ping (which calls us). Do not consume more commands while they do.
    if (harness_is_pumping()) {
        return;
    }
    if (wait_frames > 0) {
        wait_frames--;
        return;
    }

    FILE* fp = fopen(cmd_path, "r");
    if (fp == NULL) return;
    if (fseek(fp, cmd_offset, SEEK_SET) != 0) { fclose(fp); return; }

    char line[512];
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') {
            break; // partial tail; wait for next tick
        }
        cmd_offset += (long)n;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0 || line[0] == '#') continue;

        char arg[256];
        int ix, iy;
        unsigned int uix;
        float fz;
        if (sscanf(line, "loadsave %255s", arg) == 1) {
            // CE harness: auto-switch to the save's owning module before loading,
            // mirroring the Load menu (mainmenu_ui sub_5432B0). gamelib_load no longer
            // self-switches, so a cross-module `loadsave` would otherwise resolve
            // against the wrong mount. Resolve the module by directory
            // (modules\<M>\save -> M, else the default module), and if it differs from
            // what's mounted, do the gated mod_load + gameui mod_load pair. This drops
            // the need to hand-prepend a `setmodule` for cross-module test saves.
            char save_module[TIG_MAX_PATH];
            save_module[0] = '\0';
            if (gamelib_find_save_module(arg, save_module, sizeof(save_module))
                && save_module[0] != '\0'
                && SDL_strcasecmp(save_module, gamelib_loaded_module_name_get()) != 0) {
                tig_debug_printf("[gpu-cmd] loadsave: switching module %s -> %s\n",
                    gamelib_loaded_module_name_get(), save_module);
                // Bracket with loading_active so gamelib_mod_load's gameinit_reset skips
                // its throwaway fresh-game setup (else it leaks start-map mobiles into
                // the loaded save). The save's own load restores the real map/PC.
                gamelib_loading_active_set(true);
                if (gamelib_mod_load(save_module)) {
                    ui_gameuilib_mod_load();
                } else {
                    tig_debug_printf("[gpu-cmd] loadsave: module switch to '%s' FAILED\n", save_module);
                }
                gamelib_loading_active_set(false);
            }
            tig_debug_printf("[gpu-cmd] loadsave %s\n", arg);
            if (!gamelib_load(arg)) {
                tig_debug_printf("[gpu-cmd] loadsave FAILED\n");
            } else if (mainmenu_ui_is_active()) {
                // The real menu load flow (mainmenu_ui sub_5432B0) calls sub_5412D0()
                // after gamelib_load to dismiss the menu and enter the game. The harness
                // skipped that, so the world rendered UNDER the still-open main menu --
                // the menu stayed on top (invisible game). Run the same transition so the
                // harness is actually watchable.
                sub_5412D0();
                tig_debug_printf("[gpu-cmd] dismissed mainmenu -> in-game\n");
            }
        } else if (strncmp(line, "newgameat ", 10) == 0) {
            // CE harness: new game spawning the premade PC at map+(x,y) -- a heavy town
            // scene -- via the synchronous menu->game transition (channel-safe on every
            // branch, unlike an in-game tele). Checked before "newgame" (prefix match).
            int ngmap; long long ngx = 0, ngy = 0;
            if (sscanf(line, "newgameat %d %lld %lld", &ngmap, &ngx, &ngy) == 3) {
                tig_debug_printf("[gpu-cmd] newgameat map %d @ (%lld,%lld)\n", ngmap, ngx, ngy);
                mainmenu_ui_harness_newgame_at(1, ngmap, (int64_t)ngx, (int64_t)ngy);
            }
        } else if (strncmp(line, "newgame", 7) == 0) {
            // CE harness: start a new game with a premade character -- a save-format-
            // independent real-level workload for cross-branch benchmarking. Optional
            // index picks the premade (default 1 = Merwin). Lands in the start map.
            ix = 1;
            sscanf(line, "newgame %d", &ix);
            tig_debug_printf("[gpu-cmd] newgame pregen=%d\n", ix);
            mainmenu_ui_harness_newgame(ix);
        } else if (strncmp(line, "dlgclose", 8) == 0) {
            // CE harness: dismiss the intro dialogue (base Arcanum opens in one) so the
            // workout can scroll/walk/zoom freely.
            if (dialog_ui_is_local_pc_in_dialog()) {
                dialog_ui_end_dialog(player_get_local_pc_obj(), 0);
                tig_debug_printf("[gpu-cmd] dlgclose: dismissed dialog\n");
            }
        } else if (strncmp(line, "gotomap ", 8) == 0) {
            // CE harness: teleport the PC to a named map's starting location, e.g.
            // `gotomap Shrouded Hills` -- a HEAVY town scene for benchmarking, reached
            // after `newgame` so it's save-format independent and works on every branch
            // (teleport_do is core engine). map_list_info_find resolves the name to a
            // 0-based index; the map id is index+1.
            {
                const char* mapname = line + 8;
                int gmidx = map_list_info_find(mapname);
                if (gmidx >= 0) {
                    int gmap = gmidx + 1;
                    int64_t gx = 0, gy = 0;
                    if (map_get_starting_location(gmap, &gx, &gy)) {
                        TeleportData td;
                        memset(&td, 0, sizeof(td));
                        td.flags = TELEPORT_FADE_IN;
                        td.obj = player_get_local_pc_obj();
                        td.loc = LOCATION_MAKE(gx, gy);
                        td.map = gmap;
                        teleport_do(&td);
                        // Let the transition complete before the next command runs
                        // (teleport_ping carries it out AFTER this channel tick).
                        harness_settle(8000);
                        tig_debug_printf("[gpu-cmd] gotomap '%s' -> map %d @ (%lld,%lld)\n",
                            mapname, gmap, (long long)gx, (long long)gy);
                    } else {
                        tig_debug_printf("[gpu-cmd] gotomap '%s': no start loc\n", mapname);
                    }
                } else {
                    tig_debug_printf("[gpu-cmd] gotomap '%s': not found\n", mapname);
                }
            }
        } else if (strncmp(line, "maplist", 7) == 0) {
            // CE harness: dump the loaded map list (id, name, start coords) so a
            // scenario author can find ids/names for tele/gotomap. id = index+1.
            map_list_info_dump();
            tig_debug_printf("[gpu-cmd] maplist dumped\n");
        } else if (strncmp(line, "wherepc", 7) == 0) {
            // CE harness: report the PC's current map + tile coords (to capture a
            // heavy scene's location from a save, then teleport there after newgame).
            int64_t ploc = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
            tig_debug_printf("[gpu-cmd] wherepc: map=%d loc=(%lld,%lld)\n",
                map_current_map(), (long long)LOCATION_GET_X(ploc), (long long)LOCATION_GET_Y(ploc));
        } else if (strncmp(line, "tele ", 5) == 0) {
            // CE harness: teleport the PC to map + tile coords, e.g. the Shrouded Hills
            // town center on the overworld -- a heavy object-dense scene, save-independent.
            int tmap; long long tx = 0, ty = 0;
            if (sscanf(line, "tele %d %lld %lld", &tmap, &tx, &ty) == 3) {
                TeleportData td;
                memset(&td, 0, sizeof(td));
                td.flags = TELEPORT_FADE_IN;
                td.obj = player_get_local_pc_obj();
                td.loc = LOCATION_MAKE((int64_t)tx, (int64_t)ty);
                td.map = tmap;
                teleport_do(&td);
                // Let the transition complete before the next command runs
                // (teleport_ping carries it out AFTER this channel tick).
                harness_settle(8000);
                tig_debug_printf("[gpu-cmd] tele map %d @ (%lld,%lld)\n", tmap, tx, ty);
            }
        } else if (sscanf(line, "setpath %255s", arg) == 1) {
            gamelib_render_path_set(arg);
            // CE harness: a runtime render-path switch must recompute cached
            // object render flags. The COLOR_CONST-vs-PALETTE_OVERRIDE choice
            // (object lighting path) depends on the render path, but is cached
            // per object (ORF_02000000). Without clearing it, switching to
            // software keeps GPU-style COLOR_CONST flags, which the software
            // blitter renders through the ambient-darkened WORKING palette
            // (instead of PALETTE_OVERRIDE's original palette) -> objects come
            // out too dark, making the software capture an unfaithful reference.
            // The real game only sets the path at startup so it never hits this.
            light_invalidate_rect(NULL, true);
            object_invalidate_rect(NULL);
            gamelib_invalidate_rect(NULL);
            tig_debug_printf("[gpu-cmd] setpath %s\n", arg);
        } else if (sscanf(line, "wait %d", &ix) == 1) {
            wait_frames = ix;
            tig_debug_printf("[gpu-cmd] wait %d\n", ix);
            break; // honour wait immediately
        } else if (sscanf(line, "capture %255s", arg) == 1) {
            tile_gpu_test_capture(arg);
            tig_debug_printf("[gpu-cmd] capture %s\n", arg);
        } else if (sscanf(line, "scrollto %d %d", &ix, &iy) == 2) {
            int64_t loc = LOCATION_MAKE(ix, iy);
            location_origin_set(loc);
            gamelib_invalidate_rect(NULL);
            tig_debug_printf("[gpu-cmd] scrollto %d %d\n", ix, iy);
        } else if (sscanf(line, "scrollby %d %d", &ix, &iy) == 2) {
            // profiling §4: RELATIVE scroll -> drives scroll_by, the actual edge-strip /
            // GPU-world-translate path (scrollto/location_origin_set is absolute and
            // bypasses scroll_by). Wake the render with a TINY (non-full) invalidate so
            // the camera-move block runs without masking the translate behind a full
            // re-render (gamelib_invalidate_rect(NULL) would force exactly that).
            scroll_by(ix, iy);
            TigRect wake = { 0, 0, 1, 1 };
            gamelib_invalidate_rect(&wake);
            tig_debug_printf("[gpu-cmd] scrollby %d %d\n", ix, iy);
        } else if (sscanf(line, "walkby %d %d", &ix, &iy) == 2) {
            // harness: make the PC actually WALK dx,dy tiles from its current loc. With
            // camera-follow on, the camera then scrolls naturally as it walks -- real
            // movement + scrolling, not a camera snap.
            int64_t pc = player_get_local_pc_obj();
            if (pc != OBJ_HANDLE_NULL) {
                int64_t loc = obj_field_int64_get(pc, OBJ_F_LOCATION);
                int64_t tgt = LOCATION_MAKE(LOCATION_GET_X(loc) + ix, LOCATION_GET_Y(loc) + iy);
                anim_goal_run_to_tile(pc, tgt);
                tig_debug_printf("[gpu-cmd] walkby %d %d -> (%lld,%lld)\n", ix, iy,
                    (long long)LOCATION_GET_X(tgt), (long long)LOCATION_GET_Y(tgt));
            } else {
                tig_debug_printf("[gpu-cmd] walkby: no PC\n");
            }
        } else if (sscanf(line, "wmapscroll %d %d", &ix, &iy) == 2) {
            // harness: scroll the OPEN travel map by dx,dy (drives wmap_void_feather
            // each step) so the wmap fade can be measured/A-B'd like the iso path.
            if (wmap_ui_is_created()) {
                wmap_ui_scroll_test(ix, iy);
                tig_debug_printf("[gpu-cmd] wmapscroll %d %d\n", ix, iy);
            } else {
                tig_debug_printf("[gpu-cmd] wmapscroll: wmap not open\n");
            }
        } else if (strncmp(line, "wmapclose", 9) == 0) {
            wmap_ui_close();
            tig_debug_printf("[gpu-cmd] wmap close\n");
        } else if (sscanf(line, "wmapcap %255s", arg) == 1) {
            // harness: dump the wmap window to a BMP for full-res-vs-optimized diff.
            wmap_ui_capture_test(arg);
            tig_debug_printf("[gpu-cmd] wmapcap %s\n", arg);
        } else if (sscanf(line, "wmaphalf %d", &ix) == 1) {
            // harness: toggle the half-res feather at runtime (one launch A/Bs both).
            wmap_ui_set_halfres(ix);
            tig_debug_printf("[gpu-cmd] wmaphalf %d\n", ix);
        } else if (strncmp(line, "wmap", 4) == 0) {
            // harness: open the travel/world map (to drive + measure the feather).
            wmap_ui_open();
            tig_debug_printf("[gpu-cmd] wmap open (created=%d)\n", wmap_ui_is_created());
        } else if (strncmp(line, "invenclose", 10) == 0) {
            if (inven_ui_is_created()) {
                inven_ui_destroy();
            }
            tig_debug_printf("[gpu-cmd] inven close\n");
        } else if (strncmp(line, "inven", 5) == 0) {
            // harness: open the inventory window (exercises the UI render/composite).
            int64_t pc = player_get_local_pc_obj();
            if (pc != OBJ_HANDLE_NULL) {
                inven_ui_open(pc, OBJ_HANDLE_NULL, INVEN_UI_MODE_INVENTORY);
                tig_debug_printf("[gpu-cmd] inven open (created=%d)\n", inven_ui_is_created());
            } else {
                tig_debug_printf("[gpu-cmd] inven: no PC\n");
            }
        } else if (strncmp(line, "logbookclose", 12) == 0) {
            logbook_ui_close();
            tig_debug_printf("[gpu-cmd] logbook close\n");
        } else if (strncmp(line, "logbook", 7) == 0) {
            int64_t pc = player_get_local_pc_obj();
            if (pc != OBJ_HANDLE_NULL) {
                logbook_ui_open(pc);
                tig_debug_printf("[gpu-cmd] logbook open\n");
            } else {
                tig_debug_printf("[gpu-cmd] logbook: no PC\n");
            }
        } else if (sscanf(line, "setmodule %255s", arg) == 1) {
            // harness: force the active module, to test gamelib_load's save-driven
            // module auto-switch (load a save from a DIFFERENT module after this).
            if (gamelib_mod_load(arg)) {
                gamelib_current_mode_name_set(arg);
                tig_debug_printf("[gpu-cmd] setmodule %s (current=%s)\n", arg, gamelib_current_module_name_get());
            } else {
                tig_debug_printf("[gpu-cmd] setmodule %s FAILED\n", arg);
            }
        } else if (sscanf(line, "setzoom %f", &fz) == 1) {
            // profiling: drive the zoom animation toward a target (1.0 = in, 0.5 = out).
            // Force availability so it engages regardless of freshly-loaded harness state
            // (set_target resets to 1.0 when zoom isn't available).
            iso_zoom_set_available(true);
            iso_zoom_set_target(fz);
            // iso_zoom_ping (the lerp) is gated on gamelib_dirty; the idle harness is
            // clean, so wake the render to start (and sustain) the zoom animation.
            gamelib_invalidate_rect(NULL);
            tig_debug_printf("[gpu-cmd] setzoom %f (cur=%.3f avail=%d)\n",
                (double)fz, (double)iso_zoom_current(), iso_zoom_is_available());
        } else if (sscanf(line, "simd %d", &ix) == 1) {
            // profiling: runtime SIMD/scalar lighting-blit toggle so ONE launch A/Bs
            // both at the same scroll position (kills the per-launch tile-count
            // confound). Affects the software-path CPU lighting blit.
            tig_video_simd_blit_set(ix);
            tig_art_terrain_simd_set(ix); // also toggle the terrain LERP NEON path
            tig_debug_printf("[gpu-cmd] simd %d (video+terrain)\n", ix);
        } else if (sscanf(line, "presentskip %d", &ix) == 1) {
            // profiling: runtime toggle for the idle present-skip (software path).
            // Reports the running skip count so a static screen can be verified to
            // actually stop presenting (count climbs while idle, holds while active).
            tig_video_present_skip_set(ix);
            tig_debug_printf("[gpu-cmd] presentskip %d (idle skips so far: %llu)\n",
                ix, (unsigned long long)tig_video_present_skip_get_count());
        } else if (sscanf(line, "resolveonce %d", &ix) == 1) {
            // profiling: runtime toggle for the resolve-once art-cache memo so ONE
            // launch A/Bs both at the same scroll position (ambient-load robust).
            tig_art_resolve_once_set(ix);
            tig_debug_printf("[gpu-cmd] resolveonce %d\n", ix);
        } else if (sscanf(line, "halfreslerp %d", &ix) == 1) {
            // profiling: runtime toggle for the half-res-during-lerp tile skip.
            tile_halfres_lerp_set(ix);
            tig_debug_printf("[gpu-cmd] halfreslerp %d\n", ix);
        } else if (sscanf(line, "tilethreads %d", &ix) == 1) {
            // profiling: runtime toggle for the 2-thread tile pass (same-launch A/B).
            tile_threads_set(ix);
            tig_debug_printf("[gpu-cmd] tilethreads %d\n", ix);
        } else if (sscanf(line, "gpucachememo %d", &ix) == 1) {
            // profiling: runtime toggle for the GPU-cache resolve memo (same-launch A/B).
            tig_art_gpu_cache_memo_set(ix);
            tig_debug_printf("[gpu-cmd] gpucachememo %d\n", ix);
        } else if (strncmp(line, "perf", 4) == 0) {
            // profiling: toggle the F9 zoom-perf log (per-pass total + max, dumped
            // periodically to the debug log).
            gamelib_zoom_perf_toggle();
            // Skip the warmup gate so the per-pass timings (perf_on = enabled &&
            // warmed_up) capture immediately -- the harness loop doesn't drive the
            // record_tig_ping warmup counter.
            if (gamelib_zoom_perf_is_enabled()) {
                gamelib_zoom_perf_set_warmed_up(true);
            }
            // CE profiling: mirror the flip-perf split (SDL_UpdateTexture upload vs
            // SDL_RenderPresent wait) so we can tell present-bound from render-bound.
            tig_video_flip_perf_set_enabled(gamelib_zoom_perf_is_enabled());
            tig_debug_printf("[gpu-cmd] perf toggled\n");
        } else if (strncmp(line, "zoomlog", 7) == 0) {
            tig_debug_printf("[gpu-cmd] zoomlog cur=%.3f target=%.3f animating=%d avail=%d\n",
                (double)iso_zoom_current(), (double)iso_zoom_target(),
                iso_zoom_is_animating(), iso_zoom_is_available());
        } else if (strncmp(line, "trace", 5) == 0) {
            tile_gpu_trace_arm();
            tig_debug_printf("[gpu-cmd] trace armed\n");
        } else if (sscanf(line, "seed %u", &uix) == 1) {
            // determinism: fix the engine LCG (random_prev_value) so the same
            // seed + a fixed workout is byte-reproducible -- enables capture-diff
            // regression detection. Issue it right before the segment to make
            // deterministic; random_rand() is the engine's only RNG source.
            random_seed(uix);
            tig_debug_printf("[gpu-cmd] seed %u\n", uix);
        } else if (sscanf(line, "fixeddt %d", &ix) == 1) {
            // determinism: advance game/animation time by a fixed delta (ms) per
            // frame instead of wall-clock, so a seeded run is reproducible. 0 = off.
            harness_set_fixed_dt(ix);
            tig_debug_printf("[gpu-cmd] fixeddt %d\n", ix);
        } else if (strncmp(line, "spikecap ", 9) == 0) {
            // diagnosis: arm auto-capture-on-spike -- dump the iso buffer to
            // /tmp/arcanum-spike-<n>.bmp whenever a frame's render time reaches
            // <ms>, up to <max> (default 8) captures. `spikecap 0` disables.
            float sms = 0.0f;
            int smax = 8;
            if (sscanf(line, "spikecap %f %d", &sms, &smax) >= 1) {
                if (smax <= 0) smax = 8;
                harness_set_spike_capture((double)sms, smax);
                tig_debug_printf("[gpu-cmd] spikecap %.2fms max %d\n", (double)sms, smax);
            }
        } else if (strncmp(line, "bench-ab ", 9) == 0) {
            // CI A/B: measure mean render (full-redraw) time with a perf toggle OFF
            // vs ON, in the SAME launch at the current scene. Interleaved over a few
            // rounds (off,on,off,on,...) so thermal drift / first-pass ordering hits
            // both arms equally -- a single off-then-on lies on sub-10% deltas.
            char tname[64];
            int frames = 120;
            if (sscanf(line, "bench-ab %63s %d", tname, &frames) >= 1) {
                if (frames <= 0) frames = 120;
                if (!harness_apply_toggle(tname, 0)) {
                    tig_debug_printf("[gpu-cmd] bench-ab: unknown toggle '%s'\n", tname);
                } else {
                    const int rounds = 3;
                    double off_sum = 0.0, on_sum = 0.0;
                    harness_measure_render_ms(20); // warmup, discard
                    int r;
                    for (r = 0; r < rounds; r++) {
                        harness_apply_toggle(tname, 0);
                        off_sum += harness_measure_render_ms(frames);
                        harness_apply_toggle(tname, 1);
                        on_sum += harness_measure_render_ms(frames);
                    }
                    double off = off_sum / rounds;
                    double on = on_sum / rounds;
                    double delta = on - off;
                    double pct = off > 0.0 ? delta / off * 100.0 : 0.0;
                    tig_debug_printf("[gpu-cmd] bench-ab %s: off %.3fms, on %.3fms, "
                        "delta %+.3fms (%+.1f%%) over %dx%d frames\n",
                        tname, off, on, delta, pct, rounds, frames);
                }
            }
        } else if (strncmp(line, "assert-render-under ", 20) == 0) {
            // CI gate: measure mean render (full-redraw) time over N frames at the
            // current scene; exit non-zero if it is at/above the threshold so a perf
            // regression fails a headless run. Default 120 frames.
            float thresh = 0.0f;
            int frames = 120;
            int nn = sscanf(line, "assert-render-under %f %d", &thresh, &frames);
            if (nn >= 1) {
                if (frames <= 0) frames = 120;
                double avg = harness_measure_render_ms(frames);
                bool pass = avg < thresh;
                tig_debug_printf("[gpu-cmd] assert-render-under %.2fms over %d frames: "
                    "avg %.3fms -> %s\n", (double)thresh, frames, avg, pass ? "PASS" : "FAIL");
                if (!pass) {
                    fprintf(stderr, "[harness] assert-render-under FAILED: "
                        "avg %.3fms >= threshold %.2fms\n", avg, (double)thresh);
                    exit(1);
                }
            }
        } else if (strncmp(line, "quit", 4) == 0) {
            tig_debug_printf("[gpu-cmd] quit\n");
            // Pre-choose OK so the main loop's quit handler skips the blocking
            // "Are you sure?" confirm modal (which the channel can't drive).
            harness_request_quit();
            TigMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = TIG_MESSAGE_QUIT;
            tig_message_enqueue(&msg);
            break;
        } else {
            tig_debug_printf("[gpu-cmd] unknown: %s\n", line);
        }
    }
    fclose(fp);
}

#endif // ARCANUM_HARNESS
