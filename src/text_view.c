#include "text_view.h"

#include <string.h>

int text_view_wrap_cols(int wrap, float fb_w, float text_x, float adv) {
    int cols = wrap ? (int)((fb_w - text_x) / adv) : WRAP_NOWRAP_COLS;
    return cols < 4 ? 4 : cols;
}

int text_view_line(const PieceTable *pt, int line, size_t total_lines, int cols,
                   char *buf, size_t buf_cap, int *breaks, int max_breaks,
                   TextLineView *out) {
    if (!pt || !buf || buf_cap == 0 || !breaks || !out || line < 0 ||
        (size_t)line >= total_lines)
        return 0;
    memset(out, 0, sizeof *out);
    out->start = pt_line_start(pt, (size_t)line);
    out->end = (line + 1 < (int)total_lines)
                   ? pt_line_start(pt, (size_t)line + 1)
                   : pt_length(pt);
    out->len = out->end - out->start;
    if (out->len > 0 && byte_at(pt, out->end - 1) == '\n') out->len--;
    if (out->len > buf_cap) out->len = buf_cap;
    pt_read(pt, out->start, out->len, buf);
    out->wrap_count = wrap_line(buf, out->len, cols, breaks, max_breaks);
    return 1;
}

float text_view_row_top(const Editor *e, int line_vstart, int subrow,
                        float top_pad, float line_h) {
    return top_pad + (float)(line_vstart + subrow) * line_h -
           (e ? e->scroll_y : 0.0f);
}

float text_view_gutter_x(float side_px, float gutter, float adv, int line_no) {
    int digits = 1;
    for (int n = line_no; n >= 10; n /= 10) digits++;
    return side_px + gutter - adv * 0.5f - adv * (float)digits;
}

int text_view_cells_between(const char *line, int from, int to, int line_len) {
    if (!line || line_len <= 0) return 0;
    if (from < 0) from = 0;
    if (to < from) to = from;
    if (from > line_len) from = line_len;
    if (to > line_len) to = line_len;
    int cells = 0;
    for (int i = from; i < to; i++)
        cells += cell_w((unsigned char)line[i]);
    return cells;
}

int text_view_clip_selection(const char *line, int line_len, size_t line_start,
                             int row_start, int row_end, const EditorRange *sel,
                             TextColumnSpan *out) {
    if (!line || !sel || !out) return 0;
    size_t rowa = line_start + (size_t)row_start;
    size_t rowb = line_start + (size_t)row_end;
    size_t a = sel->start > rowa ? sel->start : rowa;
    size_t b = sel->end < rowb ? sel->end : rowb;
    if (!(sel->end > rowa && sel->start < rowb + 1 && b > a)) return 0;
    int from = (int)(a - line_start);
    int to = (int)(b - line_start);
    int xa = text_view_cells_between(line, row_start, from, line_len);
    int xb = xa + text_view_cells_between(line, from, to, line_len);
    if (xb == xa) xb = xa + 1;
    out->start_col = xa;
    out->end_col = xb;
    return 1;
}

int text_view_clip_diagnostic(const char *line, int line_len, int line_no,
                              int row_start, int row_end, const Diagnostic *diag,
                              TextColumnSpan *out) {
    if (!line || !diag || !out) return 0;
    if ((int)diag->start_row > line_no || (int)diag->end_row < line_no) return 0;
    int dc0 = (diag->start_row == (size_t)line_no) ? (int)diag->start_col : 0;
    int dc1 = (diag->end_row == (size_t)line_no) ? (int)diag->end_col : line_len;
    if (dc1 <= dc0) dc1 = dc0 + 1;
    int a = dc0 > row_start ? dc0 : row_start;
    int b = dc1 < row_end ? dc1 : row_end;
    if (b <= a) return 0;
    int xa = text_view_cells_between(line, row_start, a, line_len);
    int xb = xa + text_view_cells_between(line, a, b, line_len);
    if (xb == xa) xb = xa + 1;
    out->start_col = xa;
    out->end_col = xb;
    return 1;
}

int text_view_filter_spans(const HighlightSpan *spans, size_t nspans,
                           size_t row_start, size_t row_end,
                           int *out, int max_out) {
    if (!spans || !out || max_out <= 0) return 0;
    int n = 0;
    for (size_t s = 0; s < nspans && n < max_out; s++)
        if (spans[s].start_byte < row_end && spans[s].end_byte > row_start)
            out[n++] = (int)s;
    return n;
}

