//! Safe wrapper over Wave's C core (`libwave.a` via `shim/wave_ffi.c`).
//!
//! The seam is deliberately narrow: commands in, state out. Nothing here knows
//! the layout of `Editor`, `TabSet`, `Workspace` or the piece table.
//!
//! This module exposes the whole seam, including a few accessors the current
//! front-end does not read yet (`goto_line`, `native_titlebar`, …). They are
//! part of the C API surface being bound, so they are kept rather than trimmed
//! to whatever today's UI happens to call.
#![allow(dead_code)]

use std::ffi::{c_char, c_int, c_uint, c_void, CStr, CString};
use std::path::Path;

#[repr(C)]
#[derive(Clone, Copy)]
struct WaveSpanRaw {
    start_col: usize,
    end_col: usize,
    name: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct WaveEntryRaw {
    rel: *const c_char,
    name: *const c_char,
    depth: c_int,
    is_dir: c_int,
    collapsed: c_int,
}

unsafe extern "C" {
    fn wave_new() -> *mut c_void;
    fn wave_free(s: *mut c_void);
    fn wave_open_path(s: *mut c_void, path: *const c_char) -> c_int;

    fn wave_cli_open_request(
        argc: c_int,
        argv: *const *const c_char,
        path: *mut *const c_char,
        line: *mut c_int,
        column: *mut c_int,
    ) -> c_int;

    fn wave_has_workspace(s: *const c_void) -> c_int;
    fn wave_ws_root(s: *const c_void) -> *const c_char;
    fn wave_ws_count(s: *const c_void) -> usize;
    fn wave_ws_entry(s: *const c_void, vi: usize, out: *mut WaveEntryRaw) -> c_int;
    fn wave_ws_activate(s: *mut c_void, row: c_int, double_click: c_int) -> c_int;

    fn wave_palette_open(s: *mut c_void) -> c_int;
    fn wave_palette_close(s: *mut c_void);
    fn wave_palette_active(s: *const c_void) -> c_int;
    fn wave_palette_query(s: *const c_void) -> *const c_char;
    fn wave_palette_count(s: *const c_void) -> usize;
    fn wave_palette_selected(s: *const c_void) -> c_int;
    fn wave_palette_entry(s: *const c_void, i: usize, out: *mut WaveEntryRaw) -> c_int;
    fn wave_palette_input(s: *mut c_void, text: *const c_char);
    fn wave_palette_backspace(s: *mut c_void);
    fn wave_palette_move(s: *mut c_void, delta: c_int);
    fn wave_palette_accept(s: *mut c_void) -> c_int;

    fn wave_tab_count(s: *const c_void) -> c_int;
    fn wave_tab_active(s: *const c_void) -> c_int;
    fn wave_tab_label(s: *const c_void, i: c_int, out: *mut c_char, cap: usize) -> usize;
    fn wave_tab_modified(s: *const c_void, i: c_int) -> c_int;
    fn wave_tab_set_active(s: *mut c_void, i: c_int);
    fn wave_tab_close(s: *mut c_void, i: c_int);
    fn wave_tab_goto(s: *mut c_void, delta: c_int);

    fn wave_has_buffer(s: *const c_void) -> c_int;
    fn wave_path(s: *const c_void) -> *const c_char;
    fn wave_line_count(s: *const c_void) -> usize;
    fn wave_line_text(s: *const c_void, line: usize, out: *mut c_char, cap: usize) -> usize;
    fn wave_line_spans(s: *mut c_void, line: usize, out: *mut WaveSpanRaw, max: usize) -> usize;
    fn wave_line_diagnostics(
        s: *mut c_void,
        line: usize,
        out: *mut WaveSpanRaw,
        max: usize,
    ) -> usize;
    fn wave_line_selection(s: *mut c_void, line: usize, a: *mut usize, b: *mut usize) -> c_int;
    fn wave_line_selections(
        s: *mut c_void,
        line: usize,
        starts: *mut usize,
        ends: *mut usize,
        max: usize,
    ) -> usize;
    fn wave_caret_visual(s: *mut c_void, i: usize, vrow: *mut usize, col: *mut usize) -> c_int;
    fn wave_click_at(s: *mut c_void, line: c_int, col: c_int);
    fn wave_drag_to(s: *mut c_void, line: c_int, col: c_int);
    fn wave_has_selection(s: *const c_void) -> c_int;
    fn wave_selection_text(s: *mut c_void) -> *mut c_char;
    fn wave_string_free(p: *mut c_char);
    fn wave_theme_rgb(name: *const c_char) -> c_uint;
    fn wave_theme_ui(slot: *const c_char) -> c_uint;
    fn wave_theme_count() -> c_int;
    fn wave_theme_name(i: c_int) -> *const c_char;
    fn wave_theme_label(i: c_int) -> *const c_char;
    fn wave_theme_index(s: *const c_void) -> c_int;
    fn wave_theme_preview(name: *const c_char) -> c_int;
    fn wave_theme_set(s: *mut c_void, name: *const c_char) -> c_int;
    fn wave_check_updates(manual: c_int);
    fn wave_update_poll(
        state: *mut c_int,
        version: *mut *const c_char,
        detail: *mut *const c_char,
        progress: *mut f64,
    ) -> c_int;
    fn wave_version() -> *const c_char;
    fn wave_pump_main_queue(seconds: f64);
    fn wave_cursor(s: *const c_void, row: *mut usize, col: *mut usize);
    fn wave_mode(s: *const c_void) -> c_int;
    fn wave_mode_name(s: *const c_void) -> *const c_char;
    fn wave_modified(s: *const c_void) -> c_int;

    fn wave_cursor_diagnostic(s: *mut c_void, out: *mut c_char, cap: usize) -> usize;
    fn wave_lsp_active(s: *const c_void) -> c_int;
    fn wave_lsp_poll(s: *mut c_void) -> c_int;
    fn wave_hover(s: *const c_void) -> *const c_char;
    fn wave_hover_clear(s: *mut c_void);

    fn wave_text_input(s: *mut c_void, cp: c_uint) -> c_uint;
    fn wave_special_key(s: *mut c_void, key: c_int) -> c_int;
    fn wave_escape(s: *mut c_void);
    fn wave_motion(s: *mut c_void, motion: c_int, extend: c_int) -> c_int;
    fn wave_select_all(s: *mut c_void) -> c_int;
    fn wave_select_word(s: *mut c_void) -> c_int;
    fn wave_standard_copy(s: *mut c_void) -> *mut c_char;
    fn wave_standard_cut(s: *mut c_void) -> *mut c_char;
    fn wave_delete_line(s: *mut c_void) -> c_int;
    fn wave_duplicate_line(s: *mut c_void) -> c_int;
    fn wave_move_line(s: *mut c_void, dir: c_int) -> c_int;
    fn wave_delete_to_line_start(s: *mut c_void) -> c_int;
    fn wave_toggle_comment(s: *mut c_void) -> c_int;
    fn wave_goto_definition(s: *mut c_void);
    fn wave_indent(s: *mut c_void, outdent: c_int) -> c_int;
    fn wave_delete_word_left(s: *mut c_void) -> c_int;
    fn wave_delete_word_right(s: *mut c_void) -> c_int;
    fn wave_delete_to_line_end(s: *mut c_void) -> c_int;
    fn wave_select_line(s: *mut c_void) -> c_int;
    fn wave_insert_line(s: *mut c_void, below: c_int) -> c_int;
    fn wave_select_next_occurrence(s: *mut c_void) -> c_int;
    fn wave_add_caret_at(s: *mut c_void, line: c_int, col: c_int) -> c_int;
    fn wave_caret_count(s: *const c_void) -> usize;
    fn wave_clear_carets(s: *mut c_void);
    fn wave_caret_at(s: *const c_void, i: usize, anchor: *mut usize, cursor: *mut usize) -> c_int;
    fn wave_jump_push(s: *mut c_void);
    fn wave_jump_go(s: *mut c_void, dir: c_int) -> c_int;
    fn wave_cursor_visible(blink: c_int, now: f64, last_activity: f64) -> c_int;
    fn wave_vim_enabled(s: *const c_void) -> c_int;
    fn wave_set_vim_enabled(s: *mut c_void, on: c_int) -> c_int;
    fn wave_toggle_vim(s: *mut c_void) -> c_int;
    fn wave_undo(s: *mut c_void) -> c_int;
    fn wave_redo(s: *mut c_void) -> c_int;
    fn wave_save(s: *mut c_void) -> c_int;
    fn wave_goto_line(s: *mut c_void, line: c_int, column: c_int);

    // terminal
    fn wave_tab_kind(s: *const c_void, i: c_int) -> c_int;
    fn wave_term_open(s: *mut c_void, label: *const c_char, cmd: *const c_char) -> c_int;
    fn wave_term_active(s: *const c_void) -> c_int;
    fn wave_term_poll(s: *mut c_void) -> c_int;
    fn wave_term_resize(s: *mut c_void, rows: c_int, cols: c_int);
    fn wave_term_visible_start(s: *const c_void, rows: c_int) -> usize;
    fn wave_term_total_lines(s: *const c_void) -> usize;
    fn wave_term_col_to_byte(s: *const c_void, index: usize, col: usize) -> usize;
    fn wave_term_line(s: *const c_void, index: usize, out: *mut c_char, cap: usize) -> usize;
    fn wave_term_line_styles(
        s: *const c_void,
        index: usize,
        out: *mut WaveCellStyleRaw,
        max: usize,
    ) -> usize;
    fn wave_term_cursor(s: *const c_void, row: *mut c_int, col: *mut c_int, vis: *mut c_int);
    fn wave_term_rows(s: *const c_void) -> c_int;
    fn wave_term_running(s: *const c_void) -> c_int;
    fn wave_term_status(s: *const c_void) -> *const c_char;
    fn wave_term_write(s: *mut c_void, text: *const c_char);
    fn wave_term_key(s: *mut c_void, key: c_int, shift: c_int, alt: c_int, ctrl: c_int);
    fn wave_term_scroll(s: *mut c_void, units: c_int);

    // git
    fn wave_git_open(s: *mut c_void) -> c_int;
    fn wave_git_active(s: *const c_void) -> c_int;
    fn wave_git_mode(s: *const c_void) -> c_int;
    fn wave_git_repo_count(s: *const c_void) -> c_int;
    fn wave_git_repo_label(s: *const c_void, i: c_int) -> *const c_char;
    fn wave_git_selected_repo(s: *const c_void) -> c_int;
    fn wave_git_file_count(s: *const c_void) -> c_int;
    fn wave_git_file(
        s: *const c_void,
        i: c_int,
        code: *mut *const c_char,
        path: *mut *const c_char,
    ) -> c_int;
    fn wave_git_selected_file(s: *const c_void) -> c_int;
    fn wave_git_diff_count(s: *const c_void) -> c_int;
    fn wave_git_diff_line(s: *const c_void, i: c_int) -> *const c_char;
    fn wave_git_message(s: *const c_void) -> *const c_char;
    fn wave_git_info(s: *const c_void) -> *const c_char;
    fn wave_git_move(s: *mut c_void, delta: c_int);
    fn wave_git_accept(s: *mut c_void) -> c_int;
    fn wave_git_stage_toggle(s: *mut c_void) -> c_int;
    fn wave_git_begin_commit(s: *mut c_void) -> c_int;
    fn wave_git_commit(s: *mut c_void) -> c_int;
    fn wave_git_cancel_input(s: *mut c_void);
    fn wave_git_insert_text(s: *mut c_void, text: *const c_char) -> c_int;
    fn wave_git_backspace(s: *mut c_void) -> c_int;
    fn wave_git_refresh(s: *mut c_void) -> c_int;
    fn wave_git_diff_scroll(s: *mut c_void, delta: c_int);

    // completion
    fn wave_complete_active(s: *const c_void) -> c_int;
    fn wave_complete_loading(s: *const c_void) -> c_int;
    fn wave_complete_count(s: *const c_void) -> c_int;
    fn wave_complete_selected(s: *const c_void) -> c_int;
    fn wave_complete_item(
        s: *const c_void,
        i: c_int,
        label: *mut *const c_char,
        detail: *mut *const c_char,
        kind: *mut *const c_char,
    ) -> c_int;
    fn wave_complete_move(s: *mut c_void, delta: c_int);
    fn wave_complete_close(s: *mut c_void);
    fn wave_complete_accept(s: *mut c_void) -> c_int;

    // project search
    fn wave_search_open(s: *mut c_void) -> c_int;
    fn wave_search_active(s: *const c_void) -> c_int;
    fn wave_search_query(s: *const c_void) -> *const c_char;
    fn wave_search_input(s: *mut c_void, text: *const c_char);
    fn wave_search_backspace(s: *mut c_void);
    fn wave_search_move(s: *mut c_void, delta: c_int);
    fn wave_search_poll(s: *mut c_void) -> c_int;
    fn wave_search_running(s: *const c_void) -> c_int;
    fn wave_search_count(s: *const c_void) -> usize;
    fn wave_search_selected(s: *const c_void) -> c_int;
    fn wave_search_hit(
        s: *const c_void,
        i: usize,
        path: *mut *const c_char,
        line: *mut c_int,
        col: *mut c_int,
        text: *mut *const c_char,
    ) -> c_int;
    fn wave_search_accept(s: *mut c_void) -> c_int;

    // command line
    fn wave_cmd_active(s: *const c_void) -> c_int;
    fn wave_cmd_text(s: *const c_void) -> *const c_char;
    fn wave_cmd_open(s: *mut c_void);
    fn wave_cmd_close(s: *mut c_void);
    fn wave_cmd_input(s: *mut c_void, text: *const c_char);
    fn wave_cmd_backspace(s: *mut c_void);
    fn wave_cmd_accept(s: *mut c_void) -> c_int;
    fn wave_info(s: *const c_void) -> *const c_char;
    fn wave_info_clear(s: *mut c_void);
    fn wave_cfg_opacity_pct(s: *const c_void) -> c_int;
    fn wave_cfg_native_titlebar(s: *const c_void) -> c_int;
    fn wave_cfg_blur(s: *const c_void) -> c_int;
    fn wave_cfg_radius(s: *const c_void) -> f32;
    fn wave_cfg_base_pt(s: *const c_void) -> f32;
    fn wave_cfg_show_sidebar(s: *const c_void) -> c_int;
    fn wave_cfg_wrap(s: *const c_void) -> c_int;
    fn wave_cfg_toggle_sidebar(s: *mut c_void) -> c_int;
    fn wave_cfg_toggle_wrap(s: *mut c_void) -> c_int;
    fn wave_cfg_zoom(s: *mut c_void, dir: c_int) -> c_int;
    fn wave_cfg_save(s: *mut c_void) -> c_int;
    fn wave_cfg_defaults(s: *mut c_void) -> c_int;
    fn wave_cfg_set_opacity(s: *mut c_void, v: f32);
    fn wave_cfg_set_radius(s: *mut c_void, v: f32);
    fn wave_cfg_set_base_pt(s: *mut c_void, v: f32);
    fn wave_cfg_set_side_cells(s: *mut c_void, v: c_int);
    fn wave_cfg_side_cells(s: *const c_void) -> c_int;
    fn wave_cfg_toggle_blur(s: *mut c_void) -> c_int;
    fn wave_cfg_toggle_titlebar(s: *mut c_void) -> c_int;
    fn wave_cfg_scale_pct(s: *const c_void) -> c_int;

    // buffer search
    fn wave_bufsearch_active(s: *const c_void) -> c_int;
    fn wave_bufsearch_open(s: *mut c_void);
    fn wave_bufsearch_repeat(s: *mut c_void, reverse: c_int);
    fn wave_bufsearch_text(s: *const c_void) -> *const c_char;
    fn wave_bufsearch_input(s: *mut c_void, text: *const c_char);
    fn wave_bufsearch_backspace(s: *mut c_void);
    fn wave_bufsearch_cancel(s: *mut c_void);
    fn wave_bufsearch_accept(s: *mut c_void);
    fn wave_line_matches(s: *mut c_void, line: usize, out: *mut WaveSpanRaw, max: usize) -> usize;
    fn wave_yank_text(s: *const c_void) -> *const c_char;

    // sidebar file operations
    fn wave_ws_create(
        s: *mut c_void,
        dir_rel: *const c_char,
        name: *const c_char,
        is_dir: c_int,
        message: *mut c_char,
        cap: usize,
    ) -> c_int;
    fn wave_ws_delete(
        s: *mut c_void,
        rel: *const c_char,
        message: *mut c_char,
        cap: usize,
    ) -> c_int;
    fn wave_ws_parent_dir(s: *const c_void, vi: usize, out: *mut c_char, cap: usize) -> c_int;

    // recent projects
    fn wave_recent_count(s: *const c_void) -> usize;
    fn wave_recent_path(s: *const c_void, i: usize) -> *const c_char;
    fn wave_recent_selected(s: *const c_void) -> c_int;
    fn wave_recent_move(s: *mut c_void, delta: c_int);
    fn wave_recent_input(s: *mut c_void, text: *const c_char);
    fn wave_recent_backspace(s: *mut c_void);
    fn wave_recent_query(s: *const c_void) -> *const c_char;
    fn wave_recent_accept(s: *mut c_void) -> c_int;
    fn wave_recent_add(s: *mut c_void, path: *const c_char);
    fn wave_close_workspace(s: *mut c_void);

    // file watching
    fn wave_watch_poll(s: *mut c_void, now: f64, message: *mut c_char, cap: usize) -> c_int;
    fn wave_watch_workspace_start(s: *mut c_void);

    // paste / centre
    fn wave_paste(s: *mut c_void, text: *const c_char) -> c_int;

    // terminal selection
    fn wave_term_sel_begin(s: *mut c_void, row: usize, col: c_int);
    fn wave_term_sel_update(s: *mut c_void, row: usize, col: c_int);
    fn wave_term_sel_end(s: *mut c_void);
    fn wave_term_sel_clear(s: *mut c_void);
    fn wave_term_sel_span(s: *const c_void, row: usize, a: *mut c_int, b: *mut c_int) -> c_int;
    fn wave_term_copy_selection(s: *const c_void) -> *mut c_char;

    // git diff selection
    fn wave_git_sel_begin(s: *mut c_void, line: c_int, col: c_int);
    fn wave_git_sel_update(s: *mut c_void, line: c_int, col: c_int);
    fn wave_git_sel_end(s: *mut c_void);
    fn wave_git_sel_clear(s: *mut c_void);
    fn wave_git_sel_span(s: *const c_void, line: c_int, a: *mut c_int, b: *mut c_int) -> c_int;
    fn wave_git_copy_selection(s: *const c_void) -> *mut c_char;
    fn wave_git_diff_scroll_pos(s: *const c_void) -> c_int;

    // signature help
    fn wave_signature_request(s: *mut c_void, trigger: c_uint, retrigger: c_int) -> c_int;

    // soft wrap
    fn wave_wrap_set_cols(s: *mut c_void, cols: c_int);
    fn wave_visual_rows(s: *mut c_void) -> usize;
    fn wave_visual_row(s: *mut c_void, vrow: usize, out: *mut WaveVisualRowRaw) -> c_int;
    fn wave_cursor_visual(s: *mut c_void, vrow: *mut usize, col: *mut usize) -> c_int;

    // popover
    fn wave_popover_active(s: *const c_void) -> c_int;
    fn wave_popover_loading(s: *const c_void) -> c_int;
    fn wave_popover_text(s: *const c_void) -> *const c_char;
    fn wave_popover_close(s: *mut c_void);
    fn wave_popover_scroll_by(s: *mut c_void, delta: c_int);
    fn wave_popover_set_view(s: *mut c_void, total_rows: c_int, vis_rows: c_int);
}

#[repr(C)]
#[derive(Clone, Copy)]
struct WaveCellStyleRaw {
    start_byte: usize,
    end_byte: usize,
    fg: c_uint,
    bg: c_uint,
}

/// Sentinel for "use the default foreground/background".
pub const COLOR_DEFAULT: u32 = 0xFFFF_FFFF;

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct WaveVisualRowRaw {
    line: usize,
    start_byte: usize,
    end_byte: usize,
}

/// One rendered row: a byte slice of a logical line. With wrapping off there
/// is exactly one row per line.
#[derive(Clone, Copy)]
pub struct VisualRow {
    pub line: usize,
    pub start_byte: usize,
    pub end_byte: usize,
}

/// GLFW key codes, which is the vocabulary `terminal_key_sequence` speaks.
/// These are deliberately *not* [`Key`]/`EditorKey` values — the terminal and
/// the editor take different key encodings.
pub mod term_key {
    pub const ESCAPE: i32 = 256;
    pub const ENTER: i32 = 257;
    pub const TAB: i32 = 258;
    pub const BACKSPACE: i32 = 259;
    pub const INSERT: i32 = 260;
    pub const DELETE: i32 = 261;
    pub const RIGHT: i32 = 262;
    pub const LEFT: i32 = 263;
    pub const DOWN: i32 = 264;
    pub const UP: i32 = 265;
    pub const PAGE_UP: i32 = 266;
    pub const PAGE_DOWN: i32 = 267;
    pub const HOME: i32 = 268;
    pub const END: i32 = 269;
}

/// Mirrors `TabItemKind` in `src/tabs.h`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TabKind {
    Editor,
    Terminal,
    Git,
}

