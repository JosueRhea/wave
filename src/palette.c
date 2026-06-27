#include "palette.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int idx, score; } PalHit;

static int fuzzy_score(const char *q, const char *s) {
    if (!*q) return 0;
    const char *base = s;
    for (const char *c = s; *c; c++)
        if (*c == '/') base = c + 1;

    int score = 0, run = 0;
    char prev = '/';
    const char *sp = s;
    for (; *q; q++) {
        char qc = (char)tolower((unsigned char)*q);
        int matched = 0;
        for (; *sp; sp++) {
            char sc = (char)tolower((unsigned char)*sp);
            if (sc == qc) {
                int boundary = prev == '/' || prev == '_' || prev == '-' ||
                               prev == '.' || prev == ' ' ||
                               (islower((unsigned char)prev) &&
                                isupper((unsigned char)*sp));
                score += 16;
                if (boundary) score += 30;
                if (run) score += 15;
                if (sp >= base) score += 10;
                if (sp == s) score += 20;
                run = 1;
                prev = *sp;
                sp++;
                matched = 1;
                break;
            }
            score -= 1;
            run = 0;
            prev = *sp;
        }
        if (!matched) return INT_MIN;
    }
    return score;
}

static int pal_cmp(const void *a, const void *b) {
    const PalHit *x = a, *y = b;
    if (x->score != y->score) return x->score < y->score ? 1 : -1;
    return x->idx - y->idx;
}

void palette_init(FilePalette *p) {
    memset(p, 0, sizeof *p);
}

void palette_free(FilePalette *p) {
    if (!p) return;
    free(p->filtered);
    palette_init(p);
}

void palette_clear(FilePalette *p) {
    if (!p) return;
    p->query[0] = '\0';
    p->query_len = 0;
    p->sel = 0;
    p->filtered_n = 0;
}

void palette_refilter(FilePalette *p, Workspace *ws) {
    if (!p) return;
    p->filtered_n = 0;
    p->query[p->query_len] = '\0';
    if (!ws) return;

    static PalHit *hits = NULL;
    static size_t hits_cap = 0;
    size_t hn = 0;
    size_t n = ws_count(ws);
    for (size_t i = 0; i < n; i++) {
        const WsEntry *e = ws_entry(ws, i);
        if (e->is_dir) continue;
        int sc = 0;
        if (p->query_len > 0) {
            sc = fuzzy_score(p->query, e->rel);
            if (sc == INT_MIN) continue;
        }
        if (hn == hits_cap) {
            hits_cap = hits_cap ? hits_cap * 2 : 256;
            hits = realloc(hits, hits_cap * sizeof *hits);
        }
        hits[hn].idx = (int)i;
        hits[hn].score = sc;
        hn++;
    }
    if (p->query_len > 0) qsort(hits, hn, sizeof *hits, pal_cmp);

    for (size_t i = 0; i < hn; i++) {
        if (p->filtered_n == p->filtered_cap) {
            p->filtered_cap = p->filtered_cap ? p->filtered_cap * 2 : 256;
            p->filtered = realloc(p->filtered, (size_t)p->filtered_cap * sizeof *p->filtered);
        }
        p->filtered[p->filtered_n++] = hits[i].idx;
    }
    p->sel = 0;
}

void palette_set_query(FilePalette *p, Workspace *ws, const char *query) {
    if (!p) return;
    snprintf(p->query, sizeof p->query, "%s", query ? query : "");
    p->query_len = (int)strlen(p->query);
    palette_refilter(p, ws);
}

void palette_insert_text(FilePalette *p, Workspace *ws, const char *text) {
    if (!p || !text) return;
    while (*text && p->query_len < (int)sizeof p->query - 1) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32 && c < 127)
            p->query[p->query_len++] = (char)c;
    }
    p->query[p->query_len] = '\0';
    palette_refilter(p, ws);
}

void palette_backspace(FilePalette *p, Workspace *ws) {
    if (!p || p->query_len <= 0) return;
    p->query[--p->query_len] = '\0';
    palette_refilter(p, ws);
}

void palette_move(FilePalette *p, int delta) {
    if (!p) return;
    p->sel += delta;
    if (p->sel < 0) p->sel = 0;
    if (p->sel >= p->filtered_n) p->sel = p->filtered_n ? p->filtered_n - 1 : 0;
}

const WsEntry *palette_selected(FilePalette *p, Workspace *ws) {
    if (!p || !ws || p->sel < 0 || p->sel >= p->filtered_n) return NULL;
    return ws_entry(ws, p->filtered[p->sel]);
}