void text_view_cursor(Editor *e, int cols, TextCursorView *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!e || !e->buf || !e->vstart) return;

    const PieceTable *pt = buffer_pt(e->buf);
    size_t total_lines = pt_line_count(pt);
    pt_offset_to_rowcol(pt, e->cursor, &out->row, &out->col);

    static char cbuf[WRAP_MAXLINE];
    static int cbrk[WRAP_MAXBRK];
    size_t ls = pt_line_start(pt, out->row);
    size_t le = (out->row + 1 < total_lines) ? pt_line_start(pt, out->row + 1) : pt_length(pt);
    size_t llen = le - ls;
    if (llen > 0 && byte_at(pt, le - 1) == '\n') llen--;
    if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
    pt_read(pt, ls, llen, cbuf);
    int nb = wrap_line(cbuf, llen, cols, cbrk, WRAP_MAXBRK);
    for (int k = 0; k < nb; k++) {
        int s = cbrk[k], en = (k + 1 < nb) ? cbrk[k + 1] : (int)llen;
        if ((int)out->col >= s && ((int)out->col < en || k == nb - 1)) {
            out->subrow = k;
            break;
        }
    }
    for (int i = cbrk[out->subrow]; i < (int)out->col && i < (int)llen; i++)
        out->xcol += cell_w((unsigned char)cbuf[i]);
    out->vrow = e->vstart[out->row] + out->subrow;
}

void text_view_clamp_scroll(Editor *e, int total_vrows, int cur_vrow,
                            float line_h, float view_h) {
    if (!e || line_h <= 0 || view_h <= 0) return;
    if (e->cursor != e->seen_cursor) {
        float cy = (float)cur_vrow * line_h;
        if (cy < e->scroll_y) e->scroll_y = cy;
        else if (cy + line_h > e->scroll_y + view_h) e->scroll_y = cy + line_h - view_h;
        e->seen_cursor = e->cursor;
    }
    float maxscroll = (float)total_vrows * line_h - view_h;
    if (e->scroll_y > maxscroll) e->scroll_y = maxscroll;
    if (e->scroll_y < 0) e->scroll_y = 0;
}

void text_view_visible_window(Editor *e, const PieceTable *pt, size_t total_lines,
                              float fb_h, float top_pad, float line_h,
                              TextVisibleWindow *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!e || !pt || line_h <= 0) return;
    out->first_vrow = (int)(e->scroll_y / line_h);
    if (out->first_vrow < 0) out->first_vrow = 0;
    out->vis_vrows = (int)((fb_h - top_pad) / line_h) + 2;
    out->first_line = line_at_vrow(e, out->first_vrow);
    out->last_line = line_at_vrow(e, out->first_vrow + out->vis_vrows);
    out->byte_start = pt_line_start(pt, (size_t)out->first_line);
    out->byte_end = (out->last_line + 1 < (int)total_lines)
                        ? pt_line_start(pt, (size_t)out->last_line + 1)
                        : pt_length(pt);
}

int text_view_selection(Editor *e, int visual_mode, EditorRange *out) {
    if (!visual_mode || !out) return 0;
    return editor_visual_range(e, out);
}

int text_view_prepare(Editor *e, int visual_mode, int wrap, float fb_w,
                      float text_x, float adv, float fb_h, float top_pad,
                      float line_h, float status_bar_h, TextFrameView *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!e || !e->buf) return 0;

    out->pt = buffer_pt(e->buf);
    out->total_lines = pt_line_count(out->pt);
    out->cols = text_view_wrap_cols(wrap, fb_w, text_x, adv);
    wrap_build(e, out->cols);
    out->total_vrows = e->vstart[out->total_lines];
    text_view_cursor(e, out->cols, &out->cursor);
    text_view_clamp_scroll(e, out->total_vrows, out->cursor.vrow, line_h,
                           fb_h - top_pad - status_bar_h);
    text_view_visible_window(e, out->pt, out->total_lines, fb_h, top_pad,
                             line_h, &out->window);
    out->has_selection = text_view_selection(e, visual_mode, &out->selection);
    return 1;
}
