//! A GPUI front-end driving Wave's existing C core over FFI.
//!
//! Everything on screen — buffer text, tree-sitter spans, diagnostics, the
//! workspace tree, tabs, terminal cells, git status and diffs, completion
//! items, search hits, modal state — is computed in C by `libwave.a`. This file
//! lays it out and forwards input; it holds no editor state of its own beyond
//! scroll positions.
//!
//! The text surfaces are built from declarative `div`s, one per styled run.
//! That is fine at viewport scale and wrong long-term: the editor and terminal
//! surfaces should be custom `Element`s painting shaped glyph runs, the way
//! Zed's are.

mod ffi;
mod frontend_config;

use ffi::{flags, CloseAction, GitMode, Key, Mode, Session, TabKind, COLOR_DEFAULT};
use frontend_config::FrontendConfig;
use gpui::{
    actions, div, point, prelude::*, px, rgb, rgba, size, App, Application, Bounds, ClickEvent,
    ClipboardItem, Context, CursorStyle, FocusHandle, HighlightStyle, KeyBinding, KeyDownEvent,
    Menu, MenuItem, MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, ScrollDelta,
    FontStyle, FontWeight, ScrollWheelEvent, StyledText, SystemMenuType, Timer, TitlebarOptions,
    UnderlineStyle, Window,
    WindowBackgroundAppearance, WindowBounds, WindowOptions,
};
use std::ops::Range;
use std::path::PathBuf;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

actions!(wave, [Quit]);

/// How often to drain async work (server replies, pty output, ripgrep).
const POLL: Duration = Duration::from_millis(50);

/// Fallbacks until the config is read; the live values live on WaveView.
const TEXT_SIZE_DEFAULT: f32 = 13.0;
/// Menlo's advance at 13px. Only used to anchor the cursor-relative completion
/// menu; everything else is laid out by flexbox.
const ADVANCE_FALLBACK: f32 = 7.82;
const LINE_HEIGHT_DEFAULT: f32 = 19.0;
const STATUS_HEIGHT: f32 = 24.0;
const TAB_HEIGHT: f32 = 30.0;
const TITLEBAR_HEIGHT: f32 = 36.0;
const TRAFFIC_LIGHT_INSET: f32 = 78.0;
const GUTTER_WIDTH: f32 = 52.0;
const SIDEBAR_WIDTH: f32 = 240.0;
const GIT_FILES_WIDTH: f32 = 320.0;
const INDENT: f32 = 12.0;

const BG: u32 = 0x1a1c20;
const SIDEBAR_BG: u32 = 0x16181c;
const STATUS_BG: u32 = 0x22252b;
const TAB_BG: u32 = 0x16181c;
const TAB_ACTIVE_BG: u32 = 0x1a1c20;
const BORDER: u32 = 0x33383f;
const DEFAULT_FG: u32 = 0xd9dbe0;
const DIM_FG: u32 = 0x6b7280;
const GUTTER_FG: u32 = 0x4a505c;
const CURSOR_BG: u32 = 0xd9dbe0;
const SELECTION_BG: u32 = 0x2f4f6f;
const MATCH_BG: u32 = 0x5a4a1f;
const DIAGNOSTIC: u32 = 0xd9534f;
const DIR_FG: u32 = 0x89b4fa;
const ADDED_FG: u32 = 0x7fb069;
const REMOVED_FG: u32 = 0xd9534f;

/// An entry in the Cmd-Shift-P command palette.
///
/// These are front-end actions, not `:` commands — `command.c` only parses the
/// vim-style set (`:w`, `:opacity`, …) and has no notion of "open a folder".
#[derive(Clone, Copy, PartialEq, Eq)]
enum Cmd {
    OpenFolder,
    OpenFile,
    CloseProject,
    RecentProjects,
    NewFile,
    NewFolder,
    SaveFile,
    SaveConfig,
    ResetConfig,
    Settings,
    ChooseFont,
    FindFile,
    ProjectSearch,
    BufferSearch,
    NewTerminal,
    GitView,
    CloseTab,
    NextTab,
    PrevTab,
    ToggleSidebar,
    ToggleWrap,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Quit,
}

impl Cmd {
    const ALL: [Cmd; 25] = [
        Cmd::OpenFolder,
        Cmd::OpenFile,
        Cmd::Settings,
        Cmd::ChooseFont,
        Cmd::RecentProjects,
        Cmd::CloseProject,
        Cmd::NewFile,
        Cmd::NewFolder,
        Cmd::SaveFile,
        Cmd::FindFile,
        Cmd::ProjectSearch,
        Cmd::BufferSearch,
        Cmd::NewTerminal,
        Cmd::GitView,
        Cmd::CloseTab,
        Cmd::NextTab,
        Cmd::PrevTab,
        Cmd::ToggleSidebar,
        Cmd::ToggleWrap,
        Cmd::ZoomIn,
        Cmd::ZoomOut,
        Cmd::ZoomReset,
        Cmd::SaveConfig,
        Cmd::ResetConfig,
        Cmd::Quit,
    ];

    fn label(self) -> &'static str {
        match self {
            Cmd::OpenFolder => "Open Folder…",
            Cmd::OpenFile => "Open File…",
            Cmd::CloseProject => "Close Project",
            Cmd::RecentProjects => "Recent Projects",
            Cmd::NewFile => "New File",
            Cmd::NewFolder => "New Folder",
            Cmd::SaveFile => "Save File",
            Cmd::Settings => "Settings",
            Cmd::ChooseFont => "Change Font…",
            Cmd::SaveConfig => "Save Config",
            Cmd::ResetConfig => "Reset Settings to Defaults",
            Cmd::FindFile => "Find File",
            Cmd::ProjectSearch => "Search in Project",
            Cmd::BufferSearch => "Find in File",
            Cmd::NewTerminal => "New Terminal",
            Cmd::GitView => "Git Changes",
            Cmd::CloseTab => "Close Tab",
            Cmd::NextTab => "Next Tab",
            Cmd::PrevTab => "Previous Tab",
            Cmd::ToggleSidebar => "Toggle Sidebar",
            Cmd::ToggleWrap => "Toggle Soft Wrap",
            Cmd::ZoomIn => "Zoom In",
            Cmd::ZoomOut => "Zoom Out",
            Cmd::ZoomReset => "Reset Zoom",
            Cmd::Quit => "Quit Wave",
        }
    }

    fn shortcut(self) -> &'static str {
        match self {
            Cmd::SaveFile => "⌘S",
            Cmd::FindFile => "⌘P",
            Cmd::ProjectSearch => "⇧⌘F",
            Cmd::BufferSearch => "/",
            Cmd::NewTerminal => "⌘T",
            Cmd::GitView => "⇧⌘G",
            Cmd::CloseTab => "⌘W",
            Cmd::NextTab => "⌃⇥",
            Cmd::PrevTab => "⇧⌃⇥",
            Cmd::ToggleSidebar => "⌘B",
            Cmd::Settings => "⌘,",
            Cmd::NewFile => "⌘N",
            Cmd::NewFolder => "⇧⌘N",
            Cmd::ZoomIn => "⌘+",
            Cmd::ZoomOut => "⌘-",
            Cmd::ZoomReset => "⌘0",
            Cmd::Quit => "⌘Q",
            _ => "",
        }
    }

    /// Case-insensitive subsequence match, so "of" finds "Open Folder".
    fn matches(self, query: &str) -> bool {
        if query.is_empty() {
            return true;
        }
        let label = self.label().to_lowercase();
        let mut chars = label.chars();
        query
            .to_lowercase()
            .chars()
            .all(|q| q == ' ' || chars.any(|c| c == q))
    }
}

/// A row on the settings screen. Each one maps to a `WaveConfig` field.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Setting {
    Font,
    Opacity,
    Blur,
    Radius,
    FontSize,
    Zoom,
    Sidebar,
    SidebarWidth,
    Wrap,
    NativeTitlebar,
}

impl Setting {
    const ALL: [Setting; 10] = [
        Setting::Font,
        Setting::Opacity,
        Setting::Blur,
        Setting::Radius,
        Setting::FontSize,
        Setting::Zoom,
        Setting::Sidebar,
        Setting::SidebarWidth,
        Setting::Wrap,
        Setting::NativeTitlebar,
    ];

    fn label(self) -> &'static str {
        match self {
            Setting::Font => "Font",
            Setting::Opacity => "Window opacity",
            Setting::Blur => "Background blur",
            Setting::Radius => "Corner radius",
            Setting::FontSize => "Font size",
            Setting::Zoom => "Zoom",
            Setting::Sidebar => "Sidebar",
            Setting::SidebarWidth => "Sidebar width",
            Setting::Wrap => "Soft wrap",
            Setting::NativeTitlebar => "Native titlebar",
        }
    }

    /// Whether ← / → adjust it, versus Enter toggling it.
    fn is_toggle(self) -> bool {
        matches!(
            self,
            Setting::Blur | Setting::Sidebar | Setting::Wrap | Setting::NativeTitlebar
        )
    }

    fn note(self) -> &'static str {
        match self {
            Setting::Opacity => "needs a transparent window; 20–100%",
            Setting::Radius => "not applied — GPUI has no window corner radius",
            Setting::Wrap => "rewraps to the pane width",
            Setting::NativeTitlebar => "not applied — this build always draws its own",
            Setting::Zoom => "multiplies the font size",
            Setting::Font => "monospace families only · ⏎ to choose",
            _ => "",
        }
    }
}

/// A one-line prompt in the status bar, for the workspace file operations that
/// need a name or a confirmation.
enum Prompt {
    None,
    NewFile { dir: String, text: String },
    NewDir { dir: String, text: String },
    Delete { rel: String },
}

struct WaveView {
    session: Session,
    focus: FocusHandle,
    /// First visible buffer line.
    scroll: usize,
    /// Visible line capacity, recomputed each frame from the viewport.
    rows: usize,
    side_scroll: usize,
    status: String,
    prompt: Prompt,
    /// Measured monospace advance, cached from the text system on first render.
    advance: Option<f32>,
    /// True while the left button is held down over the text.
    dragging: bool,
    /// Last cursor position seen by `render`, so free scrolling is only
    /// interrupted when the cursor genuinely moves.
    last_cursor: (usize, usize),
    /// Leftover sub-line scroll. A trackpad delivers many small pixel deltas;
    /// rounding each one independently discards everything under a line and
    /// makes the gesture feel stuck.
    scroll_accum: f32,
    side_accum: f32,
    /// Live metrics, recomputed from `WaveConfig.base_pt` each frame so the
    /// zoom shortcuts and `:set` take effect immediately.
    text_size: f32,
    line_height: f32,
    /// Last background appearance pushed to the platform window.
    window_bg: WindowBackgroundAppearance,
    /// Viewport height from the last frame, for cursor-anchored popups.
    viewport_height: f32,
    /// Cmd-Shift-P command palette.
    cmds_open: bool,
    cmds_query: String,
    cmds_sel: usize,
    /// Recent-projects overlay, openable without closing the workspace.
    recent_open: bool,
    /// Settings screen.
    settings_open: bool,
    settings_sel: usize,
    /// Front-end-only settings (font family), stored separately from WaveConfig.
    fe_config: FrontendConfig,
    /// Font picker.
    fonts_open: bool,
    fonts_query: String,
    fonts_sel: usize,
    /// Show every family, not just the ones measured as fixed-pitch.
    fonts_all: bool,
    /// Monospace families, discovered once from the platform text system.
    mono_fonts: Option<Vec<String>>,
}

/// Folder and file marks, mirroring `draw_folder_icon` / `draw_file_icon`:
/// a tab-and-body rectangle, and a page with a folded corner.
/// A vertical caret bar. Insert mode uses a bar rather than a block, and the
/// single-line inputs get one instead of a literal `_` character.
fn caret(height: f32) -> impl IntoElement {
    div()
        .w(px(2.))
        .h(px(height))
        .flex_none()
        .bg(rgb(CURSOR_BG))
}

fn folder_icon(color: u32) -> impl IntoElement {
    div()
        .w(px(13.))
        .h(px(13.))
        .flex_none()
        .relative()
        .child(
            div()
                .absolute()
                .left_0()
                .top(px(2.))
                .w(px(6.))
                .h(px(3.))
                .rounded(px(1.))
                .bg(rgb(color)),
        )
        .child(
            div()
                .absolute()
                .left_0()
                .top(px(4.))
                .w(px(13.))
                .h(px(8.))
                .rounded(px(1.5))
                .bg(rgb(color)),
        )
}

fn file_icon(color: u32) -> impl IntoElement {
    div()
        .w(px(13.))
        .h(px(13.))
        .flex_none()
        .relative()
        .child(
            div()
                .absolute()
                .left(px(2.))
                .top(px(1.))
                .w(px(9.))
                .h(px(11.))
                .rounded(px(1.))
                .bg(rgb(color)),
        )
        // The fold, punched out in the sidebar's own background.
        .child(
            div()
                .absolute()
                .left(px(7.))
                .top(px(1.))
                .w(px(4.))
                .h(px(4.))
                .bg(rgb(SIDEBAR_BG)),
        )
}

/// Styling for a single byte of a line.
///
/// The whole point of accumulating per-byte and *then* collapsing into ranges
/// is that the line is handed to the text system as one string. Splitting a
/// line into several text elements re-shapes each piece independently, and the
/// sub-pixel advances no longer sum to the same total — so the line visibly
/// shifts as the cursor moves through it.
#[derive(Clone, Copy, PartialEq, Default)]
struct Cell {
    fg: Option<u32>,
    bg: Option<u32>,
    underline: Option<u32>,
    /// Squiggly rather than straight, for diagnostics.
    wavy: bool,
    bold: bool,
    italic: bool,
    /// The block cursor: swap foreground and background.
    invert: bool,
}

/// Trace to stderr when `WAVE_DEBUG` is set. Key routing in a GUI is otherwise
/// invisible, and "it doesn't work" needs to be answerable without screenshots.
fn dbg(msg: impl std::fmt::Display) {
    use std::sync::OnceLock;
    static ON: OnceLock<bool> = OnceLock::new();
    if *ON.get_or_init(|| std::env::var_os("WAVE_DEBUG").is_some()) {
        eprintln!("[wave] {msg}");
    }
}

/// Monotonic-enough seconds for the C core's watcher poll throttle.
fn now_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

