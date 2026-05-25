#ifndef TIG_WINDOW_H_
#define TIG_WINDOW_H_

#include "tig/rect.h"
#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIG_WINDOW_HANDLE_INVALID ((tig_window_handle_t)(-1))

#define TIG_WINDOW_TOP (-2)

typedef unsigned int TigWindowFlags;

#define TIG_WINDOW_TRANSPARENT 0x0001
#define TIG_WINDOW_MESSAGE_FILTER 0x0002
#define TIG_WINDOW_MODAL 0x0004
#define TIG_WINDOW_ALWAYS_ON_TOP 0x0008
#define TIG_WINDOW_VIDEO_MEMORY 0x0010
#define TIG_WINDOW_HIDDEN 0x0020
#define TIG_WINDOW_RENDER_TARGET 0x0040
#define TIG_WINDOW_ALWAYS_ON_BOTTOM 0x0080

typedef bool (*TigWindowMessageFilterFunc)(TigMessage* msg);

typedef struct TigWindowData {
    /* 0000 */ TigWindowFlags flags;
    /* 0004 */ TigRect rect;
    /* 0014 */ unsigned int background_color;
    /* 0018 */ unsigned int color_key;
    /* 001C */ TigWindowMessageFilterFunc message_filter;
} TigWindowData;

typedef enum TigWindowBltType {
    // Blits `src_window_handle` to `dst_window_handle`.
    TIG_WINDOW_BLIT_WINDOW_TO_WINDOW = 1,

    // Blits `src_video_buffer` to `dst_window_handle`.
    TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW = 2,

    // Blits `src_window_handle` to `dst_window_buffer`.
    TIG_WINDOW_BLT_WINDOW_TO_VIDEO_BUFFER = 3,
} TigWindowBltType;

typedef struct TigWindowBlitInfo {
    /* 0000 */ int type;
    /* 0004 */ unsigned int vb_blit_flags;
    /* 0008 */ tig_window_handle_t src_window_handle;
    /* 000C */ TigVideoBuffer* src_video_buffer;
    /* 0010 */ TigRect* src_rect;
    /* 0014 */ uint8_t alpha[4];
    /* 0018 */ int field_18;
    /* 001C */ tig_window_handle_t dst_window_handle;
    /* 0020 */ TigVideoBuffer* dst_video_buffer;
    /* 0024 */ TigRect* dst_rect;
} TigWindowBlitInfo;

typedef enum TigWindowModalDialogType {
    // The modal dialog contains a single OK-like button (green checkmark).
    TIG_WINDOW_MODAL_DIALOG_TYPE_OK,

    // The modal dialog contains a single CANCEL-like button (red cross).
    TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL,

    // The modal dialog contains two buttons (OK and CANCEL).
    TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL,
} TigWindowModalDialogType;

typedef enum TigWindowModalDialogChoice {
    TIG_WINDOW_MODAL_DIALOG_CHOICE_OK,
    TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL,
    TIG_WINDOW_MODAL_DIALOG_CHOICE_COUNT,
} TigWindowModalDialogChoice;

typedef bool (*TigWindowDialogProcess)(TigWindowModalDialogChoice* choice_ptr);
typedef void (*TigWindowDialogRedraw)(void);

typedef struct TigWindowModalDialogInfo {
    /* 0000 */ int type;
    /* 0004 */ int x;
    /* 0008 */ int y;
    /* 000C */ const char* text;
    /* 0010 */ TigWindowDialogProcess process;
    /* 0014 */ unsigned char keys[2];
    /* 0018 */ TigWindowDialogRedraw redraw;
} TigWindowModalDialogInfo;

