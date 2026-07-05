#include "draw.h"

#include <stdio.h>
#include <string.h>

#include "project_search.h"
#include "view.h"

void draw_text_run(Font *f, Renderer *r, const char *s, int n, float x,
                   float y, Color c) {
    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1, s0, t0, s1, t1;
        if (font_quad(f, (unsigned char)s[i], &x, &y, &x0, &y0, &x1, &y1, &s0,
                      &t0, &s1, &t1))
            renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1, c.r, c.g, c.b, 1.0f);
    }
}

void draw_triangle(Renderer *r, float x, float y, float size, int dir, Color c) {
    const int slices = 7;
    float step = size / (float)slices;
    for (int i = 0; i < slices; i++) {
        float t = ((float)i + 0.5f) / (float)slices;
        float w, ox;
        if (dir) {
            w = size * (1.0f - t);
            ox = x + (size - w) * 0.5f;
            renderer_rect(r, ox, y + (float)i * step, w, step + 0.5f,
                          c.r, c.g, c.b, 1.0f);
        } else {
            float h = size * (1.0f - t);
            float oy = y + (size - h) * 0.5f;
            renderer_rect(r, x + (float)i * step, oy, step + 0.5f, h,
                          c.r, c.g, c.b, 1.0f);
        }
    }
}

void draw_folder_icon(Renderer *r, float x, float y, float size, Color c) {
    float tab_h = size * 0.22f;
    renderer_rect(r, x, y + tab_h * 0.4f, size * 0.5f, tab_h,
                  c.r, c.g, c.b, 1.0f);
    renderer_rect(r, x, y + tab_h, size, size - tab_h, c.r, c.g, c.b, 1.0f);
}

void draw_file_icon(Renderer *r, float x, float y, float size, Color c) {
    float w = size * 0.78f, h = size;
    float fx = x + (size - w) * 0.5f;
    renderer_rect(r, fx, y, w, h, c.r, c.g, c.b, 1.0f);
    float fold = size * 0.32f;
    renderer_rect(r, fx + w - fold, y, fold, fold, 0.09f, 0.10f, 0.12f, 1.0f);
}

void draw_sidebar_panel(Workspace *ws, const char *active_path, int side_cells,
                        float side_scroll, int fb_h, Font *font, Renderer *r,
                        float adv, float line_h, float ascent, float side_px,
                        float top_y, float side_pad, float opacity,
                        float scrollbar_hover) {
    renderer_rect(r, 0, top_y, side_px, (float)fb_h - top_y,
                  0.09f, 0.10f, 0.12f, opacity);
    renderer_rect(r, side_px - 1.0f, top_y, 1.0f, (float)fb_h - top_y,
                  0.04f, 0.05f, 0.06f, opacity);

    float bottom_y = (float)fb_h - (line_h + 6.0f);
    size_t n = ws_visible_count(ws);
    ViewSidebarWindow win = view_sidebar_window((int)bottom_y, top_y, side_pad,
                                                side_scroll, line_h);
    for (int i = win.first; i < win.first + win.count && i < (int)n; i++) {
        const WsEntry *e = ws_visible(ws, (size_t)i);
        ViewSidebarRow row = view_sidebar_row(e, active_path, side_cells,
                                              i, side_scroll, side_pad, top_y,
                                              adv, line_h, ascent);

        if (row.active)
            renderer_rect(r, 0, row.top, side_px, line_h,
                          0.16f, 0.18f, 0.22f, 1.0f);

        Color c = e->is_dir ? (Color){0.58f, 0.70f, 0.92f}
                            : (Color){0.78f, 0.80f, 0.84f};

        if (e->is_dir) {
            Color chev = {0.55f, 0.60f, 0.68f};
            draw_triangle(r, row.chevron_x + (adv - row.chevron_size) * 0.5f,
                          row.chevron_y, row.chevron_size,
                          e->collapsed ? 0 : 1, chev);
            draw_folder_icon(r, row.icon_x, row.icon_y, row.icon_size, c);
        } else {
            draw_file_icon(r, row.icon_x, row.icon_y, row.icon_size, c);
        }

        draw_text_run(font, r, e->name, row.name_len, row.name_x,
                      row.baseline, c);
    }

    LayoutScrollbar bar = layout_scrollbar(
        side_px - 5.6f, top_y + 4.0f, 3.6f, bottom_y - top_y - 8.0f,
        (float)n * line_h + side_pad, bottom_y - top_y, side_scroll);
    bar = layout_scrollbar_expand(bar, scrollbar_hover, 6.0f);
    draw_scrollbar(r, bar, opacity);
}

