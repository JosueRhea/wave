#ifndef WAVE_LSP_MANAGER_H
#define WAVE_LSP_MANAGER_H

#include <stddef.h>

#include "editor.h"
#include "lsp.h"

enum {
    LSP_MANAGER_EVENT_NONE = 0,
    LSP_MANAGER_EVENT_DEFINITION = 1 << 0,
    LSP_MANAGER_EVENT_HOVER = 1 << 1,
};

typedef struct {
    char key[16];
    Lsp *l;
} LspSlot;

typedef struct {
    LspSlot servers[6];
    int nservers;
    char *root_uri;
    int disabled;
} LspManager;

void lsp_manager_init(LspManager *m, int disabled);
void lsp_manager_set_root_path(LspManager *m, const char *path);
void lsp_manager_shutdown(LspManager *m);

Lsp *lsp_manager_for(LspManager *m, Editor *e);
void lsp_manager_open_editor(LspManager *m, Editor *e);
int lsp_manager_push_change(LspManager *m, Editor *e);
size_t lsp_manager_diagnostics(LspManager *m, Editor *e, LspDiag *out,
                               size_t max, int *published);
int lsp_manager_request_hover(LspManager *m, Editor *e, int row, int col);
int lsp_manager_request_definition(LspManager *m, Editor *e, int row, int col);
int lsp_manager_poll(LspManager *m, LspLocation *definition,
                     char *hover, size_t hover_cap);
int lsp_manager_active(const LspManager *m);

#endif