/// Mirrors `GitViewMode` in `src/git_view.h`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum GitMode {
    RepoSelect,
    Changes,
    CommitInput,
}

/// Mirrors `CommandCloseAction` in `src/command.h`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CloseAction {
    None,
    Tab,
    Window,
}

/// Terminal cell styling, in **byte** offsets into the line's UTF-8 text.
pub struct CellStyle {
    pub start_byte: usize,
    pub end_byte: usize,
    pub fg: u32,
    pub bg: u32,
}

pub struct CompletionItem {
    pub label: String,
    pub detail: String,
    pub kind: String,
}

pub struct SearchHit {
    pub path: String,
    pub line: i32,
    pub col: i32,
    pub text: String,
}

pub struct GitFile {
    pub code: String,
    pub path: String,
}

/// Mirrors `EditorKey` in `src/editor.h`.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Key {
    Backspace = 1,
    Delete = 2,
    Enter = 3,
    Tab = 4,
    Left = 5,
    Right = 6,
    Up = 7,
    Down = 8,
}

/// Mirrors `StdMotion` in `src/standard.h` — caret movement for standard
/// (non-vim) editing, where the shift key turns any of these into a selection.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(i32)]
pub enum Motion {
    Left = 1,
    Right = 2,
    Up = 3,
    Down = 4,
    WordLeft = 5,
    WordRight = 6,
    LineStart = 7,
    LineEnd = 8,
    DocStart = 9,
    DocEnd = 10,
}

