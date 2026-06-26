/* main.c — Wave application.
 *
 * A GLFW/OpenGL window that opens a file or a folder, browses files in a
 * sidebar, finds files with a Cmd-P palette, edits with vim-style modal keys,
 * keeps several files open as tabs, and syntax-highlights C / JavaScript /
 * TypeScript / JSX / TSX with tree-sitter (underlining syntax errors live).
 *
 * Layout is in framebuffer pixels (hi-DPI aware). Each frame:
 *   1. apply pending edits to the tree (hl_update) and recompute diagnostics
 *   2. draw the sidebar (if a folder is open) and the tab strip
 *   3. for the visible line range, lay out glyphs colored by highlight spans,
 *      plus gutter, selection, cursor, and diagnostic underlines
 *   4. draw the status bar / command line, then the Cmd-P palette on top
 *   5. one renderer_flush -> one draw call
 *
 * Beyond plain editing the editor offers a few "navigation" verbs that do not
 * need a language server: `gh` reports the diagnostic or syntax node under the
 * cursor, and `gd` jumps to the most likely definition of the identifier under
 * the cursor (a whole-word scan ranked by a preceding declaration keyword).
 */
#include <GLFW/glfw3.h>
#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
/* implemented in mac.m: background blur + transparent full-size-content titlebar
 * plus the title-bar gestures the OS would normally provide (drag, double-click
 * zoom/minimise, right-click path menu). */
void mac_set_blur(void *nswindow, int enable);
void mac_use_native_titlebar(void *nswindow, int enable);
void mac_window_drag(void *nswindow);
void mac_window_titlebar_doubleclick(void *nswindow);
void mac_window_titlebar_menu(void *nswindow);
void mac_window_set_file(void *nswindow, const char *path);
#else
static void mac_set_blur(void *nswindow, int enable) { (void)nswindow; (void)enable; }
static void mac_use_native_titlebar(void *nswindow, int enable) { (void)nswindow; (void)enable; }
static void mac_window_drag(void *nswindow) { (void)nswindow; }
static void mac_window_titlebar_doubleclick(void *nswindow) { (void)nswindow; }
static void mac_window_titlebar_menu(void *nswindow) { (void)nswindow; }
static void mac_window_set_file(void *nswindow, const char *path) { (void)nswindow; (void)path; }
static void *glfwGetCocoaWindow(GLFWwindow *w) { (void)w; return 0; }
#endif
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h> /* _NSGetExecutablePath — locate our vendored servers */
#endif

#include "buffer.h"
#include "font.h"
#include "highlight.h"
#include "langs.h"
#include "lsp.h"
#include "render.h"
#include "search.h"
#include "workspace.h"

/* ---- theme: highlight-capture name -> RGB ---- */
typedef struct { float r, g, b; } Color;

static Color theme(const char *name) {
    if (!name) return (Color){0.85f, 0.86f, 0.88f};
    if (!strcmp(name, "keyword")) return (Color){0.86f, 0.51f, 0.78f};
    if (!strcmp(name, "type")) return (Color){0.46f, 0.71f, 0.96f};
    if (!strcmp(name, "string")) return (Color){0.62f, 0.80f, 0.42f};
    if (!strcmp(name, "number")) return (Color){0.90f, 0.62f, 0.36f};
    if (!strcmp(name, "constant")) return (Color){0.90f, 0.62f, 0.36f};
    if (!strcmp(name, "comment")) return (Color){0.42f, 0.46f, 0.52f};
    if (!strcmp(name, "function")) return (Color){0.92f, 0.80f, 0.45f};
    if (!strcmp(name, "property")) return (Color){0.60f, 0.78f, 0.90f};
    return (Color){0.85f, 0.86f, 0.88f};
}

/* ---- application state ---- */
typedef enum { MODE_NORMAL, MODE_INSERT, MODE_VISUAL } Mode;
typedef enum { OVERLAY_NONE, OVERLAY_PALETTE, OVERLAY_SEARCH } Overlay;

#define MAX_TABS 32

/* ---- undo/redo ----
 * Every document mutation is recorded as an inverse-applicable EditOp: an
 * INSERT remembers the text it added (undo = delete it); a DELETE remembers the
 * text it removed (undo = put it back). Contiguous typing/backspacing coalesces
 * into one op so a whole insert burst undoes at once. */
typedef enum { OP_INSERT, OP_DELETE } OpType;
typedef struct {
    OpType type;
    size_t pos;
    char *text;  /* owned: inserted (INSERT) or deleted (DELETE) bytes */
    size_t len;
    size_t cur;  /* cursor to restore when this op is undone */
} EditOp;
typedef struct { EditOp *v; int n, cap; } OpStack;

typedef struct {
    Buffer *buf;
    Highlighter *hl; /* NULL = plain text (no grammar for this file) */
    char *path;      /* owned; NULL for a scratch buffer */
    size_t cursor;   /* byte offset */
    size_t anchor;   /* visual-mode selection anchor (byte offset) */
    float scroll_y;  /* pixels */
    size_t seen_cursor; /* cursor offset the last frame followed (for auto-scroll) */
    int dirty;       /* needs reparse */
    int preview;     /* 1 = transient "peek" tab: a single sidebar click reuses
                        this slot instead of opening a new tab; pinned on
                        double-click or first edit */

    OpStack undo, redo;
    int group_open;  /* 1 = coalesce the next contiguous edit into the top op */
    int lsp_open;    /* 1 = we've sent textDocument/didOpen for this file */
    int lsp_dirty;   /* 1 = content changed; owes the server a didChange */
    int version;     /* LSP document version counter */

    /* word-wrap layout cache: vstart[i] = the visual-row index at which logical
     * line i begins (vstart[line_count] = total visual rows). Rebuilt when the
     * content changes or the wrap width changes. */
    int *vstart;
    size_t vstart_n;  /* number of entries = line_count + 1 */
    int wrap_cols;    /* the column width this cache was built for */
    int wrap_dirty;   /* 1 = content changed since the cache was built */
} Editor;

typedef struct {
    GLFWwindow *win;
    Font *font;
    Renderer *rend;
    float fb_scale;
    float ui_scale;   /* user zoom (Cmd +/-), multiplies the base font size */
    float base_pt;    /* base font point size before fb_scale/ui_scale */

    Workspace *ws;     /* open folder, or NULL */
    int show_sidebar;
    float side_scroll;
    int side_cells;    /* sidebar width in character cells */
    int wrap;          /* 1 = word-wrap long lines to the viewport width */
    float opacity;     /* window/background opacity 0..1 (1 = fully opaque) */
    int blur;          /* 1 = macOS background blur behind the window */

    /* open files, shown as tabs */
    Editor tabs[MAX_TABS];
    int tab_count;
    int active;

    /* vim */
    Mode mode;
    char pending;      /* operator/prefix awaiting a second key ('d','c','y','g') */
    int op_g;          /* 1 = an operator is waiting on a `g`-prefixed motion (dgg) */
    int count;         /* numeric prefix */

    /* command line ( : ) */
    int cmd_active;
    char cmd[256];
    int cmd_len;

    /* command palette ( Cmd-P ) */
    Overlay overlay;
    char query[256];
    int query_len;
    int sel;
    int *filtered;     /* indices into workspace entries (files only) */
    int filtered_n, filtered_cap;

    /* content search ( Cmd-Shift-F ): a ripgrep-backed project-wide search.
     * `s_sel`/`s_scroll` index into the live result list. */
    Search *search;
    char s_query[256];
    int s_query_len;
    int s_sel, s_scroll;

    /* yank register */
    char *yank;
    size_t yank_len;
    int yank_line;     /* 1 = line-wise */

    char info[256];    /* transient `gd`/status message for the status bar */

    /* `gh` popover: diagnostics + hover under the cursor, scrollable, anchored
     * to the cursor and re-placed each frame to stay fully on screen. */
    struct {
        int active;
        int loading;          /* awaiting an async LSP hover reply */
        int scroll;           /* first visible wrapped row */
        int total_rows;       /* set during draw, for clamp/scrollbar */
        int vis_rows;         /* visible row count this frame */
        char base[4096];      /* diagnostics text (shown with/before hover) */
        char text[4096];      /* full composed content; '\n' separates lines */
    } pop;

    /* language servers, started lazily per language family (clangd, tsserver) */
    struct { char key[16]; Lsp *l; } servers[6];
    int nservers;
    char *root_uri;    /* file:// URI of the workspace root */
    int no_lsp;        /* 1 = LSP disabled (e.g. headless snapshot) */

    int mods;          /* latest modifier state */

    double last_activity; /* glfwGetTime() of the last keypress (for blink reset) */
    int blink;            /* 1 = animate the cursor (off in snapshot mode) */
    int persist;          /* 1 = read/write the config file (off in snapshot mode) */

    int native_titlebar;  /* 1 = transparent full-size-content titlebar (macOS) */

    /* cached layout from the last frame, for click hit-testing */
    float m_line_h, m_adv, m_ascent, m_side_px, m_text_x, m_gutter;
    float m_top_pad, m_tab_w, m_header_h, m_side_pad;
    int m_fb_h;
} App;

static App g;

/* The active editor (the front tab). Always valid: tabs[0] exists even when
 * empty, so callers never have to null-check the slot itself. */
static Editor *cur(void) { return &g.tabs[g.active]; }

static const char *FONT_PATH = "/System/Library/Fonts/SFNSMono.ttf";

/* ---- config persistence (~/.config/wave/config) ----
 * A flat key=value file for the handful of UI preferences worth remembering
 * across launches. Loaded once at startup; rewritten whenever one of them
 * changes (zoom, word wrap, sidebar) or on the :config command. */
static void config_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/.config/wave/config", home ? home : ".");
}

static void config_save(void) {
    if (!g.persist) return;
    const char *home = getenv("HOME");
    if (!home) return;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/.config", home); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/.config/wave", home); mkdir(dir, 0755);
    char path[1100];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# wave config\n");
    fprintf(f, "wrap=%d\n", g.wrap);
    fprintf(f, "scale=%.3f\n", g.ui_scale);
    fprintf(f, "sidebar=%d\n", g.show_sidebar);
    fprintf(f, "side_cells=%d\n", g.side_cells);
    fprintf(f, "opacity=%.3f\n", g.opacity);
    fprintf(f, "blur=%d\n", g.blur);
    fprintf(f, "titlebar=%d\n", g.native_titlebar);
    fclose(f);
}

static void config_load(void) {
    char path[1100];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        char *nl = strpbrk(val, "\r\n");
        if (nl) *nl = '\0';
        if (!strcmp(key, "wrap")) g.wrap = atoi(val) != 0;
        else if (!strcmp(key, "scale")) {
            float v = (float)atof(val);
            if (v >= 0.5f && v <= 3.0f) g.ui_scale = v;
        } else if (!strcmp(key, "sidebar")) g.show_sidebar = atoi(val) != 0;
        else if (!strcmp(key, "side_cells")) {
            int v = atoi(val);
            if (v >= 8 && v <= 80) g.side_cells = v;
        } else if (!strcmp(key, "opacity")) {
            float v = (float)atof(val);
            if (v >= 0.2f && v <= 1.0f) g.opacity = v;
        } else if (!strcmp(key, "blur")) g.blur = atoi(val) != 0;
        else if (!strcmp(key, "titlebar")) g.native_titlebar = atoi(val) != 0;
    }
    fclose(f);
}

/* ---- utf-8 / byte helpers ---- */
static unsigned char byte_at(const PieceTable *pt, size_t i) {
    char c;
    pt_read(pt, i, 1, &c);
    return (unsigned char)c;
}
static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

static size_t prev_boundary(const PieceTable *pt, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && is_cont(byte_at(pt, pos))) pos--;
    return pos;
}
static size_t next_boundary(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos >= len) return len;
    pos++;
    while (pos < len && is_cont(byte_at(pt, pos))) pos++;
    return pos;
}

static int utf8_encode(unsigned int cp, char out[4]) {
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

/* ---- undo/redo bookkeeping ---- */
static char *memdup(const char *s, size_t n) {
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

/* Record an insert; coalesce with the previous op when it's contiguous typing. */
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
    ops_push(&e->undo, (EditOp){OP_INSERT, pos, memdup(text, len), len, pos});
    ops_clear(&e->redo);
}
/* Record a delete; coalesce successive backspaces (each removes the bytes just
 * left of the previous removal). */
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
    ops_push(&e->undo, (EditOp){OP_DELETE, pos, memdup(text, len), len, cur});
    ops_clear(&e->redo);
}

/* ---- editor text ops (all mutations funnel through these) ---- */
static void edit_insert_at(Editor *e, size_t pos, const char *text, size_t len) {
    if (!len) return;
    record_insert(e, pos, text, len);
    buffer_insert(e->buf, pos, text, len);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1; e->preview = 0;
}
static void edit_delete_at(Editor *e, size_t pos, size_t len, size_t cur) {
    if (!len) return;
    char *t = malloc(len);
    pt_read(buffer_pt(e->buf), pos, len, t);
    record_delete(e, pos, t, len, cur);
    free(t);
    buffer_delete(e->buf, pos, len);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1; e->preview = 0;
}

static void ed_insert(Editor *e, const char *s, size_t n) {
    edit_insert_at(e, e->cursor, s, n);
    e->cursor += n;
}
static void ed_delete_range(Editor *e, size_t a, size_t b) {
    if (b <= a) return;
    edit_delete_at(e, a, b - a, e->cursor);
    if (e->cursor >= b) e->cursor -= (b - a);
    else if (e->cursor > a) e->cursor = a;
}

static void editor_undo(Editor *e) {
    e->group_open = 0;
    if (e->undo.n == 0) { snprintf(g.info, sizeof g.info, "already at oldest change"); return; }
    EditOp op = e->undo.v[--e->undo.n];
    if (op.type == OP_INSERT) {        /* undo an insert: remove the text */
        buffer_delete(e->buf, op.pos, op.len);
        e->cursor = op.pos;
    } else {                           /* undo a delete: restore the text */
        buffer_insert(e->buf, op.pos, op.text, op.len);
        e->cursor = op.cur;
    }
    ops_push(&e->redo, op);            /* ownership of op.text moves to redo */
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1;
}
static void editor_redo(Editor *e) {
    e->group_open = 0;
    if (e->redo.n == 0) { snprintf(g.info, sizeof g.info, "already at newest change"); return; }
    EditOp op = e->redo.v[--e->redo.n];
    if (op.type == OP_INSERT) {        /* redo the insert */
        buffer_insert(e->buf, op.pos, op.text, op.len);
        e->cursor = op.pos + op.len;
    } else {                           /* redo the delete */
        buffer_delete(e->buf, op.pos, op.len);
        e->cursor = op.pos;
    }
    ops_push(&e->undo, op);
    e->dirty = 1; e->lsp_dirty = 1; e->wrap_dirty = 1;
}

