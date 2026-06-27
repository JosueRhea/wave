#include "popover.h"

#include <stdio.h>
#include <string.h>

void popover_init(Popover *p) {
    memset(p, 0, sizeof *p);
}

void popover_close(Popover *p) {
    if (!p) return;
    p->active = 0;
    p->loading = 0;
}

void popover_set_base(Popover *p, const char *base) {
    if (!p) return;
    snprintf(p->base, sizeof p->base, "%s", base ? base : "");
    p->scroll = 0;
}

void popover_set_encoded_base(Popover *p, const char *encoded) {
    if (!p) return;
    char out[sizeof p->base];
    size_t o = 0;
    if (encoded) {
        for (const char *s = encoded; *s && o + 2 < sizeof out; s++) {
            if (*s == '\\' && s[1] == 'n') {
                out[o++] = '\n';
                s++;
            } else if (*s == '|') {
                out[o++] = '\n';
                out[o++] = '\n';
            } else {
                out[o++] = *s;
            }
        }
    }
    out[o] = '\0';
    popover_set_base(p, out);
}

void popover_set_loading(Popover *p, int loading) {
    if (!p) return;
    p->loading = loading != 0;
}

void popover_compose(Popover *p, const char *hover) {
    if (!p) return;
    char out[sizeof p->text];
    size_t off = 0;
    out[0] = '\0';
    if (p->base[0])
        off += snprintf(out + off, sizeof out - off, "%s", p->base);
    if (hover && hover[0])
        off += snprintf(out + off, sizeof out - off, "%s%s", off ? "\n\n" : "", hover);
    else if (p->loading)
        off += snprintf(out + off, sizeof out - off, "%s%s", off ? "\n\n" : "", "Loading...");
    if (!out[0]) snprintf(out, sizeof out, "No diagnostics or info here.");
    snprintf(p->text, sizeof p->text, "%s", out);
    size_t L = strlen(p->text);
    while (L && p->text[L - 1] == '\n') p->text[--L] = '\0';
    p->active = 1;
}

void popover_show_base(Popover *p, const char *base, int loading) {
    if (!p) return;
    popover_set_base(p, base);
    popover_set_loading(p, loading);
    popover_compose(p, NULL);
}

void popover_show_hover(Popover *p, const char *hover) {
    if (!p || !p->active) return;
    popover_set_loading(p, 0);
    popover_compose(p, hover && hover[0] ? hover : NULL);
}

void popover_scroll(Popover *p, int delta) {
    if (!p) return;
    int maxscroll = p->total_rows - p->vis_rows;
    if (maxscroll < 0) maxscroll = 0;
    p->scroll += delta;
    if (p->scroll < 0) p->scroll = 0;
    if (p->scroll > maxscroll) p->scroll = maxscroll;
}

void popover_set_view(Popover *p, int total_rows, int vis_rows) {
    if (!p) return;
    p->total_rows = total_rows;
    p->vis_rows = vis_rows;
    popover_scroll(p, 0);
}

int popover_apply_normal_char(Popover *p, unsigned int cp, int g_pending) {
    if (!p || !p->active) return 0;
    if (cp == 'j' || cp == 'k') {
        popover_scroll(p, (cp == 'j') ? 1 : -1);
        return 1;
    }
    if (!(g_pending || cp == 'g')) popover_close(p);
    return 0;
}

int popover_apply_key(Popover *p, PopoverKey key, int insert_mode) {
    if (!p || !p->active) return 0;
    switch (key) {
    case POPOVER_KEY_ESCAPE:
        popover_close(p);
        return 1;
    case POPOVER_KEY_UP:
    case POPOVER_KEY_DOWN:
        if (insert_mode) return 0;
        popover_scroll(p, key == POPOVER_KEY_DOWN ? 1 : -1);
        return 1;
    case POPOVER_KEY_NONE:
    default:
        return 0;
    }
}

void popover_wrap_text(const char *text, int max_cols, PopoverRows *rows) {
    if (!rows) return;
    memset(rows, 0, sizeof *rows);
    if (!text || !text[0]) return;
    if (max_cols < 1) max_cols = 1;

    int n = (int)strlen(text);
    int p = 0;
    while (p <= n && rows->count < POPOVER_DISPLAY_ROWS) {
        int ls = p;
        while (p < n && text[p] != '\n') p++;
        int llen = p - ls;
        int off = 0;
        for (;;) {
            int seg = llen - off;
            if (seg > max_cols) seg = max_cols;
            if (off + seg < llen) {
                int br = seg;
                while (br > max_cols / 3 && text[ls + off + br] != ' ') br--;
                if (br > max_cols / 3) seg = br;
            }
            rows->off[rows->count] = ls + off;
            rows->len[rows->count] = seg;
            if (seg > rows->longest) rows->longest = seg;
            rows->count++;
            off += seg;
            if (off < llen && text[ls + off] == ' ') off++;
            if (off >= llen || rows->count >= POPOVER_DISPLAY_ROWS) break;
        }
        p++;
    }
}

int popover_layout(Popover *p, int fb_w, int fb_h, float adv, float line_h,
                   float top_pad, float bar_h, float anchor_x, float cur_top,
                   float left_limit, float fb_scale, PopoverLayout *out) {
    if (!p || !out || !p->active || !p->text[0]) return 0;
    memset(out, 0, sizeof *out);
    popover_wrap_text(p->text, POPOVER_MAX_COLS, &out->rows);
    int nrows = out->rows.count;
    if (nrows == 0) return 0;

    out->padx = adv * 0.8f;
    out->pady = line_h * 0.35f;
    float gap = 6.0f * fb_scale;
    float bottom_limit = (float)fb_h - bar_h;

    float room_below = bottom_limit - (cur_top + line_h + gap) - out->pady * 2.0f;
    float room_above = (cur_top - gap) - top_pad - out->pady * 2.0f;
    int fit_below = (int)(room_below / line_h);
    int fit_above = (int)(room_above / line_h);
    int fit = fit_below > fit_above ? fit_below : fit_above;
    if (fit < 1) fit = 1;

    int vis = nrows;
    if (vis > POPOVER_MAX_ROWS) vis = POPOVER_MAX_ROWS;
    if (vis > fit) vis = fit;
    popover_set_view(p, nrows, vis);

    int maxscroll = nrows - vis;
    if (maxscroll < 0) maxscroll = 0;
    out->scrollable = nrows > vis;
    out->scrollbar_w = out->scrollable ? adv * 0.5f : 0.0f;
    out->w = (float)out->rows.longest * adv + out->padx * 2.0f + out->scrollbar_w;
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
    float content_w = out->w - out->padx * 2.0f - out->scrollbar_w;
    out->max_text_cells = (int)(content_w / adv);

    if (out->scrollable) {
        out->scrollbar_track_x = out->x + out->w - out->scrollbar_w +
                                 out->scrollbar_w * 0.25f;
        out->scrollbar_track_w = out->scrollbar_w * 0.5f;
        out->scrollbar_thumb_h = out->h * (float)vis / (float)nrows;
        if (out->scrollbar_thumb_h < line_h * 0.5f)
            out->scrollbar_thumb_h = line_h * 0.5f;
        out->scrollbar_thumb_y = out->y + (out->h - out->scrollbar_thumb_h) *
                                 (float)p->scroll / (float)maxscroll;
    }
    return 1;
}
