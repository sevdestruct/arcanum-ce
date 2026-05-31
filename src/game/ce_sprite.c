#include "game/ce_sprite.h"

#include <string.h>

// Registry of composite sprites. Small fixed table — these are a handful of
// hand-authored named assets (coin variants, UI bits), not a per-object pool.
#define CE_SPRITE_CAP 64
#define CE_SPRITE_NAME_MAX 32

// Color key for the offscreen canvas: pixels left at this color are treated as
// transparent both when art is composited in (the art's own transparent pixels
// leave the key showing through) and when the finished sprite is drawn to a
// window. Bright magenta is effectively absent from the game's art.
#define CE_SPRITE_COLOR_KEY tig_color_make(255, 0, 255)

typedef struct CeSprite {
    char name[CE_SPRITE_NAME_MAX];
    TigVideoBuffer* vb;
    int width;
    int height;
} CeSprite;

static CeSprite ce_sprites[CE_SPRITE_CAP];
static int ce_sprite_count;

static CeSprite* ce_sprite_find(const char* name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < ce_sprite_count; i++) {
        if (strncmp(ce_sprites[i].name, name, CE_SPRITE_NAME_MAX) == 0) {
            return &ce_sprites[i];
        }
    }
    return NULL;
}

// Blit one layer into the canvas. `canvas` is the destination VB; layer source
// is either a named sprite VB or a real art id.
static void ce_sprite_blit_layer(TigVideoBuffer* canvas, int canvas_w,
    int canvas_h, const CeSpriteLayer* layer)
{
    TigRect src_rect;
    TigRect dst_rect;
    int sw;
    int sh;
    TigVideoBuffer* src_vb = NULL;

    // Resolve source extent.
    if (layer->src_sprite != NULL) {
        CeSprite* s = ce_sprite_find(layer->src_sprite);
        if (s == NULL || s->vb == NULL) {
            return;
        }
        src_vb = s->vb;
        sw = (layer->sw > 0) ? layer->sw : s->width;
        sh = (layer->sh > 0) ? layer->sh : s->height;
    } else {
        TigArtFrameData afd;
        if (tig_art_frame_data(layer->src_art, &afd) != TIG_OK) {
            return;
        }
        sw = (layer->sw > 0) ? layer->sw : afd.width;
        sh = (layer->sh > 0) ? layer->sh : afd.height;
    }

    src_rect.x = layer->sx;
    src_rect.y = layer->sy;
    src_rect.width = sw;
    src_rect.height = sh;

    // Center-first placement, then the caller's offset.
    dst_rect.x = (canvas_w - sw) / 2 + layer->off_x;
    dst_rect.y = (canvas_h - sh) / 2 + layer->off_y;
    dst_rect.width = sw;
    dst_rect.height = sh;

    if (src_vb != NULL) {
        TigVideoBufferBlitInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.flags = 0;
        if (layer->flip_x) bi.flags |= TIG_VIDEO_BUFFER_BLIT_FLIP_X;
        if (layer->flip_y) bi.flags |= TIG_VIDEO_BUFFER_BLIT_FLIP_Y;
        bi.src_video_buffer = src_vb;
        bi.src_rect = &src_rect;
        bi.dst_video_buffer = canvas;
        bi.dst_rect = &dst_rect;
        tig_video_buffer_blit(&bi);
    } else {
        TigArtBlitInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.flags = 0;
        if (layer->flip_x) bi.flags |= TIG_ART_BLT_FLIP_X;
        if (layer->flip_y) bi.flags |= TIG_ART_BLT_FLIP_Y;
        bi.art_id = layer->src_art;
        bi.src_rect = &src_rect;
        bi.dst_rect = &dst_rect;
        bi.dst_video_buffer = canvas;
        tig_art_blit(&bi);
    }
}

