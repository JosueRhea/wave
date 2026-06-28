#include "editor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "langs.h"

void editor_init(Editor *e) {
    memset(e, 0, sizeof *e);
    watch_file_init(&e->watch);
}

int editor_has_buffer(const Editor *e) {
    return e && e->buf != NULL;
}

int editor_has_path(const Editor *e) {
    return e && e->path != NULL;
}

const char *editor_path(const Editor *e) {
    return e ? e->path : NULL;
}

int editor_has_visual_rows(const Editor *e) {
    return e && e->vstart != NULL;
}

float editor_scroll_y(const Editor *e) {
    return e ? e->scroll_y : 0.0f;
}

void editor_set_scroll_y(Editor *e, float scroll_y) {
    if (!e) return;
    e->scroll_y = scroll_y < 0.0f ? 0.0f : scroll_y;
}

static char *memdup_local(const char *s, size_t n) {
    char *p = malloc(n ? n : 1);
    if (n) memcpy(p, s, n);
    return p;
}

static void ops_clear(OpStack *s) {
    for (int i = 0; i < s->n; i++) free(s->v[i].text);
    s->n = 0;
}

static void ops_push(OpStack *s, EditOp op) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->v = realloc(s->v, (size_t)s->cap * sizeof *s->v);
    }
    s->v[s->n++] = op;
}

void editor_clear_history(Editor *e) {
    if (!e) return;
    ops_clear(&e->undo);
    ops_clear(&e->redo);
}

void editor_watch_stop(Editor *e) {
    if (!e) return;
    watch_file_stop(&e->watch);
}

int editor_watch_start(Editor *e, WatchService *watch) {
    if (!e || !e->path) return -1;
    int rc = watch_file_start(watch, &e->watch, e->path);
    if (rc == 0) e->external_changed = 0;
    return rc;
}

void editor_close(Editor *e) {
    editor_watch_stop(e);
    if (e->hl) hl_free(e->hl);
    if (e->buf) buffer_free(e->buf);
    free(e->path);
    ops_clear(&e->undo); free(e->undo.v);
    ops_clear(&e->redo); free(e->redo.v);
    free(e->vstart);
    editor_init(e);
}

void editor_attach_highlighter(Editor *e) {
    if (e->hl) { hl_free(e->hl); e->hl = NULL; }
    const Language *L = lang_detect(e->path);
    if (!L) return;
    e->hl = hl_new(e->buf, L->lang, L->query);
    if (e->hl) hl_update(e->hl);
}

int editor_refresh_highlighter(Editor *e) {
    if (!e || !e->hl) return 0;
    hl_update(e->hl);
    return 1;
}

int editor_update_highlighter(Editor *e) {
    if (!e || !e->hl || !e->buf) return 0;
    if (!e->dirty && buffer_pending_edits(e->buf) == 0) return 0;
    hl_update(e->hl);
    e->dirty = 0;
    return 1;
}

size_t editor_highlight_spans(Editor *e, size_t start_byte, size_t end_byte,
                              HighlightSpan *out, size_t max) {
    if (!e || !e->hl || !out || max == 0) return 0;
    size_t n = hl_spans(e->hl, start_byte, end_byte, out, max);
    return n > max ? max : n;
}

void editor_cancel_group(Editor *e) {
    if (!e) return;
    e->group_open = 0;
}

