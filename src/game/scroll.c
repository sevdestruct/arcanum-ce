#include "game/scroll.h"

#include <math.h>
#include <stdlib.h>

#include "game/gamelib.h"
#include "game/iso_zoom.h"
#include "game/tb.h"
#include "game/gsound.h"
#include "game/location.h"
#include "game/name.h"
#include "game/object.h"
#include "game/player.h"
#include "game/stat.h"
#include "game/tc.h"
#include "game/tile.h"
#include "game/ui.h"

#define SCROLL_DIAG_X 4
#define SCROLL_DIAG_Y 2
// Leash resistance spring (CE). As the view approaches the leash limit,
// scroll input is scaled down so it eases into the edge instead of
// slamming to a stop.
//
//   START_RATIO — where resistance begins, as a fraction of the leash
//     limit. Lower = resistance starts sooner = longer, more gradual
//     ramp. 0.50 begins gently at the halfway point.
//   MIN_SCALE   — slowest crawl, applied right at the edge. Lower =
//     firmer final resistance (a slower crawl into the limit).
//   CURVE       — ease-in exponent on the resistance ramp. 1.0 is the
//     old linear feel; > 1 keeps the start gentle and makes the spring
//     "tighten" progressively, so it really clamps down only near the
//     very edge (takes a bit to hit max resistance, then crawls).
#define SCROLL_LEASH_SPRING_START_RATIO 0.50f
#define SCROLL_LEASH_SPRING_MIN_SCALE 0.05f
#define SCROLL_LEASH_SPRING_CURVE 2.0f
// Final-approach taper, in pixels. Over the last this-many pixels before
// the leash limit the scale ramps linearly to 0, so velocity reaches
// zero exactly AT the wall instead of snapping from the MIN_SCALE crawl
// to a dead stop. Bigger = longer, softer settle into the edge.
#define SCROLL_LEASH_SPRING_END_TAPER 28

void scroll_by(int64_t dx, int64_t dy); // non-static: exported in scroll.h for the harness
static void scroll_origin_changed(int64_t loc);
static void scroll_speed_changed(void);
static bool scroll_cursor_art_set(tig_art_id_t art_id);
static void scroll_refresh_clamped_view(void);
static float scroll_leash_scale(int current_dist, int next_dist, int limit);
static void scroll_leash_apply(float fdx, float fdy);

/**
 * The minimum time (in milliseconds) between scroll updates.
 *
 * 0x59F050
 */
static unsigned int scroll_fps = 1000;

/**
 * A copy of initialization info.
 *
 * 0x5D1168
 */
static GameInitInfo scroll_init_info;

/**
 * The current scrolling center location.
 *
 * 0x5D1180
 */
static int64_t scroll_center;

/**
 * Scroll speed.
 *
 * 0x5D1188
 */
static ScrollSpeed scroll_speed;

/**
 * Parent window bounds.
 *
 * 0x5D1190
 */
static TigRect scroll_iso_content_rect;

/**
 * Vertical scroll speed (in pixels per update).
 *
 * 0x5D11A0
 */
static int scroll_speed_y;

/**
 * Horizontal scroll speed (in pixels per update).
 *
 * 0x5D11A4
 */
static int scroll_speed_x;

/**
 * Editor view options.
 *
 * 0x5D11A8
 */
static ViewOptions scroll_view_options;

/**
 * Last known listener location for sound positioning.
 *
 * 0x5D11B8
 */
static int64_t scroll_origin;

/**
 * Flag indicating if scrolling is active.
 *
 * 0x5D11C0
 */
static bool is_scrolling;

/**
 * The maximum scroll distance from the scroll center.
 *
 * See `scroll_distance_set` for actual meaning.
 *
 * 0x5D11C4
 */
static int scroll_distance;

/**
 * Custom scroll function callback.
 *
 * 0x5D11C8
 */
static ScrollFunc scroll_func;

/**
 * Forces a viewport refresh when a scroll attempt is clamped by the leash or
 * map bounds.
 */
static void scroll_refresh_clamped_view(void)
{
    if (!scroll_init_info.editor) {
        scroll_init_info.invalidate_rect_func(&scroll_iso_content_rect);
    }
}

