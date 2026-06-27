/* test_layout.c — shared layout hit-test math. */
#include "test.h"
#include "layout.h"

int main(void) {
    LayoutState l = {0};
    layout_set(&l, 20.0f, 10.0f, 15.0f, 200.0f, 260.0f, 50.0f,
               52.0f, 28.0f, 720);
    l.tab_w = 180.0f;

    CHECK_EQ((int)layout_status_bar_h(&l), 26);
    CHECK_EQ(layout_sidebar_row(&l, 36.0f, 0.0f), 0);
    CHECK_EQ(layout_sidebar_row(&l, 20.0f, 0.0f), -1);
    CHECK_EQ(layout_sidebar_row(&l, 56.0f, 20.0f), 2);

    CHECK_EQ(layout_tab_index(&l, 199.0f), -1);
    CHECK_EQ(layout_tab_index(&l, 200.0f), 0);
    CHECK_EQ(layout_tab_index(&l, 381.0f), 1);
    CHECK(!layout_tab_close_hit(&l, 0, 250.0f));
    CHECK(layout_tab_close_hit(&l, 0, 365.0f));
    CHECK(!layout_drag_should_start(10.0f, 10.0f, 12.0f, 12.0f, 1.0f));
    CHECK(layout_drag_should_start(10.0f, 10.0f, 14.0f, 12.0f, 1.0f));
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
    CHECK_EQ((int)m.tab_h, 26);
    CHECK_EQ((int)m.header_h, 56);
    CHECK_EQ((int)m.tab_strip, 26);
    CHECK_EQ((int)m.top_pad, 82);
    CHECK_EQ((int)l.side_px, 240);
    CHECK_EQ((int)l.text_x, 300);
    CHECK_EQ((int)l.top_pad, 82);

    m = layout_frame_metrics(&l, 20.0f, 10.0f, 15.0f, 720,
                             1, 0, 24, 0, 2.0f, 0);
    CHECK_EQ((int)m.side_px, 0);
    CHECK_EQ((int)m.header_h, 0);
    CHECK_EQ((int)m.tab_strip, 0);
    CHECK_EQ((int)m.top_pad, 0);

    TEST_REPORT();
}
