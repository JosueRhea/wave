/* wave_ffi.c — a narrow C ABI over Wave's headless core, for the GPUI front-end.
 *
 * libwave.a (CORE_SRC + tree-sitter) has no GL/GLFW/Cocoa in it — the same
 * thing the 28 test binaries link against. This shim exposes the shape the Rust
 * UI needs — commands in, state out — so the front-end never has to know the
 * layout of Editor, TabSet, Workspace or the piece table, and we need no
 * bindgen.
 *
 * Key dispatch mirrors main.c: printable input goes to editor_apply_text_input
 * in INSERT and edit_command_apply otherwise; special keys go to
 * editor_apply_insert_key in INSERT and editor_apply_motion_key otherwise. */

#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "diagnostics.h"
#include "edit_command.h"
#include "editor.h"
#include "highlight.h"
#include "command.h"
#include "complete.h"
#include "config.h"
#include "git_view.h"
#include "lsp_manager.h"
#include "mode.h"
#include "overlay.h"
#include "piece_table.h"
#include "popover.h"
#include "recent.h"
#include "runtime.h"
#include "standard.h"
#include "tabs.h"
#include "view.h"
#include "terminal.h"
#include "theme.h"
#include "updater.h"
#include "watch.h"
#include "workspace.h"
#include "yank.h"

#define WAVE_MAX_DIAGS 256

typedef struct WaveSession {
    TabSet tabs;
    Workspace *ws;
    ModalState modal;
    YankRegister yank;
    OverlayState overlay;
    CommandLine cmd;
    CompleteState comp;
    Popover pop;
    WaveConfig config;
    WatchService watch;
    RecentProjects recent;

    /* `/` buffer search reuses the command-line widget, as main.c does. */
    CommandLine buf_search;
    Editor *buf_search_editor;
    size_t buf_search_origin;
    char last_buf_search[256];

    LspManager lsp;
    /* Merged tree-sitter + server diagnostics for the active editor, refreshed
     * on poll and after edits so per-line lookups stay cheap. */
    Diagnostic diags[WAVE_MAX_DIAGS];
    size_t ndiags;
    char hover[LSP_MANAGER_HOVER_CAP];
    int has_hover;
    char info[256];

    /* Jump list, for going back after a go-to-definition. A jump can land in
     * another file, so a position alone is not enough — each entry carries the
     * path it belongs to and is reopened on the way back.
     *
     * `pos` is the index of the *next* slot, so back/forward is a walk through
     * `jumps`. A new jump made after going back truncates the forward tail, the
     * way browser history does. */
    struct {
        char path[1024];
        size_t offset;
    } jumps[64];
    int njumps;
    int jump_pos;
} WaveSession;

/* The LSP completion pool, kept file-static exactly as main.c keeps it on its
 * `g` global: LspCompletionItem is large and there is one session per process. */
static LspCompletionItem g_comp_lsp[LSP_MAX_COMPLETIONS];
static size_t g_comp_lsp_count;
static unsigned int g_comp_lsp_generation;
static char g_comp_trigger;
static int g_comp_member_context;

/* Defined with their subsystems below; used from the input path. */
typedef struct WaveSession WaveSession;
static void comp_after_insert(WaveSession *s, Editor *e,
                              unsigned int trigger_character);
static int comp_drain_lsp(WaveSession *s, Editor *e);
int wave_signature_request(WaveSession *s, unsigned int trigger, int retrigger);
static void bufsearch_open(WaveSession *s);
static void bufsearch_repeat(WaveSession *s, int reverse);
static void bufsearch_word(WaveSession *s);
static int line_bounds(const Editor *e, size_t line, size_t *a, size_t *b);

typedef struct {
    size_t start_col;
    size_t end_col;
    const char *name; /* static capture-name table; not owned */
} WaveSpan;

typedef struct {
    const char *rel;
    const char *name;
    int depth;
    int is_dir;
    int collapsed;
} WaveEntry;

/* ---- session ---- */

WaveSession *wave_new(void) {
    WaveSession *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    modal_init(&s->modal);
    overlay_init(&s->overlay);
    complete_init(&s->comp);
    popover_init(&s->pop);
    wave_config_defaults(&s->config);
    wave_config_load(&s->config);
    /* After the load, so a config with vim=0 comes up already non-modal rather
     * than eating the first keystroke as a NORMAL-mode command. */
    standard_set_enabled(NULL, &s->modal, !s->config.vim);
    watch_service_init(&s->watch);
    recent_projects_init(&s->recent);
    recent_projects_load(&s->recent);
    lsp_manager_init(&s->lsp, 0);
    return s;
}

void wave_free(WaveSession *s) {
    if (!s) return;
    lsp_manager_shutdown(&s->lsp);
    watch_service_shutdown(&s->watch);
    for (int i = tabs_count(&s->tabs) - 1; i >= 0; i--) tabs_close(&s->tabs, i);
    overlay_free(&s->overlay);
    if (s->ws) ws_free(s->ws);
    yank_free(&s->yank);
    free(s);
}

static Editor *cur(WaveSession *s) {
    return s ? tabs_current(&s->tabs) : NULL;
}

/* The mode the editor rests in once something else finishes — a command line
 * closing, a project closing, a paste landing. Under vim that is NORMAL; under
 * standard editing there is no such state, and dropping into NORMAL would make
 * the next keystroke a vim command instead of text. Every "back to normal" in
 * this file goes through here for that reason. */
/* Defined with the rest of the jump list below; declared here because both
 * go-to-definition paths (vim's `gd` and ⌘-click) record a jump before moving. */
void wave_jump_push(WaveSession *s);

static void enter_rest_mode(WaveSession *s) {
    if (!s) return;
    if (s->config.vim) modal_enter_normal(&s->modal);
    else modal_enter_insert(&s->modal);
}

/* Merged tree-sitter + server diagnostics for the active editor. Cached so the
 * per-line lookups the UI does while painting stay cheap. */
static void refresh_diags(WaveSession *s) {
    Editor *e = cur(s);
    s->ndiags = e ? lsp_manager_editor_diagnostics(&s->lsp, e, s->diags,
                                                   WAVE_MAX_DIAGS)
                  : 0;
    if (s->ndiags > WAVE_MAX_DIAGS) s->ndiags = WAVE_MAX_DIAGS;
}

/* Every path that opens a file goes through here, so the server always learns
 * about the buffer (main.c does the same via its open_path_mode helper). */
static int open_in_tab(WaveSession *s, const char *path, int preview) {
    TabOpenResult r = tabs_open_file(&s->tabs, path, preview, &s->watch);
    if (!r.ok) return 0;
    Editor *e = tabs_current(&s->tabs);
    if (e) lsp_manager_open_editor(&s->lsp, e);
    refresh_diags(s);
    return 1;
}

/* Open a file or a folder, exactly as main.c does: a folder becomes the
 * workspace; a file opens in a tab and its parent directory becomes the
 * workspace, so the sidebar is populated either way. */
int wave_open_path(WaveSession *s, const char *path) {
    if (!s || !path) return -1;
    WsOpenContext ctx = ws_open_context(path);
    if (ctx.kind == WS_OPEN_NONE) return -1;

    if (ctx.workspace) {
        if (s->ws) ws_free(s->ws);
        s->ws = ctx.workspace;
        lsp_manager_set_root_path(&s->lsp, ws_root(s->ws));
    }
    if (ctx.kind == WS_OPEN_FILE) return open_in_tab(s, ctx.file, 0) ? 0 : -1;
    return 0;
}

/* ---- command line ----
 *
 * `wave [--line N] [--column N] [file-or-folder]` is parsed by the same
 * function main.c calls, so the argument contract does not fork between the two
 * front-ends — the `wave` shim inside the .app just execs whichever binary is
 * bundled. `path` points into `argv`, which the caller owns.
 *
 * Returns 0 for an unusable command line (the caller should print usage and
 * exit non-zero), 1 when a path/no-path is all that was asked for, 2 when a
 * line/column was given too. */
int wave_cli_open_request(int argc, char **argv, const char **path, int *line,
                          int *column) {
    WaveRuntimeOpenRequest r = wave_runtime_open_request(argc, argv);
    if (path) *path = r.path;
    if (line) *line = r.line;
    if (column) *column = r.column;
    if (!r.valid) return 0;
    return r.has_location ? 2 : 1;
}

/* ---- workspace / sidebar ---- */

int wave_has_workspace(const WaveSession *s) { return s && s->ws ? 1 : 0; }

const char *wave_ws_root(const WaveSession *s) {
    return (s && s->ws) ? ws_root(s->ws) : "";
}

size_t wave_ws_count(const WaveSession *s) {
    return (s && s->ws) ? ws_visible_count(s->ws) : 0;
}

int wave_ws_entry(const WaveSession *s, size_t vi, WaveEntry *out) {
    if (!s || !s->ws || !out) return 0;
    const WsEntry *e = ws_visible(s->ws, vi);
    if (!e) return 0;
    out->rel = e->rel;
    out->name = e->name;
    out->depth = e->depth;
    out->is_dir = e->is_dir;
    out->collapsed = e->collapsed;
    return 1;
}

/* Activate a sidebar row. Directories toggle; files open in a tab (preview on
 * a single click, pinned on a double click). Returns 1 if a file was opened. */
int wave_ws_activate(WaveSession *s, int row, int double_click) {
    if (!s || !s->ws) return 0;
    WsClickAction click = ws_click_visible(s->ws, row, double_click);
    if (click.kind != WS_CLICK_OPEN_FILE || !click.entry) return 0;

    char *full = ws_fullpath(s->ws, click.entry->rel);
    if (!full) return 0;
    int ok = open_in_tab(s, full, click.preview);
    free(full);
    return ok;
}

/* ---- Cmd-P file palette (overlay.c + palette.c) ----
 *
 * The palette filters over the workspace's *full* entry list, so its rows are
 * indexed through ws_entry(), not the collapsed sidebar view. */

int wave_palette_open(WaveSession *s) {
    if (!s || !s->ws) return 0;
    return overlay_open_palette(&s->overlay, s->ws);
}

void wave_palette_close(WaveSession *s) {
    if (s) overlay_close(&s->overlay);
}

int wave_palette_active(const WaveSession *s) {
    return s && overlay_active(&s->overlay) == OVERLAY_PALETTE;
}

const char *wave_palette_query(const WaveSession *s) {
    if (!s) return "";
    const char *q = overlay_query((OverlayState *)&s->overlay);
    return q ? q : "";
}

size_t wave_palette_count(const WaveSession *s) {
    if (!s) return 0;
    int n = s->overlay.palette.filtered_n;
    return n > 0 ? (size_t)n : 0;
}

int wave_palette_selected(const WaveSession *s) {
    return s ? s->overlay.palette.sel : 0;
}

int wave_palette_entry(const WaveSession *s, size_t i, WaveEntry *out) {
    if (!s || !s->ws || !out) return 0;
    if (i >= wave_palette_count(s)) return 0;
    const WsEntry *e = ws_entry(s->ws, (size_t)s->overlay.palette.filtered[i]);
    if (!e) return 0;
    out->rel = e->rel;
    out->name = e->name;
    out->depth = e->depth;
    out->is_dir = e->is_dir;
    out->collapsed = e->collapsed;
    return 1;
}

void wave_palette_input(WaveSession *s, const char *text) {
    if (!s || !text) return;
    overlay_insert_text(&s->overlay, s->ws, s->ws ? ws_root(s->ws) : "", text);
}

void wave_palette_backspace(WaveSession *s) {
    if (s) overlay_backspace(&s->overlay, s->ws, s->ws ? ws_root(s->ws) : "");
}

void wave_palette_move(WaveSession *s, int delta) {
    if (s) overlay_move(&s->overlay, delta);
}