/// Is this family fixed-pitch?
///
/// GPUI exposes no monospace trait, so this measures. Comparing only `i` and
/// `M` lets symbol faces like Webdings through, so a wider sample is used —
/// every glyph in a fixed-pitch font advances identically.
fn is_monospace(ts: &std::sync::Arc<gpui::TextSystem>, size: gpui::Pixels, name: &str) -> bool {
    let id = ts.resolve_font(&gpui::font(name.to_string()));
    let mut widths = ['i', 'M', 'W', 'l', '0', '.'].into_iter().map(|c| {
        ts.advance(id, size, c)
            .map(|s| f32::from(s.width))
            .unwrap_or(-1.0)
    });
    let Some(first) = widths.next() else {
        return false;
    };
    first > 0.0 && widths.all(|w| (w - first).abs() < 0.01)
}

fn on_off(v: bool) -> String {
    if v { "on".into() } else { "off".into() }
}

fn wheel_lines(ev: &ScrollWheelEvent, line_height: f32) -> f32 {
    match ev.delta {
        ScrollDelta::Pixels(p) => f32::from(p.y) / line_height,
        ScrollDelta::Lines(l) => l.y,
    }
}

fn highlight_for(cell: Cell) -> HighlightStyle {
    let mut style = HighlightStyle::default();
    if cell.invert {
        style.color = Some(rgb(BG).into());
        style.background_color = Some(rgb(CURSOR_BG).into());
    } else {
        style.color = cell.fg.map(|c| rgb(c).into());
        style.background_color = cell.bg.map(|c| rgb(c).into());
    }
    if cell.bold {
        style.font_weight = Some(FontWeight::BOLD);
    }
    if cell.italic {
        style.font_style = Some(FontStyle::Italic);
    }
    if let Some(u) = cell.underline {
        style.underline = Some(UnderlineStyle {
            thickness: px(1.0),
            color: Some(rgb(u).into()),
            wavy: cell.wavy,
        });
    }
    style
}

/// Weight and slant for a tree-sitter capture.
///
/// `theme.c` only maps a capture name to a colour — it has no notion of weight
/// or slant — so the emphasis table lives here rather than in the C core.
fn capture_emphasis(name: &str) -> (bool, bool) {
    match name {
        // (bold, italic)
        "comment" => (false, true),
        "keyword" => (true, false),
        "function" => (true, false),
        "type" => (false, false),
        _ => (false, false),
    }
}

/// Collapse per-byte cells into contiguous highlight ranges and shape the line
/// as a single text run.
fn styled_line(text: String, cells: &[Cell]) -> StyledText {
    let mut highlights: Vec<(Range<usize>, HighlightStyle)> = Vec::new();
    let mut start = 0usize;
    let mut current: Option<Cell> = None;

    for (i, _) in text.char_indices() {
        let cell = cells.get(i).copied().unwrap_or_default();
        if current != Some(cell) {
            if let Some(prev) = current {
                if start < i && prev != Cell::default() {
                    highlights.push((start..i, highlight_for(prev)));
                }
            }
            start = i;
            current = Some(cell);
        }
    }
    if let Some(prev) = current {
        if start < text.len() && prev != Cell::default() {
            highlights.push((start..text.len(), highlight_for(prev)));
        }
    }

    StyledText::new(text).with_highlights(highlights)
}

impl WaveView {
    fn new(path: Option<PathBuf>, focus: FocusHandle) -> Self {
        let mut session = Session::new();
        let status = match &path {
            Some(p) => match session.open(p) {
                Ok(()) => {
                    session.watch_workspace_start();
                    String::new()
                }
                Err(e) => e,
            },
            None => String::new(),
        };
        WaveView {
            session,
            focus,
            scroll: 0,
            rows: 30,
            side_scroll: 0,
            status,
            prompt: Prompt::None,
            advance: None,
            dragging: false,
            last_cursor: (0, 0),
            scroll_accum: 0.0,
            side_accum: 0.0,
            text_size: TEXT_SIZE_DEFAULT,
            line_height: LINE_HEIGHT_DEFAULT,
            window_bg: WindowBackgroundAppearance::Opaque,
            viewport_height: 720.0,
            cmds_open: false,
            cmds_query: String::new(),
            cmds_sel: 0,
            recent_open: false,
            settings_open: false,
            settings_sel: 0,
            fe_config: FrontendConfig::load(),
            fonts_open: false,
            fonts_query: String::new(),
            fonts_sel: 0,
            fonts_all: false,
            mono_fonts: None,
        }
    }

    /// Pull the type metrics out of the config. Changing the size invalidates
    /// the cached advance, which is measured at a specific point size.
    fn sync_metrics(&mut self) {
        let pt = self.session.base_pt().clamp(8.0, 48.0);
        if (pt - self.text_size).abs() > f32::EPSILON {
            self.text_size = pt;
            self.line_height = (pt * 1.46).round().max(pt + 2.0);
            self.advance = None;
        }
    }

    /// Fold a wheel event into `accum` and return whole lines to scroll.
    fn take_lines(accum: &mut f32, ev: &ScrollWheelEvent, line_height: f32) -> i64 {
        *accum += -wheel_lines(ev, line_height);
        let whole = accum.trunc();
        *accum -= whole;
        whole as i64
    }

    /// The real monospace advance, measured once from the font rather than
    /// guessed. Mouse hit-testing and the completion anchor both need it.
    fn advance(&mut self, window: &Window) -> f32 {
        if let Some(a) = self.advance {
            return a;
        }
        let ts = window.text_system();
        let font_id = ts.resolve_font(&gpui::font(self.fe_config.font.clone()));
        let a = ts
            .advance(font_id, px(self.text_size), 'M')
            .map(|s| f32::from(s.width))
            .unwrap_or(ADVANCE_FALLBACK);
        dbg(format_args!("measured advance = {a}"));
        self.advance = Some(a);
        a
    }

    /// Window point -> buffer (line, column). The text origin is known from the
    /// layout constants, and scrolling is in whole lines.
    fn point_to_position(&mut self, position: gpui::Point<gpui::Pixels>, window: &Window) -> (usize, usize) {
        let adv = self.advance(window).max(1.0);
        let has_ws = self.session.has_workspace();
        let has_tabs = self.session.tab_count() > 0;

        let text_left = if has_ws { SIDEBAR_WIDTH } else { 0.0 } + GUTTER_WIDTH;
        let text_top = TITLEBAR_HEIGHT + if has_tabs { TAB_HEIGHT } else { 0.0 };

        let x = (f32::from(position.x) - text_left).max(0.0);
        let y = (f32::from(position.y) - text_top).max(0.0);

        let vrow = self.scroll + (y / self.line_height) as usize;
        let col_in_row = (x / adv + 0.5) as usize;

        // A visual row maps back to a logical line plus a byte offset, so a
        // click on a wrapped continuation lands in the right place.
        match self.session.visual_row(vrow) {
            Some(row) => (row.line, row.start_byte + col_in_row),
            None => {
                let last = self.session.line_count().saturating_sub(1);
                (last, col_in_row)
            }
        }
    }

    fn on_text_mouse_down(
        &mut self,
        ev: &MouseDownEvent,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let (line, col) = self.point_to_position(ev.position, window);
        if ev.click_count >= 2 {
            // Double click selects the word under the cursor, via the same
            // motion the editor uses for `*`.
            self.session.click_at(line, col);
            self.session.text_input('v');
            self.session.text_input('i');
            self.session.text_input('w');
        } else {
            self.session.click_at(line, col);
            self.dragging = true;
        }
        cx.notify();
    }

    fn on_text_mouse_move(
        &mut self,
        ev: &MouseMoveEvent,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if !self.dragging || ev.pressed_button != Some(MouseButton::Left) {
            return;
        }
        let (line, col) = self.point_to_position(ev.position, window);
        self.session.drag_to(line, col);
        self.follow_cursor();
        cx.notify();
    }

    fn on_text_mouse_up(&mut self, _ev: &MouseUpEvent, _window: &mut Window, cx: &mut Context<Self>) {
        if self.dragging {
            self.dragging = false;
            cx.notify();
        }
    }

    /// Window point -> terminal (scrollback row, column).
    fn point_to_term_cell(&mut self, position: gpui::Point<gpui::Pixels>, window: &Window) -> (usize, usize) {
        let adv = self.advance(window).max(1.0);
        let has_ws = self.session.has_workspace() && self.session.show_sidebar();
        let left = if has_ws { SIDEBAR_WIDTH } else { 0.0 } + 8.0;
        let top = TITLEBAR_HEIGHT + if self.session.tab_count() > 0 { TAB_HEIGHT } else { 0.0 };

        let x = (f32::from(position.x) - left).max(0.0);
        let y = (f32::from(position.y) - top).max(0.0);
        let screen_row = (y / self.line_height) as usize;
        let start = self.session.term_visible_start(self.rows);
        (start + screen_row, (x / adv) as usize)
    }

    fn on_term_mouse_down(&mut self, ev: &MouseDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        let (row, col) = self.point_to_term_cell(ev.position, window);
        self.session.term_sel_begin(row, col);
        self.dragging = true;
        cx.notify();
    }

    fn on_term_mouse_move(&mut self, ev: &MouseMoveEvent, window: &mut Window, cx: &mut Context<Self>) {
        if !self.dragging || ev.pressed_button != Some(MouseButton::Left) {
            return;
        }
        let (row, col) = self.point_to_term_cell(ev.position, window);
        self.session.term_sel_update(row, col);
        cx.notify();
    }

    fn on_term_mouse_up(&mut self, _ev: &MouseUpEvent, _window: &mut Window, cx: &mut Context<Self>) {
        if self.dragging {
            self.dragging = false;
            self.session.term_sel_end();
            cx.notify();
        }
    }

    /// Window point -> git diff (line index, column).
    fn point_to_git_cell(&mut self, position: gpui::Point<gpui::Pixels>, window: &Window) -> (usize, usize) {
        let adv = self.advance(window).max(1.0);
        let has_ws = self.session.has_workspace() && self.session.show_sidebar();
        // The diff pane sits right of the sidebar and the file list.
        let left = if has_ws { SIDEBAR_WIDTH } else { 0.0 } + GIT_FILES_WIDTH + 10.0;
        let top = TITLEBAR_HEIGHT + if self.session.tab_count() > 0 { TAB_HEIGHT } else { 0.0 };

        let x = (f32::from(position.x) - left).max(0.0);
        let y = (f32::from(position.y) - top).max(0.0);
        let row = (y / self.line_height) as usize + self.session.git_diff_scroll_pos();
        (row, (x / adv) as usize)
    }

    fn on_git_mouse_down(&mut self, ev: &MouseDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        let (line, col) = self.point_to_git_cell(ev.position, window);
        self.session.git_sel_begin(line, col);
        self.dragging = true;
        cx.notify();
    }

    fn on_git_mouse_move(&mut self, ev: &MouseMoveEvent, window: &mut Window, cx: &mut Context<Self>) {
        if !self.dragging || ev.pressed_button != Some(MouseButton::Left) {
            return;
        }
        let (line, col) = self.point_to_git_cell(ev.position, window);
        self.session.git_sel_update(line, col);
        cx.notify();
    }

    fn on_git_mouse_up(&mut self, _ev: &MouseUpEvent, _window: &mut Window, cx: &mut Context<Self>) {
        if self.dragging {
            self.dragging = false;
            self.session.git_sel_end();
            cx.notify();
        }
    }

    /// The workspace-relative directory of the active file, for "new file here".
    fn current_dir(&self) -> String {
        let path = self.session.path();
        let root = self.session.ws_root();
        let rel = path.strip_prefix(&root).unwrap_or(&path).trim_start_matches('/');
        rel.rsplit_once('/').map(|(d, _)| d.to_string()).unwrap_or_default()
    }

    fn current_rel(&self) -> String {
        let path = self.session.path();
        let root = self.session.ws_root();
        path.strip_prefix(&root)
            .unwrap_or(&path)
            .trim_start_matches('/')
            .to_string()
    }

    /// Returns true if the prompt consumed the key.
    fn prompt_key(&mut self, key: &str, typed: Option<String>, plain: bool) -> bool {
        if matches!(self.prompt, Prompt::None) {
            return false;
        }
        match key {
            "escape" => {
                self.prompt = Prompt::None;
                self.status = String::new();
            }
            "enter" => {
                let prompt = std::mem::replace(&mut self.prompt, Prompt::None);
                match prompt {
                    Prompt::NewFile { dir, text } if !text.is_empty() => {
                        let (_, msg) = self.session.ws_create(&dir, &text, false);
                        self.status = msg;
                    }
                    Prompt::NewDir { dir, text } if !text.is_empty() => {
                        let (_, msg) = self.session.ws_create(&dir, &text, true);
                        self.status = msg;
                    }
                    Prompt::Delete { rel } => {
                        let (_, msg) = self.session.ws_delete(&rel);
                        self.status = msg;
                    }
                    _ => {}
                }
            }
            "backspace" => match &mut self.prompt {
                Prompt::NewFile { text, .. } | Prompt::NewDir { text, .. } => {
                    text.pop();
                }
                _ => {}
            },
            _ => {
                if let (true, Some(t)) = (plain, typed) {
                    match &mut self.prompt {
                        Prompt::NewFile { text, .. } | Prompt::NewDir { text, .. } => {
                            text.push_str(&t)
                        }
                        _ => {}
                    }
                }
            }
        }
        true
    }

    fn prompt_line(&self) -> Option<String> {
        match &self.prompt {
            Prompt::None => None,
            Prompt::NewFile { dir, text } => Some(format!(
                "new file: {}{}{text}",
                dir,
                if dir.is_empty() { "" } else { "/" }
            )),
            Prompt::NewDir { dir, text } => Some(format!(
                "new folder: {}{}{text}",
                dir,
                if dir.is_empty() { "" } else { "/" }
            )),
            Prompt::Delete { rel } => Some(format!("delete {rel}? (Enter / Esc)")),
        }
    }

    /// Current value of a setting, formatted for display.
    fn setting_value(&self, setting: Setting) -> String {
        match setting {
            Setting::Font => self.fe_config.font.clone(),
            Setting::Opacity => format!("{}%", self.session.opacity_pct()),
            Setting::Blur => on_off(self.session.blur()),
            Setting::Radius => format!("{:.0}", self.session.radius()),
            Setting::FontSize => format!("{:.0}pt", self.session.base_pt() / (self.session.scale_pct() as f32 / 100.0)),
            Setting::Zoom => format!("{}%", self.session.scale_pct()),
            Setting::Sidebar => on_off(self.session.show_sidebar()),
            Setting::SidebarWidth => format!("{} cols", self.session.side_cells()),
            Setting::Wrap => on_off(self.session.wrap()),
            Setting::NativeTitlebar => on_off(self.session.native_titlebar()),
        }
    }

