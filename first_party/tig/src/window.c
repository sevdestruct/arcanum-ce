#include "tig/window.h"

#include <ctype.h>

#include "tig/art.h"
#include "tig/bmp.h"
#include "tig/button.h"
#include "tig/color.h"
#include "tig/core.h"
#include "tig/debug.h"
#include "tig/font.h"
#include "tig/mouse.h"
#include "tig/rect.h"
#include "tig/video.h"

#define TIG_WINDOW_MAX 50
#define TIG_WINDOW_BUTTON_MAX 200

// The following constants define layout and visual style of modal dialog
// created by `tig_window_modal_dialog`.
//
// NOTE: For engine-like thing like TIG it's too much hardcoded stuff. Either
// it should not be part of the engine at all, or at least art ids should be
// externalized into `TigWindowModalDialogInfo`.

#define MODAL_DIALOG_WIDTH 325
#define MODAL_DIALOG_HEIGHT 136

#define MODAL_DIALOG_TEXT_X 30
#define MODAL_DIALOG_TEXT_Y 14
#define MODAL_DIALOG_TEXT_WIDTH 265
#define MODAL_DIALOG_TEXT_HEIGHT 65

#define MODAL_DIALOG_CENTER_BUTTON_X 149
#define MODAL_DIALOG_CENTER_BUTTON_Y 102

#define MODAL_DIALOG_OK_BUTTON_X 73
#define MODAL_DIALOG_OK_BUTTON_Y 102

#define MODAL_DIALOG_CANCEL_BUTTON_X 225
#define MODAL_DIALOG_CANCEL_BUTTON_Y 102

#define MODAL_DIALOG_FONT_ART_NUM 229
#define MODAL_DIALOG_BACKGROUND_ART_NUM 822
#define MODAL_DIALOG_OK_BUTTON_ART_NUM 823
#define MODAL_DIALOG_CANCEL_BUTTON_ART_NUM 824

typedef enum TigWindowUsage {
    TIG_WINDOW_USAGE_FREE = 1 << 0,
} TigWindowUsafe;

typedef struct TigWindow {
    /* 0000 */ unsigned int usage;
    /* 0004 */ unsigned int flags;
    /* 0008 */ TigRect frame;
    /* 0018 */ TigRect bounds;
    /* 0028 */ unsigned int background_color;
    /* 002C */ unsigned int color_key;
    /* 0030 */ TigVideoBuffer* video_buffer;
    /* 0034 */ TigVideoBuffer* secondary_video_buffer;
    /* 0038 */ int num_buttons;
    /* 003C */ tig_button_handle_t buttons[TIG_WINDOW_BUTTON_MAX];
    /* 035C */ TigWindowMessageFilterFunc message_filter;
    // CE: optional screen-coords composite clip. When has_clip,
    // the compositor uses (frame ∩ clip_rect) to decide what to
    // paint; otherwise frame alone defines visibility.
    bool has_clip;
    TigRect clip_rect;
    // CE: optional translucent-black pathway. When enabled, the
    // compositor routes this window's blit through
    // tig_video_blit_near_black_tinted, which replaces near-black
    // source pixels with subtract-tinted underlay pixels and copies
    // other pixels through opaque. Used by the HUD bar to show a
    // darkened world through its dark panel regions.
    bool tint_enabled;
    uint8_t tint_threshold;
    uint8_t tint_r;
    uint8_t tint_g;
    uint8_t tint_b;
    tig_window_handle_t tint_underlay;
    // CE: optional world-knockout pathway. When enabled, the compositor
    // routes this window's blit through tig_video_blit_knockout, which
    // replaces pixels matching knockout_key (RGB) with the raw underlay
    // (game world) pixel — a true untinted cut-out for custom window
    // shapes. Separate from the near-black tint above; a window uses one
    // or the other. knockout_underlay is the world source.
    bool knockout_enabled;
    tig_color_t knockout_key;
    tig_window_handle_t knockout_underlay;
    // CE: optional per-window scale + alpha + scale-anchor for the
    // ui_anim spring-driven entrance/exit animations. When
    // transform_active, the compositor blits this window's VB to a
    // scaled dst rect (around the anchor) with const-alpha blending
    // instead of the standard 1:1 opaque blit. Fast-path when
    // !transform_active so unanimated windows pay zero cost.
    bool transform_active;
    float transform_scale_x;
    float transform_scale_y;
    float transform_alpha;       // 0.0..1.0
    float transform_anchor_x;    // 0.0..1.0, frame-relative
    float transform_anchor_y;    // 0.0..1.0, frame-relative
    // CE: optional modulator on the translucent-black tint amount.
    // Compositor tint pathway multiplies tint_r/g/b by tint_reveal
    // before writing — 0.0 = no darkening (window appears opaque
    // where it would normally show the underlay through near-black
    // areas), 1.0 = full configured tint strength. Used by ui_anim
    // to fade tint in smoothly after a scale+alpha entrance lands,
    // masking the "snap to tinted" pop that would otherwise occur
    // when the transform path clears and tint takes over.
    float tint_reveal;
    // CE (feature/perf-gpu-accel step 6): when set, the compositor fills this
    // window's region with transparent (alpha 0) instead of blitting its VB, so
    // the GPU world drawn under the framebuffer at flip shows through. The game
    // sets this on the iso window only while "gpu-present" is active.
    bool gpu_world;
} TigWindow;

// CE: optional notification when a window is destroyed. ui_anim
// registers itself so any in-flight tween targeting the now-dead
// handle cancels cleanly (no on_complete fires — caller destroyed
// directly, not via the tween path). Single-slot is enough; if other
// modules need notifications later, this generalizes to a list.
static void (*tig_window_destroy_notify_func)(tig_window_handle_t) = NULL;

static int tig_window_free_index(void);
static void tig_window_gpu_composite(void);
static int tig_window_handle_to_index(tig_window_handle_t window_handle);
static tig_window_handle_t tig_window_index_to_handle(int window_index);
static void push_window_stack(tig_window_handle_t window_handle);
static bool pop_window_stack(tig_window_handle_t window_handle);
static bool tig_window_modal_dialog_message_filter(TigMessage* msg);
static void tig_window_modal_dialog_close(void);
static void tig_window_modal_dialog_refresh(TigRect* rect);
static bool tig_window_modal_dialog_create_buttons(int type, tig_window_handle_t window_handle);
static bool tig_window_modal_dialog_init(void);
static void tig_window_modal_dialog_exit(void);

// 0x5BED98
static tig_window_handle_t tig_window_modal_dialog_window_handle = TIG_WINDOW_HANDLE_INVALID;

// CE: modal close is now deferred — the filter/process sets this flag
// (via tig_window_modal_dialog_close) and the modal loop plays a brief
// exit animation before actually destroying the window.
static bool tig_window_modal_dialog_close_requested = false;

// CE: modal entrance/exit animation timing. The modal runs its own
// blocking pump and can't use the game's ui_anim, so the loop drives a
// time-based scale+alpha transform directly. Exit is faster than
// entrance (snappier dismissal).
#define MODAL_ANIM_SCALE_FROM 0.94f
#define MODAL_ANIM_ENTER_MS 140u
#define MODAL_ANIM_EXIT_MS 90u

// CE: modal-dialog auto-tint state. When enabled (configured by
// gamelib at iso-interface-create time and cleared at destroy),
// tig_window_modal_dialog auto-calls tig_window_tint_enable on each
// modal it creates so the modal's near-black panel regions show
// the tinted iso world through them — same effect we apply to the
// HUD bar. Disabled in pre-game contexts (gamelib never sets it
// before iso is up) so the title-screen mainmenu_bg doesn't get
// darkened behind quit-confirmation dialogs.
static bool tig_window_modal_tint_enabled = false;
static tig_window_handle_t tig_window_modal_tint_underlay = TIG_WINDOW_HANDLE_INVALID;
static uint8_t tig_window_modal_tint_threshold = 8;
static uint8_t tig_window_modal_tint_r = 30;
static uint8_t tig_window_modal_tint_g = 30;
static uint8_t tig_window_modal_tint_b = 30;

// 0x5BEDA0
static TigRect tig_window_modal_dialog_bounds = { 0, 0, MODAL_DIALOG_WIDTH, MODAL_DIALOG_HEIGHT };

// 0x604758
static TigWindowModalDialogInfo tig_window_modal_dialog_info;

// 0x604778
static TigWindow windows[TIG_WINDOW_MAX];

// 0x60F038
static tig_button_handle_t tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_COUNT];

// 0x60F040
static TigWindowModalDialogChoice tig_message_modal_dialog_choice;

// 0x60F044
static tig_window_handle_t tig_window_stack[TIG_WINDOW_MAX];

// 0x60F10C
static int tig_window_ctx_flags;

// 0x60F110
static TigRect tig_window_screen_rect;

// 0x60F124
static bool tig_window_initialized;

// 0x60F128
static int tig_window_num_windows;

// 0x60F12C
static TigRectListNode* tig_window_dirty_rects;

static bool tig_window_invalidate_suppressed;

// 0x60F130
static tig_font_handle_t tig_window_modal_dialog_font;

// 0x51CAD0
int tig_window_init(TigInitInfo* init_info)
{
    int index;

    tig_window_num_windows = 0;
    tig_window_dirty_rects = NULL;

    tig_window_screen_rect.x = 0;
    tig_window_screen_rect.y = 0;
    tig_window_screen_rect.width = init_info->width;
    tig_window_screen_rect.height = init_info->height;

    for (index = 0; index < TIG_WINDOW_MAX; index++) {
        windows[index].usage = TIG_WINDOW_USAGE_FREE;
    }

    tig_window_ctx_flags = init_info->flags;
    tig_window_initialized = true;

    // CE (full GPU/UI): register the GPU window compositor used by gpu-ui mode.
    tig_video_set_ui_composite_func(tig_window_gpu_composite);

    return TIG_OK;
}

// 0x51CB30
void tig_window_exit(void)
{
    int window_index;
    tig_window_handle_t window_handle;
    TigRectListNode* curr;

    for (window_index = 0; window_index < TIG_WINDOW_MAX; window_index++) {
        if ((windows[window_index].usage & TIG_WINDOW_USAGE_FREE) == 0) {
            window_handle = tig_window_index_to_handle(window_index);
            tig_window_destroy(window_handle);
        }
    }

    while (tig_window_dirty_rects != NULL) {
        curr = tig_window_dirty_rects;
        tig_window_dirty_rects = curr->next;
        tig_rect_node_destroy(curr);
    }

    tig_window_initialized = false;
}

// 0x51CB90
int tig_window_create(TigWindowData* window_data, tig_window_handle_t* window_handle_ptr)
{
    int window_index;
    TigWindow* win;
    TigVideoBufferCreateInfo vb_create_info;
    int rc;

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (tig_window_num_windows >= TIG_WINDOW_MAX) {
        return TIG_ERR_OUT_OF_HANDLES;
    }

    window_index = tig_window_free_index();
    win = &(windows[window_index]);

    win->flags = window_data->flags;

    if ((window_data->flags & TIG_WINDOW_MODAL) != 0) {
        win->flags |= TIG_WINDOW_MESSAGE_FILTER;
    }

    win->message_filter = window_data->message_filter;
    win->frame = window_data->rect;
    win->bounds.x = 0;
    win->bounds.y = 0;
    win->bounds.width = window_data->rect.width;
    win->bounds.height = window_data->rect.height;
    win->background_color = window_data->background_color;
    win->color_key = window_data->color_key;
    win->num_buttons = 0;
    win->has_clip = false;
    win->tint_enabled = false;
    win->tint_threshold = 0;
    win->tint_r = 0;
    win->tint_g = 0;
    win->tint_b = 0;
    win->tint_underlay = TIG_WINDOW_HANDLE_INVALID;
    win->knockout_enabled = false;
    win->knockout_key = 0;
    win->knockout_underlay = TIG_WINDOW_HANDLE_INVALID;
    // CE: ui_anim transform defaults — inactive, identity scale/alpha,
    // anchor at frame center. Compositor short-circuits the transform
    // path when transform_active is false so unanimated windows are
    // zero-cost.
    win->transform_active = false;
    win->transform_scale_x = 1.0f;
    win->transform_scale_y = 1.0f;
    win->transform_alpha = 1.0f;
    win->transform_anchor_x = 0.5f;
    win->transform_anchor_y = 0.5f;
    // CE: tint at full strength when enabled (no fade modulation).
    win->tint_reveal = 1.0f;

    vb_create_info.flags = 0;

    if ((window_data->flags & TIG_WINDOW_TRANSPARENT) != 0) {
        vb_create_info.flags |= TIG_VIDEO_BUFFER_CREATE_COLOR_KEY;
    }

    if ((window_data->flags & TIG_WINDOW_VIDEO_MEMORY) != 0) {
        vb_create_info.flags |= TIG_VIDEO_BUFFER_CREATE_VIDEO_MEMORY;
    } else {
        vb_create_info.flags |= TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    }

    if ((window_data->flags & TIG_WINDOW_RENDER_TARGET) != 0) {
        vb_create_info.flags |= TIG_VIDEO_BUFFER_CREATE_RENDER_TARGET;
    }

    vb_create_info.width = window_data->rect.width;
    vb_create_info.height = window_data->rect.height;
    vb_create_info.background_color = window_data->background_color;
    vb_create_info.color_key = window_data->color_key;

    rc = tig_video_buffer_create(&vb_create_info, &(win->video_buffer));
    if (rc != TIG_OK) {
        return rc;
    }

    if ((tig_window_ctx_flags & TIG_INITIALIZE_SCRATCH_BUFFER) != 0) {
        if ((window_data->flags & TIG_WINDOW_TRANSPARENT) != 0) {
            vb_create_info.flags &= ~TIG_VIDEO_BUFFER_CREATE_COLOR_KEY;

            rc = tig_video_buffer_create(&vb_create_info, &(win->secondary_video_buffer));
            if (rc != TIG_OK) {
                tig_video_buffer_destroy(win->video_buffer);
                return rc;
            }
        }
    }

    *window_handle_ptr = tig_window_index_to_handle(window_index);
    push_window_stack(*window_handle_ptr);

    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(win->frame));
    }

    win->usage = 0;

    return TIG_OK;
}