/* Open the highlighted result. Returns 1 if a file was opened. */
int wave_palette_accept(WaveSession *s) {
    if (!s) return 0;
    OverlayAcceptTarget t = overlay_accept_target(&s->overlay, s->ws);
    int ok = 0;
    if (t.has_target && t.path) {
        ok = open_in_tab(s, t.path, 0);
        if (ok && t.has_location) {
            Editor *e = tabs_current(&s->tabs);
            if (e) editor_move_to_line_col(e, t.line, t.col);
        }
    }
    overlay_accept_target_free(&t);
    overlay_close(&s->overlay);
    return ok;
}

/* ---- tabs ---- */

int wave_tab_count(const WaveSession *s) {
    return s ? tabs_count(&s->tabs) : 0;
}

int wave_tab_active(const WaveSession *s) {
    return s ? tabs_active_index(&s->tabs) : 0;
}

size_t wave_tab_label(const WaveSession *s, int i, char *out, size_t cap) {
    if (!s || !out || cap == 0) return 0;
    tabs_label(&s->tabs, i, out, cap);
    return strlen(out);
}

int wave_tab_modified(const WaveSession *s, int i) {
    if (!s) return 0;
    const Editor *e = tabs_at_const(&s->tabs, i);
    return e ? e->modified : 0;
}

void wave_tab_set_active(WaveSession *s, int i) {
    if (s) tabs_set_active(&s->tabs, i);
}

void wave_tab_close(WaveSession *s, int i) {
    if (s) tabs_close(&s->tabs, i);
}

void wave_tab_goto(WaveSession *s, int delta) {
    if (s) tabs_goto(&s->tabs, delta);
}

/* ---- current editor: state out ---- */

int wave_has_buffer(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    return (e && editor_has_buffer(e)) ? 1 : 0;
}

const char *wave_path(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    return (e && editor_has_path(e)) ? editor_path(e) : "";
}

size_t wave_line_count(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    if (!e || !e->buf) return 0;
    return pt_line_count(buffer_pt(e->buf));
}

/* Byte range of `line`, newline excluded. */
static int line_bounds(const Editor *e, size_t line, size_t *a, size_t *b) {
    const PieceTable *pt = buffer_pt(e->buf);
    size_t nlines = pt_line_count(pt);
    if (line >= nlines) return 0;
    size_t start = pt_line_start(pt, line);
    size_t end = (line + 1 < nlines) ? pt_line_start(pt, line + 1) : pt_length(pt);
    while (end > start) {
        unsigned char c = byte_at(pt, end - 1);
        if (c != '\n' && c != '\r') break;
        end--;
    }
    *a = start;
    *b = end;
    return 1;
}

size_t wave_line_text(const WaveSession *s, size_t line, char *out, size_t cap) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    if (!e || !e->buf || !out || cap == 0) return 0;
    size_t start, end;
    if (!line_bounds(e, line, &start, &end)) return 0;
    size_t n = end - start;
    if (n > cap) n = cap;
    return pt_read(buffer_pt(e->buf), start, n, out);
}

/* tree-sitter spans for `line`, as byte columns relative to the line start. */
size_t wave_line_spans(WaveSession *s, size_t line, WaveSpan *out, size_t max) {
    Editor *e = cur(s);
    if (!e || !e->buf || !out || max == 0) return 0;
    size_t start, end;
    if (!line_bounds(e, line, &start, &end)) return 0;

    HighlightSpan tmp[256];
    size_t n = editor_highlight_spans(e, start, end, tmp,
                                      sizeof tmp / sizeof tmp[0]);
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        size_t a = tmp[i].start_byte < start ? start : tmp[i].start_byte;
        size_t b = tmp[i].end_byte > end ? end : tmp[i].end_byte;
        if (a >= b) continue;
        out[k].start_col = a - start;
        out[k].end_col = b - start;
        out[k].name = tmp[i].name;
        k++;
    }
    return k;
}

/* Diagnostics touching `line`, as byte columns. These are the merged set —
 * tree-sitter ERROR/MISSING nodes plus whatever the language server published —
 * courtesy of diagnostics_for_editor() behind lsp_manager_editor_diagnostics. */
size_t wave_line_diagnostics(WaveSession *s, size_t line, WaveSpan *out,
                             size_t max) {
    Editor *e = cur(s);
    if (!e || !e->buf || !out || max == 0) return 0;
    size_t start, end;
    if (!line_bounds(e, line, &start, &end)) return 0;
    size_t width = end - start;

    size_t k = 0;
    for (size_t i = 0; i < s->ndiags && k < max; i++) {
        const Diagnostic *d = &s->diags[i];
        if (line < d->start_row || line > d->end_row) continue;
        size_t a = (line == d->start_row) ? d->start_col : 0;
        size_t b = (line == d->end_row) ? d->end_col : width;
        if (a > width) a = width;
        if (b > width) b = width;
        if (b <= a) b = a + 1; /* zero-width diagnostics still need a mark */
        out[k].start_col = a;
        out[k].end_col = b;
        out[k].name = d->message;
        k++;
    }
    return k;
}

/* ---- language server ---- */

/* The full text of the diagnostic under the cursor.
 *
 * Diagnostic.message is a non-owning `const char *`, so diagnostic_from_lsp()
 * can only store the literal "diagnostic" — a server's message lives in an
 * LspDiag buffer that would dangle. main.c has the same constraint and solves
 * it the same way: underline from the merged set, but pull real text for the
 * one under the cursor straight from the LspDiag array. */
size_t wave_cursor_diagnostic(WaveSession *s, char *out, size_t cap) {
    Editor *e = cur(s);
    if (!e || !e->buf || !out || cap == 0) return 0;
    out[0] = '\0';

    LspDiag lsp[128];
    int published = 0;
    size_t n = lsp_manager_diagnostics(&s->lsp, e, lsp,
                                       sizeof lsp / sizeof lsp[0], &published);
    DiagnosticCursorInfo info = {0};
    if (!diagnostics_cursor_info(e, lsp, n, out, cap, &info)) return 0;
    return strlen(out);
}

int wave_lsp_active(const WaveSession *s) {
    return s ? lsp_manager_active(&s->lsp) : 0;
}

const char *wave_hover(const WaveSession *s) {
    return (s && s->has_hover) ? s->hover : "";
}

void wave_hover_clear(WaveSession *s) {
    if (s) s->has_hover = 0;
}

/* Drain server replies. Returns 1 if anything changed and the UI should
 * repaint. Called from a GPUI timer, since replies arrive asynchronously. */
int wave_lsp_poll(WaveSession *s) {
    if (!s) return 0;
    Editor *e = cur(s);
    if (!e) return 0;

    LspManagerUpdate update = lsp_manager_update_ui(&s->lsp, e);
    LspManagerUiPlan plan = lsp_manager_ui_plan(update);
    int changed = 0;

    if (plan.open_definition && plan.definition.path[0]) {
        if (open_in_tab(s, plan.definition.path, 0)) {
            Editor *ne = tabs_current(&s->tabs);
            if (ne) editor_move_to_line_col(ne, plan.definition.line,
                                            plan.definition.col);
        }
        changed = 1;
    }
    if (plan.show_hover && plan.hover[0]) {
        snprintf(s->hover, sizeof s->hover, "%s", plan.hover);
        s->has_hover = 1;
        popover_show_hover(&s->pop, plan.hover);
        changed = 1;
    }
    if (plan.show_signature && plan.signature[0]) {
        popover_show_signature(&s->pop, plan.signature);
        changed = 1;
    }
    if (comp_drain_lsp(s, e)) changed = 1;

    size_t before = s->ndiags;
    refresh_diags(s);
    if (s->ndiags != before) changed = 1;
    return changed;
}

/* Wave's own palette, packed 0xRRGGBB, so the GPUI front-end doesn't fork it. */
unsigned wave_theme_rgb(const char *name) {
    Color c = theme_color(name);
    unsigned r = (unsigned)(c.r * 255.0f + 0.5f);
    unsigned g = (unsigned)(c.g * 255.0f + 0.5f);
    unsigned b = (unsigned)(c.b * 255.0f + 0.5f);
    return (r << 16) | (g << 8) | b;
}

void wave_cursor(const WaveSession *s, size_t *row, size_t *col) {
    size_t r = 0, c = 0;
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    if (e && e->buf) pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &r, &c);
    if (row) *row = r;
    if (col) *col = c;
}

int wave_mode(const WaveSession *s) {
    return s ? (int)s->modal.mode : (int)MODE_NORMAL;
}

/* The status-bar mode chip. Standard editing has no modes to report — NORMAL /
 * INSERT / VISUAL are vim vocabulary — so it reports an empty string and the
 * front-end drops the chip entirely. */
const char *wave_mode_name(const WaveSession *s) {
    if (s && !s->config.vim) return "";
    return mode_name(s ? s->modal.mode : MODE_NORMAL);
}

int wave_modified(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    return e ? e->modified : 0;
}

/* ---- mouse selection ----
 *
 * editor_apply_click_position/editor_apply_drag_selection hit-test in pixels
 * using e->scroll_y and the wrap index (e->vstart), neither of which this
 * front-end drives — it scrolls in whole lines and lays out with flexbox. So
 * the row/column are resolved on the Rust side and applied here, which is the
 * same end state: cursor moved, anchor held. */

/* `line`/`col` are 0-based, like every other position in this ABI.
 * editor_move_to_line_col() is 1-based (it backs `:123` and the file finder),
 * so convert rather than leaking that off-by-one to the front-end. */
void wave_click_at(WaveSession *s, int line, int col) {
    Editor *e = cur(s);
    if (!e || !e->buf) return;
    editor_move_to_line_col(e, line + 1, col + 1);
    e->anchor = e->cursor; /* a bare click collapses any selection */
    standard_clear_carets(e);
    enter_rest_mode(s);
}

/* A drag enters VISUAL mode, exactly as main.c does after
 * editor_apply_drag_selection. Keeping the selection outside the modal state
 * instead would leave NORMAL on the status bar while a selection is painted,
 * and normal-mode motions would then extend it, since the range is just
 * cursor/anchor.
 *
 * main.c gates on a movement threshold (layout_drag_should_start) so a jittery
 * click does not become a drag; this front-end has no such threshold, so the
 * mode only flips once the drag has actually covered ground. */
void wave_drag_to(WaveSession *s, int line, int col) {
    Editor *e = cur(s);
    if (!e || !e->buf) return;
    size_t anchor = e->anchor; /* move_to_line_col resets it; put it back */
    editor_move_to_line_col(e, line + 1, col + 1);
    e->anchor = anchor;
    e->group_open = 0;
    if (e->cursor != e->anchor) modal_enter_visual(&s->modal);
}

/* VISUAL mode always covers at least the character under the cursor, which is
 * what editor_visual_range returns and what main.c copies on Cmd-C. */
int wave_has_selection(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    if (!e || !e->buf) return 0;
    return s->modal.mode == MODE_VISUAL;
}

/* The selected text, for Cmd-C. Caller must free via wave_string_free(). */
char *wave_selection_text(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !e->buf) return NULL;
    if (!wave_has_selection(s)) return NULL;
    return editor_copy_text(e, 1);
}

void wave_string_free(char *p) { free(p); }

/* Selection on `line`, as byte columns. editor_visual_range only looks at
 * cursor/anchor, so the mode gate here is what keeps a stale anchor from
 * painting a selection outside VISUAL — text_view_selection does the same. */
int wave_line_selection(WaveSession *s, size_t line, size_t *a, size_t *b) {
    Editor *e = cur(s);
    if (!e || !e->buf) return 0;
    if (s->modal.mode != MODE_VISUAL) return 0;
    EditorRange sel;
    if (!editor_visual_range(e, &sel)) return 0;
    size_t start, end;
    if (!line_bounds(e, line, &start, &end)) return 0;
    if (sel.end <= start || sel.start > end) return 0;
    size_t lo = sel.start < start ? start : sel.start;
    size_t hi = sel.end > end ? end : sel.end;
    *a = lo - start;
    *b = hi - start;
    return 1;
}

/* ---- current editor: commands in ---- */

