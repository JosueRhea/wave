/* complete.c — see complete.h. */
#include "complete.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static unsigned char pbyte(const PieceTable *pt, size_t i) {
    char c;
    pt_read(pt, i, 1, &c);
    return (unsigned char)c;
}

static int is_ident_byte(unsigned char c) {
    return isalnum(c) || c == '_';
}

void complete_init(CompleteState *c) {
    memset(c, 0, sizeof *c);
}

void complete_close(CompleteState *c) {
    if (!c) return;
    c->active = 0;
    c->loading = 0;
    c->nitems = 0;
    c->nfiltered = 0;
    c->sel = 0;
    c->scroll = 0;
}

int complete_is_active(const CompleteState *c) {
    return c && c->active;
}

size_t complete_prefix_start(const PieceTable *pt, size_t cursor) {
    if (!pt) return cursor;
    size_t pos = cursor;
    while (pos > 0 && is_ident_byte(pbyte(pt, pos - 1))) pos--;
    return pos;
}

unsigned int complete_begin(CompleteState *c, size_t word_start, const char *prefix) {
    if (!c) return 0;
    c->generation++;
    c->active = 1;
    c->loading = 0;
    c->word_start = word_start;
    snprintf(c->prefix, sizeof c->prefix, "%s", prefix ? prefix : "");
    c->nitems = 0;
    c->nfiltered = 0;
    c->sel = 0;
    c->scroll = 0;
    return c->generation;
}

void complete_set_loading(CompleteState *c, unsigned int generation) {
    if (!c || !c->active || generation != c->generation) return;
    c->loading = 1;
}

/* ----- filter + sort ----- */

static int ci_prefix(const char *s, const char *prefix) {
    for (; *prefix; s++, prefix++) {
        if (!*s) return 0;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    }
    return 1;
}

static int ci_subsequence(const char *s, const char *prefix) {
    if (!*prefix) return 1;
    for (; *s && *prefix; s++)
        if (tolower((unsigned char)*s) == tolower((unsigned char)*prefix)) prefix++;
    return !*prefix;
}

/* -1 = excluded, 0 = prefix match (best), 1 = subsequence match (fallback). */
static int tier_of(const CompleteItem *it, const char *prefix) {
    if (!prefix[0]) return 0;
    if (ci_prefix(it->label, prefix)) return 0;
    if (ci_subsequence(it->label, prefix)) return 1;
    return -1;
}

static int item_key_cmp(const CompleteItem *a, const CompleteItem *b) {
    const char *ka = a->sort_text[0] ? a->sort_text : a->label;
    const char *kb = b->sort_text[0] ? b->sort_text : b->label;
    return strcmp(ka, kb);
}

static void refilter(CompleteState *c) {
    int idx[COMPLETE_MAX_ITEMS];
    int tiers[COMPLETE_MAX_ITEMS];
    int n = 0;
    for (int i = 0; i < c->nitems; i++) {
        int t = tier_of(&c->items[i], c->prefix);
        if (t < 0) continue;
        idx[n] = i;
        tiers[n] = t;
        n++;
    }
    /* stable insertion sort by (tier, sort key) — n is capped at
     * COMPLETE_MAX_ITEMS, so O(n^2) is cheap and needs no extra buffer. */
    for (int i = 1; i < n; i++) {
        int ti = tiers[i], vi = idx[i];
        int j = i - 1;
        while (j >= 0 && (tiers[j] > ti ||
               (tiers[j] == ti && item_key_cmp(&c->items[idx[j]], &c->items[vi]) > 0))) {
            tiers[j + 1] = tiers[j];
            idx[j + 1] = idx[j];
            j--;
        }
        tiers[j + 1] = ti;
        idx[j + 1] = vi;
    }
    memcpy(c->filtered, idx, (size_t)n * sizeof *idx);
    c->nfiltered = n;
    if (c->sel >= n) c->sel = n > 0 ? n - 1 : 0;
    if (c->sel < 0) c->sel = 0;
    if (c->scroll > c->sel) c->scroll = c->sel;
}

int complete_set_items(CompleteState *c, unsigned int generation,
                       const CompleteItem *items, int n) {
    if (!c || !c->active || generation != c->generation) return 0;
    if (n < 0) n = 0;
    if (n > COMPLETE_MAX_ITEMS) n = COMPLETE_MAX_ITEMS;
    if (n > 0) memcpy(c->items, items, (size_t)n * sizeof *items);
    c->nitems = n;
    c->loading = 0;
    c->sel = 0;
    c->scroll = 0;
    refilter(c);
    return 1;
}

void complete_set_prefix(CompleteState *c, const char *prefix) {
    if (!c || !c->active) return;
    snprintf(c->prefix, sizeof c->prefix, "%s", prefix ? prefix : "");
    refilter(c);
}

void complete_move(CompleteState *c, int delta) {
    if (!c || !c->active || c->nfiltered == 0) return;
    c->sel = (c->sel + delta) % c->nfiltered;
    if (c->sel < 0) c->sel += c->nfiltered;
}

void complete_set_view(CompleteState *c, int visible_rows) {
    if (!c) return;
    if (visible_rows < 1) visible_rows = 1;
    if (c->sel < c->scroll) c->scroll = c->sel;
    if (c->sel >= c->scroll + visible_rows) c->scroll = c->sel - visible_rows + 1;
    int maxscroll = c->nfiltered - visible_rows;
    if (maxscroll < 0) maxscroll = 0;
    if (c->scroll > maxscroll) c->scroll = maxscroll;
    if (c->scroll < 0) c->scroll = 0;
}

