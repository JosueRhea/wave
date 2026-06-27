#ifndef WAVE_DIAGNOSTICS_H
#define WAVE_DIAGNOSTICS_H

#include "highlight.h"
#include "lsp.h"
#include "editor.h"

typedef struct {
    size_t row;
    size_t col;
    int have_diag;
} DiagnosticCursorInfo;

const char *diagnostic_severity_name(int severity);
int lsp_diag_covers(const LspDiag *d, int row, int col);
void diagnostic_from_lsp(Diagnostic *out, const LspDiag *in);
size_t diagnostics_apply_lsp(Diagnostic *out, size_t current_count, size_t cap,
                             const LspDiag *lsp, size_t lsp_count,
                             int published);
int diagnostics_cursor_info(Editor *e, const LspDiag *lsp, size_t lsp_count,
                            char *out, size_t cap,
                            DiagnosticCursorInfo *info);

#endif