// Sub-pixel remainder so the leashed crawl can move at fractional
// velocity (e.g. 1px every few frames) and ease smoothly into the
// wall instead of stepping at a constant >=1px until the hard limit.
// Reset when a scroll gesture ends (scroll_stop).
static float scroll_leash_accum_x;
static float scroll_leash_accum_y;

// Returns a 0..1 multiplier for the scroll delta based on how close the
// view is to the leash limit on one axis. 1 = full speed (not near the
// limit, or moving back toward center); shrinks through the spring zone
// to MIN_SCALE; 0 exactly at/past the limit. Pure — no rounding, no
// per-pixel floor; the caller applies it in float and accumulates the
// fraction, which is what produces the smooth ease-in.
static float scroll_leash_scale(int current_dist, int next_dist, int limit)
{
    int spring_start;
    int remaining;
    float t;
    float scale;

    if (limit <= 0 || next_dist <= current_dist) {
        // Not approaching the limit (idle axis or moving toward center).
        return 1.0f;
    }
    if (current_dist >= limit) {
        return 0.0f;
    }

    spring_start = (int)roundf((float)limit * SCROLL_LEASH_SPRING_START_RATIO);
    if (spring_start >= limit) {
        spring_start = limit - 1;
    }
    if (current_dist < spring_start) {
        scale = 1.0f;
    } else {
        // t = 1 at spring_start (full speed), 0 at the limit (max
        // resistance). Ease-in curve (power > 1) keeps the early
        // approach near full speed, then drops scale off steeply and
        // flattens into the slow crawl close to the edge — the spring
        // "tightening".
        t = (float)(limit - current_dist) / (float)(limit - spring_start);
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
        t = powf(t, SCROLL_LEASH_SPRING_CURVE);
        scale = SCROLL_LEASH_SPRING_MIN_SCALE
            + (1.0f - SCROLL_LEASH_SPRING_MIN_SCALE) * t;
    }

    // Final-approach taper: over the last END_TAPER px, ramp scale
    // linearly to 0 so velocity reaches zero exactly AT the wall rather
    // than snapping from the MIN_SCALE crawl to a dead stop. This is the
    // bit that removes the residual abruptness right at max distance.
    remaining = limit - current_dist;
    if (remaining < SCROLL_LEASH_SPRING_END_TAPER) {
        scale *= (float)remaining / (float)SCROLL_LEASH_SPRING_END_TAPER;
    }

    return scale;
}

// Apply a (possibly fractional) leashed scroll delta through the
// sub-pixel accumulator. Only the integer part scrolls this frame; the
// fraction carries to the next. Frames where the accumulator hasn't
// reached a whole pixel simply don't move — that's the slow crawl that
// eases the view into the leash wall.
static void scroll_leash_apply(float fdx, float fdy)
{
    int idx;
    int idy;

    scroll_leash_accum_x += fdx;
    scroll_leash_accum_y += fdy;
    idx = (int)scroll_leash_accum_x;  // truncate toward zero
    idy = (int)scroll_leash_accum_y;
    scroll_leash_accum_x -= (float)idx;
    scroll_leash_accum_y -= (float)idy;
    if (idx != 0 || idy != 0) {
        scroll_by(idx, idy);
    }
}

/**
 * Called when the game is initialized.
 *
 * 0x40DF50
 */
bool scroll_init(GameInitInfo* init_info)
{
    TigWindowData window_data;

    if (tig_window_data(init_info->iso_window_handle, &window_data) != TIG_OK) {
        return false;
    }

    scroll_iso_content_rect.width = window_data.rect.width;
    scroll_iso_content_rect.height = window_data.rect.height;
    scroll_iso_content_rect.y = 0;
    scroll_iso_content_rect.x = 0;

    // Keep a copy of initialization info for later use. All props are being
    // used, so there is no point splitting it into individual variables.
    scroll_init_info = *init_info;

    scroll_view_options.type = VIEW_TYPE_ISOMETRIC;

    // Set the default scroll speed.
    scroll_speed = SCROLL_SPEED_NORMAL;
    scroll_speed_changed();

    location_origin_significant_change_callback_set(scroll_origin_changed);

    return true;
}

/**
 * Called when the game shuts down.
 *
 * 0x40E000
 */
void scroll_exit(void)
{
}

/**
 * Called when the game is being reset.
 *
 * 0x40E010
 */
void scroll_reset(void)
{
    scroll_func = NULL;
}