/// Mirrors `Mode` in `src/mode.h`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    Normal,
    Insert,
    Visual,
}

/// Mirrors the `EditCommandFlags` bits the front-end acts on.
pub mod flags {
    pub const YANKED: u32 = 1 << 0;
    pub const TAB_NEXT: u32 = 1 << 3;
    pub const TAB_PREV: u32 = 1 << 4;
}

pub struct Span {
    pub start_col: usize,
    pub end_col: usize,
    pub name: &'static str,
}

pub struct Entry {
    pub rel: String,
    pub name: String,
    pub depth: i32,
    pub is_dir: bool,
    pub collapsed: bool,
}

pub struct Tab {
    pub label: String,
    pub modified: bool,
}

pub struct Session {
    raw: *mut c_void,
}

fn cstr(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

impl Session {
    pub fn new() -> Self {
        let raw = unsafe { wave_new() };
        assert!(!raw.is_null(), "wave_new() returned NULL");
        Session { raw }
    }

    /// Open a file or a folder, as `main.c` does.
    pub fn open(&mut self, path: &Path) -> Result<(), String> {
        let c = CString::new(path.as_os_str().as_encoded_bytes())
            .map_err(|_| format!("path contains a NUL byte: {}", path.display()))?;
        if unsafe { wave_open_path(self.raw, c.as_ptr()) } == 0 {
            Ok(())
        } else {
            Err(format!("could not open {}", path.display()))
        }
    }

    // ---- workspace ----

    pub fn has_workspace(&self) -> bool {
        unsafe { wave_has_workspace(self.raw) != 0 }
    }

    pub fn ws_root(&self) -> String {
        cstr(unsafe { wave_ws_root(self.raw) })
    }

    pub fn ws_count(&self) -> usize {
        unsafe { wave_ws_count(self.raw) }
    }

    pub fn ws_entry(&self, vi: usize) -> Option<Entry> {
        let mut raw = WaveEntryRaw {
            rel: std::ptr::null(),
            name: std::ptr::null(),
            depth: 0,
            is_dir: 0,
            collapsed: 0,
        };
        if unsafe { wave_ws_entry(self.raw, vi, &mut raw) } == 0 {
            return None;
        }
        Some(Entry {
            rel: cstr(raw.rel),
            name: cstr(raw.name),
            depth: raw.depth,
            is_dir: raw.is_dir != 0,
            collapsed: raw.collapsed != 0,
        })
    }

    /// Returns true if a file was opened into a tab.
    pub fn ws_activate(&mut self, row: usize, double_click: bool) -> bool {
        unsafe { wave_ws_activate(self.raw, row as c_int, double_click as c_int) != 0 }
    }

    // ---- Cmd-P palette ----

    pub fn palette_open(&mut self) -> bool {
        unsafe { wave_palette_open(self.raw) != 0 }
    }

    pub fn palette_close(&mut self) {
        unsafe { wave_palette_close(self.raw) }
    }

    pub fn palette_active(&self) -> bool {
        unsafe { wave_palette_active(self.raw) != 0 }
    }

    pub fn palette_query(&self) -> String {
        cstr(unsafe { wave_palette_query(self.raw) })
    }

    pub fn palette_count(&self) -> usize {
        unsafe { wave_palette_count(self.raw) }
    }

    pub fn palette_selected(&self) -> usize {
        unsafe { wave_palette_selected(self.raw).max(0) as usize }
    }

    pub fn palette_entry(&self, i: usize) -> Option<Entry> {
        let mut raw = WaveEntryRaw {
            rel: std::ptr::null(),
            name: std::ptr::null(),
            depth: 0,
            is_dir: 0,
            collapsed: 0,
        };
        if unsafe { wave_palette_entry(self.raw, i, &mut raw) } == 0 {
            return None;
        }
        Some(Entry {
            rel: cstr(raw.rel),
            name: cstr(raw.name),
            depth: raw.depth,
            is_dir: raw.is_dir != 0,
            collapsed: raw.collapsed != 0,
        })
    }

    pub fn palette_input(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_palette_input(self.raw, c.as_ptr()) }
    }

    pub fn palette_backspace(&mut self) {
        unsafe { wave_palette_backspace(self.raw) }
    }

    pub fn palette_move(&mut self, delta: i32) {
        unsafe { wave_palette_move(self.raw, delta as c_int) }
    }

    pub fn palette_accept(&mut self) -> bool {
        unsafe { wave_palette_accept(self.raw) != 0 }
    }

    // ---- tabs ----

    pub fn tab_count(&self) -> usize {
        unsafe { wave_tab_count(self.raw).max(0) as usize }
    }

    pub fn tab_active(&self) -> usize {
        unsafe { wave_tab_active(self.raw).max(0) as usize }
    }

    pub fn tab(&self, i: usize) -> Tab {
        let mut buf = vec![0u8; 128];
        let n =
            unsafe { wave_tab_label(self.raw, i as c_int, buf.as_mut_ptr() as *mut c_char, buf.len()) };
        buf.truncate(n);
        Tab {
            label: String::from_utf8_lossy(&buf).into_owned(),
            modified: unsafe { wave_tab_modified(self.raw, i as c_int) != 0 },
        }
    }

    pub fn tab_set_active(&mut self, i: usize) {
        unsafe { wave_tab_set_active(self.raw, i as c_int) }
    }

    pub fn tab_close(&mut self, i: usize) {
        unsafe { wave_tab_close(self.raw, i as c_int) }
    }

    pub fn tab_goto(&mut self, delta: i32) {
        unsafe { wave_tab_goto(self.raw, delta as c_int) }
    }

    // ---- current editor ----

    pub fn has_buffer(&self) -> bool {
        unsafe { wave_has_buffer(self.raw) != 0 }
    }

    pub fn path(&self) -> String {
        cstr(unsafe { wave_path(self.raw) })
    }

    pub fn line_count(&self) -> usize {
        unsafe { wave_line_count(self.raw) }
    }

    pub fn line_text(&self, line: usize) -> String {
        // Long lines are truncated rather than grown; the front-end only ever
        // asks for lines it is about to draw.
        let mut buf = vec![0u8; 4096];
        let n =
            unsafe { wave_line_text(self.raw, line, buf.as_mut_ptr() as *mut c_char, buf.len()) };
        buf.truncate(n);
        String::from_utf8_lossy(&buf).into_owned()
    }

    fn collect_spans(
        &mut self,
        line: usize,
        f: unsafe extern "C" fn(*mut c_void, usize, *mut WaveSpanRaw, usize) -> usize,
    ) -> Vec<Span> {
        let mut raw = vec![
            WaveSpanRaw {
                start_col: 0,
                end_col: 0,
                name: std::ptr::null(),
            };
            128
        ];
        let n = unsafe { f(self.raw, line, raw.as_mut_ptr(), raw.len()) };
        raw.truncate(n);
        raw.into_iter()
            .filter_map(|s| {
                // Capture names and diagnostic messages are static tables owned
                // by the compiled TSQuery / the C core, so 'static is honest.
                let name = if s.name.is_null() {
                    ""
                } else {
                    unsafe { CStr::from_ptr(s.name) }.to_str().ok()?
                };
                Some(Span {
                    start_col: s.start_col,
                    end_col: s.end_col,
                    name,
                })
            })
            .collect()
    }

    pub fn line_spans(&mut self, line: usize) -> Vec<Span> {
        self.collect_spans(line, wave_line_spans)
    }

    pub fn line_diagnostics(&mut self, line: usize) -> Vec<Span> {
        self.collect_spans(line, wave_line_diagnostics)
    }

    pub fn line_selection(&mut self, line: usize) -> Option<(usize, usize)> {
        let (mut a, mut b) = (0usize, 0usize);
        if unsafe { wave_line_selection(self.raw, line, &mut a, &mut b) } != 0 {
            Some((a, b))
        } else {
            None
        }
    }

    /// Every selection on `line` — the primary plus any extra carets sharing
    /// it. `line_selection` only ever reports the primary one.
    pub fn line_selections(&mut self, line: usize) -> Vec<(usize, usize)> {
        const MAX: usize = 64;
        let mut starts = [0usize; MAX];
        let mut ends = [0usize; MAX];
        let n = unsafe {
            wave_line_selections(self.raw, line, starts.as_mut_ptr(), ends.as_mut_ptr(), MAX)
        };
        (0..n).map(|i| (starts[i], ends[i])).collect()
    }

    /// Visual (wrapped) position of extra caret `i`.
    pub fn caret_visual(&mut self, i: usize) -> Option<(usize, usize)> {
        let (mut r, mut c) = (0usize, 0usize);
        if unsafe { wave_caret_visual(self.raw, i, &mut r, &mut c) } != 0 {
            Some((r, c))
        } else {
            None
        }
    }

    /// Place the cursor at a buffer position, collapsing any selection.
    pub fn click_at(&mut self, line: usize, col: usize) {
        unsafe { wave_click_at(self.raw, line as c_int, col as c_int) }
    }

    /// Extend a selection to a buffer position, holding the anchor.
    pub fn drag_to(&mut self, line: usize, col: usize) {
        unsafe { wave_drag_to(self.raw, line as c_int, col as c_int) }
    }

    pub fn has_selection(&self) -> bool {
        unsafe { wave_has_selection(self.raw) != 0 }
    }

    pub fn selection_text(&mut self) -> Option<String> {
        let p = unsafe { wave_selection_text(self.raw) };
        if p.is_null() {
            return None;
        }
        let text = cstr(p);
        unsafe { wave_string_free(p) };
        Some(text)
    }

    pub fn cursor(&self) -> (usize, usize) {
        let (mut row, mut col) = (0usize, 0usize);
        unsafe { wave_cursor(self.raw, &mut row, &mut col) };
        (row, col)
    }

    pub fn mode(&self) -> Mode {
        match unsafe { wave_mode(self.raw) } {
            1 => Mode::Insert,
            2 => Mode::Visual,
            _ => Mode::Normal,
        }
    }

    pub fn mode_name(&self) -> String {
        cstr(unsafe { wave_mode_name(self.raw) })
    }

    pub fn modified(&self) -> bool {
        unsafe { wave_modified(self.raw) != 0 }
    }

    // ---- language server ----

    /// Full text of the diagnostic under the cursor, if any.
    pub fn cursor_diagnostic(&mut self) -> String {
        let mut buf = vec![0u8; 512];
        let n = unsafe {
            wave_cursor_diagnostic(self.raw, buf.as_mut_ptr() as *mut c_char, buf.len())
        };
        buf.truncate(n);
        String::from_utf8_lossy(&buf).into_owned()
    }

    pub fn lsp_active(&self) -> bool {
        unsafe { wave_lsp_active(self.raw) != 0 }
    }

    /// Drain async server replies. True if the UI should repaint.
    pub fn lsp_poll(&mut self) -> bool {
        unsafe { wave_lsp_poll(self.raw) != 0 }
    }

    pub fn hover(&self) -> String {
        cstr(unsafe { wave_hover(self.raw) })
    }

    pub fn hover_clear(&mut self) {
        unsafe { wave_hover_clear(self.raw) }
    }

    pub fn text_input(&mut self, cp: char) -> u32 {
        unsafe { wave_text_input(self.raw, cp as c_uint) }
    }

    pub fn special_key(&mut self, key: Key) -> bool {
        unsafe { wave_special_key(self.raw, key as c_int) != 0 }
    }

    /// Move the caret in standard editing. `extend` is the shift key.
    pub fn motion(&mut self, motion: Motion, extend: bool) -> bool {
        unsafe { wave_motion(self.raw, motion as c_int, extend as c_int) != 0 }
    }

    pub fn select_all(&mut self) -> bool {
        unsafe { wave_select_all(self.raw) != 0 }
    }

    /// Select the word under the caret (double-click).
    pub fn select_word(&mut self) -> bool {
        unsafe { wave_select_word(self.raw) != 0 }
    }

    /// ⌘C in standard editing: the selection, or the whole line when there is
    /// none. `None` when there is nothing to copy.
    pub fn standard_copy(&mut self) -> Option<String> {
        let p = unsafe { wave_standard_copy(self.raw) };
        if p.is_null() {
            return None;
        }
        let text = cstr(p);
        unsafe { wave_string_free(p) };
        Some(text)
    }

    /// ⌘X: same text as `standard_copy`, and removes it.
    pub fn standard_cut(&mut self) -> Option<String> {
        let p = unsafe { wave_standard_cut(self.raw) };
        if p.is_null() {
            return None;
        }
        let text = cstr(p);
        unsafe { wave_string_free(p) };
        Some(text)
    }

    pub fn delete_line(&mut self) -> bool {
        unsafe { wave_delete_line(self.raw) != 0 }
    }

    pub fn duplicate_line(&mut self) -> bool {
        unsafe { wave_duplicate_line(self.raw) != 0 }
    }

    /// Move the selected lines up (`-1`) or down (`+1`).
    pub fn move_line(&mut self, dir: i32) -> bool {
        unsafe { wave_move_line(self.raw, dir as c_int) != 0 }
    }

    pub fn delete_to_line_start(&mut self) -> bool {
        unsafe { wave_delete_to_line_start(self.raw) != 0 }
    }

    pub fn toggle_comment(&mut self) -> bool {
        unsafe { wave_toggle_comment(self.raw) != 0 }
    }

    /// Go to the definition under the caret (the `gd` path), for ⌘/⌃-click.
    /// Records a jump, so `jump_go(-1)` comes back.
    pub fn goto_definition(&mut self) {
        unsafe { wave_goto_definition(self.raw) }
    }

    /// Tab / ⇧Tab block indent. False when it was not a block operation and the
    /// caller should insert a plain tab instead.
    pub fn indent(&mut self, outdent: bool) -> bool {
        unsafe { wave_indent(self.raw, outdent as c_int) != 0 }
    }

    pub fn delete_word_left(&mut self) -> bool {
        unsafe { wave_delete_word_left(self.raw) != 0 }
    }

    pub fn delete_word_right(&mut self) -> bool {
        unsafe { wave_delete_word_right(self.raw) != 0 }
    }

    pub fn delete_to_line_end(&mut self) -> bool {
        unsafe { wave_delete_to_line_end(self.raw) != 0 }
    }

    pub fn select_line(&mut self) -> bool {
        unsafe { wave_select_line(self.raw) != 0 }
    }

    pub fn insert_line(&mut self, below: bool) -> bool {
        unsafe { wave_insert_line(self.raw, below as c_int) != 0 }
    }

    /// ⌘D — select the word, then each next occurrence, adding a caret.
    pub fn select_next_occurrence(&mut self) -> bool {
        unsafe { wave_select_next_occurrence(self.raw) != 0 }
    }

    /// ⌥-click — add a caret at a screen position.
    pub fn add_caret_at(&mut self, line: usize, col: usize) -> bool {
        unsafe { wave_add_caret_at(self.raw, line as c_int, col as c_int) != 0 }
    }

    /// Number of *extra* carets, beyond the primary one.
    pub fn caret_count(&self) -> usize {
        unsafe { wave_caret_count(self.raw) }
    }

    pub fn clear_carets(&mut self) {
        unsafe { wave_clear_carets(self.raw) }
    }

    /// Extra caret `i` as `(anchor, cursor)` byte offsets, for painting.
    pub fn caret_at(&self, i: usize) -> Option<(usize, usize)> {
        let (mut a, mut c) = (0usize, 0usize);
        if unsafe { wave_caret_at(self.raw, i, &mut a, &mut c) } != 0 {
            Some((a, c))
        } else {
            None
        }
    }

    /// Record the caret's position, so a later `jump_go(-1)` returns here.
    pub fn jump_push(&mut self) {
        unsafe { wave_jump_push(self.raw) }
    }

    /// Walk the jump list: `-1` back, `+1` forward. False if there was nowhere
    /// to go.
    pub fn jump_go(&mut self, dir: i32) -> bool {
        unsafe { wave_jump_go(self.raw, dir as c_int) != 0 }
    }

    /// Whether modal (vim) editing is on. False selects standard editing.
    pub fn vim_enabled(&self) -> bool {
        unsafe { wave_vim_enabled(self.raw) != 0 }
    }

    pub fn set_vim_enabled(&mut self, on: bool) -> bool {
        unsafe { wave_set_vim_enabled(self.raw, on as c_int) != 0 }
    }

    pub fn toggle_vim(&mut self) -> bool {
        unsafe { wave_toggle_vim(self.raw) != 0 }
    }

    pub fn escape(&mut self) {
        unsafe { wave_escape(self.raw) }
    }

    pub fn undo(&mut self) -> bool {
        unsafe { wave_undo(self.raw) != 0 }
    }

    pub fn redo(&mut self) -> bool {
        unsafe { wave_redo(self.raw) != 0 }
    }

    pub fn save(&mut self) -> bool {
        unsafe { wave_save(self.raw) == 0 }
    }

    pub fn goto_line(&mut self, line: usize, column: usize) {
        unsafe { wave_goto_line(self.raw, line as c_int, column as c_int) }
    }
}

