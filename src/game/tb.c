#include "game/tb.h"

#include <string.h>

#include "game/dialog_camera.h"
#include "game/gamelib.h"
#include "game/tc.h"
#include "ui/follower_ui.h"
#include "ui/intgame.h"
#include "ui/ui_anim.h"
#include "game/iso_zoom.h"
#include "game/object.h"

/**
 * The maximum number of text bubbles that can be active simultaneously.
 */
#define MAX_TEXT_BUBBLES 8

/**
 * The maximum width of the text bubble rectangle.
 */
#define TEXT_BUBBLE_WIDTH 200

/**
 * The maximum height of the text bubble rectangle.
 */
#define TEXT_BUBBLE_HEIGHT 200

/**
 * Maximum text length stored per bubble for re-rendering with correct
 * alignment when placement is determined. Covers typical NPC speech;
 * longer strings are silently truncated (the video buffer clips them anyway).
 */
#define TB_STR_MAXLEN 512

/**
 * Fixed screen-pixel gap between sprite top and bubble bottom,
 * zoom-independent. Clears the bubble tail with a little breathing room.
 */
#define TB_BUBBLE_GAP_PX 10

/**
 * Minimum distance between the bubble and the viewport edge / UI bars.
 * Prevents the bubble from being flush against the screen boundary.
 */
#define TB_EDGE_MARGIN_PX 10

/**
 * Maximum pixels the bubble may drift from its ideal position before it is
 * hidden. Prevents it from pinning to the edge margin as the camera pans
 * away from the NPC.
 */
#define TB_DRIFT_MAX_PX 160

/**
 * Vertical gap (screen pixels) inserted between stacked speech bubbles when
 * the overlap resolver pushes them apart.
 */
#define TB_BUBBLE_STACK_GAP_PX 4

/**
 * Margin added to the half-sprite-width threshold before centered bubbles
 * switch to left/right aligned text.
 */
#define TB_ALIGN_SWITCH_MARGIN_PX 12

/**
 * Width of the follower portrait panel (leftmost column, anchored at x=0).
 * Its active height varies with party size; tb_calc_rect queries
 * follower_ui_panel_bottom() at runtime.
 */
#define TB_FOLLOWER_PANEL_WIDTH 67


typedef unsigned int TextBubbleFlags;
typedef unsigned int TextBubblePlacementFlags;

/**
 * Flag indicating that a text bubble is currently active.
 */
#define TEXT_BUBBLE_IN_USE 0x0001u

/**
 * Flag indicating that a text bubble is permanent and does not expire
 * automatically.
 */
#define TEXT_BUBBLE_PERMANENT 0x0002u

// Placement flags describe which viewport/UI safety clamped the preferred
// position. Side clamps can prefer lateral cascades; top/bottom clamps keep
// the existing Y-first stack behavior.
#define TB_PLACEMENT_CLAMP_LEFT   0x01u
#define TB_PLACEMENT_CLAMP_RIGHT  0x02u
#define TB_PLACEMENT_CLAMP_TOP    0x04u
#define TB_PLACEMENT_CLAMP_BOTTOM 0x08u


/**
 * Represents a text bubble associated with a game object.
 */
typedef struct TextBubble {
    /* 0000 */ TextBubbleFlags flags;
    /* 0008 */ int64_t obj;
    /* 0010 */ tig_timestamp_t timestamp;
    /* 0014 */ int duration; // Duration (in milliseconds) the bubble remains visible.
    /* 0018 */ TigVideoBuffer* video_buffer;
    /* 001C */ TigRect rect;
    int type;                // TB_TYPE_* — selects the correct font set on re-render
    char str[TB_STR_MAXLEN]; // original text, stored for alignment re-render
    int rendered_align;      // TB_ALIGN_* — last alignment rendered into video_buffer
} TextBubble;

// Text alignment used when rendering into the bubble's video buffer.
// Stored per-bubble so the re-render is skipped when alignment hasn't changed.
#define TB_ALIGN_INVALID  -1
#define TB_ALIGN_CENTERED  0
#define TB_ALIGN_LEFT      1
#define TB_ALIGN_RIGHT     2

static void tb_remove_internal(TextBubble* tb);
static void tb_get_rect_ex(TextBubble* tb, TigRect* rect, TextBubblePlacementFlags* placement_flags);
static void tb_get_rect(TextBubble* tb, TigRect* rect);
static void tb_calc_rect(TextBubble* tb, int64_t loc, int offset_x, int offset_y, TigRect* rect);
static void tb_calc_rect_ex(TextBubble* tb, int64_t loc, int offset_x, int offset_y, TigRect* rect, TextBubblePlacementFlags* placement_flags);
static int  tb_get_anchor_y(TextBubble* tb);
static int  tb_get_safe_bottom(int rect_x, int rect_width, int rect_height);
static bool tb_rects_overlap_horizontally(const TigRect* a, const TigRect* b, int gap);
static bool tb_rects_overlap_vertically(const TigRect* a, const TigRect* b);
static bool tb_pair_prefers_x_cascade(int first,
    int second,
    TigRect* rects,
    TigRect* base_rects,
    TextBubblePlacementFlags* placement_flags,
    bool allow_x_preferred_skip);
static bool tb_has_unresolved_overlap(TigRect* rects, int* indices, int count);
static void tb_collect_resolved_rects(TigRect* rects, int* anchor_ys, TextBubblePlacementFlags* placement_flags);
static void tb_invalidate_resolved_changes(void);
static void tb_resolve_overlaps(TigRect* rects, int* anchor_ys, TextBubblePlacementFlags* placement_flags);
static void tb_text_duration_changed(void);
static TextBubble* find_text_bubble(int64_t obj);
static TextBubble* find_free_text_bubble(int64_t obj);

/**
 * Color values (RGB) for text bubble types.
 *
 * 0x5B8EA0
 */
static uint8_t tb_colors[TB_TYPE_COUNT][3] = {
    /* TB_TYPE_WHITE */ { 255, 255, 255 },
    /*   TB_TYPE_RED */ { 255, 0, 0 },
    /* TB_TYPE_GREEN */ { 0, 255, 0 },
    /*  TB_TYPE_BLUE */ { 0, 0, 255 },
};

/**
 * The maximum text bubble bounds (in it's own coordinate system).
 *
 * 0x5B8EB0
 */
static TigRect tb_content_rect = { 0, 0, TEXT_BUBBLE_WIDTH, TEXT_BUBBLE_HEIGHT };

/**
 * Table defining preferred text bubble positions for nine screen cells. Each
 * row corresponds to a cell, with columns listing position preferences in
 * order.
 *
 * 0x5B8EC0
 */

/**
 * Parent window bounds.
 *
 * 0x602920
 */
static TigRect tb_iso_content_rect;

/**
 * Text bubbles.
 *
 * 0x602930
 */
static TextBubble tb_text_bubbles[MAX_TEXT_BUBBLES];

// CE: per-bubble alpha (0..255) animated by ui_anim_int_to when the
// bubble's NPC drifts past the on-screen / drift threshold. tb_draw
// blends the bubble's video buffer at this alpha so bubbles fade out
// instead of popping invisible when their NPC pans off-frame, and
// fade back in when the NPC returns. Slightly longer fade-out (~360
// ms) than fade-in (~200 ms) reads as "drifted away, then comes
// back" rather than a strobe. The tweens are retargetable in-place
// (ui_anim_int_to convention), so quick on/off transitions stay
// continuous instead of stuttering.
static int tb_alpha[MAX_TEXT_BUBBLES];
static int tb_alpha_target[MAX_TEXT_BUBBLES];
// CE: per-bubble ui_anim handle for the alpha fade tween. Tracked so
// we can ui_anim_cancel a stale in-flight tween before starting a
// new one — without this, ui_anim_int_to retargets the OLD slot
// while preserving its tracked value+velocity, which means a fresh
// tb_add's "start at 0" gets ignored if the previous bubble's fade
// hadn't fully settled. (The drift fades happen to work in spite of
// that because their alpha targets transition smoothly; the
// entrance/exit "start at 0 / end at 0" demand a clean slot.)
static ui_anim_handle_t tb_alpha_handle[MAX_TEXT_BUBBLES];
static const ui_anim_profile_t tb_fade_in_profile  = { 200, 1.2f };
static const ui_anim_profile_t tb_fade_out_profile = { 360, 1.2f };
// CE: tb_pending_remove[i] is true when the bubble at slot i has been
// scheduled for removal (timeout) and is currently fading out. Once
// its alpha reaches 0 the actual tb_remove_internal runs. Drift
// fade-outs do NOT set this flag — they want the bubble to stay in
// its slot so it can fade back in when the NPC returns. Cleared by
// tb_remove_internal so the next bubble that lands here starts
// clean.
static bool tb_pending_remove[MAX_TEXT_BUBBLES];

// CE: per-bubble previous-frame push state, used by the UI-obstacle
// decision logic to add hysteresis. Without this the decision is
// pure per-frame geometry — the moment an NPC's natural x crosses
// the UI's midline, the bubble flips from one side-push to the
// other (or to top), reading as a jump. With state-aware
// stickiness the bubble holds its current push longer, and only
// switches when the alternative is meaningfully better. Reset to
// TB_PUSH_NONE by tb_remove_internal.
typedef enum TbPushState {
    TB_PUSH_NONE = 0,
    TB_PUSH_TOP,
    TB_PUSH_LEFT,
    TB_PUSH_RIGHT,
} TbPushState;
// CE: per-bubble push state is tracked PER UI. Sticky state from
// one UI (e.g. TC pinned LEFT) must not leak into the other UI's
// decision (e.g. HUD seeing prev=LEFT and pinning LEFT too) —
// each UI runs its own hysteresis on its own history. A bubble
// transitioning from TC-LEFT to a fresh HUD overlap should make
// a clean HUD decision based on geometry, not inherit TC's pin.
static TbPushState tb_hud_push_state[MAX_TEXT_BUBBLES];
static TbPushState tb_tc_push_state[MAX_TEXT_BUBBLES];

// CE: drift fade-out commitment. When the speaker drifts out of range
// we start a fade-out at the bubble's current rect; tb_drift_locked
// holds that rect for the duration of the fade so the bubble dies in
// place instead of teleporting along with the speaker's continued
// motion. If the speaker comes back close (within TB_DRIFT_MAX_PX of
// the locked position) before alpha reaches 0, we reverse the fade.
// If they come back far (or alpha already hit 0), we let the fade
// finish and start a fresh entrance fade at the new natural rect —
// "respawn after far jump" — instead of snapping the bubble across
// the screen.
static int  tb_drift_fade_start_x[MAX_TEXT_BUBBLES];
static int  tb_drift_fade_start_y[MAX_TEXT_BUBBLES];
static bool tb_drift_locked[MAX_TEXT_BUBBLES];
// Entrance/exit fade timing — slower exit reads as "trailing off,"
// the entrance is snappier so the bubble doesn't feel laggy. Reused
// for the drift fades (same profile vars).
static const ui_anim_profile_t tb_entrance_profile = { 220, 1.2f };
static const ui_anim_profile_t tb_exit_profile     = { 380, 1.2f };

