# wave-gpui — GPUI front-end for Wave's C core

A Rust/GPUI front-end driving Wave's **existing C core** over FFI. Nothing in
`src/` was modified; the C code here is one new file (`shim/wave_ffi.c`).

## Why this works

`libwave.a` (the Makefile's `CORE_SRC` + tree-sitter) contains no OpenGL, GLFW
or Cocoa — the Makefile calls it the "headless core", and the 28 test binaries
already link it with nothing but `CoreServices`/`CoreFoundation`. Only
`render.c`, `font.c`, `draw.c`, `input_glfw.c`, `main.c` and `mac.m` touch the
platform, and those are exactly the files GPUI replaces.

`edit_command.h` also reifies editing as a single narrow call —
`edit_command_apply(Editor*, ModalState*, YankRegister*, codepoint) -> flags` —
so the seam is **commands in, state out**, not a wide struct-sharing interface.
`shim/wave_ffi.c` exposes that shape plus read accessors; no bindgen involved.

## Build & run

`libwave.a` must exist first:

```sh
make build/libwave.a          # from the repo root
cd rust && cargo build
```

Run **from the repo root** so `queries/<lang>/highlights.scm` resolves:

```sh
./rust/target/debug/wave-gpui .            # folder → sidebar + no file open
./rust/target/debug/wave-gpui src/main.c   # file → opens in a tab
```

## Working

All of this is computed in C; the front-end only lays it out and forwards input.

- **Buffer editing** — vim normal/insert/visual as implemented in `edit_command.c`
  (`j`/`k`/`w`/`b`/`i`/`a`/`o`/`x`/`dd`/`yy`/`p`/counts/…), arrows, `Esc`.
- **tree-sitter highlighting** — real spans per line, colored through Wave's own
  `theme_color()` so the palette isn't forked.
- **LSP** — `lsp_manager` drives clangd / the bundled TS server. Diagnostics are
  the merged tree-sitter + server set and are underlined inline; the message for
  the diagnostic under the cursor shows in the status bar. `gh` hover and `gd`
  go-to-definition work, with the tree-sitter heuristic as fallback when no
  server is running. Replies are drained on a 60 ms GPUI timer.
- **Cmd-P fuzzy file finder** — `palette.c`'s ranking and filtering, `↑`/`↓` and
  `Enter`, click to open.
- **Visual-mode selection** — highlighted from `editor_visual_range`.
- **Workspace sidebar** — the real `ws_visible()` tree, indented by depth, with
  collapse/expand and click-to-open (single = preview, double = pinned), all
  decided by `ws_click_visible`.
- **Tabs** — open/switch/close via `tabs_*`, `gt`/`gT` and `Cmd-]`/`Cmd-[`,
  `Cmd-W` to close. The `" *"` modified marker is `view_tab_label`'s, not ours.
- **App menu** — `Cmd-Q` quits. GPUI installs no default menu, so the app menu
  and its keybinding have to be registered explicitly.
- **Undo/redo** (`u`, `Cmd-Z`, `Cmd-Shift-Z`), **save** (`Cmd-S`).
- Mouse wheel scrolling in both the editor and the sidebar.

- **Terminal tabs** (`Cmd-T`) — real pty through libghostty-vt, with per-cell
  fg/bg from `terminal_line_style`, scrollback, and key encoding via
  `terminal_send_key_mods`. **The terminal speaks GLFW key codes**
  (`ffi::term_key`), not `EditorKey` — `terminal_key_sequence` switches on
  256/257/258/… and on ASCII `'A'..='Z'` for control chords. Passing the
  editor's enum here produces no escape sequence at all, so Enter silently does
  nothing. Its cursor row is an *absolute* scrollback row and has to be rebased
  onto the visible window, as `draw_terminal_panel` does.
- **Git view** (`Cmd-Shift-G`) — repo picker, status list, diffs, staging
  (`s`), and commit (`c` then type, `Enter`), all from `git_view.c`.
- **Completion** — insert-mode menu anchored under the cursor, sourced from the
  language server when one is ready and tree-sitter identifiers otherwise, with
  `complete.c`'s generation counter dropping stale async replies.
- **Cmd-Shift-F project search** — bundled ripgrep, streamed results, `Enter`
  jumps to `path:line:col`.
- **`:` command line** — `command.c`'s parser (`:w`, `:q`, `:set wrap`,
  `:set opacity`, …), with its `CommandAppPlan` deciding what closes.
- **`gh` popover** — scrollable, composed by `popover.c` from the local info
  plus the server's hover when it lands.
- **Custom titlebar** — the native one is hidden (`appears_transparent`), with
  window dragging, double-click zoom, and traffic-light inset handled here.
  Window opacity comes from `WaveConfig`.

- **`/` buffer search** — live preview from the original cursor, `n`/`N` repeat,
  `*` for the word under the cursor, and matches highlighted inline.
- **Recent projects** — the empty state, filtered by typing, from `recent.c`.
- **Workspace file operations** — `Cmd-N` new file, `Cmd-Shift-N` new folder,
  `Cmd-Backspace` delete, via `ws_create_*` / `ws_delete_path`.
- **External file-change reload** — the session owns a `WatchService`, so edits
  made outside the editor reload the affected tab.
- **Yank to system clipboard** — the C core fills its register, the front-end
  mirrors it, as `main.c` does.

