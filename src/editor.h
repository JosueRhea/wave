#ifndef WAVE_EDITOR_H
#define WAVE_EDITOR_H

#include <stddef.h>
#include <sys/stat.h>

#include "buffer.h"
#include "highlight.h"
#include "watch.h"

typedef enum { OP_INSERT, OP_DELETE } OpType;
typedef struct {
    OpType type;
    size_t pos;
    char *text;
    size_t len;
    size_t cur;
} EditOp;
typedef struct { EditOp *v; int n, cap; } OpStack;

typedef struct {
    Buffer *buf;
    Highlighter *hl;
    char *path;
    size_t cursor;
    size_t anchor;
    float scroll_y;
    size_t seen_cursor;
    int dirty;
    int modified;
    int external_changed;
    int preview;

    OpStack undo, redo;
    int group_open;
    int lsp_open;
    int lsp_dirty;
    int version;

    int *vstart;
    size_t vstart_n;
    int wrap_cols;
    int wrap_dirty;

    FileWatch watch;
} Editor;

typedef struct {
    size_t start;
    size_t end;
} EditorRange;

typedef enum {
    EDITOR_DISK_NOOP,
    EDITOR_DISK_MISSING,
    EDITOR_DISK_DIRTY_CONFLICT,
    EDITOR_DISK_RELOADED,
    EDITOR_DISK_RELOAD_FAILED
} EditorDiskChange;

typedef enum {
    EDITOR_KEY_NONE,
    EDITOR_KEY_BACKSPACE,
    EDITOR_KEY_DELETE,
    EDITOR_KEY_ENTER,
    EDITOR_KEY_TAB,
    EDITOR_KEY_LEFT,
    EDITOR_KEY_RIGHT,
    EDITOR_KEY_UP,
    EDITOR_KEY_DOWN
} EditorKey;

typedef enum {
    EDITOR_PASTE_NONE,
    EDITOR_PASTE_INSERTED,
    EDITOR_PASTE_REPLACED_VISUAL
} EditorPasteResult;

#define WRAP_MAXLINE 8192
#define WRAP_MAXBRK 4096
#define WRAP_NOWRAP_COLS (1 << 27)

void editor_init(Editor *e);
void editor_close(Editor *e);
int editor_has_buffer(const Editor *e);
int editor_has_path(const Editor *e);
const char *editor_path(const Editor *e);
int editor_has_visual_rows(const Editor *e);
float editor_scroll_y(const Editor *e);
void editor_set_scroll_y(Editor *e, float scroll_y);
void editor_attach_highlighter(Editor *e);
int editor_refresh_highlighter(Editor *e);
int editor_update_highlighter(Editor *e);
size_t editor_highlight_spans(Editor *e, size_t start_byte, size_t end_byte,
                              HighlightSpan *out, size_t max);
void editor_cancel_group(Editor *e);
char *editor_text(Editor *e);
char *editor_range_text(Editor *e, size_t a, size_t b);
void editor_clear_history(Editor *e);

void editor_watch_stop(Editor *e);
int editor_watch_start(Editor *e, WatchService *watch);
int editor_open_file(Editor *e, const char *path, int preview, WatchService *watch);
int editor_save_file(Editor *e, WatchService *watch);
int editor_reload_file(Editor *e, WatchService *watch, const struct stat *st);
EditorDiskChange editor_apply_disk_change(Editor *e, WatchService *watch);
int editor_disk_change_message(const Editor *e, EditorDiskChange change,
                               char *out, size_t cap);

unsigned char byte_at(const PieceTable *pt, size_t i);
size_t prev_boundary(const PieceTable *pt, size_t pos);
size_t next_boundary(const PieceTable *pt, size_t pos);
int utf8_encode(unsigned int cp, char out[4]);

void ed_insert(Editor *e, const char *s, size_t n);
void ed_insert_at(Editor *e, size_t pos, const char *text, size_t len);
void ed_delete_range(Editor *e, size_t a, size_t b);
int editor_insert_encoded_text(Editor *e, const char *text);
int editor_visual_range(Editor *e, EditorRange *out);
int editor_line_range(Editor *e, EditorRange *out);
char *editor_copy_text(Editor *e, int visual_mode);
EditorPasteResult editor_paste_text(Editor *e, const char *text, int replace_visual);
int editor_paste_enters_insert(EditorPasteResult result);
int editor_replace_visual_selection(Editor *e, const char *text, size_t len);
int editor_apply_text_input(Editor *e, unsigned int cp);
int editor_apply_insert_key(Editor *e, EditorKey key);
int editor_open_line(Editor *e, int below);
int editor_apply_motion_key(Editor *e, EditorKey key);
int editor_apply_click_position(Editor *e, float x, float y, float text_x,
                                float top_pad, float line_h, float adv,
                                size_t *out);
int editor_apply_drag_selection(Editor *e, size_t anchor, float x, float y,
                                float text_x, float top_pad, float line_h,
                                float adv, float autoscroll_delta);
int editor_undo(Editor *e);
int editor_redo(Editor *e);
int editor_apply_history_action(Editor *e, int redo, char *message, size_t message_cap);

size_t line_start_of(const PieceTable *pt, size_t off);
size_t line_end_of(const PieceTable *pt, size_t off);
size_t word_next(const PieceTable *pt, size_t pos);
size_t word_prev(const PieceTable *pt, size_t pos);
size_t word_end(const PieceTable *pt, size_t pos);
void move_vert(Editor *e, int dir);
void editor_center_cursor(Editor *e, float line_h, float viewport_h);
int editor_move_to_line_col(Editor *e, int line, int col);

void wrap_build(Editor *e, int cols);
int cell_w(unsigned char c);
int wrap_line(const char *s, size_t llen, int cols, int *breaks, int maxb);
int line_at_vrow(Editor *e, int vrow);
int editor_offset_at_point(Editor *e, float x, float y, float text_x,
                           float top_pad, float line_h, float adv,
                           size_t *out);
int editor_move_to_lsp_position(Editor *e, int line, int col,
                                char *message, size_t message_cap);

int editor_find_definition(Editor *e, char *word, size_t word_cap,
                           size_t *out, int *strong);
int editor_goto_local_definition(Editor *e, char *message, size_t message_cap);
int editor_find_text(Editor *e, const char *needle, size_t from, int reverse,
                     size_t *out);
int editor_search_text(Editor *e, const char *needle, int reverse,
                       char *message, size_t message_cap);
int editor_preview_search(Editor *e, const char *needle, size_t origin,
                          char *message, size_t message_cap);
size_t editor_search_matches(Editor *e, const char *needle,
                             EditorRange *out, size_t max);
int editor_word_under_cursor(Editor *e, char *out, size_t cap);

#endif
