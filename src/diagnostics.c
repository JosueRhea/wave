#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *diagnostic_severity_name(int severity) {
    switch (severity) {
    case 1: return "Error";
    case 2: return "Warning";
    case 3: return "Info";
    case 4: return "Hint";
    default: return "Diagnostic";
    }
}

int lsp_diag_covers(const LspDiag *d, int row, int col) {
    if (!d) return 0;
    if (row < d->start_line || row > d->end_line) return 0;
    if (row == d->start_line && col < d->start_col) return 0;
    if (row == d->end_line && col > d->end_col) return 0;
    return 1;
}

void diagnostic_from_lsp(Diagnostic *out, const LspDiag *in) {
    if (!out || !in) return;
    out->start_row = (size_t)in->start_line;
    out->start_col = (size_t)in->start_col;
    out->end_row = (size_t)in->end_line;
    out->end_col = (size_t)in->end_col;
    out->start_byte = out->end_byte = 0;
    out->is_missing = 0;
    out->message = "diagnostic";
}

size_t diagnostics_apply_lsp(Diagnostic *out, size_t current_count, size_t cap,
                             const LspDiag *lsp, size_t lsp_count,
                             int published) {
    if (!published) return current_count;
    if (!out || !lsp || cap == 0) return 0;
    if (lsp_count > cap) lsp_count = cap;
    for (size_t i = 0; i < lsp_count; i++)
        diagnostic_from_lsp(&out[i], &lsp[i]);
    return lsp_count;
}

static void append_info(char *out, size_t cap, size_t *off, const char *fmt, ...) {
    if (!out || cap == 0 || !off || *off >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t wrote = (size_t)n;
    if (wrote >= cap - *off) *off = cap - 1;
    else *off += wrote;
}

int diagnostics_cursor_info(Editor *e, const LspDiag *lsp, size_t lsp_count,
                            char *out, size_t cap,
                            DiagnosticCursorInfo *info) {
    if (out && cap > 0) out[0] = '\0';
    if (info) *info = (DiagnosticCursorInfo){0};
    if (!e || !e->buf) return 0;

    size_t row, col;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
    if (info) {
        info->row = row;
        info->col = col;
    }

    size_t off = 0;
    int have_diag = 0;
    if (lsp) {
        for (size_t i = 0; i < lsp_count; i++) {
            if (!lsp_diag_covers(&lsp[i], (int)row, (int)col)) continue;
            append_info(out, cap, &off, "%s%s: %s",
                        off ? "\n\n" : "",
                        diagnostic_severity_name(lsp[i].severity),
                        lsp[i].message);
            have_diag = 1;
        }
    }

    if (!have_diag && e->hl) {
        Diagnostic d[256];
        size_t nd = hl_diagnostics(e->hl, d, 256);
        for (size_t i = 0; i < nd; i++) {
            if (e->cursor < d[i].start_byte || e->cursor >= d[i].end_byte)
                continue;
            append_info(out, cap, &off, "%s%s @ Ln %zu, Col %zu",
                        off ? "\n\n" : "", d[i].message,
                        d[i].start_row + 1, d[i].start_col + 1);
            have_diag = 1;
        }
    }

    if (info) info->have_diag = have_diag;
    if (!have_diag && e->hl) {
        char type[128];
        size_t s, en;
        if (hl_node_at(e->hl, e->cursor, type, sizeof type, &s, &en)) {
            snprintf(out, cap, "node: %s  [%zu bytes]", type, en - s);
        }
    }
    return 1;
}
