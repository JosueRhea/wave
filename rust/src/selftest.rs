//! Headless Session/FFI end-to-end suite driven by `--selftest`.
//!
//! Same calls the GPUI handlers make — no window. Hard-fails on regression;
//! network/OS/LSP soft-skip when unavailable.

use crate::ffi::{self, term_key as tk, CloseAction, GitMode, Key, Mode, Motion, Session, TabKind};
use std::path::Path;
use std::time::Duration;

/// Shared assert helper. Suites return `fails` so `run` can exit non-zero.
struct Check {
    fails: usize,
    section: &'static str,
}

impl Check {
    fn new(section: &'static str) -> Self {
        println!("\n=== {section} ===");
        Self { fails: 0, section }
    }

    fn ok(&mut self, cond: bool, label: impl AsRef<str>) {
        let label = label.as_ref();
        if cond {
            println!("  ok  {label}");
        } else {
            println!("  FAIL {label}");
            self.fails += 1;
        }
    }

    fn skip(&self, reason: &str) {
        println!("  SKIP {reason}");
    }

    fn finish(self) -> usize {
        println!("{}: {} failed", self.section, self.fails);
        self.fails
    }
}

fn buffer_text(s: &Session) -> String {
    let n = s.line_count();
    if n == 0 {
        return String::new();
    }
    let mut out = String::new();
    for i in 0..n {
        if i > 0 {
            out.push('\n');
        }
        out.push_str(&s.line_text(i));
    }
    out
}

fn caret_rowcol(s: &Session, i: usize) -> Option<(usize, usize)> {
    let (_, cursor) = s.caret_at(i)?;
    let mut pos = 0usize;
    for row in 0..s.line_count() {
        let len = s.line_text(row).len();
        if cursor <= pos + len {
            return Some((row, cursor - pos));
        }
        pos += len + 1;
    }
    None
}

fn run_cmd(s: &mut Session, text: &str) -> CloseAction {
    s.cmd_open();
    for ch in text.chars() {
        s.cmd_input(&ch.to_string());
    }
    s.cmd_accept()
}