/**
 * Called when the window size has changed.
 *
 * 0x40E020
 */
void scroll_resize(GameResizeInfo* resize_info)
{
    scroll_iso_content_rect = resize_info->content_rect;
    scroll_init_info.iso_window_handle = resize_info->window_handle;
}

/**
 * Called when view settings have changed.
 *
 * 0x40E060
 */
void scroll_update_view(ViewOptions* view_options)
{
    scroll_view_options = *view_options;
    scroll_speed_changed();
}

/**
 * Sets the scroll speed.
 *
 * 0x40E080
 */
void scroll_speed_set(ScrollSpeed value)
{
    scroll_speed = value;
    scroll_speed_changed();
}

/**
 * Retrieves the scroll speed.
 *
 * 0x40E090
 */
ScrollSpeed scroll_speed_get(void)
{
    return scroll_speed;
}

// 0x40E0A0
void scroll_start(int direction)
{
    // 0x5D117C
    static unsigned int scroll_ping_time;

    int dx;
    int dy;
    float fdx;
    float fdy;
    tig_art_id_t art_id;
    TigArtFrameData art_frame_data;
    int distance;
    int64_t center_x;
    int64_t center_y;
    int viewport_center_x;
    int viewport_center_y;
    int current_hor;
    int current_vert;
    int hor_limit;
    int vert_limit;
    int hor;
    int vert;
    bool blocked;
    int64_t viewport_center_loc;
    int rot;

    // In non-editor mode, enforce scroll rate limit.
    if (!scroll_init_info.editor) {
        if ((unsigned int)tig_timer_between(scroll_ping_time, gamelib_ping_time) < scroll_fps) {
            return;
        }

        scroll_ping_time = gamelib_ping_time;
    }

    // Delegate to custom scroll function if set.
    if (scroll_func != NULL) {
        scroll_func(direction);
        return;
    }

    dx = 0;
    dy = 0;

    // Calculate scroll deltas based on direction.
    switch (direction) {
    case SCROLL_DIRECTION_UP:
        dy = scroll_speed_y;
        break;
    case SCROLL_DIRECTION_UP_RIGHT:
        dx = -(scroll_speed_x + SCROLL_DIAG_X);
        dy = scroll_speed_y + SCROLL_DIAG_Y;
        break;
    case SCROLL_DIRECTION_RIGHT:
        dx = -scroll_speed_x;
        break;
    case SCROLL_DIRECTION_DOWN_RIGHT:
        dx = -(scroll_speed_x + SCROLL_DIAG_X);
        dy = -(scroll_speed_y + SCROLL_DIAG_Y);
        break;
    case SCROLL_DIRECTION_DOWN:
        dy = -scroll_speed_y;
        break;
    case SCROLL_DIRECTION_DOWN_LEFT:
        dx = scroll_speed_x + SCROLL_DIAG_X;
        dy = -(scroll_speed_y + SCROLL_DIAG_Y);
        break;
    case SCROLL_DIRECTION_LEFT:
        dx = scroll_speed_x;
        break;
    case SCROLL_DIRECTION_UP_LEFT:
        dx = scroll_speed_x + SCROLL_DIAG_X;
        dy = scroll_speed_y + SCROLL_DIAG_Y;
        break;
    default:
        break;
    }

    // In editor mode, perform the scroll immediately.
    if (scroll_init_info.editor) {
        scroll_by(dx, dy);
        return;
    }

    // Begin continuous scrolling.
    is_scrolling = true;

    // Retrieve the effective scroll distance.
    distance = scroll_distance_get();
    if (distance == 0) {
        // No distance limit.
        tig_art_interface_id_create(direction + 679, 0, 0, 0, &art_id);
        scroll_cursor_art_set(art_id);
        scroll_by(dx, dy);
        return;
    }

    // Get the current scroll center coordinates (adjusted to center of tile)
    location_xy(scroll_center, &center_x, &center_y);
    center_x += 40;
    center_y += 20;

    // Calculate viewport center.
    viewport_center_x = scroll_iso_content_rect.width / 2;
    viewport_center_y = scroll_iso_content_rect.height / 2;
    current_hor = abs(viewport_center_x - (int)center_x);
    current_vert = abs(viewport_center_y - (int)center_y);
    hor_limit = 80 * distance;
    vert_limit = 40 * distance;

    // Apply the leash resistance spring in float, per axis. next_dist
    // uses the full (unsprung) delta — as before — to gauge how far
    // this step would push toward the limit. The scaled result stays a
    // float (fdx/fdy) so the sub-pixel accumulator can ease the crawl;
    // dx/dy hold the rounded values the bounds/direction logic uses.
    fdx = (float)dx * scroll_leash_scale(current_hor,
        abs(viewport_center_x - dx - (int)center_x), hor_limit);
    fdy = (float)dy * scroll_leash_scale(current_vert,
        abs(viewport_center_y - dy - (int)center_y), vert_limit);
    dx = (int)lroundf(fdx);
    dy = (int)lroundf(fdy);

    // Calculate horizontal and vertical distance (in pixels) from the scroll
    // center.
    hor = abs(viewport_center_x - dx - (int)center_x);
    vert = abs(viewport_center_y - dy - (int)center_y);

    // Check if scrolling is within perception-based limits.
    if (hor < hor_limit && vert < vert_limit) {
        tig_art_interface_id_create(direction + 679, 0, 0, 0, &art_id);
        scroll_cursor_art_set(art_id);
        scroll_leash_apply(fdx, fdy);
        return;
    }

    location_at(viewport_center_x, viewport_center_y, &viewport_center_loc);

    rot = location_rot(viewport_center_loc, scroll_center);
    if (rot == (direction - 1) % 8
        || rot == (direction + 1) % 8
        || rot == direction) {
        tig_art_interface_id_create(direction + 679, 0, 0, 0, &art_id);
        scroll_cursor_art_set(art_id);
        scroll_leash_apply(fdx, fdy);
        return;
    }

    blocked = false;

    // Adjust direction if horizontal distance exceeds scroll distance limit.
    // A diagonal that hits the horizontal leash slides along the vertical
    // edge: zero the horizontal component, keep the vertical one. We set
    // dx = 0 directly rather than algebraically cancelling it (the old
    // `dx += scroll_speed_x + SCROLL_DIAG_X` only zeroed the FULL delta;
    // the leash spring above has already scaled dx down, so the add
    // overshot into the opposite direction and kicked the view back
    // toward center — the corner "bounce").
    if (hor >= hor_limit) {
        switch (direction) {
        case SCROLL_DIRECTION_UP_RIGHT:
            direction = SCROLL_DIRECTION_UP;
            dx = 0;
            fdx = 0.0f;
            break;
        case SCROLL_DIRECTION_RIGHT:
        case SCROLL_DIRECTION_LEFT:
            blocked = true;
            break;
        case SCROLL_DIRECTION_DOWN_RIGHT:
            direction = SCROLL_DIRECTION_DOWN;
            dx = 0;
            fdx = 0.0f;
            break;
        case SCROLL_DIRECTION_DOWN_LEFT:
            direction = SCROLL_DIRECTION_DOWN;
            dx = 0;
            fdx = 0.0f;
            break;
        case SCROLL_DIRECTION_UP_LEFT:
            direction = SCROLL_DIRECTION_UP;
            dx = 0;
            fdx = 0.0f;
            break;
        }
    }

    // Adjust direction if vertical distance exceeds scroll distance limit.
    // Mirror of the horizontal case: zero the vertical component to slide
    // along the horizontal edge. dy = 0 directly, same spring-overshoot
    // reasoning as above. At a true corner both axes zero and `blocked`
    // is set (cardinal sub-case), so the view settles instead of bouncing.
    if (vert >= vert_limit) {
        switch (direction) {
        case SCROLL_DIRECTION_UP:
        case SCROLL_DIRECTION_DOWN:
            blocked = true;
            break;
        case SCROLL_DIRECTION_UP_RIGHT:
            direction = SCROLL_DIRECTION_RIGHT;
            dy = 0;
            fdy = 0.0f;
            break;
        case SCROLL_DIRECTION_DOWN_RIGHT:
            direction = SCROLL_DIRECTION_RIGHT;
            dy = 0;
            fdy = 0.0f;
            break;
        case SCROLL_DIRECTION_DOWN_LEFT:
            direction = SCROLL_DIRECTION_LEFT;
            dy = 0;
            fdy = 0.0f;
            break;
        case SCROLL_DIRECTION_UP_LEFT:
            direction = SCROLL_DIRECTION_LEFT;
            dy = 0;
            fdy = 0.0f;
            break;
        }
    }

    // Perform scroll unless blocked. The surviving axis keeps its
    // already-sprung float value (fdx/fdy from the single spring pass
    // above); the blocked axis was zeroed in the switches. No second
    // spring pass — the old code re-sprung here, double-scaling the
    // surviving axis and (pre-fix) feeding the corner bounce.
    if (!blocked) {
        tig_art_interface_id_create(direction + 679, 0, 0, 0, &art_id);
        scroll_cursor_art_set(art_id);
        scroll_leash_apply(fdx, fdy);
        return;
    }

    // Scrolling is blocked. Update the mouse cursor and set appropriate offsets
    // so it appears sticked to the relevant edge.
    tig_art_interface_id_create(678, 0, 0, 0, &art_id);
    if (scroll_cursor_art_set(art_id)
        && tig_art_frame_data(art_id, &art_frame_data) == TIG_OK) {
        switch (direction) {
        case SCROLL_DIRECTION_UP:
            tig_mouse_cursor_set_offset(art_frame_data.width / 2, 0);
            break;
        case SCROLL_DIRECTION_UP_RIGHT:
            tig_mouse_cursor_set_offset(art_frame_data.width - 1, 0);
            break;
        case SCROLL_DIRECTION_RIGHT:
            tig_mouse_cursor_set_offset(art_frame_data.width - 1, art_frame_data.height / 2);
            break;
        case SCROLL_DIRECTION_DOWN_RIGHT:
            tig_mouse_cursor_set_offset(art_frame_data.width - 1, art_frame_data.height - 1);
            break;
        case SCROLL_DIRECTION_DOWN:
            tig_mouse_cursor_set_offset(art_frame_data.width / 2, art_frame_data.height - 1);
            break;
        case SCROLL_DIRECTION_DOWN_LEFT:
            tig_mouse_cursor_set_offset(art_frame_data.width / 2, art_frame_data.height - 1);
            break;
        case SCROLL_DIRECTION_LEFT:
            tig_mouse_cursor_set_offset(0, art_frame_data.height / 2);
            break;
        case SCROLL_DIRECTION_UP_LEFT:
            tig_mouse_cursor_set_offset(0, 0);
            break;
        }
    }

    scroll_refresh_clamped_view();
}

