#include "view.h"

#include <stdio.h>
#include <string.h>

#include "langs.h"

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
                        int running, int truncated, int count) {
    if (!out || cap == 0) return;
    if (unavailable && query_len)
        snprintf(out, cap, "ripgrep unavailable");
    else if (running)
        snprintf(out, cap, "searching...");
    else if (truncated)
        snprintf(out, cap, "%d+ matches", count);
    else
        snprintf(out, cap, "%d match%s", count, count == 1 ? "" : "es");
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
