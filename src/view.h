#ifndef WAVE_VIEW_H
#define WAVE_VIEW_H

#include <stddef.h>

#include "editor.h"
#include "workspace.h"

typedef enum {
    VIEW_STATUS_NORMAL,
    VIEW_STATUS_COMMAND,
    VIEW_STATUS_INFO,
} ViewStatusKind;

typedef struct {
    const char *title;
    const char *hint;
} ViewEmptyState;

typedef struct {
    float title_x;
    float title_y;
    float hint_x;
    float hint_y;
} ViewEmptyLayout;

typedef struct {
    float x;
    float y;
    float w;
    float h;
    float shadow_x;
    float shadow_y;
    float shadow_w;
    float shadow_h;
    float query_h;
    float result_top;
    int rows;
    int start;
    int max_cells;
} ViewOverlayLayout;

typedef struct {
    float x;
    float y;
} ViewPoint;

typedef struct {
    ViewStatusKind kind;
    const char *lang;
    float r;
    float g;
    float b;
} ViewStatusLine;

typedef struct {
    const char *title;
    float x;
    float baseline;
} ViewHeaderTitle;

typedef struct {
    int first;
    int count;
    float content_top;
} ViewSidebarWindow;

typedef struct {
    float top;
    float baseline;
    float icon_size;
    float icon_y;
    float chevron_x;
    float chevron_y;
    float chevron_size;
    float icon_x;
    float name_x;
    int name_len;
    int active;
} ViewSidebarRow;

const char *view_base_name(const char *path);
ViewEmptyState view_empty_state(int has_workspace);
ViewHeaderTitle view_header_title(const char *workspace_root, const char *editor_path,
                                  float fb_w, float header_h, float adv,
                                  float ascent, float fb_scale);
ViewEmptyLayout view_empty_layout(float fb_w, float fb_h, float side_px,
                                  float top_pad, float adv, float line_h,
                                  const ViewEmptyState *empty);
ViewOverlayLayout view_overlay_layout(float fb_w, float width_fraction,
                                      int rows, int selected, float adv,
                                      float line_h);
ViewPoint view_popover_anchor(float text_x, float top_pad, float fb_h,
                              float bar_h, float adv, float line_h,
                              int cursor_vrow, int cursor_xcol,
                              float scroll_y);
int view_cursor_visible(int blink, double now, double last_activity);
int view_clamp_text_len(const char *text, int max_cells);
int view_sidebar_name_len(const WsEntry *entry, int side_cells);
int view_workspace_entry_active(const char *active_path, const WsEntry *entry);
ViewSidebarWindow view_sidebar_window(int fb_h, float top_y, float side_pad,
                                      float scroll, float line_h);
ViewSidebarRow view_sidebar_row(const WsEntry *entry, const char *active_path,
                                int side_cells, int index, float scroll,
                                float side_pad, float top_y, float adv,
                                float line_h, float ascent);
void view_tab_label(const Editor *e, char *out, size_t cap);
void view_search_status(char *out, size_t cap, int unavailable, int query_len,
                        int running, int truncated, int count);
ViewStatusKind view_status_text(char *out, size_t cap, const char *command,
                                const char *info, const char *mode,
                                const char *path, int modified,
                                const char *lang, size_t row, size_t col,
                                size_t diagnostics, int tab_index,
                                int tab_count);
ViewStatusLine view_status_line(char *out, size_t cap, const Editor *editor,
                                const char *command, const char *info,
                                const char *mode, size_t row, size_t col,
                                size_t diagnostics, int tab_index,
                                int tab_count);

#endif
