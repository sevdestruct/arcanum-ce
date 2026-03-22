#include "game/iso_zoom.h"

#include <stdlib.h>

#include "game/gamelib.h"

static float zoom_current = 1.0f;
static float zoom_target = 1.0f;
static float zoom_max = ISO_ZOOM_MAX;

static void iso_zoom_max_changed(void)
{
    const char* val = settings_get_str_value(&settings, ISO_ZOOM_MAX_KEY);
    if (val != NULL) {
        float v = (float)atof(val);
        if (v >= ISO_ZOOM_MIN) {
            zoom_max = v;
        }
    }
}

void iso_zoom_init(void)
{
    zoom_current = 1.0f;
    zoom_target = 1.0f;
    zoom_max = ISO_ZOOM_MAX;
    settings_register(&settings, ISO_ZOOM_MAX_KEY, "1.75", iso_zoom_max_changed);
    iso_zoom_max_changed();  // apply value already loaded from arcanum.cfg
}

void iso_zoom_ping(void)
{
    float diff = zoom_target - zoom_current;
    if (diff > -0.01f && diff < 0.01f) {
        zoom_current = zoom_target;
    } else {
        zoom_current += diff * ISO_ZOOM_LERP;
    }
}

void iso_zoom_step_in(void)
{
    zoom_target += ISO_ZOOM_STEP;
    if (zoom_target > zoom_max) {
        zoom_target = zoom_max;
    }
}

void iso_zoom_step_out(void)
{
    zoom_target -= ISO_ZOOM_STEP;
    if (zoom_target < ISO_ZOOM_MIN) {
        zoom_target = ISO_ZOOM_MIN;
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
}

void iso_zoom_set_target(float z)
{
    if (z < ISO_ZOOM_MIN) {
        z = ISO_ZOOM_MIN;
    }

    if (z > zoom_max) {
        z = zoom_max;
    }

    zoom_target = z;
}