void draw_scrollbar(Renderer *r, LayoutScrollbar bar, float opacity) {
    if (!bar.visible) return;
    renderer_rect(r, bar.track_x, bar.track_y, bar.track_w, bar.track_h,
                  0.18f, 0.20f, 0.24f, opacity * 0.55f);
    renderer_rect(r, bar.track_x, bar.thumb_y, bar.track_w, bar.thumb_h,
                  0.52f, 0.57f, 0.64f, opacity * 0.96f);
}

float draw_tabs_panel(TabSet *tabs, int fb_w, Font *font, Renderer *r,
                      float side_px, float adv, float ascent, float tab_h,
                      float top_y, float opacity) {
    renderer_rect(r, side_px, top_y, (float)fb_w - side_px, tab_h,
                  0.08f, 0.09f, 0.11f, opacity);

    float tab_w = adv * 18.0f;
    for (int i = 0; i < tabs_count(tabs); i++) {
        float x = side_px + (float)i * tab_w;
        if (x >= (float)fb_w) break;
        int act = (i == tabs_active_index(tabs));
        renderer_rect(r, x, top_y, tab_w - 1.0f, tab_h, act ? 0.15f : 0.10f,
                      act ? 0.16f : 0.11f, act ? 0.21f : 0.13f, 1.0f);
        if (act)
            renderer_rect(r, x, top_y + tab_h - 2.0f, tab_w - 1.0f, 2.0f,
                          0.45f, 0.60f, 0.95f, 1.0f);

        Editor *e = tabs_at(tabs, i);
        char label[64];
        view_tab_label(e, label, sizeof label);
        int nl = view_clamp_text_len(label, (int)(tab_w / adv) - 3);
        Color c = act ? (Color){0.92f, 0.93f, 0.95f}
                      : (Color){0.62f, 0.66f, 0.72f};
        draw_text_run(font, r, label, nl, x + adv * 0.6f,
                      top_y + ascent + 2.0f, c);
        draw_text_run(font, r, "x", 1, x + tab_w - adv * 1.5f,
                      top_y + ascent + 2.0f, (Color){0.50f, 0.54f, 0.60f});
    }
    return tab_w;
}

void draw_header_panel(const char *root, const char *path, int fb_w,
                       Font *font, Renderer *r, float adv, float ascent,
                       float header_h, float fb_scale, float opacity) {
    renderer_rect(r, 0, 0, (float)fb_w, header_h,
                  0.07f, 0.08f, 0.10f, opacity);
    renderer_rect(r, 0, header_h - 1.0f, (float)fb_w, 1.0f,
                  0.04f, 0.05f, 0.06f, opacity);

    ViewHeaderTitle title = view_header_title(root, path, (float)fb_w,
                                              header_h, adv, ascent, fb_scale);
    draw_text_run(font, r, title.title, (int)strlen(title.title), title.x,
                  title.baseline, (Color){0.66f, 0.70f, 0.76f});
}