bool ce_sprite_define(const char* name, int canvas_w, int canvas_h,
    const CeSpriteLayer* layers, int count)
{
    TigVideoBufferCreateInfo vbci;
    TigVideoBuffer* vb = NULL;
    TigRect full;
    CeSprite* slot;
    int i;

    if (name == NULL || canvas_w <= 0 || canvas_h <= 0) {
        return false;
    }

    // Reuse an existing slot (redefine) or claim a new one.
    slot = ce_sprite_find(name);
    if (slot == NULL) {
        if (ce_sprite_count >= CE_SPRITE_CAP) {
            return false;
        }
        slot = &ce_sprites[ce_sprite_count];
    }

    memset(&vbci, 0, sizeof(vbci));
    vbci.flags = TIG_VIDEO_BUFFER_CREATE_COLOR_KEY
        | TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vbci.width = canvas_w;
    vbci.height = canvas_h;
    vbci.background_color = CE_SPRITE_COLOR_KEY;
    vbci.color_key = CE_SPRITE_COLOR_KEY;
    if (tig_video_buffer_create(&vbci, &vb) != TIG_OK || vb == NULL) {
        return false;
    }

    // Start fully transparent (the key), then composite back-to-front.
    full.x = 0;
    full.y = 0;
    full.width = canvas_w;
    full.height = canvas_h;
    tig_video_buffer_fill(vb, &full, CE_SPRITE_COLOR_KEY);

    for (i = 0; i < count; i++) {
        ce_sprite_blit_layer(vb, canvas_w, canvas_h, &layers[i]);
    }

    // Commit (free any previous buffer if redefining).
    if (slot->vb != NULL && slot->vb != vb) {
        tig_video_buffer_destroy(slot->vb);
    }
    strncpy(slot->name, name, CE_SPRITE_NAME_MAX - 1);
    slot->name[CE_SPRITE_NAME_MAX - 1] = '\0';
    slot->vb = vb;
    slot->width = canvas_w;
    slot->height = canvas_h;
    if (slot == &ce_sprites[ce_sprite_count]) {
        ce_sprite_count++;
    }
    return true;
}

bool ce_sprite_exists(const char* name)
{
    return ce_sprite_find(name) != NULL;
}

void ce_sprite_size(const char* name, int* width, int* height)
{
    CeSprite* s = ce_sprite_find(name);
    if (width != NULL) {
        *width = (s != NULL) ? s->width : 0;
    }
    if (height != NULL) {
        *height = (s != NULL) ? s->height : 0;
    }
}

