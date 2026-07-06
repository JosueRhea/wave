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

static int draw_utf8_codepoint(const char *s, size_t n, unsigned int *cp);

void draw_text_run(Font *f, Renderer *r, const char *s, int n, float x,
                   float y, Color c) {
    for (int i = 0; i < n;) {
        unsigned int cp = 0;
        int used = draw_utf8_codepoint(s + i, (size_t)(n - i), &cp);
        if (used <= 0) used = 1;
        float x0, y0, x1, y1, s0, t0, s1, t1;
        if (font_quad(f, (int)cp, &x, &y, &x0, &y0, &x1, &y1, &s0,
                      &t0, &s1, &t1))
            renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1, c.r, c.g, c.b, 1.0f);
        i += used;
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

static void draw_clipped_text(Font *font, Renderer *r, const char *s,
                              float x, float y, float max_w, float adv,
                              Color c) {
    if (!s || max_w <= 0.0f || adv <= 0.0f) return;
    int max_n = (int)(max_w / adv);
    int n = (int)strlen(s);
    if (n > max_n) n = max_n;
    if (n > 0) draw_text_run(font, r, s, n, x, y, c);
}

static Color draw_terminal_color(TerminalColor c) {
    return (Color){c.r, c.g, c.b};
}

static int draw_utf8_codepoint(const char *s, size_t n, unsigned int *cp) {
    if (!s || !n || !cp) return 0;
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) {
        *cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0 && n >= 2) {
        *cp = ((unsigned int)(c0 & 0x1F) << 6) |
              (unsigned int)(((unsigned char)s[1]) & 0x3F);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && n >= 3) {
        *cp = ((unsigned int)(c0 & 0x0F) << 12) |
              ((unsigned int)(((unsigned char)s[1]) & 0x3F) << 6) |
              (unsigned int)(((unsigned char)s[2]) & 0x3F);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && n >= 4) {
        *cp = ((unsigned int)(c0 & 0x07) << 18) |
              ((unsigned int)(((unsigned char)s[1]) & 0x3F) << 12) |
              ((unsigned int)(((unsigned char)s[2]) & 0x3F) << 6) |
              (unsigned int)(((unsigned char)s[3]) & 0x3F);
        return 4;
    }
    *cp = '?';
    return 1;
}

static int draw_terminal_box_glyph(Renderer *r, unsigned int cp, float x,
                                   float top, float adv, float line_h,
                                   Color c) {
    if (cp < 0x2500 || cp > 0x257F) return 0;

    float thin = adv * 0.12f;
    if (thin < 1.25f) thin = 1.25f;
    if (thin > 2.5f) thin = 2.5f;
    float thick = thin * 1.55f;
    float t = (cp == 0x2501 || cp == 0x2503) ? thick : thin;
    float mid_x = x + adv * 0.5f;
    float mid_y = top + line_h * 0.5f;
    int left = 0, right = 0, up = 0, down = 0;

    switch (cp) {
    case 0x2500: case 0x2501: case 0x2550:
        left = right = 1;
        break;
    case 0x2502: case 0x2503: case 0x2551:
        up = down = 1;
        break;
    case 0x250C: case 0x250F: case 0x2554: case 0x256D:
        right = down = 1;
        break;
    case 0x2510: case 0x2513: case 0x2557: case 0x256E:
        left = down = 1;
        break;
    case 0x2514: case 0x2517: case 0x255A: case 0x2570:
        right = up = 1;
        break;
    case 0x2518: case 0x251B: case 0x255D: case 0x256F:
        left = up = 1;
        break;
    case 0x251C: case 0x2523: case 0x2560:
        up = down = right = 1;
        break;
    case 0x2524: case 0x252B: case 0x2563:
        up = down = left = 1;
        break;
    case 0x252C: case 0x2533: case 0x2566:
        left = right = down = 1;
        break;
    case 0x2534: case 0x253B: case 0x2569:
        left = right = up = 1;
        break;
    case 0x253C: case 0x254B: case 0x256C:
        left = right = up = down = 1;
        break;
    default:
        return 0;
    }

    if (left)
        renderer_rect(r, x, mid_y - t * 0.5f, adv * 0.5f + t * 0.5f, t,
                      c.r, c.g, c.b, 1.0f);
    if (right)
        renderer_rect(r, mid_x - t * 0.5f, mid_y - t * 0.5f,
                      adv * 0.5f + t * 0.5f, t, c.r, c.g, c.b, 1.0f);
    if (up)
        renderer_rect(r, mid_x - t * 0.5f, top, t,
                      line_h * 0.5f + t * 0.5f, c.r, c.g, c.b, 1.0f);
    if (down)
        renderer_rect(r, mid_x - t * 0.5f, mid_y - t * 0.5f, t,
                      line_h * 0.5f + t * 0.5f, c.r, c.g, c.b, 1.0f);
    return 1;
}

