#ifndef TIG_FONT_H_
#define TIG_FONT_H_

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIG_FONT_HANDLE_INVALID ((tig_font_handle_t)0)

typedef uint32_t TigFontFlags;

#define TIG_FONT_SCALE 0x0001u
#define TIG_FONT_UNDERLINE 0x0002u
#define TIG_FONT_SHADOW 0x0008u
#define TIG_FONT_CENTERED 0x0010u
#define TIG_FONT_STRIKE_THROUGH 0x0020u
#define TIG_FONT_RIGHT 0x0040u
#define TIG_FONT_NO_ALPHA_BLEND 0x0080u
#define TIG_FONT_BLEND_ADD 0x0100u
// CE: dim text inside [square brackets] (including the brackets) to
// ~50% of the font color — used to set off bracketed emote / stage-
// direction spans in dialogue. Per-glyph color switch inside
// tig_font_write, so word-wrapping is unaffected. Only takes effect
// when the global toggle (tig_font_dim_brackets_set_enabled) is on,
// so callers can leave the flag set and gate the effect at runtime.
#define TIG_FONT_DIM_BRACKETS 0x0200u
// CE: a second, independently-toggled bracket-dim channel. Identical
// effect to TIG_FONT_DIM_BRACKETS but gated by its own runtime toggle
// (tig_font_dim_brackets_set_alt_enabled), so two font groups can dim
// brackets under separate control. Shares the dim percent. The game
// maps the primary channel to NPC speech and this one to PC choices.
#define TIG_FONT_DIM_BRACKETS_ALT 0x0400u

typedef struct TigFont {
    /* 0000 */ TigFontFlags flags;
    /* 0004 */ tig_art_id_t art_id;
    /* 0008 */ tig_color_t color;
    /* 000C */ tig_color_t underline_color;
    /* 0010 */ int field_10;
    /* 0014 */ tig_color_t strike_through_color;
    /* 0018 */ float scale;
    /* 001C */ const char* str;
    /* 0020 */ int width;
    /* 0024 */ int height;
    /* 0028 */ int field_28;
} TigFont;

int tig_font_init(TigInitInfo* init_info);
void tig_font_exit(void);
void tig_font_create(TigFont* font, tig_font_handle_t* font_handle_ptr);
void tig_font_destroy(tig_font_handle_t font_handle);
int tig_font_push(tig_font_handle_t font_handle);
void tig_font_pop(void);
void tig_font_measure(TigFont* font);
int tig_font_write(TigVideoBuffer* video_buffer, const char* str, const TigRect* rect, TigRect* dirty_rect);

// CE: runtime master toggle for the TIG_FONT_DIM_BRACKETS effect.
// Defaults off; the game sets it from its dialogue-emote config.
void tig_font_dim_brackets_set_enabled(bool enabled);

// CE: runtime toggle for the TIG_FONT_DIM_BRACKETS_ALT channel.
// Defaults off; the game sets it from its dialogue-emote config.
void tig_font_dim_brackets_set_alt_enabled(bool enabled);

// CE: how bright bracketed text stays, as a percent of the normal
// color (0 = black, 100 = no dimming). Defaults to 60. Clamped to
// [0, 100]. The game sets it from its dialogue-emote config.
void tig_font_dim_brackets_set_percent(int percent);

#ifdef __cplusplus
}
#endif

#endif /* TIG_FONT_H_ */
