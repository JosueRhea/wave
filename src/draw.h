#ifndef WAVE_DRAW_H
#define WAVE_DRAW_H

#include "font.h"
#include "diagnostics.h"
#include "layout.h"
#include "overlay.h"
#include "popover.h"
#include "render.h"
#include "tabs.h"
#include "text_view.h"
#include "theme.h"

void draw_text_run(Font *f, Renderer *r, const char *s, int n, float x,
                   float y, Color c);
void draw_triangle(Renderer *r, float x, float y, float size, int dir, Color c);
void draw_folder_icon(Renderer *r, float x, float y, float size, Color c);
void draw_file_icon(Renderer *r, float x, float y, float size, Color c);
void draw_sidebar_panel(Workspace *ws, const char *active_path, int side_cells,
                        float side_scroll, int fb_h, Font *font, Renderer *r,
                        float adv, float line_h, float ascent, float side_px,
                        float top_y, float side_pad, float opacity,
                        float scrollbar_hover);
void draw_scrollbar(Renderer *r, LayoutScrollbar bar, float opacity);
float draw_tabs_panel(TabSet *tabs, int fb_w, Font *font, Renderer *r,
                      float side_px, float adv, float ascent, float tab_h,
                      float top_y, float opacity);
void draw_header_panel(const char *root, const char *path, int fb_w,
                       Font *font, Renderer *r, float adv, float ascent,
                       float header_h, float fb_scale, float opacity);
void draw_editor_text_panel(Editor *e, const TextFrameView *text,
                            const HighlightSpan *spans, size_t nspans,
                            const Diagnostic *diags, size_t ndiag,
                            int insert_mode, int cursor_on, int fb_h,
                            Font *font, Renderer *r, float side_px,
                            float gutter, float text_x, float top_pad,
                            float line_h, float adv, float ascent);
void draw_palette_panel(OverlayState *overlay, Workspace *ws, int fb_w,
                        Font *font, Renderer *r, float adv, float line_h,
                        float ascent);
void draw_search_panel(OverlayState *overlay, int fb_w, Font *font,
                       Renderer *r, float adv, float line_h, float ascent);
void draw_popover_panel(Popover *state, int fb_w, int fb_h, Font *font,
                        Renderer *r, float adv, float line_h, float ascent,
                        float top_pad, float bar_h, float anchor_x,
                        float cur_top, float side_px, float fb_scale);

#endif