// 0x51CD30
int tig_window_destroy(tig_window_handle_t window_handle)
{
    int window_index;
    TigWindow* win;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_destroy: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    // CE: notify any registered observer (ui_anim) before tearing
    // the window down so it can cancel pending tweens targeting this
    // handle. Fired before button_destroy in case the observer also
    // wants to invalidate or cancel UI state tied to the buttons.
    if (tig_window_destroy_notify_func != NULL) {
        tig_window_destroy_notify_func(window_handle);
    }

    rc = tig_window_button_destroy(window_handle);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(win->frame));
    }

    tig_video_buffer_destroy(win->video_buffer);

    if ((tig_window_ctx_flags & TIG_INITIALIZE_SCRATCH_BUFFER) != 0
        && (win->flags & TIG_WINDOW_TRANSPARENT) != 0) {
        tig_video_buffer_destroy(win->secondary_video_buffer);
    }

    pop_window_stack(window_handle);

    win->usage = TIG_WINDOW_USAGE_FREE;

    return TIG_OK;
}

// 0x51CDE0
int tig_window_button_destroy(tig_window_handle_t window_handle)
{
    int window_index;
    TigWindow* win;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_button_destroy: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    while (win->num_buttons > 0) {
        rc = tig_button_destroy(win->buttons[0]);
        if (rc != TIG_OK) {
            return rc;
        }
    }

    return TIG_OK;
}

// 0x51CE50
int tig_window_message_filter_set(tig_window_handle_t window_handle, TigWindowMessageFilterFunc func)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_message_filter_set: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (func == NULL) {
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if ((win->flags & TIG_WINDOW_MESSAGE_FILTER) == 0) {
        return TIG_ERR_INVALID_PARAM;
    }

    win->message_filter = func;

    return TIG_OK;
}

// 0x51CEB0
int tig_window_data(tig_window_handle_t window_handle, TigWindowData* window_data)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_data: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    window_data->flags = win->flags;
    window_data->rect = win->frame;
    window_data->background_color = win->background_color;
    window_data->color_key = win->color_key;
    window_data->message_filter = win->message_filter;

    return TIG_OK;
}

// CE: <math.h>-free floor/ceil that handle negative inputs (the
// oversized mainmenu backdrop has a negative frame.x/.y).
static int tig_floor_i(float v)
{
    int i = (int)v;
    return (v < (float)i) ? i - 1 : i;
}
static int tig_ceil_i(float v)
{
    int i = (int)v;
    return (v > (float)i) ? i + 1 : i;
}

// CE: compute the integer screen-space dst rect for a ui_anim
// transformed window.
//
// Rounds the dst edges OUTWARD — floor the left/top, ceil the
// right/bottom. Two reasons:
//
//  1. Settle stability. When a window rests at a non-1.0 scale
//     (the mainmenu backdrop recede sits at 0.96), the resting
//     dst edge can land exactly on a .5 boundary for common
//     resolutions (e.g. a 1080-tall screen: the centered backdrop
//     frame's integer offset is a half-pixel off, so the bottom
//     edge hits N.5 at scale 0.96). Round-to-nearest flips across
//     .5 — the penultimate spring frame rounds one way, the
//     finalize-to-exact-target frame rounds the other → a visible
//     1px snap right as it locks in. ceil/floor never flip at .5
//     (ceil(N.49)==ceil(N.5)==N+1), so the dst is identical on the
//     last animating frame and the finalize frame — no snap.
//     (Windows that settle at scale 1.0 don't hit this: they call
//     tig_window_transform_clear and revert to a crisp 1:1 blit.)
//
//  2. Coverage. Outward rounding guarantees the dst fully covers
//     the scaled content (and, for the oversized backdrop, the
//     screen) — no 1px black gap at an edge.
//
// Cost: the dst is up to 1px larger than the exact scaled size per
// edge, a <0.1% overscale at these scales — imperceptible. The
// center stays stable (floor(C-d)+ceil(C+d) is constant), so there's
// no lateral wobble. Springs are overdamped (no overshoot past 1.0),
// so shrinking windows never paint outside their natural frame.
static void tig_window_transform_dst(const TigRect* frame,
    float scale_x, float scale_y,
    float anchor_x, float anchor_y,
    int* out_x, int* out_y, int* out_w, int* out_h)
{
    float scaled_left = (float)frame->x
        + (float)frame->width * anchor_x * (1.0f - scale_x);
    float scaled_top = (float)frame->y
        + (float)frame->height * anchor_y * (1.0f - scale_y);
    float scaled_right = scaled_left + (float)frame->width * scale_x;
    float scaled_bottom = scaled_top + (float)frame->height * scale_y;

    int left = tig_floor_i(scaled_left);
    int top = tig_floor_i(scaled_top);
    int right = tig_ceil_i(scaled_right);
    int bottom = tig_ceil_i(scaled_bottom);

    *out_x = left;
    *out_y = top;
    *out_w = right - left;
    *out_h = bottom - top;
}

// 0x51CF40
int tig_window_display(void)
{
    int rc;
    TigRectListNode* node;
    TigMouseState mouse_state;
    TigRect dirty_union;
    bool dirty_union_set = false;

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    if (tig_window_dirty_rects != NULL) {
        rc = tig_mouse_get_state(&mouse_state);
        if (rc != TIG_OK) {
            return rc;
        }

        node = tig_window_dirty_rects;
        while (node != NULL) {
            tig_window_dirty_rects = node->next;

            // Accumulate the bounding rect of every area we composite. Used
            // below to hint tig_video_flip to do a partial-rect upload to the
            // GPU texture instead of re-uploading the whole surface every
            // frame (~8MB at 1080p, ~7ms CPU-side cost from earlier perf
            // logs). The hint is just an optimization — if we miss a rect
            // here we'd skip uploading bytes that did change and display
            // stale pixels, so the union must cover every write to the
            // surface during this present cycle.
            if (!dirty_union_set) {
                dirty_union = node->rect;
                dirty_union_set = true;
            } else {
                int x1 = dirty_union.x < node->rect.x ? dirty_union.x : node->rect.x;
                int y1 = dirty_union.y < node->rect.y ? dirty_union.y : node->rect.y;
                int dx2 = dirty_union.x + dirty_union.width;
                int dy2 = dirty_union.y + dirty_union.height;
                int nx2 = node->rect.x + node->rect.width;
                int ny2 = node->rect.y + node->rect.height;
                int x2 = dx2 > nx2 ? dx2 : nx2;
                int y2 = dy2 > ny2 ? dy2 : ny2;
                dirty_union.x = x1;
                dirty_union.y = y1;
                dirty_union.width = x2 - x1;
                dirty_union.height = y2 - y1;
            }

            sub_51D050(&(node->rect), NULL, 0, 0, TIG_WINDOW_TOP);
            tig_rect_node_destroy(node);

            node = tig_window_dirty_rects;
        }

        if ((mouse_state.flags & TIG_MOUSE_STATE_HIDDEN) == 0) {
            // tig_mouse_display blits the cursor sprite onto the surface at
            // mouse_state.frame, so we must union it into the dirty region.
            // CE (full GPU/UI): in gpu-ui the framebuffer isn't drawn, so the
            // cursor is composited on the GPU at the end of the window walk
            // (tig_mouse_gpu_composite) instead of blitted here.
            if (!tig_video_gpu_ui_is_enabled()) {
                tig_mouse_display();
            }
            if (mouse_state.frame.width > 0 && mouse_state.frame.height > 0) {
                if (!dirty_union_set) {
                    dirty_union = mouse_state.frame;
                    dirty_union_set = true;
                } else {
                    int x1 = dirty_union.x < mouse_state.frame.x ? dirty_union.x : mouse_state.frame.x;
                    int y1 = dirty_union.y < mouse_state.frame.y ? dirty_union.y : mouse_state.frame.y;
                    int dx2 = dirty_union.x + dirty_union.width;
                    int dy2 = dirty_union.y + dirty_union.height;
                    int mx2 = mouse_state.frame.x + mouse_state.frame.width;
                    int my2 = mouse_state.frame.y + mouse_state.frame.height;
                    int x2 = dx2 > mx2 ? dx2 : mx2;
                    int y2 = dy2 > my2 ? dy2 : my2;
                    dirty_union.x = x1;
                    dirty_union.y = y1;
                    dirty_union.width = x2 - x1;
                    dirty_union.height = y2 - y1;
                }
            }
        }
    }

    if (dirty_union_set) {
        tig_video_set_present_dirty_rect(&dirty_union);
    }

    tig_video_display_fps();

    tig_video_flip();

    return TIG_OK;
}

// CE: resolve a tint underlay window's video buffer for the compositor, or NULL
// if the underlay handle is unset or points at a window slot that has since been
// freed (or has no buffer). Without the usage check the compositor would read a
// destroyed window's dangling video_buffer and lock its freed surface (observed
// crash: SDL_LockSurface on a -1 surface). off_x/off_y receive the underlay's
// negated frame origin (0 when there's no valid underlay).
static TigVideoBuffer* tig_window_tint_underlay_vb(tig_window_handle_t underlay,
    int* off_x, int* off_y)
{
    int uidx;

    *off_x = 0;
    *off_y = 0;
    if (underlay == TIG_WINDOW_HANDLE_INVALID) {
        return NULL;
    }
    uidx = tig_window_handle_to_index(underlay);
    if (uidx < 0 || uidx >= TIG_WINDOW_MAX) {
        return NULL;
    }
    if ((windows[uidx].usage & TIG_WINDOW_USAGE_FREE) != 0
        || windows[uidx].video_buffer == NULL) {
        return NULL;
    }
    *off_x = -windows[uidx].frame.x;
    *off_y = -windows[uidx].frame.y;
    return windows[uidx].video_buffer;
}