static char *dupstr_local(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

int editor_open_file(Editor *e, const char *path, int preview, WatchService *watch) {
    if (!e || !path) return -1;
    Buffer *b = buffer_open(path);
    if (!b) return -1;
    char *owned_path = dupstr_local(path);
    if (!owned_path) {
        buffer_free(b);
        return -1;
    }

    editor_close(e);
    e->buf = b;
    e->path = owned_path;
    e->preview = preview != 0;
    editor_watch_start(e, watch);
    editor_attach_highlighter(e);
    return 0;
}

int editor_save_file(Editor *e, WatchService *watch) {
    if (!e || !e->path || !e->buf) return -1;
    if (buffer_save(e->buf, e->path) != 0) return -1;
    editor_watch_start(e, watch);
    e->modified = 0;
    e->external_changed = 0;
    fprintf(stderr, "saved %s\n", e->path);
    return 0;
}

int editor_reload_file(Editor *e, WatchService *watch, const struct stat *st) {
    if (!e || !e->path || !e->buf) return -1;
    Buffer *b = buffer_open(e->path);
    if (!b) return -1;

    size_t old_cursor = e->cursor;
    float old_scroll = e->scroll_y;
    if (e->hl) { hl_free(e->hl); e->hl = NULL; }
    buffer_free(e->buf);
    e->buf = b;
    size_t len = buffer_length(e->buf);
    e->cursor = old_cursor < len ? old_cursor : len;
    e->anchor = e->cursor;
    e->seen_cursor = (size_t)-1;
    e->scroll_y = old_scroll;
    e->dirty = 1;
    e->modified = 0;
    e->wrap_dirty = 1;
    e->group_open = 0;
    e->external_changed = 0;
    editor_clear_history(e);
    editor_attach_highlighter(e);
    if (e->lsp_open) e->lsp_dirty = 1;
    if (st) watch_file_mark_seen(&e->watch, st);
    editor_watch_start(e, watch);
    return 0;
}

EditorDiskChange editor_apply_disk_change(Editor *e, WatchService *watch) {
    if (!e || !e->path || !e->buf) return EDITOR_DISK_NOOP;

    struct stat st;
    if (stat(e->path, &st) != 0) {
        int report = e->watch.seen && !e->external_changed;
        if (report) e->external_changed = 1;
        editor_watch_stop(e);
        return report ? EDITOR_DISK_MISSING : EDITOR_DISK_NOOP;
    }
    if (!S_ISREG(st.st_mode)) return EDITOR_DISK_NOOP;
    if (!watch_file_stat_changed(&e->watch, &st)) return EDITOR_DISK_NOOP;

    if (e->modified) {
        int report = !e->external_changed;
        editor_watch_start(e, watch);
        e->external_changed = 1;
        return report ? EDITOR_DISK_DIRTY_CONFLICT : EDITOR_DISK_NOOP;
    }

    return editor_reload_file(e, watch, &st) == 0 ?
           EDITOR_DISK_RELOADED : EDITOR_DISK_RELOAD_FAILED;
}

int editor_disk_change_message(const Editor *e, EditorDiskChange change,
                               char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    const char *path = (e && e->path) ? e->path : "";
    switch (change) {
    case EDITOR_DISK_MISSING:
        snprintf(out, cap, "file missing on disk: %s", path);
        return 1;
    case EDITOR_DISK_DIRTY_CONFLICT:
        snprintf(out, cap, "external change; save would overwrite: %s", path);
        return 1;
    case EDITOR_DISK_RELOADED:
        snprintf(out, cap, "reloaded %s", path);
        return 1;
    case EDITOR_DISK_RELOAD_FAILED:
        snprintf(out, cap, "could not reload %s", path);
        return 1;
    case EDITOR_DISK_NOOP:
    default:
        return 0;
    }
}

char *editor_text(Editor *e) {
    size_t n = buffer_length(e->buf);
    char *t = malloc(n + 1);
    pt_read(buffer_pt(e->buf), 0, n, t);
    t[n] = '\0';
    return t;
}

char *editor_range_text(Editor *e, size_t a, size_t b) {
    if (!e || !e->buf || b <= a) return NULL;
    char *text = malloc(b - a + 1);
    if (!text) return NULL;
    pt_read(buffer_pt(e->buf), a, b - a, text);
    text[b - a] = '\0';
    return text;
}

unsigned char byte_at(const PieceTable *pt, size_t i) {
    char c;
    pt_read(pt, i, 1, &c);
    return (unsigned char)c;
}

static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

size_t prev_boundary(const PieceTable *pt, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && is_cont(byte_at(pt, pos))) pos--;
    return pos;
}

size_t next_boundary(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos >= len) return len;
    pos++;
    while (pos < len && is_cont(byte_at(pt, pos))) pos++;
    return pos;
}