unsigned wave_text_input(WaveSession *s, unsigned int cp) {
    if (!s) return 0;
    Editor *e = cur(s);

    /* With no buffer, Wave's planner drops text entirely
     * (input_text_target -> INPUT_TEXT_NONE), which also makes `:` unreachable.
     * We deliberately diverge: the tab-spawning commands (`:term`, `:git`,
     * `:claude`) are most useful with nothing open, and would otherwise depend
     * on a keyboard shortcut being the only way in. */
    if (!e || !e->buf) {
        if (cp == ':') command_open(&s->cmd);
        return 0;
    }

    /* Standard editing never leaves "insert": every printable key is text, and
     * a selection is replaced by it. In particular `:` types a colon rather
     * than opening the command line — everything the command line offers is on
     * the ⇧⌘P palette, which is how a non-vim user reaches it. (The no-buffer
     * branch above still takes `:`, since with nothing open it cannot be text.) */
    if (!s->config.vim) {
        /* The multi- path is the single-caret one when there are no extras. */
        standard_multi_text_input(e, &s->modal, cp);
        editor_update_highlighter(e);
        lsp_manager_push_change(&s->lsp, e);
        comp_after_insert(s, e, cp);
        if (cp == '(' || cp == ',')
            wave_signature_request(s, cp, cp == ',');
        else if (cp == ')')
            popover_close(&s->pop);
        refresh_diags(s);
        return 0;
    }

    if (s->modal.mode == MODE_INSERT) {
        editor_apply_text_input(e, cp);
        editor_update_highlighter(e);
        lsp_manager_push_change(&s->lsp, e);
        comp_after_insert(s, e, cp);
        /* `(` opens signature help, `,` moves to the next parameter, `)` ends
         * it — the same triggers main.c uses. */
        if (cp == '(' || cp == ',')
            wave_signature_request(s, cp, cp == ',');
        else if (cp == ')')
            popover_close(&s->pop);
        refresh_diags(s);
        return 0;
    }

    EditCommandResult res = edit_command_apply(e, &s->modal, &s->yank, cp);
    editor_update_highlighter(e);

    /* gd / gh. Both are async: the reply lands in wave_lsp_poll(). Without a
     * server, request_definition_at_cursor falls back to the tree-sitter
     * heuristic and moves the cursor synchronously. */
    if (res.flags & EDIT_COMMAND_GOTO_DEFINITION) {
        /* Recorded here too, so vim's `gd` is as recoverable as ⌘-click. */
        wave_jump_push(s);
        char message[256];
        if (!lsp_manager_request_definition_at_cursor(&s->lsp, e, message,
                                                      sizeof message))
            editor_goto_local_definition(e, message, sizeof message);
    }
    if (res.flags & EDIT_COMMAND_SHOW_INFO) {
        /* gh: open the popover on the local info immediately, then let the
         * server's hover compose into it when the reply lands. */
        char base[LSP_MANAGER_HOVER_CAP];
        LspManagerHoverInfo info =
            lsp_manager_hover_info(&s->lsp, e, base, sizeof base);
        popover_show_base(&s->pop, base, info.loading);
        if (info.ok)
            lsp_manager_request_hover(&s->lsp, e, (int)info.row, (int)info.col);
    }
    if (res.flags & EDIT_COMMAND_OPEN_COMMAND_LINE) command_open(&s->cmd);
    if (res.flags & EDIT_COMMAND_OPEN_BUFFER_SEARCH) bufsearch_open(s);
    if (res.flags & EDIT_COMMAND_SEARCH_NEXT) bufsearch_repeat(s, 0);
    if (res.flags & EDIT_COMMAND_SEARCH_PREV) bufsearch_repeat(s, 1);
    if (res.flags & EDIT_COMMAND_SEARCH_WORD) bufsearch_word(s);
    if (res.flags & EDIT_COMMAND_UNDO_AT_OLDEST)
        snprintf(s->info, sizeof s->info, "already at oldest change");

    lsp_manager_push_change(&s->lsp, e);
    refresh_diags(s);
    return res.flags;
}

int wave_special_key(WaveSession *s, int key) {
    Editor *e = cur(s);
    if (!e || !e->buf) return 0;
    int handled;
    if (!s->config.vim)
        handled = standard_multi_editor_key(e, &s->modal, (EditorKey)key);
    else if (s->modal.mode == MODE_INSERT)
        handled = editor_apply_insert_key(e, (EditorKey)key);
    else
        handled = editor_apply_motion_key(e, (EditorKey)key);
    editor_update_highlighter(e);
    lsp_manager_push_change(&s->lsp, e);
    refresh_diags(s);
    return handled;
}

/* Arrow keys and their ⌥/⌘/Home/End relatives, in standard editing. Kept apart
 * from wave_special_key because a motion carries `extend` (the shift key) and
 * must not be mistaken for text. Returns non-zero if anything moved. */
int wave_motion(WaveSession *s, int motion, int extend) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    /* A motion collapses to one caret, which is what every editor with
     * multiple carets does — the alternative is N carets drifting apart. */
    standard_clear_carets(e);
    int moved = standard_motion(e, &s->modal, (StdMotion)motion, extend);
    if (moved) popover_close(&s->pop);
    return moved;
}

int wave_select_all(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    return standard_select_all(e, &s->modal);
}

int wave_select_word(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    return standard_select_word(e, &s->modal);
}

/* After any command that rewrites the buffer: re-highlight, tell the server,
 * refresh diagnostics. Every line command needs the same three, so they say so
 * once here rather than each remembering. */
static void after_edit(WaveSession *s, Editor *e) {
    editor_update_highlighter(e);
    lsp_manager_push_change(&s->lsp, e);
    refresh_diags(s);
}

/* ⌘C / ⌘X. Both return malloc'd text for the caller to put on the clipboard
 * (freed with wave_string_free), or NULL when there is nothing to take. With no
 * selection they act on the whole line, as a standard editor does. */
char *wave_standard_copy(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return NULL;
    return standard_copy(e, &s->modal);
}

char *wave_standard_cut(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return NULL;
    char *text = standard_cut(e, &s->modal);
    if (text) after_edit(s, e);
    return text;
}