## Why lines are one shaped run

Each line is handed to the text system as a **single** `StyledText` with
highlight ranges, not one `div` per colored span. That is load-bearing, not a
style preference: splitting a line into several text elements re-shapes each
piece independently, so their sub-pixel advances no longer sum to the same
total — and the line visibly shifts as the cursor moves through it, since the
cursor changes where the splits fall. Per-byte `Cell`s are accumulated
(syntax, search matches, selection, diagnostics, cursor) and collapsed into
contiguous ranges just before rendering.

## Not wired yet

| Feature | Notes |
|---|---|
| Mouse text selection | `editor_apply_drag_selection` exists; no drag wiring |
| Soft wrap | `wrap_build` / `line_at_vrow` exist; the view is unwrapped |
| Sidebar rename | `ws_paste_path_into` is bound but has no UI |
| Signature help | `lsp_manager_request_signature_help` unbound |
| Terminal / git-diff selection + copy | `terminal_copy_selection`, `git_view_copy_diff_selection` |

Also: lines are truncated at 4 KiB on read, and the completion menu is the one
thing positioned with a hard-coded character advance (`ADVANCE`), since it has
to track a text position rather than flow in the layout.

### One constraint worth knowing

`Diagnostic.message` is a non-owning `const char *`, so `diagnostic_from_lsp()`
can only store the literal `"diagnostic"` — a server's message lives in an
`LspDiag` buffer that would dangle. `main.c` hits the same wall and solves it the
same way this shim does: underline from the merged set, but pull real text for
the diagnostic under the cursor straight from the `LspDiag` array via
`diagnostics_cursor_info()`.

## Headless verification

The seam can be driven with no window at all, which is how it is checked — a
GPUI window takes focus on launch and will otherwise capture your typing:

```
opened folder        ws=1 root=src entries=75
sidebar activate     row=0 path=src/buffer.c
after open           1 tabs, active=0: [buffer.c]
after 2nd open       2 tabs, active=1: [buffer.c] [buffer.h]
after gT             path=src/buffer.c
after edit           NORMAL 6:2 | X#include <string.h>
modified flag        2 tabs, active=0: [buffer.c *] [buffer.h]
after undo           #include <string.h>

empty query          q="" 75 hits, sel=0: >buffer.c buffer.h command.c
typed 'piece'        q="piece" 2 hits, sel=0: >piece_table.c piece_table.h
accept               ok=1 tabs=1 path=src/piece_table.c
```

LSP, against a file with a deliberate undefined symbol and a missing semicolon:

```
--- immediately after open (tree-sitter only) ---
  line 9 cols 29-30: missing token
--- after polling (tree-sitter + clangd) ---
  line 8 cols 12-33: diagnostic
  line 10 cols 4-10: diagnostic
lsp_active=1
cursor 8:13 -> Error: Use of undeclared identifier 'undefined_symbol_here'
hover: stdio.h … provides printf
```

Terminal, driven headlessly:

```
active=1 running=1
enter -> output present: 1
backspace -> "backspace-works": 1 (stray "backspaceXX": 0)
ctrl-c -> shell responsive again: 1
cursor abs row=8 col=36 visible=1, visible_start=0 -> screen row 8
idle polls reporting change: 0/20
```

## Debugging without a GUI

```sh
./rust/target/debug/wave-gpui --selftest .      # drive the terminal headlessly
WAVE_DEBUG=1 ./rust/target/debug/wave-gpui .    # trace key routing + render
```

`--selftest` runs the exact `Session` calls the UI makes — `:term`, `Cmd-T`'s
`term_open`, the key codes the handler sends, and the same visible-window
arithmetic `render_terminal` does — then prints the screen. If that passes but
the app misbehaves, the fault is in GPUI event/render wiring, not the core.

`WAVE_DEBUG=1` logs every keystroke with its modifiers and which surface owned
it, so "the shortcut does nothing" can be told apart from "the shortcut ran and
the view didn't update".

**Focus is the first thing to check.** GPUI's `on_key_down` only fires while the
element holds focus, and a window that opens without becoming key silently
swallows every shortcut. `window.activate_window()` at open plus a re-focus in
`render` when nothing holds focus is what makes shortcuts reliable.

## Notes / gotchas

- `runtime.c` resolves the bundled ripgrep and TS language server *relative to
  the executable* (`<exe_dir>/vendor/…`, `<exe_dir>/../vendor/…`). That works
  for the Makefile's `build/wave`, but `target/debug/wave-gpui` is two levels
  deeper, so project search silently reported "rg unavailable". `build.rs`
  symlinks the vendor tree next to the binary so those probes hit.
- On Mach-O, `cargo:rustc-link-lib=static=…` does **not** force static linkage.
  With `libghostty-vt.dylib` sitting next to the `.a`, ld picks the dylib and
  the binary dies at launch with `no LC_RPATH's found`. `build.rs` therefore
  links both archives by absolute path, as the Makefile does.
- GPUI is pre-1.0 and its published crate lags `main`: `gpui` 0.2.2 on crates.io
  is still monolithic (`Application::new()`), whereas `main` has split the
  platform layer into a `gpui_platform` crate that is **not published**. Pinning
  to the crates.io release means writing against an API upstream has already
  moved past.
