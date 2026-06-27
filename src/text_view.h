#ifndef WAVE_TEXT_VIEW_H
#define WAVE_TEXT_VIEW_H

#include <stddef.h>

#include "editor.h"
#include "highlight.h"

typedef struct {
    size_t row;
    size_t col;
    int subrow;
    int xcol;
    int vrow;
} TextCursorView;

typedef struct {
    int first_vrow;
    int vis_vrows;
    int first_line;
    int last_line;
    size_t byte_start;
    size_t byte_end;
} TextVisibleWindow;

typedef struct {
    int start_col;
    int end_col;
} TextColumnSpan;

typedef struct {
    size_t start;
    size_t end;
    size_t len;
    int wrap_count;
} TextLineView;

typedef struct {
    const PieceTable *pt;
    size_t total_lines;
    int cols;
    int total_vrows;
    TextCursorView cursor;
    TextVisibleWindow window;
    EditorRange selection;
    int has_selection;
} TextFrameView;

int text_view_wrap_cols(int wrap, float fb_w, float text_x, float adv);
int text_view_line(const PieceTable *pt, int line, size_t total_lines, int cols,
                   char *buf, size_t buf_cap, int *breaks, int max_breaks,
                   TextLineView *out);
float text_view_row_top(const Editor *e, int line_vstart, int subrow,
                        float top_pad, float line_h);
float text_view_gutter_x(float side_px, float gutter, float adv, int line_no);
int text_view_cells_between(const char *line, int from, int to, int line_len);
int text_view_clip_selection(const char *line, int line_len, size_t line_start,
                             int row_start, int row_end, const EditorRange *sel,
                             TextColumnSpan *out);
int text_view_clip_diagnostic(const char *line, int line_len, int line_no,
                              int row_start, int row_end, const Diagnostic *diag,
                              TextColumnSpan *out);
int text_view_filter_spans(const HighlightSpan *spans, size_t nspans,
                           size_t row_start, size_t row_end,
                           int *out, int max_out);
void text_view_cursor(Editor *e, int cols, TextCursorView *out);
void text_view_clamp_scroll(Editor *e, int total_vrows, int cur_vrow,
                            float line_h, float view_h);
void text_view_visible_window(Editor *e, const PieceTable *pt, size_t total_lines,
                              float fb_h, float top_pad, float line_h,
                              TextVisibleWindow *out);
int text_view_selection(Editor *e, int visual_mode, EditorRange *out);
int text_view_prepare(Editor *e, int visual_mode, int wrap, float fb_w,
                      float text_x, float adv, float fb_h, float top_pad,
                      float line_h, float status_bar_h, TextFrameView *out);

#endif
