/* test_diagnostics.c — pure diagnostic helpers. */
#include "test.h"
#include "diagnostics.h"

int main(void) {
    CHECK_STR(diagnostic_severity_name(1), "Error");
    CHECK_STR(diagnostic_severity_name(2), "Warning");
    CHECK_STR(diagnostic_severity_name(3), "Info");
    CHECK_STR(diagnostic_severity_name(4), "Hint");
    CHECK_STR(diagnostic_severity_name(99), "Diagnostic");

    LspDiag d = {.start_line = 2, .start_col = 4, .end_line = 4, .end_col = 8,
                 .severity = 1};
    CHECK(!lsp_diag_covers(&d, 1, 99));
    CHECK(!lsp_diag_covers(&d, 2, 3));
    CHECK(lsp_diag_covers(&d, 2, 4));
    CHECK(lsp_diag_covers(&d, 3, 0));
    CHECK(lsp_diag_covers(&d, 4, 8));
    CHECK(!lsp_diag_covers(&d, 4, 9));
    CHECK(!lsp_diag_covers(NULL, 3, 0));

    Diagnostic out = {0};
    diagnostic_from_lsp(&out, &d);
    CHECK_EQ(out.start_row, 2);
    CHECK_EQ(out.start_col, 4);
    CHECK_EQ(out.end_row, 4);
    CHECK_EQ(out.end_col, 8);
    CHECK_EQ(out.start_byte, 0);
    CHECK_EQ(out.end_byte, 0);
    CHECK_EQ(out.is_missing, 0);
    CHECK_STR(out.message, "diagnostic");

    Diagnostic existing[2] = {
        {.start_row = 9, .start_col = 1, .message = "syntax"},
        {.start_row = 10, .start_col = 2, .message = "syntax2"},
    };
    LspDiag lsp[2] = {
        {.start_line = 1, .start_col = 2, .end_line = 1, .end_col = 5},
        {.start_line = 3, .start_col = 4, .end_line = 3, .end_col = 8},
    };
    CHECK_EQ(diagnostics_apply_lsp(existing, 2, 2, lsp, 2, 0), 2);
    CHECK_EQ(existing[0].start_row, 9);
    CHECK_EQ(diagnostics_apply_lsp(existing, 2, 1, lsp, 2, 1), 1);
    CHECK_EQ(existing[0].start_row, 1);
    CHECK_EQ(existing[0].start_col, 2);
    CHECK_STR(existing[0].message, "diagnostic");

    Editor e;
    editor_init(&e);
    e.buf = buffer_new();
    ed_insert(&e, "alpha\nbeta\n", 11);
    e.cursor = 7; /* inside beta, row 1 col 1 */
    LspDiag cursor_diag = {.start_line = 1, .start_col = 0,
                           .end_line = 1, .end_col = 4,
                           .severity = 2};
    snprintf(cursor_diag.message, sizeof cursor_diag.message, "careful");
    char info_text[256];
    DiagnosticCursorInfo info;
    CHECK(diagnostics_cursor_info(&e, &cursor_diag, 1, info_text,
                                  sizeof info_text, &info));
    CHECK_EQ(info.row, 1);
    CHECK_EQ(info.col, 1);
    CHECK_EQ(info.have_diag, 1);
    CHECK_STR(info_text, "Warning: careful");

    cursor_diag.start_line = 2;
    CHECK(diagnostics_cursor_info(&e, &cursor_diag, 1, info_text,
                                  sizeof info_text, &info));
    CHECK_EQ(info.have_diag, 0);
    CHECK_STR(info_text, "");
    editor_close(&e);

    TEST_REPORT();
}