int tig_window_init(TigInitInfo* init_info);
void tig_window_exit(void);
int tig_window_create(TigWindowData* window_data, tig_window_handle_t* window_handle_ptr);
int tig_window_destroy(tig_window_handle_t window_handle);
int tig_window_button_destroy(tig_window_handle_t window_handle);
int tig_window_message_filter_set(tig_window_handle_t window_handle, TigWindowMessageFilterFunc func);
int tig_window_data(tig_window_handle_t window_handle, TigWindowData* window_data);
int tig_window_display(void);
void sub_51D050(TigRect* src_rect, TigVideoBuffer* dst_video_buffer, int dx, int dy, int top_window_index);
int tig_window_fill(tig_window_handle_t window_handle, TigRect* rect, tig_color_t color);
int tig_window_line(tig_window_handle_t window_handle, TigLine* line, tig_color_t color);
int tig_window_box(tig_window_handle_t window_handle, TigRect* rect, tig_color_t color);
int tig_window_blit(TigWindowBlitInfo* win_blit_info);
int tig_window_blit_art(tig_window_handle_t window_handle, TigArtBlitInfo* blit_info);
int tig_window_scroll(tig_window_handle_t window_handle, int dx, int dy);
int tig_window_scroll_rect(tig_window_handle_t window_handle, TigRect* rect, int dx, int dy);
int tig_window_copy(tig_window_handle_t dst_window_handle, TigRect* dst_rect, tig_window_handle_t src_window_handle, TigRect* src_rect);
int tig_window_copy_from_vbuffer(tig_window_handle_t dst_window_handle, TigRect* dst_rect, TigVideoBuffer* src_video_buffer, TigRect* src_rect);
// CE: same as tig_window_copy_from_vbuffer but blends the source with
// the existing destination pixels using a constant alpha (0..255).
// alpha=255 is opaque (= identical to the plain copy); alpha=0 is
// fully transparent (no-op render). Color-key transparent pixels in
// src still composite as transparent regardless of alpha. Used by
// tb.c to fade speech bubbles in/out when their NPC drifts off-/
// on-screen.
int tig_window_copy_from_vbuffer_alpha(tig_window_handle_t dst_window_handle, TigRect* dst_rect, TigVideoBuffer* src_video_buffer, TigRect* src_rect, uint8_t alpha);
int tig_window_copy_to_vbuffer(tig_window_handle_t src_window_handle, TigRect* src_rect, TigVideoBuffer* dst_video_buffer, TigRect* dst_rect);
int tig_window_tint(tig_window_handle_t window_handle, TigRect* rect, int a3, int a4);
int tig_window_text_write(tig_window_handle_t window_handle, const char* text, TigRect* rect);
void tig_window_invalidate_rect(TigRect* rect);
int tig_window_button_add(tig_window_handle_t window_handle, tig_button_handle_t button_handle);
int tig_window_button_remove(tig_window_handle_t window_handle, tig_button_handle_t button_handle);
int tig_window_button_list(tig_window_handle_t window_handle, tig_button_handle_t** buttons);
int tig_window_get_at_position(int x, int y, tig_window_handle_t* window_handle_ptr);
bool tig_window_filter_message(TigMessage* msg);
int sub_51E850(tig_window_handle_t window_handle);
int tig_window_move_on_top(tig_window_handle_t window_handle);
int tig_window_show(tig_window_handle_t window_handle);
int tig_window_hide(tig_window_handle_t window_handle);
bool tig_window_is_hidden(tig_window_handle_t window_handle);
int tig_window_vbid_get(tig_window_handle_t window_handle, TigVideoBuffer** video_buffer_ptr);
int tig_window_set_video_buffer(tig_window_handle_t window_handle, TigVideoBuffer* vb);
void tig_window_set_invalidate_suppressed(bool suppressed);
int tig_window_modal_dialog(TigWindowModalDialogInfo* modal_info, TigWindowModalDialogChoice* choice_ptr);
int tig_window_move(tig_window_handle_t window_handle, int x, int y);

// CE: Optional per-window screen-coords clip rectangle. When set
// (non-NULL), the compositor only paints pixels inside both the
// window's frame AND the clip rect — the rest falls through to
// whichever window is beneath in the stack. The VB itself stays
// untouched, and frame-relative coordinates (button positions, text
// rects, etc.) keep working unchanged: clipping affects ONLY which
// pixels reach the screen.
//
// Passing NULL clears any existing clip — the window reverts to
// "frame defines everything visible," its normal behavior.
//
// Use case: showing only a band of a tall window's VB without
// destroying the VB content (e.g. cropping the HUD bar to show
// just the rotwin row while leaving the rest of the bar's VB —
// buttons, art — intact for instant restore).
int tig_window_clip_rect_set(tig_window_handle_t window_handle, const TigRect* clip_rect);