/**
 * Function pointer to invalidate a rectangle in the parent window.
 *
 * 0x602AB0
 */
static IsoInvalidateRectFunc* tb_iso_window_invalidate_rect;

/**
 * Flag indicating whether text bubbles rendering is enabled.
 *
 * 0x602AB4
 */
static bool tb_enabled;

/**
 * The default duration (in milliseconds) that text bubbles remain visible.
 *
 * 0x602AB8
 */
static int tb_text_duration;

/**
 * Handle to the parent window.
 *
 * 0x602ABC
 */
static tig_window_handle_t tb_iso_window_handle;

/**
 * 0x602AC0
 */
static int dword_602AC0;

/**
 * Background color for text bubbles.
 *
 * 0x602AC4
 */
static tig_color_t tb_background_color;

/**
 * Editor view options.
 *
 * 0x602AC8
 */
static ViewOptions tb_view_options;

/**
 * Last-known camera origin and zoom, used by tb_ping to detect when the
 * viewport has changed so active bubble positions are re-evaluated.
 */
static int64_t tb_last_origin_x;
static int64_t tb_last_origin_y;
static float tb_last_zoom = 1.0f;
// CE: camera origin delta since previous tb_ping. Used by
// tb_calc_rect to bias the UI-push decision toward the scroll
// axis: side-push during horizontal scroll, top-push during
// vertical scroll. Sign isn't used — only axis dominance.
static int64_t tb_scroll_dx;
static int64_t tb_scroll_dy;

/**
 * Resolved screen rects from the previous frame.
 *
 * tb_resolve_overlaps() may move bubbles to positions that differ from what
 * tb_calc_rect() computed (and invalidated via tb_iso_window_invalidate_rect).
 * At 1.0× zoom the dirty-rect system only repaints specific regions, so if
 * the resolve-adjusted position was not in the dirty list that frame the blit
 * clips.  Storing the resolved rects and pre-invalidating them in tb_ping
 * ensures the game world redraws those areas before tb_draw runs, so the full
 * resolved position is always covered by a dirty rect.
 */
static TigRect tb_prev_resolved[MAX_TEXT_BUBBLES];

/**
 * Fonts for text bubble types.
 * tb_fonts:       centered  — used for TB_POS_TOP / TB_POS_BOTTOM
 * tb_fonts_left:  left-aligned — used for right-side positions (text starts near sprite)
 * tb_fonts_right: right-aligned — used for left-side positions (text ends near sprite)
 *
 * 0x602AD0
 */
static tig_font_handle_t tb_fonts[TB_TYPE_COUNT];
static tig_font_handle_t tb_fonts_left[TB_TYPE_COUNT];
static tig_font_handle_t tb_fonts_right[TB_TYPE_COUNT];

/**
 * Called when the game is initialized.
 *
 * 0x4D5B80
 */
bool tb_init(GameInitInfo* init_info)
{
    TigWindowData window_data;
    TigVideoBufferCreateInfo vb_create_info;
    int idx;
    TigFont font;

    // Retrieve window data to set up content rectangle.
    if (tig_window_data(init_info->iso_window_handle, &window_data) != TIG_OK) {
        return false;
    }

    tb_iso_window_handle = init_info->iso_window_handle;
    tb_iso_window_invalidate_rect = init_info->invalidate_rect_func;

    tb_iso_content_rect.x = 0;
    tb_iso_content_rect.y = 0;
    tb_iso_content_rect.width = window_data.rect.width;
    tb_iso_content_rect.height = window_data.rect.height;

    tb_view_options.type = VIEW_TYPE_ISOMETRIC;

    tb_enabled = true;
    tb_background_color = tig_color_make(0, 0, 255);

    // Set up video buffer creation parameters.
    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY | TIG_VIDEO_BUFFER_CREATE_COLOR_KEY;
    vb_create_info.width = TEXT_BUBBLE_WIDTH;
    vb_create_info.height = TEXT_BUBBLE_HEIGHT;
    vb_create_info.background_color = tb_background_color;
    vb_create_info.color_key = tb_background_color;

    dword_602AC0 = tig_color_make(0, 0, 0);

    // Create video buffers for each text bubble.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if (tig_video_buffer_create(&vb_create_info, &(tb_text_bubbles[idx].video_buffer)) != TIG_OK) {
            // Clean up previously allocated buffers.
            while (--idx >= 0) {
                tig_video_buffer_destroy(tb_text_bubbles[idx].video_buffer);
            }

            // Something went wrong.
            return false;
        }
    }

    // Set up font creation parameters.
    tig_art_interface_id_create(229, 0, 0, 0, &(font.art_id));

    // Centered — used for TB_POS_TOP and TB_POS_BOTTOM.
    // CE: TIG_FONT_DIM_BRACKETS opts these into the emote-dim effect
    // (gated at runtime by tig_font_dim_brackets_set_enabled).
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_CENTERED | TIG_FONT_SHADOW | TIG_FONT_DIM_BRACKETS;
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        font.color = tig_color_make(tb_colors[idx][0], tb_colors[idx][1], tb_colors[idx][2]);
        tig_font_create(&font, &(tb_fonts[idx]));
    }

    // Left-aligned — used for right-side positions (text starts near sprite).
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_SHADOW | TIG_FONT_DIM_BRACKETS;
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        font.color = tig_color_make(tb_colors[idx][0], tb_colors[idx][1], tb_colors[idx][2]);
        tig_font_create(&font, &(tb_fonts_left[idx]));
    }

    // Right-aligned — used for left-side positions (text ends near sprite).
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_RIGHT | TIG_FONT_SHADOW | TIG_FONT_DIM_BRACKETS;
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        font.color = tig_color_make(tb_colors[idx][0], tb_colors[idx][1], tb_colors[idx][2]);
        tig_font_create(&font, &(tb_fonts_right[idx]));
    }

    // Register `text duration` setting and initialize it.
    settings_register(&settings, TEXT_DURATION_KEY, "6", tb_text_duration_changed);
    tb_text_duration_changed();

    return true;
}

/**
 * Called when the game is being reset.
 *
 * 0x4D5DB0
 */
void tb_reset(void)
{
    tb_clear();
}

/**
 * Called when the game shuts down.
 *
 * 0x4D5DC0
 */
void tb_exit(void)
{
    int idx;

    // Clear all active text bubbles.
    tb_clear();

    // Destroy fonts for each text bubble type.
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        tig_font_destroy(tb_fonts[idx]);
        tig_font_destroy(tb_fonts_left[idx]);
        tig_font_destroy(tb_fonts_right[idx]);
    }

    // Destroy video buffers for each text bubble.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        tig_video_buffer_destroy(tb_text_bubbles[idx].video_buffer);
    }

    tb_iso_window_handle = TIG_WINDOW_HANDLE_INVALID;
    tb_iso_window_invalidate_rect = NULL;
}

/**
 * Called when the window size has changed.
 *
 * 0x4D5E20
 */
void tb_resize(GameResizeInfo* resize_info)
{
    tb_iso_content_rect = resize_info->content_rect;
    tb_iso_window_handle = resize_info->window_handle;
}

/**
 * Called when view settings have changed.
 *
 * 0x4D5E60
 */
void tb_update_view(ViewOptions* view_options)
{
    tb_view_options = *view_options;
}

/**
 * Called when the map is closed.
 *
 * 0x4D5E80
 */
void tb_map_close(void)
{
    tb_clear();
}

/**
 * Toggles the visibility of text bubbles.
 *
 * 0x4D5E90
 */
void tb_toggle(void)
{
    tb_enabled = !tb_enabled;
}

/**
 * Called every frame.
 *
 * 0x4D5EB0
 */
void tb_ping(tig_timestamp_t timestamp)
{
    int idx;
    TigRect rect;
    int64_t ox;
    int64_t oy;
    float z;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        // Check if the bubble has expired.
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0
            && (tb_text_bubbles[idx].flags & TEXT_BUBBLE_PERMANENT) == 0
            && tig_timer_between(tb_text_bubbles[idx].timestamp, timestamp) > tb_text_bubbles[idx].duration) {
            // CE: don't tear down immediately. Mark the slot as
            // pending-remove and retarget its alpha tween to fade
            // out (~380ms). The actual tb_remove_internal happens
            // in the second loop below once the spring lands at 0.
            // Cancel any in-flight tween first so we get a clean
            // start from the bubble's current rendered alpha.
            if (!tb_pending_remove[idx]) {
                ui_anim_cancel(tb_alpha_handle[idx]);
                tb_pending_remove[idx] = true;
                tb_alpha_target[idx] = 0;
                tb_alpha_handle[idx] = ui_anim_int_to(&tb_alpha[idx], 0,
                    &tb_exit_profile);
            }
        }
    }

    // CE: second pass — any slot that's been pending-remove long
    // enough for its alpha tween to settle at 0 is now safe to
    // actually destroy. Doing this in its own loop keeps the
    // ordering predictable: timeout decisions first, then the
    // post-fade destruction reads a stable set of bubbles.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0
            && tb_pending_remove[idx]
            && tb_alpha[idx] == 0) {
            tb_get_rect(&(tb_text_bubbles[idx]), &rect);
            tb_iso_window_invalidate_rect(&rect);
            tb_remove_internal(&(tb_text_bubbles[idx]));
        }
    }

    // Update camera-origin / zoom snapshot. tb_scroll_dx/dy is captured here
    // even though the closest-edge pin chooser no longer uses it — kept for
    // any future scroll-axis-aware behavior.
    location_origin_get(&ox, &oy);
    z = iso_zoom_current();
    tb_scroll_dx = ox - tb_last_origin_x;
    tb_scroll_dy = oy - tb_last_origin_y;
    tb_last_origin_x = ox;
    tb_last_origin_y = oy;
    tb_last_zoom = z;

    // CE: invalidate the UNION of last frame's resolved rects and this
    // frame's projected rects, so the dirty-rect system covers both the
    // bubble's old position (now empty world space) and its new position
    // (where the blit will land). Previously this only pre-invalidated
    // last frame's rects, which meant the bubble's blit could miss
    // dirty-rect coverage whenever its rect changed between frames
    // (pin engagement on bump-in, alignment switch, overlap resolution
    // shifting it sideways) — the bubble would visibly disappear for
    // one frame and reappear once tb_prev_resolved caught up.
    tb_invalidate_resolved_changes();
}