/* line geometry */
static size_t line_start_of(const PieceTable *pt, size_t off) {
    size_t row, col;
    pt_offset_to_rowcol(pt, off, &row, &col);
    return pt_line_start(pt, row);
}
static size_t line_end_of(const PieceTable *pt, size_t off) {
    /* offset of the trailing '\n' (or document end) for the line at `off` */
    size_t len = pt_length(pt);
    size_t p = off;
    while (p < len && byte_at(pt, p) != '\n') p++;
    return p;
}

/* word classes for w/b/e motions */
static int wclass(unsigned char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
    if (isalnum(c) || c == '_') return 1;
    return 2;
}
static size_t word_next(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos >= len) return len;
    int c = wclass(byte_at(pt, pos));
    if (c != 0)
        while (pos < len && wclass(byte_at(pt, pos)) == c) pos++;
    while (pos < len && wclass(byte_at(pt, pos)) == 0) pos++;
    return pos;
}
static size_t word_prev(const PieceTable *pt, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && wclass(byte_at(pt, pos)) == 0) pos--;
    int c = wclass(byte_at(pt, pos));
    while (pos > 0 && wclass(byte_at(pt, pos - 1)) == c) pos--;
    return pos;
}
static size_t word_end(const PieceTable *pt, size_t pos) {
    size_t len = pt_length(pt);
    if (pos + 1 >= len) return len ? len - 1 : 0;
    pos++;
    while (pos < len && wclass(byte_at(pt, pos)) == 0) pos++;
    int c = wclass(byte_at(pt, pos));
    while (pos + 1 < len && wclass(byte_at(pt, pos + 1)) == c) pos++;
    return pos;
}

/* vertical motion preserving column */
static void move_vert(Editor *e, int dir) {
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

/* Scroll so the cursor's row sits near the middle of the viewport (used after
 * a jump like `gd`). Relies on the last frame's cached metrics. */
static void center_cursor(Editor *e) {
    float lh = g.m_line_h;
    if (lh <= 0) return;
    size_t row, col;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
    float y = (float)row * lh;
    float vh = (float)g.m_fb_h - g.m_top_pad;
    if (y < e->scroll_y || y > e->scroll_y + vh - 2 * lh) {
        e->scroll_y = y - vh / 2.0f;
        if (e->scroll_y < 0) e->scroll_y = 0;
    }
}

/* ---- word wrap ---- */
#define WRAP_MAXLINE 8192
#define WRAP_MAXBRK 4096
/* cols sentinel at/above which wrapping is disabled (matches the caller's huge
 * 1<<28 budget); lets wrap_build skip the document entirely when wrap is off. */
#define WRAP_NOWRAP_COLS (1 << 27)

/* Display width in cells of one byte (tab = 4, others = 1). */
static int cell_w(unsigned char c) { return c == '\t' ? 4 : 1; }

/* Word-wrap a logical line of `llen` bytes to `cols` columns. Fills breaks[]
 * with the byte offset at which each visual row starts (breaks[0] = 0) and
 * returns the number of visual rows (>= 1). Breaks at the last space before the
 * limit when there is one, otherwise hard-breaks mid-token. */
static int wrap_line(const char *s, size_t llen, int cols, int *breaks, int maxb) {
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
            i = (size_t)brk - 1; /* reprocess from the break point */
            continue;
        }
        dcol += w;
        if (c == ' ') last_space = (int)i + 1;
    }
    return n;
}

/* (Re)build the per-line visual-row index for `cols`. Cheap to call every frame;
 * only does work when the content or wrap width changed. */
static void wrap_build(Editor *e, int cols) {
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    if (e->vstart && e->wrap_cols == cols && e->vstart_n == lines + 1 && !e->wrap_dirty)
        return;
    e->vstart = realloc(e->vstart, (lines + 1) * sizeof(int));
    e->vstart_n = lines + 1;
    e->wrap_cols = cols;
    e->wrap_dirty = 0;

    /* No-wrap fast path: every logical line is exactly one visual row, so the
     * index is the identity — skip reading the document entirely. (Word wrap is
     * driven by a finite `cols`; the caller passes a huge sentinel when off.) */
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
        /* line_start(i+1) sits just past line i's '\n', so the line's own bytes
         * are [ls, le-1); the last line has no trailing newline. */
        size_t le = (i + 1 < lines) ? pt_line_start(pt, i + 1) : pt_length(pt);
        size_t llen = le - ls;
        if (i + 1 < lines && llen > 0) llen--; /* drop the trailing '\n' */
        if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
        pt_read(pt, ls, llen, buf);
        acc += wrap_line(buf, llen, cols, brk, WRAP_MAXBRK);
    }
    e->vstart[lines] = acc;
}

/* Largest logical line index i with vstart[i] <= vrow (binary search). */
static int line_at_vrow(Editor *e, int vrow) {
    int lo = 0, hi = (int)e->vstart_n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->vstart[mid] <= vrow) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* ---- highlighter wiring on file open ---- */
static void editor_attach_highlighter(Editor *e) {
    if (e->hl) { hl_free(e->hl); e->hl = NULL; }
    const Language *L = lang_detect(e->path);
    if (!L) return;
    e->hl = hl_new(e->buf, L->lang, L->query);
    if (e->hl) hl_update(e->hl);
}

static void editor_close(Editor *e) {
    if (e->hl) hl_free(e->hl);
    if (e->buf) buffer_free(e->buf);
    free(e->path);
    ops_clear(&e->undo); free(e->undo.v);
    ops_clear(&e->redo); free(e->redo.v);
    free(e->vstart);
    memset(e, 0, sizeof *e);
}

/* ---- tabs ---- */
static Editor *new_tab(void) {
    if (g.tab_count >= MAX_TABS) { /* full: recycle the active tab */
        editor_close(cur());
        return cur();
    }
    g.active = g.tab_count++;
    memset(cur(), 0, sizeof(Editor));
    return cur();
}

static void close_tab(int i) {
    if (i < 0 || i >= g.tab_count) return;
    editor_close(&g.tabs[i]);
    for (int j = i; j < g.tab_count - 1; j++) g.tabs[j] = g.tabs[j + 1];
    g.tab_count--;
    if (g.tab_count == 0) { glfwSetWindowShouldClose(g.win, GLFW_TRUE); return; }
    if (g.active >= g.tab_count) g.active = g.tab_count - 1;
    g.mode = MODE_NORMAL;
}

static void tab_goto(int delta) {
    if (g.tab_count == 0) return;
    g.active = (g.active + delta % g.tab_count + g.tab_count) % g.tab_count;
    g.mode = MODE_NORMAL;
}

/* ---- LSP wiring ---- */

/* Build a file:// URI from a path (resolved to absolute, %-encoded). */
static char *path_to_uri(const char *path) {
    char abs[4096];
    if (path[0] == '/') snprintf(abs, sizeof abs, "%s", path);
    else if (!realpath(path, abs)) snprintf(abs, sizeof abs, "%s", path);
    size_t n = strlen(abs);
    char *u = malloc(n * 3 + 8);
    strcpy(u, "file://");
    size_t o = 7;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)abs[i];
        if (c == '/' || isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            u[o++] = (char)c;
        else { sprintf(u + o, "%%%02X", c); o += 3; }
    }
    u[o] = '\0';
    return u;
}

/* The whole document as a fresh NUL-terminated string (caller frees). */
static char *editor_text(Editor *e) {
    size_t n = buffer_length(e->buf);
    char *t = malloc(n + 1);
    pt_read(buffer_pt(e->buf), 0, n, t);
    t[n] = '\0';
    return t;
}

static const char *lsp_language_id(const char *name) {
    if (!strcmp(name, "tsx")) return "typescriptreact";
    if (!strcmp(name, "javascript")) return "javascript";
    if (!strcmp(name, "typescript")) return "typescript";
    return name; /* "c" */
}
/* Directory containing the running executable (resolved, no trailing slash).
 * Used to find the language servers we ship alongside the binary. */
static int exe_dir(char *out, size_t cap) {
    char buf[4096];
#ifdef __APPLE__
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) != 0) return 0;
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
#endif
    char real[4096];
    if (!realpath(buf, real)) snprintf(real, sizeof real, "%s", buf);
    char *slash = strrchr(real, '/');
    if (slash) *slash = '\0';
    snprintf(out, cap, "%s", real);
    return 1;
}

