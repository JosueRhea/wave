#include "view.h"

#include <stdio.h>
#include <string.h>

#include "langs.h"

/* Both front-ends measure text in bytes-as-columns (the glyph atlas is ASCII,
 * and the GPUI panel is monospace), so the elision marker has to be ASCII too —
 * a "…" would cost three columns and draw as three missing glyphs. */
#define VIEW_ELLIPSIS "..."
#define VIEW_SEARCH_LEAD 8 /* columns of the line kept before a scrolled match */

static char view_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

const char *view_base_name(const char *path) {
    if (!path) return "[scratch]";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

ViewEmptyState view_empty_state(int has_workspace) {
    if (has_workspace)
        return (ViewEmptyState){"No file open", "Click a file in the sidebar, or press Cmd-P"};
    return (ViewEmptyState){"Wave", "Open a file or folder to get started"};
}

ViewHeaderTitle view_header_title(const char *workspace_root, const char *editor_path,
                                  float fb_w, float header_h, float adv,
                                  float ascent, float fb_scale) {
    ViewHeaderTitle out = {0};
    out.title = workspace_root ? view_base_name(workspace_root)
                               : (editor_path ? view_base_name(editor_path) : "Wave");
    if (!out.title) out.title = "Wave";
    float title_w = (float)strlen(out.title) * adv;
    out.x = (fb_w - title_w) * 0.5f;
    float lights = 78.0f * fb_scale;
    if (out.x < lights) out.x = lights;
    out.baseline = (header_h - ascent) * 0.5f + ascent;
    return out;
}

ViewEmptyLayout view_empty_layout(float fb_w, float fb_h, float side_px,
                                  float top_pad, float adv, float line_h,
                                  const ViewEmptyState *empty) {
    ViewEmptyLayout layout = {0};
    if (!empty) return layout;
    float area_w = fb_w - side_px;
    float mid_y = top_pad + (fb_h - top_pad) * 0.5f;
    float title_w = (float)strlen(empty->title) * adv;
    float hint_w = (float)strlen(empty->hint) * adv;
    layout.title_x = side_px + (area_w - title_w) * 0.5f;
    layout.title_y = mid_y - line_h * 0.5f;
    layout.hint_x = side_px + (area_w - hint_w) * 0.5f;
    layout.hint_y = mid_y + line_h;
    return layout;
}

ViewOverlayLayout view_overlay_layout(float fb_w, float width_fraction,
                                      int rows, int selected, float adv,
                                      float line_h) {
    ViewOverlayLayout out = {0};
    if (rows < 0) rows = 0;
    out.rows = rows;
    out.w = fb_w * width_fraction;
    out.x = (fb_w - out.w) * 0.5f;
    out.y = line_h * 2.0f;
    out.h = line_h * (float)(rows + 2);
    out.shadow_x = out.x - 6.0f;
    out.shadow_y = out.y - 6.0f;
    out.shadow_w = out.w + 12.0f;
    out.shadow_h = out.h + 12.0f;
    out.query_h = line_h + 4.0f;
    out.result_top = out.y + out.query_h;
    out.start = selected >= rows ? selected - rows + 1 : 0;
    if (out.start < 0) out.start = 0;
    out.max_cells = (int)(out.w / adv) - 2;
    if (out.max_cells < 8) out.max_cells = 8;
    return out;
}

ViewFramePlan view_frame_plan(float side_px, float tab_strip, float header_h,
                              int editor_has_buffer, int overlay_kind) {
    ViewFramePlan plan = {0};
    plan.sidebar = side_px > 0.0f;
    plan.tabs = tab_strip > 0.0f;
    plan.header = header_h > 0.0f;
    plan.empty = !editor_has_buffer;
    plan.editor = editor_has_buffer;
    plan.popover = editor_has_buffer && overlay_kind == 0;
    if (overlay_kind == 1)
        plan.overlay = VIEW_OVERLAY_DRAW_PALETTE;
    else if (overlay_kind == 2)
        plan.overlay = VIEW_OVERLAY_DRAW_SEARCH;
    else
        plan.overlay = VIEW_OVERLAY_DRAW_NONE;
    return plan;
}

ViewPoint view_popover_anchor(float text_x, float top_pad, float fb_h,
                              float bar_h, float adv, float line_h,
                              int cursor_vrow, int cursor_xcol,
                              float scroll_y) {
    ViewPoint out;
    out.x = text_x + adv * (float)cursor_xcol;
    out.y = top_pad + (float)cursor_vrow * line_h - scroll_y;
    if (out.y < top_pad) out.y = top_pad;
    if (out.y > fb_h - bar_h) out.y = fb_h - bar_h;
    return out;
}

int view_cursor_visible(int blink, double now, double last_activity) {
    if (!blink) return 1;
    double phase = now - last_activity;
    return (phase - (double)(long)phase) < 0.5;
}

int view_clamp_text_len(const char *text, int max_cells) {
    if (!text || max_cells <= 0) return 0;
    int len = (int)strlen(text);
    return len > max_cells ? max_cells : len;
}

int view_sidebar_name_len(const WsEntry *entry, int side_cells) {
    if (!entry) return 0;
    int budget = side_cells - entry->depth - 4;
    return view_clamp_text_len(entry->name, budget);
}

int view_workspace_entry_active(const char *active_path, const WsEntry *entry) {
    if (!active_path || !entry || entry->is_dir) return 0;
    size_t pl = strlen(active_path), rl = strlen(entry->rel);
    return pl >= rl && !strcmp(active_path + pl - rl, entry->rel) &&
           (pl == rl || active_path[pl - rl - 1] == '/');
}

ViewSidebarWindow view_sidebar_window(int fb_h, float top_y, float side_pad,
                                      float scroll, float line_h) {
    ViewSidebarWindow out = {0};
    if (line_h <= 0) return out;
    out.content_top = top_y + side_pad;
    out.first = (int)(scroll / line_h);
    if (out.first < 0) out.first = 0;
    out.count = (int)(((float)fb_h - out.content_top) / line_h) + 1;
    if (out.count < 0) out.count = 0;
    return out;
}

ViewSidebarRow view_sidebar_row(const WsEntry *entry, const char *active_path,
                                int side_cells, int index, float scroll,
                                float side_pad, float top_y, float adv,
                                float line_h, float ascent) {
    ViewSidebarRow out = {0};
    if (!entry) return out;
    out.top = top_y + side_pad + (float)index * line_h - scroll;
    out.baseline = out.top + ascent;
    float row_x = side_pad + (float)entry->depth * adv * 1.3f;
    out.icon_size = line_h * 0.5f;
    out.icon_y = out.top + (line_h - out.icon_size) * 0.5f;
    out.chevron_x = row_x;
    out.icon_x = out.chevron_x + adv;
    out.name_x = out.icon_x + adv * 1.4f;
    out.chevron_size = out.icon_size * 0.7f;
    out.chevron_y = out.top + (line_h - out.chevron_size) * 0.5f;
    out.name_len = view_sidebar_name_len(entry, side_cells);
    out.active = view_workspace_entry_active(active_path, entry);
    return out;
}

void view_tab_label(const Editor *e, char *out, size_t cap) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "%s%s", view_base_name(e ? e->path : NULL),
             (e && e->modified) ? " *" : "");
}