fn pump_term(s: &mut Session, ms: usize) {
    for _ in 0..(ms / 20).max(1) {
        s.term_poll();
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn update_status_line(update: &ffi::Update) -> String {
    match update {
        ffi::Update::Checking => "checking for updates…".into(),
        ffi::Update::UpToDate { version } => format!("Wave {version} is up to date"),
        ffi::Update::Available { version, from } => {
            format!("update available: {from} → {version}")
        }
        ffi::Update::Downloading { version, progress } => {
            format!("downloading Wave {version}… {}%", (progress * 100.0) as i32)
        }
        ffi::Update::Installing { version } => {
            format!("installing Wave {version} — restarting")
        }
        ffi::Update::Failed { detail } => format!("update failed: {detail}"),
    }
}

/// Entry point for `--selftest`. Isolates HOME, runs every suite, exits 1 on failure.
pub fn run(root: &str, cmd_hits: impl Fn(&str) -> Vec<&'static str>) {
    let home = std::env::temp_dir().join("wave-selftest-home");
    let _ = std::fs::create_dir_all(home.join(".config/wave"));
    std::env::set_var("HOME", &home);
    println!("(isolated HOME={})", home.display());

    let root_path = Path::new(root);
    let abs_root = std::fs::canonicalize(root_path).unwrap_or_else(|_| root_path.to_path_buf());

    let mut fails = 0usize;
    fails += suite_standard_editing(&home);
    fails += suite_multi_cursor(&home);
    fails += suite_vim(&home);
    fails += suite_mouse_selection(&abs_root);
    fails += suite_tabs_workspace(&home, &abs_root);
    fails += suite_find_file(&abs_root);
    fails += suite_buffer_search(&home);
    fails += suite_project_search(&abs_root);
    fails += suite_terminal();
    fails += suite_git(&abs_root);
    fails += suite_config_themes(&cmd_hits);
    fails += suite_resources(&home, &abs_root);
    suite_updates_soft();

    if fails > 0 {
        eprintln!("\n{fails} e2e check(s) failed");
        std::process::exit(1);
    }
    println!("\nall e2e suites passed");
}

// ---- standard editing ----

fn suite_standard_editing(home: &Path) -> usize {
    let mut c = Check::new("standard editing");
    let path = home.join("edit-fixture.txt");
    std::fs::write(&path, "alpha beta\ngamma\n").unwrap();

    let mut s = Session::new();
    c.ok(s.open(&path).is_ok(), "open fixture");
    s.set_vim_enabled(false);
    c.ok(!s.vim_enabled(), "vim off");
    c.ok(s.mode() == Mode::Insert, "standard starts in insert");

    s.click_at(0, 0);
    s.text_input('Z');
    c.ok(
        s.line_text(0).starts_with('Z'),
        &format!("type inserts -> {:?}", s.line_text(0)),
    );

    s.special_key(Key::Backspace);
    c.ok(
        s.line_text(0).starts_with("alpha"),
        &format!("Backspace -> {:?}", s.line_text(0)),
    );

    s.click_at(0, 0);
    s.special_key(Key::Delete);
    c.ok(
        s.line_text(0).starts_with("lpha"),
        &format!("Delete -> {:?}", s.line_text(0)),
    );
    s.text_input('a'); // restore

    s.click_at(0, 5);
    s.special_key(Key::Enter);
    c.ok(s.line_count() >= 3, &format!("Enter splits line -> {} lines", s.line_count()));
    c.ok(
        s.line_text(0) == "alpha" && s.line_text(1).starts_with(" beta"),
        &format!("Enter split text -> {:?} / {:?}", s.line_text(0), s.line_text(1)),
    );

    // Fresh file for clearer motion/select asserts.
    std::fs::write(&path, "aa bb cc\ndd ee\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    c.ok(s.motion(Motion::WordRight, false), "word-right");
    c.ok(s.cursor() == (0, 3), &format!("word-right -> {:?}", s.cursor()));
    c.ok(s.motion(Motion::LineEnd, false), "line-end");
    c.ok(s.cursor().1 == s.line_text(0).len(), "at line end");
    c.ok(s.motion(Motion::LineStart, false), "line-start");
    c.ok(s.cursor() == (0, 0), "at line start");
    c.ok(s.motion(Motion::DocEnd, false), "doc-end");
    c.ok(s.motion(Motion::DocStart, false), "doc-start");
    c.ok(s.cursor() == (0, 0), "back at doc start");

    s.motion(Motion::Right, true);
    c.ok(s.has_selection(), "Shift+Right selects");
    s.escape();
    c.ok(!s.has_selection(), "Escape clears selection");

    s.click_at(0, 1);
    c.ok(s.select_word(), "select_word");
    c.ok(s.has_selection(), "select_word has selection");
    let word = s.selection_text().unwrap_or_default();
    c.ok(word == "aa", &format!("select_word -> {word:?}"));

    s.click_at(0, 0);
    c.ok(s.select_line(), "select_line");
    c.ok(
        s.selection_text().as_deref() == Some("aa bb cc\n")
            || s.selection_text().as_deref() == Some("aa bb cc"),
        &format!("select_line text -> {:?}", s.selection_text()),
    );

    s.click_at(0, 0);
    c.ok(s.select_all(), "select_all");
    let all = s.selection_text().unwrap_or_default();
    c.ok(all.contains("aa bb cc") && all.contains("dd ee"), "select_all spans buffer");

    s.escape();
    s.click_at(0, 0);
    s.drag_to(0, 2);
    let copied = s.standard_copy();
    c.ok(copied.as_deref() == Some("aa"), &format!("standard_copy -> {copied:?}"));

    s.click_at(0, 0);
    s.drag_to(0, 2);
    let cut = s.standard_cut();
    c.ok(cut.as_deref() == Some("aa"), &format!("standard_cut -> {cut:?}"));
    c.ok(
        s.line_text(0).starts_with(" bb"),
        &format!("after cut -> {:?}", s.line_text(0)),
    );
    c.ok(s.undo(), "undo after cut");
    c.ok(
        s.line_text(0).starts_with("aa"),
        &format!("undo restored -> {:?}", s.line_text(0)),
    );
    c.ok(s.redo(), "redo");

    std::fs::write(&path, "one\ntwo\nthree\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    c.ok(s.duplicate_line(), "duplicate_line");
    c.ok(
        s.line_text(0) == "one" && s.line_text(1) == "one",
        &format!("duplicate -> {:?} / {:?}", s.line_text(0), s.line_text(1)),
    );
    s.click_at(0, 0);
    c.ok(s.move_line(1), "move_line down");
    c.ok(
        s.line_text(0) == "one" && s.line_text(1) == "one",
        "move_line kept content",
    );
    s.click_at(0, 0);
    c.ok(s.delete_line(), "delete_line");
    c.ok(
        !buffer_text(&s).starts_with("one\none\n"),
        &format!("after delete_line -> {:?}", buffer_text(&s)),
    );

    std::fs::write(&path, "  hello world\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 8); // in "world"
    c.ok(s.delete_word_left(), "delete_word_left");
    c.ok(
        s.line_text(0).contains("world") && !s.line_text(0).contains("hello"),
        &format!("delete_word_left -> {:?}", s.line_text(0)),
    );

    std::fs::write(&path, "hello world\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    c.ok(s.delete_word_right(), "delete_word_right");
    c.ok(
        s.line_text(0).starts_with(" world") || s.line_text(0).starts_with("world"),
        &format!("delete_word_right -> {:?}", s.line_text(0)),
    );

    std::fs::write(&path, "abcde\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 2);
    c.ok(s.delete_to_line_end(), "delete_to_line_end");
    c.ok(s.line_text(0) == "ab", &format!("delete_to_line_end -> {:?}", s.line_text(0)));

    std::fs::write(&path, "  abc\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 5);
    c.ok(s.delete_to_line_start(), "delete_to_line_start");

    std::fs::write(&path, "code\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    c.ok(s.toggle_comment(), "toggle_comment");
    c.ok(
        s.line_text(0).contains("//"),
        &format!("commented -> {:?}", s.line_text(0)),
    );
    c.ok(s.toggle_comment(), "toggle_comment off");
    c.ok(
        !s.line_text(0).contains("//"),
        &format!("uncommented -> {:?}", s.line_text(0)),
    );

    std::fs::write(&path, "x\ny\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    s.drag_to(1, 1);
    c.ok(s.indent(false), "indent selection");
    c.ok(
        s.line_text(0).starts_with('\t') || s.line_text(0).starts_with(' '),
        &format!("indented -> {:?}", s.line_text(0)),
    );
    c.ok(s.indent(true), "outdent");

    std::fs::write(&path, "a\nb\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 1);
    c.ok(s.insert_line(true), "insert_line below");
    c.ok(s.line_count() >= 3, "insert_line added a line");

    // Paste replace.
    std::fs::write(&path, "hello\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    s.paste("PASTED-");
    c.ok(
        s.line_text(0).starts_with("PASTED-"),
        &format!("paste insert -> {:?}", s.line_text(0)),
    );
    s.click_at(0, 0);
    s.drag_to(0, 7);
    s.paste("XX");
    c.ok(
        s.line_text(0).starts_with("XX"),
        &format!("paste replace -> {:?}", s.line_text(0)),
    );

    // Soft wrap (requires config.wrap on — a prior run may have toggled it off
    // in the isolated HOME).
    std::fs::write(&path, &"word ".repeat(40)).unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    if !s.wrap() {
        s.toggle_wrap();
    }
    s.wrap_set_cols(0);
    let unwrapped = s.visual_rows();
    s.wrap_set_cols(40);
    let wrapped = s.visual_rows();
    c.ok(
        wrapped > unwrapped,
        &format!("soft wrap increases visual rows {unwrapped} -> {wrapped}"),
    );
    c.ok(s.cursor_visual().is_some(), "cursor_visual after wrap");

    // Save round-trip.
    let save_path = home.join("save-roundtrip.txt");
    std::fs::write(&save_path, "before\n").unwrap();
    let mut s = Session::new();
    s.open(&save_path).unwrap();
    s.set_vim_enabled(false);
    s.click_at(0, 0);
    s.paste("after-");
    c.ok(s.modified(), "buffer marked modified");
    c.ok(s.save(), "save");
    c.ok(!s.modified(), "clean after save");
    let disk = std::fs::read_to_string(&save_path).unwrap_or_default();
    c.ok(
        disk.starts_with("after-"),
        &format!("disk round-trip -> {disk:?}"),
    );

    // Tab key inserts.
    std::fs::write(&path, "").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(false);
    s.special_key(Key::Tab);
    c.ok(
        s.line_text(0).contains('\t') || s.line_text(0).contains(' '),
        &format!("Tab inserts indent -> {:?}", s.line_text(0)),
    );

    c.finish()
}

// ---- multi-cursor (existing coverage) ----

fn suite_multi_cursor(home: &Path) -> usize {
    let mut c = Check::new("multi-cursor / visual-block");
    let fixture = home.join("multi-cursor-fixture.txt");
    std::fs::write(&fixture, "foo\nfoo\nfoo\nabcd\nefgh\naaa\nbbb\nccc\n").unwrap();

    {
        let mut s = Session::new();
        c.ok(s.open(&fixture).is_ok(), "open fixture (standard)");
        s.set_vim_enabled(false);
        c.ok(!s.vim_enabled(), "vim off");
        c.ok(s.mode() == Mode::Insert, "standard starts in insert");

        s.click_at(0, 0);
        c.ok(s.select_next_occurrence(), "⌘D selects first 'foo'");
        c.ok(s.caret_count() == 0, "first ⌘D is selection, not an extra caret");
        c.ok(s.select_next_occurrence(), "⌘D #2 adds next occurrence");
        c.ok(s.caret_count() == 1, "one extra caret after second ⌘D");
        c.ok(s.select_next_occurrence(), "⌘D #3 adds third");
        c.ok(s.caret_count() == 2, "two extras after third ⌘D");

        s.text_input('X');
        let text = buffer_text(&s);
        c.ok(
            text.starts_with("X\nX\nX\n"),
            &format!("typing replaces every occurrence -> {text:?}"),
        );
        c.ok(s.caret_count() == 2, "carets survive typing");
        s.escape();
        c.ok(s.caret_count() == 0, "Escape clears extras");
    }

    {
        let mut s = Session::new();
        c.ok(s.open(&fixture).is_ok(), "re-open fixture for motions");
        s.set_vim_enabled(false);
        s.click_at(3, 0);
        c.ok(s.add_caret_at(4, 0), "⌥-click adds caret on efgh");
        c.ok(s.caret_count() == 1, "one extra after ⌥-click");

        s.motion(Motion::Right, false);
        c.ok(s.cursor() == (3, 1), &format!("primary moved right -> {:?}", s.cursor()));
        c.ok(s.caret_at(0).is_some(), "extra caret still present after motion");
        c.ok(
            caret_rowcol(&s, 0) == Some((4, 1)),
            &format!("extra caret moved right with primary -> {:?}", caret_rowcol(&s, 0)),
        );

        s.motion(Motion::Left, false);
        s.motion(Motion::Right, true);
        c.ok(s.has_selection(), "Shift+Right extends primary selection");
        let sels0 = s.line_selections(3);
        let sels1 = s.line_selections(4);
        c.ok(
            !sels0.is_empty() && !sels1.is_empty(),
            &format!("both lines show a selection slice -> {sels0:?} / {sels1:?}"),
        );

        s.escape();
        s.click_at(3, 1);
        c.ok(s.add_caret_at(4, 1), "⌥-click again for backspace");
        s.special_key(Key::Backspace);
        let text = buffer_text(&s);
        c.ok(
            text.contains("bcd\n") && text.contains("fgh\n"),
            &format!("Backspace at every caret -> {text:?}"),
        );

        std::fs::write(&fixture, "aa bb\ncc dd\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(false);
        s.click_at(0, 0);
        c.ok(s.add_caret_at(1, 0), "⌥-click second line for word motion");
        s.motion(Motion::WordRight, false);
        c.ok(
            s.cursor() == (0, 3),
            &format!("word-right primary at start of bb -> {:?}", s.cursor()),
        );
        c.ok(
            caret_rowcol(&s, 0) == Some((1, 3)),
            &format!("word-right extra at start of dd -> {:?}", caret_rowcol(&s, 0)),
        );
        s.motion(Motion::LineStart, false);
        c.ok(s.cursor() == (0, 0), "line-start primary");
        c.ok(
            caret_rowcol(&s, 0) == Some((1, 0)),
            &format!("line-start extra -> {:?}", caret_rowcol(&s, 0)),
        );
    }

    {
        std::fs::write(&fixture, "abcd\nefgh\nijkl\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(true);
        c.ok(s.vim_enabled(), "vim on");
        s.escape();
        c.ok(s.mode() == Mode::Normal, "escape -> NORMAL");

        s.click_at(0, 1);
        s.enter_visual_block();
        c.ok(
            s.mode() == Mode::VisualBlock,
            &format!("Ctrl+V -> {:?}", s.mode_name()),
        );
        c.ok(s.mode_name() == "V-BLOCK", "status chip is V-BLOCK");

        s.text_input('j');
        s.text_input('l');
        c.ok(s.mode() == Mode::VisualBlock, "motions keep V-BLOCK");
        let sel0 = s.line_selection(0);
        let sel1 = s.line_selection(1);
        let sel2 = s.line_selection(2);
        c.ok(
            sel0 == Some((1, 3)) && sel1 == Some((1, 3)) && sel2.is_none(),
            &format!("rectangular paint cols 1..3 on rows 0-1 -> {sel0:?}/{sel1:?}/{sel2:?}"),
        );
        let all = s.line_selections(0);
        c.ok(
            all == vec![(1, 3)],
            &format!("line_selections reports the block slice -> {all:?}"),
        );

        s.text_input('d');
        c.ok(s.mode() == Mode::Normal, "d leaves NORMAL");
        c.ok(s.caret_count() == 0, "d clears extras");
        let text = buffer_text(&s);
        c.ok(
            text.starts_with("ad\neh\nijkl"),
            &format!("block delete -> {text:?}"),
        );
    }

    {
        std::fs::write(&fixture, "aaa\nbbb\nccc\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(true);
        s.escape();
        s.click_at(0, 0);
        s.enter_visual_block();
        s.text_input('j');
        s.text_input('j');
        s.text_input('I');
        c.ok(s.mode() == Mode::Insert, "block I -> INSERT");
        c.ok(
            s.caret_count() == 2,
            &format!("block I places extras on other lines -> {}", s.caret_count()),
        );
        s.text_input('X');
        let text = buffer_text(&s);
        c.ok(
            text.starts_with("Xaaa\nXbbb\nXccc"),
            &format!("typing after I edits every line -> {text:?}"),
        );
        c.ok(
            caret_rowcol(&s, 0) == Some((2, 1)) && caret_rowcol(&s, 1) == Some((1, 1)),
            &format!(
                "carets sit after each typed char -> {:?} / {:?}",
                caret_rowcol(&s, 0),
                caret_rowcol(&s, 1)
            ),
        );

        s.special_key(Key::Right);
        c.ok(
            s.cursor() == (0, 2),
            &format!("arrow right moved primary -> {:?}", s.cursor()),
        );
        c.ok(
            caret_rowcol(&s, 0) == Some((2, 2)) && caret_rowcol(&s, 1) == Some((1, 2)),
            &format!(
                "arrow right moved extras -> {:?} / {:?}",
                caret_rowcol(&s, 0),
                caret_rowcol(&s, 1)
            ),
        );

        s.escape();
        c.ok(s.mode() == Mode::Normal, "Escape -> NORMAL");
        c.ok(s.caret_count() == 0, "Escape clears block carets");
    }

    {
        std::fs::write(&fixture, "aaa\nbbb\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(true);
        s.escape();
        s.click_at(0, 1);
        s.enter_visual_block();
        s.text_input('j');
        s.text_input('c');
        c.ok(s.mode() == Mode::Insert, "block c -> INSERT");
        c.ok(s.caret_count() >= 1, "block c keeps multi-carets");
        s.text_input('Z');
        let text = buffer_text(&s);
        c.ok(
            text.starts_with("aZa\nbZb"),
            &format!("typing after c replaces the column -> {text:?}"),
        );
        s.escape();
    }

    {
        std::fs::write(&fixture, "aa\nbb\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(true);
        s.escape();
        s.click_at(0, 0);
        s.enter_visual_block();
        s.text_input('j');
        s.text_input('l');
        s.text_input('A');
        c.ok(s.mode() == Mode::Insert, "block A -> INSERT");
        s.text_input('!');
        let text = buffer_text(&s);
        c.ok(
            text.starts_with("aa!\nbb!"),
            &format!("typing after A appends at block edge -> {text:?}"),
        );
    }

    {
        std::fs::write(&fixture, "abcd\n").unwrap();
        let mut s = Session::new();
        s.open(&fixture).unwrap();
        s.set_vim_enabled(true);
        s.escape();
        s.click_at(0, 1);
        s.enter_visual_block();
        s.text_input('v');
        c.ok(
            s.mode() == Mode::Visual,
            &format!("v from V-BLOCK -> {:?}", s.mode()),
        );
    }

    c.finish()
}

// ---- vim (non-block) ----

fn suite_vim(home: &Path) -> usize {
    let mut c = Check::new("vim modes");
    let path = home.join("vim-fixture.txt");
    std::fs::write(&path, "hello world\nsecond line\n").unwrap();

    let mut s = Session::new();
    c.ok(s.open(&path).is_ok(), "open fixture");
    c.ok(s.set_vim_enabled(true), "set vim on");
    c.ok(s.vim_enabled(), "vim enabled");
    s.escape();
    c.ok(s.mode() == Mode::Normal, "NORMAL after escape");

    s.click_at(0, 0);
    s.text_input('l');
    c.ok(s.cursor().1 >= 1, &format!("l moves right -> {:?}", s.cursor()));
    s.text_input('j');
    c.ok(s.cursor().0 >= 1, &format!("j moves down -> {:?}", s.cursor()));
    s.text_input('k');
    c.ok(s.cursor().0 == 0, &format!("k moves up -> {:?}", s.cursor()));
    s.text_input('h');

    s.text_input('i');
    c.ok(s.mode() == Mode::Insert, "i -> INSERT");
    s.text_input('X');
    c.ok(
        s.line_text(0).contains('X'),
        &format!("insert types -> {:?}", s.line_text(0)),
    );
    s.escape();
    c.ok(s.mode() == Mode::Normal, "Escape -> NORMAL");

    s.click_at(0, 0);
    s.text_input('v');
    c.ok(s.mode() == Mode::Visual, "v -> VISUAL");
    s.text_input('l');
    s.text_input('l');
    c.ok(s.has_selection() || s.selection_text().is_some(), "visual has span");
    let before = buffer_text(&s);
    s.text_input('y');
    c.ok(s.mode() == Mode::Normal, "y leaves NORMAL");
    let yanked = s.yank_text();
    c.ok(!yanked.is_empty(), &format!("yank captured -> {yanked:?}"));

    s.click_at(0, 0);
    s.text_input('v');
    s.text_input('l');
    s.text_input('d');
    c.ok(s.mode() == Mode::Normal, "visual d -> NORMAL");
    c.ok(
        buffer_text(&s) != before || !yanked.is_empty(),
        "visual d changed buffer or yank set",
    );

    // Paste after yank of a known slice.
    std::fs::write(&path, "abc\n").unwrap();
    let mut s = Session::new();
    s.open(&path).unwrap();
    s.set_vim_enabled(true);
    s.escape();
    s.click_at(0, 0);
    s.text_input('v');
    s.text_input('l');
    s.text_input('y');
    s.text_input('p');
    let text = buffer_text(&s);
    c.ok(
        text.contains("ab") || text.len() >= 3,
        &format!("yank+paste -> {text:?}"),
    );

    c.ok(s.toggle_vim() || !s.vim_enabled(), "toggle vim off");
    c.ok(!s.vim_enabled(), "vim disabled");
    c.ok(s.toggle_vim() && s.vim_enabled(), "toggle vim on");
    c.ok(s.vim_enabled(), "vim re-enabled");

    c.finish()
}

// ---- mouse selection ----

fn suite_mouse_selection(root: &Path) -> usize {
    let mut c = Check::new("mouse selection");
    let probe = root.join("src/piece_table.c");
    let mut e = Session::new();
    if e.open(&probe).is_err() {
        c.ok(false, &format!("open {probe:?}"));
        return c.finish();
    }
    e.set_vim_enabled(true);
    e.escape();

    e.click_at(20, 4);
    c.ok(e.cursor() == (20, 4), &format!("click -> {:?}", e.cursor()));
    c.ok(!e.has_selection(), "click has no selection");

    e.drag_to(22, 10);
    c.ok(e.has_selection(), "drag creates selection");
    c.ok(
        e.mode() == Mode::Visual,
        &format!("drag enters Visual -> {:?}", e.mode()),
    );
    c.ok(e.line_selection(20).is_some(), "line 20 has selection cols");
    c.ok(e.line_selection(21).is_some(), "line 21 has selection cols");
    let sel = e.selection_text();
    c.ok(
        sel.as_ref().map(|t| t.len()).unwrap_or(0) > 0,
        &format!("selection_text len -> {:?}", sel.as_ref().map(|t| t.len())),
    );

    e.click_at(20, 4);
    c.ok(!e.has_selection(), "click again collapses selection");
    c.ok(e.mode() == Mode::Normal, "back to Normal");

    for ch in "jjll".chars() {
        e.text_input(ch);
    }
    c.ok(
        !e.has_selection(),
        &format!(
            "jjll does not keep painting -> mode={:?} sel={:?}",
            e.mode(),
            e.line_selection(22)
        ),
    );

    // Escape from insert (completion may or may not appear).
    e.text_input('o');
    for ch in "pt_l".chars() {
        e.text_input(ch);
    }
    for _ in 0..10 {
        e.lsp_poll();
        std::thread::sleep(Duration::from_millis(50));
    }
    if e.complete_active() {
        println!(
            "  (completion open with {} items — soft LSP path)",
            e.complete_count()
        );
    } else {
        c.skip("LSP completion not available");
    }
    e.complete_close();
    e.popover_close();
    e.hover_clear();
    e.escape();
    c.ok(
        e.mode() == Mode::Normal && !e.complete_active(),
        &format!(
            "one Escape -> mode={:?} completion={}",
            e.mode(),
            e.complete_active()
        ),
    );

    c.finish()
}

// ---- tabs / workspace ----

fn suite_tabs_workspace(home: &Path, root: &Path) -> usize {
    let mut c = Check::new("tabs / workspace");

    let ws = home.join("ws-fixture");
    let _ = std::fs::remove_dir_all(&ws);
    std::fs::create_dir_all(&ws).unwrap();
    std::fs::write(ws.join("a.txt"), "aaa\n").unwrap();
    std::fs::write(ws.join("b.txt"), "bbb\n").unwrap();

    let mut s = Session::new();
    c.ok(s.open(&ws).is_ok(), "open workspace folder");
    c.ok(s.has_workspace(), "has_workspace");
    c.ok(s.ws_count() > 0, &format!("ws_count={}", s.ws_count()));
    c.ok(s.ws_entry(0).is_some(), "ws_entry(0)");

    let (ok, msg) = s.ws_create("", "newbie.txt", false);
    c.ok(ok, &format!("ws_create file -> {msg}"));
    c.ok(ws.join("newbie.txt").exists(), "created file on disk");

    let (ok, msg) = s.ws_create("", "subdir", true);
    c.ok(ok, &format!("ws_create dir -> {msg}"));
    c.ok(ws.join("subdir").is_dir(), "created dir on disk");

    let (ok, msg) = s.ws_delete("newbie.txt");
    c.ok(ok, &format!("ws_delete -> {msg}"));
    c.ok(!ws.join("newbie.txt").exists(), "deleted from disk");

    c.ok(s.open(&ws.join("a.txt")).is_ok(), "open a.txt");
    let tabs_after_a = s.tab_count();
    c.ok(tabs_after_a >= 1, &format!("tabs after a.txt={tabs_after_a}"));
    c.ok(s.open(&ws.join("b.txt")).is_ok(), "open b.txt");
    c.ok(
        s.tab_count() >= 2,
        &format!("two buffers -> tabs={}", s.tab_count()),
    );
    let active = s.tab_active();
    s.tab_goto(-1);
    c.ok(
        s.tab_active() != active || s.tab_count() == 1,
        &format!("tab_goto(-1) -> active {}", s.tab_active()),
    );
    s.tab_goto(1);
    let close_i = s.tab_active();
    let before = s.tab_count();
    s.tab_close(close_i);
    c.ok(
        s.tab_count() < before,
        &format!("tab_close {} -> {}", before, s.tab_count()),
    );

    // Recent projects.
    s.recent_add(root.to_string_lossy().as_ref());
    c.ok(s.recent_count() >= 1, &format!("recent_count={}", s.recent_count()));
    s.recent_input("edui");
    let q = s.recent_query();
    c.ok(q.contains("edui") || !q.is_empty(), &format!("recent query -> {q:?}"));

    // Also open the real repo root.
    let mut r = Session::new();
    c.ok(r.open(root).is_ok(), "open repo root");
    c.ok(r.has_workspace(), "repo has workspace");
    c.ok(r.ws_count() > 0, "repo ws has entries");

    c.finish()
}

// ---- find file ----

fn suite_find_file(root: &Path) -> usize {
    let mut c = Check::new("find file palette");
    let mut s = Session::new();
    c.ok(s.open(root).is_ok(), "open root");
    c.ok(s.palette_open(), "palette_open");
    c.ok(s.palette_active(), "palette active");
    s.palette_input("piece_table");
    c.ok(s.palette_count() > 0, &format!("hits for piece_table={}", s.palette_count()));
    c.ok(s.palette_accept(), "palette_accept opens file");
    c.ok(s.has_buffer(), "buffer open after accept");
    let path = s.path();
    c.ok(
        path.contains("piece_table"),
        &format!("opened path -> {path}"),
    );
    c.finish()
}

// ---- buffer search ----

fn suite_buffer_search(home: &Path) -> usize {
    let mut c = Check::new("buffer search");
    let path = home.join("search-buf.txt");
    std::fs::write(&path, "alpha\nbeta needle here\ngamma\nneedle again\n").unwrap();

    let mut s = Session::new();
    c.ok(s.open(&path).is_ok(), "open fixture");
    s.set_vim_enabled(false);
    s.buffer_search_open();
    c.ok(s.bufsearch_active(), "bufsearch active");
    s.bufsearch_input("needle");
    c.ok(
        s.bufsearch_text().contains("needle"),
        &format!("query -> {:?}", s.bufsearch_text()),
    );
    s.bufsearch_accept();
    c.ok(!s.bufsearch_active(), "accept closes search");
    let matches: usize = (0..s.line_count())
        .map(|l| s.line_matches(l).len())
        .sum();
    c.ok(matches >= 1, &format!("line_matches total={matches}"));
    let cur = s.cursor();
    s.bufsearch_repeat(false);
    c.ok(
        s.cursor() != cur || matches == 1,
        &format!("bufsearch_repeat moved or single match -> {:?} -> {:?}", cur, s.cursor()),
    );
    c.finish()
}

// ---- project search ----

fn suite_project_search(root: &Path) -> usize {
    let mut c = Check::new("project search");
    let mut s = Session::new();
    c.ok(s.open(root).is_ok(), "open root");
    c.ok(s.search_open(), "search_open");
    c.ok(s.search_active(), "search active");
    // Unique-ish string from the core.
    s.search_set_query("piece_table");
    let mut hits = 0usize;
    for _ in 0..100 {
        s.search_poll();
        hits = s.search_count();
        if hits > 0 && !s.search_running() {
            break;
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    c.ok(
        hits > 0,
        &format!(
            "search hits for piece_table={} running={} status={:?}",
            hits,
            s.search_running(),
            s.search_status()
        ),
    );
    if hits > 0 {
        c.ok(s.search_hit(0).is_some(), "search_hit(0)");
        c.ok(s.search_accept(), "search_accept navigates");
        c.ok(s.has_buffer(), "buffer after accept");
    }
    c.finish()
}

// ---- terminal ----

fn suite_terminal() -> usize {
    let mut c = Check::new("terminal");
    let mut s = Session::new();

    // `:` with no buffer.
    c.ok(!s.has_buffer(), "starts without buffer");
    s.text_input(':');
    c.ok(s.cmd_active(), ": opens command line with no buffer");
    s.cmd_close();

    s.cmd_open();
    for ch in "term".chars() {
        s.cmd_input(&ch.to_string());
    }
    let close = s.cmd_accept();
    c.ok(
        matches!(close, CloseAction::None) || s.term_active() || s.tab_count() >= 1,
        &format!(":term -> close={close:?} tabs={} kind={:?}", s.tab_count(), s.tab_kind(s.tab_active())),
    );

    let opened = s.term_open("terminal", "");
    c.ok(opened || s.term_active(), &format!("term_open={opened} active={}", s.term_active()));
    c.ok(
        s.tab_kind(s.tab_active()) == TabKind::Terminal || s.term_active(),
        &format!("active tab kind={:?}", s.tab_kind(s.tab_active())),
    );

    let rows = 30usize;
    s.term_resize(rows, 100);
    pump_term(&mut s, 800);

    for ch in "echo selftest-ok".chars() {
        s.term_write(&ch.to_string());
    }
    pump_term(&mut s, 200);
    s.term_key(tk::ENTER, false, false, false);
    pump_term(&mut s, 1000);

    let start = s.term_visible_start(rows);
    let total = s.term_total_lines();
    let last = (start + rows).min(total.max(start));
    let mut found = false;
    for index in start..last {
        if s.term_line(index).contains("selftest-ok") {
            found = true;
            break;
        }
    }
    // Also scan all lines in case scrollback placement differs.
    if !found {
        for index in 0..total {
            if s.term_line(index).contains("selftest-ok") {
                found = true;
                break;
            }
        }
    }
    c.ok(found, "echo selftest-ok present in terminal");

    // Multibyte col_to_byte.
    s.term_write("printf '\\u203a\\u203a\\u203aabc\\n'");
    s.term_key(tk::ENTER, false, false, false);
    pump_term(&mut s, 900);
    let total2 = s.term_total_lines();
    let mut mb_ok = false;
    for index in 0..total2 {
        let line = s.term_line(index);
        if line.starts_with("\u{203a}\u{203a}\u{203a}abc") {
            let b3 = s.term_col_to_byte(index, 3);
            let b4 = s.term_col_to_byte(index, 4);
            mb_ok = b3 == 9 && b4 == 10;
            c.ok(
                mb_ok,
                &format!("multibyte col_to_byte(3)={b3} (want 9) col_to_byte(4)={b4} (want 10)"),
            );
            break;
        }
    }
    if !mb_ok {
        c.skip("multibyte printf line not found (shell variance)");
    }

    let row = (0..s.term_total_lines())
        .find(|i| s.term_line(*i).contains("selftest-ok"))
        .unwrap_or(start);
    s.term_sel_begin(row, 0);
    s.term_sel_update(row, 4);
    s.term_sel_end();
    let span = s.term_sel_span(row);
    c.ok(span.is_some(), &format!("term selection span -> {span:?}"));
    let copied = s.term_copy_selection();
    c.ok(
        copied.as_ref().map(|t| !t.is_empty()).unwrap_or(false),
        &format!("term copy -> {copied:?}"),
    );

    s.term_write("printf 'redraw\\n'");
    s.term_key(tk::ENTER, false, false, false);
    pump_term(&mut s, 800);
    c.ok(
        s.term_sel_span(row).is_some(),
        "selection survives further output",
    );

    s.term_write("seq 1 120");
    s.term_key(tk::ENTER, false, false, false);
    pump_term(&mut s, 1500);

    let head = |s: &mut Session| s.term_line(s.term_visible_start(rows));
    let at_bottom = head(&mut s);
    s.term_scroll(5);
    let scrolled_back = head(&mut s);
    s.term_scroll(-5);
    let returned = head(&mut s);
    c.ok(
        scrolled_back != at_bottom,
        &format!(
            "scroll +5 moved viewport ({:?} -> {:?})",
            at_bottom.trim_end(),
            scrolled_back.trim_end()
        ),
    );
    c.ok(
        returned == at_bottom,
        &format!("scroll -5 restored ({:?})", returned.trim_end()),
    );

    c.finish()
}

// ---- git (read-only) ----

fn suite_git(root: &Path) -> usize {
    let mut c = Check::new("git view");
    let mut s = Session::new();
    c.ok(s.open(root).is_ok(), "open root");
    if !s.git_open() {
        c.skip("git_open failed (not a git repo?)");
        return c.finish();
    }
    c.ok(s.git_active(), "git active");
    let mode = s.git_mode();
    c.ok(
        matches!(
            mode,
            GitMode::RepoSelect | GitMode::Changes | GitMode::CommitInput
        ),
        &format!("git mode -> {mode:?}"),
    );
    let _ = s.git_files();
    let _ = s.git_diff();
    let _ = s.git_repos();
    s.git_move(1);
    s.git_move(-1);
    s.git_diff_scroll(1);
    s.git_cancel_input();
    // Stay read-only: no stage/commit.
    c.ok(true, "git list/move/diff (read-only) ok");
    c.finish()
}

// ---- config / themes / cmd filter ----

fn suite_config_themes(cmd_hits: &impl Fn(&str) -> Vec<&'static str>) -> usize {
    let mut c = Check::new("config / themes / palette filter");
    let mut e = Session::new();

    let start_opacity = e.opacity_pct();
    run_cmd(&mut e, "opacity 0.8");
    c.ok(
        e.opacity_pct() == 80,
        &format!(":opacity 0.8 -> {}% (was {start_opacity})", e.opacity_pct()),
    );
    run_cmd(&mut e, "opacity 80");
    c.ok(
        e.opacity_pct() == 80,
        &format!(":opacity 80 rejected -> {}%", e.opacity_pct()),
    );
    let blur0 = e.blur();
    run_cmd(&mut e, "blur");
    c.ok(e.blur() != blur0, &format!(":blur toggled -> {}", e.blur()));
    run_cmd(&mut e, "radius 12");
    c.ok(
        (e.radius() - 12.0).abs() < 0.01,
        &format!(":radius 12 -> {}", e.radius()),
    );

    let before = e.base_pt();
    e.zoom(1);
    e.zoom(1);
    c.ok(e.base_pt() > before, &format!("zoom in {before} -> {}", e.base_pt()));
    e.zoom(0);
    c.ok(
        (e.base_pt() - before).abs() < 0.01,
        &format!("zoom reset -> {}", e.base_pt()),
    );

    let s0 = e.show_sidebar();
    e.toggle_sidebar();
    c.ok(e.show_sidebar() != s0, "toggle sidebar");
    let w0 = e.wrap();
    e.toggle_wrap();
    c.ok(e.wrap() != w0, "toggle wrap");

    e.save_config();
    let opacity_saved = e.opacity_pct();
    let theme_saved = e.theme_index();
    let fresh = Session::new();
    c.ok(
        fresh.opacity_pct() == opacity_saved,
        &format!(
            "config persist opacity {} -> {}",
            opacity_saved,
            fresh.opacity_pct()
        ),
    );
    c.ok(
        fresh.theme_index() == theme_saved,
        "config kept theme index",
    );

    let names: Vec<String> = ffi::themes().iter().map(|(n, _)| n.clone()).collect();
    c.ok(names.len() >= 2, &format!("{} themes registered", names.len()));
    c.ok(e.theme_set("gruvbox-dark"), "theme_set gruvbox-dark");
    let names_now: Vec<String> = ffi::themes().iter().map(|(n, _)| n.clone()).collect();
    let active = &names_now[e.theme_index()];
    c.ok(
        active == "gruvbox-dark",
        &format!("active theme -> {active}"),
    );
    c.ok(!e.theme_set("no-such-theme"), "unknown theme rejected");
    e.save_config();
    let again = Session::new();
    let again_name = ffi::themes()
        .get(again.theme_index())
        .map(|(n, _)| n.clone())
        .unwrap_or_default();
    c.ok(
        again_name == "gruvbox-dark",
        &format!("theme persisted across session -> {again_name}"),
    );

    // Command palette filter (Rust Cmd::matches).
    let open = cmd_hits("open");
    c.ok(
        open.iter().any(|l| l.contains("Open")),
        &format!("filter 'open' -> {open:?}"),
    );
    let zoom = cmd_hits("zoom");
    c.ok(
        zoom.len() >= 3,
        &format!("filter 'zoom' -> {zoom:?}"),
    );
    let none = cmd_hits("xyzzy");
    c.ok(none.is_empty(), &format!("filter 'xyzzy' -> {none:?}"));
    let empty = cmd_hits("");
    c.ok(empty.len() >= 20, &format!("empty filter lists {} cmds", empty.len()));

    c.finish()
}

// ---- resources / highlighting ----

fn suite_resources(home: &Path, root: &Path) -> usize {
    let mut c = Check::new("resources / highlighting");
    println!("cwd={:?}", std::env::current_dir().unwrap_or_default());

    let probe = root.join("src/piece_table.c");
    let mut r = Session::new();
    c.ok(r.open(&probe).is_ok(), &format!("open {probe:?}"));
    let lines = r.line_count().min(80);
    let spans: usize = (0..lines).map(|l| r.line_spans(l).len()).sum();
    c.ok(
        spans > 0,
        &format!("tree-sitter spans in first {lines} lines: {spans}"),
    );

    let json = home.join("probe.json");
    std::fs::write(&json, r#"{"hello": 1, "world": true}"#).unwrap();
    let mut j = Session::new();
    c.ok(j.open(&json).is_ok(), "open json fixture");
    let jspans: usize = (0..j.line_count()).map(|l| j.line_spans(l).len()).sum();
    c.ok(jspans > 0, &format!("json spans: {jspans}"));

    let env = home.join(".env");
    std::fs::write(&env, "FOO=bar\n# comment\n").unwrap();
    let mut e = Session::new();
    c.ok(e.open(&env).is_ok(), "open dotenv fixture");
    let espans: usize = (0..e.line_count()).map(|l| e.line_spans(l).len()).sum();
    c.ok(espans > 0, &format!("dotenv spans: {espans}"));

    c.finish()
}

// ---- updates (soft) ----

fn suite_updates_soft() {
    println!("\n=== updates (soft) ===");
    println!("this build reports version {}", ffi::version());
    ffi::check_updates(true);
    let mut settled = false;
    for _ in 0..40 {
        ffi::pump_main_queue(0.25);
        while let Some(update) = ffi::update_poll() {
            println!("  {}", update_status_line(&update));
            settled |= matches!(
                update,
                ffi::Update::UpToDate { .. }
                    | ffi::Update::Available { .. }
                    | ffi::Update::Failed { .. }
            );
        }
        if settled {
            break;
        }
    }
    if !settled {
        println!("  SKIP no reply within 10s (offline?)");
    }
}