/* First executable named `name` on PATH, written absolute into `out`. */
static int find_on_path(const char *name, char *out, size_t cap) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    size_t nlen = strlen(name);
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 1 + nlen + 1 < cap) {
            memcpy(out, path, dlen);
            out[dlen] = '/';
            memcpy(out + dlen + 1, name, nlen + 1);
            if (access(out, X_OK) == 0) return 1;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

/* The bundled typescript-language-server entry point, searched relative to our
 * binary: inside a .app bundle (Contents/Resources/vendor/, the canonical spot
 * for non-code payload), next to it when shipped flat (vendor/ alongside the
 * executable), or one level up in the source tree (build/wave + vendor/).
 * 1 + path on success. */
static int ts_server_cli(char *out, size_t cap) {
    char dir[4096];
    if (!exe_dir(dir, sizeof dir)) return 0;
    static const char *rel[] = {
        "../Resources/vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        "vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        "../vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        NULL,
    };
    for (int i = 0; rel[i]; i++) {
        snprintf(out, cap, "%s/%s", dir, rel[i]);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

/* The ripgrep binary: the copy vendored alongside our executable first (so a
 * shipped wave needs no global install), else one on PATH. Returns 1 + path. */
static int rg_binary(char *out, size_t cap) {
    char dir[4096];
    if (exe_dir(dir, sizeof dir)) {
        static const char *rel[] = { "../Resources/vendor/rg/rg",
                                     "vendor/rg/rg", "../vendor/rg/rg", NULL };
        for (int i = 0; rel[i]; i++) {
            snprintf(out, cap, "%s/%s", dir, rel[i]);
            if (access(out, X_OK) == 0) return 1;
        }
    }
    return find_on_path("rg", out, cap);
}

/* Fill `argv` (NULL-terminated, >= 5 slots) with the spawn command for the
 * server handling language `name`. `rt`/`cli` are scratch buffers the argv may
 * point into, so they must outlive the lsp_start() call. Returns a cache key. */
static const char *lsp_server_argv(const char *name, const char *argv[5],
                                   char *rt, size_t rtcap, char *cli, size_t clicap) {
    if (!strcmp(name, "c")) {
        /* --background-index lets clangd resolve go-to-definition *across files*
         * (a call jumps to the function body, not just the header declaration)
         * and report project-wide diagnostics. --header-insertion=never keeps
         * it from volunteering #include edits we don't apply. */
        argv[0] = "clangd";
        argv[1] = "--background-index";
        argv[2] = "--header-insertion=never";
        argv[3] = "--log=error";
        argv[4] = NULL;
        return "clangd";
    }
    /* TypeScript / JavaScript family. Prefer the server we vendored next to the
     * binary, run through bun (the project's runtime) or node — so a shipped
     * wave needs no global install. Fall back to a `typescript-language-server`
     * on PATH if the bundle or a JS runtime is missing. */
    if ((find_on_path("bun", rt, rtcap) || find_on_path("node", rt, rtcap)) &&
        ts_server_cli(cli, clicap)) {
        argv[0] = rt;
        argv[1] = cli;
        argv[2] = "--stdio";
        argv[3] = NULL;
    } else {
        argv[0] = "typescript-language-server";
        argv[1] = "--stdio";
        argv[2] = NULL;
    }
    return "tsls";
}

/* Find (or lazily start) the server for editor `e`'s language. A failed start
 * is cached as NULL so we don't fork repeatedly. Returns NULL when LSP is
 * disabled, the file has no language, or no server binary is installed. */
static Lsp *lsp_for(Editor *e) {
    if (g.no_lsp || !e->path) return NULL;
    const Language *L = lang_detect(e->path);
    if (!L) return NULL;
    const char *argv[5];
    char rt[4096], cli[4096];
    const char *key = lsp_server_argv(L->name, argv, rt, sizeof rt, cli, sizeof cli);
    for (int i = 0; i < g.nservers; i++)
        if (!strcmp(g.servers[i].key, key)) return g.servers[i].l;
    if (g.nservers >= (int)(sizeof g.servers / sizeof g.servers[0])) return NULL;
    Lsp *l = lsp_start((const char *const *)argv, g.root_uri);
    snprintf(g.servers[g.nservers].key, sizeof g.servers[g.nservers].key, "%s", key);
    g.servers[g.nservers].l = l; /* may be NULL: cache the negative result */
    g.nservers++;
    return l;
}

/* Tell the server (if any) that this file is now open. */
static void lsp_open_editor(Editor *e) {
    Lsp *l = lsp_for(e);
    if (!l) return;
    const Language *L = lang_detect(e->path);
    char *uri = path_to_uri(e->path);
    char *txt = editor_text(e);
    lsp_did_open(l, uri, lsp_language_id(L->name), txt);
    e->lsp_open = 1; e->version = 1; e->lsp_dirty = 0;
    free(uri); free(txt);
}

/* Open `path` (absolute), focusing it if already open. `preview` makes it a
 * transient peek tab (see Editor.preview): the next preview-open reuses the
 * slot instead of stacking another tab. */
static int open_path_mode(const char *path, int preview) {
    for (int i = 0; i < g.tab_count; i++)
        if (g.tabs[i].path && !strcmp(g.tabs[i].path, path)) {
            g.active = i;
            if (!preview) g.tabs[i].preview = 0; /* a real open pins the tab */
            g.mode = MODE_NORMAL;
            return 0;
        }

    Buffer *b = buffer_open(path);
    if (!b) { fprintf(stderr, "could not open %s\n", path); return -1; }

    Editor *slot = NULL;
    if (preview) {
        /* reuse the existing preview tab, if any, so clicking through a lot of
         * files peeks at each one in place instead of piling up tabs. */
        for (int i = 0; i < g.tab_count; i++)
            if (g.tabs[i].preview) { slot = &g.tabs[i]; g.active = i; break; }
    }
    if (slot) {
        editor_close(slot); /* drop the previously-peeked file, reuse the slot */
    } else if (g.tab_count == 1 && !g.tabs[0].path && !g.tabs[0].dirty &&
               g.tabs[0].buf && buffer_length(g.tabs[0].buf) == 0) {
        editor_close(&g.tabs[0]); /* swallow the throwaway scratch tab */
        g.active = 0;
        slot = &g.tabs[0];
    } else {
        slot = new_tab();
    }
    slot->buf = b;
    slot->path = strdup(path);
    slot->preview = preview;
    editor_attach_highlighter(slot);
    lsp_open_editor(slot);
    g.mode = MODE_NORMAL;
    return 0;
}

/* Open `path` (absolute) as a pinned tab: focus it if already open, otherwise
 * load it into a fresh tab (reusing an empty untitled scratch tab if that's all
 * there is). */
static int open_path(const char *path) { return open_path_mode(path, 0); }

/* ---- command palette (Cmd-P) ---- */
/* Fuzzy score for query `q` against path `s`: higher is better, INT_MIN means
 * no match. A greedy, case-insensitive subsequence walk that rewards matches at
 * word boundaries (start, after / _ - . space, or a camelCase hump), runs of
 * consecutive matches, and matches inside the basename, while charging a small
 * penalty per skipped character. The bonuses are what make a real fuzzy finder:
 * "lsp" ranks src/lsp.c above docs/examples/sample.c. */
static int fuzzy_score(const char *q, const char *s) {
    if (!*q) return 0;
    const char *base = s; /* start of the basename, for a path-tail bonus */
    for (const char *c = s; *c; c++)
        if (*c == '/') base = c + 1;

    int score = 0, run = 0;
    char prev = '/'; /* treat the string start as a boundary */
    const char *sp = s;
    for (; *q; q++) {
        char qc = (char)tolower((unsigned char)*q);
        int matched = 0;
        for (; *sp; sp++) {
            char sc = (char)tolower((unsigned char)*sp);
            if (sc == qc) {
                int boundary = prev == '/' || prev == '_' || prev == '-' ||
                               prev == '.' || prev == ' ' ||
                               (islower((unsigned char)prev) &&
                                isupper((unsigned char)*sp));
                score += 16;
                if (boundary) score += 30;
                if (run) score += 15;          /* consecutive match */
                if (sp >= base) score += 10;   /* in the filename, not the dir */
                if (sp == s) score += 20;      /* very first char */
                run = 1;
                prev = *sp;
                sp++;
                matched = 1;
                break;
            }
            score -= 1; /* gap: skipped a char */
            run = 0;
            prev = *sp;
        }
        if (!matched) return INT_MIN;
    }
    return score;
}

typedef struct { int idx, score; } PalHit;
static int pal_cmp(const void *a, const void *b) {
    const PalHit *x = a, *y = b;
    if (x->score != y->score) return x->score < y->score ? 1 : -1; /* high first */
    return x->idx - y->idx; /* tie-break: keep workspace (display) order */
}

static void palette_refilter(void) {
    g.filtered_n = 0;
    if (!g.ws) return;
    g.query[g.query_len] = '\0';
    size_t n = ws_count(g.ws);

    static PalHit *hits = NULL;
    static size_t hits_cap = 0;
    size_t hn = 0;
    for (size_t i = 0; i < n; i++) {
        const WsEntry *e = ws_entry(g.ws, i);
        if (e->is_dir) continue;
        int sc = 0;
        if (g.query_len > 0) {
            sc = fuzzy_score(g.query, e->rel);
            if (sc == INT_MIN) continue;
        }
        if (hn == hits_cap) {
            hits_cap = hits_cap ? hits_cap * 2 : 256;
            hits = realloc(hits, hits_cap * sizeof *hits);
        }
        hits[hn].idx = (int)i;
        hits[hn].score = sc;
        hn++;
    }
    if (g.query_len > 0) /* empty query keeps the workspace's display order */
        qsort(hits, hn, sizeof *hits, pal_cmp);

    for (size_t i = 0; i < hn; i++) {
        if (g.filtered_n == g.filtered_cap) {
            g.filtered_cap = g.filtered_cap ? g.filtered_cap * 2 : 256;
            g.filtered = realloc(g.filtered, g.filtered_cap * sizeof(int));
        }
        g.filtered[g.filtered_n++] = hits[i].idx;
    }
    /* a fresh filter selects the best (top) match */
    g.sel = 0;
}

static void palette_open(void) {
    if (!g.ws) return;
    g.overlay = OVERLAY_PALETTE;
    g.query_len = 0;
    g.sel = 0;
    palette_refilter();
}
static void palette_accept(void) {
    if (g.sel >= 0 && g.sel < g.filtered_n) {
        const WsEntry *e = ws_entry(g.ws, g.filtered[g.sel]);
        char *full = ws_fullpath(g.ws, e->rel);
        open_path(full);
        free(full);
    }
    g.overlay = OVERLAY_NONE;
}

/* ---- content search ( Cmd-Shift-F ): project-wide ripgrep ---- */
/* Issue the current query against the workspace, creating the searcher lazily
 * (resolving the vendored/PATH ripgrep on first use). */
static void search_run(void) {
    if (!g.ws) return;
    if (!g.search) {
        char rg[1024];
        if (!rg_binary(rg, sizeof rg)) return; /* no ripgrep → no search */
        g.search = search_new(rg, ws_root(g.ws));
        if (!g.search) return;
    }
    g.s_query[g.s_query_len] = '\0';
    search_query(g.search, g.s_query);
    g.s_sel = 0;
    g.s_scroll = 0;
}

static void search_open(void) {
    if (!g.ws) return;
    g.overlay = OVERLAY_SEARCH;
    /* keep the last query so reopening picks up where you left off */
    g.s_sel = 0;
    g.s_scroll = 0;
}

/* Open the selected hit's file and place the cursor at its line/col. */
static void search_accept(void) {
    if (!g.search) { g.overlay = OVERLAY_NONE; return; }
    const SearchHit *h = search_hit(g.search, (size_t)g.s_sel);
    if (h) {
        char *full = ws_fullpath(g.ws, h->path);
        if (open_path(full) == 0) {
            Editor *te = cur();
            const PieceTable *pt = buffer_pt(te->buf);
            size_t lines = pt_line_count(pt);
            size_t ln = (size_t)(h->line - 1) < lines ? (size_t)(h->line - 1)
                                                      : (lines ? lines - 1 : 0);
            size_t off = pt_line_start(pt, ln) + (size_t)(h->col > 0 ? h->col - 1 : 0);
            if (off > pt_length(pt)) off = pt_length(pt);
            te->cursor = off;
            center_cursor(te);
        }
        free(full);
    }
    g.overlay = OVERLAY_NONE;
}

/* ---- command line ( :w :q :wq ) ---- */
static void cmd_exec(void) {
    g.cmd[g.cmd_len] = '\0';
    const char *c = g.cmd;
    int do_write = 0, do_quit = 0, quit_all = 0;
    if (!strcmp(c, "w")) do_write = 1;
    else if (!strcmp(c, "q")) do_quit = 1;
    else if (!strcmp(c, "q!")) do_quit = 1;
    else if (!strcmp(c, "wq") || !strcmp(c, "x")) { do_write = do_quit = 1; }
    else if (!strcmp(c, "qa") || !strcmp(c, "qa!") || !strcmp(c, "qall")) {
        do_quit = quit_all = 1;
    }
    else if (!strcmp(c, "wrap") || !strcmp(c, "set wrap")) { g.wrap = 1; config_save(); }
    else if (!strcmp(c, "nowrap") || !strcmp(c, "set nowrap")) { g.wrap = 0; config_save(); }
    else if (!strcmp(c, "config")) {
        config_save();
        char p[1100];
        config_path(p, sizeof p);
        snprintf(g.info, sizeof g.info, "config saved: %s", p);
    }
    else if (!strncmp(c, "opacity", 7)) {
        float v = (float)atof(c + 7); /* ":opacity 0.85" */
        if (v >= 0.2f && v <= 1.0f) { g.opacity = v; config_save(); }
        snprintf(g.info, sizeof g.info, "opacity %.2f", g.opacity);
    }
    else if (!strcmp(c, "blur")) {
        g.blur = !g.blur;
        mac_set_blur(glfwGetCocoaWindow(g.win), g.blur);
        config_save();
        snprintf(g.info, sizeof g.info, "blur %s", g.blur ? "on" : "off");
    }
    else if (!strcmp(c, "titlebar")) {
        g.native_titlebar = !g.native_titlebar;
        mac_use_native_titlebar(glfwGetCocoaWindow(g.win), g.native_titlebar);
        config_save();
        snprintf(g.info, sizeof g.info, "titlebar %s",
                 g.native_titlebar ? "native" : "default");
    }

    if (do_write && cur()->path) {
        if (buffer_save(cur()->buf, cur()->path) == 0)
            fprintf(stderr, "saved %s\n", cur()->path);
    }
    g.cmd_active = 0;
    g.cmd_len = 0;
    if (do_quit) {
        if (quit_all) glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        else close_tab(g.active); /* closes the window when the last tab goes */
    }
}

/* ---- yank register ---- */
static void yank_set(const char *s, size_t n, int line_wise) {
    g.yank = realloc(g.yank, n + 1);
    if (n) memcpy(g.yank, s, n);
    g.yank[n] = '\0';
    g.yank_len = n;
    g.yank_line = line_wise;
}
static void yank_range(Editor *e, size_t a, size_t b, int line_wise) {
    if (b <= a) return;
    char *tmp = malloc(b - a);
    pt_read(buffer_pt(e->buf), a, b - a, tmp);
    yank_set(tmp, b - a, line_wise);
    free(tmp);
}

/* ---- gh: diagnostic / hover popover under the cursor ---- */
#define MAXDIAG 256

static void popover_close(void) { g.pop.active = 0; g.pop.loading = 0; }

static const char *severity_name(int sev) {
    switch (sev) {
    case 1: return "Error";
    case 2: return "Warning";
    case 3: return "Info";
    case 4: return "Hint";
    default: return "Diagnostic";
    }
}

/* Does an LSP diagnostic's (0-based) range cover the cursor row/col? */
static int lsp_diag_covers(const LspDiag *d, int row, int col) {
    if (row < d->start_line || row > d->end_line) return 0;
    if (row == d->start_line && col < d->start_col) return 0;
    if (row == d->end_line && col > d->end_col) return 0;
    return 1;
}

/* Compose pop.text from the diagnostics prefix (pop.base) + the hover body (or
 * a "Loading…" placeholder while the async reply is in flight). */
static void popover_compose(const char *hover) {
    char out[sizeof g.pop.text];
    size_t off = 0;
    out[0] = '\0';
    if (g.pop.base[0])
        off += snprintf(out + off, sizeof out - off, "%s", g.pop.base);
    if (hover && hover[0])
        off += snprintf(out + off, sizeof out - off, "%s%s", off ? "\n\n" : "", hover);
    else if (g.pop.loading)
        off += snprintf(out + off, sizeof out - off, "%s%s", off ? "\n\n" : "", "Loading…");
    if (!out[0]) snprintf(out, sizeof out, "No diagnostics or info here.");
    snprintf(g.pop.text, sizeof g.pop.text, "%s", out);
    size_t L = strlen(g.pop.text); /* drop trailing blank lines */
    while (L && g.pop.text[L - 1] == '\n') g.pop.text[--L] = '\0';
    g.pop.active = 1;
}

static void show_info(void) {
    Editor *e = cur();
    if (!e->buf) return;

    size_t row, col;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);

    /* gather diagnostics covering the cursor — the server's if it has published,
     * otherwise the tree-sitter syntax diagnostics. */
    char base[sizeof g.pop.base];
    size_t boff = 0;
    base[0] = '\0';
    int have_diag = 0;
    Lsp *l = lsp_for(e);
    if (l && e->path) {
        static LspDiag ld[MAXDIAG];
        int published = 0;
        char *uri = path_to_uri(e->path);
        size_t nl = lsp_diagnostics(l, uri, ld, MAXDIAG, &published);
        free(uri);
        for (size_t i = 0; i < nl; i++)
            if (lsp_diag_covers(&ld[i], (int)row, (int)col)) {
                boff += snprintf(base + boff, sizeof base - boff, "%s%s: %s",
                                 boff ? "\n\n" : "", severity_name(ld[i].severity),
                                 ld[i].message);
                have_diag = 1;
            }
    }
    if (!have_diag && e->hl) {
        static Diagnostic d[MAXDIAG];
        size_t nd = hl_diagnostics(e->hl, d, MAXDIAG);
        for (size_t i = 0; i < nd; i++)
            if (e->cursor >= d[i].start_byte && e->cursor < d[i].end_byte) {
                boff += snprintf(base + boff, sizeof base - boff,
                                 "%s%s @ Ln %zu, Col %zu", boff ? "\n\n" : "",
                                 d[i].message, d[i].start_row + 1, d[i].start_col + 1);
                have_diag = 1;
            }
    }

    snprintf(g.pop.base, sizeof g.pop.base, "%s", base);
    g.pop.scroll = 0;

    /* ask the server for a hover; it arrives asynchronously and is appended */
    if (l && lsp_ready(l) && e->path) {
        char *uri = path_to_uri(e->path);
        lsp_hover(l, uri, (int)row, (int)col);
        free(uri);
        g.pop.loading = 1;
        popover_compose(NULL);
        return;
    }
    g.pop.loading = 0;

    /* no server: fall back to the tree-sitter node under the cursor */
    if (!have_diag && e->hl) {
        char type[128];
        size_t s, en;
        if (hl_node_at(e->hl, e->cursor, type, sizeof type, &s, &en))
            snprintf(g.pop.base, sizeof g.pop.base, "node: %s  [%zu bytes]", type,
                     en - s);
    }
    popover_compose(NULL);
}

/* ---- gd: jump to the likely definition of the identifier under the cursor --
 *
 * No language server: we take the identifier under the cursor and scan the
 * buffer for whole-word occurrences, scoring each by whether it is immediately
 * preceded by a declaration keyword. The earliest highest-scoring hit wins
 * (definitions usually precede uses, and the strict `>` keeps the first one). */
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

static void goto_definition(void) {
    Editor *e = cur();
    if (!e->buf) return;

    /* prefer the language server; the async reply is applied in the main loop */
    Lsp *l = lsp_for(e);
    if (l && lsp_ready(l) && e->path) {
        size_t row, col;
        pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
        char *uri = path_to_uri(e->path);
        lsp_definition(l, uri, (int)row, (int)col);
        free(uri);
        snprintf(g.info, sizeof g.info, "resolving definition…");
        return;
    }

    /* no server: fall back to the in-file tree-sitter heuristic */
    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);

    /* identifier under (or just before) the cursor */
    size_t a = e->cursor, b = e->cursor;
    while (a > 0 && is_word(byte_at(pt, a - 1))) a--;
    while (b < len && is_word(byte_at(pt, b))) b++;
    if (b <= a) { snprintf(g.info, sizeof g.info, "no identifier here"); return; }
    size_t wl = b - a;
    if (wl > 255) wl = 255;
    char word[256];
    pt_read(pt, a, wl, word);
    word[wl] = '\0';

    size_t best = (size_t)-1;
    int best_score = -1;
    for (size_t i = 0; i + wl <= len; i++) {
        if (byte_at(pt, i) != (unsigned char)word[0]) continue;
        if (i > 0 && is_word(byte_at(pt, i - 1))) continue;
        if (i + wl < len && is_word(byte_at(pt, i + wl))) continue;
        int ok = 1;
        for (size_t k = 1; k < wl; k++)
            if (byte_at(pt, i + k) != (unsigned char)word[k]) { ok = 0; break; }
        if (!ok) continue;
        int score = def_score(pt, i);
        if (i == a) score -= 1; /* don't "find" the very spot we're sitting on */
        if (score > best_score) { best_score = score; best = i; }
    }

    if (best == (size_t)-1) {
        snprintf(g.info, sizeof g.info, "definition not found: %s", word);
        return;
    }
    e->cursor = best;
    center_cursor(e);
    size_t row, col;
    pt_offset_to_rowcol(pt, best, &row, &col);
    snprintf(g.info, sizeof g.info, "%s -> Ln %zu, Col %zu%s", word, row + 1,
             col + 1, best_score >= 3 ? "" : " (first use)");
}

/* ---- vim normal/visual mode ---- */
static void enter_insert(void) {
    g.mode = MODE_INSERT; g.pending = 0; g.count = 0;
    cur()->group_open = 0; /* first keystroke of this insert starts a new undo op */
}

static void vim_normal_char(unsigned int cp) {
    Editor *e = cur();
    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);

    /* while the `gh` popover is open, j/k scroll it (cursor stays put). It is
     * re-opened by the gh sequence below; every other key dismisses it. */
    if (g.pop.active) {
        if (cp == 'j' || cp == 'k') {
            int maxscroll = g.pop.total_rows - g.pop.vis_rows;
            if (maxscroll < 0) maxscroll = 0;
            g.pop.scroll += (cp == 'j') ? 1 : -1;
            if (g.pop.scroll < 0) g.pop.scroll = 0;
            if (g.pop.scroll > maxscroll) g.pop.scroll = maxscroll;
            return;
        }
        if (!(g.pending == 'g' || cp == 'g')) popover_close();
    }

    g.info[0] = '\0';     /* any normal-mode key dismisses a lingering gd note */
    e->group_open = 0;    /* ...and ends the current insert-coalescing group */

    /* accumulate a numeric count (but a leading 0 is the "start of line" motion) */
    if (cp >= '1' && cp <= '9') { g.count = g.count * 10 + (int)(cp - '0'); return; }
    if (cp == '0' && g.count > 0) { g.count = g.count * 10; return; }
    int count = g.count ? g.count : 1;

    /* pending operator (d/c/y) or prefix (g) */
    if (g.pending) {
        char op = g.pending;
        g.pending = 0;
        g.count = 0;
        if (op == 'g') {
            switch (cp) {
            case 'g': e->cursor = 0; break;
            case 'h': show_info(); break;       /* diagnostic / node info */
            case 'd': goto_definition(); break; /* go to definition */
            case 't': tab_goto(+1); break;      /* next tab */
            case 'T': tab_goto(-1); break;      /* previous tab */
            default: break;
            }
            return;
        }
        if (op == 'd' || op == 'c' || op == 'y') {
            size_t a = e->cursor, b = e->cursor;
            int line_wise = 0, ok = 1;
            size_t row, col;
            pt_offset_to_rowcol(pt, e->cursor, &row, &col);
            size_t lines = pt_line_count(pt);

            if (g.op_g) { /* second key of dgg/cgg/ygg: from file start to here */
                g.op_g = 0;
                if (cp == 'g') {
                    a = 0;
                    size_t le = line_end_of(pt, e->cursor);
                    b = (le < len) ? le + 1 : le;
                    line_wise = 1;
                } else ok = 0;
            } else if (cp == 'g') {        /* wait for the trailing g */
                g.pending = op; g.op_g = 1; return;
            } else if (cp == (unsigned int)(unsigned char)op) { /* dd/cc/yy + count */
                a = line_start_of(pt, e->cursor);
                size_t endrow = row + (size_t)count - 1;
                if (endrow >= lines) endrow = lines ? lines - 1 : 0;
                size_t le = line_end_of(pt, pt_line_start(pt, endrow));
                b = (le < len) ? le + 1 : le;
                line_wise = 1;
                if (op == 'c') { a = line_start_of(pt, e->cursor); b = le; }
            } else if (cp == 'j' || cp == 'k' || cp == 'G') { /* linewise spans */
                size_t lo = row, hi = row;
                if (cp == 'j') hi = row + (size_t)count;
                else if (cp == 'k') lo = row >= (size_t)count ? row - (size_t)count : 0;
                else { /* G: to a given line, or end of file */
                    hi = g.count ? (size_t)count - 1 : (lines ? lines - 1 : 0);
                    if (hi < row) { lo = hi; hi = row; }
                }
                if (hi >= lines) hi = lines ? lines - 1 : 0;
                a = pt_line_start(pt, lo);
                size_t le = line_end_of(pt, pt_line_start(pt, hi));
                b = (le < len) ? le + 1 : le;
                line_wise = 1;
            } else if (cp == 'w') {
                for (int i = 0; i < count; i++) b = word_next(pt, b);
            } else if (cp == 'b') {
                for (int i = 0; i < count; i++) a = word_prev(pt, a);
            } else if (cp == 'e') {
                for (int i = 0; i < count; i++) b = word_end(pt, b) + 1;
            } else if (cp == 'h') {
                for (int i = 0; i < count; i++) a = prev_boundary(pt, a);
            } else if (cp == 'l') {
                for (int i = 0; i < count; i++) b = next_boundary(pt, b);
            } else if (cp == '$') {
                b = line_end_of(pt, e->cursor);
            } else if (cp == '0') {
                a = line_start_of(pt, e->cursor);
            } else if (cp == '^') {
                size_t s = line_start_of(pt, e->cursor), le = line_end_of(pt, e->cursor);
                while (s < le && (byte_at(pt, s)==' '||byte_at(pt, s)=='\t')) s++;
                a = s;
            } else {
                ok = 0; /* unsupported text object */
            }

            if (ok && b > a) {
                if (op == 'y') { yank_range(e, a, b, line_wise); e->cursor = a; }
                else {
                    yank_range(e, a, b, line_wise);
                    ed_delete_range(e, a, b);
                    if (op == 'c') enter_insert();
                }
            } else if (ok && op == 'c') {
                enter_insert(); /* e.g. `cl` on an empty line still enters insert */
            }
        }
        return;
    }

    switch (cp) {
    /* motions */
    case 'h': for (int i=0;i<count;i++) e->cursor = prev_boundary(pt, e->cursor); break;
    case 'l': for (int i=0;i<count;i++) e->cursor = next_boundary(pt, e->cursor); break;
    case 'k': for (int i=0;i<count;i++) move_vert(e, -1); break;
    case 'j': for (int i=0;i<count;i++) move_vert(e, +1); break;
    case 'w': for (int i=0;i<count;i++) e->cursor = word_next(pt, e->cursor); break;
    case 'b': for (int i=0;i<count;i++) e->cursor = word_prev(pt, e->cursor); break;
    case 'e': for (int i=0;i<count;i++) e->cursor = word_end(pt, e->cursor); break;
    case '0': e->cursor = line_start_of(pt, e->cursor); break;
    case '^': {
        size_t s = line_start_of(pt, e->cursor), le = line_end_of(pt, e->cursor);
        while (s < le && (byte_at(pt, s)==' '||byte_at(pt, s)=='\t')) s++;
        e->cursor = s; break;
    }
    case '$': e->cursor = line_end_of(pt, e->cursor); break;
    case 'G': e->cursor = (g.count ? pt_line_start(pt, (size_t)count-1) : len); break;
    case 'g': g.pending = 'g'; return;

    /* enter insert */
    case 'i': enter_insert(); break;
    case 'a': e->cursor = next_boundary(pt, e->cursor); enter_insert(); break;
    case 'I': {
        size_t s = line_start_of(pt, e->cursor), le = line_end_of(pt, e->cursor);
        while (s < le && (byte_at(pt, s)==' '||byte_at(pt, s)=='\t')) s++;
        e->cursor = s; enter_insert(); break;
    }
    case 'A': e->cursor = line_end_of(pt, e->cursor); enter_insert(); break;
    case 'o': {
        size_t le = line_end_of(pt, e->cursor);
        e->cursor = le; ed_insert(e, "\n", 1); enter_insert(); break;
    }
    case 'O': {
        size_t s = line_start_of(pt, e->cursor);
        e->cursor = s; ed_insert(e, "\n", 1); e->cursor = s; enter_insert(); break;
    }

    /* edits */
    case 'x': {
        size_t nx = next_boundary(pt, e->cursor);
        if (nx > e->cursor) { yank_range(e, e->cursor, nx, 0); ed_delete_range(e, e->cursor, nx); }
        break;
    }
    case 's': {
        size_t nx = next_boundary(pt, e->cursor);
        if (nx > e->cursor) ed_delete_range(e, e->cursor, nx);
        enter_insert(); break;
    }
    case 'D': { size_t le = line_end_of(pt, e->cursor); yank_range(e, e->cursor, le, 0); ed_delete_range(e, e->cursor, le); break; }
    case 'C': { size_t le = line_end_of(pt, e->cursor); ed_delete_range(e, e->cursor, le); enter_insert(); break; }
    case 'd': case 'c': case 'y': g.pending = (char)cp; return;

    /* paste */
    case 'p': case 'P': {
        if (!g.yank) break;
        if (g.yank_line) {
            size_t at = (cp=='p') ? (line_end_of(pt, e->cursor) < len ? line_end_of(pt, e->cursor)+1 : len)
                                  : line_start_of(pt, e->cursor);
            edit_insert_at(e, at, g.yank, g.yank_len);
            e->cursor = at;
        } else {
            size_t at = (cp=='p') ? next_boundary(pt, e->cursor) : e->cursor;
            edit_insert_at(e, at, g.yank, g.yank_len);
            e->cursor = at + g.yank_len - (g.yank_len?1:0);
        }
        break;
    }

    /* undo (redo is Ctrl-R / Cmd-Shift-Z, handled in on_key) */
    case 'u': editor_undo(e); break;

    /* visual mode */
    case 'v':
        if (g.mode == MODE_VISUAL) g.mode = MODE_NORMAL;
        else { g.mode = MODE_VISUAL; e->anchor = e->cursor; }
        break;

    /* command line */
    case ':': g.cmd_active = 1; g.cmd_len = 0; break;

    default: break;
    }

    /* in visual mode, an operator key acts on the selection */
    if (g.mode == MODE_VISUAL && (cp=='d'||cp=='x'||cp=='y'||cp=='c')) {
        size_t a = e->cursor < e->anchor ? e->cursor : e->anchor;
        size_t b = e->cursor < e->anchor ? e->anchor : e->cursor;
        b = next_boundary(pt, b); /* inclusive */
        if (cp=='y') { yank_range(e, a, b, 0); e->cursor = a; }
        else { yank_range(e, a, b, 0); ed_delete_range(e, a, b); if (cp=='c') enter_insert(); }
        if (cp!='c') g.mode = MODE_NORMAL;
    }
    g.count = 0;
}

