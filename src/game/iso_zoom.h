#ifndef ARCANUM_GAME_ISO_ZOOM_H_
#define ARCANUM_GAME_ISO_ZOOM_H_

#include <stdbool.h>

#define ISO_ZOOM_MIN  0.5f
#define ISO_ZOOM_MAX  1.75f
#define ISO_ZOOM_STEP 0.25f
#define ISO_ZOOM_LERP 0.25f

#define ISO_ZOOM_MIN_KEY "min zoom"
#define ISO_ZOOM_MAX_KEY "max zoom"

void iso_zoom_init(void);
void iso_zoom_ping(void);
void iso_zoom_step_in(void);
void iso_zoom_step_out(void);
void iso_zoom_wheel(int dy);
float iso_zoom_current(void);
float iso_zoom_target(void);
bool iso_zoom_is_animating(void);
bool iso_zoom_is_available(void);
void iso_zoom_reset(void);
void iso_zoom_set_available(bool available);
void iso_zoom_set_target(float z);

#endif /* ARCANUM_GAME_ISO_ZOOM_H_ */
