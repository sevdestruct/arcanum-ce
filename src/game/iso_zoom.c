#include "game/iso_zoom.h"

#include <math.h>
#include <stdlib.h>

#include "game/gamelib.h"

static float zoom_current = 1.0f;
static float zoom_target = 1.0f;
static float zoom_min = ISO_ZOOM_MIN;
static float zoom_max = ISO_ZOOM_MAX;

static void iso_zoom_min_changed(void)
{
    const char* val = settings_get_str_value(&settings, ISO_ZOOM_MIN_KEY);
    if (val != NULL) {
        float v = (float)atof(val);
        if (v > 0.0f && v <= 1.0f) {
            zoom_min = v;
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


void iso_zoom_init(void)
{
    zoom_current = 1.0f;
    zoom_target = 1.0f;
    zoom_min = ISO_ZOOM_MIN;
    zoom_max = ISO_ZOOM_MAX;
    settings_register(&settings, ISO_ZOOM_MIN_KEY, "0.5", iso_zoom_min_changed);
    iso_zoom_min_changed();  // apply value already loaded from arcanum.cfg
    settings_register(&settings, ISO_ZOOM_MAX_KEY, "1.75", iso_zoom_max_changed);
    iso_zoom_max_changed();  // apply value already loaded from arcanum.cfg
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
        }
    } else {
        zoom_current += diff * ISO_ZOOM_LERP;
    }
}

void iso_zoom_step_in(void)
{
    zoom_target = roundf(zoom_target / ISO_ZOOM_STEP) * ISO_ZOOM_STEP + ISO_ZOOM_STEP;
    if (zoom_target > zoom_max) {
        zoom_target = zoom_max;
    }
}

void iso_zoom_step_out(void)
{
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

void iso_zoom_reset(void)
{
    zoom_target = 1.0f;
    zoom_current = 1.0f;
}

void iso_zoom_set_target(float z)
{
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