int wave_delete_line(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_delete_line(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_duplicate_line(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_duplicate_line(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_move_line(WaveSession *s, int dir) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_move_line(e, &s->modal, dir);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_delete_to_line_start(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_delete_to_line_start(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_toggle_comment(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_toggle_comment(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

/* Tab / ⇧Tab. Returns 0 when it was not a block operation, so the front-end
 * knows to fall back to inserting a plain tab. */
int wave_indent(WaveSession *s, int outdent) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_indent(e, &s->modal, outdent);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_delete_word_left(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_delete_word_left(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_delete_word_right(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_delete_word_right(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_delete_to_line_end(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_delete_to_line_end(e, &s->modal);
    if (ok) after_edit(s, e);
    return ok;
}

int wave_select_line(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    return standard_select_line(e, &s->modal);
}

int wave_insert_line(WaveSession *s, int below) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    int ok = standard_insert_line(e, &s->modal, below);
    if (ok) after_edit(s, e);
    return ok;
}

/* ---- multiple carets ---- */

int wave_select_next_occurrence(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    return standard_select_next_occurrence(e, &s->modal);
}

int wave_add_caret_at(WaveSession *s, int line, int col) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return 0;
    /* Resolve the click through the editor, then put the caret back: this is
     * the same trick wave_drag_to uses, since move_to_line_col owns the
     * row/column arithmetic and resets the anchor as a side effect. */
    size_t keep_cursor = e->cursor, keep_anchor = e->anchor;
    editor_move_to_line_col(e, line + 1, col + 1);
    size_t at = e->cursor;
    e->cursor = keep_cursor;
    e->anchor = keep_anchor;
    return standard_add_caret(e, at, at);
}

size_t wave_caret_count(const WaveSession *s) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    return e ? standard_caret_count(e) : 0;
}

void wave_clear_carets(WaveSession *s) {
    Editor *e = cur(s);
    if (e) standard_clear_carets(e);
}

/* Extra caret i, as byte offsets. For painting the additional carets and their
 * selections; the primary one is already on e->cursor/e->anchor. */
int wave_caret_at(const WaveSession *s, size_t i, size_t *anchor,
                  size_t *cursor) {
    const Editor *e = s ? tabs_current_const(&s->tabs) : NULL;
    return e ? standard_caret_at(e, i, anchor, cursor) : 0;
}

/* ---- jump list ----
 *
 * Record where the caret is before a jump, so ⌃- (and vim's `gd`) can come
 * back. Anything that moves the caret a long way should call this first. */
void wave_jump_push(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf || !e->path) return;
    /* A jump made after going back drops whatever was ahead, like a browser. */
    if (s->jump_pos < s->njumps) s->njumps = s->jump_pos;
    if (s->njumps == (int)(sizeof s->jumps / sizeof s->jumps[0])) {
        /* Full: drop the oldest and slide down, so the most recent history is
         * what survives. */
        memmove(&s->jumps[0], &s->jumps[1], sizeof s->jumps - sizeof s->jumps[0]);
        s->njumps--;
    }
    snprintf(s->jumps[s->njumps].path, sizeof s->jumps[s->njumps].path, "%s",
             e->path);
    s->jumps[s->njumps].offset = e->cursor;
    s->njumps++;
    s->jump_pos = s->njumps;
}

/* Walk the jump list. `dir` < 0 goes back, > 0 forward. Returns 1 if it moved.
 *
 * Going back from the newest entry has to record where we are first, otherwise
 * there would be nothing to go forward *to*. */
int wave_jump_go(WaveSession *s, int dir) {
    Editor *e = cur(s);
    if (!s || dir == 0) return 0;
    if (dir < 0) {
        if (s->jump_pos <= 0) return 0;
        if (s->jump_pos == s->njumps && e && e->buf && e->path) {
            if (s->njumps < (int)(sizeof s->jumps / sizeof s->jumps[0])) {
                snprintf(s->jumps[s->njumps].path, sizeof s->jumps[s->njumps].path,
                         "%s", e->path);
                s->jumps[s->njumps].offset = e->cursor;
                s->njumps++;
            }
        }
        s->jump_pos--;
    } else {
        if (s->jump_pos + 1 >= s->njumps) return 0;
        s->jump_pos++;
    }
    const char *path = s->jumps[s->jump_pos].path;
    size_t off = s->jumps[s->jump_pos].offset;
    if (!path[0]) return 0;

    /* Reopen if the jump belongs to another file; tabs_open_file is what the
     * file finder uses, so an already-open file is switched to, not duplicated. */
    Editor *target = cur(s);
    if (!target || !target->path || strcmp(target->path, path) != 0) {
        if (wave_open_path(s, path) != 0) return 0;
        target = cur(s);
    }
    if (!target || !target->buf) return 0;
    size_t len = pt_length(buffer_pt(target->buf));
    target->cursor = off < len ? off : len;
    target->anchor = target->cursor;
    standard_clear_carets(target);
    enter_rest_mode(s);
    return 1;
}

/* Go to the definition under the caret — the `gd` path, reachable without vim
 * so ⌘-click and ⌃-click can use it. Async when a server is up: the reply lands
 * in wave_lsp_poll(). Without one, the tree-sitter fallback moves the caret
 * synchronously, exactly as wave_text_input's `gd` branch does.
 *
 * The jump is recorded first either way, so ⌃- comes back whether the answer
 * arrived from the server or the local heuristic. */
void wave_goto_definition(WaveSession *s) {
    Editor *e = cur(s);
    if (!s || !e || !e->buf) return;
    wave_jump_push(s);
    char message[256];
    if (!lsp_manager_request_definition_at_cursor(&s->lsp, e, message,
                                                  sizeof message))
        editor_goto_local_definition(e, message, sizeof message);
}

/* Caret blink phase, shared with the GLFW front-end rather than reimplemented:
 * both draw the same 1 Hz blink that holds solid for half a second after the
 * last keystroke, so the caret never strobes while you type. `blink` is the
 * runtime's opt-out (snapshot mode renders a solid caret). */
int wave_cursor_visible(int blink, double now, double last_activity) {
    return view_cursor_visible(blink, now, last_activity);
}

int wave_vim_enabled(const WaveSession *s) { return s ? s->config.vim : 1; }

/* Flip modal editing at runtime. Applied immediately — waiting for a restart
 * would leave the editor in whichever mode it was already in, which for vim=0
 * means a NORMAL mode the user just asked not to have.
 *
 * Persisted immediately too, like wave_theme_set() and unlike the rest of the
 * settings screen (which waits for `s`). This one decides what every keystroke
 * does: someone who turns vim off, quits, and finds it back on next launch will
 * reasonably conclude the setting does not work. */
int wave_set_vim_enabled(WaveSession *s, int on) {
    if (!s) return 0;
    s->config.vim = on != 0;
    standard_set_enabled(cur(s), &s->modal, !s->config.vim);
    wave_config_save(&s->config);
    snprintf(s->info, sizeof s->info, "vim mode %s", s->config.vim ? "on" : "off");
    return s->config.vim;
}

int wave_toggle_vim(WaveSession *s) {
    return s ? wave_set_vim_enabled(s, !s->config.vim) : 0;
}

void wave_escape(WaveSession *s) {
    Editor *e = cur(s);
    if (!s) return;
    /* Standard editing has nowhere to escape *to*: Escape drops the selection
     * and leaves the caret where it is, rather than entering NORMAL. */
    if (!s->config.vim) {
        if (e) {
            /* Escape drops the extra carets first — that is what a user reaches
             * for to get out of a multi-caret edit. */
            standard_clear_carets(e);
            standard_escape(e, &s->modal);
        }
        return;
    }
    modal_enter_normal(&s->modal);
    if (e) editor_cancel_group(e);
}

int wave_undo(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !e->buf) return 0;
    int ok = editor_undo(e);
    editor_update_highlighter(e);
    return ok;
}

int wave_redo(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !e->buf) return 0;
    int ok = editor_redo(e);
    editor_update_highlighter(e);
    return ok;
}

int wave_save(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !e->buf) return -1;
    return editor_save_file(e, &s->watch);
}

/* Jump the cursor to a 0-based line/column (used by the file finder). */
void wave_goto_line(WaveSession *s, int line, int column) {
    Editor *e = cur(s);
    if (e && e->buf) editor_move_to_line_col(e, line, column);
}

/* ==========================================================================
 * Terminal tabs (terminal.c over libghostty-vt)
 * ========================================================================== */

/* Byte offsets, not columns. TerminalCellStyle carries both; the front-end
 * indexes UTF-8 text, and a single multi-byte glyph (box drawing, `›`, …) is
 * enough to make the two disagree — which misplaces colours and the cursor. */
typedef struct {
    size_t start_byte;
    size_t end_byte;
    unsigned fg; /* 0xRRGGBB, or WAVE_COLOR_DEFAULT */
    unsigned bg;
} WaveCellStyle;

#define WAVE_COLOR_DEFAULT 0xFFFFFFFFu

int wave_tab_kind(const WaveSession *s, int i) {
    return s ? (int)tabs_kind_at(&s->tabs, i) : 0;
}

/* Spawn a login shell in a new tab, rooted at the workspace. */
int wave_term_open(WaveSession *s, const char *label, const char *cmd) {
    if (!s) return 0;
    const char *root = s->ws ? ws_root(s->ws) : ".";
    const char *shell = getenv("SHELL");
    if (!shell || !*shell) shell = "/bin/zsh";

    /* main.c spawns the bare shell; `-l` makes it a login shell and changes
     * which startup files run, so keep it identical to the known-good path. */
    const char *argv_shell[] = {shell, NULL};
    const char *argv_cmd[] = {shell, "-lc", cmd, NULL};
    Terminal *t = tabs_new_terminal(&s->tabs, label ? label : "terminal", root,
                                    cmd && *cmd ? argv_cmd : argv_shell);
    return t != NULL;
}

static Terminal *cur_term(WaveSession *s) {
    return s ? tabs_current_terminal(&s->tabs) : NULL;
}

int wave_term_active(const WaveSession *s) {
    return s && tabs_current_kind(&s->tabs) == TAB_ITEM_TERMINAL;
}

/* Drain any pty output across every terminal tab, as main.c's poll loop does.
 *
 * Returns 1 only when something actually changed. terminal_poll() is void, so
 * "changed" is a cheap snapshot of the active terminal's line count, pending
 * partial line and cursor — otherwise the UI would repaint at the poll rate
 * forever just because a terminal tab exists. */
int wave_term_poll(WaveSession *s) {
    if (!s) return 0;
    static size_t last_nlines, last_current_len;
    static int last_row, last_col, last_running;

    int any = 0;
    for (int i = 0; i < tabs_count(&s->tabs); i++) {
        Terminal *t = tabs_terminal_at(&s->tabs, i);
        if (!t) continue;
        terminal_poll(t);
        any = 1;
    }
    if (!any) return 0;

    const Terminal *t = tabs_current_terminal_const(&s->tabs);
    if (!t) return 0;
    int changed = t->nlines != last_nlines || t->current_len != last_current_len ||
                  t->cursor_row != last_row || t->cursor_col != last_col ||
                  t->running != last_running;
    last_nlines = t->nlines;
    last_current_len = t->current_len;
    last_row = t->cursor_row;
    last_col = t->cursor_col;
    last_running = t->running;
    return changed;
}

/* Total addressable lines, so the view can stop at the end of the scrollback
 * instead of painting blank rows past it (draw_terminal_panel clamps too). */
size_t wave_term_total_lines(const WaveSession *s) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (!t) return 0;
    return t->nlines + (t->current_len ? 1u : 0u);
}

void wave_term_resize(WaveSession *s, int rows, int cols) {
    Terminal *t = cur_term(s);
    if (t) terminal_resize(t, rows, cols);
}

size_t wave_term_visible_start(const WaveSession *s, int rows) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    return t ? terminal_visible_start(t, rows) : 0;
}

size_t wave_term_line(const WaveSession *s, size_t index, char *out, size_t cap) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (!t || !out || cap == 0) return 0;
    const char *line = terminal_line(t, index);
    if (!line) return 0;
    size_t n = strlen(line);
    if (n > cap) n = cap;
    memcpy(out, line, n);
    return n;
}

static unsigned pack_term_color(TerminalColor c) {
    unsigned r = (unsigned)(c.r * 255.0f + 0.5f);
    unsigned g = (unsigned)(c.g * 255.0f + 0.5f);
    unsigned b = (unsigned)(c.b * 255.0f + 0.5f);
    return (r << 16) | (g << 8) | b;
}

size_t wave_term_line_styles(const WaveSession *s, size_t index,
                             WaveCellStyle *out, size_t max) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (!t || !out || max == 0) return 0;
    const TerminalLineStyle *st = terminal_line_style(t, index);
    if (!st) return 0;
    size_t k = 0;
    for (size_t i = 0; i < st->ncells && k < max; i++) {
        const TerminalCellStyle *c = &st->cells[i];
        out[k].start_byte = c->byte_start;
        out[k].end_byte = c->byte_start + c->byte_len;
        out[k].fg = c->has_fg ? pack_term_color(c->fg) : WAVE_COLOR_DEFAULT;
        out[k].bg = c->has_bg ? pack_term_color(c->bg) : WAVE_COLOR_DEFAULT;
        k++;
    }
    return k;
}

/* Byte offset of display column `col` on the terminal line at `index`, so the
 * cursor (reported in columns) can be placed in byte-indexed text. */
size_t wave_term_col_to_byte(const WaveSession *s, size_t index, size_t col) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (!t) return 0;
    const char *line = terminal_line(t, index);
    if (!line) return 0;

    size_t byte = 0, seen = 0;
    while (line[byte] && seen < col) {
        unsigned char c = (unsigned char)line[byte];
        /* Skip one UTF-8 sequence per display column. */
        byte += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        seen++;
    }
    return byte;
}

/* cursor_row is an *absolute* scrollback row — draw_terminal_panel subtracts
 * terminal_visible_start() from it to get a screen row. Reported as-is; the
 * caller does the same subtraction. */
void wave_term_cursor(const WaveSession *s, int *row, int *col, int *visible) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (row) *row = t ? t->cursor_row : 0;
    if (col) *col = t ? t->cursor_col : 0;
    /* The C renderer only draws the cursor while the child is alive. */
    if (visible) *visible = t ? (t->cursor_visible && t->running) : 0;
}

int wave_term_rows(const WaveSession *s) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    return t ? t->rows : 0;
}

int wave_term_running(const WaveSession *s) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    return t ? t->running : 0;
}

const char *wave_term_status(const WaveSession *s) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    return t ? terminal_status(t) : "";
}

void wave_term_write(WaveSession *s, const char *text) {
    Terminal *t = cur_term(s);
    if (t && text) terminal_write(t, text, strlen(text));
}

/* `key` is a GLFW key code — terminal_key_sequence() switches on those
 * directly (256 Escape, 257 Enter, 258 Tab, 259 Backspace, 261 Delete,
 * 262..265 arrows), and expects ASCII 'A'-'Z' for control chords. Passing
 * EditorKey values here silently produces the wrong escape sequences. */
void wave_term_key(WaveSession *s, int key, int shift, int alt, int control) {
    Terminal *t = cur_term(s);
    if (t) terminal_send_key_mods(t, key, shift, alt, control);
}

void wave_term_scroll(WaveSession *s, int units) {
    Terminal *t = cur_term(s);
    if (t) terminal_scroll(t, units);
}

/* ==========================================================================
 * Git view (git_view.c)
 * ========================================================================== */

int wave_git_open(WaveSession *s) {
    if (!s) return 0;
    const char *root = s->ws ? ws_root(s->ws) : ".";
    return tabs_new_git(&s->tabs, "git", root) != NULL;
}

static GitView *cur_git(WaveSession *s) {
    return s ? tabs_current_git(&s->tabs) : NULL;
}

int wave_git_active(const WaveSession *s) {
    return s && tabs_current_kind(&s->tabs) == TAB_ITEM_GIT;
}

int wave_git_mode(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? (int)g->mode : 0;
}

int wave_git_repo_count(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->repo_count : 0;
}

const char *wave_git_repo_label(const WaveSession *s, int i) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    if (!g || i < 0 || i >= g->repo_count) return "";
    return g->repos[i].label;
}

int wave_git_selected_repo(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->selected_repo : 0;
}

int wave_git_file_count(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->file_count : 0;
}

int wave_git_file(const WaveSession *s, int i, const char **code,
                  const char **path) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    if (!g || i < 0 || i >= g->file_count) return 0;
    if (code) *code = g->files[i].code;
    if (path) *path = g->files[i].path;
    return 1;
}

int wave_git_selected_file(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->selected_file : 0;
}

int wave_git_diff_count(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->diff_count : 0;
}

const char *wave_git_diff_line(const WaveSession *s, int i) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    if (!g || i < 0 || i >= g->diff_count) return "";
    return g->diff[i];
}

const char *wave_git_message(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->message : "";
}

const char *wave_git_info(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->info : "";
}

void wave_git_move(WaveSession *s, int delta) {
    GitView *g = cur_git(s);
    if (g) git_view_move(g, delta);
}

int wave_git_accept(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_accept(g) : 0;
}

int wave_git_stage_toggle(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_stage_toggle(g) : 0;
}

int wave_git_begin_commit(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_begin_commit(g) : 0;
}

int wave_git_commit(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_commit(g) : 0;
}

void wave_git_cancel_input(WaveSession *s) {
    GitView *g = cur_git(s);
    if (g) git_view_cancel_input(g);
}

int wave_git_insert_text(WaveSession *s, const char *text) {
    GitView *g = cur_git(s);
    return (g && text) ? git_view_insert_text(g, text) : 0;
}

