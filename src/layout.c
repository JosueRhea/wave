#include "layout.h"

void layout_set(LayoutState *l, float line_h, float adv, float ascent,
                float side_px, float text_x, float gutter, float top_pad,
                float header_h, int fb_h) {
    if (!l) return;
    l->line_h = line_h;
    l->adv = adv;
    l->ascent = ascent;
    l->side_px = side_px;
    l->text_x = text_x;
    l->gutter = gutter;
    l->top_pad = top_pad;
    l->header_h = header_h;
    l->fb_h = fb_h;
    l->side_pad = line_h * 0.4f;
}

LayoutFrameMetrics layout_frame_metrics(LayoutState *l, float line_h, float adv,
                                        float ascent, int fb_h,
                                        int has_workspace, int show_sidebar,
                                        int side_cells, int native_titlebar,
                                        float fb_scale, int tab_count) {
    LayoutFrameMetrics m = {0};
    m.side_px = (has_workspace && show_sidebar) ? adv * (float)side_cells : 0.0f;
    m.gutter = adv * 5.0f;
    m.pad = adv;
    m.text_x = m.side_px + m.gutter + m.pad;
    m.tab_h = line_h + 6.0f;
    m.header_h = native_titlebar ? (float)(int)(28.0f * fb_scale + 0.5f) : 0.0f;
    m.tab_strip = (tab_count > 0) ? m.tab_h : 0.0f;
    m.top_pad = m.header_h + m.tab_strip;
    layout_set(l, line_h, adv, ascent, m.side_px, m.text_x, m.gutter,
               m.top_pad, m.header_h, fb_h);
    return m;
}

float layout_status_bar_h(const LayoutState *l) {
    return l ? l->line_h + 6.0f : 0.0f;
}

int layout_sidebar_row(const LayoutState *l, float y, float scroll) {
    if (!l || l->line_h <= 0) return -1;
    float pos = (y - l->header_h - l->side_pad + scroll) / l->line_h;
    if (pos < 0) return -1;
    return (int)pos;
}

int layout_tab_index(const LayoutState *l, float x) {
    if (!l || l->tab_w <= 0 || x < l->side_px) return -1;
    return (int)((x - l->side_px) / l->tab_w);
}

int layout_tab_close_hit(const LayoutState *l, int index, float x) {
    if (!l || index < 0 || l->tab_w <= 0) return 0;
    float tx = l->side_px + (float)index * l->tab_w;
    return x > tx + l->tab_w - l->adv * 1.8f;
}

int layout_drag_should_start(float start_x, float start_y, float x, float y, float scale) {
    float dx = x - start_x;
    float dy = y - start_y;
    float threshold = 3.0f * (scale > 0 ? scale : 1.0f);
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx >= threshold || dy >= threshold;
}

int layout_point_double_click(LayoutPointClick *click, double now, float x, float y,
                              double max_delay, float max_distance) {
    if (!click) return 0;
    float dx = x - click->x;
    float dy = y - click->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int dbl = click->seen && now - click->time < max_delay &&
              dx < max_distance && dy < max_distance;
    click->seen = 1;
    click->time = now;
    click->x = x;
    click->y = y;
    return dbl;
}

int layout_index_double_click(LayoutIndexClick *click, double now, int index,
                              double max_delay) {
    if (!click) return 0;
    int dbl = click->seen && now - click->time < max_delay &&
              index == click->index;
    click->seen = 1;
    click->time = now;
    click->index = index;
    return dbl;
}

LayoutScrollTarget layout_scroll_target(const LayoutState *l, float mouse_x,
                                        double dy, int search_active,
                                        int popover_scrollable,
                                        int workspace_open,
                                        int sidebar_visible) {
    LayoutScrollTarget out = {LAYOUT_SCROLL_EDITOR, -(int)dy, 0.0f};
    if (l) out.pixels = -(float)dy * l->line_h * 3.0f;
    if (search_active) {
        out.kind = LAYOUT_SCROLL_SEARCH;
        return out;
    }
    if (popover_scrollable) {
        out.kind = LAYOUT_SCROLL_POPOVER;
        return out;
    }
    if (layout_in_sidebar(l, mouse_x, workspace_open, sidebar_visible)) {
        out.kind = LAYOUT_SCROLL_SIDEBAR;
        return out;
    }
    return out;
}

int layout_in_titlebar(const LayoutState *l, float y) {
    return l && y < l->header_h;
}

int layout_in_sidebar(const LayoutState *l, float x, int workspace_open, int sidebar_visible) {
    return l && workspace_open && sidebar_visible && x < l->side_px;
}

int layout_in_tab_strip(const LayoutState *l, float x, float y) {
    return l && l->top_pad > 0 && y < l->top_pad && x >= l->side_px && l->tab_w > 0;
}

int layout_in_text_area(const LayoutState *l, float x) {
    return l && x >= l->text_x;
}

float layout_text_autoscroll_delta(const LayoutState *l, float y) {
    if (!l) return 0.0f;
    if (y < l->top_pad) return -l->line_h;
    if (y > (float)l->fb_h - layout_status_bar_h(l)) return l->line_h;
    return 0.0f;
}

LayoutClickTarget layout_click_target(const LayoutState *l, float x, float y,
                                      float sidebar_scroll, int right_button,
                                      int workspace_open, int sidebar_visible) {
    LayoutClickTarget out = {LAYOUT_CLICK_NONE, -1, -1, 0, 0};
    if (!l) return out;

    if (layout_in_titlebar(l, y)) {
        out.kind = LAYOUT_CLICK_TITLEBAR;
        out.titlebar_right = right_button != 0;
        return out;
    }

    if (layout_in_sidebar(l, x, workspace_open, sidebar_visible)) {
        int row = layout_sidebar_row(l, y, sidebar_scroll);
        if (row < 0) return out;
        out.kind = LAYOUT_CLICK_SIDEBAR;
        out.row = row;
        return out;
    }

    if (layout_in_tab_strip(l, x, y)) {
        out.kind = LAYOUT_CLICK_TAB;
        out.tab_index = layout_tab_index(l, x);
        out.tab_close = layout_tab_close_hit(l, out.tab_index, x);
        return out;
    }

    if (layout_in_text_area(l, x))
        out.kind = LAYOUT_CLICK_TEXT;

    return out;
}
