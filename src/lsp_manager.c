#include "lsp_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "langs.h"
#include "runtime.h"

void lsp_manager_init(LspManager *m, int disabled) {
    memset(m, 0, sizeof *m);
    m->disabled = disabled;
}

void lsp_manager_set_root_path(LspManager *m, const char *path) {
    if (!m) return;
    free(m->root_uri);
    m->root_uri = path ? path_to_uri(path) : NULL;
}

void lsp_manager_shutdown(LspManager *m) {
    if (!m) return;
    for (int i = 0; i < m->nservers; i++) lsp_stop(m->servers[i].l);
    free(m->root_uri);
    lsp_manager_init(m, m->disabled);
}

Lsp *lsp_manager_for(LspManager *m, Editor *e) {
    if (!m || m->disabled || !e || !e->path) return NULL;
    const Language *L = lang_detect(e->path);
    if (!L) return NULL;
    const char *argv[5];
    char rt[4096], cli[4096];
    const char *key = lsp_server_argv(L->name, argv, rt, sizeof rt, cli, sizeof cli);
    for (int i = 0; i < m->nservers; i++)
        if (!strcmp(m->servers[i].key, key)) return m->servers[i].l;
    if (m->nservers >= (int)(sizeof m->servers / sizeof m->servers[0])) return NULL;
    Lsp *l = lsp_start((const char *const *)argv, m->root_uri);
    snprintf(m->servers[m->nservers].key, sizeof m->servers[m->nservers].key, "%s", key);
    m->servers[m->nservers].l = l;
    m->nservers++;
    return l;
}

void lsp_manager_open_editor(LspManager *m, Editor *e) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l) return;
    const Language *L = lang_detect(e->path);
    char *uri = path_to_uri(e->path);
    char *txt = editor_text(e);
    lsp_did_open(l, uri, lsp_language_id(L->name), txt);
    e->lsp_open = 1;
    e->version = 1;
    e->lsp_dirty = 0;
    free(uri);
    free(txt);
}

int lsp_manager_push_change(LspManager *m, Editor *e) {
    if (!e || !e->path || !e->lsp_open || !e->lsp_dirty) return 0;
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l)) return 0;
    char *uri = path_to_uri(e->path);
    char *txt = editor_text(e);
    lsp_did_change(l, uri, ++e->version, txt);
    e->lsp_dirty = 0;
    free(uri);
    free(txt);
    return 1;
}

size_t lsp_manager_diagnostics(LspManager *m, Editor *e, LspDiag *out,
                               size_t max, int *published) {
    if (published) *published = 0;
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !e || !e->path) return 0;
    char *uri = path_to_uri(e->path);
    size_t n = lsp_diagnostics(l, uri, out, max, published);
    free(uri);
    return n;
}

size_t lsp_manager_editor_diagnostics(LspManager *m, Editor *e,
                                      Diagnostic *out, size_t max) {
    LspDiag lsp[256];
    int published = 0;
    size_t nl = 0;
    if (max > 256) max = 256;
    if (e && e->path)
        nl = lsp_manager_diagnostics(m, e, lsp, max, &published);
    return diagnostics_for_editor(e, out, max, lsp, nl, published);
}

LspManagerHoverInfo lsp_manager_hover_info(LspManager *m, Editor *e,
                                           char *base, size_t base_cap) {
    LspManagerHoverInfo info = {0};
    if (!editor_has_buffer(e)) return info;

    LspDiag lsp[256];
    size_t nl = 0;
    if (editor_has_path(e)) {
        int published = 0;
        nl = lsp_manager_diagnostics(m, e, lsp, 256, &published);
    }

    DiagnosticCursorInfo cursor = {0};
    diagnostics_cursor_info(e, editor_has_path(e) ? lsp : NULL, nl,
                            base, base_cap, &cursor);
    info.ok = 1;
    info.row = cursor.row;
    info.col = cursor.col;
    info.loading = lsp_manager_request_hover(m, e, (int)cursor.row, (int)cursor.col);
    return info;
}

int lsp_manager_request_hover(LspManager *m, Editor *e, int row, int col) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !e || !e->path) return 0;
    char *uri = path_to_uri(e->path);
    lsp_hover(l, uri, row, col);
    free(uri);
    return 1;
}

int lsp_manager_request_definition(LspManager *m, Editor *e, int row, int col) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !e || !e->path) return 0;
    char *uri = path_to_uri(e->path);
    lsp_definition(l, uri, row, col);
    free(uri);
    return 1;
}

int lsp_manager_request_definition_at_cursor(LspManager *m, Editor *e,
                                             char *message, size_t message_cap) {
    if (!e || !e->buf || !e->path) return 0;
    size_t row, col;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
    if (!lsp_manager_request_definition(m, e, (int)row, (int)col)) return 0;
    if (message && message_cap > 0)
        snprintf(message, message_cap, "resolving definition...");
    return 1;
}

int lsp_manager_poll(LspManager *m, LspLocation *definition,
                     char *hover, size_t hover_cap) {
    if (!m) return LSP_MANAGER_EVENT_NONE;
    int events = LSP_MANAGER_EVENT_NONE;
    for (int i = 0; i < m->nservers; i++) {
        Lsp *l = m->servers[i].l;
        if (!l) continue;
        lsp_poll(l);
        if (definition && lsp_take_definition(l, definition))
            events |= LSP_MANAGER_EVENT_DEFINITION;
        if (hover && hover_cap && lsp_take_hover(l, hover, hover_cap))
            events |= LSP_MANAGER_EVENT_HOVER;
    }
    return events;
}

int lsp_manager_update(LspManager *m, Editor *active, LspLocation *definition,
                       char *hover, size_t hover_cap) {
    int events = lsp_manager_poll(m, definition, hover, hover_cap);
    lsp_manager_push_change(m, active);
    return events;
}

LspManagerUpdate lsp_manager_update_ui(LspManager *m, Editor *active) {
    LspManagerUpdate update = {0};
    int events = lsp_manager_update(m, active, &update.definition,
                                    update.hover, sizeof update.hover);
    update.has_definition = (events & LSP_MANAGER_EVENT_DEFINITION) != 0;
    update.has_hover = (events & LSP_MANAGER_EVENT_HOVER) != 0;
    return update;
}

int lsp_manager_active(const LspManager *m) {
    return m && m->nservers > 0;
}