/**
 * Renders all active text bubbles to the window.
 *
 * 0x4D5F10
 */
void tb_draw(GameDrawInfo* draw_info)
{
    int idx;
    TigRect rects[MAX_TEXT_BUBBLES];
    int anchor_ys[MAX_TEXT_BUBBLES];
    TextBubblePlacementFlags placement_flags[MAX_TEXT_BUBBLES];
    TigRectListNode* node;
    TigRect dst_rect;
    TigRect src_rect;

    // Ensure text bubble rendering is enabled.
    if (!tb_enabled) {
        return;
    }

    // Ensure we're in isometric view. The text bubble module is not supposed
    // to work in the editor.
    if (tb_view_options.type != VIEW_TYPE_ISOMETRIC) {
        return;
    }

    tb_collect_resolved_rects(rects, anchor_ys, placement_flags);

    // Store resolved rects so tb_ping can pre-invalidate them next frame,
    // guaranteeing dirty-rect coverage even at 1.0x zoom.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        tb_prev_resolved[idx] = rects[idx];
    }

    // Blit each bubble using the resolved screen rect.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) == 0) {
            continue;
        }

        TigRect* tb_rect = &rects[idx];

        // Skip bubbles hidden by drift or pushed fully off-screen.
        if (tb_rect->width == 0 || tb_rect->height == 0) {
            continue;
        }

        // CE: clamp the per-bubble alpha to a usable byte range. The
        // ui_anim spring can overshoot transiently (overdamped, so
        // small overshoot at most) and could land at 256+ momentarily;
        // bound it so tig_window_copy_from_vbuffer_alpha sees a
        // legal value.
        int a = tb_alpha[idx];
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        if (a == 0) {
            continue;  // fully faded out — skip blit entirely
        }

        // Iterate through dirty rects to check if text bubble needs to be
        // rendered at all.
        node = *draw_info->rects;
        while (node != NULL) {
            if (tig_rect_intersection(tb_rect, &(node->rect), &dst_rect) == TIG_OK) {
                // Map the on-screen destination back to video-buffer source coords.
                src_rect.x = dst_rect.x + tb_text_bubbles[idx].rect.x - tb_rect->x;
                src_rect.y = dst_rect.y + tb_text_bubbles[idx].rect.y - tb_rect->y;
                src_rect.width = dst_rect.width;
                src_rect.height = dst_rect.height;

                // Copy the affected portion of the text bubble's video
                // buffer onto the window, blended at the bubble's
                // current fade alpha.
                tig_window_copy_from_vbuffer_alpha(tb_iso_window_handle,
                    &dst_rect,
                    tb_text_bubbles[idx].video_buffer,
                    &src_rect,
                    (uint8_t)a);
            }
            node = node->next;
        }
    }
}

// CE (gpu-ui iso overlay port): composite each active bubble directly over the live
// GPU world at its fade alpha. Replaces tb_draw's blend-into-the-iso-surface (which
// is bypassed when the world renders on the GPU). Called by the iso overlay composite
// callback at the iso z-slot. Each bubble VB is blue-keyed, so its GPU mirror keys
// the background transparent and the bubble fades against the real GPU world — same
// visual result as tb_draw, just over the GPU world instead of the CPU surface.
void tb_gpu_composite(void)
{
    int idx;
    TigRect rects[MAX_TEXT_BUBBLES];
    int anchor_ys[MAX_TEXT_BUBBLES];
    TextBubblePlacementFlags placement_flags[MAX_TEXT_BUBBLES];

    if (!tb_enabled) {
        return;
    }
    if (tb_view_options.type != VIEW_TYPE_ISOMETRIC) {
        return;
    }
    // Called every gpu-ui walk frame; skip the rect-resolve when nothing's on screen.
    if (!tb_any_active()) {
        return;
    }

    tb_collect_resolved_rects(rects, anchor_ys, placement_flags);

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        tb_prev_resolved[idx] = rects[idx];

        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) == 0) {
            continue;
        }

        TigRect* tb_rect = &rects[idx];
        if (tb_rect->width == 0 || tb_rect->height == 0) {
            continue;
        }

        int a = tb_alpha[idx];
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        if (a == 0) {
            continue;
        }

        SDL_Texture* tex = tig_video_buffer_gpu_mirror_sync(tb_text_bubbles[idx].video_buffer);
        if (tex == NULL) {
            continue;
        }

        // src: the bubble's content within its VB; dst: the resolved screen rect.
        TigRect src;
        src.x = tb_text_bubbles[idx].rect.x;
        src.y = tb_text_bubbles[idx].rect.y;
        src.width = tb_rect->width;
        src.height = tb_rect->height;

        tig_video_composite_ui_texture(tex, &src, tb_rect, (float)a / 255.0f, NULL);
    }
}

/**
 * Adds a new text bubble for a game object.
 *
 * 0x4D5FE0
 */
void tb_add(int64_t obj, int type, const char* str)
{
    TextBubble* tb;
    TigRect dirty_rect;

    // CE: if the object already has a bubble whose content matches
    // (same type AND same text), don't tear down + rebuild. Just
    // refresh the timestamp so it stays visible for another
    // duration window. Without this, NPCs that chirp the same line
    // repeatedly (script polling, idle barks) would evict their own
    // bubble every call — restarting the entrance fade-in at
    // alpha=0 each time, so the bubble appears to flicker, and
    // (with the timeout-driven exit fade now in place) never
    // settling at full alpha long enough for the timeout to fire,
    // leaving the bubble effectively stuck. Refresh-only short-
    // circuit fixes both.
    {
        TextBubble* existing = find_text_bubble(obj);
        if (existing != NULL
            && existing->type == type
            && strncmp(existing->str, str, TB_STR_MAXLEN - 1) == 0) {
            existing->timestamp = gamelib_ping_time;
            existing->duration = tb_text_duration;
            int idx = (int)(existing - tb_text_bubbles);
            if (idx >= 0 && idx < MAX_TEXT_BUBBLES) {
                // Cancel any in-flight exit fade — bubble is
                // refreshed, no longer pending removal.
                if (tb_pending_remove[idx]) {
                    ui_anim_cancel(tb_alpha_handle[idx]);
                    tb_pending_remove[idx] = false;
                    tb_alpha_target[idx] = 255;
                    tb_alpha_handle[idx] = ui_anim_int_to(
                        &tb_alpha[idx], 255, &tb_entrance_profile);
                }
                // Clear any drift-fade lock — bubble is being refreshed and
                // should follow its natural placement again.
                tb_drift_locked[idx] = false;
            }
            return;
        }
    }

    // Find or allocate a text bubble for the object.
    tb = find_free_text_bubble(obj);
    if (tb == NULL) {
        return;
    }

    // Reset video buffer with the background color.
    tig_video_buffer_fill(tb->video_buffer, &tb_content_rect, tb_background_color);

    // Render text to the video buffer.
    tig_font_push(tb_fonts[type]);
    tig_font_write(tb->video_buffer, str, &tb_content_rect, &dirty_rect);
    tig_font_pop();

    // Set up the text bubble properties.
    tb->timestamp = gamelib_ping_time;
    tb->duration = tb_text_duration;
    tb->flags = TEXT_BUBBLE_IN_USE;
    tb->obj = obj;
    tb->rect = dirty_rect;
    tb->type = type;
    tb->rendered_align = TB_ALIGN_INVALID;
    strncpy(tb->str, str, TB_STR_MAXLEN - 1);
    tb->str[TB_STR_MAXLEN - 1] = '\0';

    // CE: seed alpha to 0 and animate it up to 255 so the bubble
    // fades in as it appears, rather than popping. Cancel any stale
    // in-flight tween FIRST so the int_to actually starts from our
    // fresh 0 — otherwise ui_anim retargets the old slot and keeps
    // its tracked value (e.g. mid-exit-fade at 200) instead of
    // restarting from 0.
    {
        int idx = (int)(tb - tb_text_bubbles);
        if (idx >= 0 && idx < MAX_TEXT_BUBBLES) {
            ui_anim_cancel(tb_alpha_handle[idx]);
            tb_alpha[idx] = 0;
            tb_alpha_target[idx] = 255;
            tb_pending_remove[idx] = false;
            tb_drift_locked[idx] = false;
            tb_alpha_handle[idx] = ui_anim_int_to(&tb_alpha[idx], 255,
                &tb_entrance_profile);
        }
    }

    // Mark the object as having a text bubble.
    object_flags_set(obj, OF_TEXT);

    // Invalidate the screen rect as dirty.
    tb_get_rect(tb, &dirty_rect);
    tb_iso_window_invalidate_rect(&dirty_rect);
    tb_invalidate_resolved_changes();
}

/**
 * Sets the expiration time for a text bubble.
 *
 * Does nothing if the object have no associated text bubble.
 *
 * 0x4D6160
 */
void tb_expire_in(int64_t obj, int seconds)
{
    TextBubble* tb;

    // Find the text bubble for the object.
    tb = find_text_bubble(obj);
    if (tb == NULL) {
        return;
    }

    if (seconds == TB_EXPIRE_NEVER) {
        // Special case - mark text bubble as permanent.
        tb->flags |= TEXT_BUBBLE_PERMANENT;
    } else {
        // Update duration.
        tb->flags &= ~TEXT_BUBBLE_PERMANENT;
        tb->timestamp = gamelib_ping_time;
        tb->duration = (seconds >= 0) ? 1000 * seconds : tb_text_duration;
    }
}

/**
 * Called when the game object's position is about to change.
 *
 * 0x4D6210
 */
void tb_notify_moved(int64_t obj, int64_t loc, int offset_x, int offset_y)
{
    TextBubble* tb;
    TigRect rect;
    TigRect new_rect;

    // Find the text bubble for the object.
    tb = find_text_bubble(obj);
    if (tb == NULL) {
        return;
    }

    // Retrieve current screen rect (based on the current object's position).
    tb_get_rect(tb, &rect);

    // Calculate the new screen rect.
    tb_calc_rect(tb, loc, offset_x, offset_y, &new_rect);

    // Invalidate the combined area.
    tig_rect_union(&rect, &new_rect, &rect);
    tb_iso_window_invalidate_rect(&rect);
}

/**
 * Removes a text bubble associated with a game object.
 *
 * 0x4D62B0
 */
void tb_remove(int64_t obj)
{
    TextBubble* tb;
    unsigned int flags;

    // Find and remove the text bubble if it exists.
    tb = find_text_bubble(obj);
    if (tb != NULL) {
        tb_remove_internal(tb);
    } else {
        // Clear the text flag on the object to maintain consistency.
        flags = obj_field_int32_get(obj, OBJ_F_FLAGS);
        flags &= ~OF_TEXT;
        obj_field_int32_set(obj, OBJ_F_FLAGS, flags);
    }
}