    /// Adjust a setting. `delta` is +1/-1; toggles ignore it.
    fn adjust_setting(&mut self, setting: Setting, delta: i32) {
        match setting {
            // Opened from the settings screen with Enter; nothing to nudge.
            Setting::Font => {}
            Setting::Opacity => {
                let next = self.session.opacity_pct() as i32 + delta * 5;
                self.session.set_opacity(next.clamp(20, 100) as f32 / 100.0);
            }
            Setting::Blur => {
                self.session.toggle_blur();
            }
            Setting::Radius => {
                let next = self.session.radius() + delta as f32;
                self.session.set_radius(next);
            }
            Setting::FontSize => {
                // base_pt is the unscaled size; back it out of the effective one.
                let scale = (self.session.scale_pct() as f32 / 100.0).max(0.01);
                let base = self.session.base_pt() / scale;
                self.session.set_base_pt(base + delta as f32);
            }
            Setting::Zoom => {
                self.session.zoom(delta);
            }
            Setting::Sidebar => {
                self.session.toggle_sidebar();
            }
            Setting::SidebarWidth => {
                let next = self.session.side_cells() as i32 + delta * 2;
                self.session.set_side_cells(next.max(0) as usize);
            }
            Setting::Wrap => {
                self.session.toggle_wrap();
            }
            Setting::NativeTitlebar => {
                self.session.toggle_titlebar();
            }
        }
        // Type metrics may have moved; re-measure the advance.
        self.advance = None;
    }