int wave_git_backspace(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_backspace(g) : 0;
}

int wave_git_refresh(WaveSession *s) {
    GitView *g = cur_git(s);
    return g ? git_view_refresh(g) : 0;
}

void wave_git_diff_scroll(WaveSession *s, int delta) {
    GitView *g = cur_git(s);
    if (g) git_view_diff_scroll(g, delta);
}

/* ==========================================================================
 * Insert-mode completion (complete.c, sourced from LSP / tree-sitter / words)
 *
 * Mirrors main.c's complete_trigger / complete_source_locally / after_insert.
 * ========================================================================== */

static void comp_clear_lsp_source(void) {
    g_comp_lsp_count = 0;
    g_comp_lsp_generation = 0;
}

/* LSP CompletionItemKind numbering, mapped the same way main.c maps it. */
static CompleteKind comp_kind_from_lsp(int lsp_kind) {
    switch (lsp_kind) {
    case 3:  /* Function */
    case 2:  /* Method */
    case 4:  return COMPLETE_KIND_FUNCTION; /* Constructor */
    case 5:  /* Field */
    case 10: return COMPLETE_KIND_FIELD;    /* Property */
    case 6:  return COMPLETE_KIND_VARIABLE;
    case 7:  /* Class */
    case 8:  /* Interface */
    case 22: return COMPLETE_KIND_TYPE;     /* Struct */
    case 9:  return COMPLETE_KIND_MODULE;
    case 14: return COMPLETE_KIND_KEYWORD;
    default: return COMPLETE_KIND_TEXT;
    }
}

static void comp_source_locally(WaveSession *s, Editor *e, unsigned int gen,
                                const char *prefix) {
    comp_clear_lsp_source();
    static CompleteItem items[COMPLETE_MAX_ITEMS];
    int n = 0;
    if (e->hl) {
        static HlIdent idents[COMPLETE_MAX_ITEMS];
        int ni = (int)hl_identifiers(e->hl, prefix, idents, COMPLETE_MAX_ITEMS);
        for (int i = 0; i < ni; i++) {
            CompleteItem *it = &items[n];
            snprintf(it->label, sizeof it->label, "%s", idents[i].text);
            snprintf(it->insert_text, sizeof it->insert_text, "%s",
                     idents[i].text);
            it->detail[0] = '\0';
            it->sort_text[0] = '\0';
            it->kind = idents[i].kind == HL_IDENT_TYPE      ? COMPLETE_KIND_TYPE
                       : idents[i].kind == HL_IDENT_PROPERTY ? COMPLETE_KIND_FIELD
                                                             : COMPLETE_KIND_VARIABLE;
            it->scope = COMPLETE_SCOPE_UNKNOWN;
            n++;
        }
    } else {
        char *txt = editor_text(e);
        n = complete_collect_buffer_words(txt, prefix, items, COMPLETE_MAX_ITEMS);
        free(txt);
    }
    complete_set_items(&s->comp, gen, items, n);
    if (s->comp.nfiltered == 0) complete_close(&s->comp);
}

static void comp_trigger(WaveSession *s, Editor *e, size_t word_start,
                         const char *prefix, char trigger_character) {
    int member = trigger_character == '.';
    if (!member && e->buf && word_start > 0) {
        char previous = 0;
        pt_read(buffer_pt(e->buf), word_start - 1, 1, &previous);
        member = previous == '.';
    }
    comp_clear_lsp_source();
    unsigned int gen = complete_begin(&s->comp, word_start, prefix);

    Lsp *l = lsp_manager_for(&s->lsp, e);
    if (l && lsp_ready(l)) {
        g_comp_trigger = trigger_character;
        g_comp_member_context = member;
        size_t row = 0, col = 0;
        pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
        if (trigger_character)
            lsp_manager_request_triggered_completion(&s->lsp, e, (int)row,
                                                     (int)col, trigger_character);
        else
            lsp_manager_request_completion(&s->lsp, e, (int)row, (int)col);
        complete_set_loading(&s->comp, gen);
        return;
    }
    comp_source_locally(s, e, gen, prefix);
}

static void comp_after_insert(WaveSession *s, Editor *e,
                              unsigned int trigger_character) {
    if (!e || !e->buf) {
        complete_close(&s->comp);
        return;
    }
    const PieceTable *pt = buffer_pt(e->buf);
    size_t ws = complete_prefix_start(pt, e->cursor);
    size_t plen = e->cursor - ws;

    if (trigger_character == '.') {
        comp_trigger(s, e, e->cursor, "", '.');
        return;
    }
    if (plen == 0) {
        complete_close(&s->comp);
        return;
    }

    char prefix[COMPLETE_LABEL_CAP];
    size_t n = plen < sizeof prefix - 1 ? plen : sizeof prefix - 1;
    pt_read(pt, ws, n, prefix);
    prefix[n] = '\0';

    if (s->comp.active && s->comp.word_start == ws) {
        complete_set_prefix(&s->comp, prefix);
        if (s->comp.nfiltered == 0 && !s->comp.loading)
            comp_trigger(s, e, ws, prefix, 0);
        return;
    }
    comp_trigger(s, e, ws, prefix, 0);
}

/* Drain a completion reply. Called from the LSP poll. */
static int comp_drain_lsp(WaveSession *s, Editor *e) {
    if (!s->comp.active || !e) return 0;
    size_t n = 0;
    if (!lsp_manager_take_completions(&s->lsp, e, g_comp_lsp,
                                      LSP_MAX_COMPLETIONS, &n))
        return 0;

    g_comp_lsp_count = n;
    g_comp_lsp_generation = s->comp.generation;

    static CompleteItem items[COMPLETE_MAX_ITEMS];
    int count = 0;
    for (size_t i = 0; i < n && count < COMPLETE_MAX_ITEMS; i++) {
        const LspCompletionItem *src = &g_comp_lsp[i];
        CompleteItem *it = &items[count];
        snprintf(it->label, sizeof it->label, "%s", src->label);
        snprintf(it->insert_text, sizeof it->insert_text, "%s",
                 src->insert_text[0] ? src->insert_text : src->label);
        snprintf(it->detail, sizeof it->detail, "%s", src->detail);
        snprintf(it->sort_text, sizeof it->sort_text, "%s", src->sort_text);
        it->kind = comp_kind_from_lsp(src->kind);
        it->scope = complete_scope_from_lsp_kind(src->kind, g_comp_member_context);
        count++;
    }
    complete_set_items(&s->comp, s->comp.generation, items, count);
    s->comp.loading = 0;
    if (s->comp.nfiltered == 0) {
        /* Lazy servers answer the first request with nothing; fall back. */
        comp_source_locally(s, e, s->comp.generation, s->comp.prefix);
    }
    return 1;
}

int wave_complete_active(const WaveSession *s) {
    return s ? complete_is_active(&s->comp) : 0;
}

int wave_complete_loading(const WaveSession *s) {
    return s ? s->comp.loading : 0;
}

int wave_complete_count(const WaveSession *s) {
    return s ? s->comp.nfiltered : 0;
}

int wave_complete_selected(const WaveSession *s) {
    return s ? s->comp.sel : 0;
}

int wave_complete_item(const WaveSession *s, int i, const char **label,
                       const char **detail, const char **kind) {
    if (!s || i < 0 || i >= s->comp.nfiltered) return 0;
    const CompleteItem *it = &s->comp.items[s->comp.filtered[i]];
    if (label) *label = it->label;
    if (detail) *detail = it->detail;
    if (kind) *kind = complete_kind_tag(it->kind);
    return 1;
}

void wave_complete_move(WaveSession *s, int delta) {
    if (s) complete_move(&s->comp, delta);
}

void wave_complete_close(WaveSession *s) {
    if (s) complete_close(&s->comp);
}

/* Apply the selection, keeping the server's additional edits (auto-imports).
 *
 * TypeScript marks cross-module suggestions with a `detail` and a resolve id,
 * and only attaches the auto-import edit on resolve. Resolving synchronously
 * here keeps accept a single user action; main.c defers it and re-enters, but
 * that needs an accept-pending state machine this front-end does not have. */
int wave_complete_accept(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !s->comp.active || s->comp.nfiltered == 0) return 0;
    CompleteEdit edit;
    if (!complete_accept(&s->comp, e->cursor, &edit)) {
        complete_close(&s->comp);
        return 0;
    }
    int raw = s->comp.filtered[s->comp.sel];
    LspCompletionItem *item = NULL;
    if (g_comp_lsp_generation == s->comp.generation && raw >= 0 &&
        (size_t)raw < g_comp_lsp_count)
        item = &g_comp_lsp[raw];

    if (item && item->resolve_id > 0 && item->detail[0] && !item->resolved &&
        lsp_manager_resolve_completion(&s->lsp, e, item)) {
        /* Give the server a brief window to answer; fall through with the
         * unresolved item if it does not. */
        for (int i = 0; i < 40; i++) {
            LspCompletionItem resolved;
            if (lsp_manager_take_resolved_completion(&s->lsp, e, &resolved)) {
                *item = resolved;
                break;
            }
            usleep(5000);
        }
    }

    lsp_manager_apply_completion(e, edit.start, edit.end, edit.text, item);
    complete_close(&s->comp);
    comp_clear_lsp_source();
    editor_update_highlighter(e);
    lsp_manager_push_change(&s->lsp, e);
    refresh_diags(s);
    return 1;
}

/* ==========================================================================
 * Cmd-Shift-F project search (project_search.c through the overlay)
 * ========================================================================== */

int wave_search_open(WaveSession *s) {
    if (!s) return 0;
    return overlay_open_search(&s->overlay);
}

int wave_search_active(const WaveSession *s) {
    return s && overlay_active(&s->overlay) == OVERLAY_SEARCH;
}

const char *wave_search_query(const WaveSession *s) {
    if (!s) return "";
    const char *q = overlay_query((OverlayState *)&s->overlay);
    return q ? q : "";
}

void wave_search_input(WaveSession *s, const char *text) {
    if (!s || !text) return;
    overlay_insert_text(&s->overlay, s->ws, s->ws ? ws_root(s->ws) : "", text);
}

/* Replace the whole query and re-run. The front-end owns the search box's caret
 * and selection, so it edits the text itself and hands the result over here —
 * the append-only wave_search_input() above cannot express an edit in the middle
 * of the line. */
void wave_search_set_query(WaveSession *s, const char *text) {
    if (!s || !text) return;
    overlay_set_search_query(&s->overlay, s->ws ? ws_root(s->ws) : "", text);
}

void wave_search_backspace(WaveSession *s) {
    if (s) overlay_backspace(&s->overlay, s->ws, s->ws ? ws_root(s->ws) : "");
}

void wave_search_move(WaveSession *s, int delta) {
    if (s) overlay_move(&s->overlay, delta);
}

int wave_search_poll(WaveSession *s) {
    if (!s || overlay_active(&s->overlay) != OVERLAY_SEARCH) return 0;
    int running = overlay_search_running(&s->overlay);
    overlay_poll_search(&s->overlay);
    return running;
}

int wave_search_running(const WaveSession *s) {
    return s ? overlay_search_running(&s->overlay) : 0;
}

size_t wave_search_count(const WaveSession *s) {
    return s ? project_search_count(&s->overlay.search) : 0;
}

int wave_search_selected(const WaveSession *s) {
    return s ? s->overlay.search.sel : 0;
}

int wave_search_hit(const WaveSession *s, size_t i, const char **path,
                    int *line, int *col, const char **text) {
    if (!s) return 0;
    const SearchHit *h = project_search_hit(&s->overlay.search, i);
    if (!h) return 0;
    if (path) *path = h->path;
    if (line) *line = h->line;
    if (col) *col = h->col;
    if (text) *text = h->text;
    return 1;
}

int wave_search_truncated(const WaveSession *s) {
    return s ? project_search_truncated(&s->overlay.search) : 0;
}

size_t wave_search_file_count(const WaveSession *s) {
    return s ? project_search_file_count(&s->overlay.search) : 0;
}

