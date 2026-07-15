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

int lsp_manager_request_completion(LspManager *m, Editor *e, int row, int col) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !e || !e->path) return 0;
    lsp_manager_push_change(m, e);
    char *uri = path_to_uri(e->path);
    lsp_completion(l, uri, row, col);
    free(uri);
    return 1;
}

int lsp_manager_request_triggered_completion(LspManager *m, Editor *e, int row,
                                             int col, char trigger_character) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !e || !e->path) return 0;
    lsp_manager_push_change(m, e);
    char *uri = path_to_uri(e->path);
    lsp_completion_triggered(l, uri, row, col, trigger_character);
    free(uri);
    return 1;
}

int lsp_manager_resolve_completion(LspManager *m, Editor *e,
                                   const LspCompletionItem *item) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !item || item->resolve_id <= 0) return 0;
    lsp_completion_resolve(l, item);
    return 1;
}

int lsp_manager_take_completions(LspManager *m, Editor *e, LspCompletionItem *out,
                                 size_t max, size_t *n) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l) return 0;
    return lsp_take_completions(l, out, max, n);
}

int lsp_manager_take_resolved_completion(LspManager *m, Editor *e,
                                         LspCompletionItem *out) {
    Lsp *l = lsp_manager_for(m, e);
    return l ? lsp_take_resolved_completion(l, out) : 0;
}

int lsp_manager_progress(LspManager *m, Editor *e, LspProgress *out) {
    Lsp *l = lsp_manager_for(m, e);
    return l ? lsp_progress(l, out) : 0;
}

int lsp_manager_request_signature_help(LspManager *m, Editor *e, int row, int col,
                                       char trigger_character, int retrigger) {
    Lsp *l = lsp_manager_for(m, e);
    if (!l || !lsp_ready(l) || !e || !e->path) return 0;
    lsp_manager_push_change(m, e);
    char *uri = path_to_uri(e->path);
    lsp_signature_help(l, uri, row, col, trigger_character, retrigger);
    free(uri);
    return 1;
}

static size_t lsp_edit_offset(const PieceTable *pt, int line, int col) {
    size_t lines = pt_line_count(pt);
    size_t row = line < 0 ? 0 : (size_t)line;
    if (row >= lines) return pt_length(pt);
    size_t start = pt_line_start(pt, row);
    size_t end = row + 1 < lines ? pt_line_start(pt, row + 1) : pt_length(pt);
    size_t off = start + (size_t)(col > 0 ? col : 0);
    return off < end ? off : end;
}

int lsp_manager_apply_completion(Editor *e, size_t primary_start,
                                 size_t primary_end, const char *primary_text,
                                 const LspCompletionItem *item) {
    if (!e || !e->buf || !primary_text || primary_end < primary_start) return 0;
    typedef struct { size_t start, end; const char *text; int primary; } PendingEdit;
    PendingEdit edits[1 + LSP_MAX_ADDITIONAL_EDITS];
    int n = 1;
    edits[0] = (PendingEdit){primary_start, primary_end, primary_text, 1};

    const PieceTable *pt = buffer_pt(e->buf);
    int nadditional = item ? item->nadditional_edits : 0;
    if (nadditional > LSP_MAX_ADDITIONAL_EDITS)
        nadditional = LSP_MAX_ADDITIONAL_EDITS;
    for (int i = 0; i < nadditional; i++) {
        const LspTextEdit *src = &item->additional_edits[i];
        edits[n].start = lsp_edit_offset(pt, src->start_line, src->start_col);
        edits[n].end = lsp_edit_offset(pt, src->end_line, src->end_col);
        edits[n].text = src->new_text;
        edits[n].primary = 0;
        n++;
    }

    size_t final_cursor = primary_start + strlen(primary_text);
    for (int i = 1; i < n; i++) {
        if (edits[i].end <= primary_start) {
            long delta = (long)strlen(edits[i].text) -
                         (long)(edits[i].end - edits[i].start);
            long moved = (long)final_cursor + delta;
            final_cursor = (size_t)(moved < 0 ? 0 : moved);
        }
    }
    for (int i = 1; i < n; i++) {
        PendingEdit edit = edits[i];
        int j = i - 1;
        while (j >= 0 && edits[j].start < edit.start) {
            edits[j + 1] = edits[j];
            j--;
        }
        edits[j + 1] = edit;
    }

    e->group_open = 0;
    for (int i = 0; i < n; i++) {
        if (edits[i].end > edits[i].start)
            ed_delete_range(e, edits[i].start, edits[i].end);
        if (edits[i].text[0])
            ed_insert_at(e, edits[i].start, edits[i].text, strlen(edits[i].text));
    }
    e->cursor = final_cursor;
    e->anchor = final_cursor;
    e->group_open = 0;
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
    if (m) {
        for (int i = 0; i < m->nservers; i++) {
            Lsp *l = m->servers[i].l;
            if (l && lsp_take_signature_help(l, &update.signature)) {
                update.has_signature = 1;
                break;
            }
        }
    }
    return update;
}

LspManagerUiPlan lsp_manager_ui_plan(LspManagerUpdate update) {
    LspManagerUiPlan plan = {0};
    if (update.has_definition && update.definition.path[0]) {
        plan.open_definition = 1;
        plan.definition = update.definition;
    }
    if (update.has_hover) {
        plan.show_hover = 1;
        snprintf(plan.hover, sizeof plan.hover, "%s", update.hover);
    }
    if (update.has_signature) {
        plan.show_signature = 1;
        if (update.signature.label[0]) {
            if (update.signature.signature_count > 1)
                snprintf(plan.signature, sizeof plan.signature, "%s  [%d/%d]",
                         update.signature.label, update.signature.active_signature + 1,
                         update.signature.signature_count);
            else
                snprintf(plan.signature, sizeof plan.signature, "%s",
                         update.signature.label);
        }
    }
    return plan;
}

int lsp_manager_active(const LspManager *m) {
    return m && m->nservers > 0;
}
