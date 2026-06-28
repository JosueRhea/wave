/* test_lsp_manager.c - pure manager intent helpers. */
#include "test.h"
#include "lsp_manager.h"

#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    editor_clear_history(e);
}

int main(void) {
    LspManager m;
    lsp_manager_init(&m, 1);
    char msg[64] = "stale";

    CHECK(!lsp_manager_request_definition_at_cursor(NULL, NULL, msg, sizeof msg));
    CHECK_STR(msg, "stale");

    Editor e;
    editor_init(&e);
    CHECK(!lsp_manager_request_definition_at_cursor(&m, &e, msg, sizeof msg));
    CHECK_STR(msg, "stale");
    char base[256] = "stale";
    LspManagerHoverInfo hover = lsp_manager_hover_info(&m, &e, base, sizeof base);
    CHECK(!hover.ok);
    CHECK(!hover.loading);
    CHECK_STR(base, "stale");
    Diagnostic diags[4];
    CHECK_EQ(lsp_manager_editor_diagnostics(&m, &e, diags, 4), 0);
    CHECK_EQ(lsp_manager_editor_diagnostics(NULL, NULL, diags, 4), 0);
    editor_close(&e);

    fill(&e, "int main(void) { return 0; }\n");
    e.path = dupstr("/tmp/wave_lsp_manager_test.c");
    e.cursor = strlen("int main");
    snprintf(base, sizeof base, "stale");
    hover = lsp_manager_hover_info(&m, &e, base, sizeof base);
    CHECK(hover.ok);
    CHECK(!hover.loading);
    CHECK_EQ(hover.row, 0);
    CHECK_EQ(hover.col, strlen("int main"));
    CHECK_STR(base, "");
    CHECK(!lsp_manager_request_definition_at_cursor(&m, &e, msg, sizeof msg));
    CHECK_STR(msg, "stale");
    CHECK_EQ(lsp_manager_update(&m, &e, NULL, NULL, 0), LSP_MANAGER_EVENT_NONE);
    LspManagerUpdate update = lsp_manager_update_ui(&m, &e);
    CHECK(!update.has_definition);
    CHECK(!update.has_hover);
    CHECK_STR(update.hover, "");
    editor_close(&e);

    CHECK_EQ(lsp_manager_update(NULL, NULL, NULL, NULL, 0), LSP_MANAGER_EVENT_NONE);
    update = lsp_manager_update_ui(NULL, NULL);
    CHECK(!update.has_definition);
    CHECK(!update.has_hover);

    lsp_manager_shutdown(&m);

    TEST_REPORT();
}