int utf8_encode(unsigned int cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static void record_insert(Editor *e, size_t pos, const char *text, size_t len) {
    if (e->group_open && e->undo.n > 0) {
        EditOp *t = &e->undo.v[e->undo.n - 1];
        if (t->type == OP_INSERT && t->pos + t->len == pos) {
            t->text = realloc(t->text, t->len + len);
            memcpy(t->text + t->len, text, len);
            t->len += len;
            ops_clear(&e->redo);
            return;
        }
    }
    ops_push(&e->undo, (EditOp){OP_INSERT, pos, memdup_local(text, len), len, pos});
    ops_clear(&e->redo);
}

static void record_delete(Editor *e, size_t pos, const char *text, size_t len,
                          size_t cur) {
    if (e->group_open && e->undo.n > 0) {
        EditOp *t = &e->undo.v[e->undo.n - 1];
        if (t->type == OP_DELETE && pos + len == t->pos) {
            char *nt = malloc(len + t->len);
            memcpy(nt, text, len);
            memcpy(nt + len, t->text, t->len);
            free(t->text);
            t->text = nt; t->len += len; t->pos = pos;
            ops_clear(&e->redo);
            return;
        }
    }
    ops_push(&e->undo, (EditOp){OP_DELETE, pos, memdup_local(text, len), len, cur});
    ops_clear(&e->redo);
}

void ed_insert_at(Editor *e, size_t pos, const char *text, size_t len) {
    if (!len) return;
    record_insert(e, pos, text, len);
    buffer_insert(e->buf, pos, text, len);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1; e->preview = 0;
    e->modified = 1;
}

static void edit_delete_at(Editor *e, size_t pos, size_t len, size_t cur) {
    if (!len) return;
    char *t = malloc(len);
    pt_read(buffer_pt(e->buf), pos, len, t);
    record_delete(e, pos, t, len, cur);
    free(t);
    buffer_delete(e->buf, pos, len);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1; e->preview = 0;
    e->modified = 1;
}

void ed_insert(Editor *e, const char *s, size_t n) {
    ed_insert_at(e, e->cursor, s, n);
    e->cursor += n;
}

void ed_delete_range(Editor *e, size_t a, size_t b) {
    if (b <= a) return;
    edit_delete_at(e, a, b - a, e->cursor);
    if (e->cursor >= b) e->cursor -= (b - a);
    else if (e->cursor > a) e->cursor = a;
}

int editor_insert_encoded_text(Editor *e, const char *text) {
    if (!e || !e->buf || !text) return 0;
    int inserted = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\\' && p[1] == 'n') {
            ed_insert(e, "\n", 1);
            p++;
        } else {
            ed_insert(e, p, 1);
        }
        inserted = 1;
    }
    return inserted;
}

int editor_visual_range(Editor *e, EditorRange *out) {
    if (!e || !e->buf || !out) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t a = e->cursor < e->anchor ? e->cursor : e->anchor;
    size_t b = e->cursor < e->anchor ? e->anchor : e->cursor;
    b = next_boundary(pt, b);
    if (b <= a) return 0;
    out->start = a;
    out->end = b;
    return 1;
}

int editor_line_range(Editor *e, EditorRange *out) {
    if (!e || !e->buf || !out) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t a = line_start_of(pt, e->cursor);
    size_t b = line_end_of(pt, e->cursor);
    if (b < pt_length(pt)) b++;
    if (b <= a) return 0;
    out->start = a;
    out->end = b;
    return 1;
}

char *editor_copy_text(Editor *e, int visual_mode) {
    EditorRange r;
    int ok = visual_mode ? editor_visual_range(e, &r) : editor_line_range(e, &r);
    return ok ? editor_range_text(e, r.start, r.end) : NULL;
}

int editor_replace_visual_selection(Editor *e, const char *text, size_t len) {
    if (!e || !e->buf || !text || !len) return 0;
    EditorRange r;
    if (!editor_visual_range(e, &r)) return 0;
    ed_delete_range(e, r.start, r.end);
    e->cursor = r.start;
    ed_insert(e, text, len);
    return 1;
}