// 0x51D050
void sub_51D050(TigRect* src_rect, TigVideoBuffer* dst_video_buffer, int dx, int dy, int top_window_index)
{
    TigVideoBufferBlitInfo vb_blit_info;
    TigRectListNode* head;
    TigRectListNode* node;
    TigRect dirty_rect;
    TigRect clips[4];
    TigRect blt_src_rect;
    TigRect blt_dst_rect;
    TigWindow* wins[20];
    TigRect rects[20];
    int rc;
    int v45;
    int v47;
    int num_clips;
    int index;
    int v38 = 0;

    rc = tig_rect_intersection(src_rect, &tig_window_screen_rect, &dirty_rect);
    if (rc != TIG_OK) {
        return;
    }

    if (dst_video_buffer != NULL) {
        v45 = src_rect->x - dx;
        v47 = src_rect->y - dy;
    } else {
        v45 = 0;
        v47 = 0;
    }

    head = tig_rect_node_create();
    if (head == NULL) {
        return;
    }

    head->rect = dirty_rect;
    head->next = NULL;

    if (top_window_index == TIG_WINDOW_TOP) {
        top_window_index = tig_window_num_windows - 1;
    }

    while (top_window_index >= 0 && head != NULL) {
        tig_window_handle_t window_handle = tig_window_stack[top_window_index];
        unsigned int window_index = tig_window_handle_to_index(window_handle);
        TigWindow* win = &(windows[window_index]);

        if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
            TigRectListNode* curr = head;
            TigRectListNode* prev = NULL;

            // CE: clip-rect support. Use frame ∩ clip_rect as the
            // effective visible region when a clip is set. VB-source
            // math below still uses win->frame as the anchor, so
            // VB pixels stay at their natural positions — we just
            // composite a sub-band of them.
            TigRect effective_frame = win->frame;
            bool clip_removes_window = false;
            if (win->has_clip) {
                if (tig_rect_intersection(&(win->frame), &(win->clip_rect), &effective_frame) != TIG_OK) {
                    clip_removes_window = true;
                }
            }
            if (clip_removes_window) {
                top_window_index--;
                continue;
            }

            // CE: ui_anim transform — if active, the window paints to
            // a scaled dst rect (around the anchor) instead of its
            // natural frame. Recompute effective_frame so dirty-rect
            // intersection and rect-list propagation use the scaled
            // dst — surrounding area (outside the scaled rect, inside
            // the frame) falls through to whichever window is beneath.
            // Skip clip + transform combined (no callers need it; if
            // a caller hits this, clip wins so the transform inherits
            // the same scaled-out behavior).
            bool transform_active = win->transform_active
                && !win->has_clip;
            int transform_dst_x = 0, transform_dst_y = 0;
            int transform_dst_w = 0, transform_dst_h = 0;
            if (transform_active) {
                // CE: independent-edge rounding (see
                // tig_window_transform_dst) — width changes in 1px
                // steps with a sub-pixel-stable anchor, so the dst
                // converges smoothly through the spring's settle tail
                // with no terminal 1-2px snap (the old round-width-
                // then-center-with-parity approach stepped the width
                // in 2px chunks, which was visible on the slow
                // mainmenu backdrop recede).
                tig_window_transform_dst(&win->frame,
                    win->transform_scale_x, win->transform_scale_y,
                    win->transform_anchor_x, win->transform_anchor_y,
                    &transform_dst_x, &transform_dst_y,
                    &transform_dst_w, &transform_dst_h);
                if (transform_dst_w <= 0 || transform_dst_h <= 0
                    || win->transform_alpha <= 0.0f) {
                    // Effectively invisible — skip this window's
                    // blit. The dirty-rect list isn't clipped here so
                    // lower windows fully paint the area.
                    top_window_index--;
                    continue;
                }
                effective_frame.x = transform_dst_x;
                effective_frame.y = transform_dst_y;
                effective_frame.width = transform_dst_w;
                effective_frame.height = transform_dst_h;
            }

            while (curr != NULL) {
                rc = tig_rect_intersection(&(curr->rect), &effective_frame, &dirty_rect);
                if (rc == TIG_OK) {
                    // TODO: Not sure how to represent it one to one.
                    bool cont;
                    TigVideoBuffer* src_video_buffer = win->video_buffer;
                    if (transform_active && v38 < 20) {
                        // CE: ui_anim transform path needs lower
                        // windows painted FIRST so the alpha blend has
                        // valid dst pixels to blend with. Defer to the
                        // post-pass like the transparent path does;
                        // unlike transparent we explicitly don't clip
                        // the dirty against effective_frame either
                        // (skip via cont=false), so the iso world (or
                        // whatever is beneath) propagates through and
                        // paints first.
                        wins[v38] = win;
                        rects[v38] = dirty_rect;
                        v38++;
                        cont = false;
                    } else if ((win->flags & TIG_WINDOW_TRANSPARENT) == 0) {
                        cont = true;
                    } else if ((tig_window_ctx_flags & TIG_INITIALIZE_SCRATCH_BUFFER) != 0) {
                        sub_51D050(&dirty_rect,
                            win->secondary_video_buffer,
                            dirty_rect.x - win->frame.x,
                            dirty_rect.y - win->frame.y,
                            window_index - 1);
                        src_video_buffer = win->secondary_video_buffer;
                        cont = true;
                    } else if (v38 < 20) {
                        wins[v38] = win;
                        rects[v38] = dirty_rect;
                        v38++;
                        cont = false;
                    } else {
                        cont = true;
                    }

                    if (cont) {
                        blt_src_rect.x = dirty_rect.x - win->frame.x;
                        blt_src_rect.y = dirty_rect.y - win->frame.y;
                        blt_src_rect.width = dirty_rect.width;
                        blt_src_rect.height = dirty_rect.height;

                        if ((win->flags & TIG_WINDOW_TRANSPARENT) != 0
                            && (tig_window_ctx_flags & TIG_INITIALIZE_SCRATCH_BUFFER) != 0) {
                            vb_blit_info.flags = 0;
                            vb_blit_info.src_video_buffer = win->video_buffer;
                            vb_blit_info.src_rect = &blt_src_rect;
                            vb_blit_info.dst_video_buffer = win->secondary_video_buffer;
                            vb_blit_info.dst_rect = &blt_src_rect;
                            tig_video_buffer_blit(&vb_blit_info);
                        }

                        blt_dst_rect.x = dirty_rect.x - v45;
                        blt_dst_rect.y = dirty_rect.y - v47;
                        blt_dst_rect.width = blt_src_rect.width;
                        blt_dst_rect.height = blt_src_rect.height;

                        if (dst_video_buffer != NULL) {
                            vb_blit_info.flags = 0;
                            vb_blit_info.src_video_buffer = src_video_buffer;
                            vb_blit_info.src_rect = &blt_src_rect;
                            vb_blit_info.dst_video_buffer = dst_video_buffer;
                            vb_blit_info.dst_rect = &blt_dst_rect;
                            tig_video_buffer_blit(&vb_blit_info);
                        } else if (win->tint_enabled) {
                            // CE: translucent-black tint pathway — near-
                            // black source pixels get replaced with
                            // subtract-tinted underlay pixels read
                            // directly from the underlay VB (bypasses
                            // the layered screen-surface route, which is
                            // unreliable when the underlay window uses
                            // VIDEO_MEMORY).
                            TigVideoBuffer* under_vb = NULL;
                            int under_off_x = 0;
                            int under_off_y = 0;
                            under_vb = tig_window_tint_underlay_vb(
                                win->tint_underlay, &under_off_x, &under_off_y);
                            // CE: tint_reveal controls how much of
                            // the see-through tinted result is mixed
                            // in vs the opaque source pixel. 0 = the
                            // panel reads fully opaque (near-black
                            // stays solid); 1 = the configured tint
                            // see-through at full strength. Used by
                            // ui_anim to fade the see-through IN
                            // after a window's scale+alpha entrance
                            // settles.
                            uint8_t reveal_inline = (uint8_t)(
                                win->tint_reveal * 255.0f + 0.5f);
                            tig_video_blit_near_black_tinted(src_video_buffer,
                                &blt_src_rect,
                                &blt_dst_rect,
                                under_vb,
                                under_off_x,
                                under_off_y,
                                win->tint_threshold,
                                win->tint_r,
                                win->tint_g,
                                win->tint_b,
                                reveal_inline);
                        } else if (transform_active) {
                            // CE: scale + alpha composite path. Re-
                            // project the dirty-rect-derived src
                            // (currently window-local based on
                            // frame) into the transformed dst's
                            // proportional coords, since dirty_rect
                            // now lives inside the SCALED dst rect.
                            float inv_sx = 1.0f / win->transform_scale_x;
                            float inv_sy = 1.0f / win->transform_scale_y;
                            blt_src_rect.x = (int)((float)(dirty_rect.x - transform_dst_x) * inv_sx);
                            blt_src_rect.y = (int)((float)(dirty_rect.y - transform_dst_y) * inv_sy);
                            blt_src_rect.width = (int)((float)dirty_rect.width * inv_sx);
                            blt_src_rect.height = (int)((float)dirty_rect.height * inv_sy);
                            if (blt_src_rect.width <= 0) blt_src_rect.width = 1;
                            if (blt_src_rect.height <= 0) blt_src_rect.height = 1;
                            uint8_t a = (uint8_t)(win->transform_alpha * 255.0f + 0.5f);
                            tig_video_blit_scaled_alpha(src_video_buffer,
                                &blt_src_rect, &blt_dst_rect, a);
                        } else if (win->gpu_world && !tig_video_gpu_ui_is_enabled()) {
                            // CE (step 6): gpu-present composites the GPU world
                            // UNDER the framebuffer at flip, so paint this region
                            // transparent to let it show through. In gpu-ui the
                            // framebuffer isn't drawn (the GPU window walk is), so
                            // a transparent fill here would cache BLACK behind any
                            // translucent window — which then shows when zoom falls
                            // back to the framebuffer. Blit the CPU world instead
                            // so the cached backdrop is world content, not black.
                            tig_video_fill_transparent(&blt_dst_rect);
                        } else {
                            tig_video_blit(src_video_buffer, &blt_src_rect, &blt_dst_rect);
                        }

                        // CE: world-knockout overlay — after the base blit
                        // (opaque, near-black-tinted, or the scaled
                        // transform blit) above, punch the key-colour pixels
                        // through to the raw world. Runs on top so a window
                        // can combine the near-black see-through with hard
                        // knockouts. blt_src_rect/blt_dst_rect carry whatever
                        // the taken branch left (including the scaled
                        // transform rects), and tig_video_blit_knockout
                        // samples the source accordingly, so the cut-out
                        // tracks the panel through its entrance/exit
                        // animation (no magenta marker flash).
                        if (dst_video_buffer == NULL && win->knockout_enabled) {
                            TigVideoBuffer* ko_under = NULL;
                            int ko_off_x = 0;
                            int ko_off_y = 0;
                            ko_under = tig_window_tint_underlay_vb(
                                win->knockout_underlay, &ko_off_x, &ko_off_y);
                            tig_video_blit_knockout(src_video_buffer,
                                &blt_src_rect, &blt_dst_rect,
                                ko_under, ko_off_x, ko_off_y,
                                win->knockout_key);
                        }

                        // CE: clip against the EFFECTIVE frame (frame ∩ clip_rect)
                        // so the un-clipped portions of the dirty rect remain in
                        // the list and propagate down the stack to the window
                        // beneath. Using win->frame here would mark all of the
                        // bar's frame as "covered" even though the clip exposes
                        // most of it — leaving stale pixels (smearing) in the
                        // uncovered area.
                        num_clips = tig_rect_clip(&(curr->rect), &effective_frame, clips);
                        for (index = 0; index < num_clips; index++) {
                            node = tig_rect_node_create();
                            if (node == NULL) {
                                break;
                            }

                            node->rect = clips[index];
                            node->next = curr->next;
                            curr->next = node;
                        }

                        if (curr == head) {
                            head = curr->next;
                            tig_rect_node_destroy(curr);
                            curr = head;
                        } else {
                            prev->next = curr->next;
                            tig_rect_node_destroy(curr);
                            curr = prev->next;
                        }

                        continue;
                    }
                }
                prev = curr;
                curr = curr->next;
            }
        }

        top_window_index--;
    }

    while (head != NULL) {
        node = head;
        head = head->next;
        tig_video_fill(&(node->rect), 0);
        tig_rect_node_destroy(node);
    }

    --v38;
    while (v38 >= 0) {
        // CE: for ui_anim transformed windows, the queued rect lives
        // inside the SCALED dst (not the natural frame). Recompute
        // the transform geometry here so the proportional src/dst
        // math works — can't reuse the natural-frame src offset.
        bool defer_transform = wins[v38]->transform_active
            && !wins[v38]->has_clip;
        int tx_dx = 0, tx_dy = 0, tx_dw = 0, tx_dh = 0;
        if (defer_transform) {
            // CE: independent-edge rounding — MUST match the pre-pass
            // computation exactly (same tig_window_transform_dst) so
            // the queued-rect src reprojection below maps correctly.
            tig_window_transform_dst(&wins[v38]->frame,
                wins[v38]->transform_scale_x, wins[v38]->transform_scale_y,
                wins[v38]->transform_anchor_x, wins[v38]->transform_anchor_y,
                &tx_dx, &tx_dy, &tx_dw, &tx_dh);
        }

        blt_src_rect.x = rects[v38].x - wins[v38]->frame.x;
        blt_src_rect.y = rects[v38].y - wins[v38]->frame.y;
        blt_src_rect.width = rects[v38].width;
        blt_src_rect.height = rects[v38].height;

        blt_dst_rect.x = rects[v38].x - v45;
        blt_dst_rect.y = rects[v38].y - v47;
        blt_dst_rect.width = rects[v38].width;
        blt_dst_rect.height = rects[v38].height;

        if (dst_video_buffer != NULL) {
            vb_blit_info.flags = 0;
            vb_blit_info.src_video_buffer = wins[v38]->video_buffer;
            vb_blit_info.dst_video_buffer = dst_video_buffer;
            vb_blit_info.src_rect = &blt_src_rect;
            vb_blit_info.dst_rect = &blt_dst_rect;
            tig_video_buffer_blit(&vb_blit_info);
        } else if (defer_transform) {
            // Re-project queued screen rect into window-local src
            // (proportional to the scaled dst), keep dst as queued
            // screen rect.
            float inv_sx = 1.0f / wins[v38]->transform_scale_x;
            float inv_sy = 1.0f / wins[v38]->transform_scale_y;
            if (tx_dw > 0 && tx_dh > 0) {
                blt_src_rect.x = (int)((float)(rects[v38].x - tx_dx) * inv_sx);
                blt_src_rect.y = (int)((float)(rects[v38].y - tx_dy) * inv_sy);
                blt_src_rect.width = (int)((float)rects[v38].width * inv_sx);
                blt_src_rect.height = (int)((float)rects[v38].height * inv_sy);
                if (blt_src_rect.width <= 0) blt_src_rect.width = 1;
                if (blt_src_rect.height <= 0) blt_src_rect.height = 1;
                uint8_t a = (uint8_t)(wins[v38]->transform_alpha * 255.0f + 0.5f);
                if (wins[v38]->tint_enabled) {
                    // CE: integrated scale + tint + alpha in one pass —
                    // the near-black see-through tracks the live underlay
                    // each frame during the scale/alpha phase.
                    TigVideoBuffer* under_vb = NULL;
                    int under_off_x = 0;
                    int under_off_y = 0;
                    under_vb = tig_window_tint_underlay_vb(
                        wins[v38]->tint_underlay, &under_off_x, &under_off_y);
                    tig_video_blit_transform_tinted(wins[v38]->video_buffer,
                        &blt_src_rect, &blt_dst_rect,
                        under_vb, under_off_x, under_off_y,
                        wins[v38]->tint_threshold,
                        wins[v38]->tint_r,
                        wins[v38]->tint_g,
                        wins[v38]->tint_b,
                        a);
                } else {
                    tig_video_blit_scaled_alpha(wins[v38]->video_buffer,
                        &blt_src_rect, &blt_dst_rect, a);
                }
            }
        } else if (wins[v38]->tint_enabled) {
            TigVideoBuffer* under_vb = NULL;
            int under_off_x = 0;
            int under_off_y = 0;
            under_vb = tig_window_tint_underlay_vb(
                wins[v38]->tint_underlay, &under_off_x, &under_off_y);
            uint8_t reveal_def = (uint8_t)(wins[v38]->tint_reveal * 255.0f + 0.5f);
            tig_video_blit_near_black_tinted(wins[v38]->video_buffer,
                &blt_src_rect,
                &blt_dst_rect,
                under_vb,
                under_off_x,
                under_off_y,
                wins[v38]->tint_threshold,
                wins[v38]->tint_r,
                wins[v38]->tint_g,
                wins[v38]->tint_b,
                reveal_def);
        } else {
            tig_video_blit(wins[v38]->video_buffer, &blt_src_rect, &blt_dst_rect);
        }

        // CE: world-knockout overlay — punch key-colour pixels through to
        // the raw world on top of the base blit, so knockouts compose with
        // the near-black see-through. Runs through the transform too: the
        // knockout samples the (scaled) src rect, so the cut-out tracks the
        // panel and the magenta marker never flashes during the animation.
        if (dst_video_buffer == NULL && wins[v38]->knockout_enabled) {
            TigVideoBuffer* ko_under = NULL;
            int ko_off_x = 0;
            int ko_off_y = 0;
            ko_under = tig_window_tint_underlay_vb(
                wins[v38]->knockout_underlay, &ko_off_x, &ko_off_y);
            tig_video_blit_knockout(wins[v38]->video_buffer,
                &blt_src_rect, &blt_dst_rect,
                ko_under, ko_off_x, ko_off_y,
                wins[v38]->knockout_key);
        }

        v38--;
    }
}