/**
 * Ends scrolling and resets the cursor.
 *
 * 0x40E610
 */
void scroll_stop(void)
{
    if (is_scrolling) {
        ui_refresh_cursor();
        is_scrolling = false;
    }
    // Drop any sub-pixel leash remainder so the next gesture starts clean.
    scroll_leash_accum_x = 0.0f;
    scroll_leash_accum_y = 0.0f;
}

/**
 * Scrolls the game view by the specified offsets.
 *
 * 0x40E630
 */
void scroll_by(int64_t dx, int64_t dy)
{
    int64_t old_origin_x;
    int64_t old_origin_y;
    int64_t new_origin_x;
    int64_t new_origin_y;
    TigRect rect;
    float z;

    z = iso_zoom_current();
    if (z != 1.0f) {
        dx = (int64_t)roundf((float)dx / z);
        dy = (int64_t)roundf((float)dy / z);
    }

    // Update the view origin and check for actual movement.
    location_origin_get(&old_origin_x, &old_origin_y);
    location_origin_scroll(dx, dy);
    location_origin_get(&new_origin_x, &new_origin_y);

    // Exit if no actual movement occurred (at map edges).
    if (old_origin_x == new_origin_x && old_origin_y == new_origin_y) {
        scroll_refresh_clamped_view();
        return;
    }

    // Calculate effective scroll offsets.
    dx = new_origin_x - old_origin_x;
    dy = new_origin_y - old_origin_y;

    // Always invalidate the iso view on a scroll. This is load-bearing: it sets
    // gamelib_dirty so gamelib_draw doesn't early-return before its camera-move
    // re-render. (A prior "skip and let gamelib full-invalidate" experiment janked
    // badly -- the full-invalidate lives AFTER the !gamelib_dirty early-return.)
    scroll_init_info.invalidate_rect_func(&scroll_iso_content_rect);

    // Notify text conversation system of the scroll, so it can update it's
    // dimming overlay.
    tc_scroll((int)dx, (int)dy);

    if (!scroll_init_info.editor) {
        int64_t loc;

        // Update the sound listener position.
        location_at(scroll_iso_content_rect.width / 2, scroll_iso_content_rect.height / 2, &loc);
        if (loc != scroll_origin) {
            gsound_listener_set(loc);
            scroll_origin = loc;
        }
    }
}