    fn render_settings(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let sel = self.settings_sel.min(Setting::ALL.len() - 1);
        let rows: Vec<_> = Setting::ALL
            .into_iter()
            .enumerate()
            .map(|(i, setting)| {
                let selected = i == sel;
                let value = self.setting_value(setting);
                let note = setting.note();
                div()
                    .id(("setting", i))
                    .flex()
                    .flex_row()
                    .items_center()
                    .justify_between()
                    .h(px(self.line_height + 6.))
                    .px(px(12.))
                    .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.settings_sel = i;
                        this.adjust_setting(setting, 1);
                        cx.notify();
                    }))
                    .child(
                        div()
                            .flex()
                            .flex_row()
                            .gap(px(10.))
                            .child(
                                div()
                                    .w(px(150.))
                                    .flex_none()
                                    .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                                    .child(setting.label()),
                            )
                            .when(!note.is_empty(), |d| {
                                d.child(div().text_color(rgb(GUTTER_FG)).child(note))
                            }),
                    )
                    .child(
                        div()
                            .flex_none()
                            .text_color(rgb(if selected { 0xe6c07b } else { DIM_FG }))
                            .child(value),
                    )
            })
            .collect();

        div()
            .absolute()
            .top_0()
            .left_0()
            .size_full()
            .flex()
            .flex_col()
            .items_center()
            .child(
                div()
                    .mt(px(56.))
                    .w(px(720.))
                    .flex()
                    .flex_col()
                    .bg(rgb(SIDEBAR_BG))
                    .border_1()
                    .border_color(rgb(BORDER))
                    .rounded(px(6.))
                    .overflow_hidden()
                    .child(
                        div()
                            .h(px(self.line_height + 10.))
                            .flex()
                            .flex_row()
                            .items_center()
                            .px(px(12.))
                            .border_b_1()
                            .border_color(rgb(BORDER))
                            .text_color(rgb(DEFAULT_FG))
                            .child("Settings"),
                    )
                    .children(rows)
                    .child(
                        div()
                            .h(px(self.line_height + 6.))
                            .px(px(12.))
                            .border_t_1()
                            .border_color(rgb(BORDER))
                            .text_color(rgb(GUTTER_FG))
                            .child("↑↓ select · ←→ adjust · ⏎ toggle · s save · r reset · esc close"),
                    ),
            )
    }

    /// Monospace families available to the platform text system.
    ///
    /// GPUI has no "is this monospace" query, so each family is measured: in a
    /// fixed-pitch font `i` and `M` advance identically. Done once and cached —
    /// it resolves every installed family.
    fn mono_fonts(&mut self, window: &Window) -> Vec<String> {
        if let Some(cached) = &self.mono_fonts {
            return cached.clone();
        }
        let ts = window.text_system();
        let size = px(TEXT_SIZE_DEFAULT);

        let mut names: Vec<String> = ts
            .all_font_names()
            .into_iter()
            .filter(|name| is_monospace(&ts, size, name))
            .collect();
        names.sort_by_key(|n| n.to_lowercase());
        names.dedup();

        dbg(format_args!("{} monospace families", names.len()));
        self.mono_fonts = Some(names.clone());
        names
    }

    fn fonts_filtered(&mut self, window: &Window) -> Vec<String> {
        let query = self.fonts_query.to_lowercase();
        let names = if self.fonts_all {
            let mut all = window.text_system().all_font_names();
            all.sort_by_key(|n| n.to_lowercase());
            all.dedup();
            all
        } else {
            self.mono_fonts(window)
        };
        names
            .into_iter()
            .filter(|n| query.is_empty() || n.to_lowercase().contains(&query))
            .collect()
    }

    fn set_font(&mut self, name: String) {
        self.fe_config.font = name;
        self.fe_config.save();
        // The advance was measured against the previous family.
        self.advance = None;
        self.status = format!("font: {}", self.fe_config.font);
    }

    fn render_fonts(&mut self, window: &Window, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 12;
        let items = self.fonts_filtered(window);
        let total = items.len();
        let sel = self.fonts_sel.min(total.saturating_sub(1));
        let first = sel
            .saturating_sub(VISIBLE / 2)
            .min(total.saturating_sub(VISIBLE.min(total)));
        let last = (first + VISIBLE).min(total);
        let current = self.fe_config.font.clone();

        let rows: Vec<_> = (first..last)
            .map(|i| {
                let name = items[i].clone();
                let selected = i == sel;
                let is_current = name == current;
                let pick = name.clone();
                div()
                    .id(("font-row", i))
                    .flex()
                    .flex_row()
                    .items_center()
                    .justify_between()
                    .h(px(self.line_height + 4.))
                    .px(px(12.))
                    .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.set_font(pick.clone());
                        this.fonts_open = false;
                        cx.notify();
                    }))
                    .child(
                        div()
                            .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                            .child(name.clone()),
                    )
                    // Preview each family in itself.
                    .child(
                        div()
                            .font_family(name)
                            .text_color(rgb(if is_current { 0xe6c07b } else { GUTTER_FG }))
                            .child(if is_current {
                                "fn main() {}  ✓"
                            } else {
                                "fn main() {}"
                            }),
                    )
            })
            .collect();

        overlay_panel(
            format!("font › {}", self.fonts_query),
            rows,
            if self.fonts_all {
                format!("{total} families (all) · ⌃A for monospace only · current: {current}")
            } else {
                format!("{total} monospace · ⌃A for all families · current: {current}")
            },
            self.line_height - 6.0,
        )
    }

    fn cmds_filtered(&self) -> Vec<Cmd> {
        Cmd::ALL
            .into_iter()
            .filter(|c| c.matches(&self.cmds_query))
            .collect()
    }

    fn open_path(&mut self, path: &std::path::Path, cx: &mut Context<Self>) {
        match self.session.open(path) {
            Ok(()) => {
                self.session.watch_workspace_start();
                self.session.recent_add(&path.to_string_lossy());
                self.scroll = 0;
                self.status = String::new();
            }
            Err(e) => self.status = e,
        }
        cx.notify();
    }

    /// Run a palette entry. Anything needing a native dialog goes async.
    fn run_cmd(&mut self, cmd: Cmd, window: &mut Window, cx: &mut Context<Self>) {
        match cmd {
            Cmd::OpenFolder | Cmd::OpenFile => {
                let directories = cmd == Cmd::OpenFolder;
                let rx = cx.prompt_for_paths(gpui::PathPromptOptions {
                    files: !directories,
                    directories,
                    multiple: false,
                    prompt: None,
                });
                cx.spawn(async move |this, cx| {
                    let Ok(Ok(Some(paths))) = rx.await else {
                        return;
                    };
                    let Some(path) = paths.into_iter().next() else {
                        return;
                    };
                    let _ = this.update(cx, |this: &mut WaveView, cx| {
                        this.open_path(&path, cx);
                    });
                })
                .detach();
            }
            Cmd::CloseProject => {
                self.session.close_workspace();
                self.scroll = 0;
            }
            Cmd::RecentProjects => self.recent_open = true,
            Cmd::NewFile => {
                self.prompt = Prompt::NewFile {
                    dir: self.current_dir(),
                    text: String::new(),
                }
            }
            Cmd::NewFolder => {
                self.prompt = Prompt::NewDir {
                    dir: self.current_dir(),
                    text: String::new(),
                }
            }
            Cmd::SaveFile => {
                let ok = self.session.save();
                self.status = if ok { "written".into() } else { "write failed".into() };
            }
            Cmd::SaveConfig => {
                self.session.save_config();
                self.status = "config saved".into();
            }
            Cmd::Settings => {
                self.settings_open = true;
                self.settings_sel = 0;
            }
            Cmd::ChooseFont => {
                self.fonts_open = true;
                self.fonts_query.clear();
                self.fonts_sel = 0;
            }
            Cmd::ResetConfig => {
                self.session.reset_config();
                // Metrics are derived from the config, so re-measure.
                self.advance = None;
                self.status = "settings reset".into();
            }
            Cmd::FindFile => {
                self.session.palette_open();
            }
            Cmd::ProjectSearch => {
                self.session.search_open();
            }
            Cmd::BufferSearch => {
                self.session.text_input('/');
            }
            Cmd::NewTerminal => {
                self.session.term_open("terminal", "");
            }
            Cmd::GitView => {
                self.session.git_open();
            }
            Cmd::CloseTab => {
                let i = self.session.tab_active();
                self.session.tab_close(i);
            }
            Cmd::NextTab => self.session.tab_goto(1),
            Cmd::PrevTab => self.session.tab_goto(-1),
            Cmd::ToggleSidebar => {
                self.session.toggle_sidebar();
            }
            Cmd::ToggleWrap => {
                self.session.toggle_wrap();
            }
            Cmd::ZoomIn => {
                self.session.zoom(1);
            }
            Cmd::ZoomOut => {
                self.session.zoom(-1);
            }
            Cmd::ZoomReset => {
                self.session.zoom(0);
            }
            Cmd::Quit => cx.quit(),
        }
        let _ = window;
        self.follow_cursor();
        cx.notify();
    }

    fn render_cmds(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 12;
        let items = self.cmds_filtered();
        let total = items.len();
        let sel = self.cmds_sel.min(total.saturating_sub(1));
        let first = sel
            .saturating_sub(VISIBLE / 2)
            .min(total.saturating_sub(VISIBLE.min(total)));
        let last = (first + VISIBLE).min(total);

        let rows: Vec<_> = (first..last)
            .map(|i| {
                let cmd = items[i];
                let selected = i == sel;
                div()
                    .id(("cmd-row", i))
                    .flex()
                    .flex_row()
                    .items_center()
                    .justify_between()
                    .h(px(self.line_height))
                    .px(px(10.))
                    .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                    .on_click(cx.listener(move |this, _, window, cx| {
                        this.cmds_open = false;
                        this.run_cmd(cmd, window, cx);
                    }))
                    .child(
                        div()
                            .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                            .child(cmd.label()),
                    )
                    .child(
                        div()
                            .text_color(rgb(GUTTER_FG))
                            .child(cmd.shortcut()),
                    )
            })
            .collect();

        overlay_panel(
            format!("> {}", self.cmds_query),
            rows,
            format!("{total} commands"),
            self.line_height - 6.0,
        )
    }

    /// The empty state: recent projects, straight from `recent.c`.
    /// Recent-project rows, shared by the empty state and the overlay.
    fn recent_rows(&mut self, cx: &mut Context<Self>) -> Vec<gpui::Stateful<gpui::Div>> {
        let total = self.session.recent_count();
        let sel = self.session.recent_selected().min(total.saturating_sub(1));

        (0..total.min(16))
            .map(|i| {
                let path = self.session.recent_path(i);
                let selected = i == sel;
                let name = path.rsplit('/').next().unwrap_or(&path).to_string();
                // Show the containing directory rather than the full path; the
                // basename is the useful part and these get long.
                let parent = path
                    .rsplit_once('/')
                    .map(|(d, _)| d.to_string())
                    .unwrap_or_default();
                div()
                    .id(("recent", i))
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap(px(10.))
                    .h(px(self.line_height + 2.))
                    .px(px(12.))
                    .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        let cur = this.session.recent_selected();
                        this.session.recent_move(i as i32 - cur as i32);
                        if this.session.recent_accept() {
                            this.session.watch_workspace_start();
                            this.scroll = 0;
                        }
                        this.recent_open = false;
                        cx.notify();
                    }))
                    .child(folder_icon(DIR_FG))
                    .child(
                        div()
                            .flex_none()
                            .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                            .child(name),
                    )
                    .child(
                        div()
                            .text_color(rgb(GUTTER_FG))
                            .whitespace_nowrap()
                            .overflow_hidden()
                            .child(parent),
                    )
            })
            .collect()
    }

    /// The empty state, shown when no project is open.
    fn render_recent_empty(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let total = self.session.recent_count();
        let query = self.session.recent_query();
        let rows = self.recent_rows(cx);
        div()
            .flex()
            .flex_col()
            .flex_grow()
            .items_center()
            .pt(px(80.))
            .child(
                div()
                    .w(px(620.))
                    .flex()
                    .flex_col()
                    .child(
                        div()
                            .h(px(self.line_height + 10.))
                            .px(px(12.))
                            .text_color(rgb(DIM_FG))
                            .child(if total == 0 && query.is_empty() {
                                "no recent projects — ⇧⌘P then \"Open Folder\"".to_string()
                            } else {
                                "recent projects".to_string()
                            }),
                    )
                    // Typing here filters the list, so it needs a visible query
                    // and caret — without them the keystrokes look ignored.
                    .child(
                        div()
                            .flex()
                            .flex_row()
                            .items_center()
                            .h(px(self.line_height + 6.))
                            .px(px(12.))
                            .border_b_1()
                            .border_color(rgb(BORDER))
                            .text_color(rgb(DEFAULT_FG))
                            .child(format!("› {query}"))
                            .child(caret(self.line_height - 6.0)),
                    )
                    .children(rows)
                    .when(total == 0 && !query.is_empty(), |d| {
                        d.child(
                            div()
                                .h(px(self.line_height))
                                .px(px(12.))
                                .text_color(rgb(GUTTER_FG))
                                .child("no match"),
                        )
                    }),
            )
    }

    /// The same list as an overlay, openable over an existing project.
    fn render_recent_overlay(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let total = self.session.recent_count();
        let query = self.session.recent_query();
        let rows = self.recent_rows(cx);
        overlay_panel(
            format!("recent › {query}"),
            rows,
            format!("{total} projects"),
            self.line_height - 6.0,
        )
    }

    // ---- editor surface ----

    /// Style one buffer line, byte by byte, then hand it to the text system as
    /// a single shaped run. Byte columns come from the C core.
    fn editor_line(&mut self, line: usize, cursor_col: Option<usize>) -> (String, Vec<Cell>) {
        let text = self.session.line_text(line);
        let spans = self.session.line_spans(line);
        let diags = self.session.line_diagnostics(line);
        let matches = self.session.line_matches(line);
        let selection = self.session.line_selection(line);

        // One extra cell so a cursor parked past the last character has a slot.
        let mut cells = vec![Cell::default(); text.len() + 1];
        for c in cells.iter_mut() {
            c.fg = Some(DEFAULT_FG);
        }

        for s in &spans {
            let color = ffi::theme_rgb(s.name);
            let (bold, italic) = capture_emphasis(s.name);
            let end = s.end_col.min(text.len());
            for cell in cells.iter_mut().take(end).skip(s.start_col) {
                cell.fg = Some(color);
                cell.bold = bold;
                cell.italic = italic;
            }
        }
        for m in &matches {
            let end = m.end_col.min(text.len());
            for cell in cells.iter_mut().take(end).skip(m.start_col) {
                cell.bg = Some(MATCH_BG);
            }
        }
        if let Some((a, b)) = selection {
            let end = b.min(text.len());
            for cell in cells.iter_mut().take(end).skip(a) {
                cell.bg = Some(SELECTION_BG);
            }
        }
        for d in &diags {
            let end = d.end_col.min(text.len());
            for cell in cells.iter_mut().take(end).skip(d.start_col) {
                cell.underline = Some(DIAGNOSTIC);
                cell.wavy = true;
            }
        }
        if let Some(col) = cursor_col {
            if let Some(cell) = cells.get_mut(col) {
                cell.invert = true;
            }
        }

        (text, cells)
    }

    /// Render one visual row: a byte slice of a logical line. With wrapping off
    /// the slice is the whole line.
    fn editor_row(&mut self, row: ffi::VisualRow, cursor_col: Option<usize>) -> StyledText {
        let (text, cells) = self.editor_line(row.line, cursor_col);

        // wrap_line breaks on byte offsets; snap defensively so a slice can
        // never land inside a multi-byte sequence.
        let snap = |i: usize| {
            let mut i = i.min(text.len());
            while i > 0 && !text.is_char_boundary(i) {
                i -= 1;
            }
            i
        };
        let start = snap(row.start_byte);
        let end = snap(row.end_byte.max(row.start_byte));

        let mut sliced_text = text[start..end].to_string();
        let mut sliced: Vec<Cell> = cells[start..end.min(cells.len())].to_vec();

        // A cursor parked past the last character needs a cell of its own, and
        // it belongs to the row that actually ends the line.
        if cursor_col == Some(text.len()) && end == text.len() {
            sliced_text.push(' ');
            sliced.push(Cell {
                invert: true,
                ..Cell::default()
            });
        }

        styled_line(sliced_text, &sliced)
    }

    /// Put the cursor line in the middle of the viewport. main.c does this
    /// after a jump (go-to-definition, a search hit, opening at a line) rather
    /// than scrolling minimally, which loses the surrounding context.
    fn center_cursor(&mut self) {
        let Some((vrow, _)) = self.session.cursor_visual() else {
            return;
        };
        self.scroll = vrow.saturating_sub(self.rows / 2);
    }

    fn follow_cursor(&mut self) {
        // Scrolling is in visual rows, which equal logical lines when wrapping
        // is off.
        let Some((vrow, _)) = self.session.cursor_visual() else {
            return;
        };
        if vrow < self.scroll {
            self.scroll = vrow;
        } else if self.rows > 0 && vrow >= self.scroll + self.rows {
            self.scroll = vrow + 1 - self.rows;
        }
    }

    fn render_editor(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let total = self.session.visual_rows();
        let (cur_row, cur_col) = self.session.cursor();
        let insert_mode = self.session.mode() == Mode::Insert;
        let last = (self.scroll + self.rows).min(total);
        let has_buffer = self.session.has_buffer();

        let lines: Vec<_> = (self.scroll..last)
            .filter_map(|vrow| {
                let row = self.session.visual_row(vrow)?;
                // The cursor belongs to this row when its byte offset falls in
                // the row's slice; `<=` so an end-of-line cursor lands on the
                // row that ends the line rather than the next one.
                // Insert mode draws a bar caret over the top instead, so the
                // block-inverting cell is suppressed there.
                let cursor_col = (!insert_mode
                    && row.line == cur_row
                    && cur_col >= row.start_byte
                    && cur_col <= row.end_byte)
                    .then_some(cur_col);
                let is_first = row.start_byte == 0;
                let styled = self.editor_row(row, cursor_col);
                Some(
                    div()
                        .flex()
                        .flex_row()
                        .h(px(self.line_height))
                        .child(
                            div()
                                .w(px(GUTTER_WIDTH))
                                .flex_none()
                                .text_color(rgb(if row.line == cur_row {
                                    DIM_FG
                                } else {
                                    GUTTER_FG
                                }))
                                // Continuation rows get a blank gutter, so the
                                // numbering still counts logical lines.
                                .child(if is_first {
                                    format!("{:>4} ", row.line + 1)
                                } else {
                                    "     ".to_string()
                                }),
                        )
                        .child(styled),
                )
            })
            .collect();

        // Insert-mode caret, placed in the pane's own coordinate space.
        let insert_caret = insert_mode.then(|| {
            let (vrow, vcol) = self.session.cursor_visual().unwrap_or((0, 0));
            let adv = self.advance.unwrap_or(ADVANCE_FALLBACK);
            let y = vrow.saturating_sub(self.scroll) as f32 * self.line_height;
            div()
                .absolute()
                .left(px(GUTTER_WIDTH + vcol as f32 * adv))
                .top(px(y + 2.0))
                .child(caret(self.line_height - 4.0))
        });

        div()
            .flex()
            .flex_col()
            .flex_grow()
            .relative()
            .overflow_hidden()
            .cursor(CursorStyle::IBeam)
            .on_scroll_wheel(cx.listener(Self::on_wheel))
            .on_mouse_down(MouseButton::Left, cx.listener(Self::on_text_mouse_down))
            .on_mouse_move(cx.listener(Self::on_text_mouse_move))
            .on_mouse_up(MouseButton::Left, cx.listener(Self::on_text_mouse_up))
            .on_mouse_up_out(MouseButton::Left, cx.listener(Self::on_text_mouse_up))
            .when(!has_buffer, |d| {
                d.text_color(rgb(GUTTER_FG))
                    .child("  no file open — Cmd-P, or click one in the sidebar")
            })
            .children(lines)
            .children(insert_caret)
    }

    // ---- terminal surface ----

    fn render_terminal(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let rows = self.rows;
        let start = self.session.term_visible_start(rows);
        let total = self.session.term_total_lines();
        let (cur_row, cur_col, cur_vis) = self.session.term_cursor();
        let running = self.session.term_running();
        let status = self.session.term_status();
        // cursor_row is an absolute scrollback row, so it has to be rebased
        // onto the visible window the same way draw_terminal_panel does.
        let cursor_screen_row = cur_row.checked_sub(start);

        let last = (start + rows).min(total.max(start));
        dbg(format_args!(
            "render_terminal rows={rows} start={start} total={total} last={last} running={running} cursor={cur_row}:{cur_col} vis={cur_vis}"
        ));
        let lines: Vec<_> = (start..last)
            .map(|index| {
                let i = index - start;
                let text = self.session.term_line(index);
                let styles = self.session.term_line_styles(index);

                let mut text = text;
                let mut cells = vec![Cell::default(); text.len() + 1];
                for c in cells.iter_mut() {
                    c.fg = Some(DEFAULT_FG);
                }
                // Both of these are byte offsets: the terminal reports styles
                // in bytes and the cursor in columns, and `cells` is indexed by
                // byte, so the cursor column has to be converted.
                for s in &styles {
                    let end = s.end_byte.min(text.len());
                    for cell in cells.iter_mut().take(end).skip(s.start_byte) {
                        if s.fg != COLOR_DEFAULT {
                            cell.fg = Some(s.fg);
                        }
                        if s.bg != COLOR_DEFAULT {
                            cell.bg = Some(s.bg);
                        }
                    }
                }
                if let Some((sa, sb)) = self.session.term_sel_span(index) {
                    let a = self.session.term_col_to_byte(index, sa);
                    let b = self.session.term_col_to_byte(index, sb).min(text.len());
                    for cell in cells.iter_mut().take(b).skip(a) {
                        cell.bg = Some(SELECTION_BG);
                    }
                }
                if cur_vis && cursor_screen_row == Some(i) {
                    let byte = self.session.term_col_to_byte(index, cur_col);
                    if byte >= text.len() {
                        text.push(' ');
                    }
                    if let Some(cell) = cells.get_mut(byte.min(text.len() - 1)) {
                        cell.invert = true;
                    }
                }

                div()
                    .h(px(self.line_height))
                    .pl(px(8.))
                    .child(styled_line(text, &cells))
            })
            .collect();

        div()
            .flex()
            .flex_col()
            .flex_grow()
            .overflow_hidden()
            .cursor(CursorStyle::IBeam)
            .on_scroll_wheel(cx.listener(|this, ev: &ScrollWheelEvent, _, cx| {
                this.session.term_scroll(-wheel_lines(ev, this.line_height).round() as i32);
                cx.notify();
            }))
            .on_mouse_down(MouseButton::Left, cx.listener(Self::on_term_mouse_down))
            .on_mouse_move(cx.listener(Self::on_term_mouse_move))
            .on_mouse_up(MouseButton::Left, cx.listener(Self::on_term_mouse_up))
            .on_mouse_up_out(MouseButton::Left, cx.listener(Self::on_term_mouse_up))
            .children(lines)
            .when(!running, |d| {
                d.child(
                    div()
                        .pl(px(8.))
                        .text_color(rgb(DIM_FG))
                        .child(format!("[{status}]")),
                )
            })
    }

    // ---- git surface ----

    fn render_git(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let mode = self.session.git_mode();
        let info = self.session.git_info();

        let body = match mode {
            GitMode::RepoSelect => {
                let sel = self.session.git_selected_repo();
                let rows: Vec<_> = self
                    .session
                    .git_repos()
                    .into_iter()
                    .enumerate()
                    .map(|(i, label)| {
                        div()
                            .id(("git-repo", i))
                            .h(px(self.line_height))
                            .px(px(10.))
                            .when(i == sel, |d| d.bg(rgb(SELECTION_BG)))
                            .text_color(rgb(if i == sel { DEFAULT_FG } else { DIM_FG }))
                            .on_click(cx.listener(move |this, _, _, cx| {
                                let cur = this.session.git_selected_repo();
                                this.session.git_move(i as i32 - cur as i32);
                                this.session.git_accept();
                                cx.notify();
                            }))
                            .child(label)
                    })
                    .collect();
                div()
                    .flex()
                    .flex_col()
                    .flex_grow()
                    .overflow_hidden()
                    .child(
                        div()
                            .h(px(self.line_height))
                            .px(px(10.))
                            .text_color(rgb(DIM_FG))
                            .child("select a repository — ↑/↓, Enter"),
                    )
                    .children(rows)
            }
            GitMode::Changes | GitMode::CommitInput => {
                let sel = self.session.git_selected_file();
                let files: Vec<_> = self
                    .session
                    .git_files()
                    .into_iter()
                    .enumerate()
                    .map(|(i, f)| {
                        let staged = f.code.starts_with(['A', 'M', 'D', 'R']);
                        div()
                            .id(("git-file", i))
                            .flex()
                            .flex_row()
                            .gap(px(8.))
                            .h(px(self.line_height))
                            .px(px(10.))
                            .when(i == sel, |d| d.bg(rgb(SELECTION_BG)))
                            .on_click(cx.listener(move |this, _, _, cx| {
                                let cur = this.session.git_selected_file();
                                this.session.git_move(i as i32 - cur as i32);
                                this.session.git_accept();
                                cx.notify();
                            }))
                            .child(
                                div()
                                    .w(px(24.))
                                    .flex_none()
                                    .text_color(rgb(if staged { ADDED_FG } else { REMOVED_FG }))
                                    .child(f.code),
                            )
                            .child(
                                div()
                                    .text_color(rgb(if i == sel { DEFAULT_FG } else { DIM_FG }))
                                    .whitespace_nowrap()
                                    .child(f.path),
                            )
                    })
                    .collect();

                let diff_lines = self.session.git_diff();
                let diff: Vec<_> = diff_lines
                    .into_iter()
                    .enumerate()
                    .map(|(i, l)| {
                        let color = match l.as_bytes().first() {
                            Some(b'+') => ADDED_FG,
                            Some(b'-') => REMOVED_FG,
                            Some(b'@') => DIR_FG,
                            _ => DIM_FG,
                        };
                        let mut cells = vec![Cell::default(); l.len() + 1];
                        for c in cells.iter_mut() {
                            c.fg = Some(color);
                        }
                        // Columns are byte columns here: the diff is plain
                        // ASCII-ish text straight out of `git diff`.
                        if let Some((a, b)) = self.session.git_sel_span(i) {
                            let end = b.min(l.len());
                            for cell in cells.iter_mut().take(end).skip(a) {
                                cell.bg = Some(SELECTION_BG);
                            }
                        }
                        div()
                            .h(px(self.line_height))
                            .px(px(10.))
                            .child(styled_line(l, &cells))
                    })
                    .collect();

                div()
                    .flex()
                    .flex_row()
                    .flex_grow()
                    .overflow_hidden()
                    .child(
                        div()
                            .w(px(GIT_FILES_WIDTH))
                            .flex_none()
                            .flex()
                            .flex_col()
                            .bg(rgb(SIDEBAR_BG))
                            .overflow_hidden()
                            .children(files),
                    )
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .flex_grow()
                            .overflow_hidden()
                            .cursor(CursorStyle::IBeam)
                            .on_scroll_wheel(cx.listener(|this, ev: &ScrollWheelEvent, _, cx| {
                                let lh = this.line_height;
                                this.session
                                    .git_diff_scroll(-wheel_lines(ev, lh).round() as i32);
                                cx.notify();
                            }))
                            .on_mouse_down(MouseButton::Left, cx.listener(Self::on_git_mouse_down))
                            .on_mouse_move(cx.listener(Self::on_git_mouse_move))
                            .on_mouse_up(MouseButton::Left, cx.listener(Self::on_git_mouse_up))
                            .on_mouse_up_out(MouseButton::Left, cx.listener(Self::on_git_mouse_up))
                            .children(diff),
                    )
            }
        };

        div()
            .flex()
            .flex_col()
            .flex_grow()
            .overflow_hidden()
            .child(body)
            .when(mode == GitMode::CommitInput, |d| {
                let msg = self.session.git_message();
                d.child(
                    div()
                        .h(px(self.line_height + 8.))
                        .px(px(10.))
                        .border_t_1()
                        .border_color(rgb(BORDER))
                        .text_color(rgb(DEFAULT_FG))
                        .child(format!("commit: {msg}")),
                )
            })
            .when(!info.is_empty(), |d| {
                d.child(
                    div()
                        .h(px(self.line_height))
                        .px(px(10.))
                        .text_color(rgb(DIM_FG))
                        .child(info),
                )
            })
    }

    // ---- chrome ----

    fn render_sidebar(&mut self, visible: usize, cx: &mut Context<Self>) -> impl IntoElement {
        let total = self.session.ws_count();
        let last = (self.side_scroll + visible).min(total);

        let rows: Vec<_> = (self.side_scroll..last)
            .filter_map(|i| {
                let e = self.session.ws_entry(i)?;
                let is_dir = e.is_dir;
                let collapsed = e.collapsed;
                let color = if is_dir { DIR_FG } else { DEFAULT_FG };
                Some(
                    div()
                        .id(("ws-row", i))
                        .flex()
                        .flex_row()
                        .items_center()
                        .gap(px(6.))
                        .h(px(self.line_height))
                        .pl(px(6.0 + e.depth as f32 * INDENT))
                        .text_color(rgb(color))
                        .hover(|s| s.bg(rgb(TAB_ACTIVE_BG)))
                        .on_click(cx.listener(move |this, ev: &ClickEvent, _, cx| {
                            this.session.ws_activate(i, ev.click_count() >= 2);
                            this.scroll = 0;
                            this.follow_cursor();
                            cx.notify();
                        }))
                        // Disclosure triangle, directories only.
                        .child(
                            div()
                                .w(px(9.))
                                .flex_none()
                                .text_color(rgb(GUTTER_FG))
                                .child(if is_dir {
                                    if collapsed {
                                        "▸"
                                    } else {
                                        "▾"
                                    }
                                } else {
                                    ""
                                }),
                        )
                        .child(if is_dir {
                            folder_icon(DIR_FG).into_any_element()
                        } else {
                            file_icon(0x8b93a1).into_any_element()
                        })
                        .child(div().whitespace_nowrap().child(e.name)),
                )
            })
            .collect();

        div()
            .w(px(SIDEBAR_WIDTH))
            .flex_none()
            .flex()
            .flex_col()
            .bg(rgb(SIDEBAR_BG))
            .overflow_hidden()
            .on_scroll_wheel(cx.listener(Self::on_side_wheel))
            .children(rows)
    }

    fn render_tabs(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let count = self.session.tab_count();
        let active = self.session.tab_active();

        let chips: Vec<_> = (0..count)
            .map(|i| {
                let tab = self.session.tab(i);
                let is_active = i == active;
                let kind = self.session.tab_kind(i);
                let icon = match kind {
                    TabKind::Terminal => "▸ ",
                    TabKind::Git => "± ",
                    TabKind::Editor => "",
                };
                div()
                    .id(("tab", i))
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap(px(6.))
                    .h(px(TAB_HEIGHT))
                    .px(px(10.))
                    .flex_none()
                    .bg(rgb(if is_active { TAB_ACTIVE_BG } else { TAB_BG }))
                    // view_tab_label() already appends " *" when modified, so
                    // the marker is the C core's convention, not ours.
                    .text_color(rgb(match (is_active, tab.modified) {
                        (true, true) => 0xe6c07b,
                        (true, false) => DEFAULT_FG,
                        (false, _) => DIM_FG,
                    }))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.session.tab_set_active(i);
                        this.scroll = 0;
                        this.follow_cursor();
                        cx.notify();
                    }))
                    .child(format!("{icon}{}", tab.label))
                    .child(
                        div()
                            .id(("tab-x", i))
                            .text_color(rgb(DIM_FG))
                            .hover(|s| s.text_color(rgb(DEFAULT_FG)))
                            .on_click(cx.listener(move |this, _, _, cx| {
                                cx.stop_propagation();
                                this.session.tab_close(i);
                                this.scroll = 0;
                                cx.notify();
                            }))
                            .child("×"),
                    )
            })
            .collect();

        div()
            .h(px(TAB_HEIGHT))
            .flex_none()
            .flex()
            .flex_row()
            .bg(rgb(TAB_BG))
            .overflow_hidden()
            .children(chips)
    }

    /// Custom titlebar. The native one is hidden via `appears_transparent`, so
    /// this row also has to provide window dragging and the traffic-light inset.
    fn render_titlebar(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        let root = self.session.ws_root();
        let root_name = root.rsplit('/').next().unwrap_or(&root).to_string();
        let path = self.session.path();

        div()
            .h(px(TITLEBAR_HEIGHT))
            .flex_none()
            .flex()
            .flex_row()
            .items_center()
            .bg(rgb(SIDEBAR_BG))
            .border_b_1()
            .border_color(rgb(BORDER))
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|_, ev: &MouseDownEvent, window: &mut Window, _| {
                    if ev.click_count == 2 {
                        window.zoom_window();
                    } else {
                        window.start_window_move();
                    }
                }),
            )
            .child(div().w(px(TRAFFIC_LIGHT_INSET)).flex_none())
            .child(
                div()
                    .flex()
                    .flex_row()
                    .gap(px(6.))
                    .text_color(rgb(DIM_FG))
                    .child(root_name)
                    .when(!path.is_empty(), |d| {
                        d.child(div().text_color(rgb(GUTTER_FG)).child("—"))
                            .child(div().text_color(rgb(DIM_FG)).child(path))
                    }),
            )
    }

    // ---- overlays ----

    fn render_palette(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 12;
        let total = self.session.palette_count();
        let sel = self.session.palette_selected().min(total.saturating_sub(1));
        let query = self.session.palette_query();
        let first = sel
            .saturating_sub(VISIBLE / 2)
            .min(total.saturating_sub(VISIBLE.min(total)));
        let last = (first + VISIBLE).min(total);

        let rows: Vec<_> = (first..last)
            .filter_map(|i| {
                let e = self.session.palette_entry(i)?;
                let selected = i == sel;
                let dir = e
                    .rel
                    .rsplit_once('/')
                    .map(|(d, _)| d.to_string())
                    .unwrap_or_default();
                Some(
                    div()
                        .id(("palette-row", i))
                        .flex()
                        .flex_row()
                        .items_center()
                        .gap(px(8.))
                        .h(px(self.line_height))
                        .px(px(10.))
                        .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                        .on_click(cx.listener(move |this, _, _, cx| {
                            let cur = this.session.palette_selected();
                            this.session.palette_move(i as i32 - cur as i32);
                            if this.session.palette_accept() {
                                this.scroll = 0;
                                this.follow_cursor();
                            }
                            cx.notify();
                        }))
                        .child(
                            div()
                                .flex_none()
                                .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                                .child(e.name),
                        )
                        .child(
                            div()
                                .text_color(rgb(GUTTER_FG))
                                .whitespace_nowrap()
                                .overflow_hidden()
                                .child(dir),
                        ),
                )
            })
            .collect();

        overlay_panel(
            format!("› {query}"),
            rows,
            format!("{total} files"),
            self.line_height - 6.0,
        )
    }

    fn render_search(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 12;
        let total = self.session.search_count();
        let sel = self.session.search_selected().min(total.saturating_sub(1));
        let query = self.session.search_query();
        let running = self.session.search_running();
        let first = sel
            .saturating_sub(VISIBLE / 2)
            .min(total.saturating_sub(VISIBLE.min(total)));
        let last = (first + VISIBLE).min(total);

        let rows: Vec<_> = (first..last)
            .filter_map(|i| {
                let h = self.session.search_hit(i)?;
                let selected = i == sel;
                Some(
                    div()
                        .id(("search-row", i))
                        .flex()
                        .flex_row()
                        .items_center()
                        .gap(px(8.))
                        .h(px(self.line_height))
                        .px(px(10.))
                        .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                        .on_click(cx.listener(move |this, _, _, cx| {
                            let cur = this.session.search_selected();
                            this.session.search_move(i as i32 - cur as i32);
                            if this.session.search_accept() {
                                this.scroll = 0;
                                this.follow_cursor();
                            }
                            cx.notify();
                        }))
                        .child(
                            div()
                                .flex_none()
                                .text_color(rgb(DIR_FG))
                                .child(format!("{}:{}", h.path, h.line)),
                        )
                        .child(
                            div()
                                .text_color(rgb(if selected { DEFAULT_FG } else { DIM_FG }))
                                .whitespace_nowrap()
                                .overflow_hidden()
                                .child(h.text),
                        ),
                )
            })
            .collect();

        overlay_panel(
            format!("search › {query}"),
            rows,
            if running {
                "searching…".to_string()
            } else {
                format!("{total} matches")
            },
            self.line_height - 6.0,
        )
    }

    /// The completion menu, anchored under the cursor. This is the one place
    /// the layout needs a character advance, since it tracks a text position.
    fn render_completion(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 10;
        let total = self.session.complete_count();
        let sel = self.session.complete_selected().min(total.saturating_sub(1));
        let first = sel
            .saturating_sub(VISIBLE / 2)
            .min(total.saturating_sub(VISIBLE.min(total)));
        let last = (first + VISIBLE).min(total);

        // The anchor must be in the same coordinate space the view scrolls in.
        // `cursor()` returns a logical line and a column within that line;
        // `scroll` counts visual rows. Mixing them puts the menu at an
        // arbitrary offset once any line above the cursor has wrapped.
        let (cur_vrow, cur_vcol) = self.session.cursor_visual().unwrap_or((0, 0));
        let visible_row = cur_vrow.saturating_sub(self.scroll);

        let has_ws = self.session.has_workspace() && self.session.show_sidebar();
        // Measured in render() before this runs.
        let adv = self.advance.unwrap_or(ADVANCE_FALLBACK);
        let x = (if has_ws { SIDEBAR_WIDTH } else { 0.0 }) + GUTTER_WIDTH + cur_vcol as f32 * adv;

        let has_tabs = self.session.tab_count() > 0;
        let top = TITLEBAR_HEIGHT + if has_tabs { TAB_HEIGHT } else { 0.0 };
        let below = top + (visible_row + 1) as f32 * self.line_height;

        // Flip above the cursor when there is not room below, the way
        // complete_layout() does for the C renderer.
        let menu_h = (last - first) as f32 * self.line_height + 8.0;
        let viewport_h = self.viewport_height;
        let y = if below + menu_h > viewport_h - STATUS_HEIGHT && visible_row as f32 * self.line_height > menu_h {
            top + visible_row as f32 * self.line_height - menu_h
        } else {
            below
        };

        let rows: Vec<_> = (first..last)
            .filter_map(|i| {
                let item = self.session.complete_item(i)?;
                let selected = i == sel;
                Some(
                    div()
                        .id(("comp-row", i))
                        .flex()
                        .flex_row()
                        .items_center()
                        .gap(px(8.))
                        .h(px(self.line_height))
                        .px(px(8.))
                        .when(selected, |d| d.bg(rgb(SELECTION_BG)))
                        .on_click(cx.listener(move |this, _, _, cx| {
                            let cur = this.session.complete_selected();
                            this.session.complete_move(i as i32 - cur as i32);
                            this.session.complete_accept();
                            cx.notify();
                        }))
                        .child(
                            div()
                                .w(px(28.))
                                .flex_none()
                                .text_color(rgb(GUTTER_FG))
                                .child(item.kind),
                        )
                        .child(
                            div()
                                .flex_none()
                                .text_color(rgb(if selected { DEFAULT_FG } else { 0xb5bac4 }))
                                .child(item.label),
                        )
                        .when(!item.detail.is_empty(), |d| {
                            d.child(
                                div()
                                    .text_color(rgb(GUTTER_FG))
                                    .whitespace_nowrap()
                                    .overflow_hidden()
                                    .child(item.detail),
                            )
                        }),
                )
            })
            .collect();

        div()
            .absolute()
            .left(px(x))
            .top(px(y))
            .min_w(px(280.))
            .max_w(px(560.))
            .flex()
            .flex_col()
            .bg(rgb(SIDEBAR_BG))
            .border_1()
            .border_color(rgb(BORDER))
            .rounded(px(4.))
            .overflow_hidden()
            .children(rows)
            .when(self.session.complete_loading(), |d| {
                d.child(
                    div()
                        .h(px(self.line_height))
                        .px(px(8.))
                        .text_color(rgb(GUTTER_FG))
                        .child("loading…"),
                )
            })
    }

    fn render_popover(&mut self, cx: &mut Context<Self>) -> impl IntoElement {
        const VISIBLE: usize = 12;
        let text = self.session.popover_text();
        let loading = self.session.popover_loading();
        let lines: Vec<String> = text.lines().map(str::to_string).collect();
        self.session.popover_set_view(lines.len(), VISIBLE);

        div()
            .flex_none()
            .max_h(px(VISIBLE as f32 * self.line_height + 12.))
            .px(px(10.))
            .py(px(6.))
            .bg(rgb(SIDEBAR_BG))
            .border_t_1()
            .border_color(rgb(BORDER))
            .text_color(rgb(0xb5bac4))
            .overflow_hidden()
            .on_scroll_wheel(cx.listener(|this, ev: &ScrollWheelEvent, _, cx| {
                this.session.popover_scroll_by(-wheel_lines(ev, this.line_height).round() as i32);
                cx.notify();
            }))
            .when(loading, |d| {
                d.child(div().text_color(rgb(GUTTER_FG)).child("loading…"))
            })
            .children(lines.into_iter().take(VISIBLE).map(|l| {
                div()
                    .h(px(self.line_height))
                    .whitespace_nowrap()
                    .child(l)
            }))
    }

    // ---- input ----

    fn on_wheel(&mut self, ev: &ScrollWheelEvent, _window: &mut Window, cx: &mut Context<Self>) {
        let line_height = self.line_height;
        let delta = Self::take_lines(&mut self.scroll_accum, ev, line_height);
        if delta == 0 {
            return;
        }
        let total = self.session.visual_rows() as i64;
        let max = (total - self.rows as i64).max(0);
        let next = (self.scroll as i64 + delta).clamp(0, max) as usize;
        if next == self.scroll {
            return;
        }
        self.scroll = next;
        cx.notify();
    }

    fn on_side_wheel(
        &mut self,
        ev: &ScrollWheelEvent,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let line_height = self.line_height;
        let delta = Self::take_lines(&mut self.side_accum, ev, line_height);
        if delta == 0 {
            return;
        }
        let max = (self.session.ws_count() as i64 - 1).max(0);
        let next = (self.side_scroll as i64 + delta).clamp(0, max) as usize;
        if next == self.side_scroll {
            return;
        }
        self.side_scroll = next;
        cx.notify();
    }

    fn apply_flags(&mut self, f: u32, cx: &mut Context<Self>) {
        if f & flags::TAB_NEXT != 0 {
            self.session.tab_goto(1);
        }
        if f & flags::TAB_PREV != 0 {
            self.session.tab_goto(-1);
        }
        // The C core fills its yank register; mirroring it to the system
        // clipboard is the front-end's job, as it is in main.c.
        if f & flags::YANKED != 0 {
            let text = self.session.yank_text();
            if !text.is_empty() {
                cx.write_to_clipboard(ClipboardItem::new_string(text));
            }
        }
    }

    /// GLFW key codes for the pty; see [`ffi::term_key`].
    fn term_key_code(key: &str) -> Option<i32> {
        use ffi::term_key as t;
        Some(match key {
            "escape" => t::ESCAPE,
            "enter" => t::ENTER,
            "tab" => t::TAB,
            "backspace" => t::BACKSPACE,
            "insert" => t::INSERT,
            "delete" => t::DELETE,
            "right" => t::RIGHT,
            "left" => t::LEFT,
            "down" => t::DOWN,
            "up" => t::UP,
            "pageup" => t::PAGE_UP,
            "pagedown" => t::PAGE_DOWN,
            "home" => t::HOME,
            "end" => t::END,
            _ => return None,
        })
    }

    fn special_key(key: &str) -> Option<Key> {
        Some(match key {
            "backspace" => Key::Backspace,
            "delete" => Key::Delete,
            "enter" => Key::Enter,
            "tab" => Key::Tab,
            "left" => Key::Left,
            "right" => Key::Right,
            "up" => Key::Up,
            "down" => Key::Down,
            _ => return None,
        })
    }

    fn on_key(&mut self, ev: &KeyDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        let ks = &ev.keystroke;
        let key = ks.key.as_str();
        let typed = ks.key_char.clone();
        let plain = !ks.modifiers.platform && !ks.modifiers.control;

        dbg(format_args!(
            "key={key:?} char={typed:?} cmd={} ctrl={} shift={} | tabs={} active={} kind={:?} term_active={}",
            ks.modifiers.platform,
            ks.modifiers.control,
            ks.modifiers.shift,
            self.session.tab_count(),
            self.session.tab_active(),
            self.session.tab_kind(self.session.tab_active()),
            self.session.term_active(),
        ));

        // The command palette and the recent-projects list own the keyboard
        // while they are up, ahead of every other surface.
        if self.cmds_open {
            match key {
                "escape" => self.cmds_open = false,
                "enter" => {
                    let items = self.cmds_filtered();
                    if let Some(&cmd) = items.get(self.cmds_sel.min(items.len().saturating_sub(1))) {
                        self.cmds_open = false;
                        self.run_cmd(cmd, window, cx);
                        return;
                    }
                    self.cmds_open = false;
                }
                "up" => self.cmds_sel = self.cmds_sel.saturating_sub(1),
                "down" => {
                    let n = self.cmds_filtered().len();
                    self.cmds_sel = (self.cmds_sel + 1).min(n.saturating_sub(1));
                }
                "backspace" => {
                    self.cmds_query.pop();
                    self.cmds_sel = 0;
                }
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.cmds_query.push_str(&t);
                        self.cmds_sel = 0;
                    }
                }
            }
            cx.notify();
            return;
        }

        if self.fonts_open {
            match key {
                "escape" => self.fonts_open = false,
                "enter" => {
                    let items = self.fonts_filtered(window);
                    if let Some(name) = items.get(self.fonts_sel.min(items.len().saturating_sub(1)))
                    {
                        self.set_font(name.clone());
                    }
                    self.fonts_open = false;
                }
                "up" => self.fonts_sel = self.fonts_sel.saturating_sub(1),
                "down" => {
                    let n = self.fonts_filtered(window).len();
                    self.fonts_sel = (self.fonts_sel + 1).min(n.saturating_sub(1));
                }
                "backspace" => {
                    self.fonts_query.pop();
                    self.fonts_sel = 0;
                }
                "a" if ks.modifiers.control => {
                    self.fonts_all = !self.fonts_all;
                    self.fonts_sel = 0;
                }
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.fonts_query.push_str(&t);
                        self.fonts_sel = 0;
                    }
                }
            }
            cx.notify();
            return;
        }

        if self.settings_open {
            let sel = self.settings_sel.min(Setting::ALL.len() - 1);
            let setting = Setting::ALL[sel];
            match key {
                "escape" => self.settings_open = false,
                "up" | "k" => self.settings_sel = sel.saturating_sub(1),
                "down" | "j" => self.settings_sel = (sel + 1).min(Setting::ALL.len() - 1),
                "left" | "-" => self.adjust_setting(setting, -1),
                "right" | "=" | "+" => self.adjust_setting(setting, 1),
                "enter" | " " | "space" => {
                    if setting == Setting::Font {
                        self.fonts_open = true;
                        self.fonts_query.clear();
                        self.fonts_sel = 0;
                    } else if setting.is_toggle() {
                        self.adjust_setting(setting, 1);
                    }
                }
                "s" => {
                    self.session.save_config();
                    self.status = "settings saved".into();
                }
                "r" => {
                    self.session.reset_config();
                    self.advance = None;
                    self.status = "settings reset".into();
                }
                _ => {}
            }
            cx.notify();
            return;
        }

        if self.recent_open {
            match key {
                "escape" => self.recent_open = false,
                "enter" => {
                    if self.session.recent_accept() {
                        self.session.watch_workspace_start();
                        self.scroll = 0;
                    }
                    self.recent_open = false;
                }
                "up" => self.session.recent_move(-1),
                "down" => self.session.recent_move(1),
                "backspace" => self.session.recent_backspace(),
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.session.recent_input(&t);
                    }
                }
            }
            cx.notify();
            return;
        }

        // A workspace file-operation prompt owns the keyboard while it is up.
        if self.prompt_key(key, typed.clone(), plain) {
            cx.notify();
            return;
        }

        // Empty state: the recent-projects list is the only thing on screen.
        if !self.session.has_workspace() && self.session.tab_count() == 0 {
            match key {
                "up" => self.session.recent_move(-1),
                "down" => self.session.recent_move(1),
                "enter" => {
                    if self.session.recent_accept() {
                        self.session.watch_workspace_start();
                        self.scroll = 0;
                    }
                }
                "backspace" => self.session.recent_backspace(),
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.session.recent_input(&t);
                    }
                }
            }
            cx.notify();
            return;
        }

        // `/` buffer search: live-previews from the original cursor as it grows.
        if self.session.bufsearch_active() {
            match key {
                "escape" => self.session.bufsearch_cancel(),
                "enter" => {
                    self.session.bufsearch_accept();
                    self.center_cursor();
                }
                "backspace" => self.session.bufsearch_backspace(),
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.session.bufsearch_input(&t);
                    }
                }
            }
            self.follow_cursor();
            cx.notify();
            return;
        }

        // Modal surfaces claim input in the same order main.c does: command
        // line, overlays, completion menu, popover, then the active tab.
        if self.session.cmd_active() {
            match key {
                "escape" => self.session.cmd_close(),
                "enter" => match self.session.cmd_accept() {
                    CloseAction::Window => cx.quit(),
                    CloseAction::Tab => {
                        let i = self.session.tab_active();
                        self.session.tab_close(i);
                    }
                    CloseAction::None => {}
                },
                "backspace" => self.session.cmd_backspace(),
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        self.session.cmd_input(&t);
                    }
                }
            }
            cx.notify();
            return;
        }

        if self.session.palette_active() || self.session.search_active() {
            let is_search = self.session.search_active();
            match key {
                "escape" => {
                    if is_search {
                        self.session.palette_close(); // overlay_close covers both
                    } else {
                        self.session.palette_close();
                    }
                }
                "enter" => {
                    let opened = if is_search {
                        self.session.search_accept()
                    } else {
                        self.session.palette_accept()
                    };
                    if opened {
                        self.scroll = 0;
                        self.center_cursor();
                    }
                }
                "up" => {
                    if is_search {
                        self.session.search_move(-1)
                    } else {
                        self.session.palette_move(-1)
                    }
                }
                "down" => {
                    if is_search {
                        self.session.search_move(1)
                    } else {
                        self.session.palette_move(1)
                    }
                }
                "backspace" => {
                    if is_search {
                        self.session.search_backspace()
                    } else {
                        self.session.palette_backspace()
                    }
                }
                _ => {
                    if let (true, Some(t)) = (plain, typed) {
                        if is_search {
                            self.session.search_input(&t);
                        } else {
                            self.session.palette_input(&t);
                        }
                    }
                }
            }
            cx.notify();
            return;
        }

        // Cmd- shortcuts.
        if ks.modifiers.platform {
            match key {
                "s" => {
                    let ok = self.session.save();
                    self.status = if ok { "written".into() } else { "write failed".into() };
                }
                "z" if ks.modifiers.shift => {
                    self.session.redo();
                }
                "z" => {
                    self.session.undo();
                }
                "c" => {
                    // Copy from whichever surface owns the selection.
                    let text = if self.session.term_active() {
                        self.session.term_copy_selection()
                    } else if self.session.git_active() {
                        self.session.git_copy_selection()
                    } else {
                        self.session.selection_text()
                    };
                    if let Some(text) = text {
                        cx.write_to_clipboard(ClipboardItem::new_string(text));
                        self.status = "copied".into();
                    }
                }
                "v" => {
                    let Some(text) = cx
                        .read_from_clipboard()
                        .and_then(|item| item.text())
                        .filter(|t| !t.is_empty())
                    else {
                        return;
                    };
                    if self.session.term_active() {
                        // Bracketed paste is the pty's business; hand it the
                        // bytes and let the shell decide.
                        self.session.term_write(&text);
                    } else {
                        self.session.paste(&text);
                        self.follow_cursor();
                    }
                }
                "w" => {
                    let i = self.session.tab_active();
                    self.session.tab_close(i);
                }
                "]" => self.session.tab_goto(1),
                "[" => self.session.tab_goto(-1),
                "p" if ks.modifiers.shift => {
                    self.cmds_open = true;
                    self.cmds_query.clear();
                    self.cmds_sel = 0;
                }
                "p" => {
                    self.session.palette_open();
                }
                "f" if ks.modifiers.shift => {
                    self.session.search_open();
                }
                "t" => {
                    let ok = self.session.term_open("terminal", "");
                    dbg(format_args!(
                        "term_open -> {ok} tabs={} active={} kind={:?} running={}",
                        self.session.tab_count(),
                        self.session.tab_active(),
                        self.session.tab_kind(self.session.tab_active()),
                        self.session.term_running(),
                    ));
                }
                "g" if ks.modifiers.shift => {
                    self.session.git_open();
                }
                "n" if ks.modifiers.shift => {
                    self.prompt = Prompt::NewDir {
                        dir: self.current_dir(),
                        text: String::new(),
                    };
                }
                "n" => {
                    self.prompt = Prompt::NewFile {
                        dir: self.current_dir(),
                        text: String::new(),
                    };
                }
                "backspace" => {
                    let rel = self.current_rel();
                    if !rel.is_empty() {
                        self.prompt = Prompt::Delete { rel };
                    }
                }
                "b" => {
                    self.session.toggle_sidebar();
                }
                "," => {
                    self.settings_open = true;
                    self.settings_sel = 0;
                }
                "=" | "+" => {
                    self.session.zoom(1);
                }
                "-" => {
                    self.session.zoom(-1);
                }
                "0" => {
                    self.session.zoom(0);
                }
                // Cmd-Shift-Tab / Cmd-Tab also cycle, for muscle memory.
                "tab" => self.session.tab_goto(if ks.modifiers.shift { -1 } else { 1 }),
                _ => return,
            }
            self.follow_cursor();
            cx.notify();
            return;
        }

        // Tab cycling has to be claimed before the terminal, which forwards
        // every key to the pty. Without this, a focused terminal has no
        // keyboard way out at all: `gt`/`gT` never reach edit_command_apply
        // either, so the mouse would be the only escape.
        if key == "tab" && ks.modifiers.control {
            self.session.tab_goto(if ks.modifiers.shift { -1 } else { 1 });
            cx.notify();
            return;
        }

        // Terminal tab: everything goes to the pty. Note this uses GLFW key
        // codes, not EditorKey — terminal_key_sequence speaks a different
        // vocabulary than editor_apply_motion_key.
        if self.session.term_active() {
            let m = &ks.modifiers;
            if let Some(k) = Self::term_key_code(key) {
                self.session.term_key(k, m.shift, m.alt, m.control);
            } else if m.control {
                // Control chords are encoded from the uppercase ASCII letter.
                if let Some(c) = key.chars().next().filter(char::is_ascii_alphabetic) {
                    self.session
                        .term_key(c.to_ascii_uppercase() as i32, m.shift, m.alt, true);
                }
            } else if let Some(t) = typed {
                self.session.term_write(&t);
            }
            cx.notify();
            return;
        }

        // Git tab.
        if self.session.git_active() {
            if self.session.git_mode() == GitMode::CommitInput {
                match key {
                    "escape" => self.session.git_cancel_input(),
                    "enter" => {
                        self.session.git_commit();
                    }
                    "backspace" => {
                        self.session.git_backspace();
                    }
                    _ => {
                        if let (true, Some(t)) = (plain, typed) {
                            self.session.git_insert_text(&t);
                        }
                    }
                }
            } else {
                match key {
                    "j" | "down" => self.session.git_move(1),
                    "k" | "up" => self.session.git_move(-1),
                    "enter" => {
                        self.session.git_accept();
                    }
                    "s" => {
                        self.session.git_stage_toggle();
                    }
                    "c" => {
                        self.session.git_begin_commit();
                    }
                    "r" => {
                        self.session.git_refresh();
                    }
                    _ => {}
                }
            }
            cx.notify();
            return;
        }

        // Escape leaves insert mode in one press, dismissing whatever transient
        // UI is up on the way. Both Wave and vim make you press it twice when a
        // completion popup is open — the popup eats the first one — which is a
        // papercut every time you finish typing an identifier.
        if key == "escape" {
            self.session.complete_close();
            self.session.popover_close();
            self.session.hover_clear();
            self.session.escape();
            self.follow_cursor();
            cx.notify();
            return;
        }

        // Completion menu owns navigation while it is up.
        if self.session.complete_active() {
            match key {
                "up" => {
                    self.session.complete_move(-1);
                    cx.notify();
                    return;
                }
                "down" => {
                    self.session.complete_move(1);
                    cx.notify();
                    return;
                }
                "tab" | "enter" => {
                    self.session.complete_accept();
                    cx.notify();
                    return;
                }
                _ => {}
            }
        }

        // Popover scrolling before the editor sees j/k.
        if self.session.popover_active() {
            match key {
                "up" => {
                    self.session.popover_scroll_by(-1);
                    cx.notify();
                    return;
                }
                "down" => {
                    self.session.popover_scroll_by(1);
                    cx.notify();
                    return;
                }
                _ => {}
            }
        }

        // Control-modified normal-mode keys. key_char is None with ctrl held,
        // so these never reach the codepoint path below.
        if ks.modifiers.control {
            match key {
                // Ctrl-Tab is claimed earlier, above the terminal branch.
                "r" => {
                    self.session.redo();
                }
                "d" => {
                    let half = (self.rows / 2).max(1);
                    for _ in 0..half {
                        self.session.special_key(Key::Down);
                    }
                }
                "u" => {
                    let half = (self.rows / 2).max(1);
                    for _ in 0..half {
                        self.session.special_key(Key::Up);
                    }
                }
                _ => return,
            }
            self.follow_cursor();
            cx.notify();
            return;
        }

        // Shift-Tab cycles tabs backwards outside insert mode, where Tab is
        // still an indent. Ctrl-Tab (above) works in every mode.
        if key == "tab" && ks.modifiers.shift && self.session.mode() != Mode::Insert {
            self.session.tab_goto(-1);
            cx.notify();
            return;
        }

        // Escape was handled above, before the transient surfaces.
        if let Some(k) = Self::special_key(key) {
            self.session.special_key(k);
        } else {
            let Some(ch) = typed.and_then(|s| s.chars().next()) else {
                return;
            };
            let f = self.session.text_input(ch);
            self.apply_flags(f, cx);
        }

        let _ = window;
        self.follow_cursor();
        cx.notify();
    }
}