// CE (full GPU/UI): composite the UI window stack directly on the GPU. Registered
// as the gpu-ui UI-composite callback; tig_video_flip invokes it with the render
// target cleared and the world+roof underlay already drawn. Walks the stack
// bottom-to-top so transparent / transform windows alpha-blend over whatever is
// beneath — the dirty-rect + deferred-pass machinery in sub_51D050 exists only
// for partial CPU compositing and isn't needed for a full-frame GPU recomposite.
// The gpu_world (iso) window is skipped (its world+roof were drawn by the flip).
// tint / knockout windows render plain for now (returns in stage 2).
static void tig_window_gpu_composite(void)
{
    int i;
    for (i = 0; i < tig_window_num_windows; i++) {
        tig_window_handle_t window_handle = tig_window_stack[i];
        unsigned int window_index = tig_window_handle_to_index(window_handle);
        TigWindow* win = &(windows[window_index]);

        if ((win->flags & TIG_WINDOW_HIDDEN) != 0) {
            continue;
        }
        if (win->gpu_world || win->video_buffer == NULL) {
            continue;
        }

        // dst rect: scaled (ui_anim transform) or the natural frame.
        TigRect dst_rect = win->frame;
        float alpha = 1.0f;
        if (win->transform_active && !win->has_clip) {
            int tx, ty, tw, th;
            tig_window_transform_dst(&win->frame,
                win->transform_scale_x, win->transform_scale_y,
                win->transform_anchor_x, win->transform_anchor_y,
                &tx, &ty, &tw, &th);
            if (tw <= 0 || th <= 0 || win->transform_alpha <= 0.0f) {
                continue;
            }
            dst_rect.x = tx;
            dst_rect.y = ty;
            dst_rect.width = tw;
            dst_rect.height = th;
            alpha = win->transform_alpha;
        }

        // CE (full GPU/UI stage 2): translucent-black HUD bar — the tint mirror
        // makes near-black art pixels darken the live GPU world beneath when
        // composited (BLEND), reproducing tig_video_blit_near_black_tinted.
        SDL_Texture* tex;
        if (win->tint_enabled) {
            tex = tig_video_buffer_gpu_tint_mirror_sync(win->video_buffer,
                win->tint_threshold, win->tint_r);
        } else {
            tex = tig_video_buffer_gpu_mirror_sync(win->video_buffer);
        }
        if (tex == NULL) {
            continue;
        }

        TigRect clip;
        TigRect* clip_ptr = NULL;
        if (win->has_clip) {
            if (tig_rect_intersection(&win->frame, &win->clip_rect, &clip) != TIG_OK) {
                continue; // fully clipped away
            }
            clip_ptr = &clip;
        }

        tig_video_composite_ui_texture(tex, NULL, &dst_rect, alpha, clip_ptr);
    }

    // CE (full GPU/UI): the mouse cursor is normally blitted onto the CPU
    // framebuffer (tig_mouse_display) which gpu-ui doesn't draw — composite it
    // here on top of the window stack instead.
    tig_mouse_gpu_composite();
}

// 0x51D570
int tig_window_fill(tig_window_handle_t window_handle, TigRect* rect, tig_color_t color)
{
    int window_index;
    TigWindow* win;
    TigRect normalized;
    TigRect clamped_normalized_rect;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_fill: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (rect != NULL) {
        normalized = *rect;
    } else {
        normalized.x = 0;
        normalized.y = 0;
        normalized.width = win->frame.width;
        normalized.height = win->frame.height;
    }

    if (tig_rect_intersection(&normalized, &(win->bounds), &clamped_normalized_rect) != TIG_OK) {
        return TIG_OK;
    }

    rc = tig_video_buffer_fill(win->video_buffer, &clamped_normalized_rect, color);
    if (rc != TIG_OK) {
        return rc;
    }

    clamped_normalized_rect.x += win->frame.x;
    clamped_normalized_rect.y += win->frame.y;
    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&clamped_normalized_rect);
        tig_button_refresh_rect(window_handle, &clamped_normalized_rect);
    }

    return TIG_OK;
}

// 0x51D6B0
int tig_window_line(tig_window_handle_t window_handle, TigLine* line, tig_color_t color)
{
    int window_index;
    TigWindow* win;
    TigRect dirty_rect;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_line: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (tig_line_intersection(&(win->bounds), line) != TIG_OK) {
        return TIG_OK;
    }

    if (tig_line_bounding_box(line, &dirty_rect) != TIG_OK) {
        return TIG_ERR_NO_INTERSECTION;
    }

    rc = tig_video_buffer_line(win->video_buffer, line, &dirty_rect, color);

    dirty_rect.x += win->frame.x;
    dirty_rect.y += win->frame.y;

    if (rc == 0) {
        if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
            tig_window_invalidate_rect(&dirty_rect);
            tig_button_refresh_rect(window_handle, &dirty_rect);
        }
    }

    return rc;
}

// 0x51D7B0
int tig_window_box(tig_window_handle_t window_handle, TigRect* rect, tig_color_t color)
{
    int rc;
    TigRect side_rect;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_box: ERROR: Attempt to reference Empty WinID!\n");
        return 12;
    }

    side_rect.x = rect->x;
    side_rect.y = rect->y;
    side_rect.width = rect->width;
    side_rect.height = 1;
    rc = tig_window_fill(window_handle, &side_rect, color);
    if (rc != TIG_OK) {
        return rc;
    }

    side_rect.x = rect->x;
    side_rect.y = rect->y;
    side_rect.width = 1;
    side_rect.height = rect->height;
    rc = tig_window_fill(window_handle, &side_rect, color);
    if (rc != TIG_OK) {
        return rc;
    }

    side_rect.x = rect->x + rect->width - 1;
    side_rect.y = rect->y;
    side_rect.width = 1;
    side_rect.height = rect->height;
    rc = tig_window_fill(window_handle, &side_rect, color);
    if (rc != TIG_OK) {
        return rc;
    }

    side_rect.x = rect->x;
    side_rect.y = rect->y + rect->height - 1;
    side_rect.width = rect->width;
    side_rect.height = 1;
    rc = tig_window_fill(window_handle, &side_rect, color);
    if (rc != TIG_OK) {
        return rc;
    }

    return TIG_OK;
}