/* The status shown under the results ("128 matches in 9 files"), from the same
 * helper the GLFW panel draws. */
size_t wave_search_status(const WaveSession *s, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!s) return 0;
    const ProjectSearch *ps = &s->overlay.search;
    view_search_status(out, cap, ps->unavailable, ps->query_len,
                       project_search_running(ps), project_search_truncated(ps),
                       (int)project_search_count(ps),
                       (int)project_search_file_count(ps));
    return strlen(out);
}

/* One result row, laid out by view_search_row(): the file's name and elided
 * directory, the matched line windowed to `text_cells`, and where the query sits
 * inside it. The strings live in a static row, valid until the next call — the
 * caller (a single-threaded renderer) copies them out immediately. */
int wave_search_row(const WaveSession *s, size_t i, int dir_cells,
                    int text_cells, const char **name, const char **dir,
                    const char **text, int *line, int *repeats,
                    int *match_start, int *match_len, size_t *group_count) {
    if (!s) return 0;
    const ProjectSearch *ps = &s->overlay.search;
    const SearchHit *h = project_search_hit(ps, i);
    if (!h) return 0;
    const SearchHit *prev = i ? project_search_hit(ps, i - 1) : NULL;

    static ViewSearchRow row;
    static char row_path[sizeof h->path];
    /* view_search_row() returns `name` as a pointer into the path it was given,
     * so the path has to outlive the call as well. */
    snprintf(row_path, sizeof row_path, "%s", h->path);
    row = view_search_row(row_path, h->line, h->text,
                          overlay_query((OverlayState *)&s->overlay),
                          prev ? prev->path : NULL, dir_cells, text_cells);

    if (name) *name = row.name;
    if (dir) *dir = row.dir;
    if (text) *text = row.text;
    if (line) *line = row.line;
    if (repeats) *repeats = row.repeats;
    if (match_start) *match_start = row.match_start;
    if (match_len) *match_len = row.match_len;
    if (group_count) *group_count = project_search_group_count(ps, i);
    return 1;
}

/* Fit `text` into `cells` columns, keeping the tail: ".../orders/detail". */
void wave_elide_left(const char *text, int cells, char *out, size_t cap) {
    view_elide_left(out, cap, text, cells);
}

/* Open the highlighted hit at its line/column. */
int wave_search_accept(WaveSession *s) {
    if (!s) return 0;
    OverlayAcceptTarget t = overlay_accept_target(&s->overlay, s->ws);
    int ok = 0;
    if (t.has_target && t.path) {
        ok = open_in_tab(s, t.path, 0);
        if (ok && t.has_location) {
            Editor *e = tabs_current(&s->tabs);
            if (e) editor_move_to_line_col(e, t.line, t.col);
        }
    }
    overlay_accept_target_free(&t);
    overlay_close(&s->overlay);
    return ok;
}

/* ==========================================================================
 * `:` command line (command.c)
 * ========================================================================== */

int wave_cmd_active(const WaveSession *s) { return s ? s->cmd.active : 0; }

const char *wave_cmd_text(const WaveSession *s) {
    return s ? command_text((CommandLine *)&s->cmd) : "";
}

void wave_cmd_open(WaveSession *s) {
    if (s) command_open(&s->cmd);
}

void wave_cmd_close(WaveSession *s) {
    if (s) command_close(&s->cmd);
}

void wave_cmd_input(WaveSession *s, const char *text) {
    if (s && text) command_insert_text(&s->cmd, text);
}

void wave_cmd_backspace(WaveSession *s) {
    if (s) command_backspace(&s->cmd);
}

/* The tab-spawning commands main.c checks *before* handing the text to
 * command_run: `:term`, `:codex`, `:claude`, `:git`. */
static int cmd_open_tab(WaveSession *s, const char *text) {
    if (!text) return 0;
    const char *root = s->ws ? ws_root(s->ws) : ".";

    if (!strcmp(text, "term") || !strcmp(text, "terminal")) {
        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/zsh";
        const char *argv[] = {shell, NULL};
        tabs_new_terminal(&s->tabs, "term", root, argv);
        snprintf(s->info, sizeof s->info, "terminal opened");
        return 1;
    }
    if (!strcmp(text, "codex")) {
        const char *argv[] = {"codex", NULL};
        tabs_new_terminal(&s->tabs, "codex", root, argv);
        snprintf(s->info, sizeof s->info, "codex opened");
        return 1;
    }
    if (!strcmp(text, "claude")) {
        const char *argv[] = {"claude", NULL};
        tabs_new_terminal(&s->tabs, "claude", root, argv);
        snprintf(s->info, sizeof s->info, "claude opened");
        return 1;
    }
    if (!strcmp(text, "git") || !strcmp(text, "changes")) {
        tabs_new_git(&s->tabs, "git", root);
        snprintf(s->info, sizeof s->info, "git opened");
        return 1;
    }
    return 0;
}

/* Run the typed command. Returns a CommandCloseAction so the front-end knows
 * whether to close a tab or the window; `info` carries any message. */
int wave_cmd_accept(WaveSession *s) {
    if (!s) return 0;
    if (cmd_open_tab(s, command_text(&s->cmd))) {
        command_close(&s->cmd);
        return 0;
    }
    char config_path[1024];
    wave_config_path(config_path, sizeof config_path);

    CommandRun run = command_run(command_text(&s->cmd), &s->config, config_path);
    snprintf(s->info, sizeof s->info, "%s", run.info);

    Editor *e = cur(s);
    CommandAppPlan plan =
        command_app_plan(run.effect, e && editor_has_path(e));
    if (plan.write_file && e) editor_save_file(e, &s->watch);
    if (plan.save_config) wave_config_save(&s->config);

    command_close(&s->cmd);
    return (int)plan.close;
}

const char *wave_info(const WaveSession *s) { return s ? s->info : ""; }

void wave_info_clear(WaveSession *s) {
    if (s) s->info[0] = '\0';
}

/* ---- window / editor configuration (config.c) ----
 *
 * Namespaced `wave_cfg_` on purpose: libwave.a already exports
 * wave_config_toggle_sidebar, wave_config_zoom, wave_config_toggle_wrap and
 * friends, and reusing those names here would collide at link time. */

int wave_cfg_opacity_pct(const WaveSession *s) {
    return s ? (int)(s->config.opacity * 100.0f + 0.5f) : 100;
}

int wave_cfg_native_titlebar(const WaveSession *s) {
    return s ? s->config.native_titlebar : 0;
}

int wave_cfg_blur(const WaveSession *s) { return s ? s->config.blur : 0; }

float wave_cfg_radius(const WaveSession *s) { return s ? s->config.radius : 0.0f; }

/* The *effective* point size. main.c loads its font at
 * `base_pt * fb_scale * ui_scale`; the zoom shortcuts move ui_scale, not
 * base_pt, so reading base_pt alone makes zoom look like a no-op. GPUI handles
 * the device pixel ratio itself, so fb_scale is left out here. */
float wave_cfg_base_pt(const WaveSession *s) {
    if (!s) return 13.0f;
    float pt = s->config.base_pt > 0.0f ? s->config.base_pt : 13.0f;
    float scale = s->config.ui_scale > 0.0f ? s->config.ui_scale : 1.0f;
    return pt * scale;
}

int wave_cfg_show_sidebar(const WaveSession *s) {
    return s ? s->config.show_sidebar : 1;
}

int wave_cfg_wrap(const WaveSession *s) { return s ? s->config.wrap : 0; }

int wave_cfg_toggle_sidebar(WaveSession *s) {
    return s ? wave_config_toggle_sidebar(&s->config) : 0;
}

int wave_cfg_toggle_wrap(WaveSession *s) {
    if (!s) return 0;
    int on = wave_config_toggle_wrap(&s->config);
    wave_config_wrap_text(&s->config, s->info, sizeof s->info);
    return on;
}

/* dir: +1 larger, -1 smaller, 0 reset. */
int wave_cfg_zoom(WaveSession *s, int dir) {
    if (!s) return 0;
    int changed = wave_config_zoom(&s->config, dir);
    wave_config_zoom_text(&s->config, s->info, sizeof s->info);
    return changed;
}

int wave_cfg_save(WaveSession *s) {
    return s ? wave_config_save(&s->config) : 0;
}

/* ---- themes (theme.c) ----
 *
 * The active theme is process-global in C — theme_color() is called from
 * highlight drawing with no session in hand — so the listing accessors take no
 * session. Only selection does, because that is what records the choice in the
 * config and writes it out. */

int wave_theme_count(void) { return theme_count(); }
const char *wave_theme_name(int i) { return theme_name(i); }
const char *wave_theme_label(int i) { return theme_label(i); }
/* Takes a session it does not read, for symmetry with the rest of this ABI and
 * so the front-end need not know the theme is process-global. */
int wave_theme_index(const WaveSession *s) { return theme_active_index(); }

/* 0xRRGGBB for a chrome slot; see theme.h for the slot names. */
unsigned wave_theme_ui(const char *slot) { return theme_ui(slot); }

/* Repaint in `name` without committing to it — what arrowing through the theme
 * picker does. Escaping the picker previews the original back, so nothing here
 * touches the config. */
int wave_theme_preview(const char *name) { return theme_set(name); }

/* Select and persist. Saving here rather than on quit means the choice survives
 * a crash, and it is the only config write the front-end does not have to ask
 * for. Returns 1 if the name was known. */
int wave_theme_set(WaveSession *s, const char *name) {
    if (!theme_set(name)) return 0;
    if (s) {
        snprintf(s->config.theme, sizeof s->config.theme, "%s", name);
        wave_config_save(&s->config);
    }
    return 1;
}

/* ---- auto-update (updater_mac.m) ----
 *
 * The check is process-global and asynchronous: wave_check_updates() returns
 * immediately and the states arrive later, on the main thread, from a callback
 * the front-end cannot own (it would have to be a Rust fn pointer kept alive
 * across the FFI boundary for the life of the download). So the callback lands
 * here instead, in a slot the front-end drains from the same poll it already
 * runs for LSP replies and pty output.
 *
 * Only the newest state is kept. Every state is either terminal or a progress
 * tick, so a front-end that polls slower than the download reports simply sees
 * fewer percentages — never a wrong or out-of-order one. */
static struct {
    int state;
    char version[64];
    char detail[192];
    double progress;
    int pending; /* a state arrived that the front-end has not read yet */
} g_update;

static void wave_update_state_cb(int state, const char *version,
                                 const char *detail, double progress) {
    g_update.state = state;
    snprintf(g_update.version, sizeof g_update.version, "%s", version ? version : "");
    snprintf(g_update.detail, sizeof g_update.detail, "%s", detail ? detail : "");
    g_update.progress = progress;
    g_update.pending = 1;
}

/* `manual` distinguishes the palette command (report everything, including "up
 * to date" and errors) from the check on launch (stay quiet unless there is an
 * update). Safe to call again while one is in flight — the states just keep
 * coming from whichever check is furthest along. */
void wave_check_updates(int manual) {
    wave_check_for_updates(WAVE_VERSION, manual, wave_update_state_cb);
}

/* Returns 1 and fills the out-params when a new state has arrived since the
 * last call, else 0. The strings point at static storage and stay valid until
 * the next state arrives — copy them. */
int wave_update_poll(int *state, const char **version, const char **detail,
                     double *progress) {
    if (!g_update.pending) return 0;
    g_update.pending = 0;
    if (state) *state = g_update.state;
    if (version) *version = g_update.version;
    if (detail) *detail = g_update.detail;
    if (progress) *progress = g_update.progress;
    return 1;
}

/* What this build believes it is, for the front-end to show. A bundled Wave
 * reports its Info.plist version instead when it checks — this is the fallback
 * compiled in, and what an unbundled `cargo run` uses. */
const char *wave_version(void) { return WAVE_VERSION; }

