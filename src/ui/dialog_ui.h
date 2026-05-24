#ifndef ARCANUM_UI_DIALOG_UI_H_
#define ARCANUM_UI_DIALOG_UI_H_

#include "game/context.h"
#include "game/dialog.h"

bool dialog_ui_init(GameInitInfo* init_info);
void dialog_ui_exit(void);
void dialog_ui_reset(void);
bool dialog_ui_is_in_dialog(int64_t obj);
void dialog_ui_start_dialog(int64_t pc_obj, int64_t npc_obj, int script_num, int script_line, int num);
void dialog_ui_end_dialog(int64_t obj, int a2);
bool dialog_ui_is_local_pc_in_dialog(void);

// CE: the NPC the local PC is currently talking to (or
// OBJ_HANDLE_NULL if not in a dialog). Used by the HUD-cycle MSG
// rotwin restore path to re-display "talking to X" content when
// TAB exits MINI mid-dialogue — the natural hover/target path
// doesn't drive a fresh sub_57CCF0 call during dialogue, so the
// MSG rotwin would otherwise show blank until the next speaker
// focus change.
int64_t dialog_ui_get_local_pc_npc_obj(void);
void dialog_ui_notify_dialog_ended(int64_t obj);
void dialog_ui_notify_dialog_started(int64_t obj);
void sub_5681C0(int64_t pc_obj, int64_t npc_obj);
void dialog_ui_float_line(int64_t npc_obj, int64_t pc_obj, const char* str, int speech_id);
void dialog_ui_notify_object_destroyed(int64_t obj);

#endif /* ARCANUM_UI_DIALOG_UI_H_ */