impl Session {
    // ---- terminal ----

    pub fn tab_kind(&self, i: usize) -> TabKind {
        match unsafe { wave_tab_kind(self.raw, i as c_int) } {
            1 => TabKind::Terminal,
            2 => TabKind::Git,
            _ => TabKind::Editor,
        }
    }

    pub fn term_open(&mut self, label: &str, cmd: &str) -> bool {
        let (Ok(l), Ok(c)) = (CString::new(label), CString::new(cmd)) else {
            return false;
        };
        unsafe { wave_term_open(self.raw, l.as_ptr(), c.as_ptr()) != 0 }
    }

    pub fn term_active(&self) -> bool {
        unsafe { wave_term_active(self.raw) != 0 }
    }

    pub fn term_poll(&mut self) -> bool {
        unsafe { wave_term_poll(self.raw) != 0 }
    }

    pub fn term_resize(&mut self, rows: usize, cols: usize) {
        unsafe { wave_term_resize(self.raw, rows as c_int, cols as c_int) }
    }

    pub fn term_visible_start(&self, rows: usize) -> usize {
        unsafe { wave_term_visible_start(self.raw, rows as c_int) }
    }

    pub fn term_line(&self, index: usize) -> String {
        let mut buf = vec![0u8; 4096];
        let n =
            unsafe { wave_term_line(self.raw, index, buf.as_mut_ptr() as *mut c_char, buf.len()) };
        buf.truncate(n);
        String::from_utf8_lossy(&buf).into_owned()
    }