/* Run the main run loop for `seconds`, so main-queue work gets a chance to run.
 *
 * Only for headless callers: the updater reports through dispatch_async onto the
 * main queue, which is serviced by whatever run loop the app is already running.
 * The GPUI front-end has one; `--selftest` does not, and without this its checks
 * would sit in the queue forever. Returns when the time is up, not when the
 * queue empties. */
void wave_pump_main_queue(double seconds) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
}

/* ==========================================================================
 * gh popover (popover.c)
 * ========================================================================== */

int wave_popover_active(const WaveSession *s) { return s ? s->pop.active : 0; }
int wave_popover_loading(const WaveSession *s) { return s ? s->pop.loading : 0; }
const char *wave_popover_text(const WaveSession *s) { return s ? s->pop.text : ""; }
int wave_popover_scroll(const WaveSession *s) { return s ? s->pop.scroll : 0; }

void wave_popover_close(WaveSession *s) {
    if (s) popover_close(&s->pop);
}

void wave_popover_scroll_by(WaveSession *s, int delta) {
    if (s) popover_scroll(&s->pop, delta);
}

void wave_popover_set_view(WaveSession *s, int total_rows, int vis_rows) {
    if (s) popover_set_view(&s->pop, total_rows, vis_rows);
}

/* ==========================================================================
 * `/` buffer search (command.c widget + editor_*_search)
 * ========================================================================== */

int wave_bufsearch_active(const WaveSession *s) {
    return s ? s->buf_search.active : 0;
}

/* Open it without going through vim's `/`, so ⌘F can reach it in standard
 * editing. Same entry point the `/` command uses. */
void wave_bufsearch_open(WaveSession *s) {
    if (!s) return;
    bufsearch_open(s);
}

/* Find next / previous, for ⌘G and ⇧⌘G — the `n` / `N` commands without vim.
 * Records a jump first, so a search that throws you across the file is as
 * recoverable as a go-to-definition. */
void wave_bufsearch_repeat(WaveSession *s, int reverse) {
    if (!s) return;
    wave_jump_push(s);
    bufsearch_repeat(s, reverse);
}

const char *wave_bufsearch_text(const WaveSession *s) {
    return s ? command_text((CommandLine *)&s->buf_search) : "";
}

static void bufsearch_open(WaveSession *s) {
    Editor *e = cur(s);
    if (!editor_has_buffer(e)) return;
    command_open(&s->buf_search);
    s->buf_search_editor = e;
    s->buf_search_origin = e->cursor;
    s->info[0] = '\0';
    enter_rest_mode(s);
}

static void bufsearch_close(WaveSession *s) {
    command_close(&s->buf_search);
    s->buf_search_editor = NULL;
}

/* Live preview from the original cursor as the query grows. */
static void bufsearch_update(WaveSession *s) {
    Editor *e = s->buf_search_editor;
    if (!editor_has_buffer(e)) return;
    editor_preview_search(e, command_text(&s->buf_search), s->buf_search_origin,
                          s->info, sizeof s->info);
}

void wave_bufsearch_input(WaveSession *s, const char *text) {
    if (!s || !text) return;
    command_insert_text(&s->buf_search, text);
    bufsearch_update(s);
}

void wave_bufsearch_backspace(WaveSession *s) {
    if (!s) return;
    command_backspace(&s->buf_search);
    bufsearch_update(s);
}

void wave_bufsearch_cancel(WaveSession *s) {
    if (!s) return;
    if (editor_has_buffer(s->buf_search_editor))
        s->buf_search_editor->cursor = s->buf_search_origin;
    s->info[0] = '\0';
    bufsearch_close(s);
}

void wave_bufsearch_accept(WaveSession *s) {
    if (!s) return;
    const char *q = command_text(&s->buf_search);
    if (q[0]) snprintf(s->last_buf_search, sizeof s->last_buf_search, "%s", q);
    bufsearch_close(s);
}

static void bufsearch_jump(WaveSession *s, const char *query, int reverse) {
    if (!query || !query[0]) return;
    Editor *e = cur(s);
    editor_search_text(e, query, reverse, s->info, sizeof s->info);
}

static void bufsearch_repeat(WaveSession *s, int reverse) {
    if (!s->last_buf_search[0]) {
        snprintf(s->info, sizeof s->info, "no previous search");
        return;
    }
    bufsearch_jump(s, s->last_buf_search, reverse);
}

static void bufsearch_word(WaveSession *s) {
    char word[256];
    if (!editor_word_under_cursor(cur(s), word, sizeof word)) {
        snprintf(s->info, sizeof s->info, "no word under cursor");
        return;
    }
    snprintf(s->last_buf_search, sizeof s->last_buf_search, "%s", word);
    bufsearch_jump(s, s->last_buf_search, 0);
}

/* Match ranges on `line`, so the UI can highlight them like the C renderer. */
size_t wave_line_matches(WaveSession *s, size_t line, WaveSpan *out, size_t max) {
    Editor *e = cur(s);
    if (!e || !e->buf || !out || max == 0) return 0;

    const char *needle = s->buf_search.active ? command_text(&s->buf_search)
                                              : s->last_buf_search;
    if (!needle || !needle[0]) return 0;

    size_t start, end;
    if (!line_bounds(e, line, &start, &end)) return 0;

    static EditorRange ranges[512];
    size_t n = editor_search_matches(e, needle, ranges,
                                     sizeof ranges / sizeof ranges[0]);
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        if (ranges[i].end <= start || ranges[i].start >= end) continue;
        size_t a = ranges[i].start < start ? start : ranges[i].start;
        size_t b = ranges[i].end > end ? end : ranges[i].end;
        if (a >= b) continue;
        out[k].start_col = a - start;
        out[k].end_col = b - start;
        out[k].name = "match";
        k++;
    }
    return k;
}

/* ==========================================================================
 * Yank register (so the front-end can mirror it to the system clipboard)
 * ========================================================================== */

const char *wave_yank_text(const WaveSession *s) {
    return (s && s->yank.text) ? s->yank.text : "";
}

/* ==========================================================================
 * Sidebar file operations (ws_create_* / ws_delete_path)
 * ========================================================================== */

/* dir_rel is the workspace-relative directory; "" means the root. */
int wave_ws_create(WaveSession *s, const char *dir_rel, const char *name,
                   int is_dir, char *message, size_t cap) {
    if (!s || !s->ws || !name) return 0;
    WsFileEffect eff = is_dir ? ws_create_dir_in(s->ws, dir_rel, name)
                              : ws_create_file_in(s->ws, dir_rel, name);
    if (message && cap) snprintf(message, cap, "%s", eff.message);
    if (eff.ok && !is_dir && eff.path[0]) open_in_tab(s, eff.path, 0);
    return eff.ok;
}

int wave_ws_delete(WaveSession *s, const char *rel, char *message, size_t cap) {
    if (!s || !s->ws || !rel) return 0;
    WsFileEffect eff = ws_delete_path(s->ws, rel);
    if (message && cap) snprintf(message, cap, "%s", eff.message);
    return eff.ok;
}

/* Rename is a move onto the same parent: the core exposes it as paste+cut. */
int wave_ws_rename(WaveSession *s, const char *rel, const char *dir_rel,
                   char *message, size_t cap) {
    if (!s || !s->ws || !rel) return 0;
    char *full = ws_fullpath(s->ws, rel);
    if (!full) return 0;
    WsFileEffect eff = ws_paste_path_into(s->ws, full, dir_rel, 1);
    free(full);
    if (message && cap) snprintf(message, cap, "%s", eff.message);
    return eff.ok;
}

/* The directory a sidebar row belongs to, for "new file here". */
int wave_ws_parent_dir(const WaveSession *s, size_t vi, char *out, size_t cap) {
    if (!s || !s->ws || !out || cap == 0) return 0;
    out[0] = '\0';
    const WsEntry *e = ws_visible(s->ws, vi);
    if (!e) return 0;
    if (e->is_dir) {
        snprintf(out, cap, "%s", e->rel);
        return 1;
    }
    const char *slash = strrchr(e->rel, '/');
    if (slash) {
        size_t n = (size_t)(slash - e->rel);
        if (n >= cap) n = cap - 1;
        memcpy(out, e->rel, n);
        out[n] = '\0';
    }
    return 1;
}

/* ==========================================================================
 * Recent projects (recent.c)
 * ========================================================================== */

size_t wave_recent_count(const WaveSession *s) {
    return s ? s->recent.filtered_count : 0;
}

const char *wave_recent_path(const WaveSession *s, size_t i) {
    if (!s) return "";
    const char *p = recent_projects_filtered_path(&s->recent, i);
    return p ? p : "";
}

int wave_recent_selected(const WaveSession *s) {
    return s ? s->recent.selected : 0;
}

void wave_recent_move(WaveSession *s, int delta) {
    if (s) recent_projects_move(&s->recent, delta);
}

void wave_recent_input(WaveSession *s, const char *text) {
    if (s && text) recent_projects_insert_text(&s->recent, text);
}

void wave_recent_backspace(WaveSession *s) {
    if (s) recent_projects_backspace(&s->recent);
}

const char *wave_recent_query(const WaveSession *s) {
    return s ? s->recent.query : "";
}

/* Open the highlighted project as the workspace. */
int wave_recent_accept(WaveSession *s) {
    if (!s) return 0;
    const char *path = recent_projects_selected(&s->recent);
    if (!path || !*path) return 0;
    return wave_open_path(s, path) == 0;
}

/* ==========================================================================
 * File watching (watch.c) — external edits reload the affected tab
 * ========================================================================== */

int wave_watch_poll(WaveSession *s, double now, char *message, size_t cap) {
    if (!s) return 0;
    if (message && cap) message[0] = '\0';

    static double next_poll = 0.0;
    TabWatchEffect effect =
        tabs_process_file_watchers(&s->tabs, &s->watch, now, &next_poll, 1.0);
    if (effect.has_message) {
        if (message && cap) snprintf(message, cap, "%s", effect.message);
        snprintf(s->info, sizeof s->info, "%s", effect.message);
    }
    if (effect.reset_mode) enter_rest_mode(s);

    /* The workspace tree itself may have changed on disk. */
    if (s->ws) {
        WsReloadEffect ws_effect =
            ws_apply_watch_event(s->ws, watch_workspace_consume(&s->watch));
        if (ws_effect.ok && ws_effect.refilter_palette)
            overlay_refilter_palette(&s->overlay, s->ws);
    }
    if (effect.has_message) refresh_diags(s);
    return effect.has_message || effect.reset_mode;
}

void wave_watch_workspace_start(WaveSession *s) {
    if (s && s->ws) watch_workspace_start(&s->watch, ws_root(s->ws));
}

/* ==========================================================================
 * Clipboard paste
 * ========================================================================== */

/* Paste `text` at the cursor, replacing a selection when one is live.
 * Returns 1 if the caller should switch to INSERT (editor_paste_enters_insert
 * decides, mirroring what main.c does after a paste). */
int wave_paste(WaveSession *s, const char *text) {
    Editor *e = cur(s);
    if (!e || !e->buf || !text || !*text) return 0;

    int replace = wave_has_selection(s);
    EditorPasteResult r = editor_paste_text(e, text, replace);
    if (r == EDITOR_PASTE_NONE) return 0;

    /* The selection is consumed by the paste either way; under standard editing
     * that lands back in insert with the caret after the pasted text. */
    if (s->modal.mode == MODE_VISUAL) enter_rest_mode(s);
    editor_update_highlighter(e);
    lsp_manager_push_change(&s->lsp, e);
    refresh_diags(s);

    if (editor_paste_enters_insert(r)) {
        modal_enter_insert(&s->modal);
        return 1;
    }
    return 0;
}

/* editor_center_cursor()/editor_scroll_y() are deliberately not exposed: they
 * drive e->scroll_y in pixels, and this front-end owns scrolling itself in
 * visual rows. Centring is done on the Rust side instead. */

/* ==========================================================================
 * Terminal selection
 * ========================================================================== */

