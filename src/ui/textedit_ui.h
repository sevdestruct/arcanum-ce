#ifndef ARCANUM_UI_TEXTEDIT_UI_H_
#define ARCANUM_UI_TEXTEDIT_UI_H_

#include "game/context.h"

typedef unsigned int TextEditFlags;

#define TEXTEDIT_PATH_SAFE 0x01
#define TEXTEDIT_NO_ALPHA 0x02

struct TextEdit;

typedef void (*TextEditCallback)(struct TextEdit* textedit);

typedef struct TextEdit {
    /* 0000 */ TextEditFlags flags;
    /* 0004 */ char* buffer;
    /* 0008 */ int size;
    /* 000C */ TextEditCallback on_enter;
    /* 0010 */ TextEditCallback on_change;
    /* 0014 */ TextEditCallback on_tab;
    // CE: optional up/down arrow-at-boundary callbacks (appended past the original
    // layout). When set, the up/down arrows first move the cursor to the line
    // start/end; once it is already there (or the field is empty) they fire these so
    // the host UI can leave the input and move selection in its own list. NULL keeps
    // the vanilla page-up / page-down cursor jump.
    TextEditCallback on_arrow_up;
    TextEditCallback on_arrow_down;
} TextEdit;

bool textedit_ui_init(GameInitInfo* init_info);
void textedit_ui_reset(void);
void textedit_ui_exit(void);
void textedit_ui_focus(TextEdit* textedit);
void textedit_ui_unfocus(TextEdit* textedit);
bool textedit_ui_is_focused(void);
bool textedit_ui_process_message(TigMessage* msg);
void textedit_ui_clear(void);
void textedit_ui_restore(void);
void textedit_ui_submit(void);
int textedit_ui_pos_get(void);

#endif /* ARCANUM_UI_TEXTEDIT_UI_H_ */