int complete_accept(const CompleteState *c, size_t cursor, CompleteEdit *out) {
    if (!c || !out || !c->active || c->nfiltered == 0) return 0;
    if (c->sel < 0 || c->sel >= c->nfiltered) return 0;
    const CompleteItem *it = &c->items[c->filtered[c->sel]];
    const char *text = it->insert_text[0] ? it->insert_text : it->label;
    out->start = c->word_start;
    out->end = cursor;
    snprintf(out->text, sizeof out->text, "%s", text);
    return 1;
}

const char *complete_kind_tag(CompleteKind kind) {
    switch (kind) {
    case COMPLETE_KIND_FUNCTION: return "fn";
    case COMPLETE_KIND_VARIABLE: return "var";
    case COMPLETE_KIND_KEYWORD: return "kw";
    case COMPLETE_KIND_TYPE: return "ty";
    case COMPLETE_KIND_FIELD: return "fld";
    case COMPLETE_KIND_MODULE: return "mod";
    case COMPLETE_KIND_TEXT:
    default: return "txt";
    }
}

/* ----- plain-text fallback source ----- */

static int word_already_collected(const CompleteItem *out, int n, const char *word) {
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i].label, word)) return 1;
    return 0;
}

int complete_collect_buffer_words(const char *text, const char *prefix,
                                  CompleteItem *out, int max) {
    if (!text || max <= 0) return 0;
    int n = 0;
    size_t i = 0;
    while (text[i] && n < max) {
        if (!is_ident_byte((unsigned char)text[i])) { i++; continue; }
        size_t start = i;
        while (text[i] && is_ident_byte((unsigned char)text[i])) i++;
        size_t len = i - start;
        if (len > 0 && len < COMPLETE_LABEL_CAP && !isdigit((unsigned char)text[start])) {
            char word[COMPLETE_LABEL_CAP];
            memcpy(word, text + start, len);
            word[len] = '\0';
            if (ci_prefix(word, prefix ? prefix : "") &&
                !word_already_collected(out, n, word)) {
                snprintf(out[n].label, sizeof out[n].label, "%s", word);
                snprintf(out[n].insert_text, sizeof out[n].insert_text, "%s", word);
                out[n].detail[0] = '\0';
                out[n].sort_text[0] = '\0';
                out[n].kind = COMPLETE_KIND_TEXT;
                n++;
            }
        }
    }
    return n;
}

/* ----- layout ----- */

int complete_layout(CompleteState *c, int fb_w, int fb_h, float adv, float line_h,
                    float top_pad, float bar_h, float anchor_x, float cur_top,
                    float left_limit, float fb_scale, CompleteLayout *out) {
    if (!c || !out || !c->active || c->nfiltered == 0) return 0;
    memset(out, 0, sizeof *out);

    int longest = 0;
    for (int i = 0; i < c->nfiltered; i++) {
        int l = (int)strlen(c->items[c->filtered[i]].label) + 4; /* + kind tag/margin */
        if (l > longest) longest = l;
    }
    if (longest < 8) longest = 8;
    if (longest > COMPLETE_LABEL_CAP) longest = COMPLETE_LABEL_CAP;

    out->padx = adv * 0.7f;
    out->pady = line_h * 0.25f;
    float gap = 4.0f * fb_scale;
    float bottom_limit = (float)fb_h - bar_h;

    float room_below = bottom_limit - (cur_top + line_h + gap) - out->pady * 2.0f;
    float room_above = (cur_top - gap) - top_pad - out->pady * 2.0f;
    int fit_below = (int)(room_below / line_h);
    int fit_above = (int)(room_above / line_h);
    int fit = fit_below > fit_above ? fit_below : fit_above;
    if (fit < 1) fit = 1;

    int vis = c->nfiltered;
    if (vis > COMPLETE_MAX_VISIBLE_ROWS) vis = COMPLETE_MAX_VISIBLE_ROWS;
    if (vis > fit) vis = fit;
    complete_set_view(c, vis);

    out->w = (float)longest * adv + out->padx * 2.0f;
    float max_w = (float)fb_w - left_limit;
    if (max_w > (float)fb_w - 2.0f * adv) max_w = (float)fb_w - 2.0f * adv;
    if (max_w < adv) max_w = adv;
    if (out->w > max_w) out->w = max_w;
    out->h = (float)vis * line_h + out->pady * 2.0f;

    out->x = anchor_x;
    if (fit_below >= vis && cur_top + line_h + gap + out->h <= bottom_limit)
        out->y = cur_top + line_h + gap;
    else
        out->y = cur_top - gap - out->h;
    if (out->y < top_pad) out->y = top_pad;
    if (out->y + out->h > bottom_limit) out->y = bottom_limit - out->h;

    if (out->x + out->w > (float)fb_w) out->x = (float)fb_w - out->w;
    if (out->x < left_limit) out->x = left_limit;
    if (out->x < 0) out->x = 0;

    out->border = 1.0f * fb_scale;
    out->visible_rows = vis;
    out->max_label_cells = (int)((out->w - out->padx * 2.0f) / adv);
    out->scrollable = c->nfiltered > vis;
    return 1;
}
