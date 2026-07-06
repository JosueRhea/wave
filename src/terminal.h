#ifndef WAVE_TERMINAL_H
#define WAVE_TERMINAL_H

#include <stddef.h>
#include <sys/types.h>

#ifndef WAVE_USE_GHOSTTY_VT
#error "Wave terminal tabs require Ghostty VT; the Makefile enables it by default."
#endif

#include <ghostty/vt.h>

typedef struct {
    float r, g, b;
} TerminalColor;

typedef struct {
    size_t col_start;
    size_t col_len;
    size_t byte_start;
    size_t byte_len;
    TerminalColor fg;
    TerminalColor bg;
    unsigned char has_fg;
    unsigned char has_bg;
} TerminalCellStyle;

typedef struct {
    TerminalCellStyle *cells;
    size_t ncells;
    size_t cap_cells;
} TerminalLineStyle;

typedef struct {
    char **lines;
    TerminalLineStyle *line_styles;
    size_t nlines;
    size_t cap_lines;
    size_t scroll;
    pid_t pid;
    int fd;
    int running;
    int exit_status;
    char title[64];
    char cwd[1024];
    char current[4096];
    size_t current_len;
    size_t max_lines;
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    int cursor_visible;
    int selection_active;
    int selection_dragging;
    size_t selection_anchor_row;
    size_t selection_head_row;
    int selection_anchor_col;
    int selection_head_col;
    int ghostty_enabled;
    GhosttyTerminal ghostty_terminal;
    GhosttyRenderState ghostty_render_state;
    GhosttyRenderStateRowIterator ghostty_row_iter;
    GhosttyRenderStateRowCells ghostty_row_cells;
} Terminal;

void terminal_init(Terminal *t);
void terminal_free(Terminal *t);
int terminal_spawn(Terminal *t, const char *title, const char *cwd,
                   const char *const argv[]);
void terminal_stop(Terminal *t);
void terminal_poll(Terminal *t);
void terminal_write(Terminal *t, const char *data, size_t len);
void terminal_send_key(Terminal *t, int key);
void terminal_send_key_mods(Terminal *t, int key, int shift, int alt, int control);
size_t terminal_key_sequence(int key, int shift, int alt, int control,
                             char *out, size_t cap);
void terminal_resize(Terminal *t, int rows, int cols);
void terminal_scroll(Terminal *t, int units);
size_t terminal_visible_start(const Terminal *t, int rows);
const char *terminal_line(const Terminal *t, size_t index);
const TerminalLineStyle *terminal_line_style(const Terminal *t, size_t index);
const char *terminal_status(const Terminal *t);
void terminal_selection_clear(Terminal *t);
void terminal_selection_begin(Terminal *t, size_t row, int col);
void terminal_selection_update(Terminal *t, size_t row, int col);
void terminal_selection_end(Terminal *t);
int terminal_selection_span(const Terminal *t, size_t row, int *start_col,
                            int *end_col);
char *terminal_copy_selection(const Terminal *t);

#endif
