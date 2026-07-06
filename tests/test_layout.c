/* test_layout.c — shared layout hit-test math. */
#include "test.h"
#include "layout.h"

int main(void) {
    LayoutState l = {0};
    layout_set(&l, 20.0f, 10.0f, 15.0f, 200.0f, 260.0f, 50.0f,
               52.0f, 28.0f, 720);
    l.tab_w = 180.0f;
    l.tab_gap = 8.0f;

    CHECK_EQ((int)layout_status_bar_h(&l), 26);
    CHECK_EQ(layout_sidebar_row(&l, 36.0f, 0.0f), 0);
    CHECK_EQ(layout_sidebar_row(&l, 20.0f, 0.0f), -1);
    CHECK_EQ(layout_sidebar_row(&l, 56.0f, 20.0f), 2);

    CHECK_EQ(layout_tab_index(&l, 199.0f), -1);
    CHECK_EQ(layout_tab_index(&l, 200.0f), 0);
    CHECK_EQ(layout_tab_index(&l, 381.0f), 1);
    CHECK_EQ(layout_tab_index(&l, 375.0f), -1);
    l.header_h = 20.0f;
    l.top_pad = 50.0f;
    CHECK(!layout_in_tab_strip(&l, 210.0f, 19.0f));
    CHECK(layout_in_tab_strip(&l, 210.0f, 21.0f));
    l.header_h = 28.0f;
    l.top_pad = 52.0f;
    l.tab_scroll = 180.0f;
    CHECK_EQ(layout_tab_index(&l, 200.0f), 1);
    l.tab_scroll = 0.0f;
    CHECK(!layout_tab_close_hit(&l, 0, 250.0f));
    CHECK(layout_tab_close_hit(&l, 0, 365.0f));
    CHECK(!layout_drag_should_start(10.0f, 10.0f, 12.0f, 12.0f, 1.0f));
    CHECK(layout_drag_should_start(10.0f, 10.0f, 14.0f, 12.0f, 1.0f));
    LayoutDragState drag = {0};
    CHECK(!layout_drag_update(&drag, 20.0f, 20.0f, 1.0f));
    layout_drag_begin(&drag, 123, 10.0f, 10.0f);
    CHECK(drag.down);
    CHECK(!drag.dragging);
    CHECK_EQ(drag.anchor, 123);
    CHECK(!layout_drag_update(&drag, 12.0f, 12.0f, 1.0f));
    CHECK(!drag.dragging);
    CHECK(layout_drag_update(&drag, 14.0f, 12.0f, 1.0f));
    CHECK(drag.dragging);
    CHECK(layout_drag_update(&drag, 12.0f, 12.0f, 1.0f));
    layout_drag_release(&drag);
    CHECK(!drag.down);
    CHECK(!drag.dragging);
    LayoutPointClick pc = {0};
    CHECK(!layout_point_double_click(&pc, 1.0, 20.0f, 20.0f, 0.4, 6.0f));
    CHECK(layout_point_double_click(&pc, 1.2, 24.0f, 23.0f, 0.4, 6.0f));
    CHECK(!layout_point_double_click(&pc, 1.3, 40.0f, 23.0f, 0.4, 6.0f));
    CHECK(!layout_point_double_click(&pc, 2.0, 41.0f, 24.0f, 0.4, 6.0f));
    LayoutIndexClick ic = {0};
    CHECK(!layout_index_double_click(&ic, 1.0, 3, 0.4));
    CHECK(layout_index_double_click(&ic, 1.2, 3, 0.4));
    CHECK(!layout_index_double_click(&ic, 1.3, 4, 0.4));
    CHECK(!layout_index_double_click(&ic, 2.0, 4, 0.4));
    CHECK(layout_in_titlebar(&l, 20.0f));
    CHECK(!layout_in_titlebar(&l, 30.0f));
    CHECK(layout_in_sidebar(&l, 100.0f, 1, 1));
    CHECK(!layout_in_sidebar(&l, 100.0f, 0, 1));
    CHECK(layout_in_tab_strip(&l, 220.0f, 40.0f));
    CHECK(!layout_in_tab_strip(&l, 100.0f, 40.0f));
    CHECK(layout_in_text_area(&l, 260.0f));
    CHECK(!layout_in_text_area(&l, 250.0f));
    CHECK_EQ((int)layout_text_autoscroll_delta(&l, 20.0f), -20);
    CHECK_EQ((int)layout_text_autoscroll_delta(&l, 700.0f), 20);
    CHECK_EQ((int)layout_text_autoscroll_delta(&l, 300.0f), 0);
    LayoutScrollTarget st = layout_scroll_target(&l, 100.0f, 2.0, 1, 1, 1, 1);
    CHECK_EQ(st.kind, LAYOUT_SCROLL_SEARCH);
    CHECK_EQ(st.units, -2);
    CHECK_EQ((int)st.pixels, -120);
    st = layout_scroll_target(&l, 100.0f, -1.0, 0, 1, 1, 1);
    CHECK_EQ(st.kind, LAYOUT_SCROLL_POPOVER);
    CHECK_EQ(st.units, 1);
    CHECK_EQ((int)st.pixels, 60);
    st = layout_scroll_target(&l, 100.0f, 1.0, 0, 0, 1, 1);
    CHECK_EQ(st.kind, LAYOUT_SCROLL_SIDEBAR);
    CHECK_EQ((int)st.pixels, -60);
    st = layout_scroll_target(&l, 300.0f, 1.0, 0, 0, 1, 1);
    CHECK_EQ(st.kind, LAYOUT_SCROLL_EDITOR);
    st = layout_scroll_target(&l, 100.0f, 1.0, 0, 0, 0, 1);
    CHECK_EQ(st.kind, LAYOUT_SCROLL_EDITOR);
    CHECK_EQ((int)layout_scroll_offset(20.0f, 15.0f), 35);
    CHECK_EQ((int)layout_scroll_offset(20.0f, -15.0f), 5);
    CHECK_EQ((int)layout_scroll_offset(20.0f, -30.0f), 0);
    CHECK_EQ((int)layout_max_scroll(100.0f, 40.0f), 60);
    CHECK_EQ((int)layout_max_scroll(40.0f, 100.0f), 0);
    CHECK_EQ((int)layout_scroll_offset_clamped(20.0f, 100.0f, 80.0f), 80);
    CHECK_EQ((int)layout_scroll_offset_clamped(20.0f, -100.0f, 80.0f), 0);
    CHECK_EQ((int)layout_sidebar_max_scroll(&l, 50), 342);

    LayoutScrollbar sb = layout_scrollbar(90.0f, 10.0f, 4.0f, 100.0f,
                                          400.0f, 100.0f, 150.0f);
    CHECK(sb.visible);
    CHECK_EQ((int)sb.track_x, 90);
    CHECK_EQ((int)sb.thumb_h, 25);
    CHECK_EQ((int)sb.thumb_y, 47);
    CHECK(layout_scrollbar_hit(sb, 92.0f, 50.0f, 0.0f));
    CHECK(layout_scrollbar_hit(sb, 86.0f, 50.0f, 5.0f));
    CHECK(!layout_scrollbar_hit(sb, 84.0f, 50.0f, 5.0f));
    CHECK(layout_scrollbar_thumb_hit(sb, 92.0f, 50.0f, 0.0f));
    CHECK(!layout_scrollbar_thumb_hit(sb, 92.0f, 20.0f, 0.0f));
    CHECK_EQ((int)layout_scrollbar_drag_scroll(sb, 47.0f, 0.0f, 300.0f), 148);
    CHECK_EQ((int)layout_scrollbar_drag_scroll(sb, 500.0f, 0.0f, 300.0f), 300);
    CHECK_EQ((int)layout_scrollbar_drag_scroll(sb, -50.0f, 0.0f, 300.0f), 0);
    LayoutScrollbar wide = layout_scrollbar_expand(sb, 1.0f, 6.0f);
    CHECK_EQ((int)wide.track_x, 87);
    CHECK_EQ((int)wide.track_w, 10);
    sb = layout_scrollbar(90.0f, 10.0f, 4.0f, 100.0f,
                          90.0f, 100.0f, 0.0f);
    CHECK(!sb.visible);

    LayoutClickTarget ct = layout_click_target(&l, 100.0f, 20.0f, 0.0f, 1, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_TITLEBAR);
    CHECK(ct.titlebar_right);
    ct = layout_click_target(&l, 100.0f, 36.0f, 0.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_SIDEBAR);
    CHECK_EQ(ct.row, 0);
    ct = layout_click_target(&l, 100.0f, 56.0f, 20.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_SIDEBAR);
    CHECK_EQ(ct.row, 2);
    ct = layout_click_target(&l, 220.0f, 40.0f, 0.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_TAB);
    CHECK_EQ(ct.tab_index, 0);
    CHECK(!ct.tab_close);
    ct = layout_click_target(&l, 365.0f, 40.0f, 0.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_TAB);
    CHECK_EQ(ct.tab_index, 0);
    CHECK(ct.tab_close);
    ct = layout_click_target(&l, 300.0f, 120.0f, 0.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_TEXT);
    ct = layout_click_target(&l, 100.0f, 36.0f, 0.0f, 0, 0, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_NONE);
    ct = layout_click_target(NULL, 100.0f, 36.0f, 0.0f, 0, 1, 1);
    CHECK_EQ(ct.kind, LAYOUT_CLICK_NONE);

    LayoutFrameMetrics m = layout_frame_metrics(&l, 20.0f, 10.0f, 15.0f, 720,
                                                1, 1, 24, 1, 2.0f, 3);
    CHECK_EQ((int)m.side_px, 240);
    CHECK_EQ((int)m.gutter, 50);
    CHECK_EQ((int)m.pad, 10);
    CHECK_EQ((int)m.text_x, 300);
    CHECK_EQ((int)m.tab_h, 30);
    CHECK_EQ((int)m.header_h, 56);
    CHECK_EQ((int)m.tab_strip, 30);
    CHECK_EQ((int)m.top_pad, 86);
    CHECK_EQ((int)l.side_px, 240);
    CHECK_EQ((int)l.text_x, 300);
    CHECK_EQ((int)l.top_pad, 86);

    m = layout_frame_metrics(&l, 20.0f, 10.0f, 15.0f, 720,
                             1, 0, 24, 0, 2.0f, 0);
    CHECK_EQ((int)m.side_px, 0);
    CHECK_EQ((int)m.header_h, 0);
    CHECK_EQ((int)m.tab_strip, 0);
    CHECK_EQ((int)m.top_pad, 0);

    TEST_REPORT();
}