/**
 * Sets the scroll frame rate.
 *
 * 0x40E890
 */
void scroll_fps_set(int fps)
{
    // FIX: Make sure `fps` is positive.
    if (fps <= 0) {
        return;
    }

    scroll_fps = 1000 / fps;
}

/**
 * Sets the maximum scroll distance.
 *
 * The value of `0` indicates that there is no scrolling limit. Any other value
 * indicates that scrolling limit is in effect, subject to player's perception
 * level.
 *
 * 0x40E8A0
 */
void scroll_distance_set(int distance)
{
    scroll_distance = distance;
}

/**
 * Retrieves the effective scrolling distance (in tiles), factoring in player
 * perception.
 *
 * The maximum scrolling distance is 13 tiles from the scrolling center
 * location.
 *
 * Returns `0` if there is no scrolling distance limit.
 *
 * 0x40E8B0
 */
int scroll_distance_get(void)
{
    int64_t pc_obj;

    if (scroll_distance == 0) {
        return 0;
    }

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL) {
        return 0;
    }

    return stat_level_get(pc_obj, STAT_PERCEPTION) / 2 + 3;
}

/**
 * Sets the center location which is used to determine maximum allowed scrolling
 * distance.
 *
 * 0x40E8E0
 */
void scroll_set_center(int64_t location)
{
    scroll_center = location;
}

