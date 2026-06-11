#ifndef ARCANUM_GAME_CONTEXT_H_
#define ARCANUM_GAME_CONTEXT_H_

#include <tig/tig.h>

#include "net_compat.h"

#define GAME 0
#define EDITOR 1

typedef enum ViewType {
    VIEW_TYPE_ISOMETRIC,
    VIEW_TYPE_TOP_DOWN,
} ViewType;

typedef void(IsoInvalidateRectFunc)(TigRect* rect);
typedef bool(IsoRedrawFunc)(void);

typedef struct GameInitInfo {
    bool editor;
    tig_window_handle_t iso_window_handle;
    IsoInvalidateRectFunc* invalidate_rect_func;
    IsoRedrawFunc* draw_func;
} GameInitInfo;

typedef struct GameResizeInfo {
    tig_window_handle_t window_handle;
    TigRect window_rect;
    TigRect content_rect;
} GameResizeInfo;

typedef struct GameLoadInfo {
    int version;
    TigFile* stream;
} GameLoadInfo;

typedef struct ViewOptions {
    int type;
    int zoom;
} ViewOptions;

typedef struct LocRect LocRect;

typedef struct SectorListNode {
    /* 0000 */ int64_t sec;
    /* 0008 */ int64_t loc;
    /* 0010 */ int width;
    /* 0014 */ int height;
    /* 0018 */ struct SectorListNode* next;
} SectorListNode;

// CE: the original game capped a draw at 3x3 sectors because its fixed-zoom
// camera never showed more. The zoom-out / high-res viewport can span more, so
// the rect (and every per-column array sized to it) is enlarged. Anything beyond
// this many sectors per axis would need an ~8K display at 0.5x zoom.
#define SECTOR_RECT_DIM 10

typedef struct SectorRectRow {
    /* 0000 */ int num_cols;
    /* 0008 */ int64_t origin_locs[SECTOR_RECT_DIM];
    /* ____ */ int64_t sector_ids[SECTOR_RECT_DIM];
    /* ____ */ int tile_ids[SECTOR_RECT_DIM];
    /* ____ */ int num_hor_tiles[SECTOR_RECT_DIM];
    /* ____ */ int num_vert_tiles;
} SectorRectRow;

typedef struct SectorRect {
    /* 0000 */ int num_rows;
    /* 0008 */ SectorRectRow rows[SECTOR_RECT_DIM];
} SectorRect;

typedef struct GameDrawInfo {
    TigRect* screen_rect;
    LocRect* loc_rect;
    SectorRect* sector_rect;
    SectorListNode* sectors;
    TigRectListNode** rects;
} GameDrawInfo;

typedef struct MapNewInfo {
    /* 0000 */ const char* base_path;
    /* 0004 */ const char* save_path;
    /* 0008 */ int base_terrain_type;
    /* 0010 */ int64_t width;
    /* 0018 */ int64_t height;
} MapNewInfo;

#endif /* ARCANUM_GAME_CONTEXT_H_ */