void ce_sprite_draw(tig_window_handle_t window_handle, const char* name,
    int x, int y, uint8_t alpha)
{
    CeSprite* s = ce_sprite_find(name);
    TigRect src_rect;
    TigRect dst_rect;

    if (s == NULL || s->vb == NULL) {
        return;
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = s->width;
    src_rect.height = s->height;
    dst_rect.x = x;
    dst_rect.y = y;
    dst_rect.width = s->width;
    dst_rect.height = s->height;

    tig_window_copy_from_vbuffer_alpha(window_handle, &dst_rect, s->vb,
        &src_rect, alpha);
}

TigVideoBuffer* ce_sprite_vbuffer(const char* name)
{
    CeSprite* s = ce_sprite_find(name);
    return (s != NULL) ? s->vb : NULL;
}

void ce_sprite_shutdown(void)
{
    int i;
    for (i = 0; i < ce_sprite_count; i++) {
        if (ce_sprites[i].vb != NULL) {
            tig_video_buffer_destroy(ce_sprites[i].vb);
            ce_sprites[i].vb = NULL;
        }
    }
    ce_sprite_count = 0;
}

// --- Reserved art ids backed by raw .dat paths ----------------------------
// A free art type (the TigArtType enum uses 0..14; the 4-bit type field leaves
// 15) tags ids whose path we resolve ourselves. The low bits are an index into
// the path table. tig_art_build_path routes non-MISC types to the game's
// resolver, which calls ce_named_art_resolve first (see name_resolve_path).
#define CE_ART_TYPE_NAMED 15
#define CE_NAMED_ART_CAP 64

// Each reserved id is either PATH-backed (a raw .dat art loaded by the normal
// pipeline via name_resolve_path) or COMPOSITE-backed (a ce_sprite vbuffer
// served through tig's composite resolver). The low 28 bits of the id index
// this table; the top 4 bits are CE_ART_TYPE_NAMED.
typedef struct CeNamedArt {
    bool is_composite;
    char path[TIG_MAX_PATH];          // is_composite == false
    char sprite[CE_SPRITE_NAME_MAX];  // is_composite == true
} CeNamedArt;

static CeNamedArt ce_named_art_tbl[CE_NAMED_ART_CAP];
static int ce_named_art_count;
static bool ce_composite_resolver_registered;

static tig_art_id_t ce_named_art_id_for(int index)
{
    return ((tig_art_id_t)CE_ART_TYPE_NAMED << 28) | (tig_art_id_t)index;
}

tig_art_id_t ce_named_art(const char* art_path)
{
    int i;
    if (art_path == NULL) {
        return TIG_ART_ID_INVALID;
    }
    for (i = 0; i < ce_named_art_count; i++) {
        if (!ce_named_art_tbl[i].is_composite
            && strcmp(ce_named_art_tbl[i].path, art_path) == 0) {
            return ce_named_art_id_for(i);
        }
    }
    if (ce_named_art_count >= CE_NAMED_ART_CAP) {
        return TIG_ART_ID_INVALID;
    }
    i = ce_named_art_count++;
    ce_named_art_tbl[i].is_composite = false;
    strncpy(ce_named_art_tbl[i].path, art_path, TIG_MAX_PATH - 1);
    ce_named_art_tbl[i].path[TIG_MAX_PATH - 1] = '\0';
    return ce_named_art_id_for(i);
}

bool ce_named_art_resolve(tig_art_id_t aid, char* path, size_t maxlen)
{
    int index;
    if ((aid >> 28) != CE_ART_TYPE_NAMED) {
        return false;
    }
    index = (int)(aid & 0x0FFFFFFF);
    if (index < 0 || index >= ce_named_art_count) {
        return false;
    }
    if (ce_named_art_tbl[index].is_composite) {
        return false; // served as a composite video buffer, not a path
    }
    snprintf(path, maxlen, "%s", ce_named_art_tbl[index].path);
    return true;
}

bool ce_sprite_is_composite_art(tig_art_id_t aid)
{
    int index;
    if ((aid >> 28) != CE_ART_TYPE_NAMED) {
        return false;
    }
    index = (int)(aid & 0x0FFFFFFF);
    if (index < 0 || index >= ce_named_art_count) {
        return false;
    }
    return ce_named_art_tbl[index].is_composite;
}

// tig composite resolver: reserved id -> the named sprite's video buffer.
static TigVideoBuffer* ce_sprite_composite_resolve(tig_art_id_t aid)
{
    int index;
    if ((aid >> 28) != CE_ART_TYPE_NAMED) {
        return NULL;
    }
    index = (int)(aid & 0x0FFFFFFF);
    if (index < 0 || index >= ce_named_art_count
        || !ce_named_art_tbl[index].is_composite) {
        return NULL;
    }
    return ce_sprite_vbuffer(ce_named_art_tbl[index].sprite);
}

tig_art_id_t ce_sprite_art_id(const char* name)
{
    int i;
    if (name == NULL || !ce_sprite_exists(name)) {
        return TIG_ART_ID_INVALID;
    }
    if (!ce_composite_resolver_registered) {
        tig_art_set_composite_resolver(ce_sprite_composite_resolve);
        ce_composite_resolver_registered = true;
    }
    for (i = 0; i < ce_named_art_count; i++) {
        if (ce_named_art_tbl[i].is_composite
            && strncmp(ce_named_art_tbl[i].sprite, name, CE_SPRITE_NAME_MAX) == 0) {
            return ce_named_art_id_for(i);
        }
    }
    if (ce_named_art_count >= CE_NAMED_ART_CAP) {
        return TIG_ART_ID_INVALID;
    }
    i = ce_named_art_count++;
    ce_named_art_tbl[i].is_composite = true;
    strncpy(ce_named_art_tbl[i].sprite, name, CE_SPRITE_NAME_MAX - 1);
    ce_named_art_tbl[i].sprite[CE_SPRITE_NAME_MAX - 1] = '\0';
    return ce_named_art_id_for(i);
}