static int draw_terminal_block_glyph(Renderer *r, unsigned int cp, float x,
                                     float top, float adv, float line_h,
                                     Color c) {
    switch (cp) {
    case 0x2588:
        renderer_rect(r, x, top, adv, line_h, c.r, c.g, c.b, 1.0f);
        return 1;
    case 0x2580:
        renderer_rect(r, x, top, adv, line_h * 0.5f, c.r, c.g, c.b, 1.0f);
        return 1;
    case 0x2584:
        renderer_rect(r, x, top + line_h * 0.5f, adv, line_h * 0.5f,
                      c.r, c.g, c.b, 1.0f);
        return 1;
    case 0x258C:
        renderer_rect(r, x, top, adv * 0.5f, line_h, c.r, c.g, c.b, 1.0f);
        return 1;
    case 0x2590:
        renderer_rect(r, x + adv * 0.5f, top, adv * 0.5f, line_h,
                      c.r, c.g, c.b, 1.0f);
        return 1;
    default:
        return 0;
    }
}

static void draw_cell_triangle_right(Renderer *r, float x, float top,
                                     float adv, float line_h, Color c) {
    int slices = 9;
    float left = x + adv * 0.24f;
    float width = adv * 0.54f;
    float mid = top + line_h * 0.5f;
    for (int i = 0; i < slices; i++) {
        float t = ((float)(slices - i) - 0.5f) / (float)slices;
        float h = line_h * 0.58f * t;
        float sx = left + width * (float)i / (float)slices;
        renderer_rect(r, sx, mid - h * 0.5f, width / (float)slices + 0.75f, h,
                      c.r, c.g, c.b, 1.0f);
    }
}

static void draw_cell_triangle_left(Renderer *r, float x, float top,
                                    float adv, float line_h, Color c) {
    int slices = 9;
    float left = x + adv * 0.22f;
    float width = adv * 0.54f;
    float mid = top + line_h * 0.5f;
    for (int i = 0; i < slices; i++) {
        float t = ((float)i + 0.5f) / (float)slices;
        float h = line_h * 0.58f * t;
        float sx = left + width * (float)i / (float)slices;
        renderer_rect(r, sx, mid - h * 0.5f, width / (float)slices + 0.75f, h,
                      c.r, c.g, c.b, 1.0f);
    }
}

static void draw_cell_angle_right(Renderer *r, float x, float top,
                                  float adv, float line_h, Color c) {
    float thick = adv * 0.12f;
    if (thick < 1.2f) thick = 1.2f;
    if (thick > 2.3f) thick = 2.3f;
    float left = x + adv * 0.35f;
    float tip = x + adv * 0.66f;
    float mid = top + line_h * 0.50f;
    float upper = top + line_h * 0.34f;
    float lower = top + line_h * 0.66f;
    int steps = 6;
    for (int i = 0; i < steps; i++) {
        float t = (float)i / (float)(steps - 1);
        float ux = left + (tip - left) * t;
        float uy = upper + (mid - upper) * t;
        float lx = left + (tip - left) * t;
        float ly = lower + (mid - lower) * t;
        renderer_rect(r, ux, uy - thick * 0.5f, thick, thick,
                      c.r, c.g, c.b, 1.0f);
        renderer_rect(r, lx, ly - thick * 0.5f, thick, thick,
                      c.r, c.g, c.b, 1.0f);
    }
}

static void draw_cell_angle_left(Renderer *r, float x, float top,
                                 float adv, float line_h, Color c) {
    float thick = adv * 0.12f;
    if (thick < 1.2f) thick = 1.2f;
    if (thick > 2.3f) thick = 2.3f;
    float left = x + adv * 0.34f;
    float right = x + adv * 0.65f;
    float mid = top + line_h * 0.50f;
    float upper = top + line_h * 0.34f;
    float lower = top + line_h * 0.66f;
    int steps = 6;
    for (int i = 0; i < steps; i++) {
        float t = (float)i / (float)(steps - 1);
        float ux = right + (left - right) * t;
        float uy = upper + (mid - upper) * t;
        float lx = right + (left - right) * t;
        float ly = lower + (mid - lower) * t;
        renderer_rect(r, ux, uy - thick * 0.5f, thick, thick,
                      c.r, c.g, c.b, 1.0f);
        renderer_rect(r, lx, ly - thick * 0.5f, thick, thick,
                      c.r, c.g, c.b, 1.0f);
    }
}

static int draw_terminal_angle_glyph(Renderer *r, unsigned int cp, float x,
                                     float top, float adv, float line_h,
                                     Color c) {
    switch (cp) {
    case 0x203A: /* single right-pointing angle quotation mark */
    case 0x276F: /* heavy right-pointing angle quotation mark */
        draw_cell_angle_right(r, x, top, adv, line_h, c);
        return 1;
    case 0x2039: /* single left-pointing angle quotation mark */
    case 0x276E: /* heavy left-pointing angle quotation mark */
        draw_cell_angle_left(r, x, top, adv, line_h, c);
        return 1;
    default:
        return 0;
    }
}

