#include "game/iso_zoom.h"

#include <math.h>
#include <stdlib.h>

#include "game/gamelib.h"
#include "game/perception_fog.h"

static float zoom_current = 1.0f;
static float zoom_target = 1.0f;
static float zoom_min = ISO_ZOOM_MIN;
static float zoom_min_config = ISO_ZOOM_MIN; /* user-config floor, preserved across perception updates */
static float zoom_max = ISO_ZOOM_MAX;
static bool zoom_available = true;
static bool zoom_enabled = true;
static bool zoom_floor_enabled = true; /* perception-based zoom floor on/off toggle */

static void iso_zoom_enabled_changed(void)
{
    zoom_enabled = settings_get_value(&settings, ISO_ZOOM_ENABLED_KEY) != 0;
    if (!iso_zoom_is_available()) {
        iso_zoom_reset();
    }
}

static void iso_zoom_min_changed(void)
{
    const char* val = settings_get_str_value(&settings, ISO_ZOOM_MIN_KEY);
    if (val != NULL) {
        float v = (float)atof(val);
        if (v > 0.0f && v <= 1.0f) {
            zoom_min = v;
            zoom_min_config = v; /* remember user's configured floor */
        }
    }
}

static void iso_zoom_max_changed(void)
{
    const char* val = settings_get_str_value(&settings, ISO_ZOOM_MAX_KEY);
    if (val != NULL) {
        float v = (float)atof(val);
        if (v >= zoom_min) {
            zoom_max = v;
        }
    }
}

static void iso_zoom_floor_changed(void)
{
    zoom_floor_enabled = settings_get_value(&settings, ISO_ZOOM_FLOOR_KEY) != 0;
}


void iso_zoom_init(void)
{
    zoom_current = 1.0f;
    zoom_target = 1.0f;
    zoom_min = ISO_ZOOM_MIN;
    zoom_max = ISO_ZOOM_MAX;
    zoom_enabled = true;
    settings_register(&settings, ISO_ZOOM_ENABLED_KEY, "1", iso_zoom_enabled_changed);
    iso_zoom_enabled_changed();  // apply value already loaded from arcanum.cfg
    settings_register(&settings, ISO_ZOOM_MIN_KEY, "0.5", iso_zoom_min_changed);
    iso_zoom_min_changed();  // apply value already loaded from arcanum.cfg
    settings_register(&settings, ISO_ZOOM_MAX_KEY, "1.75", iso_zoom_max_changed);
    iso_zoom_max_changed();  // apply value already loaded from arcanum.cfg
    settings_register(&settings, ISO_ZOOM_FLOOR_KEY, "1", iso_zoom_floor_changed);
    iso_zoom_floor_changed();  // apply value already loaded from arcanum.cfg
}

void iso_zoom_ping(void)
{
    float diff = zoom_target - zoom_current;
    if (diff > -0.001f && diff < 0.001f) {
        if (zoom_current != zoom_target) {
            zoom_current = zoom_target;
            // The previous zoom frame cleared dirty rects and did its own
            // screen flush. Force a full invalidate so the non-zoom render
            // path actually runs and updates the screen after the snap.
            gamelib_invalidate_rect(NULL);
            perception_fog_mark_dirty(); /* zoom settled — fog ellipse size changed */
        }
    } else {
        zoom_current += diff * ISO_ZOOM_LERP;
        perception_fog_mark_dirty(); /* zoom is animating — fog ellipse changes each frame */
    }
}

void iso_zoom_step_in(void)
{
    if (!zoom_available) {
        return;
    }

    zoom_target = roundf(zoom_target / ISO_ZOOM_STEP) * ISO_ZOOM_STEP + ISO_ZOOM_STEP;
    if (zoom_target > zoom_max) {
        zoom_target = zoom_max;
    }
}

void iso_zoom_step_out(void)
{
    if (!zoom_available) {
        return;
    }

    zoom_target = roundf(zoom_target / ISO_ZOOM_STEP) * ISO_ZOOM_STEP - ISO_ZOOM_STEP;
    if (zoom_target < zoom_min) {
        zoom_target = zoom_min;
    }
}