// CE: like tb_remove but defers the actual tear-down until the
// bubble has had a chance to fade out (~380ms). Used by paths that
// dismiss a bubble in response to player action (dialogue end /
// choice selection) — without it the bubble snaps invisible, which
// reads as a glitch next to the rest of the dialogue's animated
// transitions. tb_ping's post-fade pass calls tb_remove_internal
// once alpha settles at 0. Bubbles that aren't in_use, or that
// can't be located, fall through to the immediate path so the
// OF_TEXT flag still gets cleared.
void tb_remove_with_fade(int64_t obj)
{
    TextBubble* tb = find_text_bubble(obj);
    if (tb == NULL) {
        // No bubble — just clear the consistency flag.
        unsigned int flags = obj_field_int32_get(obj, OBJ_F_FLAGS);
        flags &= ~OF_TEXT;
        obj_field_int32_set(obj, OBJ_F_FLAGS, flags);
        return;
    }
    int idx = (int)(tb - tb_text_bubbles);
    if (idx < 0 || idx >= MAX_TEXT_BUBBLES) {
        tb_remove_internal(tb);
        return;
    }
    if (tb_pending_remove[idx]) {
        // Already fading out — let it finish.
        return;
    }
    ui_anim_cancel(tb_alpha_handle[idx]);
    tb_pending_remove[idx] = true;
    tb_alpha_target[idx] = 0;
    tb_alpha_handle[idx] = ui_anim_int_to(&tb_alpha[idx], 0,
        &tb_exit_profile);
}

/**
 * Re-invalidates the dirty rects associated with each active bubble so
 * 1.0x dirty-rect rendering picks up overlap-driven moves.  Called when
 * the camera has scrolled or zoom changed.
 *
 * CE: previously also reset each bubble's rendered_align to INVALID to
 * force a re-render every scroll frame.  That re-render mutated
 * tb->rect.width mid-frame; since tb_calc_rect_ex runs twice per
 * scroll frame (once via tb_invalidate_resolved_changes, again via
 * tb_draw), the second call saw a different width than the first.
 * For sprites near a screen edge that flipped a clamp and could
 * flip the drift result — leading to alpha-tween retargets fighting
 * each other within a single frame.  The reset is also redundant:
 * tb_calc_rect_ex's natural "required_align != rendered_align"
 * check re-renders on real alignment changes anyway.
 */
void tb_invalidate_positions(void)
{
    tb_invalidate_resolved_changes();
}

/**
 * Returns true if any speech bubbles are currently active.
 * Used by the scroll system to bypass hardware scroll (which would leave
 * stale bubble pixels) when bubbles need to be repainted clean each frame.
 */
bool tb_any_active(void)
{
    int idx;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0) {
            return true;
        }
    }
    return false;
}

/**
 * Clears all active text bubbles.
 *
 * 0x4D6320
 */
void tb_clear(void)
{
    int idx;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0) {
            tb_remove_internal(&(tb_text_bubbles[idx]));
        }
    }
}

/**
 * Internal function to remove a text bubble and clean up its resources.
 *
 * 0x4D6350
 */
void tb_remove_internal(TextBubble* tb)
{
    TigRect rect;
    unsigned int flags;
    int slot;

    // Invalidate the bubble's screen area.
    tb_get_rect(tb, &rect);
    tb_iso_window_invalidate_rect(&rect);

    // Clear the text flag on the associated object.
    flags = obj_field_int32_get(tb->obj, OBJ_F_FLAGS);
    flags &= ~OF_TEXT;
    obj_field_int32_set(tb->obj, OBJ_F_FLAGS, flags);

    // Reset the text bubble's properties.
    slot = (int)(tb - tb_text_bubbles);
    tb->flags = 0;
    tb->obj = OBJ_HANDLE_NULL;
    // CE: reset fade state so the slot is clean for the next bubble.
    // Cancel any in-flight alpha tween (the slot's stored value is
    // about to become irrelevant). tb_add reseeds the alpha on the
    // next use; tb_pending_remove starts false again here so the
    // post-fade destroy loop doesn't fire on a freshly-recycled slot.
    if (slot >= 0 && slot < MAX_TEXT_BUBBLES) {
        ui_anim_cancel(tb_alpha_handle[slot]);
        tb_alpha_handle[slot] = UI_ANIM_HANDLE_INVALID;
        tb_pending_remove[slot] = false;
        tb_alpha[slot] = 0;
        tb_alpha_target[slot] = 0;
        tb_hud_push_state[slot] = TB_PUSH_NONE;
        tb_tc_push_state[slot] = TB_PUSH_NONE;
        tb_drift_locked[slot] = false;
        tb_drift_fade_start_x[slot] = 0;
        tb_drift_fade_start_y[slot] = 0;
    }

    // Invalidate both the previously drawn resolved rects and the survivors'
    // newly resolved positions so 1.0x dirty-rect redraws do not leave stale
    // fragments after removals or expiry-driven overlap changes.
    tb_invalidate_resolved_changes();

    tb_prev_resolved[slot].x = 0;
    tb_prev_resolved[slot].y = 0;
    tb_prev_resolved[slot].width = 0;
    tb_prev_resolved[slot].height = 0;
}

/**
 * Retrieves the screen rectangle for a text bubble.
 *
 * 0x4D63B0
 */
void tb_get_rect_ex(TextBubble* tb, TigRect* rect, TextBubblePlacementFlags* placement_flags)
{
    int64_t loc;
    int offset_x;
    int offset_y;

    // Retrieve the object's position and offsets.
    loc = obj_field_int64_get(tb->obj, OBJ_F_LOCATION);
    offset_x = obj_field_int32_get(tb->obj, OBJ_F_OFFSET_X);
    offset_y = obj_field_int32_get(tb->obj, OBJ_F_OFFSET_Y);

    // Calculate the screen rectangle.
    tb_calc_rect_ex(tb, loc, offset_x, offset_y, rect, placement_flags);
}

void tb_get_rect(TextBubble* tb, TigRect* rect)
{
    tb_get_rect_ex(tb, rect, NULL);
}


/**
 * Computes the text bubble's screen rectangle based on object location and
 * offsets.
 *
 * 0x4D6410
 */
void tb_calc_rect(TextBubble* tb, int64_t loc, int offset_x, int offset_y, TigRect* rect)
{
    tb_calc_rect_ex(tb, loc, offset_x, offset_y, rect, NULL);
}

// CE: picks a pin (TOP/LEFT/RIGHT) for a bubble against a rectangular
// UI obstacle and applies its clamp to *vp_left/right/bottom (and
// updates *forced_align for side pins). Returns the committed
// TbPushState for the caller to remember. Used for both HUD and TC.
//
// Closest-edge projection: from the bubble's NATURAL (ideal) position,
// compute the minimum displacement required to escape via each of the
// UI's three reachable edges (top, left, right). Smallest wins.
// Side escapes are biased by (width/2 + margin) — the bubble's
// structural overhang past its center — so wide bubbles don't lose
// to top by default just because moving sideways visually means
// moving width/2 past the edge.
//
// Stay-if-viable: if a prior frame's pin is still within drift
// tolerance, keep it. If not but an alternative pin IS viable,
// switch (saves the bubble from fading). If neither is viable,
// keep the prior pin — let the bubble fade at its committed pin
// rather than jump into another fade-zone position.
//
// Clamps are one-sided ("if (target < *vp_bottom) *vp_bottom = target"),
// so when the bubble's natural position lies in the negative space
// beside/above the UI, the clamp is a no-op against the existing
// viewport bound. That's the property that lets us call this
// unconditionally without a separate "x_over_band" gate.
static TbPushState tb_apply_ui_clamp(
    int ui_left, int ui_right, int ui_top,
    int ideal_x, int ideal_y,
    int width, int height,
    TbPushState prev_pin,
    int* vp_left, int* vp_right, int* vp_bottom,
    int* forced_align, bool align_override)
{
    int margin = TB_EDGE_MARGIN_PX;

    // Does the bubble's natural rect overlap the UI's expanded rect?
    // Bottom of UI is treated as off-screen (always overlapped on that side).
    bool overlap = (ideal_x + width > ui_left - margin)
                && (ideal_x < ui_right + margin)
                && (ideal_y + height > ui_top - margin);
    if (!overlap) {
        return TB_PUSH_NONE;
    }

    // Escapes from ideal position: minimum displacement to clear each edge.
    int top_escape   = (ideal_y + height) - (ui_top - margin);
    int left_escape  = (ideal_x + width)  - (ui_left - margin);
    int right_escape = (ui_right + margin) - ideal_x;
    if (top_escape   < 0) top_escape   = 0;
    if (left_escape  < 0) left_escape  = 0;
    if (right_escape < 0) right_escape = 0;

    // Side bias: a bubble centered over a sprite has its left/right
    // edges extending width/2 past the sprite's screen x. So any
    // sprite inside the UI's x range has a baseline left_escape of
    // at least (width/2 + margin) just from bubble structure, even
    // when the sprite is at the very edge. Subtracting the baseline
    // gives a comparable score: 0 means "sprite is at the edge,
    // bubble barely overlaps", positive means "sprite is further in".
    int side_bias = width / 2 + margin;
    int top_score   = top_escape;
    int left_score  = left_escape  - side_bias;
    int right_score = right_escape - side_bias;
    if (left_score  < 0) left_score  = 0;
    if (right_score < 0) right_score = 0;

    // Drift tolerances per pin direction. These mirror the axis-aware
    // drift check at the end of tb_calc_rect_ex: TOP pin locks y,
    // tolerable y displacement is TB_DRIFT_MAX_PX; SIDE pin locks x,
    // tolerable x displacement is TB_DRIFT_MAX_PX + width/2 (the
    // bubble's half-width is "free" because it doesn't reflect real
    // speaker motion, just bubble centering).
    int drift_top  = TB_DRIFT_MAX_PX;
    int drift_side = TB_DRIFT_MAX_PX + width / 2;

    // Closest-edge fresh winner.
    TbPushState fresh = TB_PUSH_TOP;
    int fresh_score = top_score;
    if (left_score  < fresh_score) { fresh = TB_PUSH_LEFT;  fresh_score = left_score; }
    if (right_score < fresh_score) { fresh = TB_PUSH_RIGHT; fresh_score = right_score; }

    // Stay-if-viable.
    TbPushState chosen;
    if (prev_pin == TB_PUSH_NONE) {
        chosen = fresh;
    } else {
        int prev_esc, prev_drift;
        if (prev_pin == TB_PUSH_TOP)        { prev_esc = top_escape;   prev_drift = drift_top;  }
        else if (prev_pin == TB_PUSH_LEFT)  { prev_esc = left_escape;  prev_drift = drift_side; }
        else                                { prev_esc = right_escape; prev_drift = drift_side; }
        int fresh_esc, fresh_drift;
        if (fresh == TB_PUSH_TOP)           { fresh_esc = top_escape;   fresh_drift = drift_top;  }
        else if (fresh == TB_PUSH_LEFT)     { fresh_esc = left_escape;  fresh_drift = drift_side; }
        else                                { fresh_esc = right_escape; fresh_drift = drift_side; }

        if (prev_esc <= prev_drift) {
            chosen = prev_pin;          // viable, stay committed
        } else if (fresh_esc <= fresh_drift) {
            chosen = fresh;             // switch to save from fade
        } else {
            chosen = prev_pin;          // both unviable; let it fade at prev
        }
    }

    // Apply the chosen pin's clamp.
    if (chosen == TB_PUSH_TOP) {
        int top_b = ui_top - margin - height;
        if (top_b < *vp_bottom) *vp_bottom = top_b;
    } else if (chosen == TB_PUSH_LEFT) {
        int r = ui_left - margin - width;
        if (r < *vp_right) *vp_right = r;
        if (align_override || *forced_align == TB_ALIGN_INVALID) {
            *forced_align = TB_ALIGN_RIGHT;
        }
    } else if (chosen == TB_PUSH_RIGHT) {
        int l = ui_right + margin;
        if (l > *vp_left) *vp_left = l;
        if (align_override || *forced_align == TB_ALIGN_INVALID) {
            *forced_align = TB_ALIGN_LEFT;
        }
    }

    return chosen;
}