EditorPasteResult editor_paste_text(Editor *e, const char *text, int replace_visual) {
    if (!e || !e->buf || !text || !*text) return EDITOR_PASTE_NONE;
    e->group_open = 0;
    int replaced = 0;
    if (replace_visual)
        replaced = editor_replace_visual_selection(e, text, strlen(text));
    else
        ed_insert(e, text, strlen(text));
    e->group_open = 0;
    return replaced ? EDITOR_PASTE_REPLACED_VISUAL : EDITOR_PASTE_INSERTED;
}

int editor_paste_enters_insert(EditorPasteResult result) {
    return result == EDITOR_PASTE_REPLACED_VISUAL;
}

int editor_apply_text_input(Editor *e, unsigned int cp) {
    if (!e || !e->buf) return 0;
    char buf[4];
    int n = utf8_encode(cp, buf);
    ed_insert(e, buf, (size_t)n);
    e->group_open = 1;
    return 1;
}

int editor_apply_insert_key(Editor *e, EditorKey key) {
    if (!e || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);

    switch (key) {
    case EDITOR_KEY_BACKSPACE:
        if (e->cursor > 0) {
            size_t prev = prev_boundary(pt, e->cursor);
            ed_delete_range(e, prev, e->cursor);
        }
        e->group_open = 1;
        return 1;
    case EDITOR_KEY_DELETE:
        if (e->cursor < pt_length(pt))
            ed_delete_range(e, e->cursor, next_boundary(pt, e->cursor));
        e->group_open = 1;
        return 1;
    case EDITOR_KEY_ENTER:
        ed_insert(e, "\n", 1);
        e->group_open = 1;
        return 1;
    case EDITOR_KEY_TAB:
        ed_insert(e, "    ", 4);
        e->group_open = 1;
        return 1;
    case EDITOR_KEY_LEFT:
    case EDITOR_KEY_RIGHT:
    case EDITOR_KEY_UP:
    case EDITOR_KEY_DOWN:
        return editor_apply_motion_key(e, key);
    case EDITOR_KEY_NONE:
    default:
        return 0;
    }
}

int editor_apply_motion_key(Editor *e, EditorKey key) {
    if (!e || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);

    switch (key) {
    case EDITOR_KEY_LEFT:
        e->cursor = prev_boundary(pt, e->cursor);
        return 1;
    case EDITOR_KEY_RIGHT:
        e->cursor = next_boundary(pt, e->cursor);
        return 1;
    case EDITOR_KEY_UP:
        move_vert(e, -1);
        return 1;
    case EDITOR_KEY_DOWN:
        move_vert(e, +1);
        return 1;
    case EDITOR_KEY_NONE:
    case EDITOR_KEY_BACKSPACE:
    case EDITOR_KEY_DELETE:
    case EDITOR_KEY_ENTER:
    case EDITOR_KEY_TAB:
    default:
        return 0;
    }
}

int editor_apply_click_position(Editor *e, float x, float y, float text_x,
                                float top_pad, float line_h, float adv,
                                size_t *out) {
    if (!e || !e->buf) return 0;
    size_t off = e->cursor;
    if (!editor_offset_at_point(e, x, y, text_x, top_pad, line_h, adv, &off))
        return 0;
    e->cursor = off;
    if (out) *out = off;
    return 1;
}

int editor_apply_drag_selection(Editor *e, size_t anchor, float x, float y,
                                float text_x, float top_pad, float line_h,
                                float adv, float autoscroll_delta) {
    if (!e || !e->buf) return 0;
    e->scroll_y += autoscroll_delta;
    if (e->scroll_y < 0) e->scroll_y = 0;
    size_t off = e->cursor;
    if (!editor_offset_at_point(e, x, y, text_x, top_pad, line_h, adv, &off))
        return 0;
    e->anchor = anchor;
    e->cursor = off;
    e->group_open = 0;
    return 1;
}

int editor_undo(Editor *e) {
    e->group_open = 0;
    if (e->undo.n == 0) return 0;
    EditOp op = e->undo.v[--e->undo.n];
    if (op.type == OP_INSERT) {
        buffer_delete(e->buf, op.pos, op.len);
        e->cursor = op.pos;
    } else {
        buffer_insert(e->buf, op.pos, op.text, op.len);
        e->cursor = op.cur;
    }
    ops_push(&e->redo, op);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1;
    e->modified = 1;
    return 1;
}