void iso_zoom_wheel(int dy)
{
    if (dy > 0) {
        iso_zoom_step_in();
    } else if (dy < 0) {
        iso_zoom_step_out();
    }
}

float iso_zoom_current(void)
{
    return zoom_current;
}

float iso_zoom_target(void)
{
    return zoom_target;
}

bool iso_zoom_is_animating(void)
{
    return zoom_current != zoom_target;
}

bool iso_zoom_is_available(void)
{
    return zoom_available && zoom_enabled;
}

void iso_zoom_reset(void)
{
    zoom_target = 1.0f;
    zoom_current = 1.0f;
}

void iso_zoom_set_available(bool available)
{
    zoom_available = available;
    if (!zoom_available) {
        iso_zoom_reset();
    }
}

void iso_zoom_set_target(float z)
{
    if (!zoom_available) {
        iso_zoom_reset();
        return;
    }

    if (z < zoom_min) {
        z = zoom_min;
    }

    if (z > zoom_max) {
        z = zoom_max;
    }

    // Gravity well: pull any zoom within 5% of 1.0 to exactly 1.0 so the
    // lerp settles at hardware-scroll-compatible 1.0 rather than a near-miss.
    if (z != 1.0f && fabsf(z - 1.0f) <= 0.05f) {
        z = 1.0f;
    }

    zoom_target = z;
}

/**
 * Called every frame from gamelib_draw to enforce a Perception-based minimum
 * zoom level.
 *
 * When ScrollDist is non-zero the player's Perception stat limits how far the
 * camera may scroll.  This function converts those same pixel limits into a
 * minimum zoom value so the player cannot zoom out far enough to see beyond
 * the leash boundary.
 *
 * hor_limit / vert_limit are the leash half-extents in world pixels returned
 * by scroll_perception_pixel_limits().  Pass 0,0 to disable the perception
 * floor (e.g. when ScrollDist=0).
 */
void iso_zoom_update_perception_floor(int vp_w, int vp_h, int hor_limit, int vert_limit)
{
    float z_floor;
    float candidate;

    if (hor_limit <= 0 || vert_limit <= 0 || !zoom_floor_enabled) {
        /* ScrollDist=0, no player, or floor toggled off: restore config min. */
        if (zoom_min != zoom_min_config) {
            zoom_min = zoom_min_config;
            perception_fog_mark_dirty(); /* leash disabled — fog should clear */
        }
        /* Snap target up if it fell below the (now-restored) floor. */
        if (zoom_target < zoom_min) {
            iso_zoom_set_target(zoom_min);
        }
        if (zoom_current < zoom_min) {
            zoom_current = zoom_min;
        }
        return;
    }

    /* z must be large enough that the visible half-span <= leash limit.
     * visible_half_w = (vp_w/2) / z  <=  hor_limit  =>  z >= (vp_w/2)/hor_limit
     * visible_half_h = (vp_h/2) / z  <=  vert_limit =>  z >= (vp_h/2)/vert_limit
     * Take the more restrictive of the two axes.
     */
    z_floor = (float)(vp_w / 2) / (float)hor_limit;
    candidate = (float)(vp_h / 2) / (float)vert_limit;
    if (candidate > z_floor) {
        z_floor = candidate;
    }

    /* Clamp to valid zoom range.  Never drop below the user's configured
     * minimum; never exceed the configured maximum (happens for very low
     * perception characters on high-res screens). */
    if (z_floor < zoom_min_config) {
        z_floor = zoom_min_config;
    }
    if (z_floor < ISO_ZOOM_MIN) {
        z_floor = ISO_ZOOM_MIN;
    }
    if (z_floor > zoom_max) {
        z_floor = zoom_max;
    }

    /* Only update and mark dirty when the floor actually changes. */
    if (z_floor != zoom_min) {
        zoom_min = z_floor;
        perception_fog_mark_dirty(); /* perception stat changed — fog ellipse resized */

        /* Snap existing target/current if they are now below the new floor. */
        if (zoom_target < zoom_min) {
            iso_zoom_set_target(zoom_min);
        }
        if (zoom_current < zoom_min) {
            zoom_current = zoom_min;
        }
    }
}
