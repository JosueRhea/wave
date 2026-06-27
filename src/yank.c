#include "yank.h"

#include <stdlib.h>
#include <string.h>

void yank_free(YankRegister *y) {
    if (!y) return;
    free(y->text);
    y->text = NULL;
    y->len = 0;
    y->line_wise = 0;
}

int yank_set(YankRegister *y, const char *text, size_t len, int line_wise) {
    if (!y) return -1;
    if (len && !text) return -1;
    char *copy = malloc(len + 1);
    if (!copy) return -1;
    if (len && text) memcpy(copy, text, len);
    copy[len] = '\0';
    free(y->text);
    y->text = copy;
    y->len = len;
    y->line_wise = line_wise != 0;
    return 0;
}

int yank_from_range(YankRegister *y, const PieceTable *pt, size_t a, size_t b,
                    int line_wise) {
    if (!y || !pt || b <= a) return 0;
    size_t len = b - a;
    char *tmp = malloc(len);
    if (!tmp) return 0;
    pt_read(pt, a, len, tmp);
    int ok = yank_set(y, tmp, len, line_wise) == 0;
    free(tmp);
    return ok;
}

int yank_empty(const YankRegister *y) {
    return !y || !y->text;
}