    /// Style runs for a terminal line. Called for every visible line on every
    /// frame, so the staging buffer is on the stack — a heap allocation per
    /// line per frame is pure churn, and `terminal.c` coalesces adjacent cells
    /// with the same colours so `n` is a handful of runs, not one per column.
    pub fn term_line_styles(&self, index: usize) -> Vec<CellStyle> {
        const CAP: usize = 256;
        let mut raw = [WaveCellStyleRaw {
            start_byte: 0,
            end_byte: 0,
            fg: COLOR_DEFAULT,
            bg: COLOR_DEFAULT,
        }; CAP];
        let n = unsafe { wave_term_line_styles(self.raw, index, raw.as_mut_ptr(), CAP) };
        raw[..n]
            .iter()
            .map(|c| CellStyle {
                start_byte: c.start_byte,
                end_byte: c.end_byte,
                fg: c.fg,
                bg: c.bg,
            })
            .collect()
    }

    /// Byte offset of a display column on a terminal line.
    pub fn term_col_to_byte(&self, index: usize, col: usize) -> usize {
        unsafe { wave_term_col_to_byte(self.raw, index, col) }
    }

    pub fn term_cursor(&self) -> (usize, usize, bool) {
        let (mut r, mut c, mut v) = (0, 0, 0);
        unsafe { wave_term_cursor(self.raw, &mut r, &mut c, &mut v) };
        (r.max(0) as usize, c.max(0) as usize, v != 0)
    }

    pub fn term_rows(&self) -> usize {
        unsafe { wave_term_rows(self.raw).max(0) as usize }
    }

    pub fn term_running(&self) -> bool {
        unsafe { wave_term_running(self.raw) != 0 }
    }

    pub fn term_status(&self) -> String {
        cstr(unsafe { wave_term_status(self.raw) })
    }