/**
 * Sets a custom scroll function to override default behaviour.
 *
 * 0x40E900
 */
void scroll_set_scroll_func(ScrollFunc func)
{
    scroll_func = func;
}

/**
 * Called when location origin suddenly changes.
 *
 * 0x40E910
 */
void scroll_origin_changed(int64_t loc)
{
    if (!scroll_init_info.editor) {
        // Update listener location.
        gsound_listener_set(loc);
        scroll_origin = loc;
    }
}

/**
 * Internal helper to update scroll speeds based on view options and zoom level.
 *
 * 0x40E940
 */
void scroll_speed_changed(void)
{
    if (scroll_view_options.type == VIEW_TYPE_ISOMETRIC) {
        switch (scroll_speed) {
        case SCROLL_SPEED_SLOW:
            scroll_speed_x = 8;
            scroll_speed_y = 4;
            break;
        case SCROLL_SPEED_NORMAL:
            scroll_speed_x = 20;
            scroll_speed_y = 10;
            break;
        case SCROLL_SPEED_FAST:
            scroll_speed_x = 28;
            scroll_speed_y = 14;
            break;
        case SCROLL_SPEED_VERY_FAST:
            scroll_speed_x = 56;
            scroll_speed_y = 28;
            break;
        }
    } else {
        switch (scroll_speed) {
        case SCROLL_SPEED_SLOW:
            scroll_speed_x = scroll_view_options.zoom / 2;
            scroll_speed_y = scroll_view_options.zoom / 4;
            break;
        case SCROLL_SPEED_NORMAL:
            scroll_speed_x = scroll_view_options.zoom;
            scroll_speed_y = scroll_view_options.zoom / 2;
            break;
        case SCROLL_SPEED_FAST:
            scroll_speed_x = scroll_view_options.zoom * 2;
            scroll_speed_y = scroll_view_options.zoom;
            break;
        case SCROLL_SPEED_VERY_FAST:
            scroll_speed_x = scroll_view_options.zoom * 4;
            scroll_speed_y = scroll_view_options.zoom * 2;
            break;
        }
    }
}

/**
 * Internal helper to set the mouse cursor art.
 *
 * 0x40EA50
 */
bool scroll_cursor_art_set(tig_art_id_t art_id)
{
    // Check if the desired cursor is already set.
    if (name_normalize_aid(art_id) == name_normalize_aid(tig_mouse_cursor_get_art_id())) {
        return true;
    }

    // Attempt to set the new cursor art.
    if (tig_mouse_cursor_set_art_id(art_id) == TIG_OK) {
        return true;
    }

    return false;
}
