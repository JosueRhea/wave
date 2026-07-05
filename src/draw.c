#include "draw.h"

#include <stdio.h>
#include <string.h>

#include "project_search.h"
#include "view.h"

static float draw_clamp_radius(float radius, float w, float h) {
    if (radius < 0.0f) return 0.0f;
    float max = w < h ? w * 0.5f : h * 0.5f;
    return radius > max ? max : radius;
}

static void draw_round_rect(Renderer *r, float x, float y, float w, float h,
                            float radius, Color c, float alpha) {
    radius = draw_clamp_radius(radius, w, h);
    if (radius < 1.0f) {
        renderer_rect(r, x, y, w, h, c.r, c.g, c.b, alpha);
        return;
    }

    int rr = (int)(radius + 0.5f);
    renderer_rect(r, x + radius, y, w - radius * 2.0f, h,
                  c.r, c.g, c.b, alpha);
    renderer_rect(r, x, y + radius, w, h - radius * 2.0f,
                  c.r, c.g, c.b, alpha);

    for (int i = 0; i < rr; i++) {
        float dy = (float)i + 0.5f;
        float span = radius * radius - dy * dy;
        if (span < 0.0f) span = 0.0f;
        float dx = 0.0f;
        while ((dx + 1.0f) * (dx + 1.0f) <= span) dx += 1.0f;
        float row_y_top = y + radius - dy - 0.5f;
        float row_y_bottom = y + h - radius + dy - 0.5f;
        renderer_rect(r, x + radius - dx, row_y_top, w - (radius - dx) * 2.0f,
                      1.0f, c.r, c.g, c.b, alpha);
        renderer_rect(r, x + radius - dx, row_y_bottom, w - (radius - dx) * 2.0f,
                      1.0f, c.r, c.g, c.b, alpha);
    }
}

static const char *draw_path_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int draw_path_parent_len(const char *path, const char *base) {
    if (!path || !base || base <= path) return 0;
    int len = (int)(base - path);
    return len > 0 && path[len - 1] == '/' ? len - 1 : len;
}

static Color draw_file_type_color(const char *path) {
    const char *base = draw_path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!strcmp(base, ".env") || !strncmp(base, ".env.", 5))
        return (Color){0.56f, 0.82f, 0.38f};
    if (!dot) return (Color){0.62f, 0.68f, 0.78f};
    if (!strcmp(dot, ".tsx") || !strcmp(dot, ".jsx"))
        return (Color){0.34f, 0.70f, 0.92f};
    if (!strcmp(dot, ".ts") || !strcmp(dot, ".js"))
        return (Color){0.48f, 0.76f, 0.95f};
    if (!strcmp(dot, ".json"))
        return (Color){0.72f, 0.82f, 0.36f};
    if (!strcmp(dot, ".yaml") || !strcmp(dot, ".yml"))
        return (Color){0.82f, 0.62f, 0.95f};
    if (!strcmp(dot, ".c") || !strcmp(dot, ".h") || !strcmp(dot, ".m"))
        return (Color){0.58f, 0.73f, 0.98f};
    return (Color){0.62f, 0.68f, 0.78f};
}