    pub fn term_write(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_term_write(self.raw, c.as_ptr()) }
    }

    pub fn term_total_lines(&self) -> usize {
        unsafe { wave_term_total_lines(self.raw) }
    }

    /// `key` must be a [`TermKey`] code or an ASCII `'A'..='Z'` for a control
    /// chord — `terminal_key_sequence` switches on GLFW key codes.
    pub fn term_key(&mut self, key: i32, shift: bool, alt: bool, ctrl: bool) {
        unsafe {
            wave_term_key(
                self.raw,
                key as c_int,
                shift as c_int,
                alt as c_int,
                ctrl as c_int,
            )
        }
    }

    pub fn term_scroll(&mut self, units: i32) {
        unsafe { wave_term_scroll(self.raw, units as c_int) }
    }

    // ---- git ----

    pub fn git_open(&mut self) -> bool {
        unsafe { wave_git_open(self.raw) != 0 }
    }

    pub fn git_active(&self) -> bool {
        unsafe { wave_git_active(self.raw) != 0 }
    }

    pub fn git_mode(&self) -> GitMode {
        match unsafe { wave_git_mode(self.raw) } {
            1 => GitMode::Changes,
            2 => GitMode::CommitInput,
            _ => GitMode::RepoSelect,
        }
    }

    pub fn git_repos(&self) -> Vec<String> {
        let n = unsafe { wave_git_repo_count(self.raw) }.max(0);
        (0..n)
            .map(|i| cstr(unsafe { wave_git_repo_label(self.raw, i) }))
            .collect()
    }

    pub fn git_selected_repo(&self) -> usize {
        unsafe { wave_git_selected_repo(self.raw).max(0) as usize }
    }

    pub fn git_files(&self) -> Vec<GitFile> {
        let n = unsafe { wave_git_file_count(self.raw) }.max(0);
        (0..n)
            .filter_map(|i| {
                let mut code = std::ptr::null();
                let mut path = std::ptr::null();
                if unsafe { wave_git_file(self.raw, i, &mut code, &mut path) } == 0 {
                    return None;
                }
                Some(GitFile {
                    code: cstr(code),
                    path: cstr(path),
                })
            })
            .collect()
    }

    pub fn git_selected_file(&self) -> usize {
        unsafe { wave_git_selected_file(self.raw).max(0) as usize }
    }

    pub fn git_diff(&self) -> Vec<String> {
        let n = unsafe { wave_git_diff_count(self.raw) }.max(0);
        (0..n)
            .map(|i| cstr(unsafe { wave_git_diff_line(self.raw, i) }))
            .collect()
    }

    pub fn git_message(&self) -> String {
        cstr(unsafe { wave_git_message(self.raw) })
    }

    pub fn git_info(&self) -> String {
        cstr(unsafe { wave_git_info(self.raw) })
    }

    pub fn git_move(&mut self, delta: i32) {
        unsafe { wave_git_move(self.raw, delta as c_int) }
    }

    pub fn git_accept(&mut self) -> bool {
        unsafe { wave_git_accept(self.raw) != 0 }
    }

    pub fn git_stage_toggle(&mut self) -> bool {
        unsafe { wave_git_stage_toggle(self.raw) != 0 }
    }

    pub fn git_begin_commit(&mut self) -> bool {
        unsafe { wave_git_begin_commit(self.raw) != 0 }
    }

    pub fn git_commit(&mut self) -> bool {
        unsafe { wave_git_commit(self.raw) != 0 }
    }

    pub fn git_cancel_input(&mut self) {
        unsafe { wave_git_cancel_input(self.raw) }
    }

    pub fn git_insert_text(&mut self, text: &str) -> bool {
        let Ok(c) = CString::new(text) else {
            return false;
        };
        unsafe { wave_git_insert_text(self.raw, c.as_ptr()) != 0 }
    }

    pub fn git_backspace(&mut self) -> bool {
        unsafe { wave_git_backspace(self.raw) != 0 }
    }

    pub fn git_refresh(&mut self) -> bool {
        unsafe { wave_git_refresh(self.raw) != 0 }
    }

    pub fn git_diff_scroll(&mut self, delta: i32) {
        unsafe { wave_git_diff_scroll(self.raw, delta as c_int) }
    }

    // ---- completion ----

    pub fn complete_active(&self) -> bool {
        unsafe { wave_complete_active(self.raw) != 0 }
    }

    pub fn complete_loading(&self) -> bool {
        unsafe { wave_complete_loading(self.raw) != 0 }
    }

    pub fn complete_count(&self) -> usize {
        unsafe { wave_complete_count(self.raw).max(0) as usize }
    }

    pub fn complete_selected(&self) -> usize {
        unsafe { wave_complete_selected(self.raw).max(0) as usize }
    }

    pub fn complete_item(&self, i: usize) -> Option<CompletionItem> {
        let mut label = std::ptr::null();
        let mut detail = std::ptr::null();
        let mut kind = std::ptr::null();
        if unsafe { wave_complete_item(self.raw, i as c_int, &mut label, &mut detail, &mut kind) }
            == 0
        {
            return None;
        }
        Some(CompletionItem {
            label: cstr(label),
            detail: cstr(detail),
            kind: cstr(kind),
        })
    }

    pub fn complete_move(&mut self, delta: i32) {
        unsafe { wave_complete_move(self.raw, delta as c_int) }
    }

    pub fn complete_close(&mut self) {
        unsafe { wave_complete_close(self.raw) }
    }

    pub fn complete_accept(&mut self) -> bool {
        unsafe { wave_complete_accept(self.raw) != 0 }
    }

    // ---- project search ----

    pub fn search_open(&mut self) -> bool {
        unsafe { wave_search_open(self.raw) != 0 }
    }

    pub fn search_active(&self) -> bool {
        unsafe { wave_search_active(self.raw) != 0 }
    }

    pub fn search_query(&self) -> String {
        cstr(unsafe { wave_search_query(self.raw) })
    }

    pub fn search_input(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_search_input(self.raw, c.as_ptr()) }
    }

    pub fn search_backspace(&mut self) {
        unsafe { wave_search_backspace(self.raw) }
    }

    pub fn search_move(&mut self, delta: i32) {
        unsafe { wave_search_move(self.raw, delta as c_int) }
    }

    pub fn search_poll(&mut self) -> bool {
        unsafe { wave_search_poll(self.raw) != 0 }
    }

    pub fn search_running(&self) -> bool {
        unsafe { wave_search_running(self.raw) != 0 }
    }

    pub fn search_count(&self) -> usize {
        unsafe { wave_search_count(self.raw) }
    }

    pub fn search_selected(&self) -> usize {
        unsafe { wave_search_selected(self.raw).max(0) as usize }
    }

    pub fn search_hit(&self, i: usize) -> Option<SearchHit> {
        let mut path = std::ptr::null();
        let mut text = std::ptr::null();
        let (mut line, mut col) = (0, 0);
        if unsafe { wave_search_hit(self.raw, i, &mut path, &mut line, &mut col, &mut text) } == 0 {
            return None;
        }
        Some(SearchHit {
            path: cstr(path),
            line,
            col,
            text: cstr(text),
        })
    }

    pub fn search_accept(&mut self) -> bool {
        unsafe { wave_search_accept(self.raw) != 0 }
    }

    // ---- command line ----

    pub fn cmd_active(&self) -> bool {
        unsafe { wave_cmd_active(self.raw) != 0 }
    }

    pub fn cmd_text(&self) -> String {
        cstr(unsafe { wave_cmd_text(self.raw) })
    }

    pub fn cmd_open(&mut self) {
        unsafe { wave_cmd_open(self.raw) }
    }

    pub fn cmd_close(&mut self) {
        unsafe { wave_cmd_close(self.raw) }
    }

    pub fn cmd_input(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_cmd_input(self.raw, c.as_ptr()) }
    }

    pub fn cmd_backspace(&mut self) {
        unsafe { wave_cmd_backspace(self.raw) }
    }

    pub fn cmd_accept(&mut self) -> CloseAction {
        match unsafe { wave_cmd_accept(self.raw) } {
            1 => CloseAction::Tab,
            2 => CloseAction::Window,
            _ => CloseAction::None,
        }
    }

    pub fn info(&self) -> String {
        cstr(unsafe { wave_info(self.raw) })
    }

    pub fn info_clear(&mut self) {
        unsafe { wave_info_clear(self.raw) }
    }

    pub fn opacity_pct(&self) -> u32 {
        unsafe { wave_cfg_opacity_pct(self.raw).clamp(0, 100) as u32 }
    }

    pub fn native_titlebar(&self) -> bool {
        unsafe { wave_cfg_native_titlebar(self.raw) != 0 }
    }

    pub fn blur(&self) -> bool {
        unsafe { wave_cfg_blur(self.raw) != 0 }
    }

    pub fn radius(&self) -> f32 {
        unsafe { wave_cfg_radius(self.raw) }
    }

    /// Base font size in points, driven by `:set` and the zoom shortcuts.
    pub fn base_pt(&self) -> f32 {
        unsafe { wave_cfg_base_pt(self.raw) }
    }

    pub fn show_sidebar(&self) -> bool {
        unsafe { wave_cfg_show_sidebar(self.raw) != 0 }
    }

    pub fn wrap(&self) -> bool {
        unsafe { wave_cfg_wrap(self.raw) != 0 }
    }

    pub fn toggle_sidebar(&mut self) -> bool {
        unsafe { wave_cfg_toggle_sidebar(self.raw) != 0 }
    }

    pub fn toggle_wrap(&mut self) -> bool {
        unsafe { wave_cfg_toggle_wrap(self.raw) != 0 }
    }

    /// `dir`: +1 larger, -1 smaller, 0 reset.
    pub fn zoom(&mut self, dir: i32) -> bool {
        unsafe { wave_cfg_zoom(self.raw, dir as c_int) != 0 }
    }

    pub fn save_config(&mut self) -> bool {
        unsafe { wave_cfg_save(self.raw) != 0 }
    }

    /// Reset every setting to `config.c`'s defaults and persist.
    pub fn reset_config(&mut self) -> bool {
        unsafe { wave_cfg_defaults(self.raw) != 0 }
    }

    pub fn set_opacity(&mut self, v: f32) {
        unsafe { wave_cfg_set_opacity(self.raw, v) }
    }

    pub fn set_radius(&mut self, v: f32) {
        unsafe { wave_cfg_set_radius(self.raw, v) }
    }

    pub fn set_base_pt(&mut self, v: f32) {
        unsafe { wave_cfg_set_base_pt(self.raw, v) }
    }

    pub fn set_side_cells(&mut self, v: usize) {
        unsafe { wave_cfg_set_side_cells(self.raw, v as c_int) }
    }

    pub fn side_cells(&self) -> usize {
        unsafe { wave_cfg_side_cells(self.raw).max(0) as usize }
    }

    pub fn toggle_blur(&mut self) -> bool {
        unsafe { wave_cfg_toggle_blur(self.raw) != 0 }
    }

    pub fn toggle_titlebar(&mut self) -> bool {
        unsafe { wave_cfg_toggle_titlebar(self.raw) != 0 }
    }

    /// `ui_scale` as a percentage; the effective size is base_pt x this.
    pub fn scale_pct(&self) -> u32 {
        unsafe { wave_cfg_scale_pct(self.raw).max(0) as u32 }
    }

    // ---- buffer search ----

    pub fn bufsearch_active(&self) -> bool {
        unsafe { wave_bufsearch_active(self.raw) != 0 }
    }

    /// Open find-in-file without vim's `/`, for ⌘F.
    pub fn buffer_search_open(&mut self) {
        unsafe { wave_bufsearch_open(self.raw) }
    }

    /// ⌘G / ⇧⌘G — jump to the next (or previous) match of the last search.
    pub fn bufsearch_repeat(&mut self, reverse: bool) {
        unsafe { wave_bufsearch_repeat(self.raw, reverse as c_int) }
    }

    pub fn bufsearch_text(&self) -> String {
        cstr(unsafe { wave_bufsearch_text(self.raw) })
    }

    pub fn bufsearch_input(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_bufsearch_input(self.raw, c.as_ptr()) }
    }

    pub fn bufsearch_backspace(&mut self) {
        unsafe { wave_bufsearch_backspace(self.raw) }
    }

    pub fn bufsearch_cancel(&mut self) {
        unsafe { wave_bufsearch_cancel(self.raw) }
    }

    pub fn bufsearch_accept(&mut self) {
        unsafe { wave_bufsearch_accept(self.raw) }
    }

    pub fn line_matches(&mut self, line: usize) -> Vec<Span> {
        self.collect_spans(line, wave_line_matches)
    }

    pub fn yank_text(&self) -> String {
        cstr(unsafe { wave_yank_text(self.raw) })
    }

    // ---- sidebar file operations ----

    pub fn ws_create(&mut self, dir_rel: &str, name: &str, is_dir: bool) -> (bool, String) {
        let (Ok(d), Ok(n)) = (CString::new(dir_rel), CString::new(name)) else {
            return (false, "invalid name".into());
        };
        let mut msg = vec![0u8; 256];
        let ok = unsafe {
            wave_ws_create(
                self.raw,
                d.as_ptr(),
                n.as_ptr(),
                is_dir as c_int,
                msg.as_mut_ptr() as *mut c_char,
                msg.len(),
            )
        };
        (ok != 0, cstr(msg.as_ptr() as *const c_char))
    }

    pub fn ws_delete(&mut self, rel: &str) -> (bool, String) {
        let Ok(r) = CString::new(rel) else {
            return (false, "invalid path".into());
        };
        let mut msg = vec![0u8; 256];
        let ok = unsafe {
            wave_ws_delete(
                self.raw,
                r.as_ptr(),
                msg.as_mut_ptr() as *mut c_char,
                msg.len(),
            )
        };
        (ok != 0, cstr(msg.as_ptr() as *const c_char))
    }

    pub fn ws_parent_dir(&self, vi: usize) -> String {
        let mut buf = vec![0u8; 4096];
        if unsafe { wave_ws_parent_dir(self.raw, vi, buf.as_mut_ptr() as *mut c_char, buf.len()) }
            == 0
        {
            return String::new();
        }
        cstr(buf.as_ptr() as *const c_char)
    }

    // ---- recent projects ----

    pub fn recent_count(&self) -> usize {
        unsafe { wave_recent_count(self.raw) }
    }

    pub fn recent_path(&self, i: usize) -> String {
        cstr(unsafe { wave_recent_path(self.raw, i) })
    }

    pub fn recent_selected(&self) -> usize {
        unsafe { wave_recent_selected(self.raw).max(0) as usize }
    }

    pub fn recent_move(&mut self, delta: i32) {
        unsafe { wave_recent_move(self.raw, delta as c_int) }
    }

    pub fn recent_input(&mut self, text: &str) {
        let Ok(c) = CString::new(text) else { return };
        unsafe { wave_recent_input(self.raw, c.as_ptr()) }
    }

    pub fn recent_backspace(&mut self) {
        unsafe { wave_recent_backspace(self.raw) }
    }

    pub fn recent_query(&self) -> String {
        cstr(unsafe { wave_recent_query(self.raw) })
    }

    pub fn recent_accept(&mut self) -> bool {
        unsafe { wave_recent_accept(self.raw) != 0 }
    }

    pub fn recent_add(&mut self, path: &str) {
        let Ok(c) = CString::new(path) else { return };
        unsafe { wave_recent_add(self.raw, c.as_ptr()) }
    }

    /// Close the workspace and all tabs, back to the empty state.
    pub fn close_workspace(&mut self) {
        unsafe { wave_close_workspace(self.raw) }
    }

    // ---- file watching ----

    pub fn watch_poll(&mut self, now: f64) -> Option<String> {
        let mut msg = vec![0u8; 256];
        let changed = unsafe {
            wave_watch_poll(self.raw, now, msg.as_mut_ptr() as *mut c_char, msg.len())
        };
        if changed == 0 {
            return None;
        }
        Some(cstr(msg.as_ptr() as *const c_char))
    }

    pub fn watch_workspace_start(&mut self) {
        unsafe { wave_watch_workspace_start(self.raw) }
    }

    // ---- paste / centre ----

    /// Paste at the cursor, replacing a live selection. True if the editor
    /// switched to insert mode as a result.
    pub fn paste(&mut self, text: &str) -> bool {
        let Ok(c) = CString::new(text) else {
            return false;
        };
        unsafe { wave_paste(self.raw, c.as_ptr()) != 0 }
    }

    // ---- terminal selection ----

    pub fn term_sel_begin(&mut self, row: usize, col: usize) {
        unsafe { wave_term_sel_begin(self.raw, row, col as c_int) }
    }

    pub fn term_sel_update(&mut self, row: usize, col: usize) {
        unsafe { wave_term_sel_update(self.raw, row, col as c_int) }
    }

    pub fn term_sel_end(&mut self) {
        unsafe { wave_term_sel_end(self.raw) }
    }

    pub fn term_sel_clear(&mut self) {
        unsafe { wave_term_sel_clear(self.raw) }
    }

    pub fn term_sel_span(&self, row: usize) -> Option<(usize, usize)> {
        let (mut a, mut b) = (0, 0);
        if unsafe { wave_term_sel_span(self.raw, row, &mut a, &mut b) } == 0 {
            return None;
        }
        Some((a.max(0) as usize, b.max(0) as usize))
    }

    pub fn term_copy_selection(&self) -> Option<String> {
        let p = unsafe { wave_term_copy_selection(self.raw) };
        if p.is_null() {
            return None;
        }
        let text = cstr(p);
        unsafe { wave_string_free(p) };
        Some(text)
    }

    // ---- git diff selection ----

    pub fn git_sel_begin(&mut self, line: usize, col: usize) {
        unsafe { wave_git_sel_begin(self.raw, line as c_int, col as c_int) }
    }

    pub fn git_sel_update(&mut self, line: usize, col: usize) {
        unsafe { wave_git_sel_update(self.raw, line as c_int, col as c_int) }
    }

    pub fn git_sel_end(&mut self) {
        unsafe { wave_git_sel_end(self.raw) }
    }

    pub fn git_sel_clear(&mut self) {
        unsafe { wave_git_sel_clear(self.raw) }
    }

    pub fn git_sel_span(&self, line: usize) -> Option<(usize, usize)> {
        let (mut a, mut b) = (0, 0);
        if unsafe { wave_git_sel_span(self.raw, line as c_int, &mut a, &mut b) } == 0 {
            return None;
        }
        Some((a.max(0) as usize, b.max(0) as usize))
    }

    pub fn git_copy_selection(&self) -> Option<String> {
        let p = unsafe { wave_git_copy_selection(self.raw) };
        if p.is_null() {
            return None;
        }
        let text = cstr(p);
        unsafe { wave_string_free(p) };
        Some(text)
    }

    pub fn git_diff_scroll_pos(&self) -> usize {
        unsafe { wave_git_diff_scroll_pos(self.raw).max(0) as usize }
    }

    // ---- signature help ----

    pub fn signature_request(&mut self, trigger: char, retrigger: bool) -> bool {
        unsafe { wave_signature_request(self.raw, trigger as c_uint, retrigger as c_int) != 0 }
    }

    // ---- soft wrap ----

    /// Rebuild the wrap index for a viewport `cols` wide. Honours the config
    /// flag; pass 0 to disable wrapping outright.
    pub fn wrap_set_cols(&mut self, cols: usize) {
        unsafe { wave_wrap_set_cols(self.raw, cols as c_int) }
    }

    pub fn visual_rows(&mut self) -> usize {
        unsafe { wave_visual_rows(self.raw) }
    }

    pub fn visual_row(&mut self, vrow: usize) -> Option<VisualRow> {
        let mut raw = WaveVisualRowRaw::default();
        if unsafe { wave_visual_row(self.raw, vrow, &mut raw) } == 0 {
            return None;
        }
        Some(VisualRow {
            line: raw.line,
            start_byte: raw.start_byte,
            end_byte: raw.end_byte,
        })
    }

    /// Visual row containing the cursor, and its column within that row.
    pub fn cursor_visual(&mut self) -> Option<(usize, usize)> {
        let (mut vrow, mut col) = (0usize, 0usize);
        if unsafe { wave_cursor_visual(self.raw, &mut vrow, &mut col) } == 0 {
            return None;
        }
        Some((vrow, col))
    }

    // ---- popover ----

    pub fn popover_active(&self) -> bool {
        unsafe { wave_popover_active(self.raw) != 0 }
    }

    pub fn popover_loading(&self) -> bool {
        unsafe { wave_popover_loading(self.raw) != 0 }
    }

    pub fn popover_text(&self) -> String {
        cstr(unsafe { wave_popover_text(self.raw) })
    }

    pub fn popover_close(&mut self) {
        unsafe { wave_popover_close(self.raw) }
    }

    pub fn popover_scroll_by(&mut self, delta: i32) {
        unsafe { wave_popover_scroll_by(self.raw, delta as c_int) }
    }

    pub fn popover_set_view(&mut self, total_rows: usize, vis_rows: usize) {
        unsafe { wave_popover_set_view(self.raw, total_rows as c_int, vis_rows as c_int) }
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        unsafe { wave_free(self.raw) }
    }
}