void draw_editor_text_panel(Editor *e, const TextFrameView *text,
                            const HighlightSpan *spans, size_t nspans,
                            const Diagnostic *diags, size_t ndiag,
                            int insert_mode, int cursor_on, int fb_h,
                            Font *font, Renderer *r, float side_px,
                            float gutter, float text_x, float top_pad,
                            float line_h, float adv, float ascent) {
    if (!e || !text || !text->pt) return;

    static char linebuf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    static int rowspans[4096];
    for (int ln = text->window.first_line; ln < (int)text->total_lines; ln++) {
        if (e->vstart[ln] - text->window.first_vrow > text->window.vis_vrows) break;

        TextLineView line;
        if (!text_view_line(text->pt, ln, text->total_lines, text->cols,
                            linebuf, WRAP_MAXLINE, brk, WRAP_MAXBRK, &line))
            continue;

        for (int k = 0; k < line.wrap_count; k++) {
            float top = text_view_row_top(e, e->vstart[ln], k, top_pad, line_h);
            if (top > (float)fb_h) {
                k = line.wrap_count;
                ln = (int)text->total_lines;
                break;
            }
            if (top + line_h < top_pad) continue;
            float baseline = top + ascent;
            int rs = brk[k];
            int re = (k + 1 < line.wrap_count) ? brk[k + 1] : (int)line.len;

            if (k == 0) {
                char num[16];
                int nl = snprintf(num, sizeof num, "%d", ln + 1);
                float nx = text_view_gutter_x(side_px, gutter, adv, ln + 1);
                draw_text_run(font, r, num, nl, nx, baseline,
                              (Color){0.38f, 0.42f, 0.48f});
            }

            if (text->has_selection) {
                TextColumnSpan span;
                if (text_view_clip_selection(linebuf, (int)line.len, line.start,
                                             rs, re, &text->selection, &span)) {
                    renderer_rect(r, text_x + adv * (float)span.start_col, top,
                                  adv * (float)(span.end_col - span.start_col),
                                  line_h, 0.22f, 0.28f, 0.40f, 1.0f);
                }
            }

            size_t row_lo = line.start + (size_t)rs;
            size_t row_hi = line.start + (size_t)re;
            int nrow = text_view_filter_spans(spans, nspans, row_lo, row_hi,
                                              rowspans, 4096);

            float pen_x = text_x, pen_y = baseline;
            for (int i = rs; i < re; i++) {
                size_t abs = line.start + (size_t)i;
                unsigned char ch = (unsigned char)linebuf[i];
                if (ch == '\t') {
                    pen_x += adv * 4.0f;
                    continue;
                }
                Color c = theme_color(NULL);
                for (int s = 0; s < nrow; s++)
                    if (abs >= spans[rowspans[s]].start_byte &&
                        abs < spans[rowspans[s]].end_byte)
                        c = theme_color(spans[rowspans[s]].name);
                float x0, y0, x1, y1, s0, t0, s1, t1;
                if (font_quad(font, ch, &pen_x, &pen_y, &x0, &y0, &x1, &y1,
                              &s0, &t0, &s1, &t1))
                    renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1,
                                   c.r, c.g, c.b, 1.0f);
            }

            if ((int)text->cursor.row == ln && text->cursor.subrow == k && cursor_on) {
                float cx = text_x + adv * (float)text->cursor.xcol;
                if (insert_mode)
                    renderer_rect(r, cx, top + 2.0f, 2.0f, line_h - 4.0f,
                                  0.95f, 0.85f, 0.30f, 1.0f);
                else
                    renderer_rect(r, cx, top + 2.0f, adv, line_h - 4.0f,
                                  0.95f, 0.85f, 0.30f, 0.55f);
            }

            for (size_t d = 0; d < ndiag; d++) {
                TextColumnSpan span;
                if (!text_view_clip_diagnostic(linebuf, (int)line.len, ln, rs,
                                               re, &diags[d], &span))
                    continue;
                renderer_rect(r, text_x + adv * (float)span.start_col,
                              top + line_h - 3.0f,
                              adv * (float)(span.end_col - span.start_col),
                              2.0f, 0.92f, 0.30f, 0.30f, 0.9f);
            }
        }
    }
}

void draw_palette_panel(OverlayState *overlay, Workspace *ws, int fb_w,
                        Font *font, Renderer *r, float adv, float line_h,
                        float ascent) {
    ViewOverlayLayout box = view_overlay_layout((float)fb_w, 0.6f, 12,
                                                overlay->palette.sel, adv,
                                                line_h);

    renderer_rect(r, box.shadow_x, box.shadow_y, box.shadow_w, box.shadow_h,
                  0.0f, 0.0f, 0.0f, 0.5f);
    renderer_rect(r, box.x, box.y, box.w, box.h, 0.13f, 0.14f, 0.17f, 1.0f);
    renderer_rect(r, box.x, box.y, box.w, box.query_h, 0.17f, 0.18f, 0.22f, 1.0f);

    char qline[300];
    overlay->palette.query[overlay->palette.query_len] = '\0';
    snprintf(qline, sizeof qline, "> %s", overlay->palette.query);
    draw_text_run(font, r, qline, (int)strlen(qline), box.x + adv,
                  box.y + ascent + 2, (Color){0.92f, 0.93f, 0.95f});

    int shown = overlay->palette.filtered_n < box.rows ? overlay->palette.filtered_n : box.rows;
    for (int i = 0; i < shown; i++) {
        int idx = box.start + i;
        if (idx >= overlay->palette.filtered_n) break;
        const WsEntry *e = ws_entry(ws, overlay->palette.filtered[idx]);
        float top = box.result_top + (float)i * line_h;
        if (idx == overlay->palette.sel)
            renderer_rect(r, box.x, top, box.w, line_h, 0.22f, 0.30f, 0.42f, 1.0f);
        int nl = view_clamp_text_len(e->rel, box.max_cells);
        draw_text_run(font, r, e->rel, nl, box.x + adv, top + ascent,
                      (Color){0.85f, 0.87f, 0.90f});
    }
}