int editor_redo(Editor *e) {
    e->group_open = 0;
    if (e->redo.n == 0) return 0;
    EditOp op = e->redo.v[--e->redo.n];
    if (op.type == OP_INSERT) {
        buffer_insert(e->buf, op.pos, op.text, op.len);
        e->cursor = op.pos + op.len;
    } else {
        buffer_delete(e->buf, op.pos, op.len);
        e->cursor = op.pos;
    }
    ops_push(&e->undo, op);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1;
    e->modified = 1;
    return 1;
}

int editor_apply_history_action(Editor *e, int redo, char *message, size_t message_cap) {
    int ok = redo ? editor_redo(e) : editor_undo(e);
    if (message && message_cap > 0) {
        if (ok) message[0] = '\0';
        else snprintf(message, message_cap, "%s",
                      redo ? "already at newest change" : "already at oldest change");
    }
    return ok;
}

size_t line_start_of(const PieceTable *pt, size_t off) {
    size_t row, col;
    pt_offset_to_rowcol(pt, off, &row, &col);
    return pt_line_start(pt, row);
}

size_t line_end_of(const PieceTable *pt, size_t off) {
    size_t len = pt_length(pt);
    size_t p = off;
    while (p < len && byte_at(pt, p) != '\n') p++;
    return p;
}

static int wclass(unsigned char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
    if (isalnum(c) || c == '_') return 1;
    return 2;
}

size_t word_next(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos >= len) return len;
    int c = wclass(byte_at(pt, pos));
    if (c != 0)
        while (pos < len && wclass(byte_at(pt, pos)) == c) pos++;
    while (pos < len && wclass(byte_at(pt, pos)) == 0) pos++;
    return pos;
}

size_t word_prev(const PieceTable *pt, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && wclass(byte_at(pt, pos)) == 0) pos--;
    int c = wclass(byte_at(pt, pos));
    while (pos > 0 && wclass(byte_at(pt, pos - 1)) == c) pos--;
    return pos;
}

size_t word_end(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos + 1 >= len) return len ? len - 1 : 0;
    pos++;
    while (pos < len && wclass(byte_at(pt, pos)) == 0) pos++;
    int c = wclass(byte_at(pt, pos));
    while (pos + 1 < len && wclass(byte_at(pt, pos + 1)) == c) pos++;
    return pos;
}

void move_vert(Editor *e, int dir) {
    const PieceTable *pt = buffer_pt(e->buf);
    size_t row, col;
    pt_offset_to_rowcol(pt, e->cursor, &row, &col);
    size_t lines = pt_line_count(pt);
    if (dir < 0 && row == 0) { e->cursor = 0; return; }
    if (dir > 0 && row + 1 >= lines) { e->cursor = pt_length(pt); return; }
    size_t target = row + (size_t)dir;
    size_t start = pt_line_start(pt, target);
    size_t next = (target + 1 < lines) ? pt_line_start(pt, target + 1)
                                       : pt_length(pt) + 1;
    size_t line_len = (next - 1) - start;
    e->cursor = start + (col < line_len ? col : line_len);
}

void editor_center_cursor(Editor *e, float line_h, float viewport_h) {
    if (!e || !e->buf || line_h <= 0 || viewport_h <= 0) return;
    size_t row, col;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
    float y = (float)row * line_h;
    if (y < e->scroll_y || y > e->scroll_y + viewport_h - 2 * line_h) {
        e->scroll_y = y - viewport_h / 2.0f;
        if (e->scroll_y < 0) e->scroll_y = 0;
    }
}