/* ---- UI zoom (Cmd +/-) ---- */

/* Rebuild the font + renderer at the current fb_scale * ui_scale. The whole UI
 * scales because every layout metric (line height, cell advance, gutter, tab
 * strip, panels) is derived from the font. Requires a current GL context. */
static void reload_font(void) {
    float px = g.base_pt * g.fb_scale * g.ui_scale;
    if (px < 6.0f) px = 6.0f;
    if (px > 96.0f) px = 96.0f;
    Font *nf = font_load(FONT_PATH, px);
    if (!nf) return;
    Renderer *nr = renderer_new(nf);
    if (!nr) { font_free(nf); return; }
    if (g.rend) renderer_free(g.rend);
    if (g.font) font_free(g.font);
    g.font = nf;
    g.rend = nr;
}

static void ui_zoom(int dir) {
    if (dir == 0) g.ui_scale = 1.0f;            /* Cmd-0: reset */
    else g.ui_scale *= (dir > 0) ? 1.1f : (1.0f / 1.1f);
    if (g.ui_scale < 0.5f) g.ui_scale = 0.5f;
    if (g.ui_scale > 3.0f) g.ui_scale = 3.0f;
    reload_font();
    config_save();
    snprintf(g.info, sizeof g.info, "zoom %d%%", (int)(g.ui_scale * 100.0f + 0.5f));
}

/* ---- GLFW callbacks ---- */
static void on_char(GLFWwindow *w, unsigned int cp) {
    (void)w;
    g.last_activity = glfwGetTime();
    if (g.mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) return; /* shortcut, not text */

    if (g.overlay == OVERLAY_PALETTE) {
        if (cp >= 32 && cp < 127 && g.query_len < (int)sizeof(g.query) - 1) {
            g.query[g.query_len++] = (char)cp;
            palette_refilter();
        }
        return;
    }
    if (g.overlay == OVERLAY_SEARCH) {
        if (cp >= 32 && cp < 127 && g.s_query_len < (int)sizeof(g.s_query) - 1) {
            g.s_query[g.s_query_len++] = (char)cp;
            search_run();
        }
        return;
    }
    if (g.cmd_active) {
        if (cp >= 32 && cp < 127 && g.cmd_len < (int)sizeof(g.cmd) - 1)
            g.cmd[g.cmd_len++] = (char)cp;
        return;
    }
    if (!cur()->buf) return; /* empty state (folder open, no file): nothing to type into */
    if (g.mode == MODE_INSERT) {
        char buf[4];
        int n = utf8_encode(cp, buf);
        ed_insert(cur(), buf, (size_t)n);
        cur()->group_open = 1; /* coalesce the rest of this typing burst */
        return;
    }
    vim_normal_char(cp); /* NORMAL or VISUAL */
}

