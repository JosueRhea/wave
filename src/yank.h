#ifndef WAVE_YANK_H
#define WAVE_YANK_H

#include <stddef.h>

#include "piece_table.h"

typedef struct {
    char *text;
    size_t len;
    int line_wise;
} YankRegister;

void yank_free(YankRegister *y);
int yank_set(YankRegister *y, const char *text, size_t len, int line_wise);
int yank_from_range(YankRegister *y, const PieceTable *pt, size_t a, size_t b,
                    int line_wise);
int yank_empty(const YankRegister *y);

#endif