void tb_calc_rect_ex(TextBubble* tb, int64_t loc, int offset_x, int offset_y, TigRect* rect, TextBubblePlacementFlags* placement_flags)
{
    int64_t x;
    int64_t y;

    if (placement_flags != NULL) {
        *placement_flags = 0;
    }

    // Retrieve screen coordinates of the location.
    location_xy(loc, &x, &y);

    // Apply tile-centering and sub-tile offsets in world space first. The
    // world scale-blit zooms everything including these offsets, so they must
    // be part of the anchor before zoom is applied for the bubble to track the
    // sprite correctly.
    x += offset_x + 40;
    y += offset_y + 20;

    // Zoom the full anchor to match the sprite's actual screen position.
    float z = iso_zoom_current();

    // Query actual sprite art dimensions so all directional gaps track the
    // real rendered size of each creature rather than using humanoid approximations.
    // hot_y = distance from sprite top to tile anchor (drives TB_POS_TOP clearance).
    // hot_x = anchor distance from left edge; (width - hot_x) = right edge extent.
    // Diagonals use half the relevant extent; pure sides use the full extent.
    int sprite_hot_y     = 75; // fallback
    int sprite_left_ext  = 80; // fallback (≈ hot_x for a humanoid)
    int sprite_right_ext = 80; // fallback (≈ width - hot_x for a humanoid)
    if (tb->obj != OBJ_HANDLE_NULL) {
        tig_art_id_t aid = (tig_art_id_t)obj_field_int32_get(tb->obj, OBJ_F_CURRENT_AID);
        TigArtFrameData art_data;
        if (tig_art_frame_data(aid, &art_data) == TIG_OK) {
            int scale = obj_field_int32_get(tb->obj, OBJ_F_BLIT_SCALE);
            bool shrunk = (obj_field_int32_get(tb->obj, OBJ_F_FLAGS) & OF_SHRUNK) != 0;
            int hy   = art_data.hot_y;
            int lext = art_data.hot_x;
            int rext = art_data.width - art_data.hot_x;
            if (scale != 100) {
                hy   = (int)((float)hy   * (float)scale / 100.0f);
                lext = (int)((float)lext * (float)scale / 100.0f);
                rext = (int)((float)rext * (float)scale / 100.0f);
            }
            if (shrunk) {
                hy   /= 2;
                lext /= 2;
                rext /= 2;
            }
            sprite_hot_y     = hy;
            sprite_left_ext  = lext;
            sprite_right_ext = rext;
        }
    }
    if (z != 1.0f) {
        TigRect cr;
        int64_t cx;
        int64_t cy;
        gamelib_get_iso_content_rect(&cr);
        cx = cr.width / 2;
        cy = cr.height / 2;
        x = cx + (int64_t)((float)(x - cx) * z);
        y = cy + (int64_t)((float)(y - cy) * z);
    }

    // Check for coordinate overflow and return an empty rectangle if invalid.
    if (x < INT_MIN
        || x > INT_MAX
        || y < INT_MIN
        || y > INT_MAX) {
        rect->x = 0;
        rect->y = 0;
        rect->width = 0;
        rect->height = 0;
        return;
    }

    // Set the rectangle's size based on the bubble's content.
    rect->width = tb->rect.width;
    rect->height = tb->rect.height;

    // Ideal position: bubble centered above sprite head.
    int ideal_x = (int)x - rect->width / 2;
    int ideal_y = (int)y - rect->height - (int)((float)sprite_hot_y * z) - TB_BUBBLE_GAP_PX;

    // Pass 1 — base bounds (edge margin only, no UI insets).
    int vp_left   = tb_iso_content_rect.x + TB_EDGE_MARGIN_PX;
    int vp_right  = tb_iso_content_rect.x + tb_iso_content_rect.width  - rect->width  - TB_EDGE_MARGIN_PX;
    int vp_top    = tb_iso_content_rect.y + TB_EDGE_MARGIN_PX;
    int vp_bottom = tb_iso_content_rect.y + tb_iso_content_rect.height - rect->height - TB_EDGE_MARGIN_PX;

    rect->x = ideal_x < vp_left   ? vp_left   : (ideal_x > vp_right  ? vp_right  : ideal_x);
    rect->y = ideal_y < vp_top    ? vp_top    : (ideal_y > vp_bottom  ? vp_bottom : ideal_y);

    // Pass 2 — collision check against UI elements using the actual clamped
    // position. Bounds are tightened where the bubble actually lands, then
    // re-clamped. This handles all approach directions correctly.

    // Top/bottom chrome bars — span the full viewport width on all resolutions.
    // CE: read the EFFECTIVE bar heights from intgame, not the design-time
    // constants. The top bar may be slid off (any non-FULL TAB stage) and
    // the bottom bar may be cropped (MEDIUM / MINI / HIDDEN) — in those
    // states bubbles can use the freed screen space.
    //
    // The bottom bar's HORIZONTAL extent also shrinks in MEDIUM/MINI: the
    // bar window stays full-width but only a centered band is composited
    // (410px wide for MEDIUM, 394px for MINI). Bubbles whose x-range
    // doesn't overlap the visible band can use the full bottom rows —
    // the freed side areas (~195px left + ~195px right at MEDIUM) are
    // usable iso-world space.
    int ui_top    = intgame_hud_top_offset();
    int ui_bottom = GAME_UI_BAR_BOTTOM - intgame_hud_bottom_top_crop();
    int band_x_design = 0, band_w_design = 0;
    intgame_hud_bottom_band_design_x(&band_x_design, &band_w_design);
    // Translate the band's 800-wide design x to iso-world screen x.
    // The HUD bar is centered horizontally within the iso content
    // (see iso_interface_create's GRAVITY_CENTER_HORIZONTAL).
    int band_screen_left = tb_iso_content_rect.x
        + (tb_iso_content_rect.width - 800) / 2 + band_x_design;
    int band_screen_right = band_screen_left + band_w_design;
    int band_top_y = tb_iso_content_rect.y + tb_iso_content_rect.height
        - ui_bottom;
    // CE: forced text alignment from a UI side-push. INVALID = let the
    // standard sprite-anchored alignment logic below pick. When set
    // here, the side push overrides — bubble sliding along the left
    // side of HUD/TC gets right-aligned text so the text reads flush
    // against the UI it's hugging; sliding along the right side gets
    // left-aligned. Mirrors the screen-edge alignment switch.
    int forced_align = TB_ALIGN_INVALID;
    // CE: per-bubble push-state hysteresis. prev_hud / prev_tc carry
    // the prior frame's commitment per UI; new_hud / new_tc
    // accumulate this frame's decision and get committed back to
    // tb_hud_push_state / tb_tc_push_state at the end. Per-UI
    // arrays prevent direction leakage between HUD and TC.
    int slot_idx = (int)(tb - tb_text_bubbles);
    bool slot_valid = (slot_idx >= 0 && slot_idx < MAX_TEXT_BUBBLES);
    TbPushState prev_hud = slot_valid ? tb_hud_push_state[slot_idx] : TB_PUSH_NONE;
    TbPushState prev_tc  = slot_valid ? tb_tc_push_state[slot_idx]  : TB_PUSH_NONE;
    TbPushState new_hud = TB_PUSH_NONE;
    TbPushState new_tc  = TB_PUSH_NONE;
    // CE: top bar lateral negative space. The top bar is 800px wide
    // at design coords, centered horizontally. At higher resolutions
    // the iso content extends past the top bar on the left and right
    // sides — that lateral negative space is usable for bubbles
    // whose x range doesn't overlap the top bar. Only apply the
    // ui_top clamp (push bubble below the visible top bar) when
    // the bubble's natural x is over the top bar's design x range.
    //
    // Unlike the bottom band, the top bar doesn't crop laterally
    // per TAB stage — its width is always 800 when visible. So the
    // condition is simpler: just an x-range overlap check.
    int top_bar_screen_left = tb_iso_content_rect.x
        + (tb_iso_content_rect.width - 800) / 2;
    int top_bar_screen_right = top_bar_screen_left + 800;
    {
        bool bubble_x_over_top_bar = (ui_top > 0)
            && (rect->x < top_bar_screen_right + TB_EDGE_MARGIN_PX)
            && (rect->x + rect->width > top_bar_screen_left - TB_EDGE_MARGIN_PX);
        if (bubble_x_over_top_bar) {
            int t = tb_iso_content_rect.y + ui_top + TB_EDGE_MARGIN_PX;
            if (t > vp_top) vp_top = t;
        }

        // HUD band — unified pin pass. The helper checks natural
        // overlap internally and returns NONE when no clamp is
        // needed; clamps are idempotent so the call is safe even
        // when the bubble lives in lateral negative space.
        if (band_w_design > 0) {
            new_hud = tb_apply_ui_clamp(
                band_screen_left, band_screen_right, band_top_y,
                ideal_x, ideal_y, rect->width, rect->height,
                prev_hud,
                &vp_left, &vp_right, &vp_bottom,
                &forced_align, /*align_override=*/true);
        }
    }

    // Follower portrait panel (left column, variable height).
    // Use effective_y: bars run first and raise vp_top, so the bubble may land
    // lower than rect->y. Using the post-bar minimum y catches short bubbles
    // that would otherwise slip into the panel zone after the y re-clamp.
    {
        int panel_bottom = follower_ui_panel_bottom();
        if (panel_bottom > 0) {
            int panel_top   = tb_iso_content_rect.y + ui_top;
            int effective_y = rect->y < vp_top ? vp_top : rect->y;
            if (rect->x < tb_iso_content_rect.x + TB_FOLLOWER_PANEL_WIDTH + TB_EDGE_MARGIN_PX
                && effective_y + rect->height > panel_top
                && effective_y < tb_iso_content_rect.y + panel_bottom) {
                int l = tb_iso_content_rect.x + TB_FOLLOWER_PANEL_WIDTH + TB_EDGE_MARGIN_PX;
                if (l > vp_left) vp_left = l;
            }
        }
    }

    // Dialogue choice box — unified pin pass with a below-TC special case
    // preserved: when the speaker's natural y sits in TC's lower half AND
    // there's room below TC, drop the bubble below TC instead of forcing
    // it above. This matters most in cropped HUD stages where the gap
    // beneath TC is the most natural place for a follower NPC's bubble.
    if (tc_is_active()) {
        TigRect tc = tc_get_content_rect();
        bool x_over_tc = (ideal_x + rect->width > tc.x - TB_EDGE_MARGIN_PX)
            && (ideal_x < tc.x + tc.width + TB_EDGE_MARGIN_PX);
        bool ideal_below = (ideal_y >= tc.y + tc.height / 2);
        int below_min = tc.y + tc.height + TB_EDGE_MARGIN_PX;
        // CE: while TC or HUD is mid-animation (TAB-HUD stage transitions),
        // the gap below TC and the HUD's clamp geometry are both in motion.
        // If the bubble was already pinned above TC (apply_ui_clamp branch
        // = TOP/LEFT/RIGHT), suppress the below-TC entry during the
        // animation — otherwise the briefly-opened gap satisfies below_fits,
        // the bubble drops below, and a couple frames later the animation
        // closes the gap again and pushes the bubble back. Sliding the
        // bubble smoothly with the moving UI is the desired feel.
        bool ui_settling = tc_is_settling() || intgame_hud_is_settling();
        bool prev_tc_was_pin = (prev_tc == TB_PUSH_TOP
            || prev_tc == TB_PUSH_LEFT
            || prev_tc == TB_PUSH_RIGHT);
        bool inhibit_below_tc = ui_settling && prev_tc_was_pin;

        // CE: below_fits used to be `below_min <= vp_bottom`, but vp_bottom
        // is only tightened by HUD's TOP pin when the bubble's NATURAL
        // position already overlaps HUD. For a bubble pinned high up
        // against TC's top edge, natural is well above HUD, so HUD pin
        // never fires and vp_bottom stays at "stay on screen" — making
        // below_fits trivially true and dropping the bubble into a
        // below_min position that lands inside the HUD bar. Next frame
        // ideal_below flips and the bubble snaps back above TC. The
        // flicker the user reported when bumping speaker up against TC.
        //
        // Real check: would a bubble placed at rect.y = below_min still
        // be safe vs. HUD? Reuses the band geometry computed above.
        // Outside the band's x range the bubble can use the lateral
        // space all the way down to the screen edge.
        int below_max_top;
        {
            bool below_x_over_band = (band_w_design > 0)
                && (rect->x < band_screen_right + TB_EDGE_MARGIN_PX)
                && (rect->x + rect->width > band_screen_left - TB_EDGE_MARGIN_PX);
            if (below_x_over_band) {
                below_max_top = band_top_y - TB_EDGE_MARGIN_PX - rect->height;
            } else {
                below_max_top = tb_iso_content_rect.y + tb_iso_content_rect.height
                    - rect->height - TB_EDGE_MARGIN_PX;
            }
        }
        bool below_fits = (below_min <= below_max_top);

        if (x_over_tc && ideal_below && below_fits && !inhibit_below_tc) {
            if (below_min > vp_top) vp_top = below_min;
            new_tc = TB_PUSH_NONE;
        } else {
            new_tc = tb_apply_ui_clamp(
                tc.x, tc.x + tc.width, tc.y,
                ideal_x, ideal_y, rect->width, rect->height,
                prev_tc,
                &vp_left, &vp_right, &vp_bottom,
                &forced_align, /*align_override=*/false);
        }
    }

    // Re-clamp with tightened bounds.
    rect->x = rect->x < vp_left   ? vp_left   : (rect->x > vp_right  ? vp_right  : rect->x);
    rect->y = rect->y < vp_top    ? vp_top    : (rect->y > vp_bottom  ? vp_bottom : rect->y);

    if (placement_flags != NULL) {
        if (rect->x > ideal_x) {
            *placement_flags |= TB_PLACEMENT_CLAMP_LEFT;
        } else if (rect->x < ideal_x) {
            *placement_flags |= TB_PLACEMENT_CLAMP_RIGHT;
        }

        if (rect->y > ideal_y) {
            *placement_flags |= TB_PLACEMENT_CLAMP_TOP;
        } else if (rect->y < ideal_y) {
            *placement_flags |= TB_PLACEMENT_CLAMP_BOTTOM;
        }
    }

    // Hide bubble if NPC has panned too far out of frame.
    // The downward threshold gets an extra sprite_hot_y of slack: ideal_y is
    // above the sprite head, so sliding the bubble down toward (or past) the
    // sprite body is semantically natural and deserves more room than any
    // other direction before we decide to hide.
    //
    // CE: instead of a hard zero-rect when drifted, retarget the
    // bubble's alpha tween. Fade-out (~360ms) plays while the NPC
    // pans out; if it comes back in, fade-in (~200ms) starts from
    // wherever the alpha is now. Bubbles whose drift state hasn't
    // changed since last call see an int_to retarget to the same
    // value (cheap no-op).
    // CE: commit each UI's sticky state independently so neither
    // leaks direction into the other. Stay-if-viable handling lives
    // inside tb_apply_ui_clamp itself.
    if (slot_valid) {
        tb_hud_push_state[slot_idx] = new_hud;
        tb_tc_push_state[slot_idx] = new_tc;
    }
    int drift_horiz = TB_DRIFT_MAX_PX + rect->width / 2;
    int drift_down  = TB_DRIFT_MAX_PX + (int)((float)sprite_hot_y * z);
    bool drifted = (rect->x - ideal_x > drift_horiz
        || ideal_x - rect->x > drift_horiz
        || rect->y - ideal_y > drift_down
        || ideal_y - rect->y > TB_DRIFT_MAX_PX);
    // CE: if an exit fade is in flight (timeout or
    // tb_remove_with_fade), the drift system must NOT touch the
    // alpha tween. Without this guard, the drift block's "speaker
    // is in range, fade back to 255" branch cancels the exit
    // tween and revives the bubble — it then lives forever
    // because alpha never reaches 0 and the destroy condition
    // (pending_remove && alpha == 0) never fires. The exit fade
    // is authoritative; we just keep emitting the rect so the
    // diminishing alpha is still drawn.
    bool exit_in_flight = slot_valid && tb_pending_remove[slot_idx];
    if (exit_in_flight) {
        if (tb_alpha[slot_idx] == 0) {
            if (placement_flags != NULL) {
                *placement_flags = 0;
            }
            rect->x = 0; rect->y = 0; rect->width = 0; rect->height = 0;
            return;
        }
    } else if (drifted) {
        // CE: enter drift-fade-out commitment. Snapshot the rect where the
        // fade started so the bubble dies in place rather than tracking the
        // speaker mid-fade (which would teleport the bubble along with them
        // and read as a glitch). Continued drift while locked just keeps
        // alpha dropping; rect stays at the snapshot via the override below.
        if (slot_valid && !tb_drift_locked[slot_idx]) {
            tb_drift_fade_start_x[slot_idx] = rect->x;
            tb_drift_fade_start_y[slot_idx] = rect->y;
            tb_drift_locked[slot_idx] = true;
        }
        if (slot_valid && tb_alpha_target[slot_idx] != 0) {
            ui_anim_cancel(tb_alpha_handle[slot_idx]);
            tb_alpha_target[slot_idx] = 0;
            tb_alpha_handle[slot_idx] = ui_anim_int_to(&tb_alpha[slot_idx], 0,
                &tb_fade_out_profile);
        }
        // Once alpha has settled at 0, stop emitting a rect entirely —
        // saves dirty-rect / overlap work on an invisible bubble. The
        // slot stays alive so a speaker who comes back can respawn it.
        if (slot_valid && tb_alpha[slot_idx] == 0) {
            if (placement_flags != NULL) {
                *placement_flags = 0;
            }
            rect->x = 0; rect->y = 0; rect->width = 0; rect->height = 0;
            return;
        }
    } else {
        // Not drifted. Three sub-cases depending on what the alpha tween is
        // currently doing:
        //   (a) Drift fade mid-flight (locked, alpha>0): speaker came back
        //       in range. If the bubble's natural rect is within
        //       TB_DRIFT_MAX_PX of the locked position, reverse the fade
        //       smoothly (small return). If it's farther — a "far jump"
        //       like a pin release or camera snap — keep the fade going
        //       to completion at the locked position. We'll respawn at
        //       the new natural rect in case (b).
        //   (b) Drift fade completed (locked, alpha==0): unlock and
        //       trigger a fresh entrance fade at the now-natural rect.
        //       This is the "respawn at the new jump point" behavior.
        //   (c) Normal not-drifted (unlocked): ensure target=255.
        if (slot_valid && tb_drift_locked[slot_idx] && tb_alpha[slot_idx] > 0) {
            int dx = rect->x - tb_drift_fade_start_x[slot_idx];
            int dy = rect->y - tb_drift_fade_start_y[slot_idx];
            long long dist_sq = (long long)dx * dx + (long long)dy * dy;
            long long thr_sq  = (long long)TB_DRIFT_MAX_PX * TB_DRIFT_MAX_PX;
            if (dist_sq <= thr_sq) {
                tb_drift_locked[slot_idx] = false;
                if (tb_alpha_target[slot_idx] != 255) {
                    ui_anim_cancel(tb_alpha_handle[slot_idx]);
                    tb_alpha_target[slot_idx] = 255;
                    tb_alpha_handle[slot_idx] = ui_anim_int_to(&tb_alpha[slot_idx], 255,
                        &tb_fade_in_profile);
                }
            }
            // else: far jump — stay locked, let fade-out finish.
        } else if (slot_valid && tb_drift_locked[slot_idx] && tb_alpha[slot_idx] == 0) {
            // Fade-out completed at the old rect; respawn at the new natural rect.
            tb_drift_locked[slot_idx] = false;
            ui_anim_cancel(tb_alpha_handle[slot_idx]);
            tb_alpha_target[slot_idx] = 255;
            tb_alpha_handle[slot_idx] = ui_anim_int_to(&tb_alpha[slot_idx], 255,
                &tb_entrance_profile);
        } else if (slot_valid && !tb_drift_locked[slot_idx]
                && tb_alpha_target[slot_idx] != 255) {
            ui_anim_cancel(tb_alpha_handle[slot_idx]);
            tb_alpha_target[slot_idx] = 255;
            tb_alpha_handle[slot_idx] = ui_anim_int_to(&tb_alpha[slot_idx], 255,
                &tb_fade_in_profile);
        }
    }

    // CE: while drift-locked, render at the snapshot rect so the bubble
    // dies in place. Unlock happens in case (a) reverse or case (b) respawn
    // above, after which natural placement resumes.
    if (slot_valid && tb_drift_locked[slot_idx]) {
        rect->x = tb_drift_fade_start_x[slot_idx];
        rect->y = tb_drift_fade_start_y[slot_idx];
    }

    // Text alignment follows the bubble center relative to half the sprite
    // width. This keeps normal over-the-head bubbles centered, but switches to
    // side-aligned text once the bubble is meaningfully pushed to one side.
    {
        int bubble_cx = rect->x + rect->width / 2;
        int left_threshold = (int)x - (int)((float)sprite_left_ext * z * 0.5f) - TB_ALIGN_SWITCH_MARGIN_PX;
        int right_threshold = (int)x + (int)((float)sprite_right_ext * z * 0.5f) + TB_ALIGN_SWITCH_MARGIN_PX;
        int required_align;

        if (bubble_cx > right_threshold)
            required_align = TB_ALIGN_LEFT;
        else if (bubble_cx < left_threshold)
            required_align = TB_ALIGN_RIGHT;
        else
            required_align = TB_ALIGN_CENTERED;

        // CE: HUD/TC side-push override — when the bubble was just
        // clamped against the side of a UI element above, the text
        // should read flush against that element regardless of where
        // the sprite is. Mirrors the screen-edge alignment switch.
        if (forced_align != TB_ALIGN_INVALID) {
            required_align = forced_align;
        }

        if (required_align != tb->rendered_align && tb->str[0] != '\0') {
            TigRect dirty_rect;
            tig_font_handle_t* font_set = (required_align == TB_ALIGN_LEFT)  ? tb_fonts_left  :
                                          (required_align == TB_ALIGN_RIGHT) ? tb_fonts_right :
                                                                               tb_fonts;
            tig_video_buffer_fill(tb->video_buffer, &tb_content_rect, tb_background_color);
            tig_font_push(font_set[tb->type]);
            tig_font_write(tb->video_buffer, tb->str, &tb_content_rect, &dirty_rect);
            tig_font_pop();
            tb->rect    = dirty_rect;
            rect->width  = tb->rect.width;
            rect->height = tb->rect.height;
            tb->rendered_align = required_align;
        }
    }
}

