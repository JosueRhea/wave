#include "overlay.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OVERLAY_SEARCH_SETTLE_POLLS 500
#define OVERLAY_SEARCH_SETTLE_SLEEP_US 10000

void overlay_init(OverlayState *overlay) {
    if (!overlay) return;
    memset(overlay, 0, sizeof *overlay);
    palette_init(&overlay->palette);
    project_search_init(&overlay->search);
}

void overlay_free(OverlayState *overlay) {
    if (!overlay) return;
    palette_free(&overlay->palette);
    project_search_free(&overlay->search);
    overlay->active = OVERLAY_NONE;
}

OverlayKind overlay_active(const OverlayState *overlay) {
    return overlay ? overlay->active : OVERLAY_NONE;
}

void overlay_close(OverlayState *overlay) {
    if (overlay) overlay->active = OVERLAY_NONE;
}

int overlay_open_palette(OverlayState *overlay, Workspace *ws) {
    if (!overlay || !ws) return 0;
    overlay->active = OVERLAY_PALETTE;
    palette_clear(&overlay->palette);
    palette_refilter(&overlay->palette, ws);
    return 1;
}

int overlay_open_search(OverlayState *overlay) {
    if (!overlay) return 0;
    overlay->active = OVERLAY_SEARCH;
    project_search_open(&overlay->search);
    return 1;
}

void overlay_refilter_palette(OverlayState *overlay, Workspace *ws) {
    if (!overlay) return;
    palette_refilter(&overlay->palette, ws);
}

const WsEntry *overlay_palette_selected(OverlayState *overlay, Workspace *ws) {
    return overlay ? palette_selected(&overlay->palette, ws) : NULL;
}

const SearchHit *overlay_search_selected(OverlayState *overlay) {
    if (!overlay) return NULL;
    return project_search_hit(&overlay->search, (size_t)overlay->search.sel);
}

OverlayAcceptTarget overlay_accept_target(OverlayState *overlay, Workspace *ws) {
    OverlayAcceptTarget target = {0};
    if (!overlay || !ws) return target;

    if (overlay->active == OVERLAY_PALETTE) {
        const WsEntry *entry = overlay_palette_selected(overlay, ws);
        if (!entry) return target;
        target.path = ws_fullpath(ws, entry->rel);
        target.has_target = target.path != NULL;
        return target;
    }

    if (overlay->active == OVERLAY_SEARCH) {
        const SearchHit *hit = overlay_search_selected(overlay);
        if (!hit) return target;
        target.path = ws_fullpath(ws, hit->path);
        target.has_target = target.path != NULL;
        target.has_location = 1;
        target.line = hit->line;
        target.col = hit->col;
    }
    return target;
}

void overlay_accept_target_free(OverlayAcceptTarget *target) {
    if (!target) return;
    free(target->path);
    memset(target, 0, sizeof *target);
}

const char *overlay_query(OverlayState *overlay) {
    if (!overlay) return "";
    if (overlay->active == OVERLAY_PALETTE) {
        overlay->palette.query[overlay->palette.query_len] = '\0';
        return overlay->palette.query;
    }
    if (overlay->active == OVERLAY_SEARCH) {
        overlay->search.query[overlay->search.query_len] = '\0';
        return overlay->search.query;
    }
    return "";
}

void overlay_set_palette_query(OverlayState *overlay, Workspace *ws, const char *query) {
    if (!overlay) return;
    palette_set_query(&overlay->palette, ws, query);
}

void overlay_set_search_query(OverlayState *overlay, const char *root, const char *query) {
    if (!overlay) return;
    project_search_set_query(&overlay->search, root, query);
}

void overlay_set_search_selection(OverlayState *overlay, int selection) {
    if (!overlay) return;
    overlay->search.sel = selection;
}

void overlay_insert_text(OverlayState *overlay, Workspace *ws, const char *root,
                         const char *text) {
    if (!overlay) return;
    if (overlay->active == OVERLAY_PALETTE)
        palette_insert_text(&overlay->palette, ws, text);
    else if (overlay->active == OVERLAY_SEARCH)
        project_search_insert_text(&overlay->search, root, text);
}

void overlay_backspace(OverlayState *overlay, Workspace *ws, const char *root) {
    if (!overlay) return;
    if (overlay->active == OVERLAY_PALETTE)
        palette_backspace(&overlay->palette, ws);
    else if (overlay->active == OVERLAY_SEARCH)
        project_search_backspace(&overlay->search, root);
}

void overlay_move(OverlayState *overlay, int delta) {
    if (!overlay) return;
    if (overlay->active == OVERLAY_PALETTE)
        palette_move(&overlay->palette, delta);
    else if (overlay->active == OVERLAY_SEARCH)
        project_search_move(&overlay->search, delta);
}

OverlayKeyResult overlay_apply_key(OverlayState *overlay, Workspace *ws,
                                   const char *root, OverlayKey key) {
    if (!overlay || overlay->active == OVERLAY_NONE) return OVERLAY_KEY_RESULT_NONE;
    switch (key) {
    case OVERLAY_KEY_ESCAPE:
        overlay_close(overlay);
        return OVERLAY_KEY_RESULT_HANDLED;
    case OVERLAY_KEY_ACCEPT:
        return OVERLAY_KEY_RESULT_ACCEPT;
    case OVERLAY_KEY_UP:
        overlay_move(overlay, -1);
        return OVERLAY_KEY_RESULT_HANDLED;
    case OVERLAY_KEY_DOWN:
        overlay_move(overlay, +1);
        return OVERLAY_KEY_RESULT_HANDLED;
    case OVERLAY_KEY_BACKSPACE:
        overlay_backspace(overlay, ws, root);
        return OVERLAY_KEY_RESULT_HANDLED;
    case OVERLAY_KEY_NONE:
    default:
        return OVERLAY_KEY_RESULT_NONE;
    }
}

OverlayAcceptAction overlay_accept_action(OverlayKind active,
                                          OverlayKeyResult result) {
    if (result != OVERLAY_KEY_RESULT_ACCEPT) return OVERLAY_ACCEPT_NONE;
    if (active == OVERLAY_PALETTE) return OVERLAY_ACCEPT_PALETTE;
    if (active == OVERLAY_SEARCH) return OVERLAY_ACCEPT_SEARCH;
    return OVERLAY_ACCEPT_NONE;
}

void overlay_poll_search(OverlayState *overlay) {
    if (overlay) project_search_poll(&overlay->search);
}

int overlay_settle_search(OverlayState *overlay) {
    if (!overlay) return 0;
    int polls = 0;
    for (int i = 0; i < OVERLAY_SEARCH_SETTLE_POLLS && overlay_search_running(overlay); i++) {
        overlay_poll_search(overlay);
        polls++;
        if (!overlay_search_running(overlay)) break;
        usleep(OVERLAY_SEARCH_SETTLE_SLEEP_US);
    }
    overlay_poll_search(overlay);
    polls++;
    return polls;
}

int overlay_search_running(const OverlayState *overlay) {
    return overlay && project_search_running(&overlay->search);
}