void view_search_status(char *out, size_t cap, int unavailable, int query_len,
                        int running, int truncated, int count, int files) {
    if (!out || cap == 0) return;
    if (unavailable && query_len) {
        snprintf(out, cap, "ripgrep unavailable");
        return;
    }
    if (running) {
        snprintf(out, cap, "searching...");
        return;
    }
    if (count == 0) {
        snprintf(out, cap, "%s", query_len ? "no matches" : "");
        return;
    }
    /* Files matter as much as matches: they say whether a hundred hits are one
     * file to skim or a change that reaches across the project. */
    snprintf(out, cap, "%d%s match%s in %d file%s", count, truncated ? "+" : "",
             count == 1 ? "" : "es", files, files == 1 ? "" : "s");
}

int view_query_case_sensitive(const char *query) {
    if (!query) return 0;
    for (const char *p = query; *p; p++)
        if (*p >= 'A' && *p <= 'Z') return 1;
    return 0;
}

int view_match_offset(const char *text, const char *query) {
    if (!text || !query || !query[0]) return -1;
    size_t qn = strlen(query);
    int sensitive = view_query_case_sensitive(query);
    for (const char *p = text; *p; p++) {
        size_t i = 0;
        while (i < qn && p[i] &&
               (sensitive ? p[i] == query[i]
                          : view_lower(p[i]) == view_lower(query[i])))
            i++;
        if (i == qn) return (int)(p - text);
    }
    return -1;
}

void view_elide_left(char *out, size_t cap, const char *text, int cells) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!text || cells <= 0) return;
    int ell = (int)strlen(VIEW_ELLIPSIS);
    int len = (int)strlen(text);
    if (len <= cells) {
        snprintf(out, cap, "%s", text);
        return;
    }
    /* Too narrow to say anything useful, so say nothing rather than spend every
     * column on the marker. */
    if (cells <= ell) return;
    snprintf(out, cap, VIEW_ELLIPSIS "%s", text + len - (cells - ell));
}

/* Slide a window over `text` so the match at *ms stays inside `cells` columns,
 * marking either cut end with an ellipsis and moving *ms/*mlen with the text. */
