/* test_view.c — renderer-independent presentation helpers. */
#include "test.h"
#include "popover.h"
#include "text_view.h"
#include "view.h"

#include <string.h>

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    editor_clear_history(e);
    e->modified = 0;
}

int main(void) {
    CHECK_STR(view_base_name(NULL), "[scratch]");
    CHECK_STR(view_base_name("/tmp/project/main.c"), "main.c");
    ViewEmptyState empty = view_empty_state(1);
    CHECK_STR(empty.title, "No file open");
    CHECK_STR(empty.hint, "Click a file in the sidebar, or press Cmd-P");
    empty = view_empty_state(0);
    CHECK_STR(empty.title, "Wave");
    CHECK_STR(empty.hint, "Open a file or folder to get started");

    ViewHeaderTitle header = view_header_title("/tmp/project", NULL, 500.0f,
                                               40.0f, 10.0f, 15.0f, 1.0f);
    CHECK_STR(header.title, "project");
    CHECK_EQ((int)header.x, 215);
    CHECK_EQ((int)header.baseline, 27);
    header = view_header_title(NULL, "/tmp/very-long-file-name.c", 200.0f,
                               40.0f, 10.0f, 15.0f, 2.0f);
    CHECK_STR(header.title, "very-long-file-name.c");
    CHECK_EQ((int)header.x, 156);

    ViewEmptyLayout empty_layout = view_empty_layout(500.0f, 300.0f, 100.0f,
                                                     40.0f, 10.0f, 20.0f,
                                                     &empty);
    CHECK_EQ((int)empty_layout.title_x, 280);
    CHECK_EQ((int)empty_layout.title_y, 160);
    CHECK_EQ((int)empty_layout.hint_x, 120);
    CHECK_EQ((int)empty_layout.hint_y, 190);

    ViewOverlayLayout overlay_box = view_overlay_layout(1000.0f, 0.6f, 12, 0,
                                                        10.0f, 20.0f);
    CHECK_EQ((int)overlay_box.x, 200);
    CHECK_EQ((int)overlay_box.y, 40);
    CHECK_EQ((int)overlay_box.w, 600);
    CHECK_EQ((int)overlay_box.h, 280);
    CHECK_EQ((int)overlay_box.shadow_x, 194);
    CHECK_EQ((int)overlay_box.query_h, 24);
    CHECK_EQ((int)overlay_box.result_top, 64);
    CHECK_EQ(overlay_box.start, 0);
    CHECK_EQ(overlay_box.max_cells, 58);
    overlay_box = view_overlay_layout(1000.0f, 0.72f, 14, 20,
                                      10.0f, 20.0f);
    CHECK_EQ((int)overlay_box.x, 140);
    CHECK_EQ(overlay_box.start, 7);
    CHECK_EQ(overlay_box.max_cells, 70);
    overlay_box = view_overlay_layout(80.0f, 0.72f, 14, 99,
                                      10.0f, 20.0f);
    CHECK_EQ(overlay_box.max_cells, 8);

    ViewFramePlan frame_plan = view_frame_plan(100.0f, 24.0f, 36.0f, 1, 0);
    CHECK(frame_plan.sidebar);
    CHECK(frame_plan.tabs);
    CHECK(frame_plan.header);
    CHECK(!frame_plan.empty);
    CHECK(frame_plan.editor);
    CHECK(frame_plan.popover);
    CHECK_EQ(frame_plan.overlay, VIEW_OVERLAY_DRAW_NONE);

    frame_plan = view_frame_plan(0.0f, 0.0f, 0.0f, 0, 1);
    CHECK(!frame_plan.sidebar);
    CHECK(!frame_plan.tabs);
    CHECK(!frame_plan.header);
    CHECK(frame_plan.empty);
    CHECK(!frame_plan.editor);
    CHECK(!frame_plan.popover);
    CHECK_EQ(frame_plan.overlay, VIEW_OVERLAY_DRAW_PALETTE);

    frame_plan = view_frame_plan(1.0f, 1.0f, 1.0f, 0, 2);
    CHECK(frame_plan.sidebar);
    CHECK(frame_plan.tabs);
    CHECK(frame_plan.header);
    CHECK(frame_plan.empty);
    CHECK(!frame_plan.editor);
    CHECK(!frame_plan.popover);
    CHECK_EQ(frame_plan.overlay, VIEW_OVERLAY_DRAW_SEARCH);

    frame_plan = view_frame_plan(1.0f, 1.0f, 1.0f, 1, 99);
    CHECK(frame_plan.editor);
    CHECK(!frame_plan.empty);
    CHECK(!frame_plan.popover);
    CHECK_EQ(frame_plan.overlay, VIEW_OVERLAY_DRAW_NONE);

    ViewPoint anchor = view_popover_anchor(120.0f, 40.0f, 300.0f, 26.0f,
                                           10.0f, 20.0f, 3, 4, 10.0f);
    CHECK_EQ((int)anchor.x, 160);
    CHECK_EQ((int)anchor.y, 90);
    anchor = view_popover_anchor(120.0f, 40.0f, 300.0f, 26.0f,
                                 10.0f, 20.0f, -2, 4, 200.0f);
    CHECK_EQ((int)anchor.y, 40);
    anchor = view_popover_anchor(120.0f, 40.0f, 300.0f, 26.0f,
                                 10.0f, 20.0f, 99, 4, 0.0f);
    CHECK_EQ((int)anchor.y, 274);
    CHECK(view_cursor_visible(0, 100.9, 0.0));
    CHECK(view_cursor_visible(1, 10.4, 10.0));
    CHECK(!view_cursor_visible(1, 10.6, 10.0));

    CHECK_EQ(view_clamp_text_len("abcdef", 3), 3);
    CHECK_EQ(view_clamp_text_len("abc", 10), 3);
    CHECK_EQ(view_clamp_text_len("abc", -1), 0);

    WsEntry file = {.rel = "src/main.c", .name = "main.c", .depth = 1, .is_dir = 0};
    WsEntry dir = {.rel = "src", .name = "src", .depth = 0, .is_dir = 1};
    CHECK_EQ(view_sidebar_name_len(&file, 20), 6);
    CHECK_EQ(view_sidebar_name_len(&file, 8), 3);
    CHECK(view_workspace_entry_active("/tmp/proj/src/main.c", &file));
    CHECK(!view_workspace_entry_active("/tmp/proj/other/main.c", &file));
    CHECK(!view_workspace_entry_active("/tmp/proj/src", &dir));
    ViewSidebarWindow side_win = view_sidebar_window(300, 20.0f, 8.0f,
                                                     45.0f, 20.0f);
    CHECK_EQ(side_win.first, 2);
    CHECK_EQ(side_win.count, 14);
    CHECK_EQ((int)side_win.content_top, 28);
    ViewSidebarRow side_row = view_sidebar_row(&file, "/tmp/proj/src/main.c",
                                               20, 3, 20.0f, 8.0f, 20.0f,
                                               10.0f, 20.0f, 15.0f);
    CHECK_EQ((int)side_row.top, 68);
    CHECK_EQ((int)side_row.baseline, 83);
    CHECK_EQ((int)side_row.chevron_x, 21);
    CHECK_EQ((int)side_row.icon_x, 31);
    CHECK_EQ((int)side_row.name_x, 45);
    CHECK_EQ((int)side_row.icon_size, 10);
    CHECK_EQ((int)side_row.chevron_size, 7);
    CHECK_EQ(side_row.name_len, 6);
    CHECK(side_row.active);
    side_row = view_sidebar_row(&dir, "/tmp/proj/src/main.c",
                                20, 0, 0.0f, 8.0f, 20.0f,
                                10.0f, 20.0f, 15.0f);
    CHECK(!side_row.active);

    Editor e = {0};
    char out[64];
    e.path = "/tmp/proj/src/main.c";
    view_tab_label(&e, out, sizeof out);
    CHECK_STR(out, "main.c");
    e.modified = 1;
    view_tab_label(&e, out, sizeof out);
    CHECK_STR(out, "main.c *");

    view_search_status(out, sizeof out, 1, 3, 0, 0, 0);
    CHECK_STR(out, "ripgrep unavailable");
    view_search_status(out, sizeof out, 0, 3, 1, 0, 0);
    CHECK_STR(out, "searching...");
    view_search_status(out, sizeof out, 0, 3, 0, 1, 42);
    CHECK_STR(out, "42+ matches");
    view_search_status(out, sizeof out, 0, 3, 0, 0, 1);
    CHECK_STR(out, "1 match");
    view_search_status(out, sizeof out, 0, 3, 0, 0, 2);
    CHECK_STR(out, "2 matches");
    CHECK_EQ(view_status_text(out, sizeof out, "w", "", "NORMAL",
                              "file.c", 0, "c", 0, 0, 0, 0, 1),
             VIEW_STATUS_COMMAND);
    CHECK_STR(out, ":w");
    CHECK_EQ(view_status_text(out, sizeof out, NULL, "saved", "NORMAL",
                              "file.c", 0, "c", 0, 0, 0, 0, 1),
             VIEW_STATUS_INFO);
    CHECK_STR(out, "saved");
    CHECK_EQ(view_status_text(out, sizeof out, NULL, "", "INSERT",
                              "file.c", 1, "c", 4, 2, 3, 1, 4),
             VIEW_STATUS_NORMAL);
    CHECK_STR(out, "INSERT  file.c *  c  Ln 5, Col 3  3 errs  [2/4]");
    CHECK_EQ(view_status_text(out, sizeof out, NULL, "", "NORMAL",
                              NULL, 0, "text", 0, 0, 0, 0, 1),
             VIEW_STATUS_NORMAL);
    CHECK_STR(out, "NORMAL  [scratch]  text  Ln 1, Col 1  0 errs  [1/1]");
    ViewStatusLine status_line = view_status_line(out, sizeof out, &e, NULL, "saved",
                                                  "NORMAL", 0, 0, 0, 0, 1);
    CHECK_EQ(status_line.kind, VIEW_STATUS_INFO);
    CHECK(status_line.r > 0.85f && status_line.g > 0.83f && status_line.b > 0.54f);
    CHECK_STR(out, "saved");
    status_line = view_editor_status_line(out, sizeof out, NULL, "term", "",
                                          "NORMAL", 0, 0, 1);
    CHECK_EQ(status_line.kind, VIEW_STATUS_COMMAND);
    CHECK_STR(out, ":term");
    Editor status_editor;
    fill(&status_editor, "alpha\nbeta\n");
    status_editor.path = "/tmp/status.c";
    status_editor.cursor = strlen("alpha\nbe");
    status_line = view_editor_status_line(out, sizeof out, &status_editor, NULL, "",
                                          "NORMAL", 2, 0, 1);
    CHECK_EQ(status_line.kind, VIEW_STATUS_NORMAL);
    CHECK_STR(out, "NORMAL  /tmp/status.c  c  Ln 2, Col 3  2 errs  [1/1]");
    status_editor.path = NULL;
    editor_close(&status_editor);

    PopoverRows rows;
    popover_wrap_text("alpha beta gamma", 8, &rows);
    CHECK_EQ(rows.count, 3);
    CHECK_EQ(rows.len[0], 5);
    CHECK_EQ(rows.len[1], 4);
    CHECK_EQ(rows.len[2], 5);
    CHECK_EQ(rows.longest, 5);
    popover_wrap_text("one\ntwo", 8, &rows);
    CHECK_EQ(rows.count, 2);
    CHECK_EQ(rows.off[1], 4);
    CHECK_EQ(rows.len[1], 3);

    Popover pop;
    PopoverLayout pop_layout;
    popover_init(&pop);
    popover_set_base(&pop, "alpha beta gamma");
    popover_compose(&pop, NULL);
    CHECK(popover_layout(&pop, 500, 300, 10.0f, 20.0f, 40.0f, 30.0f,
                         160.0f, 80.0f, 100.0f, 1.0f, &pop_layout));
    CHECK_EQ(pop_layout.rows.count, 1);
    CHECK_EQ((int)pop_layout.x, 160);
    CHECK_EQ((int)pop_layout.y, 106);
    CHECK_EQ((int)pop_layout.w, 176);
    CHECK_EQ((int)pop_layout.h, 34);
    CHECK_EQ(pop.total_rows, 1);
    CHECK_EQ(pop.vis_rows, 1);
    CHECK(!pop_layout.scrollable);

    popover_set_base(&pop, "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\nthirteen\nfourteen\nfifteen\nsixteen");
    popover_compose(&pop, NULL);
    CHECK(popover_layout(&pop, 500, 300, 10.0f, 20.0f, 40.0f, 30.0f,
                         10.0f, 240.0f, 100.0f, 1.0f, &pop_layout));
    CHECK_EQ((int)pop_layout.x, 100);
    CHECK(pop_layout.y < 240.0f);
    CHECK(pop_layout.visible_rows <= POPOVER_MAX_ROWS);
    CHECK(pop_layout.scrollable);
    CHECK(pop_layout.scrollbar_thumb_h > 0.0f);
    pop.scroll = 99;
    CHECK(popover_layout(&pop, 500, 300, 10.0f, 20.0f, 40.0f, 30.0f,
                         490.0f, 240.0f, 100.0f, 1.0f, &pop_layout));
    CHECK(pop.scroll <= pop.total_rows - pop.vis_rows);
    CHECK((int)(pop_layout.x + pop_layout.w) <= 500);

    CHECK_EQ(text_view_wrap_cols(0, 500.0f, 100.0f, 10.0f), WRAP_NOWRAP_COLS);
    CHECK_EQ(text_view_wrap_cols(1, 130.0f, 100.0f, 10.0f), 4);
    CHECK_EQ(text_view_wrap_cols(1, 260.0f, 100.0f, 10.0f), 16);
    CHECK_EQ(text_view_cells_between("a\tbc", 0, 3, 4), 6);
    CHECK_EQ(text_view_cells_between("abc", -4, 99, 3), 3);

    Editor ed;
    fill(&ed, "abcdef\nxyz\n");
    wrap_build(&ed, 3);
    ed.cursor = 4;
    TextCursorView cur;
    text_view_cursor(&ed, 3, &cur);
    CHECK_EQ(cur.row, 0);
    CHECK_EQ(cur.col, 4);
    CHECK_EQ(cur.subrow, 1);
    CHECK_EQ(cur.xcol, 1);
    CHECK_EQ(cur.vrow, 1);

    ed.scroll_y = 999.0f;
    ed.seen_cursor = (size_t)-1;
    text_view_clamp_scroll(&ed, ed.vstart[pt_line_count(buffer_pt(ed.buf))],
                           cur.vrow, 10.0f, 30.0f);
    CHECK_EQ((int)ed.scroll_y, 10);

    TextVisibleWindow win;
    text_view_visible_window(&ed, buffer_pt(ed.buf), pt_line_count(buffer_pt(ed.buf)),
                             100.0f, 20.0f, 10.0f, &win);
    CHECK_EQ(win.first_vrow, 1);
    CHECK_EQ(win.first_line, 0);
    CHECK(win.byte_end > win.byte_start);

    char linebuf[WRAP_MAXLINE];
    int breaks[WRAP_MAXBRK];
    TextLineView line;
    CHECK(text_view_line(buffer_pt(ed.buf), 0, pt_line_count(buffer_pt(ed.buf)), 3,
                         linebuf, sizeof linebuf, breaks, WRAP_MAXBRK, &line));
    CHECK_EQ(line.start, 0);
    CHECK_EQ(line.len, 6);
    CHECK_EQ(line.wrap_count, 2);
    CHECK_EQ(breaks[0], 0);
    CHECK_EQ(breaks[1], 3);
    CHECK_EQ((int)text_view_row_top(&ed, ed.vstart[0], 1, 20.0f, 10.0f), 20);
    CHECK_EQ((int)text_view_gutter_x(100.0f, 50.0f, 10.0f, 123), 115);

    ed.anchor = 1;
    ed.cursor = 3;
    EditorRange sel;
    CHECK(text_view_selection(&ed, 1, &sel));
    CHECK_EQ(sel.start, 1);
    CHECK_EQ(sel.end, 4);
    CHECK(!text_view_selection(&ed, 0, &sel));

    TextColumnSpan cols;
    CHECK(text_view_clip_selection("abcdef", 6, 0, 0, 6, &sel, &cols));
    CHECK_EQ(cols.start_col, 1);
    CHECK_EQ(cols.end_col, 4);
    CHECK(text_view_clip_selection("abcdef", 6, 0, 2, 5, &sel, &cols));
    CHECK_EQ(cols.start_col, 0);
    CHECK_EQ(cols.end_col, 2);

    Diagnostic dg = {.start_row = 2, .start_col = 1, .end_row = 2, .end_col = 4};
    CHECK(text_view_clip_diagnostic("abcdef", 6, 2, 0, 6, &dg, &cols));
    CHECK_EQ(cols.start_col, 1);
    CHECK_EQ(cols.end_col, 4);
    CHECK(!text_view_clip_diagnostic("abcdef", 6, 1, 0, 6, &dg, &cols));

    HighlightSpan spans[3] = {
        {.start_byte = 0, .end_byte = 2},
        {.start_byte = 3, .end_byte = 5},
        {.start_byte = 8, .end_byte = 9},
    };
    int idx[3] = {-1, -1, -1};
    CHECK_EQ(text_view_filter_spans(spans, 3, 1, 4, idx, 3), 2);
    CHECK_EQ(idx[0], 0);
    CHECK_EQ(idx[1], 1);
    editor_close(&ed);

    fill(&ed, "abcdef\nxyz\n");
    ed.cursor = 4;
    ed.anchor = 1;
    TextFrameView frame;
    CHECK(text_view_prepare(&ed, 1, 1, 260.0f, 100.0f, 10.0f,
                            120.0f, 20.0f, 10.0f, 16.0f, &frame));
    CHECK(frame.pt == buffer_pt(ed.buf));
    CHECK_EQ(frame.total_lines, 3);
    CHECK_EQ(frame.cols, 16);
    CHECK_EQ(frame.cursor.row, 0);
    CHECK_EQ(frame.cursor.vrow, 0);
    CHECK(frame.total_vrows >= 3);
    CHECK(frame.window.byte_end > frame.window.byte_start);
    CHECK(frame.has_selection);
    CHECK_EQ(frame.selection.start, 1);
    CHECK_EQ(frame.selection.end, 5);
    ed.scroll_y = 999.0f;
    ed.seen_cursor = (size_t)-1;
    ed.cursor = buffer_length(ed.buf);
    CHECK(text_view_prepare(&ed, 0, 1, 130.0f, 100.0f, 10.0f,
                            60.0f, 20.0f, 10.0f, 16.0f, &frame));
    CHECK_EQ(frame.cols, 4);
    CHECK(!frame.has_selection);
    CHECK(ed.scroll_y >= 0.0f);
    CHECK(ed.scroll_y <= (float)frame.total_vrows * 10.0f);
    editor_close(&ed);

    TEST_REPORT();
}
