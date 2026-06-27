#ifndef WAVE_POPOVER_H
#define WAVE_POPOVER_H

#define POPOVER_MAX_ROWS 12
#define POPOVER_MAX_COLS 56
#define POPOVER_DISPLAY_ROWS 1024

typedef struct {
    int active;
    int loading;
    int scroll;
    int total_rows;
    int vis_rows;
    char base[4096];
    char text[4096];
} Popover;

typedef struct {
    int off[POPOVER_DISPLAY_ROWS];
    int len[POPOVER_DISPLAY_ROWS];
    int count;
    int longest;
} PopoverRows;

typedef struct {
    PopoverRows rows;
    float x;
    float y;
    float w;
    float h;
    float padx;
    float pady;
    float scrollbar_w;
    float scrollbar_track_x;
    float scrollbar_track_w;
    float scrollbar_thumb_y;
    float scrollbar_thumb_h;
    float border;
    int visible_rows;
    int max_text_cells;
    int scrollable;
} PopoverLayout;

typedef enum {
    POPOVER_KEY_NONE,
    POPOVER_KEY_ESCAPE,
    POPOVER_KEY_UP,
    POPOVER_KEY_DOWN
} PopoverKey;

void popover_init(Popover *p);
void popover_close(Popover *p);
void popover_set_base(Popover *p, const char *base);
void popover_set_encoded_base(Popover *p, const char *encoded);
void popover_set_loading(Popover *p, int loading);
void popover_compose(Popover *p, const char *hover);
void popover_show_base(Popover *p, const char *base, int loading);
void popover_show_hover(Popover *p, const char *hover);
void popover_scroll(Popover *p, int delta);
void popover_set_view(Popover *p, int total_rows, int vis_rows);
int popover_apply_normal_char(Popover *p, unsigned int cp, int g_pending);
int popover_apply_key(Popover *p, PopoverKey key, int insert_mode);
void popover_wrap_text(const char *text, int max_cols, PopoverRows *rows);
int popover_layout(Popover *p, int fb_w, int fb_h, float adv, float line_h,
                   float top_pad, float bar_h, float anchor_x, float cur_top,
                   float left_limit, float fb_scale, PopoverLayout *out);

#endif