static void draw_file_badge(Renderer *r, float x, float y, float size, Color c,
                            int selected) {
    draw_round_rect(r, x, y, size, size, size * 0.14f, c,
                    selected ? 0.95f : 0.82f);
    renderer_rect(r, x + size * 0.70f, y, size * 0.30f, size * 0.30f,
                  0.09f, 0.10f, 0.13f, selected ? 0.58f : 0.78f);
}

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
                        float scrollbar_hover, float radius,
                        int create_active, int create_row, int create_depth,
                        int create_is_dir, const char *create_text,
                        int hover_row) {
    renderer_rect(r, 0, top_y, side_px, (float)fb_h - top_y,
                  0.09f, 0.10f, 0.12f, opacity);
    renderer_rect(r, side_px - 1.0f, top_y, 1.0f, (float)fb_h - top_y,
                  0.04f, 0.05f, 0.06f, opacity);

    float bottom_y = (float)fb_h - (line_h + 6.0f);
    size_t base_n = ws_visible_count(ws);
    size_t n = base_n + (create_active ? 1u : 0u);
    if (create_row < 0) create_row = 0;
    if ((size_t)create_row > base_n) create_row = (int)base_n;
    ViewSidebarWindow win = view_sidebar_window((int)bottom_y, top_y, side_pad,
                                                side_scroll, line_h);
    for (int i = win.first; i < win.first + win.count && i < (int)n; i++) {
        WsEntry create_entry = {0};
        const WsEntry *e = NULL;
        int creating = create_active && i == create_row;
        if (creating) {
            create_entry.rel = (char *)(create_text && create_text[0] ? create_text : "");
            create_entry.name = create_entry.rel;
            create_entry.depth = create_depth;
            create_entry.is_dir = create_is_dir;
            create_entry.collapsed = 1;
            e = &create_entry;
        } else {
            int source_i = i - (create_active && i > create_row ? 1 : 0);
            e = ws_visible(ws, (size_t)source_i);
        }
        if (!e) continue;
        ViewSidebarRow row = view_sidebar_row(e, active_path, side_cells,
                                              i, side_scroll, side_pad, top_y,
                                              adv, line_h, ascent);

        int hovered = i == hover_row;
        if (row.active || creating || hovered) {
            Color bg = (row.active || creating) ? (Color){0.16f, 0.18f, 0.22f}
                                                : (Color){0.13f, 0.15f, 0.18f};
            draw_round_rect(r, side_pad * 0.5f, row.top + 1.0f,
                            side_px - side_pad, line_h - 2.0f,
                            radius * 0.7f, bg, row.active || creating ? 1.0f : 0.92f);
        }

        Color c = creating ? (Color){0.92f, 0.93f, 0.95f}
                           : (e->is_dir ? (hovered ? (Color){0.68f, 0.78f, 0.96f}
                                                   : (Color){0.58f, 0.70f, 0.92f})
                                        : (hovered ? (Color){0.90f, 0.92f, 0.95f}
                                                   : (Color){0.78f, 0.80f, 0.84f}));

        if (e->is_dir) {
            Color chev = {0.55f, 0.60f, 0.68f};
            if (!creating)
                draw_triangle(r, row.chevron_x + (adv - row.chevron_size) * 0.5f,
                              row.chevron_y, row.chevron_size,
                              e->collapsed ? 0 : 1, chev);
            draw_folder_icon(r, row.icon_x, row.icon_y, row.icon_size, c);
        } else {
            draw_file_icon(r, row.icon_x, row.icon_y, row.icon_size, c);
        }

        const char *name = creating && (!create_text || !create_text[0])
            ? (create_is_dir ? "new-folder" : "new-file") : e->name;
        int name_len = view_clamp_text_len(name, side_cells - e->depth * 2 - 6);
        draw_text_run(font, r, name, name_len, row.name_x, row.baseline, c);
        if (creating) {
            float cursor_x = row.name_x + adv * (float)name_len + adv * 0.15f;
            renderer_rect(r, cursor_x, row.top + 4.0f, 1.4f, line_h - 8.0f,
                          0.80f, 0.88f, 1.0f, 1.0f);
        }
    }

    LayoutScrollbar bar = layout_scrollbar(
        side_px - 5.6f, top_y + 4.0f, 3.6f, bottom_y - top_y - 8.0f,
        (float)n * line_h + side_pad, bottom_y - top_y, side_scroll);
    bar = layout_scrollbar_expand(bar, scrollbar_hover, 6.0f);
    draw_scrollbar(r, bar, opacity, radius);
}

void draw_scrollbar(Renderer *r, LayoutScrollbar bar, float opacity, float radius) {
    if (!bar.visible) return;
    draw_round_rect(r, bar.track_x, bar.track_y, bar.track_w, bar.track_h,
                    radius * 0.5f, (Color){0.18f, 0.20f, 0.24f},
                    opacity * 0.55f);
    draw_round_rect(r, bar.track_x, bar.thumb_y, bar.track_w, bar.thumb_h,
                    radius * 0.5f, (Color){0.52f, 0.57f, 0.64f},
                    opacity * 0.96f);
}