static void view_window_match(char *out, size_t cap, const char *text, int cells,
                             int *ms, int *mlen) {
    int ell = (int)strlen(VIEW_ELLIPSIS);
    int len = (int)strlen(text);
    out[0] = '\0';
    if (cells <= 0) {
        *ms = -1;
        return;
    }
    if (len <= cells) {
        snprintf(out, cap, "%s", text);
        return;
    }

    /* Only scroll once the match would fall off the right edge, and then keep a
     * little of the line before it — a match flush against the left edge reads
     * like the start of the line, which it is not. Note we do *not* pull the
     * window back to fill the last columns: keeping VIEW_SEARCH_LEAD before the
     * match matters more than a few trailing cells. */
    int start = 0;
    if (*ms >= 0 && *ms + *mlen > cells) {
        start = *ms - VIEW_SEARCH_LEAD;
        if (start < 0) start = 0;
        if (start > len - 1) start = len - 1;
    }

    int head = start > 0 ? ell : 0;
    int take = cells - head;
    int tail = start + take < len ? ell : 0;
    take -= tail;
    if (take < 1) take = 1;
    if (take > len - start) take = len - start;

    snprintf(out, cap, "%s%.*s%s", head ? VIEW_ELLIPSIS : "", take,
             text + start, tail ? VIEW_ELLIPSIS : "");
    if (*ms >= start && *ms < start + take) {
        *ms = head + (*ms - start);
        if (*ms + *mlen > head + take) *mlen = head + take - *ms;
    } else {
        *ms = -1;
        *mlen = 0;
    }
}

ViewSearchRow view_search_row(const char *path, int line, const char *text,
                              const char *query, const char *prev_path,
                              int dir_cells, int text_cells) {
    ViewSearchRow row;
    memset(&row, 0, sizeof row);
    row.line = line;
    row.match_start = -1;
    if (!path) path = "";
    if (!text) text = "";
    row.name = view_base_name(path);
    row.repeats = prev_path && !strcmp(prev_path, path);

    const char *slash = strrchr(path, '/');
    if (slash) {
        char dir[1024];
        size_t n = (size_t)(slash - path);
        if (n >= sizeof dir) n = sizeof dir - 1;
        memcpy(dir, path, n);
        dir[n] = '\0';
        view_elide_left(row.dir, sizeof row.dir, dir, dir_cells);
    }

    row.match_start = view_match_offset(text, query);
    row.match_len = row.match_start >= 0 ? (int)strlen(query) : 0;
    if (text_cells > VIEW_SEARCH_TEXT_MAX - 1) text_cells = VIEW_SEARCH_TEXT_MAX - 1;
    view_window_match(row.text, sizeof row.text, text, text_cells,
                      &row.match_start, &row.match_len);
    return row;
}

ViewStatusKind view_status_text(char *out, size_t cap, const char *command,
                                const char *info, const char *mode,
                                const char *path, int modified,
                                const char *lang, size_t row, size_t col,
                                size_t diagnostics, int tab_index,
                                int tab_count) {
    if (!out || cap == 0) return VIEW_STATUS_NORMAL;
    if (command) {
        snprintf(out, cap, ":%s", command);
        return VIEW_STATUS_COMMAND;
    }
    if (info && info[0]) {
        snprintf(out, cap, "%s", info);
        return VIEW_STATUS_INFO;
    }
    snprintf(out, cap, "%s  %s%s  %s  Ln %zu, Col %zu  %zu errs  [%d/%d]",
             mode ? mode : "NORMAL",
             path ? path : "[scratch]",
             modified ? " *" : "",
             lang ? lang : "text",
             row + 1, col + 1, diagnostics, tab_index + 1, tab_count);
    return VIEW_STATUS_NORMAL;
}

ViewStatusLine view_status_line(char *out, size_t cap, const Editor *editor,
                                const char *command, const char *info,
                                const char *mode, size_t row, size_t col,
                                size_t diagnostics, int tab_index,
                                int tab_count) {
    ViewStatusLine line = {
        .kind = VIEW_STATUS_NORMAL,
        .lang = "text",
        .r = 0.70f,
        .g = 0.74f,
        .b = 0.80f,
    };
    const Language *lang = lang_detect(editor ? editor->path : NULL);
    if (lang) line.lang = lang->name;
    line.kind = view_status_text(out, cap, command, info, mode,
                                 editor ? editor->path : NULL,
                                 editor ? editor->modified : 0,
                                 line.lang, row, col, diagnostics,
                                 tab_index, tab_count);
    if (line.kind == VIEW_STATUS_INFO) {
        line.r = 0.86f;
        line.g = 0.84f;
        line.b = 0.55f;
    }
    return line;
}

ViewStatusLine view_editor_status_line(char *out, size_t cap, const Editor *editor,
                                       const char *command, const char *info,
                                       const char *mode, size_t diagnostics,
                                       int tab_index, int tab_count) {
    size_t row = 0, col = 0;
    if (editor && editor->buf)
        pt_offset_to_rowcol(buffer_pt(editor->buf), editor->cursor, &row, &col);
    return view_status_line(out, cap, editor, command, info, mode,
                            row, col, diagnostics, tab_index, tab_count);
}