void draw_search_panel(OverlayState *overlay, int fb_w, Font *font,
                       Renderer *r, float adv, float line_h, float ascent) {
    ViewOverlayLayout box = view_overlay_layout((float)fb_w, 0.72f, 14,
                                                overlay->search.sel, adv,
                                                line_h);

    renderer_rect(r, box.shadow_x, box.shadow_y, box.shadow_w, box.shadow_h,
                  0.0f, 0.0f, 0.0f, 0.5f);
    renderer_rect(r, box.x, box.y, box.w, box.h, 0.13f, 0.14f, 0.17f, 1.0f);
    renderer_rect(r, box.x, box.y, box.w, box.query_h, 0.17f, 0.18f, 0.22f, 1.0f);

    char qline[300];
    overlay->search.query[overlay->search.query_len] = '\0';
    snprintf(qline, sizeof qline, "search: %s", overlay->search.query);
    draw_text_run(font, r, qline, (int)strlen(qline), box.x + adv,
                  box.y + ascent + 2, (Color){0.92f, 0.93f, 0.95f});

    int n = (int)project_search_count(&overlay->search);
    char status[64];
    view_search_status(status, sizeof status, overlay->search.unavailable,
                       overlay->search.query_len,
                       project_search_running(&overlay->search),
                       project_search_truncated(&overlay->search), n);
    float sw = (float)strlen(status) * adv;
    draw_text_run(font, r, status, (int)strlen(status), box.x + box.w - sw - adv,
                  box.y + ascent + 2, (Color){0.55f, 0.58f, 0.64f});

    for (int i = 0; i < box.rows; i++) {
        int idx = box.start + i;
        if (idx >= n) break;
        const SearchHit *h = project_search_hit(&overlay->search, (size_t)idx);
        if (!h) break;
        float top = box.result_top + (float)i * line_h;
        if (idx == overlay->search.sel)
            renderer_rect(r, box.x, top, box.w, line_h, 0.22f, 0.30f, 0.42f, 1.0f);

        char loc[1100];
        snprintf(loc, sizeof loc, "%s:%d:", h->path, h->line);
        int ll = view_clamp_text_len(loc, box.max_cells);
        draw_text_run(font, r, loc, ll, box.x + adv, top + ascent,
                      (Color){0.55f, 0.74f, 0.95f});
        int remain = box.max_cells - ll - 1;
        if (remain > 2) {
            int tl = (int)strlen(h->text);
            tl = view_clamp_text_len(h->text, remain);
            draw_text_run(font, r, h->text, tl,
                          box.x + adv + (float)(ll + 1) * adv, top + ascent,
                          (Color){0.78f, 0.80f, 0.84f});
        }
    }
}

void draw_popover_panel(Popover *state, int fb_w, int fb_h, Font *font,
                        Renderer *r, float adv, float line_h, float ascent,
                        float top_pad, float bar_h, float anchor_x,
                        float cur_top, float side_px, float fb_scale) {
    PopoverLayout pop;
    if (!popover_layout(state, fb_w, fb_h, adv, line_h, top_pad, bar_h,
                        anchor_x, cur_top, side_px, fb_scale, &pop))
        return;
    const char *t = state->text;

    renderer_rect(r, pop.x - pop.border, pop.y - pop.border,
                  pop.w + 2 * pop.border, pop.h + 2 * pop.border,
                  0.33f, 0.36f, 0.42f, 1.0f);
    renderer_rect(r, pop.x, pop.y, pop.w, pop.h, 0.16f, 0.17f, 0.20f, 1.0f);

    for (int i = 0; i < pop.visible_rows; i++) {
        int row = state->scroll + i;
        if (row >= pop.rows.count) break;
        int len = pop.rows.len[row];
        if (len > pop.max_text_cells) len = pop.max_text_cells;
        float ty = pop.y + pop.pady + ascent + (float)i * line_h;
        draw_text_run(font, r, t + pop.rows.off[row], len, pop.x + pop.padx, ty,
                      (Color){0.88f, 0.90f, 0.93f});
    }

    if (pop.scrollable) {
        renderer_rect(r, pop.scrollbar_track_x, pop.y, pop.scrollbar_track_w,
                      pop.h, 0.24f, 0.26f, 0.30f, 1.0f);
        renderer_rect(r, pop.scrollbar_track_x, pop.scrollbar_thumb_y,
                      pop.scrollbar_track_w, pop.scrollbar_thumb_h,
                      0.50f, 0.54f, 0.60f, 1.0f);
    }
}