// 0x51D8D0
int tig_window_blit(TigWindowBlitInfo* win_blit_info)
{
    TigVideoBufferBlitInfo vb_blit_info;
    TigRect dirty_rect;
    unsigned int src_window_index;
    unsigned int dst_window_index;

    switch (win_blit_info->type) {
    case TIG_WINDOW_BLIT_WINDOW_TO_WINDOW:
        src_window_index = tig_window_handle_to_index(win_blit_info->src_window_handle);
        vb_blit_info.src_video_buffer = windows[src_window_index].video_buffer;

        dst_window_index = tig_window_handle_to_index(win_blit_info->dst_window_handle);
        vb_blit_info.dst_video_buffer = windows[dst_window_index].video_buffer;

        dirty_rect = *win_blit_info->dst_rect;
        dirty_rect.x += windows[dst_window_index].frame.x;
        dirty_rect.y += windows[dst_window_index].frame.y;

        if ((windows[dst_window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
            tig_window_invalidate_rect(&dirty_rect);
        }
        break;
    case TIG_WINDOW_BLIT_VIDEO_BUFFER_TO_WINDOW:
        vb_blit_info.src_video_buffer = win_blit_info->src_video_buffer;

        dst_window_index = tig_window_handle_to_index(win_blit_info->dst_window_handle);
        vb_blit_info.dst_video_buffer = windows[dst_window_index].video_buffer;

        dirty_rect = *win_blit_info->dst_rect;
        dirty_rect.x += windows[dst_window_index].frame.x;
        dirty_rect.y += windows[dst_window_index].frame.y;

        if ((windows[dst_window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
            tig_window_invalidate_rect(&dirty_rect);
        }
        break;
    case TIG_WINDOW_BLT_WINDOW_TO_VIDEO_BUFFER:
        src_window_index = tig_window_handle_to_index(win_blit_info->src_window_handle);
        vb_blit_info.src_video_buffer = windows[src_window_index].video_buffer;
        vb_blit_info.dst_video_buffer = win_blit_info->dst_video_buffer;
        break;
    default:
        return TIG_ERR_INVALID_PARAM;
    }

    vb_blit_info.flags = win_blit_info->vb_blit_flags;
    vb_blit_info.src_rect = win_blit_info->src_rect;
    vb_blit_info.alpha[0] = win_blit_info->alpha[0];
    vb_blit_info.alpha[1] = win_blit_info->alpha[1];
    vb_blit_info.alpha[2] = win_blit_info->alpha[2];
    vb_blit_info.alpha[3] = win_blit_info->alpha[3];

    // TODO: Looks odd, investigate.
    // vb_blit_info.field_10 = win_blit_info->field_18;
    // vb_blit_info.field_14 = win_blit_info->field_1C;
    // vb_blit_info.field_18 = win_blit_info->field_20;
    // vb_blit_info.field_1C = win_blit_info->field_24;

    vb_blit_info.dst_rect = win_blit_info->dst_rect;
    return tig_video_buffer_blit(&vb_blit_info);
}

// 0x51DA80
int tig_window_blit_art(tig_window_handle_t window_handle, TigArtBlitInfo* blit_info)
{
    TigWindow* win;
    int window_index;
    int rc;
    TigRect rect;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_blit_art: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    blit_info->dst_video_buffer = win->video_buffer;

    rc = tig_art_blit(blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    rect = *(blit_info->dst_rect);
    rect.x += win->frame.x;
    rect.y += win->frame.y;

    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&rect);
        tig_button_refresh_rect(window_handle, &rect);
    }

    return rc;
}

// 0x51DB40
int tig_window_scroll(tig_window_handle_t window_handle, int dx, int dy)
{
    int window_index;
    TigWindow* window;
    TigRect src_rect;
    TigRect dst_rect;
    TigVideoBufferBlitInfo vb_blit_info;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_scroll: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    window = &(windows[window_index]);

    src_rect = window->bounds;
    dst_rect = window->bounds;

    if (dx < 0) {
        dst_rect.width += dx;
        src_rect.x -= dx;
        src_rect.width += dx;
    } else {
        dst_rect.x += dx;
        dst_rect.width -= dx;
        src_rect.width -= dx;
    }

    if (dy < 0) {
        dst_rect.height += dy;
        src_rect.y -= dy;
        src_rect.height += dy;
    } else {
        dst_rect.y += dy;
        dst_rect.height -= dy;
        src_rect.height -= dy;
    }

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = window->video_buffer;
    vb_blit_info.src_rect = &src_rect;
    vb_blit_info.dst_video_buffer = window->video_buffer;
    vb_blit_info.dst_rect = &dst_rect;

    rc = tig_video_buffer_blit(&vb_blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((window->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(window->frame));
    }

    return TIG_OK;
}

// 0x51DC90
int tig_window_scroll_rect(tig_window_handle_t window_handle, TigRect* rect, int dx, int dy)
{
    int window_index;
    TigWindow* window;
    TigRect src_rect;
    TigRect dst_rect;
    TigVideoBufferBlitInfo vb_blit_info;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_scroll_rect: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    window = &(windows[window_index]);

    src_rect = *rect;
    dst_rect = *rect;

    if (dx < 0) {
        dst_rect.width += dx;
        src_rect.x -= dx;
        src_rect.width += dx;
    } else {
        dst_rect.x += dx;
        dst_rect.width -= dx;
        src_rect.width -= dx;
    }

    if (dy < 0) {
        dst_rect.height += dy;
        src_rect.y -= dy;
        src_rect.height += dy;
    } else {
        dst_rect.y += dy;
        dst_rect.height -= dy;
        src_rect.height -= dy;
    }

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = window->video_buffer;
    vb_blit_info.src_rect = &src_rect;
    vb_blit_info.dst_video_buffer = window->video_buffer;
    vb_blit_info.dst_rect = &dst_rect;

    rc = tig_video_buffer_blit(&vb_blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((window->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(window->frame));
    }

    return TIG_OK;
}

// 0x51DDC0
int tig_window_copy(tig_window_handle_t dst_window_handle, TigRect* dst_rect, tig_window_handle_t src_window_handle, TigRect* src_rect)
{
    int dst_window_index;
    int src_window_index;
    TigVideoBufferBlitInfo vb_blit_info;
    int rc;
    TigRect dirty_rect;

    if (dst_window_handle == TIG_WINDOW_HANDLE_INVALID || src_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_copy: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    dst_window_index = tig_window_handle_to_index(dst_window_handle);
    src_window_index = tig_window_handle_to_index(src_window_handle);

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = windows[src_window_index].video_buffer;
    vb_blit_info.src_rect = src_rect;
    vb_blit_info.dst_video_buffer = windows[dst_window_index].video_buffer;
    vb_blit_info.dst_rect = dst_rect;

    rc = tig_video_buffer_blit(&vb_blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((windows[dst_window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
        dirty_rect.x = dst_rect->x + windows[dst_window_index].frame.x;
        dirty_rect.y = dst_rect->y + windows[dst_window_index].frame.y;
        dirty_rect.width = dst_rect->width;
        dirty_rect.width = dst_rect->height;
        tig_window_invalidate_rect(&dirty_rect);
    }

    return TIG_OK;
}

// 0x51DEA0
int tig_window_copy_from_vbuffer(tig_window_handle_t dst_window_handle, TigRect* dst_rect, TigVideoBuffer* src_video_buffer, TigRect* src_rect)
{
    int dst_window_index;
    TigVideoBufferBlitInfo vb_blit_info;
    int rc;
    TigRect dirty_rect;

    if (dst_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_copy_from_vbuffer: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    dst_window_index = tig_window_handle_to_index(dst_window_handle);

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = src_video_buffer;
    vb_blit_info.src_rect = src_rect;
    vb_blit_info.dst_video_buffer = windows[dst_window_index].video_buffer;
    vb_blit_info.dst_rect = dst_rect;

    rc = tig_video_buffer_blit(&vb_blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((windows[dst_window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
        dirty_rect.x = dst_rect->x + windows[dst_window_index].frame.x;
        dirty_rect.y = dst_rect->y + windows[dst_window_index].frame.y;
        dirty_rect.width = dst_rect->width;
        dirty_rect.height = dst_rect->height;
        tig_window_invalidate_rect(&dirty_rect);
    }

    return TIG_OK;
}

int tig_window_copy_from_vbuffer_alpha(tig_window_handle_t dst_window_handle, TigRect* dst_rect, TigVideoBuffer* src_video_buffer, TigRect* src_rect, uint8_t alpha)
{
    int dst_window_index;
    TigVideoBufferBlitInfo vb_blit_info;
    int rc;
    TigRect dirty_rect;

    if (dst_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_copy_from_vbuffer_alpha: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    // alpha=0 is a no-op render; skip the blit but still return OK
    // so callers don't treat it as a failure.
    if (alpha == 0) {
        return TIG_OK;
    }

    dst_window_index = tig_window_handle_to_index(dst_window_handle);

    vb_blit_info.flags = (alpha < 255)
        ? TIG_VIDEO_BUFFER_BLIT_BLEND_ALPHA_CONST
        : 0;
    vb_blit_info.src_video_buffer = src_video_buffer;
    vb_blit_info.src_rect = src_rect;
    vb_blit_info.alpha[0] = alpha;
    vb_blit_info.alpha[1] = alpha;
    vb_blit_info.alpha[2] = alpha;
    vb_blit_info.alpha[3] = alpha;
    vb_blit_info.dst_video_buffer = windows[dst_window_index].video_buffer;
    vb_blit_info.dst_rect = dst_rect;

    rc = tig_video_buffer_blit(&vb_blit_info);
    if (rc != TIG_OK) {
        return rc;
    }

    if ((windows[dst_window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
        dirty_rect.x = dst_rect->x + windows[dst_window_index].frame.x;
        dirty_rect.y = dst_rect->y + windows[dst_window_index].frame.y;
        dirty_rect.width = dst_rect->width;
        dirty_rect.height = dst_rect->height;
        tig_window_invalidate_rect(&dirty_rect);
    }

    return TIG_OK;
}

// NOTE: The purpose of this alias is unclear. Not used.
//
// 0x51DF60
int sub_51DF60(tig_window_handle_t dst_window_handle, TigRect* dst_rect, TigVideoBuffer* src_video_buffer, TigRect* src_rect)
{
    return tig_window_copy_from_vbuffer(dst_window_handle, dst_rect, src_video_buffer, src_rect);
}

// 0x51DF80
int tig_window_copy_to_vbuffer(tig_window_handle_t src_window_handle, TigRect* src_rect, TigVideoBuffer* dst_video_buffer, TigRect* dst_rect)
{
    int src_window_index;
    TigVideoBufferBlitInfo vb_blit_info;

    if (src_window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_copy_to_vbuffer: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    src_window_index = tig_window_handle_to_index(src_window_handle);

    vb_blit_info.flags = 0;
    vb_blit_info.src_video_buffer = windows[src_window_index].video_buffer;
    vb_blit_info.src_rect = src_rect;
    vb_blit_info.dst_video_buffer = dst_video_buffer;
    vb_blit_info.dst_rect = dst_rect;

    return tig_video_buffer_blit(&vb_blit_info);
}

// 0x51E0A0
int tig_window_tint(tig_window_handle_t window_handle, TigRect* rect, int a3, int a4)
{
    int window_index;
    TigWindow* win;
    TigRect dirty_rect;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_tint: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (tig_rect_intersection(rect, &(win->bounds), &dirty_rect) != TIG_OK) {
        // No intersection, nothing to refresh.
        return TIG_OK;
    }

    rc = tig_video_buffer_tint(win->video_buffer, &dirty_rect, a3, a4);

    dirty_rect.x += win->frame.x;
    dirty_rect.y += win->frame.y;

    if (rc == TIG_OK) {
        if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
            tig_window_invalidate_rect(&dirty_rect);
            tig_button_refresh_rect(window_handle, &dirty_rect);
        }
    }

    return rc;
}

// 0x51E190
int tig_window_text_write(tig_window_handle_t window_handle, const char* str, TigRect* rect)
{
    int window_index;
    TigWindow* win;
    TigRect dirty_rect;
    int rc;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_text_write: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (rect->x < win->bounds.x
        || rect->y < win->bounds.y
        || rect->x + rect->width > win->bounds.x + win->bounds.width
        || rect->y + rect->height > win->bounds.y + win->bounds.height) {
        return TIG_ERR_INVALID_PARAM;
    }

    rc = tig_font_write(win->video_buffer, str, rect, &dirty_rect);

    dirty_rect.x += win->frame.x;
    dirty_rect.y += win->frame.y;

    if (rc == TIG_OK) {
        if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
            tig_window_invalidate_rect(&dirty_rect);
            tig_button_refresh_rect(window_handle, &dirty_rect);
        }
    }

    return rc;
}

// 0x51E2A0
int tig_window_free_index(void)
{
    int window_index;

    for (window_index = 0; window_index < TIG_WINDOW_MAX; window_index++) {
        if ((windows[window_index].usage & TIG_WINDOW_USAGE_FREE) != 0) {
            return window_index;
        }
    }

    return -1;
}

// 0x51E2C0
int tig_window_handle_to_index(tig_window_handle_t window_handle)
{
    return (int)window_handle;
}

// 0x51E2D0
tig_window_handle_t tig_window_index_to_handle(int window_index)
{
    return (tig_window_handle_t)window_index;
}

// 0x51E2E0
void push_window_stack(tig_window_handle_t window_handle)
{
    int window_index;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("push_window_stack: ERROR: Attempt to reference Empty WinID!\n");
        return;
    }

    window_index = tig_window_handle_to_index(window_handle);

    if ((windows[window_index].flags & (TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MODAL)) != 0) {
        tig_window_stack[tig_window_num_windows++] = window_handle;
    } else if ((windows[window_index].flags & (TIG_WINDOW_ALWAYS_ON_BOTTOM | TIG_WINDOW_MODAL)) != 0) {
        memmove(&(tig_window_stack[1]),
            &(tig_window_stack[0]),
            sizeof(tig_window_handle_t) * tig_window_num_windows);
        tig_window_stack[0] = window_handle;
        tig_window_num_windows++;
    } else {
        int prev_index;
        tig_window_handle_t prev_window_handle;
        int prev_window_index;

        for (prev_index = tig_window_num_windows - 1; prev_index >= 0; prev_index--) {
            prev_window_handle = tig_window_stack[prev_index];
            prev_window_index = tig_window_handle_to_index(prev_window_handle);

            if ((windows[prev_window_index].flags & (TIG_WINDOW_ALWAYS_ON_TOP | TIG_WINDOW_MODAL)) == 0) {
                break;
            }
        }

        // NOTE: Original code is slightly different, but does the same thing -
        // make a room a new window by moving windows in the stack.
        if (prev_index + 1 < tig_window_num_windows) {
            memmove(&(tig_window_stack[prev_index + 1]),
                &(tig_window_stack[prev_index]),
                sizeof(tig_window_handle_t) * (tig_window_num_windows - prev_index));
        }

        tig_window_stack[prev_index + 1] = window_handle;
        tig_window_num_windows++;
    }
}

// 0x51E3E0
bool pop_window_stack(tig_window_handle_t window_handle)
{
    int index;

    for (index = 0; index < tig_window_num_windows; index++) {
        if (tig_window_stack[index] == window_handle) {
            while (index + 1 < tig_window_num_windows) {
                tig_window_stack[index] = tig_window_stack[index + 1];
                index++;
            }

            tig_window_num_windows--;

            return true;
        }
    }

    return false;
}

// 0x51E430
void tig_window_invalidate_rect(TigRect* rect)
{
    TigRect dirty_rect;
    TigRectListNode* node;

    if (!tig_window_initialized) {
        return;
    }

    if (tig_window_invalidate_suppressed) {
        return;
    }

    if (rect != NULL) {
        dirty_rect = *rect;
        if (tig_rect_intersection(&dirty_rect, &tig_window_screen_rect, &dirty_rect) != TIG_OK) {
            return;
        }
    } else {
        dirty_rect = tig_window_screen_rect;
    }

    if (tig_window_dirty_rects != NULL) {
        node = tig_rect_node_create();
        node->rect = dirty_rect;
        node->next = tig_window_dirty_rects;
        tig_window_dirty_rects = node;
    } else {
        tig_window_dirty_rects = tig_rect_node_create();
        if (tig_window_dirty_rects != NULL) {
            tig_window_dirty_rects->rect = dirty_rect;
            tig_window_dirty_rects->next = NULL;
        }
    }
}

// 0x51E530
int tig_window_button_add(tig_window_handle_t window_handle, tig_button_handle_t button_handle)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_button_add: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (win->num_buttons == TIG_WINDOW_BUTTON_MAX) {
        return TIG_ERR_OUT_OF_HANDLES;
    }

    win->buttons[win->num_buttons++] = button_handle;

    return TIG_OK;
}

// 0x51E5A0
int tig_window_button_remove(tig_window_handle_t window_handle, tig_button_handle_t button_handle)
{
    int window_index;
    TigWindow* win;
    int button_index;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_button_remove: ERROR: Attempt to reference Empty WinID!\n");
        return TIG_ERR_INVALID_PARAM;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    for (button_index = 0; button_index < win->num_buttons; button_index++) {
        if (win->buttons[button_index] == button_handle) {
            while (button_index + 1 < win->num_buttons) {
                win->buttons[button_index] = win->buttons[button_index + 1];
                button_index++;
            }

            win->num_buttons--;

            return TIG_OK;
        }
    }

    return TIG_ERR_INVALID_PARAM;
}

// 0x51E640
int tig_window_button_list(tig_window_handle_t window_handle, tig_button_handle_t** buttons_ptr)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        tig_debug_printf("tig_window_button_list: ERROR: Attempt to reference Empty WinID!\n");
        *buttons_ptr = NULL;
        return 0;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    *buttons_ptr = win->buttons;

    return win->num_buttons;
}

// 0x51E690
int tig_window_get_at_position(int x, int y, tig_window_handle_t* window_handle_ptr)
{
    int index;
    tig_window_handle_t window_handle;
    int window_index;
    TigWindow* win;
    unsigned int color;

    for (index = tig_window_num_windows - 1; index >= 0; index--) {
        window_handle = tig_window_stack[index];
        window_index = tig_window_handle_to_index(window_handle);
        win = &(windows[window_index]);
        if ((win->flags & TIG_WINDOW_HIDDEN) == 0
            && x >= win->frame.x
            && y >= win->frame.y
            && x <= win->frame.x + win->frame.width
            && y <= win->frame.y + win->frame.height) {
            // CE: clip-rect symmetry — when a clip rect is set, hit
            // testing is bounded by (frame ∩ clip_rect), matching what
            // composites visibly. Clicks in the cropped-out area fall
            // through to underlying windows instead of being eaten by
            // the invisible part of this one.
            if (win->has_clip) {
                TigRect effective_frame;
                if (tig_rect_intersection(&(win->frame), &(win->clip_rect), &effective_frame) != TIG_OK) {
                    continue;
                }
                if (x < effective_frame.x
                    || y < effective_frame.y
                    || x > effective_frame.x + effective_frame.width
                    || y > effective_frame.y + effective_frame.height) {
                    continue;
                }
            }
            if ((win->flags & TIG_WINDOW_TRANSPARENT) != 0) {
                if (tig_video_buffer_get_pixel_color(win->video_buffer, x - win->frame.x, y - win->frame.y, &color) == TIG_OK
                    && color == win->color_key) {
                    continue;
                }
            }

            *window_handle_ptr = window_handle;

            return TIG_OK;
        }
    }

    return TIG_ERR_INVALID_PARAM;
}

// 0x51E790
bool tig_window_filter_message(TigMessage* msg)
{
    int index;
    tig_window_handle_t window_handle;
    int window_index;
    TigWindow* win;
    unsigned int flags[TIG_WINDOW_MAX];
    TigWindowMessageFilterFunc filters[TIG_WINDOW_MAX];
    int cnt;

    // CE: clip-rect aware dispatch was originally also gated here for
    // positional messages, but that broke filters that piggy-back on a
    // clipped window's filter to dispatch their own positional logic
    // outside the clip (e.g. dialog UI options rendered inside the iso
    // window are routed through the bar's filter via the bar's
    // message-filter chain). Button hit-testing in
    // tig_window_get_at_position already respects the clip, which is
    // enough to make in-clip buttons exclusive and out-of-clip pixels
    // fall through to the world picker.
    cnt = 0;
    for (index = tig_window_num_windows - 1; index >= 0; index--) {
        window_handle = tig_window_stack[index];
        window_index = tig_window_handle_to_index(window_handle);
        win = &(windows[window_index]);
        if ((win->flags & TIG_WINDOW_HIDDEN) == 0
            && (win->flags & TIG_WINDOW_MESSAGE_FILTER) != 0) {
            flags[cnt] = win->flags;
            filters[cnt] = win->message_filter;
            cnt++;
        }
    }

    for (index = 0; index < cnt; index++) {
        if ((filters[index](msg) && msg->type != TIG_MESSAGE_PING)
            || ((flags[index] & TIG_WINDOW_MODAL) != 0 && msg->type != TIG_MESSAGE_PING)) {
            return true;
        }
    }

    return false;
}

// 0x51E850
int sub_51E850(tig_window_handle_t window_handle)
{
    int window_index;

    if (!pop_window_stack(window_handle)) {
        return TIG_ERR_GENERIC;
    }

    push_window_stack(window_handle);

    window_index = tig_window_handle_to_index(window_handle);
    if ((windows[window_index].flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(windows[window_index].frame));
    }

    return TIG_OK;
}

// 0x51E8A0
int tig_window_move_on_top(tig_window_handle_t window_handle)
{
    int window_index;
    TigWindow* win;

    if (!pop_window_stack(window_handle)) {
        return TIG_ERR_GENERIC;
    }

    push_window_stack(window_handle);

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(win->frame));
    }

    return TIG_OK;
}

// 0x51E8F0
int tig_window_show(tig_window_handle_t window_handle)
{
    int window_index;
    TigWindow* win;
    int index;

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);
    win->flags &= ~TIG_WINDOW_HIDDEN;
    tig_window_invalidate_rect(&(win->frame));

    for (index = 0; index < win->num_buttons; index++) {
        tig_button_show_force(win->buttons[index]);
    }

    return TIG_OK;
}

// 0x51E950
int tig_window_hide(tig_window_handle_t window_handle)
{
    int window_index;
    TigWindow* win;
    int index;

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);
    win->flags |= TIG_WINDOW_HIDDEN;
    tig_window_invalidate_rect(&(win->frame));

    for (index = 0; index < win->num_buttons; index++) {
        tig_button_hide_force(win->buttons[index]);
    }

    return TIG_OK;
}

// 0x51E9B0
bool tig_window_is_hidden(tig_window_handle_t window_handle)
{
    int window_index = tig_window_handle_to_index(window_handle);
    return (windows[window_index].flags & TIG_WINDOW_HIDDEN) != 0;
}

// CE: Set/clear an optional composite clip rect on a window.
// Passing NULL clears any existing clip. Invalidates the union of
// the old and new visible regions so the screen recomposites cleanly
// (the now-uncovered area falls through to the window beneath).
int tig_window_clip_rect_set(tig_window_handle_t window_handle, const TigRect* clip_rect)
{
    int window_index;
    TigWindow* win;
    TigRect old_effective;
    TigRect new_effective;
    bool had_clip;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return TIG_ERR_INVALID_PARAM;
    }
    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    // Compute the old effective rect (what currently composites).
    had_clip = win->has_clip;
    if (had_clip) {
        if (tig_rect_intersection(&(win->frame), &(win->clip_rect), &old_effective) != TIG_OK) {
            old_effective.x = 0;
            old_effective.y = 0;
            old_effective.width = 0;
            old_effective.height = 0;
        }
    } else {
        old_effective = win->frame;
    }

    if (clip_rect == NULL) {
        win->has_clip = false;
        new_effective = win->frame;
    } else {
        win->has_clip = true;
        win->clip_rect = *clip_rect;
        if (tig_rect_intersection(&(win->frame), &(win->clip_rect), &new_effective) != TIG_OK) {
            new_effective.x = 0;
            new_effective.y = 0;
            new_effective.width = 0;
            new_effective.height = 0;
        }
    }

    // Invalidate union of old + new visible regions so both the
    // newly-revealed area (which needs the lower window to draw)
    // and the newly-hidden area (which needs our window to redraw
    // its now-cropped portion) get re-composited.
    if (old_effective.width > 0 && old_effective.height > 0) {
        tig_window_invalidate_rect(&old_effective);
    }
    if (new_effective.width > 0 && new_effective.height > 0
        && !(new_effective.x == old_effective.x
            && new_effective.y == old_effective.y
            && new_effective.width == old_effective.width
            && new_effective.height == old_effective.height)) {
        tig_window_invalidate_rect(&new_effective);
    }

    return TIG_OK;
}

// CE: opt the given window into the translucent-black tint pathway.
// When enabled, the compositor uses tig_video_blit_near_black_tinted
// to composite this window — near-black source pixels get replaced
// with subtract-tinted underlay pixels (showing the world darkened
// through the bar's dark panel regions), all other pixels copy
// through opaque.
//
// underlay_handle = the window whose VB supplies the live underlay
// pixels (typically the iso world). Passing INVALID falls back to
// opaque copy for near-black (the feature effectively disabled).
//
// `r/g/b` = the constant subtracted from each underlay pixel for
// the tint effect (use 30/30/30 to match the dialog options
// backdrop's default tint).
// CE: globally configure the modal-dialog auto-tint. When enabled
// is true and the underlay handle is valid, tig_window_modal_dialog
// auto-calls tig_window_tint_enable on each modal it creates so
// modal-dialog near-black panel regions show the tinted world
// through them. Gamelib enables this when an in-play iso session
// starts, disables when it ends, so pre-game modals (title-screen
// quit confirm etc.) stay fully opaque over the mainmenu_bg.
int tig_window_modal_tint_set(bool enabled,
    tig_window_handle_t underlay_handle,
    uint8_t threshold,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    tig_window_modal_tint_enabled = enabled;
    tig_window_modal_tint_underlay = underlay_handle;
    tig_window_modal_tint_threshold = threshold;
    tig_window_modal_tint_r = r;
    tig_window_modal_tint_g = g;
    tig_window_modal_tint_b = b;
    return TIG_OK;
}

int tig_window_tint_enable(tig_window_handle_t window_handle,
    bool enabled,
    tig_window_handle_t underlay_handle,
    uint8_t threshold,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return TIG_ERR_INVALID_PARAM;
    }
    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }
    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    bool was_enabled = win->tint_enabled;
    win->tint_enabled = enabled;
    win->tint_threshold = threshold;
    win->tint_r = r;
    win->tint_g = g;
    win->tint_b = b;
    win->tint_underlay = underlay_handle;

    if (was_enabled != enabled) {
        // Force a recomposite so the new mode applies immediately.
        tig_window_invalidate_rect(&(win->frame));
    }
    return TIG_OK;
}

// CE: opt a window into the world-knockout composite. While enabled,
// pixels whose RGB equals `key` are replaced by the raw underlay (world)
// pixel — a true cut-out for custom window shapes. Pass enabled=false to
// turn it off. Independent of tig_window_tint_enable (a window uses one
// path or the other; the compositor checks knockout first).
int tig_window_knockout_enable(tig_window_handle_t window_handle,
    bool enabled,
    tig_window_handle_t underlay_handle,
    tig_color_t key)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return TIG_ERR_INVALID_PARAM;
    }
    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }
    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    bool was_enabled = win->knockout_enabled;
    win->knockout_enabled = enabled;
    win->knockout_key = key;
    win->knockout_underlay = underlay_handle;

    if (was_enabled != enabled) {
        tig_window_invalidate_rect(&(win->frame));
    }
    return TIG_OK;
}

// CE (feature/perf-gpu-accel step 6): mark/unmark a window as the GPU-world
// window. While set, the compositor paints its region transparent (the GPU world
// is composited under the framebuffer at flip). Invalidates the full frame on a
// state change so the whole region re-composites (transparent vs opaque world).
int tig_window_set_gpu_world(tig_window_handle_t window_handle, bool enabled)
{
    int window_index;
    TigWindow* win;

    if (window_handle == TIG_WINDOW_HANDLE_INVALID) {
        return TIG_ERR_INVALID_PARAM;
    }
    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }
    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    if (win->gpu_world != enabled) {
        win->gpu_world = enabled;
        tig_window_invalidate_rect(&(win->frame));
    }
    return TIG_OK;
}

int tig_window_transform_set(tig_window_handle_t window_handle,
    float scale_x,
    float scale_y,
    float alpha,
    float anchor_rel_x,
    float anchor_rel_y)
{
    if (window_handle == TIG_WINDOW_HANDLE_INVALID) return TIG_ERR_INVALID_PARAM;
    if (!tig_window_initialized) return TIG_ERR_NOT_INITIALIZED;

    int window_index = tig_window_handle_to_index(window_handle);
    TigWindow* win = &(windows[window_index]);

    if (scale_x < 0.0f) scale_x = 0.0f;
    if (scale_y < 0.0f) scale_y = 0.0f;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    if (anchor_rel_x < 0.0f) anchor_rel_x = 0.0f;
    if (anchor_rel_x > 1.0f) anchor_rel_x = 1.0f;
    if (anchor_rel_y < 0.0f) anchor_rel_y = 0.0f;
    if (anchor_rel_y > 1.0f) anchor_rel_y = 1.0f;

    win->transform_active = true;
    win->transform_scale_x = scale_x;
    win->transform_scale_y = scale_y;
    win->transform_alpha = alpha;
    win->transform_anchor_x = anchor_rel_x;
    win->transform_anchor_y = anchor_rel_y;

    // Invalidate the window's full frame so the compositor repaints
    // both the old (pre-transform) and new (scaled) screen areas next
    // pass. Using frame instead of any computed dst rect because the
    // dst can be smaller than the frame (scale < 1), and we need the
    // surrounding area to repaint to "uncover" what was there before.
    tig_window_invalidate_rect(&(win->frame));
    return TIG_OK;
}

int tig_window_transform_clear(tig_window_handle_t window_handle)
{
    if (window_handle == TIG_WINDOW_HANDLE_INVALID) return TIG_ERR_INVALID_PARAM;
    if (!tig_window_initialized) return TIG_ERR_NOT_INITIALIZED;

    int window_index = tig_window_handle_to_index(window_handle);
    TigWindow* win = &(windows[window_index]);

    if (!win->transform_active) return TIG_OK;
    win->transform_active = false;
    win->transform_scale_x = 1.0f;
    win->transform_scale_y = 1.0f;
    win->transform_alpha = 1.0f;
    // Invalidate so the compositor repaints at the natural 1:1 frame.
    tig_window_invalidate_rect(&(win->frame));
    return TIG_OK;
}

int tig_window_tint_reveal_set(tig_window_handle_t window_handle, float reveal)
{
    if (window_handle == TIG_WINDOW_HANDLE_INVALID) return TIG_ERR_INVALID_PARAM;
    if (!tig_window_initialized) return TIG_ERR_NOT_INITIALIZED;

    int window_index = tig_window_handle_to_index(window_handle);
    TigWindow* win = &(windows[window_index]);
    if (reveal < 0.0f) reveal = 0.0f;
    if (reveal > 1.0f) reveal = 1.0f;
    float prev = win->tint_reveal;
    win->tint_reveal = reveal;
    if (prev != reveal && win->tint_enabled) {
        tig_window_invalidate_rect(&(win->frame));
    }
    return TIG_OK;
}

void tig_window_destroy_notify_set(void (*func)(tig_window_handle_t))
{
    tig_window_destroy_notify_func = func;
}

// 0x51EA10
int tig_window_vbid_get(tig_window_handle_t window_handle, TigVideoBuffer** video_buffer_ptr)
{
    int window_index;
    TigWindow* win;

    if (video_buffer_ptr == NULL) {
        return TIG_ERR_GENERIC;
    }

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    *video_buffer_ptr = win->video_buffer;

    return TIG_OK;
}

int tig_window_set_video_buffer(tig_window_handle_t window_handle, TigVideoBuffer* vb)
{
    int window_index;
    TigWindow* win;

    if (!tig_window_initialized) {
        return TIG_ERR_NOT_INITIALIZED;
    }

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);
    win->video_buffer = vb;

    return TIG_OK;
}