static int draw_terminal_playback_glyph(Renderer *r, unsigned int cp, float x,
                                        float top, float adv, float line_h,
                                        Color c) {
    float cy = top + line_h * 0.5f;
    switch (cp) {
    case 0x23F5: /* black medium right-pointing triangle */
        draw_cell_triangle_right(r, x, top, adv, line_h, c);
        return 1;
    case 0x23F4: /* black medium left-pointing triangle */
        draw_cell_triangle_left(r, x, top, adv, line_h, c);
        return 1;
    case 0x23F8: { /* double vertical bar */
        float w = adv * 0.16f;
        float h = line_h * 0.58f;
        renderer_rect(r, x + adv * 0.30f, cy - h * 0.5f, w, h,
                      c.r, c.g, c.b, 1.0f);
        renderer_rect(r, x + adv * 0.54f, cy - h * 0.5f, w, h,
                      c.r, c.g, c.b, 1.0f);
        return 1;
    }
    case 0x23F9: { /* black square for stop */
        float s = adv * 0.55f;
        renderer_rect(r, x + (adv - s) * 0.5f, cy - s * 0.5f, s, s,
                      c.r, c.g, c.b, 1.0f);
        return 1;
    }
    case 0x23FA: { /* black circle for record */
        float s = adv * 0.52f;
        int rows = 9;
        for (int i = 0; i < rows; i++) {
            float yy = ((float)i + 0.5f) / (float)rows * 2.0f - 1.0f;
            float ww = s * (1.0f - yy * yy);
            renderer_rect(r, x + (adv - ww) * 0.5f,
                          cy - s * 0.5f + s * (float)i / (float)rows,
                          ww, s / (float)rows + 0.75f,
                          c.r, c.g, c.b, 1.0f);
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int draw_terminal_prompt_marker_line(const char *s, size_t n) {
    if (!s || n == 0) return 0;
    int saw_marker = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == ' ') continue;
        if (s[i] == '?' && !saw_marker) {
            saw_marker = 1;
            continue;
        }
        return 0;
    }
    return saw_marker;
}

static void draw_terminal_cell_text(Font *font, Renderer *r, const char *s,
                                    size_t n, float x, float y, float top,
                                    float adv, float line_h, Color c,
                                    int prompt_marker) {
    if (!s || n == 0) return;
    unsigned int cp = 0;
    (void)draw_utf8_codepoint(s, n, &cp);
    if (prompt_marker && cp == '?') cp = 0x203A;
    if (draw_terminal_box_glyph(r, cp, x, top, adv, line_h, c)) return;
    if (draw_terminal_block_glyph(r, cp, x, top, adv, line_h, c)) return;
    if (draw_terminal_angle_glyph(r, cp, x, top, adv, line_h, c)) return;
    if (draw_terminal_playback_glyph(r, cp, x, top, adv, line_h, c)) return;
    float pen_x = x;
    float pen_y = y;
    float x0, y0, x1, y1, s0, t0, s1, t1;
    if (font_quad(font, (int)cp, &pen_x, &pen_y, &x0, &y0, &x1, &y1, &s0,
                  &t0, &s1, &t1))
        renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1,
                       c.r, c.g, c.b, 1.0f);
}

static void draw_terminal_plain_text(Font *font, Renderer *r, const char *s,
                                     size_t n, float x, float y, float top,
                                     float max_w, float adv, float line_h,
                                     Color c, int prompt_marker) {
    if (!s || n == 0 || max_w <= 0.0f || adv <= 0.0f) return;
    size_t max_cols = (size_t)(max_w / adv);
    size_t col = 0;
    for (size_t i = 0; i < n && col < max_cols;) {
        unsigned int cp = 0;
        int used = draw_utf8_codepoint(s + i, n - i, &cp);
        if (used <= 0) used = 1;
        if (prompt_marker && cp != '?') {
            i += (size_t)used;
            col++;
            continue;
        }
        draw_terminal_cell_text(font, r, s + i, (size_t)used,
                                x + adv * (float)col, y, top, adv, line_h,
                                c, prompt_marker);
        if (prompt_marker) return;
        i += (size_t)used;
        col++;
    }
}

static void draw_terminal_line(Font *font, Renderer *r, const Terminal *term,
                               size_t line_index, float x, float y,
                               float top, float max_w, float adv,
                               float line_h, Color default_fg) {
    const char *s = terminal_line(term, line_index);
    if (!s || max_w <= 0.0f || adv <= 0.0f) return;
    size_t max_cols = (size_t)(max_w / adv);
    size_t n = strlen(s);

    const TerminalLineStyle *style = terminal_line_style(term, line_index);
    int prompt_marker = draw_terminal_prompt_marker_line(s, n);
    int sel_start = 0, sel_end = 0;
    if (terminal_selection_span(term, line_index, &sel_start, &sel_end)) {
        if (sel_start < 0) sel_start = 0;
        if (sel_end > (int)max_cols) sel_end = (int)max_cols;
        if (sel_end > sel_start)
            renderer_rect(r, x + adv * (float)sel_start, top,
                          adv * (float)(sel_end - sel_start), line_h,
                          0.24f, 0.34f, 0.52f, 0.88f);
    }
    if (!style || style->ncells == 0) {
        (void)max_cols;
        draw_terminal_plain_text(font, r, s, n, x, y, top, max_w, adv,
                                 line_h, default_fg, prompt_marker);
        return;
    }

    for (size_t i = 0; i < style->ncells; i++) {
        const TerminalCellStyle *cell = &style->cells[i];
        if (!cell->has_bg || cell->col_start >= max_cols) continue;
        size_t cols = cell->col_len;
        if (cell->col_start + cols > max_cols) cols = max_cols - cell->col_start;
        if (cols == 0) continue;
        Color bg = draw_terminal_color(cell->bg);
        renderer_rect(r, x + adv * (float)cell->col_start, top,
                      adv * (float)cols, line_h,
                      bg.r, bg.g, bg.b, 0.92f);
    }

    for (size_t i = 0; i < style->ncells; i++) {
        const TerminalCellStyle *cell = &style->cells[i];
        if (cell->col_start >= max_cols || cell->byte_start >= n ||
            cell->byte_len == 0)
            continue;
        Color fg = cell->has_fg ? draw_terminal_color(cell->fg) : default_fg;
        draw_terminal_cell_text(font, r, s + cell->byte_start, cell->byte_len,
                                x + adv * (float)cell->col_start, y, top,
                                adv, line_h, fg, prompt_marker);
    }
}

void draw_terminal_panel(const Terminal *term, int focused, float x, float y,
                         float w, float h, Font *font, Renderer *r,
                         float adv, float line_h, float ascent, float opacity) {
    if (!term || w <= 0.0f || h <= 0.0f) return;
    renderer_rect(r, x, y, w, h, 0.075f, 0.085f, 0.105f, opacity);

    float pad = 14.0f;
    char title[192];
    snprintf(title, sizeof title, "%s  %s%s", term->title[0] ? term->title : "Terminal",
             terminal_status(term), focused ? "  input" : "");
    draw_clipped_text(font, r, title, x + pad, y + ascent + 10.0f,
                      w - pad * 2.0f, adv,
                      focused ? (Color){0.80f, 0.88f, 1.00f}
                              : (Color){0.62f, 0.68f, 0.76f});

    float body_y = y + line_h + 20.0f;
    float bottom = y + h - 8.0f;
    int rows = (int)((bottom - body_y) / line_h);
    if (rows < 1) return;
    size_t start = terminal_visible_start(term, rows);
    size_t total = term->nlines + (term->current_len ? 1u : 0u);
    size_t end = start + (size_t)rows;
    if (end > total) end = total;
    float row_y = body_y + ascent;
    float row_top = body_y;
    for (size_t i = start; i < end; i++) {
        draw_terminal_line(font, r, term, i, x + pad, row_y, row_top,
                           w - pad * 2.0f, adv, line_h,
                           (Color){0.84f, 0.86f, 0.88f});
        row_y += line_h;
        row_top += line_h;
    }
    if (focused && term->running) {
        float cx = x + pad;
        float cy = row_y - ascent + 2.0f;
        if (term->ghostty_enabled && term->cursor_visible) {
            cx = x + pad + adv * (float)term->cursor_col;
            int visible_row = term->cursor_row - (int)start;
            cy = body_y + line_h * (float)visible_row + 2.0f;
        }
        if (cy >= body_y && cy < bottom && cx < x + w - pad)
            renderer_rect(r, cx, cy, adv * 0.7f, line_h - 4.0f,
                          0.52f, 0.70f, 1.0f, 0.75f);
    }
}

static Color git_line_color(const char *line) {
    if (!line) return (Color){0.68f, 0.72f, 0.80f};
    if (!strncmp(line, "diff --git", 10) || !strncmp(line, "index ", 6) ||
        !strncmp(line, "@@", 2))
        return (Color){0.48f, 0.68f, 0.96f};
    if (line[0] == '+') return (Color){0.48f, 0.84f, 0.56f};
    if (line[0] == '-') return (Color){0.94f, 0.44f, 0.42f};
    return (Color){0.70f, 0.74f, 0.82f};
}

static int git_parse_hunk(const char *line, int *old_line, int *new_line) {
    int old_start = 0, old_len = 0, new_start = 0, new_len = 0;
    if (!line || strncmp(line, "@@ ", 3)) return 0;
    if (sscanf(line, "@@ -%d,%d +%d,%d", &old_start, &old_len,
               &new_start, &new_len) == 4 ||
        sscanf(line, "@@ -%d +%d,%d", &old_start, &new_start, &new_len) == 3 ||
        sscanf(line, "@@ -%d,%d +%d", &old_start, &old_len, &new_start) == 3 ||
        sscanf(line, "@@ -%d +%d", &old_start, &new_start) == 2) {
        if (old_line) *old_line = old_start;
        if (new_line) *new_line = new_start;
        return 1;
    }
    return 0;
}

static void draw_git_line_number(Font *font, Renderer *r, int value,
                                 float x, float y, float adv, Color c) {
    if (value <= 0) return;
    char text[16];
    snprintf(text, sizeof text, "%d", value);
    int len = (int)strlen(text);
    draw_text_run(font, r, text, len, x - adv * (float)len, y, c);
}

static void draw_git_diff_row(Font *font, Renderer *r, const char *line,
                              int old_no, int new_no, float x, float y,
                              float w, float line_h, float adv,
                              float ascent, float opacity) {
    int is_add = line && line[0] == '+' && strncmp(line, "+++", 3);
    int is_del = line && line[0] == '-' && strncmp(line, "---", 3);
    int is_hunk = line && !strncmp(line, "@@", 2);
    int is_meta = line && (!strncmp(line, "diff --git", 10) ||
                           !strncmp(line, "index ", 6) ||
                           !strncmp(line, "--- ", 4) ||
                           !strncmp(line, "+++ ", 4));

    float old_w = adv * 5.0f;
    float new_w = adv * 5.0f;
    float mark_w = adv * 2.3f;
    float content_x = x + old_w + new_w + mark_w;
    float bg_y = y - ascent - 2.0f;
    if (is_add)
        renderer_rect(r, x, bg_y, w, line_h, 0.12f, 0.28f, 0.17f, opacity);
    else if (is_del)
        renderer_rect(r, x, bg_y, w, line_h, 0.30f, 0.13f, 0.15f, opacity);
    else if (is_hunk)
        renderer_rect(r, x, bg_y, w, line_h, 0.11f, 0.17f, 0.24f, opacity);

    renderer_rect(r, x + old_w - 1.0f, bg_y, 1.0f, line_h,
                  0.20f, 0.23f, 0.28f, opacity * 0.75f);
    renderer_rect(r, x + old_w + new_w - 1.0f, bg_y, 1.0f, line_h,
                  0.20f, 0.23f, 0.28f, opacity * 0.75f);

    Color gutter = (Color){0.52f, 0.57f, 0.65f};
    draw_git_line_number(font, r, old_no, x + old_w - adv * 0.7f, y, adv, gutter);
    draw_git_line_number(font, r, new_no, x + old_w + new_w - adv * 0.7f, y, adv, gutter);

    const char *text = line ? line : "";
    const char *body = text;
    char mark = ' ';
    if (is_add || is_del) {
        mark = text[0];
        body = text + 1;
    }
    if (mark != ' ')
        draw_text_run(font, r, &mark, 1, x + old_w + new_w + adv * 0.7f,
                      y, is_add ? (Color){0.50f, 0.92f, 0.60f}
                                : (Color){0.98f, 0.50f, 0.50f});

    Color text_color = is_add ? (Color){0.62f, 0.96f, 0.68f}
                     : is_del ? (Color){1.00f, 0.62f, 0.62f}
                     : is_hunk ? (Color){0.56f, 0.72f, 0.96f}
                     : is_meta ? (Color){0.45f, 0.53f, 0.66f}
                     : git_line_color(text);
    int text_len = view_clamp_text_len(body, (int)((w - (content_x - x) - adv) / adv));
    draw_text_run(font, r, body, text_len, content_x, y, text_color);
}

void draw_git_panel(const GitView *git, float x, float y, float w, float h,
                    Font *font, Renderer *r, float adv, float line_h,
                    float ascent, float opacity, float radius) {
    renderer_rect(r, x, y, w, h, 0.075f, 0.085f, 0.105f, opacity);
    float pad = adv * 1.4f;
    float split_w = w * 0.34f;
    if (split_w < adv * 28.0f) split_w = adv * 28.0f;
    if (split_w > w * 0.48f) split_w = w * 0.48f;
    float right_x = x + split_w;
    renderer_rect(r, right_x, y, 1.0f, h, 0.17f, 0.19f, 0.23f, opacity);

    const char *title = "Git";
    draw_text_run(font, r, title, (int)strlen(title),
                  x + pad, y + ascent + line_h * 0.55f,
                  (Color){0.93f, 0.94f, 0.97f});
    if (git && git->repo[0]) {
        int repo_len = view_clamp_text_len(git->repo, (int)((split_w - pad * 2.0f) / adv));
        draw_text_run(font, r, git->repo, repo_len,
                      x + pad, y + ascent + line_h * 1.65f,
                      (Color){0.50f, 0.56f, 0.66f});
    }

    float row_y = y + line_h * 3.0f;
    if (!git) return;
    if (git->mode == GIT_VIEW_REPO_SELECT) {
        const char *hint = git->repo_count ? "Select repository" : "No child git repositories found";
        draw_text_run(font, r, hint, (int)strlen(hint), x + pad,
                      row_y, (Color){0.68f, 0.73f, 0.82f});
        row_y += line_h * 1.25f;
        for (int i = 0; i < git->repo_count; i++) {
            float iy = row_y + (float)i * line_h * 1.35f;
            if (iy > y + h - line_h) break;
            int selected = i == git->selected_repo;
            if (selected)
                draw_round_rect(r, x + pad * 0.6f, iy - ascent,
                                split_w - pad * 1.2f, line_h * 1.15f,
                                radius, (Color){0.16f, 0.19f, 0.25f}, 1.0f);
            int label_len = view_clamp_text_len(git->repos[i].label,
                                                (int)((split_w - pad * 2.0f) / adv));
            draw_text_run(font, r, git->repos[i].label, label_len, x + pad, iy,
                          selected ? (Color){0.92f, 0.94f, 0.98f}
                                   : (Color){0.62f, 0.67f, 0.76f});
        }
        return;
    }

    const char *changes = "Changes";
    draw_text_run(font, r, changes, (int)strlen(changes), x + pad, row_y,
                  (Color){0.78f, 0.82f, 0.90f});
    row_y += line_h * 1.2f;
    if (git->file_count == 0) {
        const char *clean = "Working tree clean";
        draw_text_run(font, r, clean, (int)strlen(clean), x + pad, row_y,
                      (Color){0.50f, 0.56f, 0.64f});
    }
    int max_change_rows = (int)((h * 0.52f) / line_h);
    for (int i = git->scroll; i < git->file_count && i < git->scroll + max_change_rows; i++) {
        float iy = row_y + (float)(i - git->scroll) * line_h * 1.25f;
        int selected = i == git->selected_file;
        if (selected)
            draw_round_rect(r, x + pad * 0.55f, iy - ascent,
                            split_w - pad * 1.1f, line_h * 1.10f,
                            radius, (Color){0.16f, 0.19f, 0.25f}, 1.0f);
        Color code_color = git->files[i].code[0] == '?' ? (Color){0.82f, 0.68f, 0.42f}
                         : git->files[i].code[0] != ' ' ? (Color){0.48f, 0.84f, 0.56f}
                         : (Color){0.72f, 0.76f, 0.84f};
        draw_text_run(font, r, git->files[i].code, (int)strlen(git->files[i].code),
                      x + pad, iy, code_color);
        int path_len = view_clamp_text_len(git->files[i].path,
                                           (int)((split_w - pad * 4.0f) / adv));
        draw_text_run(font, r, git->files[i].path, path_len, x + pad + adv * 3.2f,
                      iy, selected ? (Color){0.92f, 0.94f, 0.98f}
                                   : (Color){0.62f, 0.67f, 0.76f});
    }

    float msg_y = y + h - line_h * 5.1f;
    const char *commit_title = git->mode == GIT_VIEW_COMMIT_INPUT ? "Commit message" : "Recent commits";
    draw_text_run(font, r, commit_title, (int)strlen(commit_title), x + pad, msg_y,
                  (Color){0.78f, 0.82f, 0.90f});
    msg_y += line_h * 1.25f;
    if (git->mode == GIT_VIEW_COMMIT_INPUT) {
        draw_round_rect(r, x + pad * 0.6f, msg_y - ascent, split_w - pad * 1.2f,
                        line_h * 1.45f, radius, (Color){0.12f, 0.14f, 0.17f}, 1.0f);
        const char *message = git->message[0] ? git->message : "Type message, Enter to commit";
        int ml = view_clamp_text_len(message, (int)((split_w - pad * 2.4f) / adv));
        draw_text_run(font, r, message, ml, x + pad, msg_y + line_h * 0.15f,
                      git->message[0] ? (Color){0.92f, 0.94f, 0.98f}
                                      : (Color){0.46f, 0.51f, 0.58f});
    } else {
        int rows = 3;
        for (int i = 0; i < git->history_count && i < rows; i++) {
            int hl = view_clamp_text_len(git->history[i],
                                         (int)((split_w - pad * 2.0f) / adv));
            draw_text_run(font, r, git->history[i], hl, x + pad,
                          msg_y + (float)i * line_h * 1.15f,
                          (Color){0.50f, 0.56f, 0.64f});
        }
    }

    float rx = right_x + pad;
    float ry = y + ascent + line_h * 0.55f;
    const GitFileChange *selected = git_view_selected_file(git);
    const char *diff_title = selected ? selected->path : "Diff";
    int dl = view_clamp_text_len(diff_title, (int)((w - split_w - pad * 2.0f) / adv));
    draw_text_run(font, r, diff_title, dl, rx, ry, (Color){0.93f, 0.94f, 0.97f});
    ry += line_h * 1.35f;
    if (git->info[0]) {
        int il = view_clamp_text_len(git->info, (int)((w - split_w - pad * 2.0f) / adv));
        draw_text_run(font, r, git->info, il, rx, ry, (Color){0.52f, 0.60f, 0.72f});
        ry += line_h * 1.25f;
    }
    int visible_rows = (int)((y + h - ry - line_h * 0.5f) / line_h);
    if (visible_rows < 0) visible_rows = 0;
    int old_no = 0, new_no = 0, visible_index = 0;
    for (int i = 0; i < git->diff_count; i++) {
        const char *line = git->diff[i];
        int row_old = 0, row_new = 0;
        if (git_parse_hunk(line, &old_no, &new_no)) {
            row_old = old_no;
            row_new = new_no;
        } else if (line && line[0] == '-' && strncmp(line, "---", 3)) {
            row_old = old_no++;
        } else if (line && line[0] == '+' && strncmp(line, "+++", 3)) {
            row_new = new_no++;
        } else if (line && strncmp(line, "diff --git", 10) &&
                   strncmp(line, "index ", 6) &&
                   strncmp(line, "--- ", 4) &&
                   strncmp(line, "+++ ", 4)) {
            if (old_no > 0) row_old = old_no++;
            if (new_no > 0) row_new = new_no++;
        }
        if (i < git->diff_scroll) continue;
        if (visible_index >= visible_rows) break;
        int sel_start = 0, sel_end = 0;
        if (git_view_diff_selection_span(git, i, &sel_start, &sel_end)) {
            float old_w = adv * 5.0f;
            float new_w = adv * 5.0f;
            float mark_w = adv * 2.3f;
            float content_x = rx + old_w + new_w + mark_w;
            int max_cols = (int)((w - split_w - pad * 1.4f -
                                  (content_x - rx)) / adv);
            if (sel_start < 0) sel_start = 0;
            if (sel_end > max_cols) sel_end = max_cols;
            if (sel_end > sel_start)
                renderer_rect(r, content_x + adv * (float)sel_start,
                              ry + (float)visible_index * line_h - ascent - 2.0f,
                              adv * (float)(sel_end - sel_start), line_h,
                              0.24f, 0.34f, 0.52f, 0.82f);
        }
        draw_git_diff_row(font, r, line, row_old, row_new, rx,
                          ry + (float)visible_index * line_h,
                          w - split_w - pad * 1.4f, line_h, adv, ascent,
                          opacity);
        visible_index++;
    }
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
                      float top_y, float opacity, float radius,
                      float tab_scroll) {
    renderer_rect(r, side_px, top_y, (float)fb_w - side_px, tab_h,
                  0.075f, 0.083f, 0.102f, opacity);
    renderer_rect(r, side_px, top_y + tab_h - 1.0f, (float)fb_w - side_px, 1.0f,
                  0.16f, 0.18f, 0.22f, opacity * 0.55f);

    float tab_w = adv * 18.0f;
    float gap = adv * 0.70f;
    if (gap < 8.0f) gap = 8.0f;
    float body_w = tab_w - gap;
    float tab_y = top_y + 3.0f;
    float body_h = tab_h - 6.0f;
    float tab_radius = radius < 7.0f ? 7.0f : radius;
    for (int i = 0; i < tabs_count(tabs); i++) {
        float x = side_px + (float)i * tab_w - tab_scroll;
        if (x + body_w <= side_px) continue;
        if (x < side_px) continue;
        if (x >= (float)fb_w) break;
        int act = (i == tabs_active_index(tabs));
        draw_round_rect(r, x, tab_y, body_w, body_h,
                        tab_radius,
                        act ? (Color){0.18f, 0.20f, 0.28f}
                            : (Color){0.105f, 0.115f, 0.14f},
                        1.0f);

        char label[64];
        tabs_label(tabs, i, label, sizeof label);
        int nl = view_clamp_text_len(label, (int)(body_w / adv) - 4);
        Color c = act ? (Color){0.92f, 0.93f, 0.95f}
                      : (Color){0.60f, 0.64f, 0.72f};
        draw_text_run(font, r, label, nl, x + adv * 0.75f,
                      top_y + ascent + 4.0f, c);
        draw_text_run(font, r, "x", 1, x + body_w - adv * 1.45f,
                      top_y + ascent + 4.0f,
                      act ? (Color){0.72f, 0.76f, 0.84f}
                          : (Color){0.45f, 0.49f, 0.56f});
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

void draw_update_toast(const char *title, const char *detail, float progress,
                       int show_progress, int fb_w, int fb_h, Font *font,
                       Renderer *r, float adv, float line_h, float ascent,
                       float radius) {
    if (!title || !title[0]) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    float w = adv * 42.0f;
    if (w > (float)fb_w - adv * 2.0f) w = (float)fb_w - adv * 2.0f;
    float h = show_progress ? line_h * 4.0f : line_h * 3.25f;
    float x = (float)fb_w - w - adv * 1.4f;
    float y = (float)fb_h - h - line_h * 1.9f;
    if (x < adv) x = adv;
    if (y < line_h) y = line_h;

    draw_round_rect(r, x, y, w, h, radius, (Color){0.075f, 0.083f, 0.102f}, 0.98f);
    renderer_rect(r, x, y, 3.0f, h, 0.38f, 0.58f, 0.90f, 0.95f);

    int title_len = view_clamp_text_len(title, (int)(w / adv) - 4);
    draw_text_run(font, r, title, title_len, x + adv * 1.2f,
                  y + ascent + line_h * 0.55f, (Color){0.94f, 0.95f, 0.98f});
    if (detail && detail[0]) {
        int detail_len = view_clamp_text_len(detail, (int)(w / adv) - 4);
        draw_text_run(font, r, detail, detail_len, x + adv * 1.2f,
                      y + ascent + line_h * 1.65f, (Color){0.58f, 0.64f, 0.74f});
    }

    if (show_progress) {
        float bar_x = x + adv * 1.2f;
        float bar_y = y + h - line_h * 0.95f;
        float bar_w = w - adv * 2.4f;
        draw_round_rect(r, bar_x, bar_y, bar_w, 4.0f, 2.0f,
                        (Color){0.16f, 0.18f, 0.22f}, 1.0f);
        draw_round_rect(r, bar_x, bar_y, bar_w * progress, 4.0f, 2.0f,
                        (Color){0.38f, 0.58f, 0.90f}, 1.0f);
    }
}

void draw_recent_projects_panel(const RecentProjects *recent, int fb_w, int fb_h,
                                Font *font, Renderer *r, float adv,
                                float line_h, float ascent, float top_pad,
                                float radius) {
    float w = adv * 48.0f;
    if (w > (float)fb_w - adv * 12.0f) w = (float)fb_w - adv * 12.0f;
    if (w < adv * 30.0f) w = adv * 30.0f;
    float pad = line_h * 0.72f;
    int visible = recent ? (int)recent->filtered_count : 0;
    int max_rows = 7;
    if (visible > max_rows) visible = max_rows;
    int rows_for_height = visible ? visible : 1;
    float descent = line_h - ascent;
    if (descent < 0.0f) descent = line_h * 0.25f;
    float input_h = line_h * 1.55f;
    float title_baseline = pad + ascent;
    float title_bottom = title_baseline + descent;
    float input_y_rel = title_bottom + line_h * 0.62f;
    float row_y_rel = input_y_rel + input_h + line_h * 0.42f;
    float row_pad_y = line_h * 0.18f;
    float row_name_baseline = row_pad_y + ascent;
    float row_path_baseline = row_name_baseline + line_h * 0.78f;
    float row_h = row_path_baseline + descent + row_pad_y;
    float h = row_y_rel + (float)rows_for_height * row_h + pad;
    float x = ((float)fb_w - w) * 0.5f;
    float available_h = (float)fb_h - top_pad;
    float y = top_pad + (available_h - h) * 0.24f;
    if (y + h > (float)fb_h - line_h * 1.8f)
        y = (float)fb_h - h - line_h * 1.8f;
    if (y < top_pad + line_h) y = top_pad + line_h;

    draw_round_rect(r, x, y, w, h, radius * 1.3f,
                    (Color){0.08f, 0.09f, 0.11f}, 0.96f);
    renderer_rect(r, x, y, w, 1.0f, 0.22f, 0.25f, 0.30f, 0.70f);

    const char *title = "Recent Projects";
    draw_text_run(font, r, title, (int)strlen(title),
                  x + pad, y + title_baseline,
                  (Color){0.91f, 0.93f, 0.96f});

    float input_y = y + input_y_rel;
    draw_round_rect(r, x + pad, input_y, w - pad * 2.0f, input_h,
                    radius, (Color){0.12f, 0.14f, 0.17f}, 1.0f);
    const char *query = recent ? recent->query : "";
    const char *placeholder = "Search projects";
    Color query_color = query[0] ? (Color){0.92f, 0.94f, 0.97f}
                                 : (Color){0.48f, 0.53f, 0.60f};
    const char *search_text = query[0] ? query : placeholder;
    int search_len = view_clamp_text_len(search_text, (int)((w - pad * 3.2f) / adv));
    draw_text_run(font, r, search_text, search_len,
                  x + pad * 1.45f, input_y + (input_h - line_h) * 0.5f + ascent,
                  query_color);
    float cursor_x = x + pad * 1.45f + adv * (float)(query[0] ? search_len : 0);
    renderer_rect(r, cursor_x + adv * 0.12f, input_y + 6.0f,
                  1.4f, input_h - 12.0f, 0.78f, 0.86f, 1.0f, 1.0f);

    float row_y = y + row_y_rel;
    if (!recent || recent->count == 0) {
        const char *msg = "Open a folder to start building history";
        draw_text_run(font, r, msg, (int)strlen(msg), x + pad,
                      row_y + ascent, (Color){0.49f, 0.54f, 0.61f});
        return;
    }
    if (recent->filtered_count == 0) {
        const char *msg = "No matching projects";
        draw_text_run(font, r, msg, (int)strlen(msg), x + pad,
                      row_y + ascent, (Color){0.49f, 0.54f, 0.61f});
        return;
    }

    for (int i = 0; i < visible; i++) {
        const char *path = recent_projects_filtered_path(recent, (size_t)i);
        if (!path) continue;
        int selected = i == recent->selected;
        float ry = row_y + (float)i * row_h;
        if (selected)
            draw_round_rect(r, x + pad * 0.75f, ry,
                            w - pad * 1.5f, row_h, radius,
                            (Color){0.16f, 0.19f, 0.24f}, 1.0f);

        float icon = line_h * 0.72f;
        draw_folder_icon(r, x + pad, ry + (row_h - icon) * 0.5f, icon,
                         selected ? (Color){0.62f, 0.75f, 0.98f}
                                  : (Color){0.50f, 0.63f, 0.86f});
        const char *base = draw_path_basename(path);
        int base_len = view_clamp_text_len(base, (int)((w - pad * 4.0f) / adv));
        draw_text_run(font, r, base, base_len, x + pad + adv * 2.4f,
                      ry + row_name_baseline,
                      selected ? (Color){0.94f, 0.96f, 0.99f}
                               : (Color){0.78f, 0.82f, 0.88f});

        int parent_len = draw_path_parent_len(path, base);
        int max_parent = (int)((w - pad * 4.0f) / adv);
        if (parent_len > max_parent) parent_len = max_parent;
        draw_text_run(font, r, path, parent_len, x + pad + adv * 2.4f,
                      ry + row_path_baseline,
                      selected ? (Color){0.56f, 0.62f, 0.70f}
                               : (Color){0.42f, 0.47f, 0.54f});
    }
}