/**
 * Computes the current resolved on-screen rect for every active bubble.
 *
 * This runs the normal placement pass plus overlap resolution without drawing,
 * so callers can share the same settled layout for invalidation and blitting.
 */
static void tb_collect_resolved_rects(TigRect* rects, int* anchor_ys, TextBubblePlacementFlags* placement_flags)
{
    int idx;

    // Pass 1: compute screen rect for every bubble independently. tb_calc_rect
    // handles anchor→screen transform, UI collision, and drift hiding.
    // Also capture each bubble's unclamped sprite anchor Y for stable sorting
    // plus which screen/UI safeties clamped the preferred placement.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0) {
            tb_get_rect_ex(&(tb_text_bubbles[idx]), &rects[idx], &placement_flags[idx]);
            anchor_ys[idx] = tb_get_anchor_y(&tb_text_bubbles[idx]);
        } else {
            rects[idx].x = 0;
            rects[idx].y = 0;
            rects[idx].width = 0;
            rects[idx].height = 0;
            anchor_ys[idx] = 0;
            placement_flags[idx] = 0;
        }
    }

    // Pass 2: push overlapping bubbles apart so they stack cleanly.
    tb_resolve_overlaps(rects, anchor_ys, placement_flags);
}

static void tb_invalidate_resolved_changes(void)
{
    int idx;
    TigRect rects[MAX_TEXT_BUBBLES];
    int anchor_ys[MAX_TEXT_BUBBLES];
    TextBubblePlacementFlags placement_flags[MAX_TEXT_BUBBLES];
    TigRect dirty_rect;
    bool had_prev;
    bool has_curr;

    // Dirty both the previously drawn resolved rects and the layout we would
    // draw now, so 1.0x dirty-rect rendering does not miss overlap-driven
    // bubble moves during add/remove/reset transitions.
    tb_collect_resolved_rects(rects, anchor_ys, placement_flags);

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        had_prev = tb_prev_resolved[idx].width > 0 && tb_prev_resolved[idx].height > 0;
        has_curr = rects[idx].width > 0 && rects[idx].height > 0;

        if (!had_prev && !has_curr) {
            continue;
        }

        if (had_prev && has_curr) {
            tig_rect_union(&tb_prev_resolved[idx], &rects[idx], &dirty_rect);
        } else if (had_prev) {
            dirty_rect = tb_prev_resolved[idx];
        } else {
            dirty_rect = rects[idx];
        }

        tb_iso_window_invalidate_rect(&dirty_rect);
    }
}