/// What the command line asked for: `wave [--line N] [--column N] [path]`.
pub struct OpenRequest {
    pub path: Option<String>,
    /// 1-based, and only meaningful when `has_location` is set.
    pub line: usize,
    pub column: usize,
    pub has_location: bool,
}

/// Parse argv through the C core's parser — the one `main.c` uses — so the CLI
/// contract is identical whichever front-end the `.app` bundles. `args` must
/// include the program name, since the parser skips it.
///
/// `None` means the command line was unusable (an unknown flag, a second path,
/// `--line` without a number); the caller prints usage and exits non-zero.
pub fn parse_open_request(args: &[String]) -> Option<OpenRequest> {
    let owned: Vec<CString> = args
        .iter()
        .map(|a| CString::new(a.as_str()).unwrap_or_default())
        .collect();
    let argv: Vec<*const c_char> = owned.iter().map(|a| a.as_ptr()).collect();

    let mut path: *const c_char = std::ptr::null();
    let mut line: c_int = 1;
    let mut column: c_int = 1;
    // `path` borrows from `owned`, so it is copied out before this returns.
    let rc = unsafe {
        wave_cli_open_request(
            argv.len() as c_int,
            argv.as_ptr(),
            &mut path,
            &mut line,
            &mut column,
        )
    };
    if rc == 0 {
        return None;
    }
    Some(OpenRequest {
        path: (!path.is_null())
            .then(|| unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned()),
        line: line.max(1) as usize,
        column: column.max(1) as usize,
        has_location: rc == 2,
    })
}