/// Shared chrome for the centered Cmd-P / Cmd-Shift-F panels.
fn overlay_panel(
    header: String,
    rows: Vec<gpui::Stateful<gpui::Div>>,
    footer: String,
    caret_h: f32,
) -> gpui::Div {
    div()
        .absolute()
        .top_0()
        .left_0()
        .size_full()
        .flex()
        .flex_col()
        .items_center()
        .child(
            div()
                .mt(px(56.))
                .w(px(680.))
                .flex()
                .flex_col()
                .bg(rgb(SIDEBAR_BG))
                .border_1()
                .border_color(rgb(BORDER))
                .rounded(px(6.))
                .overflow_hidden()
                .child(
                    div()
                        .h(px(30.))
                        .flex()
                        .flex_row()
                        .items_center()
                        .px(px(10.))
                        .border_b_1()
                        .border_color(rgb(BORDER))
                        .text_color(rgb(DEFAULT_FG))
                        .child(header)
                        .child(caret(caret_h)),
                )
                .children(rows)
                .child(
                    div()
                        .h(px(20.))
                        .px(px(10.))
                        .text_color(rgb(GUTTER_FG))
                        .child(footer),
                ),
        )
}

impl Render for WaveView {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        // `window.focus()` at construction can be lost if the window was not yet
        // key. Without focus, `on_key_down` never fires and every shortcut looks
        // dead, so re-assert it whenever nothing else has claimed it.
        if window.focused(cx).is_none() {
            window.focus(&self.focus);
            dbg("re-focused root (nothing held focus)");
        }