void tig_window_set_invalidate_suppressed(bool suppressed)
{
    tig_window_invalidate_suppressed = suppressed;
}

// 0x51EA60
int tig_window_modal_dialog(TigWindowModalDialogInfo* modal_info, TigWindowModalDialogChoice* choice_ptr)
{
    TigMessage msg;
    TigWindowData window_data;
    tig_button_handle_t hovered;

    if (modal_info == NULL) {
        return TIG_ERR_GENERIC;
    }

    if (!tig_window_modal_dialog_init()) {
        return TIG_ERR_GENERIC;
    }

    tig_window_modal_dialog_info = *modal_info;

    // Clear any active button hover BEFORE the modal opens so the
    // underlying window (e.g. the main menu showing a highlighted
    // "QUIT" item) un-highlights itself on the same frame the modal
    // appears. Once the modal window is created its filter swallows
    // every non-REDRAW message, including the TIG_MESSAGE_BUTTON
    // state-change notifications that would normally drive the menu's
    // morph-text refresh — so doing it here, before the swallow chain
    // is in place, is the simplest path to a consistent rollover state.
    hovered = tig_button_get_hovered();
    if (hovered != TIG_BUTTON_HANDLE_INVALID) {
        TigMessage notify;
        notify.type = TIG_MESSAGE_BUTTON;
        tig_timer_now(&(notify.timestamp));
        notify.data.button.button_handle = hovered;
        notify.data.button.state = TIG_BUTTON_STATE_MOUSE_OUTSIDE;
        notify.data.button.x = 0;
        notify.data.button.y = 0;
        // Dispatch synchronously via the window filter chain. With no
        // modal up yet, the menu's filter handles the BUTTON event,
        // calls its refresh_button_text(idx, 0), and the morph-text
        // re-renders without the highlight flag into the menu window's
        // video buffer. Then sync the button system's own internal
        // state so any art-based button using state-driven art also
        // drops back to its idle frame.
        tig_window_filter_message(&notify);
        tig_button_state_change(hovered, TIG_BUTTON_STATE_MOUSE_OUTSIDE);
        // Force a display pass to push the just-cleared vbuffer state
        // to the screen BEFORE the modal window is created. Without
        // this, the modal goes up over a screen that still shows the
        // stale highlight — the menu's video buffer is correct but
        // the compositor hasn't pushed the dirty rect yet.
        tig_window_display();
    }

    window_data.flags = TIG_WINDOW_MODAL | TIG_WINDOW_ALWAYS_ON_TOP;
    window_data.rect.width = MODAL_DIALOG_WIDTH;
    window_data.rect.height = MODAL_DIALOG_HEIGHT;
    window_data.message_filter = tig_window_modal_dialog_message_filter;
    window_data.background_color = tig_color_make(0, 0, 0);
    window_data.rect.x = modal_info->x;
    window_data.rect.y = modal_info->y;

    if (tig_window_create(&window_data, &tig_window_modal_dialog_window_handle) != TIG_OK) {
        return TIG_ERR_GENERIC;
    }

    // CE: if gamelib has set us up for translucent-black modals
    // (i.e. an in-play game session exists with an iso underlay),
    // opt this modal window into the tint pathway so its near-black
    // panel regions show the tinted world through them.
    if (tig_window_modal_tint_enabled
        && tig_window_modal_tint_underlay != TIG_WINDOW_HANDLE_INVALID) {
        tig_window_tint_enable(tig_window_modal_dialog_window_handle,
            true,
            tig_window_modal_tint_underlay,
            tig_window_modal_tint_threshold,
            tig_window_modal_tint_r,
            tig_window_modal_tint_g,
            tig_window_modal_tint_b);
    }

    tig_window_modal_dialog_create_buttons(modal_info->type, tig_window_modal_dialog_window_handle);
    tig_window_modal_dialog_refresh(NULL);

    // CE: drive a time-based scale+alpha entrance/exit on the modal. The
    // modal owns its own blocking pump (no game ui_anim tick here), so we
    // compute the transform inline each frame. Prime the first frame to
    // the scaled-down / transparent start state before the loop displays.
    tig_window_modal_dialog_close_requested = false;
    {
        TigRect* modal_frame =
            &windows[tig_window_handle_to_index(tig_window_modal_dialog_window_handle)].frame;
        tig_timestamp_t modal_anim_t0;
        tig_timestamp_t modal_exit_t0 = 0;
        bool modal_entering = true;
        bool modal_exiting = false;

        tig_window_transform_set(tig_window_modal_dialog_window_handle,
            MODAL_ANIM_SCALE_FROM, MODAL_ANIM_SCALE_FROM, 0.0f, 0.5f, 0.5f);
        tig_timer_now(&modal_anim_t0);

        while (tig_window_modal_dialog_window_handle != TIG_WINDOW_HANDLE_INVALID) {
            tig_ping();

            if (tig_window_modal_dialog_window_handle == TIG_WINDOW_HANDLE_INVALID) {
                break;
            }

            while (tig_message_dequeue(&msg) == TIG_OK) {
                if (msg.type == TIG_MESSAGE_REDRAW) {
                    if (modal_info->redraw != NULL) {
                        modal_info->redraw();
                    }

                    tig_window_invalidate_rect(NULL);
                }
            }

            // Don't take new input once we've started dismissing.
            if (!modal_exiting
                && modal_info->process != NULL
                && modal_info->process(&tig_message_modal_dialog_choice)) {
                tig_window_modal_dialog_close_requested = true;
            }

            // A close request (button / keyboard via the filter, or the
            // process callback) begins the exit animation instead of
            // destroying the window immediately.
            if (tig_window_modal_dialog_close_requested && !modal_exiting) {
                modal_exiting = true;
                modal_entering = false;
                tig_timer_now(&modal_exit_t0);
            }

            tig_timestamp_t modal_now;
            tig_timer_now(&modal_now);
            if (modal_exiting) {
                unsigned int el = (unsigned int)tig_timer_between(modal_exit_t0, modal_now);
                float t = el >= MODAL_ANIM_EXIT_MS
                    ? 1.0f : (float)el / (float)MODAL_ANIM_EXIT_MS;
                float e = t * t * (3.0f - 2.0f * t);  // smoothstep
                float scale = 1.0f + (MODAL_ANIM_SCALE_FROM - 1.0f) * e;
                tig_window_transform_set(tig_window_modal_dialog_window_handle,
                    scale, scale, 1.0f - e, 0.5f, 0.5f);
                tig_window_invalidate_rect(modal_frame);
                if (t >= 1.0f) {
                    // Dismissal complete — destroy for real and end loop.
                    tig_window_destroy(tig_window_modal_dialog_window_handle);
                    tig_window_modal_dialog_window_handle = TIG_WINDOW_HANDLE_INVALID;
                    tig_window_display();
                    break;
                }
            } else if (modal_entering) {
                unsigned int el = (unsigned int)tig_timer_between(modal_anim_t0, modal_now);
                float t = el >= MODAL_ANIM_ENTER_MS
                    ? 1.0f : (float)el / (float)MODAL_ANIM_ENTER_MS;
                float e = t * t * (3.0f - 2.0f * t);  // smoothstep
                if (t >= 1.0f) {
                    tig_window_transform_clear(tig_window_modal_dialog_window_handle);
                    modal_entering = false;
                } else {
                    float scale = MODAL_ANIM_SCALE_FROM + (1.0f - MODAL_ANIM_SCALE_FROM) * e;
                    tig_window_transform_set(tig_window_modal_dialog_window_handle,
                        scale, scale, e, 0.5f, 0.5f);
                }
                tig_window_invalidate_rect(modal_frame);
            }

            tig_window_display();
        }
    }

    if (choice_ptr != NULL) {
        *choice_ptr = tig_message_modal_dialog_choice;
    }

    tig_window_modal_dialog_exit();

    return TIG_OK;
}

