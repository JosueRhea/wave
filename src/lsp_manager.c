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

int lsp_manager_active(const LspManager *m) {
    return m && m->nservers > 0;
}