/**
 * Resolves inter-bubble overlaps without causing position inversions.
 *
 * Y axis: bubbles are sorted top-to-bottom, then resolved with a two-phase
 * cascade so relative order is always preserved:
 *   Phase 1 (top→bottom): each bubble pushes the one below it downward.
 *   Phase 2 (bottom→top): each bubble pushes the one above it upward.
 * When a bubble is stopped by a viewport boundary the cascade propagates to
 * the next bubble in the sorted chain rather than inverting order.
 *
 * X axis: any pair that still overlaps in both axes after Y settlement is
 * resolved symmetrically — each bubble moves half the X overlap in opposite
 * directions, with boundary absorption.
 *
 * Multiple passes handle chains of three or more overlapping bubbles.
 * Hidden bubbles (zero area) are skipped.
 */

/**
 * Returns the sprite's screen Y anchor for bubble ordering.
 *
 * This is the tile+offset screen coordinate after zoom — the point the bubble
 * attaches above — computed without any clamping.  Used as a stable sort key
 * so relative ordering between bubbles is preserved even when multiple bubbles
 * are clamped to the same boundary (e.g. both at vp_top).
 */
static int tb_get_anchor_y(TextBubble* tb)
{
    int64_t loc     = obj_field_int64_get(tb->obj, OBJ_F_LOCATION);
    int     off_y   = obj_field_int32_get(tb->obj, OBJ_F_OFFSET_Y);
    int64_t x;
    int64_t y;

    location_xy(loc, &x, &y);
    y += off_y + 20;

    float z = iso_zoom_current();
    if (z != 1.0f) {
        TigRect cr;
        gamelib_get_iso_content_rect(&cr);
        int64_t cy = cr.height / 2;
        y = cy + (int64_t)((float)(y - cy) * z);
    }

    return (int)y;
}

/**
 * Returns the maximum safe Y for a bubble's top edge, accounting for the
 * bottom chrome bar and (when active) the dialogue choice box.
 * Mirrors the bottom-bound logic in tb_calc_rect pass 2.
 */
static int tb_get_safe_bottom(int rect_x, int rect_width, int rect_height)
{
    // CE: shrink the reserved bottom-bar height as the TAB stage crops
    // the HUD AND only apply the reservation when the bubble's x-range
    // overlaps the bar's visible band. Bubbles in the side area (where
    // the band has been cropped away horizontally) can use the iso-
    // world bottom edge.
    int ui_bottom = GAME_UI_BAR_BOTTOM - intgame_hud_bottom_top_crop();
    int band_x_design = 0, band_w_design = 0;
    intgame_hud_bottom_band_design_x(&band_x_design, &band_w_design);
    int band_screen_left = tb_iso_content_rect.x
        + (tb_iso_content_rect.width - 800) / 2 + band_x_design;
    int band_screen_right = band_screen_left + band_w_design;
    bool bubble_over_band = (band_w_design > 0)
        && (rect_x < band_screen_right + TB_EDGE_MARGIN_PX)
        && (rect_x + rect_width > band_screen_left - TB_EDGE_MARGIN_PX);
    int limit = bubble_over_band
        ? (tb_iso_content_rect.y + tb_iso_content_rect.height
            - ui_bottom - rect_height - TB_EDGE_MARGIN_PX)
        : (tb_iso_content_rect.y + tb_iso_content_rect.height
            - rect_height - TB_EDGE_MARGIN_PX);

    if (tc_is_active()) {
        TigRect tc = tc_get_content_rect();
        if (rect_x < tc.x + tc.width + TB_EDGE_MARGIN_PX
            && rect_x + rect_width > tc.x - TB_EDGE_MARGIN_PX) {
            int tc_limit = tc.y - TB_EDGE_MARGIN_PX - rect_height;
            if (tc_limit < limit) {
                limit = tc_limit;
            }
        }
    }

    return limit;
}

static bool tb_rects_overlap_horizontally(const TigRect* a, const TigRect* b, int gap)
{
    return !(a->x + a->width + gap <= b->x
        || b->x + b->width + gap <= a->x);
}

static bool tb_rects_overlap_vertically(const TigRect* a, const TigRect* b)
{
    return !(a->y + a->height <= b->y
        || b->y + b->height <= a->y);
}

static bool tb_pair_prefers_x_cascade(int first,
    int second,
    TigRect* rects,
    TigRect* base_rects,
    TextBubblePlacementFlags* placement_flags,
    bool allow_x_preferred_skip)
{
    TextBubblePlacementFlags side_flags = TB_PLACEMENT_CLAMP_LEFT | TB_PLACEMENT_CLAMP_RIGHT;
    TextBubblePlacementFlags vertical_flags = TB_PLACEMENT_CLAMP_TOP | TB_PLACEMENT_CLAMP_BOTTOM;
    TextBubblePlacementFlags combined_flags;
    TextBubblePlacementFlags shared_vertical_flags;

    if (!allow_x_preferred_skip) {
        return false;
    }

    if (rects[first].y != base_rects[first].y
        || rects[second].y != base_rects[second].y) {
        return false;
    }

    combined_flags = placement_flags[first] | placement_flags[second];
    if ((combined_flags & side_flags) == 0) {
        return false;
    }

    shared_vertical_flags = placement_flags[first] & placement_flags[second] & vertical_flags;
    if ((combined_flags & vertical_flags) != 0
        && !(rects[first].y == rects[second].y
            && base_rects[first].y == base_rects[second].y
            && (shared_vertical_flags == TB_PLACEMENT_CLAMP_TOP
                || shared_vertical_flags == TB_PLACEMENT_CLAMP_BOTTOM))) {
        return false;
    }

    return tb_rects_overlap_vertically(&(rects[first]), &(rects[second]));
}