static void on_key(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc;
    g.mods = mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    g.last_activity = glfwGetTime();
    Editor *e = cur();
    const PieceTable *pt = e->buf ? buffer_pt(e->buf) : NULL;

    int cmd = mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL);
    if (cmd && key == GLFW_KEY_P) { palette_open(); return; }
    if (cmd && (mods & GLFW_MOD_SHIFT) && key == GLFW_KEY_F) { search_open(); return; }
    if (cmd && key == GLFW_KEY_B) { g.show_sidebar = !g.show_sidebar; config_save(); return; }
    if (cmd && key == GLFW_KEY_S) {
        if (e->path && buffer_save(e->buf, e->path) == 0)
            fprintf(stderr, "saved %s\n", e->path);
        return;
    }
    if (cmd && key == GLFW_KEY_RIGHT_BRACKET) { tab_goto(+1); return; }
    if (cmd && key == GLFW_KEY_LEFT_BRACKET) { tab_goto(-1); return; }
    if (cmd && key == GLFW_KEY_W) { close_tab(g.active); return; }
    if (cmd && key == GLFW_KEY_Z) { /* Cmd-Z undo, Cmd-Shift-Z redo */
        if (mods & GLFW_MOD_SHIFT) editor_redo(cur()); else editor_undo(cur());
        return;
    }
    if ((mods & GLFW_MOD_CONTROL) && key == GLFW_KEY_R) { editor_redo(cur()); return; }
    if (cmd && (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD)) { ui_zoom(+1); return; }
    if (cmd && (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)) { ui_zoom(-1); return; }
    if (cmd && (key == GLFW_KEY_0 || key == GLFW_KEY_KP_0)) { ui_zoom(0); return; }
    if ((mods & GLFW_MOD_ALT) && key == GLFW_KEY_Z) {
        g.wrap = !g.wrap;
        config_save();
        snprintf(g.info, sizeof g.info, "word wrap %s", g.wrap ? "on" : "off");
        return;
    }

    /* palette navigation */
    if (g.overlay == OVERLAY_PALETTE) {
        switch (key) {
        case GLFW_KEY_ESCAPE: g.overlay = OVERLAY_NONE; break;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: palette_accept(); break;
        case GLFW_KEY_UP: if (g.sel > 0) g.sel--; break;
        case GLFW_KEY_DOWN: if (g.sel + 1 < g.filtered_n) g.sel++; break;
        case GLFW_KEY_BACKSPACE:
            if (g.query_len > 0) { g.query_len--; palette_refilter(); }
            break;
        }
        return;
    }

    /* content-search navigation */
    if (g.overlay == OVERLAY_SEARCH) {
        int n = g.search ? (int)search_count(g.search) : 0;
        switch (key) {
        case GLFW_KEY_ESCAPE: g.overlay = OVERLAY_NONE; break;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: search_accept(); break;
        case GLFW_KEY_UP: if (g.s_sel > 0) g.s_sel--; break;
        case GLFW_KEY_DOWN: if (g.s_sel + 1 < n) g.s_sel++; break;
        case GLFW_KEY_BACKSPACE:
            if (g.s_query_len > 0) { g.s_query_len--; search_run(); }
            break;
        }
        return;
    }

    /* command line */
    if (g.cmd_active) {
        switch (key) {
        case GLFW_KEY_ESCAPE: g.cmd_active = 0; g.cmd_len = 0; break;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: cmd_exec(); break;
        case GLFW_KEY_BACKSPACE: if (g.cmd_len > 0) g.cmd_len--; break;
        }
        return;
    }

    if (!pt) return;

    if (key == GLFW_KEY_ESCAPE) {
        if (g.pop.active) { popover_close(); return; } /* first Esc dismisses it */
        if (g.mode != MODE_NORMAL) g.mode = MODE_NORMAL;
        g.pending = 0; g.op_g = 0; g.count = 0; g.info[0] = '\0';
        e->group_open = 0;
        return;
    }

    /* while the popover is open, the arrow keys scroll it (and j/k too, leaving
     * the cursor put); any other key falls through and dismisses it. */
    if (g.pop.active && g.mode != MODE_INSERT &&
        (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN)) {
        int maxscroll = g.pop.total_rows - g.pop.vis_rows;
        if (maxscroll < 0) maxscroll = 0;
        g.pop.scroll += (key == GLFW_KEY_DOWN) ? 1 : -1;
        if (g.pop.scroll < 0) g.pop.scroll = 0;
        if (g.pop.scroll > maxscroll) g.pop.scroll = maxscroll;
        return;
    }

    if (g.mode == MODE_INSERT) {
        switch (key) {
        case GLFW_KEY_BACKSPACE:
            if (e->cursor > 0) {
                size_t prev = prev_boundary(pt, e->cursor);
                ed_delete_range(e, prev, e->cursor);
            }
            e->group_open = 1; /* coalesce a run of backspaces */
            break;
        case GLFW_KEY_DELETE:
            if (e->cursor < pt_length(pt))
                ed_delete_range(e, e->cursor, next_boundary(pt, e->cursor));
            e->group_open = 1;
            break;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: ed_insert(e, "\n", 1); e->group_open = 1; break;
        case GLFW_KEY_TAB: ed_insert(e, "    ", 4); e->group_open = 1; break;
        case GLFW_KEY_LEFT: e->cursor = prev_boundary(pt, e->cursor); break;
        case GLFW_KEY_RIGHT: e->cursor = next_boundary(pt, e->cursor); break;
        case GLFW_KEY_UP: move_vert(e, -1); break;
        case GLFW_KEY_DOWN: move_vert(e, +1); break;
        }
        return;
    }

    /* NORMAL / VISUAL: arrows also move (convenience) */
    switch (key) {
    case GLFW_KEY_LEFT: e->cursor = prev_boundary(pt, e->cursor); g.info[0]='\0'; break;
    case GLFW_KEY_RIGHT: e->cursor = next_boundary(pt, e->cursor); g.info[0]='\0'; break;
    case GLFW_KEY_UP: move_vert(e, -1); g.info[0]='\0'; break;
    case GLFW_KEY_DOWN: move_vert(e, +1); g.info[0]='\0'; break;
    }
}

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    float line_h = font_line_height(g.font);
    /* the content-search overlay is the focused element: the wheel moves the
     * selection through the results */
    if (g.overlay == OVERLAY_SEARCH) {
        int n = g.search ? (int)search_count(g.search) : 0;
        g.s_sel -= (int)dy;
        if (g.s_sel < 0) g.s_sel = 0;
        if (g.s_sel >= n) g.s_sel = n ? n - 1 : 0;
        return;
    }
    /* an open popover is the focused element: the wheel scrolls it */
    if (g.pop.active && g.pop.total_rows > g.pop.vis_rows) {
        g.pop.scroll -= (int)dy;
        if (g.pop.scroll < 0) g.pop.scroll = 0;
        int maxscroll = g.pop.total_rows - g.pop.vis_rows;
        if (g.pop.scroll > maxscroll) g.pop.scroll = maxscroll;
        return;
    }
    if (g.ws && g.show_sidebar && mx * g.fb_scale < g.m_side_px) {
        g.side_scroll -= (float)dy * line_h * 3.0f;
        if (g.side_scroll < 0) g.side_scroll = 0;
    } else {
        cur()->scroll_y -= (float)dy * line_h * 3.0f;
        if (cur()->scroll_y < 0) cur()->scroll_y = 0;
    }
}

static void on_mouse(GLFWwindow *w, int button, int action, int mods) {
    (void)mods;
    if (action != GLFW_PRESS) return;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;

    if (g.pop.active) popover_close(); /* a click anywhere dismisses the popover */

    /* Title-bar band: our GL view covers it, so reproduce the native gestures —
     * drag to move, double-click to zoom/minimise (per System Settings), and
     * right-/control-click for the document path menu. */
    if (y < g.m_header_h) {
        void *nw = glfwGetCocoaWindow(w);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) { mac_window_titlebar_menu(nw); return; }
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        static double last_click; static float lx, ly;
        double now = glfwGetTime();
        float dx = x - lx, dy = y - ly;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int dbl = (now - last_click < 0.4) && dx < 6.0f && dy < 6.0f;
        last_click = now; lx = x; ly = y;
        if (dbl) mac_window_titlebar_doubleclick(nw);
        else mac_window_drag(nw);
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    g.last_activity = glfwGetTime();

    if (g.ws && g.show_sidebar && x < g.m_side_px) {
        int row = (int)((y - g.m_header_h - g.m_side_pad + g.side_scroll) / g.m_line_h);
        if (row < 0) return;
        const WsEntry *e = ws_visible(g.ws, (size_t)row);
        if (!e) return;
        if (e->is_dir) {
            ws_visible_toggle(g.ws, (size_t)row); /* fold/unfold the folder */
        } else {
            /* single click peeks (reuses one preview tab so a flurry of clicks
             * doesn't pile up tabs); double click on the same row pins it. */
            static double last_click; static int last_row = -1;
            double now = glfwGetTime();
            int dbl = (now - last_click < 0.4) && row == last_row;
            last_click = now; last_row = row;
            char *full = ws_fullpath(g.ws, e->rel);
            open_path_mode(full, dbl ? 0 : 1);
            free(full);
        }
        return;
    }

    /* tab strip: click to switch; click the trailing 'x' to close */
    if (g.m_top_pad > 0 && y < g.m_top_pad && x >= g.m_side_px && g.m_tab_w > 0) {
        int idx = (int)((x - g.m_side_px) / g.m_tab_w);
        if (idx >= 0 && idx < g.tab_count) {
            float tx = g.m_side_px + (float)idx * g.m_tab_w;
            if (x > tx + g.m_tab_w - g.m_adv * 1.8f) close_tab(idx);
            else { g.active = idx; g.mode = MODE_NORMAL; }
        }
        return;
    }

    /* click in the text area: map (visual row, x) back to a byte offset */
    if (cur()->buf && cur()->vstart && x >= g.m_text_x) {
        Editor *e = cur();
        const PieceTable *pt = buffer_pt(e->buf);
        size_t lines = pt_line_count(pt);
        int vrow = (int)((y - g.m_top_pad + e->scroll_y) / g.m_line_h);
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
        int cols = e->wrap_cols > 0 ? e->wrap_cols : (1 << 28);
        int nb = wrap_line(buf, llen, cols, brk, WRAP_MAXBRK);
        if (sub >= nb) sub = nb - 1;
        int rs = brk[sub], re = (sub + 1 < nb) ? brk[sub + 1] : (int)llen;

        int want = (int)((x - g.m_text_x) / g.m_adv + 0.5f);
        int xcol = 0, off = rs;
        while (off < re && xcol < want) { xcol += cell_w((unsigned char)buf[off]); off++; }
        e->cursor = ls + (size_t)off;
    }
}

/* ---- drawing ---- */
#define MAXLINE 8192
#define MAXSPANS 4096

static void draw_text_run(Font *f, Renderer *r, const char *s, int n, float x,
                          float y, Color c) {
    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1, s0, t0, s1, t1;
        if (font_quad(f, (unsigned char)s[i], &x, &y, &x0, &y0, &x1, &y1, &s0,
                      &t0, &s1, &t1))
            renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1, c.r, c.g, c.b, 1.0f);
    }
}