        self.sync_metrics();

        // Opacity and blur are window-level: painting a translucent background
        // colour does nothing unless the platform window is transparent too.
        let want_bg = match (self.session.blur(), self.session.opacity_pct() < 100) {
            (true, _) => WindowBackgroundAppearance::Blurred,
            (false, true) => WindowBackgroundAppearance::Transparent,
            (false, false) => WindowBackgroundAppearance::Opaque,
        };
        if self.window_bg != want_bg {
            self.window_bg = want_bg;
            window.set_background_appearance(want_bg);
            dbg(format_args!("window background -> {want_bg:?}"));
        }

        let viewport = window.viewport_size();
        self.viewport_height = f32::from(viewport.height);
        let has_tabs = self.session.tab_count() > 0;
        let chrome = TITLEBAR_HEIGHT + STATUS_HEIGHT + if has_tabs { TAB_HEIGHT } else { 0.0 };
        let usable = f32::from(viewport.height) - chrome;
        self.rows = ((usable / self.line_height).floor() as usize).max(1);

        // Only chase the cursor when it actually moved. Doing this every frame
        // clamps `scroll` back into the cursor's viewport, so a wheel gesture
        // could never travel further than the cursor was already visible for.
        // Tracking the last position also covers cursor moves that arrive off
        // the keyboard path, like an LSP go-to-definition landing on a poll.
        let cursor_now = self.session.cursor();
        if cursor_now != self.last_cursor {
            self.last_cursor = cursor_now;
            self.follow_cursor();
        }