static bool tb_has_unresolved_overlap(TigRect* rects, int* indices, int count)
{
    int i;
    int j;

    for (i = 0; i < count - 1; i++) {
        for (j = i + 1; j < count; j++) {
            TigRect* first = &(rects[indices[i]]);
            TigRect* second = &(rects[indices[j]]);

            if (tb_rects_overlap_horizontally(first, second, 0)
                && tb_rects_overlap_vertically(first, second)) {
                return true;
            }
        }
    }

    return false;
}

static void tb_resolve_overlaps(TigRect* rects, int* anchor_ys, TextBubblePlacementFlags* placement_flags)
{
    int sorted[MAX_TEXT_BUBBLES];
    TigRect base_rects[MAX_TEXT_BUBBLES];
    int count = 0;
    int i;
    int j;
    int k;
    int pass;
    bool allow_x_preferred_skip = true;

    int vp_left   = tb_iso_content_rect.x + TB_EDGE_MARGIN_PX;
    int vp_top    = tb_iso_content_rect.y + TB_EDGE_MARGIN_PX;
    int vp_right  = tb_iso_content_rect.x + tb_iso_content_rect.width  - TB_EDGE_MARGIN_PX;
    int vp_bottom = tb_iso_content_rect.y + tb_iso_content_rect.height
        - (GAME_UI_BAR_BOTTOM - intgame_hud_bottom_top_crop()) - TB_EDGE_MARGIN_PX;

    // Build index list of visible bubbles.
    for (i = 0; i < MAX_TEXT_BUBBLES; i++) {
        if (rects[i].width > 0 && rects[i].height > 0) {
            sorted[count++] = i;
        }
    }

    if (count < 2) {
        return;
    }

    for (i = 0; i < MAX_TEXT_BUBBLES; i++) {
        base_rects[i] = rects[i];
    }

    // Sort by sprite anchor Y ascending (topmost NPC first).
    // anchor_ys[] holds the unclamped sprite screen Y — stable across frames
    // even when multiple bubbles are clamped to the same boundary.
    // Insertion sort — count <= MAX_TEXT_BUBBLES (8), always cheap.
    for (i = 1; i < count; i++) {
        int key = sorted[i];
        int key_y = anchor_ys[key];
        j = i - 1;
        while (j >= 0 && anchor_ys[sorted[j]] > key_y) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // Normal overlap chains settle in <= count passes. Allow one extra sweep
    // if a side-driven X cascade stalls and needs to fall back to Y stacking.
    for (pass = 0; pass < count * 2; pass++) {
        bool any = false;

        // Phase 1 — cascade downward: process sorted top→bottom.
        // Each bubble pushes the next one down only if they overlap on both axes.
        for (k = 0; k < count - 1; k++) {
            i = sorted[k];
            j = sorted[k + 1];

            // No horizontal overlap means they're side-by-side — skip Y push.
            if (!tb_rects_overlap_horizontally(&(rects[i]), &(rects[j]), TB_BUBBLE_STACK_GAP_PX)) {
                continue;
            }

            if (tb_pair_prefers_x_cascade(i, j, rects, base_rects, placement_flags, allow_x_preferred_skip)) {
                continue;
            }

            int pen = (rects[i].y + rects[i].height + TB_BUBBLE_STACK_GAP_PX) - rects[j].y;
            if (pen > 0) {
                int max_j = tb_get_safe_bottom(rects[j].x, rects[j].width, rects[j].height);
                int old_y = rects[j].y;
                rects[j].y += pen;
                if (rects[j].y > max_j) {
                    rects[j].y = max_j;
                }
                if (rects[j].y != old_y) {
                    any = true;
                }
            }
        }

        // Phase 2 — cascade upward: process sorted bottom→top.
        // Each bubble pushes the one above it up only if they overlap on both axes.
        for (k = count - 1; k > 0; k--) {
            j = sorted[k];
            i = sorted[k - 1];

            // No horizontal overlap — skip Y push.
            if (!tb_rects_overlap_horizontally(&(rects[i]), &(rects[j]), TB_BUBBLE_STACK_GAP_PX)) {
                continue;
            }

            if (tb_pair_prefers_x_cascade(i, j, rects, base_rects, placement_flags, allow_x_preferred_skip)) {
                continue;
            }

            int pen = (rects[i].y + rects[i].height + TB_BUBBLE_STACK_GAP_PX) - rects[j].y;
            if (pen > 0) {
                int old_y = rects[i].y;
                rects[i].y -= pen;
                if (rects[i].y < vp_top) {
                    rects[i].y = vp_top;
                }
                if (rects[i].y != old_y) {
                    any = true;
                }
            }
        }

        // Phase 3 — build X-sorted order for cascading horizontally.
        // Re-use the sorted[] array (already Y-sorted); build a separate
        // x_sorted[] from the same active set.
        int x_sorted[MAX_TEXT_BUBBLES];
        for (k = 0; k < count; k++) {
            x_sorted[k] = sorted[k];
        }
        for (i = 1; i < count; i++) {
            int key = x_sorted[i];
            int key_x = rects[key].x;
            j = i - 1;
            while (j >= 0 && rects[x_sorted[j]].x > key_x) {
                x_sorted[j + 1] = x_sorted[j];
                j--;
            }
            x_sorted[j + 1] = key;
        }

        // Phase 3a — cascade rightward: push right neighbour further right.
        for (k = 0; k < count - 1; k++) {
            i = x_sorted[k];
            j = x_sorted[k + 1];

            // Only act when the pair also overlaps vertically.
            if (!tb_rects_overlap_vertically(&(rects[i]), &(rects[j]))) {
                continue;
            }

            int pen = (rects[i].x + rects[i].width + TB_BUBBLE_STACK_GAP_PX) - rects[j].x;
            if (pen > 0) {
                int max_j = vp_right - rects[j].width;
                int old_x = rects[j].x;
                rects[j].x += pen;
                if (rects[j].x > max_j) {
                    rects[j].x = max_j;
                }
                if (rects[j].x != old_x) {
                    any = true;
                }
            }
        }

        // Phase 3b — cascade leftward: push left neighbour further left.
        for (k = count - 1; k > 0; k--) {
            j = x_sorted[k];
            i = x_sorted[k - 1];

            if (!tb_rects_overlap_vertically(&(rects[i]), &(rects[j]))) {
                continue;
            }

            int pen = (rects[i].x + rects[i].width + TB_BUBBLE_STACK_GAP_PX) - rects[j].x;
            if (pen > 0) {
                int old_x = rects[i].x;
                rects[i].x -= pen;
                if (rects[i].x < vp_left) {
                    rects[i].x = vp_left;
                }
                if (rects[i].x != old_x) {
                    any = true;
                }
            }
        }

        if (!any) {
            if (allow_x_preferred_skip && tb_has_unresolved_overlap(rects, sorted, count)) {
                allow_x_preferred_skip = false;
                continue;
            }
            break;
        }

        // Re-sort by anchor Y after each pass — cascade may have moved
        // bubbles; anchor_ys preserves the stable world order.
        for (i = 1; i < count; i++) {
            int key = sorted[i];
            int key_y = anchor_ys[key];
            j = i - 1;
            while (j >= 0 && anchor_ys[sorted[j]] > key_y) {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = key;
        }
    }
}

/**
 * Called when `text duration` setting is changed.
 *
 * 0x4D67F0
 */
void tb_text_duration_changed(void)
{
    int index;

    // Calculate the new duration based on the setting (scaled to milliseconds).
    tb_text_duration = settings_get_value(&settings, TEXT_DURATION_KEY) * 1000;

    // Update the duration of all non-permanent active bubbles.
    for (index = 0; index < MAX_TEXT_BUBBLES; index++) {
        if ((tb_text_bubbles[index].flags & TEXT_BUBBLE_IN_USE) != 0
            && (tb_text_bubbles[index].flags & TEXT_BUBBLE_PERMANENT) == 0) {
            // FIX: There is an error in the original code which additionally
            // multiplies this value by 1000 (effectively scaling it to
            // microseconds).
            tb_text_bubbles[index].duration = tb_text_duration;
        }
    }
}

/**
 * Finds an existing text bubble associated with a given object.
 *
 * Returns `NULL` if object does not have an active text bubble.
 */
TextBubble* find_text_bubble(int64_t obj)
{
    int idx;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0
            && tb_text_bubbles[idx].obj == obj) {
            return &(tb_text_bubbles[idx]);
        }
    }

    return NULL;
}

/**
 * Finds a free text bubble slot or reuses an existing one for a given object.
 *
 * If no free slot exists, replaces the oldest non-permanent bubble or an
 * existing bubble for the same object.
 *
 * Returns `NULL` if no slot is available.
 */
TextBubble* find_free_text_bubble(int64_t obj)
{
    int idx;
    int idx_to_remove = -1;
    int idx_to_use = -1;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0) {
            // If the object already has a bubble, reuse it. This prevents one
            // object to have several text bubbles.
            if (tb_text_bubbles[idx].obj != OBJ_HANDLE_NULL
                && tb_text_bubbles[idx].obj == obj) {
                idx_to_remove = idx;
                idx_to_use = -1;
                break;
            }

            // Track the oldest non-permanent bubble as a fallback for
            // replacement.
            if (idx_to_remove == -1
                || ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_PERMANENT) == 0
                    && tb_text_bubbles[idx].timestamp < tb_text_bubbles[idx_to_remove].timestamp)) {
                idx_to_remove = idx;
            }
        } else {
            // Mark an unused slot for use. This is still a candidate because
            // we have to check all slots in case the specified object already
            // have an active bubble.
            idx_to_use = idx;
        }
    }

    // If no free slot was found, replace an existing bubble.
    if (idx_to_use == -1) {
        if (idx_to_remove == -1) {
            return NULL;
        }

        // Remove the selected bubble to free its slot.
        tb_remove_internal(&(tb_text_bubbles[idx_to_remove]));
        idx_to_use = idx_to_remove;
    }

    return &(tb_text_bubbles[idx_to_use]);
}