// 0x51EBF0
bool tig_window_modal_dialog_message_filter(TigMessage* msg)
{
    switch (msg->type) {
    case TIG_MESSAGE_KEYBOARD:
        // Close the modal on keyup, not keydown. The matching keyup is
        // still queued behind a keydown-closed modal and was leaking out
        // to the underlying message loop (e.g. mainmenu_ui_handle's ESC
        // handler), so dismissing a confirm dialog with ESC was also
        // closing the menu that opened it. Handling on keyup means the
        // modal swallows both events before anything else sees them.
        if (!msg->data.keyboard.pressed) {
            switch (tig_window_modal_dialog_info.type) {
            case TIG_WINDOW_MODAL_DIALOG_TYPE_OK:
                tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_OK;
                tig_window_modal_dialog_close();
                break;
            case TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL:
                tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
                tig_window_modal_dialog_close();
                break;
            case TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL:
                if (msg->data.keyboard.scancode == SDL_SCANCODE_RETURN
                    || msg->data.keyboard.scancode == SDL_SCANCODE_KP_ENTER
                    || SDL_toupper(msg->data.keyboard.key) == SDL_toupper(tig_window_modal_dialog_info.keys[0])) {
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_OK;
                    tig_window_modal_dialog_close();
                } else if (msg->data.keyboard.scancode == SDL_SCANCODE_ESCAPE
                    || SDL_toupper(msg->data.keyboard.key) == SDL_toupper(tig_window_modal_dialog_info.keys[1])) {
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
                    tig_window_modal_dialog_close();
                }
                break;
            }
        }
        break;
    case TIG_MESSAGE_BUTTON:
        if (msg->data.button.state == TIG_BUTTON_STATE_RELEASED) {
            if (msg->data.button.button_handle == tig_window_modal_dialog_button_handles[0]) {
                switch (tig_window_modal_dialog_info.type) {
                case TIG_WINDOW_MODAL_DIALOG_TYPE_OK:
                case TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL:
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_OK;
                    tig_window_modal_dialog_close();
                    break;
                }
            } else if (msg->data.button.button_handle == tig_window_modal_dialog_button_handles[1]) {
                switch (tig_window_modal_dialog_info.type) {
                case TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL:
                case TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL:
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
                    tig_window_modal_dialog_close();
                    break;
                }
            }
        }
        break;
    case TIG_MESSAGE_MOUSE:
        // Click-outside-the-modal dismisses, matching the click-outside
        // overlay-dismiss behavior recently added to the main menu /
        // overlay screens.  The modal flag swallows all mouse events
        // anyway, so this handler is the only place that sees a click
        // landing outside the dialog rect.  Dispatch:
        //   - Inside the dialog rect: do nothing here; the button-press
        //     path above already handles OK / Cancel clicks via
        //     TIG_MESSAGE_BUTTON.
        //   - Outside:
        //       OK-only / OK_CANCEL → Cancel (safe non-destructive
        //       default — e.g. clicking outside "Delete save?" or "Quit
        //       game?" should NOT delete or quit).
        //       CANCEL-only        → Cancel (only choice anyway).
        if (msg->data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP) {
            TigWindowData wd;
            if (tig_window_modal_dialog_window_handle != TIG_WINDOW_HANDLE_INVALID
                && tig_window_data(tig_window_modal_dialog_window_handle, &wd) == TIG_OK
                && (msg->data.mouse.x < wd.rect.x
                    || msg->data.mouse.y < wd.rect.y
                    || msg->data.mouse.x >= wd.rect.x + wd.rect.width
                    || msg->data.mouse.y >= wd.rect.y + wd.rect.height)) {
                switch (tig_window_modal_dialog_info.type) {
                case TIG_WINDOW_MODAL_DIALOG_TYPE_OK:
                    // Info / acknowledgment modal — OK is the only choice
                    // and ANY keypress already dismisses with OK above.
                    // Match: a click outside dismisses with OK.
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_OK;
                    tig_window_modal_dialog_close();
                    break;
                case TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL:
                case TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL:
                    // Confirm prompts ("Quit?", "Delete this save?",
                    // etc.) — a stray click outside should NEVER commit
                    // the destructive choice. Cancel is the universal
                    // safe default.
                    tig_message_modal_dialog_choice = TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL;
                    tig_window_modal_dialog_close();
                    break;
                }
            }
        }
        break;
    default:
        break;
    }

    return true;
}