        // Keep the pty's idea of its size in step with the pane.
        let adv = self.advance(window);

        // Rebuild the wrap index for the current text width. wrap_build() is a
        // no-op when the column count and buffer are unchanged, so this is
        // cheap to call every frame. The C side honours the config flag.
        let text_width = f32::from(viewport.width)
            - if self.session.has_workspace() && self.session.show_sidebar() {
                SIDEBAR_WIDTH
            } else {
                0.0
            }
            - GUTTER_WIDTH;
        let wrap_cols = (text_width / adv.max(1.0)).floor().max(20.0) as usize;
        self.session.wrap_set_cols(wrap_cols);
        if self.session.term_active() {
            let cols = ((f32::from(viewport.width)
                - if self.session.has_workspace() {
                    SIDEBAR_WIDTH
                } else {
                    0.0
                }
                - 16.0)
                / adv)
                .floor()
                .max(20.0) as usize;
            self.session.term_resize(self.rows, cols);
        }

        let side_rows = self.rows + if has_tabs { 1 } else { 0 };
        let (cur_row, cur_col) = self.session.cursor();
        let has_workspace = self.session.has_workspace();
        let kind = self.session.tab_kind(self.session.tab_active());

        let empty = !has_workspace && self.session.tab_count() == 0;
        let content = if empty {
            self.render_recent_empty(cx).into_any_element()
        } else {
            match kind {
                TabKind::Terminal => self.render_terminal(cx).into_any_element(),
                TabKind::Git => self.render_git(cx).into_any_element(),
                TabKind::Editor => self.render_editor(cx).into_any_element(),
            }
        };

        let mode_color = match self.session.mode() {
            Mode::Normal => 0x7fb069,
            Mode::Insert => 0x5aa9e6,
            Mode::Visual => 0xd98a4f,
        };
        // Mode and cursor are editor concepts; a terminal or git tab should say
        // what *it* is doing instead.
        let (mode_label, mode_color) = match kind {
            TabKind::Terminal => (
                if self.session.term_running() {
                    "TERM".to_string()
                } else {
                    "EXITED".to_string()
                },
                if self.session.term_running() {
                    0x5aa9e6
                } else {
                    DIM_FG
                },
            ),
            TabKind::Git => ("GIT".to_string(), 0xd98a4f),
            TabKind::Editor => (self.session.mode_name(), mode_color),
        };
        let cursor_diag = self.session.cursor_diagnostic();
        let info = self.session.info();
        let detail = match kind {
            TabKind::Terminal => self.session.term_status(),
            TabKind::Git => self.session.git_info(),
            TabKind::Editor => format!(
                "{}:{}{}{}{}",
                cur_row + 1,
                cur_col + 1,
                if self.session.modified() { "  [+]" } else { "" },
                if self.session.lsp_active() { "  lsp" } else { "" },
                if self.status.is_empty() {
                    String::new()
                } else {
                    format!("  {}", self.status)
                }
            ),
        };

        let sidebar = (has_workspace && self.session.show_sidebar())
            .then(|| self.render_sidebar(side_rows, cx));
        let palette = self.session.palette_active().then(|| self.render_palette(cx));
        let cmds = self.cmds_open.then(|| self.render_cmds(cx));
        let recent_overlay = self.recent_open.then(|| self.render_recent_overlay(cx));
        let settings = self.settings_open.then(|| self.render_settings(cx));
        let fonts = self.fonts_open.then(|| self.render_fonts(window, cx));
        let search = self.session.search_active().then(|| self.render_search(cx));
        let completion = (self.session.complete_active() && self.session.complete_count() > 0)
            .then(|| self.render_completion(cx));
        let popover = self.session.popover_active().then(|| self.render_popover(cx));
        let cmd_active = self.session.cmd_active();
        let cmd_text = self.session.cmd_text();
        let search_active = self.session.bufsearch_active();
        let search_text = self.session.bufsearch_text();

        let opacity = self.session.opacity_pct();
        let bg = rgba((BG << 8) | ((opacity * 255 / 100) & 0xff));

        div()
            .key_context("Wave")
            .track_focus(&self.focus)
            .on_key_down(cx.listener(Self::on_key))
            .size_full()
            .relative()
            .flex()
            .flex_col()
            .bg(bg)
            .font_family(self.fe_config.font.clone())
            .text_size(px(self.text_size))
            .line_height(px(self.line_height))
            .child(self.render_titlebar(cx))
            .child(
                div()
                    .flex()
                    .flex_row()
                    .flex_grow()
                    .overflow_hidden()
                    .children(sidebar)
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .flex_grow()
                            .overflow_hidden()
                            .when(has_tabs, |d| {
                                let tabs = self.render_tabs(cx);
                                d.child(tabs)
                            })
                            .child(content),
                    ),
            )
            .children(popover)
            .child(if let Some(prompt) = self.prompt_line() {
                div()
                    .h(px(STATUS_HEIGHT))
                    .flex_none()
                    .flex()
                    .flex_row()
                    .items_center()
                    .px(px(8.))
                    .bg(rgb(STATUS_BG))
                    .text_color(rgb(0xe6c07b))
                    .child(prompt)
                    .child(caret(self.line_height - 6.0))
            } else if cmd_active || search_active {
                div()
                    .h(px(STATUS_HEIGHT))
                    .flex_none()
                    .flex()
                    .flex_row()
                    .items_center()
                    .px(px(8.))
                    .bg(rgb(STATUS_BG))
                    .text_color(rgb(DEFAULT_FG))
                    .child(if cmd_active {
                        format!(":{cmd_text}")
                    } else {
                        format!("/{search_text}")
                    })
                    .child(caret(self.line_height - 6.0))
            } else {
                div()
                    .h(px(STATUS_HEIGHT))
                    .flex_none()
                    .flex()
                    .flex_row()
                    .gap(px(12.))
                    .bg(rgb(STATUS_BG))
                    .text_color(rgb(DIM_FG))
                    .child(
                        div()
                            .pl(px(8.))
                            .text_color(rgb(mode_color))
                            .child(mode_label),
                    )
                    .child(div().child(detail))
                    .when(!info.is_empty(), |d| {
                        d.child(div().text_color(rgb(DEFAULT_FG)).child(info))
                    })
                    .when(!cursor_diag.is_empty(), |d| {
                        d.child(
                            div()
                                .text_color(rgb(DIAGNOSTIC))
                                .whitespace_nowrap()
                                .overflow_hidden()
                                .child(cursor_diag),
                        )
                    })
            })
            .children(palette)
            .children(search)
            .children(completion)
            .children(cmds)
            .children(recent_overlay)
            .children(settings)
            .children(fonts)
    }
}