float draw_tabs_panel(TabSet *tabs, int fb_w, Font *font, Renderer *r,
                      float side_px, float adv, float ascent, float tab_h,
                      float top_y, float opacity, float radius) {
    renderer_rect(r, side_px, top_y, (float)fb_w - side_px, tab_h,
                  0.08f, 0.09f, 0.11f, opacity);

    float tab_w = adv * 18.0f;
    for (int i = 0; i < tabs_count(tabs); i++) {
        float x = side_px + (float)i * tab_w;
        if (x >= (float)fb_w) break;
        int act = (i == tabs_active_index(tabs));
        draw_round_rect(r, x + 1.0f, top_y + 1.0f, tab_w - 3.0f, tab_h - 1.0f,
                        radius * 0.75f,
                        act ? (Color){0.15f, 0.16f, 0.21f}
                            : (Color){0.10f, 0.11f, 0.13f},
                        1.0f);
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
                        float ascent, float top_pad, float radius) {
    ViewOverlayLayout box = view_overlay_layout((float)fb_w, 0.54f, 11,
                                                overlay->palette.sel, adv,
                                                line_h);
    if (box.w > adv * 88.0f) {
        box.w = adv * 88.0f;
        box.x = ((float)fb_w - box.w) * 0.5f;
    }
    if (box.w < adv * 44.0f) {
        box.w = adv * 44.0f;
        box.x = ((float)fb_w - box.w) * 0.5f;
        if (box.x < adv) box.x = adv;
    }
    box.y = top_pad + line_h * 0.65f;
    box.query_h = line_h * 2.85f;
    box.result_top = box.y + box.query_h;
    box.h = box.query_h + line_h * (float)box.rows + 6.0f;
    box.shadow_x = box.x - 10.0f;
    box.shadow_y = box.y - 10.0f;
    box.shadow_w = box.w + 20.0f;
    box.shadow_h = box.h + 20.0f;
    box.max_cells = (int)((box.w - adv * 7.0f) / adv);
    if (box.max_cells < 12) box.max_cells = 12;

    draw_round_rect(r, box.x, box.y, box.w, box.h, radius,
                    (Color){0.078f, 0.086f, 0.104f}, 1.0f);
    draw_round_rect(r, box.x, box.y, box.w, box.query_h, radius,
                    (Color){0.094f, 0.102f, 0.122f}, 1.0f);
    renderer_rect(r, box.x, box.result_top - 1.0f, box.w, 1.0f,
                  0.13f, 0.145f, 0.17f, 0.28f);

    char qline[300];
    overlay->palette.query[overlay->palette.query_len] = '\0';
    snprintf(qline, sizeof qline, "> %s", overlay->palette.query);
    draw_text_run(font, r, "Open file", 9, box.x + adv * 1.15f,
                  box.y + ascent + 1.0f, (Color){0.76f, 0.80f, 0.88f});

    char count[64];
    int total = overlay->palette.filtered_n;
    snprintf(count, sizeof count, "%d %s", total, total == 1 ? "match" : "matches");
    float count_x = box.x + box.w - (float)strlen(count) * adv - adv * 1.15f;
    draw_text_run(font, r, count, (int)strlen(count), count_x,
                  box.y + ascent + 1.0f, (Color){0.50f, 0.55f, 0.64f});

    float input_y = box.y + line_h * 1.92f + 4.0f;
    draw_round_rect(r, box.x + adv, input_y - ascent + 1.0f,
                    box.w - adv * 2.0f, line_h + 3.0f, radius * 0.55f,
                    (Color){0.068f, 0.076f, 0.094f}, 1.0f);
    draw_text_run(font, r, qline, (int)strlen(qline), box.x + adv * 1.65f,
                  input_y, (Color){0.78f, 0.84f, 0.94f});

    int shown = overlay->palette.filtered_n < box.rows ? overlay->palette.filtered_n : box.rows;
    if (shown == 0) {
        const char *empty = overlay->palette.query_len ? "No matching files"
                                                       : "No files in workspace";
        draw_text_run(font, r, empty, (int)strlen(empty), box.x + adv * 1.2f,
                      box.result_top + ascent + line_h,
                      (Color){0.58f, 0.62f, 0.70f});
        return;
    }

    for (int i = 0; i < shown; i++) {
        int idx = box.start + i;
        if (idx >= overlay->palette.filtered_n) break;
        const WsEntry *e = ws_entry(ws, overlay->palette.filtered[idx]);
        float top = box.result_top + (float)i * line_h;
        int selected = idx == overlay->palette.sel;
        if (selected) {
            draw_round_rect(r, box.x + adv * 0.55f, top + 2.0f,
                            box.w - adv * 1.1f, line_h - 3.0f,
                            radius * 0.6f, (Color){0.17f, 0.22f, 0.31f},
                            1.0f);
            renderer_rect(r, box.x + adv * 0.55f, top + 3.0f,
                          2.0f, line_h - 5.0f, 0.34f, 0.52f, 0.84f, 0.95f);
        }
        if (i > 0)
            renderer_rect(r, box.x + adv, top, box.w - adv * 2.0f, 1.0f,
                          0.13f, 0.14f, 0.16f, 0.16f);

        const char *base = draw_path_basename(e->rel);
        int parent_len = draw_path_parent_len(e->rel, base);
        float icon_size = line_h * 0.56f;
        float icon_x = box.x + adv * 1.25f;
        float icon_y = top + (line_h - icon_size) * 0.5f;
        draw_file_badge(r, icon_x, icon_y, icon_size,
                        draw_file_type_color(e->rel), selected);

        float text_x = box.x + adv * 3.35f;
        int name_budget = box.max_cells;
        if (parent_len > 0) name_budget -= parent_len + 3;
        if (name_budget < 10) name_budget = box.max_cells;
        int name_len = view_clamp_text_len(base, name_budget);
        Color name_color = selected ? (Color){0.96f, 0.97f, 0.99f}
                                    : (Color){0.84f, 0.87f, 0.92f};
        draw_text_run(font, r, base, name_len, text_x, top + ascent,
                      name_color);

        if (parent_len > 0 && name_len < box.max_cells - 4) {
            int path_budget = box.max_cells - name_len - 3;
            int path_len = parent_len < path_budget ? parent_len : path_budget;
            float path_x = text_x + (float)(name_len + 2) * adv;
            draw_text_run(font, r, e->rel, path_len, path_x, top + ascent,
                          selected ? (Color){0.58f, 0.64f, 0.74f}
                                   : (Color){0.43f, 0.47f, 0.55f});
        }
    }
}

void draw_search_panel(OverlayState *overlay, int fb_w, Font *font,
                       Renderer *r, float adv, float line_h, float ascent,
                       float radius) {
    ViewOverlayLayout box = view_overlay_layout((float)fb_w, 0.72f, 14,
                                                overlay->search.sel, adv,
                                                line_h);

    draw_round_rect(r, box.x, box.y, box.w, box.h, radius,
                    (Color){0.13f, 0.14f, 0.17f}, 1.0f);
    draw_round_rect(r, box.x, box.y, box.w, box.query_h, radius,
                    (Color){0.17f, 0.18f, 0.22f}, 1.0f);

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
            draw_round_rect(r, box.x + adv * 0.55f, top + 2.0f,
                            box.w - adv * 1.1f, line_h - 3.0f,
                            radius * 0.6f, (Color){0.22f, 0.30f, 0.42f},
                            1.0f);

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
                        float cur_top, float side_px, float fb_scale,
                        float radius) {
    PopoverLayout pop;
    if (!popover_layout(state, fb_w, fb_h, adv, line_h, top_pad, bar_h,
                        anchor_x, cur_top, side_px, fb_scale, &pop))
        return;
    const char *t = state->text;

    draw_round_rect(r, pop.x - pop.border, pop.y - pop.border,
                    pop.w + 2 * pop.border, pop.h + 2 * pop.border,
                    radius, (Color){0.33f, 0.36f, 0.42f}, 1.0f);
    draw_round_rect(r, pop.x, pop.y, pop.w, pop.h, radius,
                    (Color){0.16f, 0.17f, 0.20f}, 1.0f);

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
        draw_round_rect(r, pop.scrollbar_track_x, pop.y, pop.scrollbar_track_w,
                        pop.h, radius * 0.5f, (Color){0.24f, 0.26f, 0.30f},
                        1.0f);
        draw_round_rect(r, pop.scrollbar_track_x, pop.scrollbar_thumb_y,
                        pop.scrollbar_track_w, pop.scrollbar_thumb_h,
                        radius * 0.5f, (Color){0.50f, 0.54f, 0.60f},
                        1.0f);
    }
}
