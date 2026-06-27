#ifndef WAVE_PALETTE_H
#define WAVE_PALETTE_H

#include <stddef.h>

#include "workspace.h"

typedef struct {
    char query[256];
    int query_len;
    int sel;
    int *filtered;
    int filtered_n, filtered_cap;
} FilePalette;

void palette_init(FilePalette *p);
void palette_free(FilePalette *p);
void palette_clear(FilePalette *p);
void palette_refilter(FilePalette *p, Workspace *ws);
void palette_set_query(FilePalette *p, Workspace *ws, const char *query);
void palette_insert_text(FilePalette *p, Workspace *ws, const char *text);
void palette_backspace(FilePalette *p, Workspace *ws);
void palette_move(FilePalette *p, int delta);
const WsEntry *palette_selected(FilePalette *p, Workspace *ws);

#endif