// 0x51ED10
void tig_window_modal_dialog_close(void)
{
    // CE: just request the close; the modal loop plays the exit
    // animation and destroys the window when it finishes. (Was: destroy
    // synchronously here.)
    tig_window_modal_dialog_close_requested = true;
}

// 0x51ED40
void tig_window_modal_dialog_refresh(TigRect* rect)
{
    TigRect text_rect;
    TigArtBlitInfo blit_info;

    text_rect.x = MODAL_DIALOG_TEXT_X;
    text_rect.y = MODAL_DIALOG_TEXT_Y;
    text_rect.width = MODAL_DIALOG_TEXT_WIDTH;
    text_rect.height = MODAL_DIALOG_TEXT_HEIGHT;

    if (rect == NULL) {
        rect = &tig_window_modal_dialog_bounds;
    }

    if (rect->x < tig_window_modal_dialog_bounds.x + tig_window_modal_dialog_bounds.width
        && rect->y < tig_window_modal_dialog_bounds.y + tig_window_modal_dialog_bounds.height
        && tig_window_modal_dialog_bounds.x < rect->x + rect->width
        && tig_window_modal_dialog_bounds.y < rect->y + rect->height) {

        blit_info.flags = 0;
        blit_info.src_rect = rect;
        blit_info.dst_rect = rect;
        tig_art_interface_id_create(MODAL_DIALOG_BACKGROUND_ART_NUM, 0, 0, 0, &(blit_info.art_id));
        if (tig_window_blit_art(tig_window_modal_dialog_window_handle, &blit_info) == TIG_OK) {
            if (tig_window_modal_dialog_info.text != NULL) {
                tig_font_push(tig_window_modal_dialog_font);
                tig_window_text_write(tig_window_modal_dialog_window_handle,
                    tig_window_modal_dialog_info.text,
                    &text_rect);
                tig_font_pop();
            }
        }
    }
}

// 0x51EE30
bool tig_window_modal_dialog_create_buttons(int type, tig_window_handle_t window_handle)
{
    TigButtonData ok_button_data;
    TigButtonData cancel_button_data;
    TigArtFrameData ok_art_frame_data;
    TigArtFrameData cancel_art_frame_data;

    ok_button_data.mouse_up_snd_id = -1;
    ok_button_data.mouse_down_snd_id = -1;
    ok_button_data.mouse_enter_snd_id = -1;
    ok_button_data.mouse_exit_snd_id = -1;
    ok_button_data.flags = TIG_BUTTON_MOMENTARY;
    ok_button_data.window_handle = window_handle;

    cancel_button_data.mouse_up_snd_id = -1;
    cancel_button_data.mouse_down_snd_id = -1;
    cancel_button_data.mouse_enter_snd_id = -1;
    cancel_button_data.mouse_exit_snd_id = -1;
    cancel_button_data.flags = TIG_BUTTON_MOMENTARY;
    cancel_button_data.window_handle = window_handle;

    switch (type) {
    case TIG_WINDOW_MODAL_DIALOG_TYPE_OK:
        tig_art_interface_id_create(MODAL_DIALOG_OK_BUTTON_ART_NUM, 0, 0, 0, &(ok_button_data.art_id));
        tig_art_frame_data(ok_button_data.art_id, &ok_art_frame_data);
        ok_button_data.x = MODAL_DIALOG_CENTER_BUTTON_X;
        ok_button_data.y = MODAL_DIALOG_CENTER_BUTTON_Y;
        ok_button_data.width = ok_art_frame_data.width;
        ok_button_data.height = ok_art_frame_data.height;
        tig_button_create(&ok_button_data,
            &(tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_OK]));
        break;
    case TIG_WINDOW_MODAL_DIALOG_TYPE_CANCEL:
        tig_art_interface_id_create(MODAL_DIALOG_CANCEL_BUTTON_ART_NUM, 0, 0, 0, &(cancel_button_data.art_id));
        tig_art_frame_data(cancel_button_data.art_id, &cancel_art_frame_data);
        cancel_button_data.x = MODAL_DIALOG_CENTER_BUTTON_X;
        cancel_button_data.y = MODAL_DIALOG_CENTER_BUTTON_Y;
        cancel_button_data.width = cancel_art_frame_data.width;
        cancel_button_data.height = cancel_art_frame_data.height;
        tig_button_create(&cancel_button_data,
            &(tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL]));
        break;
    case TIG_WINDOW_MODAL_DIALOG_TYPE_OK_CANCEL:
        // NOTE: Other cases do not check errors.
        if (tig_art_interface_id_create(MODAL_DIALOG_OK_BUTTON_ART_NUM, 0, 0, 0, &(ok_button_data.art_id)) == TIG_OK
            && tig_art_frame_data(ok_button_data.art_id, &ok_art_frame_data) == TIG_OK
            && tig_art_interface_id_create(MODAL_DIALOG_CANCEL_BUTTON_ART_NUM, 0, 0, 0, &(cancel_button_data.art_id)) == TIG_OK
            && tig_art_frame_data(cancel_button_data.art_id, &cancel_art_frame_data) == TIG_OK) {
            ok_button_data.x = MODAL_DIALOG_OK_BUTTON_X;
            ok_button_data.y = MODAL_DIALOG_OK_BUTTON_Y;
            ok_button_data.width = ok_art_frame_data.width;
            ok_button_data.height = ok_art_frame_data.height;
            if (tig_button_create(&ok_button_data, &(tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_OK])) != TIG_OK) {
                return false;
            }

            cancel_button_data.x = MODAL_DIALOG_CANCEL_BUTTON_X;
            cancel_button_data.y = MODAL_DIALOG_CANCEL_BUTTON_Y;
            cancel_button_data.width = cancel_art_frame_data.width;
            cancel_button_data.height = cancel_art_frame_data.height;
            if (tig_button_create(&cancel_button_data, &(tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL])) != TIG_OK) {
                return false;
            }
        }
        break;
    }

    return true;
}

// 0x51F050
bool tig_window_modal_dialog_init(void)
{
    TigFont font_data;

    font_data.flags = 0;
    tig_art_interface_id_create(MODAL_DIALOG_FONT_ART_NUM, 0, 0, 0, &(font_data.art_id));
    font_data.str = 0;
    font_data.color = tig_color_make(255, 255, 255);
    tig_font_create(&font_data, &tig_window_modal_dialog_font);

    tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_OK] = TIG_BUTTON_HANDLE_INVALID;
    tig_window_modal_dialog_button_handles[TIG_WINDOW_MODAL_DIALOG_CHOICE_CANCEL] = TIG_BUTTON_HANDLE_INVALID;

    return true;
}

// 0x51F0F0
void tig_window_modal_dialog_exit(void)
{
    int index;

    if (tig_window_modal_dialog_font != TIG_FONT_HANDLE_INVALID) {
        tig_font_destroy(tig_window_modal_dialog_font);
        tig_window_modal_dialog_font = TIG_FONT_HANDLE_INVALID;
    }

    for (index = 0; index < TIG_WINDOW_MODAL_DIALOG_CHOICE_COUNT; index++) {
        if (tig_window_modal_dialog_button_handles[index] != TIG_BUTTON_HANDLE_INVALID) {
            tig_button_destroy(tig_window_modal_dialog_button_handles[index]);
        }
    }
}

int tig_window_move(tig_window_handle_t window_handle, int x, int y)
{
    int window_index;
    TigWindow* win;
    int dx;
    int dy;
    int i;

    window_index = tig_window_handle_to_index(window_handle);
    win = &(windows[window_index]);

    // Invalidate old frame area so the previous position is recomposited
    // from underlying windows.
    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(win->frame));
    }

    dx = x - win->frame.x;
    dy = y - win->frame.y;

    win->frame.x = x;
    win->frame.y = y;

    // Child buttons cache absolute screen rects (set at creation as
    // window.frame + button-local offset). Translate them by the same delta
    // so subsequent button refreshes blit art at the correct window-local
    // offset; otherwise the stale rect minus the moved frame yields a
    // wrong destination and the button art is baked into the strip's
    // pixel buffer at the wrong place (ghost buttons).
    for (i = 0; i < win->num_buttons; i++) {
        tig_button_translate(win->buttons[i], dx, dy);
    }

    if ((win->flags & TIG_WINDOW_HIDDEN) == 0) {
        tig_window_invalidate_rect(&(win->frame));
    }

    return TIG_OK;
}
