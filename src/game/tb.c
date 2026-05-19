#include "game/tb.h"

#include <string.h>

#include "game/dialog_camera.h"
#include "game/gamelib.h"
#include "game/tc.h"
#include "ui/follower_ui.h"
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
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_CENTERED | TIG_FONT_SHADOW;
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        font.color = tig_color_make(tb_colors[idx][0], tb_colors[idx][1], tb_colors[idx][2]);
        tig_font_create(&font, &(tb_fonts[idx]));
    }

    // Left-aligned — used for right-side positions (text starts near sprite).
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_SHADOW;
    for (idx = 0; idx < TB_TYPE_COUNT; idx++) {
        font.color = tig_color_make(tb_colors[idx][0], tb_colors[idx][1], tb_colors[idx][2]);
        tig_font_create(&font, &(tb_fonts_left[idx]));
    }

    // Right-aligned — used for left-side positions (text ends near sprite).
    font.flags = TIG_FONT_NO_ALPHA_BLEND | TIG_FONT_RIGHT | TIG_FONT_SHADOW;
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

    // Pre-invalidate last frame's resolved bubble positions so the game world
    // redraws those areas before tb_draw runs this frame.  tb_resolve_overlaps
    // may shift bubbles beyond the rect that tb_calc_rect invalidated, leaving
    // the adjusted area outside the dirty-rect list at 1.0x zoom.  Marking the
    // resolved rects dirty here ensures the blit always has full coverage.
    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if (tb_prev_resolved[idx].width > 0 && tb_prev_resolved[idx].height > 0) {
            tb_iso_window_invalidate_rect(&tb_prev_resolved[idx]);
        }
    }

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        // Check if the bubble has expired.
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0
            && (tb_text_bubbles[idx].flags & TEXT_BUBBLE_PERMANENT) == 0
            && tig_timer_between(tb_text_bubbles[idx].timestamp, timestamp) > tb_text_bubbles[idx].duration) {
            // Retrive and invalidate text bubble screen rectangle.
            tb_get_rect(&(tb_text_bubbles[idx]), &rect);
            tb_iso_window_invalidate_rect(&rect);

            // Destroy the bubble.
            tb_remove_internal(&(tb_text_bubbles[idx]));
        }
    }

    // Re-evaluate bubble placement whenever the camera moves or zoom changes.
    // This lets bubbles work back to their preferred position (TOP) as soon as
    // the viewport opens up, without polling every frame — positions are only
    // reset when something actually changed.
    location_origin_get(&ox, &oy);
    z = iso_zoom_current();
    if (ox != tb_last_origin_x || oy != tb_last_origin_y || z != tb_last_zoom) {
        tb_invalidate_positions();
        tb_last_origin_x = ox;
        tb_last_origin_y = oy;
        tb_last_zoom = z;
    }
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

                // Copy the affected portion of text bubble's video buffer
                // onto the window.
                tig_window_copy_from_vbuffer(tb_iso_window_handle,
                    &dst_rect,
                    tb_text_bubbles[idx].video_buffer,
                    &src_rect);
            }
            node = node->next;
        }
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

/**
 * Resets the rendered alignment of all active text bubbles to TB_ALIGN_INVALID
 * so they are repositioned on the next draw frame.  Call this after the camera
 * has finished panning so bubbles are placed with the final viewport in mind.
 */
void tb_invalidate_positions(void)
{
    int idx;

    for (idx = 0; idx < MAX_TEXT_BUBBLES; idx++) {
        if ((tb_text_bubbles[idx].flags & TEXT_BUBBLE_IN_USE) != 0) {
            tb_text_bubbles[idx].rendered_align = TB_ALIGN_INVALID;
        }
    }

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
    {
        int t = tb_iso_content_rect.y + GAME_UI_BAR_TOP + TB_EDGE_MARGIN_PX;
        int b = tb_iso_content_rect.y + tb_iso_content_rect.height
                - GAME_UI_BAR_BOTTOM - rect->height - TB_EDGE_MARGIN_PX;
        if (t > vp_top)    vp_top    = t;
        if (b < vp_bottom) vp_bottom = b;
    }

    // Follower portrait panel (left column, variable height).
    // Use effective_y: bars run first and raise vp_top, so the bubble may land
    // lower than rect->y. Using the post-bar minimum y catches short bubbles
    // that would otherwise slip into the panel zone after the y re-clamp.
    {
        int panel_bottom = follower_ui_panel_bottom();
        if (panel_bottom > 0) {
            int panel_top   = tb_iso_content_rect.y + GAME_UI_BAR_TOP;
            int effective_y = rect->y < vp_top ? vp_top : rect->y;
            if (rect->x < tb_iso_content_rect.x + TB_FOLLOWER_PANEL_WIDTH + TB_EDGE_MARGIN_PX
                && effective_y + rect->height > panel_top
                && effective_y < tb_iso_content_rect.y + panel_bottom) {
                int l = tb_iso_content_rect.x + TB_FOLLOWER_PANEL_WIDTH + TB_EDGE_MARGIN_PX;
                if (l > vp_left) vp_left = l;
            }
        }
    }

    // Dialogue choice box — only present during dialogue.
    if (tc_is_active()) {
        TigRect tc = tc_get_content_rect();
        if (rect->x < tc.x + tc.width + TB_EDGE_MARGIN_PX
            && rect->x + rect->width > tc.x - TB_EDGE_MARGIN_PX) {
            int b = tc.y - TB_EDGE_MARGIN_PX - rect->height;
            if (b < vp_bottom) vp_bottom = b;
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
    int drift_horiz = TB_DRIFT_MAX_PX + rect->width / 2;
    int drift_down  = TB_DRIFT_MAX_PX + (int)((float)sprite_hot_y * z);
    if (rect->x - ideal_x > drift_horiz  || ideal_x - rect->x > drift_horiz
        || rect->y - ideal_y > drift_down || ideal_y - rect->y > TB_DRIFT_MAX_PX) {
        if (placement_flags != NULL) {
            *placement_flags = 0;
        }
        rect->x = 0; rect->y = 0; rect->width = 0; rect->height = 0;
        return;
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
    int limit = tb_iso_content_rect.y + tb_iso_content_rect.height
                - GAME_UI_BAR_BOTTOM - rect_height - TB_EDGE_MARGIN_PX;

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
    int vp_bottom = tb_iso_content_rect.y + tb_iso_content_rect.height - GAME_UI_BAR_BOTTOM - TB_EDGE_MARGIN_PX;

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