int editor_move_to_line_col(Editor *e, int line, int col) {
    if (!e || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    size_t ln = 0;
    if (line > 1) ln = (size_t)line - 1;
    if (ln >= lines) ln = lines ? lines - 1 : 0;
    size_t off = pt_line_start(pt, ln) + (size_t)(col > 0 ? col - 1 : 0);
    if (off > pt_length(pt)) off = pt_length(pt);
    e->cursor = off;
    return 1;
}

int editor_move_to_lsp_position(Editor *e, int line, int col,
                                char *message, size_t message_cap) {
    if (message && message_cap) message[0] = '\0';
    if (!e || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    size_t ln = line > 0 ? (size_t)line : 0;
    if (ln >= lines) ln = lines ? lines - 1 : 0;
    size_t off = pt_line_start(pt, ln) + (size_t)(col > 0 ? col : 0);
    if (off > pt_length(pt)) off = pt_length(pt);
    e->cursor = off;
    if (message && message_cap)
        snprintf(message, message_cap, "def -> Ln %d, Col %d", line + 1, col + 1);
    return 1;
}

int cell_w(unsigned char c) { return c == '\t' ? 4 : 1; }

int wrap_line(const char *s, size_t llen, int cols, int *breaks, int maxb) {
    int n = 0;
    if (maxb <= 0) return 1;
    if (cols < 1) cols = 1;
    breaks[n++] = 0;
    int row_start = 0, dcol = 0, last_space = -1;
    for (size_t i = 0; i < llen; i++) {
        unsigned char c = (unsigned char)s[i];
        int w = cell_w(c);
        if (dcol + w > cols && (int)i > row_start) {
            int brk = (last_space > row_start && last_space <= (int)i) ? last_space
                                                                       : (int)i;
            if (n < maxb) breaks[n++] = brk; else break;
            row_start = brk; dcol = 0; last_space = -1;
            i = (size_t)brk - 1;
            continue;
        }
        dcol += w;
        if (c == ' ') last_space = (int)i + 1;
    }
    return n;
}

void wrap_build(Editor *e, int cols) {
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    if (e->vstart && e->wrap_cols == cols && e->vstart_n == lines + 1 && !e->wrap_dirty)
        return;
    e->vstart = realloc(e->vstart, (lines + 1) * sizeof(int));
    e->vstart_n = lines + 1;
    e->wrap_cols = cols;
    e->wrap_dirty = 0;

    if (cols >= WRAP_NOWRAP_COLS) {
        for (size_t i = 0; i <= lines; i++) e->vstart[i] = (int)i;
        return;
    }

    static char buf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    int acc = 0;
    for (size_t i = 0; i < lines; i++) {
        e->vstart[i] = acc;
        size_t ls = pt_line_start(pt, i);
        size_t le = (i + 1 < lines) ? pt_line_start(pt, i + 1) : pt_length(pt);
        size_t llen = le - ls;
        if (i + 1 < lines && llen > 0) llen--;
        if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
        pt_read(pt, ls, llen, buf);
        acc += wrap_line(buf, llen, cols, brk, WRAP_MAXBRK);
    }
    e->vstart[lines] = acc;
}

int line_at_vrow(Editor *e, int vrow) {
    int lo = 0, hi = (int)e->vstart_n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->vstart[mid] <= vrow) lo = mid; else hi = mid - 1;
    }
    return lo;
}

int editor_offset_at_point(Editor *e, float x, float y, float text_x,
                           float top_pad, float line_h, float adv,
                           size_t *out) {
    if (!out || !e || !e->buf || !e->vstart || line_h <= 0 || adv <= 0)
        return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    if (lines == 0) { *out = 0; return 1; }

    int vrow = (int)((y - top_pad + e->scroll_y) / line_h);
    if (vrow < 0) vrow = 0;
    int ln = line_at_vrow(e, vrow);
    if ((size_t)ln >= lines) ln = (int)lines - 1;
    int sub = vrow - e->vstart[ln];
    if (sub < 0) sub = 0;

    size_t ls = pt_line_start(pt, (size_t)ln);
    size_t le = (ln + 1 < (int)lines) ? pt_line_start(pt, (size_t)ln + 1) : pt_length(pt);
    size_t llen = le - ls;
    if (llen > 0 && byte_at(pt, le - 1) == '\n') llen--;
    if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
    static char buf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    pt_read(pt, ls, llen, buf);
    int cols = e->wrap_cols > 0 ? e->wrap_cols : WRAP_NOWRAP_COLS;
    int nb = wrap_line(buf, llen, cols, brk, WRAP_MAXBRK);
    if (sub >= nb) sub = nb - 1;
    int rs = brk[sub], re = (sub + 1 < nb) ? brk[sub + 1] : (int)llen;

    int want = (int)((x - text_x) / adv + 0.5f);
    if (want < 0) want = 0;
    int xcol = 0, off = rs;
    while (off < re && xcol < want) {
        xcol += cell_w((unsigned char)buf[off]);
        off++;
    }
    *out = ls + (size_t)off;
    return 1;
}

