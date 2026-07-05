#ifndef WAVE_OVERLAY_H
#define WAVE_OVERLAY_H

#include "palette.h"
#include "project_search.h"
#include "workspace.h"

typedef enum {
    OVERLAY_NONE,
    OVERLAY_PALETTE,
    OVERLAY_SEARCH
} OverlayKind;

typedef struct {
    OverlayKind active;
    FilePalette palette;
    ProjectSearch search;
} OverlayState;

typedef struct {
    int has_target;
    char *path;
    int has_location;
    int line;
    int col;
} OverlayAcceptTarget;

typedef enum {
    OVERLAY_KEY_NONE,
    OVERLAY_KEY_ESCAPE,
    OVERLAY_KEY_ACCEPT,
    OVERLAY_KEY_UP,
    OVERLAY_KEY_DOWN,
    OVERLAY_KEY_BACKSPACE
} OverlayKey;

typedef enum {
    OVERLAY_KEY_RESULT_NONE,
    OVERLAY_KEY_RESULT_HANDLED,
    OVERLAY_KEY_RESULT_ACCEPT
} OverlayKeyResult;

typedef enum {
    OVERLAY_ACCEPT_NONE,
    OVERLAY_ACCEPT_PALETTE,
    OVERLAY_ACCEPT_SEARCH
} OverlayAcceptAction;

void overlay_init(OverlayState *overlay);
void overlay_free(OverlayState *overlay);
OverlayKind overlay_active(const OverlayState *overlay);
void overlay_close(OverlayState *overlay);

int overlay_open_palette(OverlayState *overlay, Workspace *ws);
int overlay_open_search(OverlayState *overlay);
void overlay_refilter_palette(OverlayState *overlay, Workspace *ws);

const WsEntry *overlay_palette_selected(OverlayState *overlay, Workspace *ws);
const SearchHit *overlay_search_selected(OverlayState *overlay);
OverlayAcceptTarget overlay_accept_target(OverlayState *overlay, Workspace *ws);
void overlay_accept_target_free(OverlayAcceptTarget *target);
const char *overlay_query(OverlayState *overlay);

void overlay_set_palette_query(OverlayState *overlay, Workspace *ws, const char *query);
void overlay_set_search_query(OverlayState *overlay, const char *root, const char *query);
void overlay_set_search_selection(OverlayState *overlay, int selection);
void overlay_insert_text(OverlayState *overlay, Workspace *ws, const char *root,
                         const char *text);
void overlay_backspace(OverlayState *overlay, Workspace *ws, const char *root);
void overlay_move(OverlayState *overlay, int delta);
OverlayKeyResult overlay_apply_key(OverlayState *overlay, Workspace *ws,
                                   const char *root, OverlayKey key);
OverlayAcceptAction overlay_accept_action(OverlayKind active,
                                          OverlayKeyResult result);
void overlay_poll_search(OverlayState *overlay);
int overlay_settle_search(OverlayState *overlay);
int overlay_search_running(const OverlayState *overlay);

#endif /* WAVE_OVERLAY_H */