/// A tree-sitter capture's color in the active theme, packed `0xRRGGBB`.
pub fn theme_rgb(name: &str) -> u32 {
    let Ok(c) = CString::new(name) else {
        return 0xd9dbe0;
    };
    unsafe { wave_theme_rgb(c.as_ptr()) }
}

/// A chrome color in the active theme (`"bg"`, `"selection"`, …), packed
/// `0xRRGGBB`. Slot names are listed in `src/theme.h`.
pub fn theme_ui(slot: &str) -> u32 {
    let Ok(c) = CString::new(slot) else {
        return 0xd9dbe0;
    };
    unsafe { wave_theme_ui(c.as_ptr()) }
}

/// The built-in themes as `(name, label)`, in menu order.
pub fn themes() -> Vec<(String, String)> {
    (0..unsafe { wave_theme_count() })
        .map(|i| unsafe {
            (
                CStr::from_ptr(wave_theme_name(i)).to_string_lossy().into_owned(),
                CStr::from_ptr(wave_theme_label(i)).to_string_lossy().into_owned(),
            )
        })
        .collect()
}

/// Repaint in `name` without recording the choice — what the theme picker does
/// as the selection moves. Call [`Session::theme_set`] to keep it.
pub fn theme_preview(name: &str) -> bool {
    let Ok(c) = CString::new(name) else {
        return false;
    };
    unsafe { wave_theme_preview(c.as_ptr()) != 0 }
}

/// What the auto-updater is doing, as reported by [`update_poll`].
#[derive(Clone, Debug, PartialEq)]
pub enum Update {
    Checking,
    UpToDate { version: String },
    /// `from` is the version being replaced.
    Available { version: String, from: String },
    Downloading { version: String, progress: f64 },
    /// The installer is running; the app is about to quit and relaunch.
    Installing { version: String },
    Failed { detail: String },
}

/// Ask GitHub whether there is a newer release, and install it if so. Returns
/// at once — progress arrives through [`update_poll`].
///
/// `manual` is the difference between the palette command and the check on
/// launch: an automatic check says nothing unless it found an update, so a
/// machine that is offline or already current stays silent.
pub fn check_updates(manual: bool) {
    unsafe { wave_check_updates(manual as c_int) }
}

/// The next update state, or `None` if nothing has changed since the last call.
pub fn update_poll() -> Option<Update> {
    let mut state: c_int = 0;
    let mut version: *const c_char = std::ptr::null();
    let mut detail: *const c_char = std::ptr::null();
    let mut progress: f64 = 0.0;
    let changed = unsafe {
        wave_update_poll(&mut state, &mut version, &mut detail, &mut progress) != 0
    };
    if !changed {
        return None;
    }
    // Both point at static storage in the shim, valid until the next state.
    let str_at = |p: *const c_char| {
        if p.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
        }
    };
    let version = str_at(version);
    let detail = str_at(detail);
    // Mirrors the UPDATE_STATE_* enum in src/updater.h.
    Some(match state {
        1 => Update::Checking,
        2 => Update::UpToDate { version },
        3 => Update::Available { version, from: detail },
        4 => Update::Downloading { version, progress },
        6 => Update::Installing { version },
        _ => Update::Failed { detail },
    })
}

/// The version this build reports as its own.
pub fn version() -> String {
    unsafe { CStr::from_ptr(wave_version()) }.to_string_lossy().into_owned()
}

/// Give main-queue work `seconds` to run. Only needed without a UI: GPUI runs
/// the main run loop itself, so nothing outside `--selftest` should call this.
pub fn pump_main_queue(seconds: f64) {
    unsafe { wave_pump_main_queue(seconds) }
}

/// Whether the caret is in the visible half of its blink cycle. Shared with the
/// GLFW front-end (`view_cursor_visible`), so both blink identically: solid for
/// half a second after `last_activity`, then 1 Hz.
pub fn cursor_visible(now: f64, last_activity: f64) -> bool {
    unsafe { wave_cursor_visible(1, now, last_activity) != 0 }
}

impl Session {
    pub fn theme_index(&self) -> usize {
        unsafe { wave_theme_index(self.raw).max(0) as usize }
    }

    /// Select a theme by name and write the choice to the config. False if the
    /// build has no theme by that name.
    pub fn theme_set(&mut self, name: &str) -> bool {
        let Ok(c) = CString::new(name) else {
            return false;
        };
        unsafe { wave_theme_set(self.raw, c.as_ptr()) != 0 }
    }
}