static int is_word(unsigned char c) { return isalnum(c) || c == '_'; }

static int def_score(const PieceTable *pt, size_t start) {
    static const char *kw[] = {
        "function", "const", "let",   "var",    "class",     "struct",
        "enum",     "union", "type",  "interface", "def",     "fn",
        "func",     "int",   "char",  "void",   "float",     "double",
        "long",     "short", "unsigned", "signed", "typedef", "export",
        NULL,
    };
    size_t p = start;
    while (p > 0 && (byte_at(pt, p - 1) == ' ' || byte_at(pt, p - 1) == '\t'))
        p--;
    size_t we = p;
    while (p > 0 && is_word(byte_at(pt, p - 1))) p--;
    size_t pl = we - p;
    if (pl == 0 || pl > 31) return 0;
    char prev[32];
    pt_read(pt, p, pl, prev);
    prev[pl] = '\0';
    for (int i = 0; kw[i]; i++)
        if (!strcmp(prev, kw[i])) return 3;
    return 0;
}

int editor_find_definition(Editor *e, char *word, size_t word_cap,
                           size_t *out, int *strong) {
    if (word && word_cap) word[0] = '\0';
    if (!e || !e->buf || !out) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);

    size_t a = e->cursor, b = e->cursor;
    while (a > 0 && is_word(byte_at(pt, a - 1))) a--;
    while (b < len && is_word(byte_at(pt, b))) b++;
    if (b <= a) return 0;

    size_t wl = b - a;
    if (word && word_cap) {
        size_t copy = wl < word_cap - 1 ? wl : word_cap - 1;
        pt_read(pt, a, copy, word);
        word[copy] = '\0';
    }
    if (wl > 255) wl = 255;
    char needle[256];
    pt_read(pt, a, wl, needle);
    needle[wl] = '\0';

    size_t best = (size_t)-1;
    int best_score = -1;
    for (size_t i = 0; i + wl <= len; i++) {
        if (byte_at(pt, i) != (unsigned char)needle[0]) continue;
        if (i > 0 && is_word(byte_at(pt, i - 1))) continue;
        if (i + wl < len && is_word(byte_at(pt, i + wl))) continue;
        int ok = 1;
        for (size_t k = 1; k < wl; k++)
            if (byte_at(pt, i + k) != (unsigned char)needle[k]) { ok = 0; break; }
        if (!ok) continue;
        int score = def_score(pt, i);
        if (i == a) score -= 1;
        if (score > best_score) { best_score = score; best = i; }
    }

    if (best == (size_t)-1) return 0;
    *out = best;
    if (strong) *strong = best_score >= 3;
    return 1;
}

int editor_goto_local_definition(Editor *e, char *message, size_t message_cap) {
    if (message && message_cap) message[0] = '\0';
    if (!e || !e->buf) return 0;

    size_t best = 0;
    char word[256];
    int strong = 0;
    if (!editor_find_definition(e, word, sizeof word, &best, &strong)) {
        if (message && message_cap)
            snprintf(message, message_cap, word[0] ? "definition not found: %s"
                                                   : "no identifier here", word);
        return 0;
    }

    e->cursor = best;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t row, col;
    pt_offset_to_rowcol(pt, best, &row, &col);
    if (message && message_cap)
        snprintf(message, message_cap, "%s -> Ln %zu, Col %zu%s", word,
                 row + 1, col + 1, strong ? "" : " (first use)");
    return 1;
}