void wave_term_sel_begin(WaveSession *s, size_t row, int col) {
    Terminal *t = cur_term(s);
    if (t) terminal_selection_begin(t, row, col);
}

void wave_term_sel_update(WaveSession *s, size_t row, int col) {
    Terminal *t = cur_term(s);
    if (t) terminal_selection_update(t, row, col);
}

void wave_term_sel_end(WaveSession *s) {
    Terminal *t = cur_term(s);
    if (t) terminal_selection_end(t);
}

void wave_term_sel_clear(WaveSession *s) {
    Terminal *t = cur_term(s);
    if (t) terminal_selection_clear(t);
}

/* Selected columns on terminal row `row`, or 0 if none. */
int wave_term_sel_span(const WaveSession *s, size_t row, int *start, int *end) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    if (!t) return 0;
    return terminal_selection_span(t, row, start, end);
}

/* Caller frees with wave_string_free(). */
char *wave_term_copy_selection(const WaveSession *s) {
    const Terminal *t = s ? tabs_current_terminal_const(&s->tabs) : NULL;
    return t ? terminal_copy_selection(t) : NULL;
}

/* ==========================================================================
 * Git diff selection
 * ========================================================================== */

void wave_git_sel_begin(WaveSession *s, int line, int col) {
    GitView *g = cur_git(s);
    if (g) git_view_diff_selection_begin(g, line, col);
}

void wave_git_sel_update(WaveSession *s, int line, int col) {
    GitView *g = cur_git(s);
    if (g) git_view_diff_selection_update(g, line, col);
}

void wave_git_sel_end(WaveSession *s) {
    GitView *g = cur_git(s);
    if (g) git_view_diff_selection_end(g);
}

void wave_git_sel_clear(WaveSession *s) {
    GitView *g = cur_git(s);
    if (g) git_view_diff_selection_clear(g);
}

int wave_git_sel_span(const WaveSession *s, int line, int *start, int *end) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    if (!g) return 0;
    return git_view_diff_selection_span(g, line, start, end);
}

char *wave_git_copy_selection(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? git_view_copy_diff_selection(g) : NULL;
}

int wave_git_diff_scroll_pos(const WaveSession *s) {
    const GitView *g = s ? tabs_current_git_const(&s->tabs) : NULL;
    return g ? g->diff_scroll : 0;
}

/* ==========================================================================
 * Signature help
 * ========================================================================== */

/* Ask for signature help at the cursor. `trigger` is '(' or ',' when a
 * character prompted it, 0 for an explicit request. */
int wave_signature_request(WaveSession *s, unsigned int trigger, int retrigger) {
    Editor *e = cur(s);
    if (!e || !e->buf) return 0;
    size_t row = 0, col = 0;
    pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
    return lsp_manager_request_signature_help(&s->lsp, e, (int)row, (int)col,
                                              (char)trigger, retrigger);
}

/* ==========================================================================
 * Soft wrap
 *
 * wrap_build() fills e->vstart[i] with the first *visual* row of logical line
 * i (and vstart[lines] with the total), so the view can scroll and paint in
 * visual rows once wrapping is on. cols <= 0 means "no wrapping".
 * ========================================================================== */

typedef struct {
    size_t line;       /* logical line */
    size_t start_byte; /* byte range within that line */
    size_t end_byte;
} WaveVisualRow;

void wave_wrap_set_cols(WaveSession *s, int cols) {
    Editor *e = cur(s);
    if (!e || !e->buf) return;
    int want = (s->config.wrap && cols > 0) ? cols : WRAP_NOWRAP_COLS;
    wrap_build(e, want);
}

size_t wave_visual_rows(WaveSession *s) {
    Editor *e = cur(s);
    if (!e || !e->buf || !e->vstart || e->vstart_n == 0) return 0;
    return (size_t)e->vstart[e->vstart_n - 1];
}

/* Resolve a visual row to its logical line and byte span. */
int wave_visual_row(WaveSession *s, size_t vrow, WaveVisualRow *out) {
    Editor *e = cur(s);
    if (!e || !e->buf || !e->vstart || !out) return 0;

    int line = line_at_vrow(e, (int)vrow);
    const PieceTable *pt = buffer_pt(e->buf);
    size_t lines = pt_line_count(pt);
    if (line < 0 || (size_t)line >= lines) return 0;

    size_t start, end;
    if (!line_bounds(e, (size_t)line, &start, &end)) return 0;
    size_t llen = end - start;

    out->line = (size_t)line;
    out->start_byte = 0;
    out->end_byte = llen;

    if (e->wrap_cols >= WRAP_NOWRAP_COLS) return 1; /* one row per line */

    int sub = (int)vrow - e->vstart[line];
    if (sub < 0) sub = 0;

    static char buf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    size_t n = llen > WRAP_MAXLINE ? WRAP_MAXLINE : llen;
    pt_read(pt, start, n, buf);
    int nb = wrap_line(buf, n, e->wrap_cols, brk, WRAP_MAXBRK);
    if (nb <= 0) return 1;
    if (sub >= nb) sub = nb - 1;

    out->start_byte = (size_t)brk[sub];
    out->end_byte = (sub + 1 < nb) ? (size_t)brk[sub + 1] : n;
    return 1;
}

/* Visual row containing the cursor, and its column within that row. */
/* Visual (wrapped) row and column of an arbitrary byte offset. Split out of
 * wave_cursor_visual so the extra carets can be placed with the same wrap
 * arithmetic rather than a second, subtly different copy of it. */
int wave_offset_visual(WaveSession *s, size_t offset, size_t *vrow, size_t *col) {
    Editor *e = cur(s);
    if (!e || !e->buf || !e->vstart) return 0;

    size_t row = 0, c = 0;
    pt_offset_to_rowcol(buffer_pt(e->buf), offset, &row, &c);
    if (row >= (size_t)e->vstart_n - 1) return 0;

    if (e->wrap_cols >= WRAP_NOWRAP_COLS) {
        if (vrow) *vrow = (size_t)e->vstart[row];
        if (col) *col = c;
        return 1;
    }

    size_t start, end;
    if (!line_bounds(e, row, &start, &end)) return 0;
    size_t llen = end - start;

    static char buf[WRAP_MAXLINE];
    static int brk[WRAP_MAXBRK];
    size_t n = llen > WRAP_MAXLINE ? WRAP_MAXLINE : llen;
    pt_read(buffer_pt(e->buf), start, n, buf);
    int nb = wrap_line(buf, n, e->wrap_cols, brk, WRAP_MAXBRK);

    int sub = 0;
    for (int i = 0; i < nb; i++)
        if ((size_t)brk[i] <= c) sub = i;
    if (vrow) *vrow = (size_t)(e->vstart[row] + sub);
    if (col) *col = c - (size_t)brk[sub];
    return 1;
}

int wave_cursor_visual(WaveSession *s, size_t *vrow, size_t *col) {
    Editor *e = cur(s);
    if (!e) return 0;
    return wave_offset_visual(s, e->cursor, vrow, col);
}

/* Visual position of extra caret i, for painting it. */
int wave_caret_visual(WaveSession *s, size_t i, size_t *vrow, size_t *col) {
    Editor *e = cur(s);
    size_t anchor = 0, cursor = 0;
    if (!e || !standard_caret_at(e, i, &anchor, &cursor)) return 0;
    return wave_offset_visual(s, cursor, vrow, col);
}

/* Every selection on `line`, primary and extra carets alike, as byte columns.
 * Returns how many were written (capped at `max`). The renderer needs all of
 * them: with ⌘D the matches are usually on different lines, but nothing stops
 * two carets sharing one. */
size_t wave_line_selections(WaveSession *s, size_t line, size_t *starts,
                            size_t *ends, size_t max) {
    Editor *e = cur(s);
    if (!e || !e->buf || !starts || !ends || max == 0) return 0;
    if (s->modal.mode != MODE_VISUAL) return 0;
    size_t line_a, line_b;
    if (!line_bounds(e, line, &line_a, &line_b)) return 0;

    size_t n = 0;
    size_t keep_cursor = e->cursor, keep_anchor = e->anchor;
    size_t total = standard_caret_count(e) + 1;
    for (size_t i = 0; i < total && n < max; i++) {
        if (i > 0) {
            size_t a, c;
            if (!standard_caret_at(e, i - 1, &a, &c)) break;
            e->anchor = a;
            e->cursor = c;
        } else {
            e->cursor = keep_cursor;
            e->anchor = keep_anchor;
        }
        EditorRange sel;
        if (!editor_visual_range(e, &sel)) continue;
        if (sel.end <= line_a || sel.start > line_b) continue;
        size_t lo = sel.start < line_a ? line_a : sel.start;
        size_t hi = sel.end > line_b ? line_b : sel.end;
        starts[n] = lo - line_a;
        ends[n] = hi - line_a;
        n++;
    }
    e->cursor = keep_cursor;
    e->anchor = keep_anchor;
    return n;
}

/* Close the workspace and every tab, returning to the empty state. */
void wave_close_workspace(WaveSession *s) {
    if (!s) return;
    for (int i = tabs_count(&s->tabs) - 1; i >= 0; i--) tabs_close(&s->tabs, i);
    overlay_close(&s->overlay);
    complete_close(&s->comp);
    popover_close(&s->pop);
    command_close(&s->cmd);
    watch_workspace_stop(&s->watch);
    if (s->ws) {
        ws_free(s->ws);
        s->ws = NULL;
    }
    s->ndiags = 0;
    s->info[0] = '\0';
    enter_rest_mode(s);
}

/* Record a project in the recents list (and persist it). */
void wave_recent_add(WaveSession *s, const char *path) {
    if (!s || !path || !*path) return;
    if (recent_projects_add(&s->recent, path)) recent_projects_save(&s->recent);
}

/* Reset every setting to config.c's defaults and persist. There is no `:`
 * command for this — command_parse() has no such verb — so it is exposed
 * directly for the front-end's command palette. */
int wave_cfg_defaults(WaveSession *s) {
    if (!s) return 0;
    wave_config_defaults(&s->config);
    /* Applied, not just recorded — the defaults put vim back on, and the editor
     * has to actually return to NORMAL for that to mean anything. */
    standard_set_enabled(cur(s), &s->modal, !s->config.vim);
    snprintf(s->info, sizeof s->info, "settings reset to defaults");
    return wave_config_save(&s->config);
}

/* Direct setters for the settings screen. The clamps mirror command.c's, so
 * `:opacity 0.8` and the settings screen accept exactly the same range. */

void wave_cfg_set_opacity(WaveSession *s, float v) {
    if (!s) return;
    if (v < 0.2f) v = 0.2f;
    if (v > 1.0f) v = 1.0f;
    s->config.opacity = v;
}

void wave_cfg_set_radius(WaveSession *s, float v) {
    if (!s) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 40.0f) v = 40.0f;
    s->config.radius = v;
}

void wave_cfg_set_base_pt(WaveSession *s, float v) {
    if (!s) return;
    if (v < 8.0f) v = 8.0f;
    if (v > 48.0f) v = 48.0f;
    s->config.base_pt = v;
}

void wave_cfg_set_side_cells(WaveSession *s, int v) {
    if (!s) return;
    if (v < 10) v = 10;
    if (v > 80) v = 80;
    s->config.side_cells = v;
}

int wave_cfg_side_cells(const WaveSession *s) { return s ? s->config.side_cells : 26; }

int wave_cfg_toggle_blur(WaveSession *s) {
    if (!s) return 0;
    s->config.blur = !s->config.blur;
    return s->config.blur;
}

int wave_cfg_toggle_titlebar(WaveSession *s) {
    if (!s) return 0;
    s->config.native_titlebar = !s->config.native_titlebar;
    return s->config.native_titlebar;
}

/* ui_scale as a percentage, for display alongside base_pt. */
int wave_cfg_scale_pct(const WaveSession *s) {
    return s ? (int)(s->config.ui_scale * 100.0f + 0.5f) : 100;
}