/// Exercise the terminal exactly as the UI does — same `Session` calls, same
/// key codes, same visible-window arithmetic as `render_terminal` — but with no
/// window, so "the terminal doesn't work" is answerable without a GUI.
fn selftest(root: &str) {
    use ffi::term_key as tk;

    // Config commands persist to $HOME/.config/wave/config, and every Session
    // loads it. Redirect HOME before *anything* is constructed so no part of
    // the run can read or write the user's real settings.
    let home = std::env::temp_dir().join("wave-selftest-home");
    let _ = std::fs::create_dir_all(home.join(".config/wave"));
    std::env::set_var("HOME", &home);
    println!("(isolated HOME={})", home.display());

    let mut s = Session::new();
    println!("open {root}: {:?}", s.open(std::path::Path::new(root)));
    println!(
        "workspace={} tabs={} kind={:?}",
        s.has_workspace(),
        s.tab_count(),
        s.tab_kind(s.tab_active())
    );

    // `:` must reach the command line even with no buffer open — otherwise the
    // tab-spawning commands are unreachable from a freshly opened folder.
    println!(
        "no buffer: has_buffer={} typing ':' -> cmd_active={}",
        s.has_buffer(),
        {
            s.text_input(':');
            s.cmd_active()
        }
    );
    s.cmd_close();

    // The `:term` path, which does not depend on a keyboard shortcut reaching us.
    s.cmd_open();
    for ch in "term".chars() {
        s.cmd_input(&ch.to_string());
    }
    let close = s.cmd_accept();
    println!(
        ":term -> close={close:?} tabs={} kind={:?} info={:?}",
        s.tab_count(),
        s.tab_kind(s.tab_active()),
        s.info()
    );

    // What Cmd-T does.
    let opened = s.term_open("terminal", "");
    println!(
        "term_open={opened} tabs={} active={} kind={:?} term_active={} running={}",
        s.tab_count(),
        s.tab_active(),
        s.tab_kind(s.tab_active()),
        s.term_active(),
        s.term_running()
    );

    // What the render pass does before drawing.
    let rows = 30usize;
    s.term_resize(rows, 100);

    let pump = |s: &mut Session, ms: usize| {
        for _ in 0..(ms / 20) {
            s.term_poll();
            std::thread::sleep(Duration::from_millis(20));
        }
    };
    pump(&mut s, 1200);

    // What the key handler does for "echo selftest-ok" + Enter.
    for ch in "echo selftest-ok".chars() {
        s.term_write(&ch.to_string());
    }
    pump(&mut s, 300);
    s.term_key(tk::ENTER, false, false, false);
    pump(&mut s, 1000);

    // What render_terminal computes.
    let start = s.term_visible_start(rows);
    let total = s.term_total_lines();
    let last = (start + rows).min(total.max(start));
    let (cur_row, cur_col, cur_vis) = s.term_cursor();
    println!(
        "render: rows={rows} start={start} total={total} last={last} cursor={cur_row}:{cur_col} vis={cur_vis} -> screen_row={:?}",
        cur_row.checked_sub(start)
    );

    // Multi-byte lines are where column/byte indexing diverges — a TUI like
    // Claude Code is full of them, and mixing the two misplaces the cursor.
    s.term_write("printf '\\u203a\\u203a\\u203aabc\\n'");
    s.term_key(tk::ENTER, false, false, false);
    pump(&mut s, 900);
    let total2 = s.term_total_lines();
    for index in 0..total2 {
        let line = s.term_line(index);
        if line.starts_with("\u{203a}\u{203a}\u{203a}abc") {
            println!(
                "multibyte line {index:?}: chars={} bytes={} col_to_byte(3)={} (want 9) col_to_byte(4)={} (want 10)",
                line.chars().count(),
                line.trim_end().len(),
                s.term_col_to_byte(index, 3),
                s.term_col_to_byte(index, 4),
            );
            break;
        }
    }

    let mut shown = 0;
    let mut found = false;
    for index in start..last {
        let line = s.term_line(index);
        if !line.trim().is_empty() && shown < 8 {
            println!("  [{index}] {}", line.trim_end());
            shown += 1;
        }
        if line.contains("selftest-ok") {
            found = true;
        }
    }
    println!("rendered {} lines; echo output present: {found}", last - start);

    // ---- mouse selection on a buffer ----
    let mut e = Session::new();
    if e.open(std::path::Path::new("src/piece_table.c")).is_ok() {
        println!("\n=== mouse selection ===");
        e.click_at(20, 4);
        println!(
            "click(20,4) -> cursor={:?} has_selection={}",
            e.cursor(),
            e.has_selection()
        );
        e.drag_to(22, 10);
        println!(
            "drag(22,10) -> cursor={:?} has_selection={}",
            e.cursor(),
            e.has_selection()
        );
        for line in 20..=22 {
            println!("  line {line} selection cols: {:?}", e.line_selection(line));
        }
        match e.selection_text() {
            Some(t) => println!("selected {} bytes: {:?}", t.len(), t.replace('\n', "\\n")),
            None => println!("selection_text: None"),
        }
        e.click_at(20, 4);
        println!("click again -> has_selection={}", e.has_selection());

        // ---- one Escape leaves insert mode, even with a completion menu up ----
        println!("\n=== escape ===");
        e.text_input('o'); // open a line below -> INSERT
        for ch in "pt_l".chars() {
            e.text_input(ch);
        }
        for _ in 0..20 {
            e.lsp_poll();
            std::thread::sleep(Duration::from_millis(50));
        }
        println!(
            "typing in insert: mode={:?} completion_open={} items={}",
            e.mode(),
            e.complete_active(),
            e.complete_count()
        );
        // Exactly what the key handler does for "escape".
        e.complete_close();
        e.popover_close();
        e.hover_clear();
        e.escape();
        println!(
            "after ONE escape: mode={:?} completion_open={}",
            e.mode(),
            e.complete_active()
        );

        // ---- config: the `:` commands and the zoom/sidebar toggles ----
        //
        // `:opacity`, `:radius` and `:blur` set save_config, which writes to
        // $HOME/.config/wave/config. Point HOME at a scratch dir so the test
        // cannot scribble on real settings.
        println!("\n=== config ===");
        let mut e = Session::new();
        let run = |e: &mut Session, text: &str| {
            e.cmd_open();
            for ch in text.chars() {
                e.cmd_input(&ch.to_string());
            }
            e.cmd_accept();
        };
        println!(
            "start: opacity={}% blur={} radius={} base_pt={} sidebar={} wrap={}",
            e.opacity_pct(),
            e.blur(),
            e.radius(),
            e.base_pt(),
            e.show_sidebar(),
            e.wrap()
        );
        // command.c accepts a fraction in [0.2, 1.0], not a percentage.
        run(&mut e, "opacity 0.8");
        println!("after ':opacity 0.8' -> {}%", e.opacity_pct());
        run(&mut e, "opacity 80");
        println!("after ':opacity 80' (out of range) -> {}%", e.opacity_pct());
        run(&mut e, "blur");
        println!("after ':blur' -> blur={}", e.blur());
        run(&mut e, "radius 12");
        println!("after ':radius 12' -> radius={}", e.radius());

        let before = e.base_pt();
        e.zoom(1);
        e.zoom(1);
        println!("zoom in twice: effective pt {before} -> {}", e.base_pt());
        e.zoom(0);
        println!("zoom reset: effective pt -> {}", e.base_pt());

        let s0 = e.show_sidebar();
        e.toggle_sidebar();
        println!("toggle sidebar: {s0} -> {}", e.show_sidebar());
        let w0 = e.wrap();
        e.toggle_wrap();
        println!("toggle wrap: {w0} -> {}", e.wrap());

        // ---- command palette filtering ----
        println!("\n=== command palette ===");
        for q in ["", "open", "of", "zoom", "tab", "git", "xyzzy"] {
            let hits: Vec<&str> = Cmd::ALL
                .into_iter()
                .filter(|c| c.matches(q))
                .map(|c| c.label())
                .collect();
            println!("  {:8} -> {} {:?}", format!("{q:?}"), hits.len(), &hits[..hits.len().min(4)]);
        }

        // ---- paste ----
        println!("\n=== paste ===");
        let mut p = Session::new();
        if p.open(std::path::Path::new("src/piece_table.c")).is_ok() {
            p.click_at(0, 0);
            let before = p.line_text(0);
            p.paste("PASTED-");
            println!("line 0: {:?}", p.line_text(0));
            println!("  (was {:?})", before);
            // Paste over a selection replaces it.
            p.click_at(1, 0);
            p.drag_to(1, 8);
            let sel = p.selection_text().unwrap_or_default();
            p.paste("XX");
            println!("replaced {sel:?} -> line 1 now {:?}", p.line_text(1));
        }

        // ---- soft wrap ----
        println!("\n=== soft wrap ===");
        let mut w = Session::new();
        if w.open(std::path::Path::new("src/piece_table.c")).is_ok() {
            w.wrap_set_cols(0); // wrapping off
            let unwrapped = w.visual_rows();
            w.wrap_set_cols(40);
            let wrapped = w.visual_rows();
            println!(
                "lines={} visual rows: nowrap={} wrapped@40={}",
                w.line_count(),
                unwrapped,
                wrapped
            );
            for vrow in 20..24 {
                if let Some(r) = w.visual_row(vrow) {
                    let text = w.line_text(r.line);
                    let slice = &text[r.start_byte.min(text.len())..r.end_byte.min(text.len())];
                    println!("  vrow {vrow} -> line {} [{}..{}] {:?}", r.line, r.start_byte, r.end_byte, slice);
                }
            }
            println!("cursor_visual at start: {:?}", w.cursor_visual());

            // The bug the completion menu hit: once anything above the cursor
            // wraps, the logical line and the visual row diverge.
            w.click_at(60, 4);
            println!(
                "at logical line 60: cursor()={:?} cursor_visual()={:?}",
                w.cursor(),
                w.cursor_visual()
            );
        }
    }
}

/// Geist Mono, compiled into the binary so a fresh clone ships with a real
/// monospace font and no install step. SIL Open Font License; see
/// `assets/fonts/OFL.txt`.
const BUNDLED_FONTS: [&[u8]; 4] = [
    include_bytes!("../assets/fonts/GeistMono-Regular.otf"),
    include_bytes!("../assets/fonts/GeistMono-Italic.otf"),
    include_bytes!("../assets/fonts/GeistMono-Bold.otf"),
    include_bytes!("../assets/fonts/GeistMono-BoldItalic.otf"),
];

fn load_bundled_fonts(text_system: &std::sync::Arc<gpui::TextSystem>) {
    let blobs = BUNDLED_FONTS
        .iter()
        .map(|b| std::borrow::Cow::Borrowed(*b))
        .collect();
    match text_system.add_fonts(blobs) {
        Ok(()) => dbg("bundled Geist Mono registered"),
        // A missing bundled font is not fatal: resolve_font falls back, and the
        // picker still lists whatever the system has.
        Err(e) => dbg(format_args!("bundled fonts failed: {e}")),
    }
}

/// Load any fonts dropped into `~/.config/wave/fonts` so they can be selected
/// without installing them system-wide.
///
/// GPUI can register fonts from memory, which is how a font like Geist Mono
/// gets used without going through Font Book: drop the `.otf`/`.ttf` files in
/// that directory and they join the picker on next launch.
fn load_user_fonts(text_system: &std::sync::Arc<gpui::TextSystem>) -> usize {
    let Some(home) = std::env::var_os("HOME") else {
        return 0;
    };
    let dir = PathBuf::from(home).join(".config/wave/fonts");
    let Ok(entries) = std::fs::read_dir(&dir) else {
        return 0;
    };

    let mut blobs: Vec<std::borrow::Cow<'static, [u8]>> = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        let ext = path
            .extension()
            .and_then(|e| e.to_str())
            .map(str::to_lowercase);
        if !matches!(ext.as_deref(), Some("ttf" | "otf" | "ttc")) {
            continue;
        }
        match std::fs::read(&path) {
            Ok(bytes) => blobs.push(std::borrow::Cow::Owned(bytes)),
            Err(e) => dbg(format_args!("font {}: {e}", path.display())),
        }
    }

    let count = blobs.len();
    if count > 0 {
        match text_system.add_fonts(blobs) {
            Ok(()) => dbg(format_args!("loaded {count} font file(s) from {}", dir.display())),
            Err(e) => {
                dbg(format_args!("add_fonts failed: {e}"));
                return 0;
            }
        }
    }
    count
}

/// List the monospace families the picker will offer, without opening a window.
fn list_fonts() {
    Application::new().run(|cx: &mut App| {
        let ts = cx.text_system();
        load_bundled_fonts(&ts);
        let loaded = load_user_fonts(&ts);
        let size = px(TEXT_SIZE_DEFAULT);
        let all = ts.all_font_names();
        if loaded > 0 {
            println!("loaded {loaded} font file(s) from ~/.config/wave/fonts");
        }
        let mut mono: Vec<String> = all
            .iter()
            .filter(|name| is_monospace(&ts, size, name))
            .cloned()
            .collect();
        mono.sort_by_key(|n| n.to_lowercase());
        mono.dedup();
        println!("{} families installed, {} monospace", all.len(), mono.len());

        // Bold/italic must advance identically to regular, or mixing them in a
        // shaped line changes its width and the text shifts.
        println!("\nbold/italic advance parity (emphasis shifts text if these differ):");
        for family in &mono {
            let widths: Vec<f32> = [
                (gpui::FontWeight::NORMAL, gpui::FontStyle::Normal),
                (gpui::FontWeight::BOLD, gpui::FontStyle::Normal),
                (gpui::FontWeight::NORMAL, gpui::FontStyle::Italic),
            ]
            .into_iter()
            .map(|(w, st)| {
                let mut f = gpui::font(family.clone());
                f.weight = w;
                f.style = st;
                let id = ts.resolve_font(&f);
                ts.advance(id, size, 'M').map(|s| f32::from(s.width)).unwrap_or(-1.0)
            })
            .collect();
            let ok = widths.windows(2).all(|w| (w[0] - w[1]).abs() < 0.01);
            println!("  {:14} {:?} {}", family, widths, if ok { "ok" } else { "MISMATCH" });
        }

        let family = frontend_config::FrontendConfig::load().font;
        println!("\nadvance check for {family:?}:");
        for (label, weight, style) in [
            ("regular", gpui::FontWeight::NORMAL, gpui::FontStyle::Normal),
            ("bold", gpui::FontWeight::BOLD, gpui::FontStyle::Normal),
            ("italic", gpui::FontWeight::NORMAL, gpui::FontStyle::Italic),
            ("bolditalic", gpui::FontWeight::BOLD, gpui::FontStyle::Italic),
        ] {
            let mut f = gpui::font(family.clone());
            f.weight = weight;
            f.style = style;
            let id = ts.resolve_font(&f);
            let w = ts.advance(id, size, 'M').map(|s| f32::from(s.width)).unwrap_or(-1.0);
            println!("  {label:11} advance = {w}");
        }
        for name in mono.iter().take(20) {
            println!("  {name}");
        }
        cx.quit();
    });
}

fn main() {
    let mut args = std::env::args().skip(1);
    let first = args.next();
    if first.as_deref() == Some("--fonts") {
        list_fonts();
        return;
    }
    if first.as_deref() == Some("--selftest") {
        selftest(&args.next().unwrap_or_else(|| ".".into()));
        return;
    }
    let path = first.map(PathBuf::from);

    Application::new().run(move |cx: &mut App| {
        // Register fonts before any window measures text.
        let ts = cx.text_system();
        load_bundled_fonts(&ts);
        load_user_fonts(&ts);

        // Without a registered menu + keybinding, macOS has no Quit item and
        // Cmd-Q does nothing — GPUI installs no default app menu.
        cx.on_action(|_: &Quit, cx: &mut App| cx.quit());
        cx.bind_keys([KeyBinding::new("cmd-q", Quit, None)]);
        cx.set_menus(vec![Menu {
            name: "Wave".into(),
            items: vec![
                MenuItem::os_submenu("Services", SystemMenuType::Services),
                MenuItem::separator(),
                MenuItem::action("Quit Wave", Quit),
            ],
        }]);

        let bounds = Bounds::centered(None, size(px(1180.), px(760.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                // Hide the system titlebar and draw our own, as Wave does.
                titlebar: Some(TitlebarOptions {
                    title: Some("Wave".into()),
                    appears_transparent: true,
                    traffic_light_position: Some(point(px(14.), px(11.))),
                }),
                ..Default::default()
            },
            |window, cx| {
                cx.new(|cx| {
                    let focus = cx.focus_handle();
                    window.focus(&focus);
                    window.activate_window();

                    // Server replies, pty output and ripgrep results all arrive
                    // asynchronously and only surface when polled.
                    cx.spawn(async move |this, cx| loop {
                        Timer::after(POLL).await;
                        let polled = this.update(cx, |this: &mut WaveView, cx| {
                            let mut dirty = this.session.lsp_poll();
                            if this.session.term_poll() {
                                dirty = true;
                            }
                            if this.session.search_poll() {
                                dirty = true;
                            }
                            if let Some(message) = this.session.watch_poll(now_secs()) {
                                if !message.is_empty() {
                                    this.status = message;
                                }
                                dirty = true;
                            }
                            if dirty {
                                cx.notify();
                            }
                        });
                        if polled.is_err() {
                            break;
                        }
                    })
                    .detach();

                    WaveView::new(path.clone(), focus)
                })
            },
        )
        .unwrap();
        cx.activate(true);
    });
}
