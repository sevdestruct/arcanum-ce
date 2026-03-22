#ifndef ARCANUM_GAME_DIALOG_CAMERA_H_
#define ARCANUM_GAME_DIALOG_CAMERA_H_

#include <stdbool.h>
#include <stdint.h>

// Settings keys (registered during dialog_camera_init).
#define DIALOGUE_CAMERA_MODE_KEY "dialogue camera mode"
// 0 = minimal pan only (default) — only moves if NPC is off-screen/clipped
// 1 = center on NPC
// 2 = center on midpoint between player and NPC

#define DIALOGUE_CAMERA_TWEEN_BACK_KEY "dialogue camera tween back"
// 0 = do not move camera when dialogue closes (default)
// 1 = tween back to the player character when dialogue closes

void dialog_camera_init(void);
void dialog_camera_ping(void);
void dialog_camera_start(int64_t pc_obj, int64_t npc_obj);
void dialog_camera_end(int64_t pc_obj);
bool dialog_camera_is_animating(void);

#endif /* ARCANUM_GAME_DIALOG_CAMERA_H_ */