// CE: opt the given window into the translucent-black tint pathway.
// When enabled, the compositor's blit for this window replaces near-
// black source pixels with MUL-darkened underlay-VB pixels at the
// same screen position; other pixels copy through opaque. Used by
// the HUD bar to show a darkened world through its dark panel art.
//
// underlay_handle: window whose VB supplies the live world pixels
// (typically the iso world). r/g/b: per-channel "darken by N out
// of 255" — 0 = preserve channel, 255 = zero it. The multiply
// preserves the underlay's hue (channels scale in proportion to
// their original value) instead of clipping channels independently
// the way a saturating subtract would. Pass enabled=false to
// disable.
int tig_window_tint_enable(tig_window_handle_t window_handle,
    bool enabled,
    tig_window_handle_t underlay_handle,
    uint8_t threshold,
    uint8_t r,
    uint8_t g,
    uint8_t b);

// CE: globally configure auto-tint on modal dialogs created by
// tig_window_modal_dialog. Enable when an in-play game session
// exists (passing the iso window handle as underlay). Disable when
// no iso world exists — pre-game modals over the title screen stay
// opaque so the mainmenu_bg isn't darkened behind them.
int tig_window_modal_tint_set(bool enabled,
    tig_window_handle_t underlay_handle,
    uint8_t threshold,
    uint8_t r,
    uint8_t g,
    uint8_t b);

// CE: per-window scale + alpha + anchor for animated entrance / exit
// transitions. Driven by the ui_anim spring tween system. When set,
// the compositor blits this window's VB at a scaled dst rect (around
// the anchor) with const-alpha blending instead of the standard 1:1
// opaque blit. Anchor is frame-relative (0..1 each axis: 0/0 is
// top-left, 0.5/0.5 is center, 1/1 is bottom-right). alpha is 0..1
// (0 = invisible, 1 = fully opaque).
//
// _clear restores the natural 1:1 opaque composite path (zero-cost
// when no animation is active). Both calls invalidate the window's
// frame so the next composite repaints the affected screen area.
int tig_window_transform_set(tig_window_handle_t window_handle,
    float scale_x,
    float scale_y,
    float alpha,
    float anchor_rel_x,
    float anchor_rel_y);
int tig_window_transform_clear(tig_window_handle_t window_handle);

// CE: modulate the per-window translucent-black tint amount at
// composite time. Compositor multiplies the tint r/g/b by `reveal`
// before applying — 0.0 = no darkening (window appears fully
// opaque where near-black would normally show the underlay), 1.0
// = full configured tint strength. Used by ui_anim to fade tint
// in smoothly after a scale+alpha entrance lands, masking the
// "snap to tinted" pop when the transform path clears. Default
// (set at window create) is 1.0.
int tig_window_tint_reveal_set(tig_window_handle_t window_handle, float reveal);

// CE: cache a pre-tinted snapshot of the window's VB. Used by the
// ui_anim transform path when the window also has tint_enabled —
// without a snapshot the compositor falls back to a per-pixel
// integrated blit (~10× slower than SDL_BlitSurfaceScaled). The
// snapshot is the window's VB with near-black pixels replaced by
// the underlay tinted at the window's NATURAL screen position (the
// "at-rest" tinted look). Compositor uses SDL_BlitSurfaceScaled +
// alpha mod on the snapshot during the brief anim window.
//
// Brief staleness: the snapshot is captured ONCE at anim start;
// during the ~130ms anim the underlay (iso world) may drift a few
// pixels — visible only as a very slight smear in the panel's
// dark-area see-through, almost certainly imperceptible.
//
// _release frees the snapshot VB. Safe to call when no snapshot
// is allocated (no-op).
int tig_window_tint_snapshot_capture(tig_window_handle_t window_handle);
void tig_window_tint_snapshot_release(tig_window_handle_t window_handle);
TigVideoBuffer* tig_window_tint_snapshot_get(tig_window_handle_t window_handle);

// CE: register a callback that fires whenever a tig window is
// destroyed. Used by ui_anim to cancel any in-flight tween targeting
// a now-dead handle (avoids stale-handle writes on the next ping).
// Single-slot registration; passing NULL clears.
void tig_window_destroy_notify_set(void (*func)(tig_window_handle_t));

#ifdef __cplusplus
}
#endif

#endif /* TIG_WINDOW_H_ */