static const char *base_name(const char *path) {
    if (!path) return "[scratch]";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* A filled triangle, drawn as stacked horizontal slices since the renderer only
 * knows rectangles. `dir`: 0 = points right (collapsed), 1 = points down
 * (expanded). (x,y) is the top-left of the size×size bounding box. */
static void draw_triangle(Renderer *r, float x, float y, float size, int dir,
                          Color c) {
    const int slices = 7;
    float step = size / (float)slices;
    for (int i = 0; i < slices; i++) {
        float t = ((float)i + 0.5f) / (float)slices; /* 0..1 down the box */
        float w, ox;
        if (dir) {                 /* down-pointing: wide at top, apex at bottom */
            w = size * (1.0f - t);
            ox = x + (size - w) * 0.5f;
            renderer_rect(r, ox, y + (float)i * step, w, step + 0.5f, c.r, c.g, c.b, 1.0f);
        } else {                   /* right-pointing: tall at left, apex at right */
            float h = size * (1.0f - t);
            float oy = y + (size - h) * 0.5f;
            renderer_rect(r, x + (float)i * step, oy, step + 0.5f, h, c.r, c.g, c.b, 1.0f);
        }
    }
}

/* A small folder glyph: a body rectangle with a little tab on its top-left. */
static void draw_folder_icon(Renderer *r, float x, float y, float size, Color c) {
    float tab_h = size * 0.22f;
    renderer_rect(r, x, y + tab_h * 0.4f, size * 0.5f, tab_h, c.r, c.g, c.b, 1.0f);
    renderer_rect(r, x, y + tab_h, size, size - tab_h, c.r, c.g, c.b, 1.0f);
}

/* A small file glyph: a document rectangle with a folded top-right corner
 * (punched out by overdrawing the corner with the panel background). */
static void draw_file_icon(Renderer *r, float x, float y, float size, Color c) {
    float w = size * 0.78f, h = size;
    float fx = x + (size - w) * 0.5f;
    renderer_rect(r, fx, y, w, h, c.r, c.g, c.b, 1.0f);
    float fold = size * 0.32f;
    renderer_rect(r, fx + w - fold, y, fold, fold, 0.09f, 0.10f, 0.12f, 1.0f);
}

static void draw_sidebar(int fb_h, float adv, float line_h, float ascent,
                         float side_px, float top_y) {
    Renderer *r = g.rend;
    Font *font = g.font;
    /* panel background (translucent with the rest of the window) */
    renderer_rect(r, 0, top_y, side_px, (float)fb_h - top_y, 0.09f, 0.10f, 0.12f, g.opacity);
    renderer_rect(r, side_px - 1.0f, top_y, 1.0f, (float)fb_h - top_y, 0.04f, 0.05f, 0.06f, g.opacity);

    float pad = g.m_side_pad;              /* overall top/left breathing room */
    float content_top = top_y + pad;
    size_t n = ws_visible_count(g.ws);
    int first = (int)(g.side_scroll / line_h);
    if (first < 0) first = 0;
    int rows = (int)((fb_h - content_top) / line_h) + 1;
    for (int i = first; i < first + rows && i < (int)n; i++) {
        const WsEntry *e = ws_visible(g.ws, (size_t)i);
        float top = content_top + (float)i * line_h - g.side_scroll;
        float baseline = top + ascent;

        /* per-row x layout: [pad][indent][chevron][icon][gap][name] */
        float row_x = pad + (float)e->depth * adv * 1.3f;
        float icon_sz = line_h * 0.5f;
        float icon_y = top + (line_h - icon_sz) * 0.5f;
        float chevron_x = row_x;
        float icon_x = chevron_x + adv;
        float name_x = icon_x + adv * 1.4f;

        /* active file = the entry whose rel path matches the open file's tail */
        int active = 0;
        if (cur()->path && !e->is_dir) {
            size_t pl = strlen(cur()->path), rl = strlen(e->rel);
            active = pl >= rl && !strcmp(cur()->path + pl - rl, e->rel) &&
                     (pl == rl || cur()->path[pl - rl - 1] == '/');
        }
        if (active)
            renderer_rect(r, 0, top, side_px, line_h, 0.16f, 0.18f, 0.22f, 1.0f);

        /* directories are brighter/cooler; files are softer */
        Color c = e->is_dir ? (Color){0.58f, 0.70f, 0.92f}
                            : (Color){0.78f, 0.80f, 0.84f};

        if (e->is_dir) {
            Color chev = {0.55f, 0.60f, 0.68f};
            float cs = icon_sz * 0.7f;
            draw_triangle(r, chevron_x + (adv - cs) * 0.5f,
                          top + (line_h - cs) * 0.5f, cs, e->collapsed ? 0 : 1, chev);
            draw_folder_icon(r, icon_x, icon_y, icon_sz, c);
        } else {
            draw_file_icon(r, icon_x, icon_y, icon_sz, c);
        }

        int nl = (int)strlen(e->name);
        int budget = g.side_cells - e->depth - 4;
        if (nl > budget) nl = budget;
        if (nl < 0) nl = 0;
        draw_text_run(font, r, e->name, nl, name_x, baseline, c);
    }
}

/* The tab strip across the top of the text area. Tabs are fixed width so the
 * hit-test in on_mouse can recover the tab index from an x coordinate. */
static void draw_tabs(int fb_w, float side_px, float adv, float ascent,
                      float tab_h, float top_y) {
    Renderer *r = g.rend;
    Font *font = g.font;
    renderer_rect(r, side_px, top_y, (float)fb_w - side_px, tab_h, 0.08f, 0.09f, 0.11f, g.opacity);

    float tab_w = adv * 18.0f;
    g.m_tab_w = tab_w;
    for (int i = 0; i < g.tab_count; i++) {
        float x = side_px + (float)i * tab_w;
        if (x >= (float)fb_w) break;
        int act = (i == g.active);
        renderer_rect(r, x, top_y, tab_w - 1.0f, tab_h, act ? 0.15f : 0.10f,
                      act ? 0.16f : 0.11f, act ? 0.21f : 0.13f, 1.0f);
        if (act) /* accent underline on the focused tab */
            renderer_rect(r, x, top_y + tab_h - 2.0f, tab_w - 1.0f, 2.0f, 0.45f, 0.60f, 0.95f, 1.0f);

        Editor *e = &g.tabs[i];
        char label[64];
        snprintf(label, sizeof label, "%s%s", base_name(e->path),
                 e->dirty ? " *" : "");
        int nl = (int)strlen(label);
        int budget = (int)(tab_w / adv) - 3;
        if (nl > budget) nl = budget;
        if (nl < 0) nl = 0;
        Color c = act ? (Color){0.92f, 0.93f, 0.95f}
                      : (Color){0.62f, 0.66f, 0.72f};
        draw_text_run(font, r, label, nl, x + adv * 0.6f, top_y + ascent + 2.0f, c);
        draw_text_run(font, r, "x", 1, x + tab_w - adv * 1.5f, top_y + ascent + 2.0f,
                      (Color){0.50f, 0.54f, 0.60f});
    }
}

/* The native-titlebar band: a draggable bar across the top of the window where
 * the macOS traffic-light buttons float (they're drawn by the OS, top-left).
 * We fill it to match the surrounding chrome and centre the title text. */
static void draw_header(int fb_w, float adv, float ascent, float header_h) {
    Renderer *r = g.rend;
    Font *font = g.font;
    renderer_rect(r, 0, 0, (float)fb_w, header_h, 0.07f, 0.08f, 0.10f, g.opacity);
    renderer_rect(r, 0, header_h - 1.0f, (float)fb_w, 1.0f, 0.04f, 0.05f, 0.06f, g.opacity);

    Editor *e = cur();
    const char *title = g.ws ? base_name(ws_root(g.ws))
                             : (e->path ? base_name(e->path) : "Wave");
    if (!title) title = "Wave";
    int nl = (int)strlen(title);
    float tw = (float)nl * adv;
    float tx = ((float)fb_w - tw) / 2.0f;
    /* keep the title clear of the traffic lights at the far left */
    float lights = 78.0f * g.fb_scale;
    if (tx < lights) tx = lights;
    float baseline = (header_h - ascent) / 2.0f + ascent;
    draw_text_run(font, r, title, nl, tx, baseline, (Color){0.66f, 0.70f, 0.76f});
}

static void draw_palette(int fb_w, int fb_h, float adv, float line_h,
                         float ascent) {
    Renderer *r = g.rend;
    Font *font = g.font;
    float pw = (float)fb_w * 0.6f;
    float px = ((float)fb_w - pw) / 2.0f;
    float py = line_h * 2.0f;
    int rows = 12;
    float ph = line_h * (rows + 2);

    renderer_rect(r, px - 6, py - 6, pw + 12, ph + 12, 0.0f, 0.0f, 0.0f, 0.5f);
    renderer_rect(r, px, py, pw, ph, 0.13f, 0.14f, 0.17f, 1.0f);
    /* query line */
    renderer_rect(r, px, py, pw, line_h + 4, 0.17f, 0.18f, 0.22f, 1.0f);
    char qline[300];
    g.query[g.query_len] = '\0';
    snprintf(qline, sizeof qline, "> %s", g.query);
    draw_text_run(font, r, qline, (int)strlen(qline), px + adv, py + ascent + 2,
                  (Color){0.92f, 0.93f, 0.95f});
    /* results */
    int shown = g.filtered_n < rows ? g.filtered_n : rows;
    int start = 0;
    if (g.sel >= rows) start = g.sel - rows + 1;
    for (int i = 0; i < shown; i++) {
        int idx = start + i;
        if (idx >= g.filtered_n) break;
        const WsEntry *e = ws_entry(g.ws, g.filtered[idx]);
        float top = py + line_h + 4 + (float)i * line_h;
        if (idx == g.sel)
            renderer_rect(r, px, top, pw, line_h, 0.22f, 0.30f, 0.42f, 1.0f);
        int nl = (int)strlen(e->rel);
        if (nl > (int)(pw / adv) - 2) nl = (int)(pw / adv) - 2;
        draw_text_run(font, r, e->rel, nl, px + adv, top + ascent,
                      (Color){0.85f, 0.87f, 0.90f});
    }
}

/* Project-wide content-search overlay (Cmd-Shift-F): a query line plus a
 * scrolling list of `path:line` + the matched text, ripgrep-ranked by file. */
static void draw_search(int fb_w, int fb_h, float adv, float line_h,
                        float ascent) {
    Renderer *r = g.rend;
    Font *font = g.font;
    float pw = (float)fb_w * 0.72f;
    float px = ((float)fb_w - pw) / 2.0f;
    float py = line_h * 2.0f;
    int rows = 14;
    float ph = line_h * (rows + 2);
    int maxcells = (int)(pw / adv) - 2;
    if (maxcells < 8) maxcells = 8;

    renderer_rect(r, px - 6, py - 6, pw + 12, ph + 12, 0.0f, 0.0f, 0.0f, 0.5f);
    renderer_rect(r, px, py, pw, ph, 0.13f, 0.14f, 0.17f, 1.0f);

    /* query line, with a result-count / status hint on the right */
    renderer_rect(r, px, py, pw, line_h + 4, 0.17f, 0.18f, 0.22f, 1.0f);
    char qline[300];
    g.s_query[g.s_query_len] = '\0';
    snprintf(qline, sizeof qline, "search: %s", g.s_query);
    draw_text_run(font, r, qline, (int)strlen(qline), px + adv, py + ascent + 2,
                  (Color){0.92f, 0.93f, 0.95f});
    int n = g.search ? (int)search_count(g.search) : 0;
    char status[64];
    if (!g.search && g.s_query_len)
        snprintf(status, sizeof status, "ripgrep unavailable");
    else if (g.search && search_running(g.search))
        snprintf(status, sizeof status, "searching…");
    else if (g.search && search_truncated(g.search))
        snprintf(status, sizeof status, "%d+ matches", n);
    else
        snprintf(status, sizeof status, "%d match%s", n, n == 1 ? "" : "es");
    float sw = (float)strlen(status) * adv;
    draw_text_run(font, r, status, (int)strlen(status), px + pw - sw - adv,
                  py + ascent + 2, (Color){0.55f, 0.58f, 0.64f});

    /* results: keep the selection in view */
    int start = 0;
    if (g.s_sel >= rows) start = g.s_sel - rows + 1;
    for (int i = 0; i < rows; i++) {
        int idx = start + i;
        if (idx >= n) break;
        const SearchHit *h = search_hit(g.search, (size_t)idx);
        if (!h) break;
        float top = py + line_h + 4 + (float)i * line_h;
        if (idx == g.s_sel)
            renderer_rect(r, px, top, pw, line_h, 0.22f, 0.30f, 0.42f, 1.0f);

        /* "path:line" prefix in an accent color, then the matched text */
        char loc[1100];
        snprintf(loc, sizeof loc, "%s:%d:", h->path, h->line);
        int ll = (int)strlen(loc);
        if (ll > maxcells) ll = maxcells;
        draw_text_run(font, r, loc, ll, px + adv, top + ascent,
                      (Color){0.55f, 0.74f, 0.95f});
        int remain = maxcells - ll - 1;
        if (remain > 2) {
            int tl = (int)strlen(h->text);
            if (tl > remain) tl = remain;
            draw_text_run(font, r, h->text, tl,
                          px + adv + (float)(ll + 1) * adv, top + ascent,
                          (Color){0.78f, 0.80f, 0.84f});
        }
    }
}

/* The `gh` popover: a floating, scrollable box of diagnostics + hover anchored
 * to the cursor. It word-wraps to a column budget, caps its height (scrolling
 * past that), and re-places itself every frame — below the cursor when there's
 * room, otherwise above, always clamped fully on screen. `cur_top`/`anchor_x`
 * are the cursor's top-left in framebuffer pixels. */
#define POP_MAX_ROWS 12   /* max visible rows before the box scrolls */
#define POP_MAX_COLS 56   /* wrap width, in character cells */
#define POP_DISPROWS 1024 /* cap on total wrapped rows we lay out */

static void draw_popover(int fb_w, int fb_h, Font *font, Renderer *r, float adv,
                         float line_h, float ascent, float top_pad, float bar_h,
                         float anchor_x, float cur_top) {
    if (!g.pop.active || !g.pop.text[0]) return;

    /* 1. wrap the text into display rows (slices into g.pop.text). */
    static int rs_off[POP_DISPROWS], rs_len[POP_DISPROWS];
    const char *t = g.pop.text;
    int n = (int)strlen(t);
    int nrows = 0, longest = 0;
    int p = 0;
    while (p <= n && nrows < POP_DISPROWS) {
        int ls = p;
        while (p < n && t[p] != '\n') p++;
        int llen = p - ls;
        int off = 0;
        for (;;) {
            int seg = llen - off;
            if (seg > POP_MAX_COLS) seg = POP_MAX_COLS;
            if (off + seg < llen) { /* soft-wrap on the last space in the segment */
                int br = seg;
                while (br > POP_MAX_COLS / 3 && t[ls + off + br] != ' ') br--;
                if (br > POP_MAX_COLS / 3) seg = br;
            }
            rs_off[nrows] = ls + off;
            rs_len[nrows] = seg;
            if (seg > longest) longest = seg;
            nrows++;
            off += seg;
            if (off < llen && t[ls + off] == ' ') off++; /* eat the break space */
            if (off >= llen || nrows >= POP_DISPROWS) break;
        }
        p++; /* past the '\n' (or past the end) */
    }
    if (nrows == 0) return;

    /* 2. geometry. padding, then box width from the longest row (clamped). */
    float padx = adv * 0.8f, pady = line_h * 0.35f;
    float gap = 6.0f * g.fb_scale;
    float bottom_limit = (float)fb_h - bar_h;

    /* how many rows fit on the roomier side, capped by the max-height. */
    float room_below = bottom_limit - (cur_top + line_h + gap) - pady * 2.0f;
    float room_above = (cur_top - gap) - top_pad - pady * 2.0f;
    int fit_below = (int)(room_below / line_h);
    int fit_above = (int)(room_above / line_h);
    int fit = fit_below > fit_above ? fit_below : fit_above;
    if (fit < 1) fit = 1;
    int vis = nrows;
    if (vis > POP_MAX_ROWS) vis = POP_MAX_ROWS;
    if (vis > fit) vis = fit;

    int scrollable = nrows > vis;
    int maxscroll = nrows - vis;
    if (maxscroll < 0) maxscroll = 0;
    if (g.pop.scroll > maxscroll) g.pop.scroll = maxscroll;
    if (g.pop.scroll < 0) g.pop.scroll = 0;
    g.pop.total_rows = nrows;
    g.pop.vis_rows = vis;

    float sb_w = scrollable ? adv * 0.5f : 0.0f; /* scrollbar gutter */
    float box_w = (float)longest * adv + padx * 2.0f + sb_w;
    float max_w = (float)fb_w - 2.0f * adv;
    if (box_w > max_w) box_w = max_w;
    float box_h = (float)vis * line_h + pady * 2.0f;

    /* 3. placement: below the cursor if it fits there, else above; then clamp. */
    float x = anchor_x;
    float y;
    if (fit_below >= vis && cur_top + line_h + gap + box_h <= bottom_limit)
        y = cur_top + line_h + gap;
    else
        y = cur_top - gap - box_h;
    if (y < top_pad) y = top_pad;
    if (y + box_h > bottom_limit) y = bottom_limit - box_h;

    float left_limit = g.m_side_px; /* don't spill over the sidebar */
    if (x + box_w > (float)fb_w) x = (float)fb_w - box_w;
    if (x < left_limit) x = left_limit;
    if (x < 0) x = 0;

    /* 4. draw: border, background, rows, scrollbar. */
    float b = 1.0f * g.fb_scale;
    renderer_rect(r, x - b, y - b, box_w + 2 * b, box_h + 2 * b, 0.33f, 0.36f, 0.42f, 1.0f);
    renderer_rect(r, x, y, box_w, box_h, 0.16f, 0.17f, 0.20f, 1.0f);

    float content_w = box_w - padx * 2.0f - sb_w;
    int maxcells = (int)(content_w / adv);
    for (int i = 0; i < vis; i++) {
        int row = g.pop.scroll + i;
        if (row >= nrows) break;
        int len = rs_len[row];
        if (len > maxcells) len = maxcells;
        float ty = y + pady + ascent + (float)i * line_h;
        draw_text_run(font, r, t + rs_off[row], len, x + padx, ty,
                      (Color){0.88f, 0.90f, 0.93f});
    }

    if (scrollable) {
        float track_x = x + box_w - sb_w + sb_w * 0.25f;
        float track_w = sb_w * 0.5f;
        float thumb_h = box_h * (float)vis / (float)nrows;
        if (thumb_h < line_h * 0.5f) thumb_h = line_h * 0.5f;
        float thumb_y = y + (box_h - thumb_h) *
                            (float)g.pop.scroll / (float)maxscroll;
        renderer_rect(r, track_x, y, track_w, box_h, 0.24f, 0.26f, 0.30f, 1.0f);
        renderer_rect(r, track_x, thumb_y, track_w, thumb_h, 0.50f, 0.54f, 0.60f, 1.0f);
    }
}

static void draw_frame(int fb_w, int fb_h) {
    Font *font = g.font;
    Renderer *r = g.rend;
    Editor *e = cur();

    /* keep the native title path menu pointed at the active file (cheap pointer
     * guard so we only touch Cocoa when the front tab actually changes) */
    static const char *shown_path = (const char *)-1;
    if (g.native_titlebar && e->path != shown_path) {
        mac_window_set_file(glfwGetCocoaWindow(g.win), e->path);
        shown_path = e->path;
    }

    float line_h = font_line_height(font);
    float ascent = font_ascent(font);
    float adv = font_advance(font);

    float side_px = (g.ws && g.show_sidebar) ? adv * (float)g.side_cells : 0.0f;
    float gutter = adv * 5.0f;
    float pad = adv;
    float text_x = side_px + gutter + pad;
    float tab_h = line_h + 6.0f;
    /* native macOS titlebar: reserve a draggable band at the very top for the
     * OS traffic-light buttons that float there. They sit at a fixed 28pt, so
     * scale by the framebuffer scale only — not the user's Cmd-+/- zoom. */
    float header_h = g.native_titlebar ? (float)(int)(28.0f * g.fb_scale + 0.5f) : 0.0f;
    float tab_strip = (g.tab_count > 0) ? tab_h : 0.0f;
    float top_pad = header_h + tab_strip;

    /* cache for hit-testing */
    g.m_line_h = line_h; g.m_adv = adv; g.m_ascent = ascent;
    g.m_side_px = side_px; g.m_text_x = text_x; g.m_gutter = gutter;
    g.m_top_pad = top_pad; g.m_header_h = header_h; g.m_fb_h = fb_h;
    g.m_side_pad = line_h * 0.4f;

    renderer_begin(r, fb_w, fb_h, 0.11f, 0.12f, 0.14f, g.opacity);

    if (side_px > 0) draw_sidebar(fb_h, adv, line_h, ascent, side_px, header_h);
    if (tab_strip > 0) draw_tabs(fb_w, side_px, adv, ascent, tab_h, header_h);
    if (header_h > 0) draw_header(fb_w, adv, ascent, header_h);

    if (!e->buf) { /* empty state: a folder is open but no file has been chosen */
        const char *title = g.ws ? "No file open" : "Wave";
        const char *hint  = g.ws ? "Click a file in the sidebar, or press Cmd-P"
                                 : "Open a file or folder to get started";
        /* centre both lines in the editor area (right of the sidebar) */
        float area_x = side_px, area_w = (float)fb_w - side_px;
        float mid_y = top_pad + ((float)fb_h - top_pad) * 0.5f;
        float tw = (float)strlen(title) * adv;
        draw_text_run(font, r, title, (int)strlen(title),
                      area_x + (area_w - tw) * 0.5f, mid_y - line_h * 0.5f,
                      (Color){0.62f, 0.66f, 0.72f});
        float hw = (float)strlen(hint) * adv;
        draw_text_run(font, r, hint, (int)strlen(hint),
                      area_x + (area_w - hw) * 0.5f, mid_y + line_h,
                      (Color){0.42f, 0.46f, 0.52f});
        /* The Cmd-P palette and Cmd-Shift-F search are reachable from the empty
         * state too — the primary ways to open a file here — so draw whichever
         * is active on top before bailing. */
        if (g.overlay == OVERLAY_PALETTE)
            draw_palette(fb_w, fb_h, adv, line_h, ascent);
        else if (g.overlay == OVERLAY_SEARCH)
            draw_search(fb_w, fb_h, adv, line_h, ascent);
        renderer_flush(r);
        return;
    }

    const PieceTable *pt = buffer_pt(e->buf);
    size_t total_lines = pt_line_count(pt);

    /* wrap layout: column budget for the text area (a huge value = no wrap) */
    int cols = g.wrap ? (int)(((float)fb_w - text_x) / adv) : (1 << 28);
    if (cols < 4) cols = 4;
    wrap_build(e, cols);
    int total_vrows = e->vstart[total_lines];

    /* the cursor's visual sub-row within its (wrapped) logical line, and its x */
    size_t cr, cc;
    pt_offset_to_rowcol(pt, e->cursor, &cr, &cc);
    int cur_sub = 0, cur_xcol = 0;
    {
        static char cbuf[WRAP_MAXLINE];
        static int cbrk[WRAP_MAXBRK];
        size_t ls = pt_line_start(pt, cr);
        size_t le = (cr + 1 < total_lines) ? pt_line_start(pt, cr + 1) : pt_length(pt);
        size_t llen = le - ls;
        if (llen > 0 && byte_at(pt, le - 1) == '\n') llen--;
        if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
        pt_read(pt, ls, llen, cbuf);
        int nb = wrap_line(cbuf, llen, cols, cbrk, WRAP_MAXBRK);
        for (int k = 0; k < nb; k++) {
            int s = cbrk[k], en = (k + 1 < nb) ? cbrk[k + 1] : (int)llen;
            if ((int)cc >= s && ((int)cc < en || k == nb - 1)) { cur_sub = k; break; }
        }
        for (int i = cbrk[cur_sub]; i < (int)cc && i < (int)llen; i++)
            cur_xcol += cell_w((unsigned char)cbuf[i]);
    }
    int cur_vrow = e->vstart[cr] + cur_sub;

    /* Keep the cursor's visual row in view — but only when the cursor has
     * actually moved since the last frame. Doing it unconditionally every frame
     * would snap scroll_y straight back to the cursor and so swallow any manual
     * (trackpad/wheel) scroll the moment it happened. */
    {
        float view_h = (float)fb_h - top_pad - (line_h + 6.0f); /* minus status bar */
        if (e->cursor != e->seen_cursor) {
            float cy = (float)cur_vrow * line_h;
            if (cy < e->scroll_y) e->scroll_y = cy;
            else if (cy + line_h > e->scroll_y + view_h) e->scroll_y = cy + line_h - view_h;
            e->seen_cursor = e->cursor;
        }
        float maxscroll = (float)total_vrows * line_h - view_h;
        if (e->scroll_y > maxscroll) e->scroll_y = maxscroll;
        if (e->scroll_y < 0) e->scroll_y = 0;
    }

    int first_vrow = (int)(e->scroll_y / line_h);
    if (first_vrow < 0) first_vrow = 0;
    int vis_vrows = (int)((fb_h - top_pad) / line_h) + 2;
    int first_ln = line_at_vrow(e, first_vrow);
    int last_ln = line_at_vrow(e, first_vrow + vis_vrows);

    size_t win_start = pt_line_start(pt, (size_t)first_ln);
    size_t win_end = (last_ln + 1 < (int)total_lines)
                         ? pt_line_start(pt, (size_t)last_ln + 1)
                         : pt_length(pt);
    static HighlightSpan spans[MAXSPANS];
    size_t nspans = e->hl ? hl_spans(e->hl, win_start, win_end, spans, MAXSPANS) : 0;
    if (nspans > MAXSPANS) nspans = MAXSPANS; /* never index past the buffer */
    static Diagnostic diags[MAXDIAG];
    size_t ndiag = e->hl ? hl_diagnostics(e->hl, diags, MAXDIAG) : 0;
    if (ndiag > MAXDIAG) ndiag = MAXDIAG;

    /* If a language server has reported on this file, it is authoritative —
     * replace the tree-sitter syntax diagnostics with the server's. */
    Lsp *lsp = lsp_for(e);
    if (lsp && e->path) {
        static LspDiag ld[MAXDIAG];
        int published = 0;
        char *uri = path_to_uri(e->path);
        size_t nl = lsp_diagnostics(lsp, uri, ld, MAXDIAG, &published);
        free(uri);
        if (published) {
            if (nl > MAXDIAG) nl = MAXDIAG;
            ndiag = nl;
            for (size_t i = 0; i < nl; i++) {
                diags[i].start_row = (size_t)ld[i].start_line;
                diags[i].start_col = (size_t)ld[i].start_col;
                diags[i].end_row = (size_t)ld[i].end_line;
                diags[i].end_col = (size_t)ld[i].end_col;
                diags[i].start_byte = diags[i].end_byte = 0;
                diags[i].is_missing = 0;
                diags[i].message = "diagnostic";
            }
        }
    }

    /* visual selection range */
    size_t sel_a = 0, sel_b = 0;
    if (g.mode == MODE_VISUAL) {
        sel_a = e->cursor < e->anchor ? e->cursor : e->anchor;
        sel_b = e->cursor < e->anchor ? e->anchor : e->cursor;
        sel_b = next_boundary(pt, sel_b);
    }

    /* cursor blink: solid for the first half-second after any input, then a
     * ~1Hz on/off. Always solid when blinking is disabled (snapshot mode). */
    int cursor_on = 1;
    if (g.blink) {
        double phase = glfwGetTime() - g.last_activity;
        cursor_on = (phase - (double)(long)phase) < 0.5;
    }

    /* render visible visual rows */
    static char linebuf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    for (int ln = first_ln; ln < (int)total_lines; ln++) {
        if (e->vstart[ln] - first_vrow > vis_vrows) break; /* past the bottom */

        size_t ls = pt_line_start(pt, (size_t)ln);
        size_t le = (ln + 1 < (int)total_lines) ? pt_line_start(pt, (size_t)ln + 1)
                                                : pt_length(pt);
        size_t llen = le - ls;
        if (llen > 0 && byte_at(pt, le - 1) == '\n') llen--;
        if (llen > WRAP_MAXLINE) llen = WRAP_MAXLINE;
        pt_read(pt, ls, llen, linebuf);
        int nb = wrap_line(linebuf, llen, cols, brk, WRAP_MAXBRK);

        for (int k = 0; k < nb; k++) {
            float top = top_pad + (float)(e->vstart[ln] + k) * line_h - e->scroll_y;
            if (top > (float)fb_h) { k = nb; ln = (int)total_lines; break; }
            if (top + line_h < top_pad) continue; /* above the text area */
            float baseline = top + ascent;
            int rs = brk[k], re = (k + 1 < nb) ? brk[k + 1] : (int)llen;

            /* gutter line number on the first visual row only */
            if (k == 0) {
                char num[16];
                int nl = snprintf(num, sizeof num, "%d", ln + 1);
                float nx = side_px + gutter - adv * 0.5f - adv * (float)nl;
                draw_text_run(font, r, num, nl, nx, baseline, (Color){0.38f, 0.42f, 0.48f});
            }

            /* selection background, clipped to this row's byte span */
            if (g.mode == MODE_VISUAL) {
                size_t rowa = ls + (size_t)rs, rowb = ls + (size_t)re;
                size_t a = sel_a > rowa ? sel_a : rowa;
                size_t b = sel_b < rowb ? sel_b : rowb;
                if (sel_b > rowa && sel_a < rowb + 1 && b > a) {
                    int xa = 0, xb = 0;
                    for (int i = rs; i < (int)(a - ls) && i < (int)llen; i++) xa += cell_w((unsigned char)linebuf[i]);
                    xb = xa;
                    for (int i = (int)(a - ls); i < (int)(b - ls) && i < (int)llen; i++) xb += cell_w((unsigned char)linebuf[i]);
                    if (xb == xa) xb = xa + 1; /* selecting the trailing newline */
                    renderer_rect(r, text_x + adv * (float)xa, top, adv * (float)(xb - xa),
                                  line_h, 0.22f, 0.28f, 0.40f, 1.0f);
                }
            }

            /* Pre-filter spans to just those overlapping this visual row, so the
             * per-glyph color lookup scans a handful instead of all `nspans`.
             * Without this, a row of a huge minified line is O(chars * nspans)
             * and the frame crawls; with it the inner loop is tiny. Array order
             * is preserved so later spans still win (last match colors). */
            static int rowspans[MAXSPANS];
            int nrow = 0;
            size_t row_lo = ls + (size_t)rs, row_hi = ls + (size_t)re;
            for (size_t s = 0; s < nspans; s++)
                if (spans[s].start_byte < row_hi && spans[s].end_byte > row_lo)
                    rowspans[nrow++] = (int)s;

            /* glyphs for this visual row */
            float pen_x = text_x, pen_y = baseline;
            for (int i = rs; i < re; i++) {
                size_t abs = ls + (size_t)i;
                unsigned char ch = (unsigned char)linebuf[i];
                if (ch == '\t') { pen_x += adv * 4.0f; continue; }
                Color c = theme(NULL);
                for (int s = 0; s < nrow; s++)
                    if (abs >= spans[rowspans[s]].start_byte &&
                        abs < spans[rowspans[s]].end_byte)
                        c = theme(spans[rowspans[s]].name);
                float x0, y0, x1, y1, s0, t0, s1, t1;
                if (font_quad(font, ch, &pen_x, &pen_y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1))
                    renderer_glyph(r, x0, y0, x1, y1, s0, t0, s1, t1, c.r, c.g, c.b, 1.0f);
            }

            /* cursor, if it falls on this visual row */
            if ((int)cr == ln && cur_sub == k && cursor_on) {
                float cx = text_x + adv * (float)cur_xcol;
                if (g.mode == MODE_INSERT)
                    renderer_rect(r, cx, top + 2.0f, 2.0f, line_h - 4.0f, 0.95f, 0.85f, 0.30f, 1.0f);
                else
                    renderer_rect(r, cx, top + 2.0f, adv, line_h - 4.0f, 0.95f, 0.85f, 0.30f, 0.55f);
            }

            /* diagnostic underlines, clipped to this row's columns */
            for (size_t d = 0; d < ndiag; d++) {
                Diagnostic *dg = &diags[d];
                if ((int)dg->start_row > ln || (int)dg->end_row < ln) continue;
                int dc0 = (dg->start_row == (size_t)ln) ? (int)dg->start_col : 0;
                int dc1 = (dg->end_row == (size_t)ln) ? (int)dg->end_col : (int)llen;
                if (dc1 <= dc0) dc1 = dc0 + 1;
                int a = dc0 > rs ? dc0 : rs, b = dc1 < re ? dc1 : re;
                if (b <= a) continue;
                int xa = 0, xb = 0;
                for (int i = rs; i < a && i < (int)llen; i++) xa += cell_w((unsigned char)linebuf[i]);
                xb = xa;
                for (int i = a; i < b && i < (int)llen; i++) xb += cell_w((unsigned char)linebuf[i]);
                if (xb == xa) xb = xa + 1;
                renderer_rect(r, text_x + adv * (float)xa, top + line_h - 3.0f,
                              adv * (float)(xb - xa), 2.0f, 0.92f, 0.30f, 0.30f, 0.9f);
            }
        }
    }

    /* status / command line */
    float bar_h = line_h + 6.0f;
    renderer_rect(r, 0, (float)fb_h - bar_h, (float)fb_w, bar_h, 0.07f, 0.08f, 0.10f, g.opacity);
    char status[400];
    Color status_c = (Color){0.70f, 0.74f, 0.80f};
    if (g.cmd_active) {
        g.cmd[g.cmd_len] = '\0';
        snprintf(status, sizeof status, ":%s", g.cmd);
    } else if (g.info[0]) {
        snprintf(status, sizeof status, "%s", g.info);
        status_c = (Color){0.86f, 0.84f, 0.55f}; /* info stands out a touch */
    } else {
        const char *mode = g.mode == MODE_INSERT ? "INSERT"
                         : g.mode == MODE_VISUAL ? "VISUAL" : "NORMAL";
        size_t crow, ccol;
        pt_offset_to_rowcol(pt, e->cursor, &crow, &ccol);
        const char *lang = "text";
        const Language *L = lang_detect(e->path);
        if (L) lang = L->name;
        snprintf(status, sizeof status,
                 "%s  %s%s  %s  Ln %zu, Col %zu  %zu errs  [%d/%d]",
                 mode, e->path ? e->path : "[scratch]", e->dirty ? " *" : "",
                 lang, crow + 1, ccol + 1, ndiag, g.active + 1, g.tab_count);
    }
    draw_text_run(font, r, status, (int)strlen(status), pad,
                  (float)fb_h - bar_h + ascent + 2.0f, status_c);

    /* the `gh` popover floats over the text, anchored to the cursor (kept on
     * screen even when the cursor sits near an edge). Hidden under the palette. */
    if (g.overlay == OVERLAY_NONE) {
        float cur_screen_top = top_pad + (float)cur_vrow * line_h - e->scroll_y;
        float cur_screen_x = text_x + adv * (float)cur_xcol;
        if (cur_screen_top < top_pad) cur_screen_top = top_pad;
        if (cur_screen_top > (float)fb_h - bar_h) cur_screen_top = (float)fb_h - bar_h;
        draw_popover(fb_w, fb_h, font, r, adv, line_h, ascent, top_pad, bar_h,
                     cur_screen_x, cur_screen_top);
    }

    if (g.overlay == OVERLAY_PALETTE)
        draw_palette(fb_w, fb_h, adv, line_h, ascent);
    else if (g.overlay == OVERLAY_SEARCH)
        draw_search(fb_w, fb_h, adv, line_h, ascent);

    renderer_flush(r);
}

/* ---- entry ---- */
#include <sys/stat.h>

int main(int argc, char **argv) {
    const char *arg = argc > 1 ? argv[1] : NULL;

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

    const char *snapshot = getenv("WAVE_SNAPSHOT");
    if (snapshot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    /* don't spawn language servers during headless snapshots unless asked */
    g.no_lsp = (snapshot && !getenv("WAVE_LSP"));
    g.blink = (snapshot == NULL); /* keep the cursor solid for deterministic snapshots */

    /* UI preferences: defaults first, then the saved config overrides them.
     * Resolved before window creation so the transparent-framebuffer hint and
     * the initial zoom take effect immediately. */
    g.show_sidebar = 1;
    g.side_cells = 26;
    g.mode = MODE_NORMAL;
    g.wrap = 1; /* word-wrap on by default; toggle with Alt-Z or :nowrap */
    g.base_pt = 15.0f;
    g.ui_scale = 1.0f;
    g.opacity = 1.0f;
    g.blur = 0;
    g.native_titlebar = 1; /* macOS: float the traffic lights over our header */
    g.persist = (snapshot == NULL) || getenv("WAVE_PERSIST"); /* off in snapshots unless forced */
    if (g.persist) config_load();   /* saved prefs override the defaults above */
    { const char *sc = getenv("WAVE_SCALE"); if (sc) { float v = (float)atof(sc); if (v > 0.1f) g.ui_scale = v; } }

    /* a transparent framebuffer lets opacity<1 (and the macOS blur) show through */
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow *win = glfwCreateWindow(1100, 720, "Wave", NULL, NULL);
    if (!win) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    g.win = win;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    int win_w, fb_w, fb_h, tmp;
    glfwGetWindowSize(win, &win_w, &tmp);
    glfwGetFramebufferSize(win, &fb_w, &fb_h);
    g.fb_scale = win_w ? (float)fb_w / (float)win_w : 1.0f;

    if (g.blur) mac_set_blur(glfwGetCocoaWindow(win), 1);
    if (g.native_titlebar) mac_use_native_titlebar(glfwGetCocoaWindow(win), 1);

    g.font = font_load(FONT_PATH, g.base_pt * g.fb_scale * g.ui_scale);
    if (!g.font) { fprintf(stderr, "font load failed\n"); return 1; }

    /* Argument may be a folder (open as workspace) or a file. */
    if (arg) {
        struct stat st;
        if (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
            g.ws = ws_open(arg);
            g.root_uri = path_to_uri(arg); /* server root = the opened folder */
            /* Deliberately open no file: start on an empty state so clicking
             * through the sidebar (single-click previews) is the only way tabs
             * appear. The editor area shows a placeholder until then. */
        } else {
            /* root = the file's containing folder; set it before opening so the
             * server (started lazily on open) gets the right rootUri */
            char dir[4096];
            snprintf(dir, sizeof dir, "%s", arg);
            char *slash = strrchr(dir, '/');
            if (slash) { *slash = '\0'; g.ws = ws_open(dir[0] ? dir : "/"); }
            else g.ws = ws_open(".");
            g.root_uri = path_to_uri(g.ws ? ws_root(g.ws) : ".");
            open_path(arg);
        }
    }
    if (g.tab_count == 0 && !g.ws) {
        new_tab();
        cur()->buf = buffer_new();
        g.mode = MODE_INSERT;
    }

    g.rend = renderer_new(g.font);
    if (!g.rend) { fprintf(stderr, "renderer init failed\n"); return 1; }

    if (snapshot) {
        /* WAVE_OPEN: ':'-separated paths, each opened as its own tab. */
        const char *opens = getenv("WAVE_OPEN");
        if (opens) {
            char buf[4096];
            snprintf(buf, sizeof buf, "%s", opens);
            for (char *p = strtok(buf, ":"); p; p = strtok(NULL, ":"))
                open_path(p);
        }
        const char *typed = getenv("WAVE_TYPE");
        if (typed && cur()->buf) {
            g.mode = MODE_INSERT;
            for (const char *p = typed; *p; p++) {
                if (*p == '\\' && p[1] == 'n') { ed_insert(cur(), "\n", 1); p++; }
                else ed_insert(cur(), p, 1);
            }
        }
        if (cur()->hl) hl_update(cur()->hl);
        /* WAVE_KEYS: a run of NORMAL-mode keystrokes (e.g. "ggwgd"). */
        const char *keys = getenv("WAVE_KEYS");
        if (keys && cur()->buf) {
            g.mode = MODE_NORMAL;
            for (const char *p = keys; *p; p++) {
                if (cur()->hl) hl_update(cur()->hl);
                vim_normal_char((unsigned int)(unsigned char)*p);
            }
        }
        if (getenv("WAVE_PALETTE")) palette_open();
        /* WAVE_QUERY: prefill the palette query to verify fuzzy ranking. */
        const char *pq = getenv("WAVE_QUERY");
        if (pq && g.overlay == OVERLAY_PALETTE) {
            snprintf(g.query, sizeof g.query, "%s", pq);
            g.query_len = (int)strlen(g.query);
            palette_refilter();
        }
        /* WAVE_SEARCH: open the content-search overlay, run the query, and pump
         * ripgrep to completion so the single-frame snapshot shows real hits. */
        const char *sq = getenv("WAVE_SEARCH");
        if (sq) {
            search_open();
            snprintf(g.s_query, sizeof g.s_query, "%s", sq);
            g.s_query_len = (int)strlen(g.s_query);
            search_run();
            for (int i = 0; i < 500 && search_running(g.search); i++) {
                search_poll(g.search);
                if (!search_running(g.search)) break;
                usleep(10000);
            }
            if (g.search) search_poll(g.search);
            const char *ss = getenv("WAVE_SEARCHSEL");
            if (ss) g.s_sel = atoi(ss);
        }
        /* WAVE_POPTEST: force-open the popover with given text (\n = newline,
         * \\ = paragraph break) to verify max-height / scroll / placement. */
        const char *poptest = getenv("WAVE_POPTEST");
        if (poptest && cur()->buf) {
            char b[sizeof g.pop.base];
            size_t o = 0;
            for (const char *p = poptest; *p && o + 2 < sizeof b; p++) {
                if (*p == '\\' && p[1] == 'n') { b[o++] = '\n'; p++; }
                else if (*p == '|') { b[o++] = '\n'; b[o++] = '\n'; }
                else b[o++] = *p;
            }
            b[o] = '\0';
            snprintf(g.pop.base, sizeof g.pop.base, "%s", b);
            g.pop.loading = 0; g.pop.scroll = 0;
            popover_compose(NULL);
            const char *ps = getenv("WAVE_POPSCROLL");
            if (ps) g.pop.scroll = atoi(ps);
        }
        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        draw_frame(fb_w, fb_h);
        int rc = renderer_save_ppm(snapshot, fb_w, fb_h);
        fprintf(stderr, "snapshot %s (%dx%d) rc=%d\n", snapshot, fb_w, fb_h, rc);
        return rc;
    }

    glfwSetCharCallback(win, on_char);
    glfwSetKeyCallback(win, on_key);
    glfwSetScrollCallback(win, on_scroll);
    glfwSetMouseButtonCallback(win, on_mouse);

    while (!glfwWindowShouldClose(win)) {
        Editor *e = cur();
        /* re-assert the blur each frame: macOS clears it on some window events */
        if (g.blur) mac_set_blur(glfwGetCocoaWindow(win), 1);
        if (e->hl && (e->dirty || buffer_pending_edits(e->buf) > 0)) {
            hl_update(e->hl);
            e->dirty = 0;
        }

        /* drain the language servers and apply anything they sent back */
        for (int i = 0; i < g.nservers; i++) {
            Lsp *l = g.servers[i].l;
            if (!l) continue;
            lsp_poll(l);

            LspLocation loc;
            if (lsp_take_definition(l, &loc)) {
                if (open_path(loc.path) == 0) {
                    Editor *te = cur();
                    const PieceTable *pt = buffer_pt(te->buf);
                    size_t lines = pt_line_count(pt);
                    size_t ln = (size_t)loc.line < lines ? (size_t)loc.line
                                                         : (lines ? lines - 1 : 0);
                    size_t off = pt_line_start(pt, ln) + (size_t)loc.col;
                    if (off > pt_length(pt)) off = pt_length(pt);
                    te->cursor = off;
                    center_cursor(te);
                    snprintf(g.info, sizeof g.info, "def -> Ln %d, Col %d",
                             loc.line + 1, loc.col + 1);
                }
            }
            char hv[1024];
            if (lsp_take_hover(l, hv, sizeof hv)) {
                /* the hover reply lands here; fold it into the open popover */
                g.pop.loading = 0;
                if (g.pop.active) popover_compose(hv[0] ? hv : NULL);
            }
        }

        /* push a full-document didChange once the active file has edits pending */
        if (e->path && e->lsp_open && e->lsp_dirty) {
            Lsp *l = lsp_for(e);
            if (l && lsp_ready(l)) {
                char *uri = path_to_uri(e->path);
                char *txt = editor_text(e);
                lsp_did_change(l, uri, ++e->version, txt);
                e->lsp_dirty = 0;
                free(uri); free(txt);
            }
        }

        /* drain any pending ripgrep output for the live search overlay */
        if (g.search) search_poll(g.search);

        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        draw_frame(fb_w, fb_h);
        glfwSwapBuffers(win);
        /* poll faster while a server or a search is active so results stream in */
        glfwWaitEventsTimeout(
            (g.nservers || search_running(g.search)) ? 0.03 : 0.1);
    }

    renderer_free(g.rend);
    for (int i = 0; i < g.nservers; i++) lsp_stop(g.servers[i].l);
    if (g.search) search_free(g.search);
    free(g.root_uri);
    for (int i = 0; i < g.tab_count; i++) editor_close(&g.tabs[i]);
    if (g.ws) ws_free(g.ws);
    font_free(g.font);
    free(g.yank);
    free(g.filtered);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
