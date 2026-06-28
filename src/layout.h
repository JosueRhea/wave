#ifndef WAVE_LAYOUT_H
#define WAVE_LAYOUT_H

#include <stddef.h>

typedef struct {
    float line_h, adv, ascent;
    float side_px, text_x, gutter;
    float top_pad, tab_w, header_h, side_pad;
    int fb_h;
} LayoutState;

typedef struct {
    float side_px;
    float gutter;
    float pad;
    float text_x;
    float tab_h;
    float header_h;
    float tab_strip;
    float top_pad;
} LayoutFrameMetrics;

typedef struct {
    int seen;
    double time;
    float x;
    float y;
} LayoutPointClick;

typedef struct {
    int seen;
    double time;
    int index;
} LayoutIndexClick;

typedef struct {
    int down;
    int dragging;
    size_t anchor;
    float x;
    float y;
} LayoutDragState;

typedef enum {
    LAYOUT_SCROLL_EDITOR,
    LAYOUT_SCROLL_SIDEBAR,
    LAYOUT_SCROLL_POPOVER,
    LAYOUT_SCROLL_SEARCH
} LayoutScrollTargetKind;

typedef struct {
    LayoutScrollTargetKind kind;
    int units;
    float pixels;
} LayoutScrollTarget;

typedef enum {
    LAYOUT_CLICK_NONE,
    LAYOUT_CLICK_TITLEBAR,
    LAYOUT_CLICK_SIDEBAR,
    LAYOUT_CLICK_TAB,
    LAYOUT_CLICK_TEXT
} LayoutClickKind;

typedef struct {
    LayoutClickKind kind;
    int row;
    int tab_index;
    int tab_close;
    int titlebar_right;
} LayoutClickTarget;

void layout_set(LayoutState *l, float line_h, float adv, float ascent,
                float side_px, float text_x, float gutter, float top_pad,
                float header_h, int fb_h);
LayoutFrameMetrics layout_frame_metrics(LayoutState *l, float line_h, float adv,
                                        float ascent, int fb_h,
                                        int has_workspace, int show_sidebar,
                                        int side_cells, int native_titlebar,
                                        float fb_scale, int tab_count);
float layout_status_bar_h(const LayoutState *l);
int layout_sidebar_row(const LayoutState *l, float y, float scroll);
int layout_tab_index(const LayoutState *l, float x);
int layout_tab_close_hit(const LayoutState *l, int index, float x);
int layout_drag_should_start(float start_x, float start_y, float x, float y, float scale);
void layout_drag_begin(LayoutDragState *drag, size_t anchor, float x, float y);
void layout_drag_release(LayoutDragState *drag);
int layout_drag_update(LayoutDragState *drag, float x, float y, float scale);
int layout_point_double_click(LayoutPointClick *click, double now, float x, float y,
                              double max_delay, float max_distance);
int layout_index_double_click(LayoutIndexClick *click, double now, int index,
                              double max_delay);
LayoutScrollTarget layout_scroll_target(const LayoutState *l, float mouse_x,
                                        double dy, int search_active,
                                        int popover_scrollable,
                                        int workspace_open,
                                        int sidebar_visible);
float layout_scroll_offset(float current, float pixels);
int layout_in_titlebar(const LayoutState *l, float y);
int layout_in_sidebar(const LayoutState *l, float x, int workspace_open, int sidebar_visible);
int layout_in_tab_strip(const LayoutState *l, float x, float y);
int layout_in_text_area(const LayoutState *l, float x);
float layout_text_autoscroll_delta(const LayoutState *l, float y);
LayoutClickTarget layout_click_target(const LayoutState *l, float x, float y,
                                      float sidebar_scroll, int right_button,
                                      int workspace_open, int sidebar_visible);

#endif
