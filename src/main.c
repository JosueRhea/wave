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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "command.h"
#include "config.h"
#include "diagnostics.h"
#include "draw.h"
#include "edit_command.h"
#include "editor.h"
#include "font.h"
#include "highlight.h"
#include "input.h"
#include "layout.h"
#include "lsp.h"
#include "lsp_manager.h"
#include "mode.h"
#include "overlay.h"
#include "popover.h"
#include "render.h"
#include "tabs.h"
#include "text_view.h"
#include "theme.h"
#include "view.h"
#include "watch.h"
#include "workspace.h"
#include "yank.h"

/* ---- application state ---- */
typedef struct {
    GLFWwindow *win;
    Font *font;
    Renderer *rend;
    float fb_scale;

    Workspace *ws;     /* open folder, or NULL */
    float side_scroll;
    WaveConfig config;

    /* open files, shown as tabs */
    TabSet tabs;

    /* vim */
    ModalState modal;

    /* command line ( : ) */
    CommandLine cmd;

    /* command palette (Cmd-P) and content search (Cmd-Shift-F). */
    OverlayState overlay;

    /* yank register */
    YankRegister yank;

    char info[256];    /* transient `gd`/status message for the status bar */

    Popover pop;

    LspManager lsp;
    WatchService watch;

    int mods;          /* latest modifier state */
    int mouse_down;     /* 1 while left button is held in the text area */
    int mouse_dragging; /* 1 after a held click turns into a selection drag */
    size_t mouse_anchor;
    float mouse_x, mouse_y;
    LayoutPointClick title_click;
    LayoutIndexClick sidebar_click;

    double last_activity; /* glfwGetTime() of the last keypress (for blink reset) */
    double next_watch;    /* next glfwGetTime() for polling open files */
    int blink;            /* 1 = animate the cursor (off in snapshot mode) */
    int persist;          /* 1 = read/write the config file (off in snapshot mode) */

    /* cached layout from the last frame, for click hit-testing */
    LayoutState layout;
} App;

static App g;

/* The active editor (the front tab). Always valid once a tab exists, so
 * callers never have to null-check the slot itself. */
static Editor *cur(void) { return tabs_current(&g.tabs); }

static const char *FONT_PATH = "/System/Library/Fonts/SFNSMono.ttf";

static void app_palette_refilter(void);

static void config_path(char *out, size_t cap) {
    wave_config_path(out, cap);
}

static void config_save(void) {
    if (!g.persist) return;
    wave_config_save(&g.config);
}

static void editor_copy_selection(Editor *e) {
    char *text = editor_copy_text(e, g.modal.mode == MODE_VISUAL);
    if (!text) return;
    glfwSetClipboardString(g.win, text);
    free(text);
}

static void center_cursor(Editor *e) {
    editor_center_cursor(e, g.layout.line_h,
                         (float)g.layout.fb_h - g.layout.top_pad);
}

static void close_tab(int i) {
    if (tabs_close(&g.tabs, i) == 0) {
        glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        return;
    }
    modal_enter_normal(&g.modal);
}

static void tab_goto(int delta) {
    tabs_goto(&g.tabs, delta);
    modal_enter_normal(&g.modal);
}

static void handle_watched_file_change(TabDiskChange event) {
    TabDiskChangeEffect effect = tabs_describe_disk_change(
        &g.tabs, &event, g.info, sizeof g.info);
    if (effect.reset_active_mode) modal_enter_normal(&g.modal);
}

static void process_file_watchers(void) {
#ifdef __APPLE__
    for (;;) {
        int ids[16];
        int n = watch_poll_file_events(&g.watch, ids, 16);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            TabDiskChange event;
            if (tabs_apply_file_watch_event(&g.tabs, &g.watch, ids[i], &event))
                handle_watched_file_change(event);
        }
        if (n < 16) break;
    }
#else
    double now = glfwGetTime();
    if (now < g.next_watch) return;
    g.next_watch = now + 0.5;

    TabDiskChange events[16];
    int n = tabs_apply_file_watch_poll(&g.tabs, &g.watch, events, 16);
    for (int i = 0; i < n; i++)
        handle_watched_file_change(events[i]);
#endif
}

static void workspace_watch_stop(void) {
    watch_workspace_stop(&g.watch);
}

static void workspace_watch_start(void) {
    workspace_watch_stop();
    if (!g.ws) return;
    watch_workspace_start(&g.watch, ws_root(g.ws));
}

static void process_workspace_watchers(void) {
    if (!watch_workspace_consume(&g.watch) || !g.ws) return;
    WsReloadEffect effect = ws_apply_reload(g.ws);
    if (effect.refilter_palette && overlay_active(&g.overlay) == OVERLAY_PALETTE)
        app_palette_refilter();
    snprintf(g.info, sizeof g.info, "%s", effect.message);
}

/* Open `path` (absolute), focusing it if already open. `preview` makes it a
 * transient peek tab (see Editor.preview): the next preview-open reuses the
 * slot instead of stacking another tab. */
static int open_path_mode(const char *path, int preview) {
    TabOpenPlan plan = tabs_prepare_open(&g.tabs, path, preview);
    if (!plan.editor) return -1;
    if (tabs_apply_existing_open(&plan, preview)) {
        modal_enter_normal(&g.modal);
        return 0;
    }

    if (editor_open_file(plan.editor, path, preview, &g.watch) != 0) {
        tabs_cancel_open(&g.tabs, &plan);
        fprintf(stderr, "could not open %s\n", path);
        return -1;
    }
    lsp_manager_open_editor(&g.lsp, plan.editor);
    modal_enter_normal(&g.modal);
    return 0;
}

/* Open `path` (absolute) as a pinned tab: focus it if already open, otherwise
 * load it into a fresh tab (reusing an empty untitled scratch tab if that's all
 * there is). */
static int open_path(const char *path) { return open_path_mode(path, 0); }

static void app_palette_refilter(void) {
    overlay_refilter_palette(&g.overlay, g.ws);
}

static void palette_open(void) {
    overlay_open_palette(&g.overlay, g.ws);
}
static void palette_accept(void) {
    OverlayAcceptTarget target = overlay_accept_target(&g.overlay, g.ws);
    if (target.has_target) open_path(target.path);
    overlay_accept_target_free(&target);
    overlay_close(&g.overlay);
}

static void search_open(void) {
    if (!g.ws) return;
    overlay_open_search(&g.overlay);
}

/* Open the selected hit's file and place the cursor at its line/col. */
static void search_accept(void) {
    OverlayAcceptTarget target = overlay_accept_target(&g.overlay, g.ws);
    if (target.has_target) {
        if (open_path(target.path) == 0 && target.has_location) {
            Editor *te = cur();
            editor_move_to_line_col(te, target.line, target.col);
            center_cursor(te);
        }
    }
    overlay_accept_target_free(&target);
    overlay_close(&g.overlay);
}

/* ---- command line ( :w :q :wq ) ---- */
static void cmd_exec(void) {
    char path[1100];
    config_path(path, sizeof path);
    CommandRun run = command_run(command_text(&g.cmd), &g.config, path);
    CommandEffect effect = run.effect;
    if (effect.save_config) config_save();
    if (effect.apply_blur)
        mac_set_blur(glfwGetCocoaWindow(g.win), g.config.blur);
    if (effect.apply_titlebar)
        mac_use_native_titlebar(glfwGetCocoaWindow(g.win), g.config.native_titlebar);

    snprintf(g.info, sizeof g.info, "%s", run.info);

    if (effect.write && cur()->path) {
        editor_save_file(cur(), &g.watch);
    }
    command_close(&g.cmd);
    if (effect.quit) {
        if (effect.quit_all) glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        else close_tab(tabs_active_index(&g.tabs)); /* closes the window when the last tab goes */
    }
}

#define MAXDIAG 256

static void show_info(void) {
    Editor *e = cur();
    if (!e->buf) return;

    char base[sizeof g.pop.base];
    LspDiag ld[MAXDIAG];
    size_t nl = 0;
    if (e->path) {
        int published = 0;
        nl = lsp_manager_diagnostics(&g.lsp, e, ld, MAXDIAG, &published);
    }

    DiagnosticCursorInfo info;
    diagnostics_cursor_info(e, e->path ? ld : NULL, nl, base, sizeof base, &info);
    int loading = lsp_manager_request_hover(&g.lsp, e, (int)info.row, (int)info.col);
    popover_show_base(&g.pop, base, loading);
}

static void goto_definition(void) {
    Editor *e = cur();
    if (!e->buf) return;

    /* prefer the language server; the async reply is applied in the main loop */
    if (e->path) {
        size_t row, col;
        pt_offset_to_rowcol(buffer_pt(e->buf), e->cursor, &row, &col);
        if (lsp_manager_request_definition(&g.lsp, e, (int)row, (int)col)) {
            snprintf(g.info, sizeof g.info, "resolving definition...");
            return;
        }
    }

    if (!editor_goto_local_definition(e, g.info, sizeof g.info))
        return;
    center_cursor(e);
}

/* ---- vim normal/visual mode ---- */
static void vim_normal_char(unsigned int cp) {
    Editor *e = cur();

    if (popover_apply_normal_char(&g.pop, cp, g.modal.pending == 'g')) return;

    g.info[0] = '\0';     /* any normal-mode key dismisses a lingering gd note */
    EditCommandResult res = edit_command_apply(e, &g.modal, &g.yank, cp);
    if ((res.flags & EDIT_COMMAND_YANKED) && g.win && g.yank.text)
        glfwSetClipboardString(g.win, g.yank.text);
    if (res.flags & EDIT_COMMAND_SHOW_INFO) show_info();
    if (res.flags & EDIT_COMMAND_GOTO_DEFINITION) goto_definition();
    if (res.flags & EDIT_COMMAND_TAB_NEXT) tab_goto(+1);
    if (res.flags & EDIT_COMMAND_TAB_PREV) tab_goto(-1);
    if (res.flags & EDIT_COMMAND_OPEN_COMMAND_LINE) command_open(&g.cmd);
    if (res.flags & EDIT_COMMAND_UNDO_AT_OLDEST)
        snprintf(g.info, sizeof g.info, "already at oldest change");
}

/* ---- UI zoom (Cmd +/-) ---- */

/* Rebuild the font + renderer at the current fb_scale * ui_scale. The whole UI
 * scales because every layout metric (line height, cell advance, gutter, tab
 * strip, panels) is derived from the font. Requires a current GL context. */
static void reload_font(void) {
    float px = g.config.base_pt * g.fb_scale * g.config.ui_scale;
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
    wave_config_zoom(&g.config, dir);
    reload_font();
    config_save();
    wave_config_zoom_text(&g.config, g.info, sizeof g.info);
}

/* ---- GLFW callbacks ---- */
static void on_char(GLFWwindow *w, unsigned int cp) {
    (void)w;
    g.last_activity = glfwGetTime();
    InputTextTarget target = input_text_target(
        (g.mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0,
        overlay_active(&g.overlay) != OVERLAY_NONE,
        g.cmd.active,
        cur()->buf != NULL,
        g.modal.mode == MODE_INSERT);

    if (target == INPUT_TEXT_IGNORE || target == INPUT_TEXT_NONE) return;
    if (target == INPUT_TEXT_OVERLAY) {
        if (cp >= 32 && cp < 127) {
            char s[2] = {(char)cp, '\0'};
            overlay_insert_text(&g.overlay, g.ws, ws_root(g.ws), s);
        }
        return;
    }
    if (target == INPUT_TEXT_COMMAND) {
        command_insert_char(&g.cmd, cp);
        return;
    }
    if (target == INPUT_TEXT_EDITOR_INSERT) {
        editor_apply_text_input(cur(), cp);
        return;
    }
    vim_normal_char(cp); /* NORMAL or VISUAL */
}

static EditorKey editor_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_BACKSPACE: return EDITOR_KEY_BACKSPACE;
    case GLFW_KEY_DELETE: return EDITOR_KEY_DELETE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return EDITOR_KEY_ENTER;
    case GLFW_KEY_TAB: return EDITOR_KEY_TAB;
    case GLFW_KEY_LEFT: return EDITOR_KEY_LEFT;
    case GLFW_KEY_RIGHT: return EDITOR_KEY_RIGHT;
    case GLFW_KEY_UP: return EDITOR_KEY_UP;
    case GLFW_KEY_DOWN: return EDITOR_KEY_DOWN;
    default: return EDITOR_KEY_NONE;
    }
}

static CommandKey command_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return COMMAND_KEY_ESCAPE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return COMMAND_KEY_ACCEPT;
    case GLFW_KEY_BACKSPACE: return COMMAND_KEY_BACKSPACE;
    default: return COMMAND_KEY_NONE;
    }
}

static OverlayKey overlay_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return OVERLAY_KEY_ESCAPE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return OVERLAY_KEY_ACCEPT;
    case GLFW_KEY_UP: return OVERLAY_KEY_UP;
    case GLFW_KEY_DOWN: return OVERLAY_KEY_DOWN;
    case GLFW_KEY_BACKSPACE: return OVERLAY_KEY_BACKSPACE;
    default: return OVERLAY_KEY_NONE;
    }
}

static PopoverKey popover_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return POPOVER_KEY_ESCAPE;
    case GLFW_KEY_UP: return POPOVER_KEY_UP;
    case GLFW_KEY_DOWN: return POPOVER_KEY_DOWN;
    default: return POPOVER_KEY_NONE;
    }
}

static InputKey input_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_C: return INPUT_KEY_C;
    case GLFW_KEY_V: return INPUT_KEY_V;
    case GLFW_KEY_P: return INPUT_KEY_P;
    case GLFW_KEY_F: return INPUT_KEY_F;
    case GLFW_KEY_B: return INPUT_KEY_B;
    case GLFW_KEY_S: return INPUT_KEY_S;
    case GLFW_KEY_RIGHT_BRACKET: return INPUT_KEY_RIGHT_BRACKET;
    case GLFW_KEY_LEFT_BRACKET: return INPUT_KEY_LEFT_BRACKET;
    case GLFW_KEY_W: return INPUT_KEY_W;
    case GLFW_KEY_Z: return INPUT_KEY_Z;
    case GLFW_KEY_R: return INPUT_KEY_R;
    case GLFW_KEY_EQUAL: return INPUT_KEY_EQUAL;
    case GLFW_KEY_KP_ADD: return INPUT_KEY_ADD;
    case GLFW_KEY_MINUS: return INPUT_KEY_MINUS;
    case GLFW_KEY_KP_SUBTRACT: return INPUT_KEY_SUBTRACT;
    case GLFW_KEY_0: return INPUT_KEY_0;
    case GLFW_KEY_KP_0: return INPUT_KEY_KP_0;
    default: return INPUT_KEY_NONE;
    }
}

static void on_key(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc;
    g.mods = mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    g.last_activity = glfwGetTime();
    Editor *e = cur();

    WaveShortcut shortcut = input_shortcut(
        input_key_from_glfw(key),
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0,
        (mods & GLFW_MOD_CONTROL) != 0,
        (mods & GLFW_MOD_ALT) != 0,
        (mods & GLFW_MOD_SHIFT) != 0);

    if (shortcut == SHORTCUT_COPY) {
        switch (input_clipboard_target(overlay_active(&g.overlay) != OVERLAY_NONE,
                                       g.cmd.active, e->buf != NULL)) {
        case INPUT_CLIPBOARD_OVERLAY:
            glfwSetClipboardString(g.win, overlay_query(&g.overlay));
            break;
        case INPUT_CLIPBOARD_COMMAND:
            glfwSetClipboardString(g.win, command_text(&g.cmd));
            break;
        case INPUT_CLIPBOARD_EDITOR:
            editor_copy_selection(e);
            break;
        case INPUT_CLIPBOARD_NONE:
            break;
        }
        return;
    }
    if (shortcut == SHORTCUT_PASTE) {
        const char *clip = glfwGetClipboardString(g.win);
        if (!clip) return;
        switch (input_clipboard_target(overlay_active(&g.overlay) != OVERLAY_NONE,
                                       g.cmd.active, e->buf != NULL)) {
        case INPUT_CLIPBOARD_OVERLAY:
            overlay_insert_text(&g.overlay, g.ws, ws_root(g.ws), clip);
            break;
        case INPUT_CLIPBOARD_COMMAND:
            command_insert_text(&g.cmd, clip);
            break;
        case INPUT_CLIPBOARD_EDITOR: {
            EditorPasteResult paste = editor_paste_text(e, clip, g.modal.mode == MODE_VISUAL);
            if (editor_paste_enters_insert(paste)) modal_enter_insert(&g.modal);
            break;
        }
        case INPUT_CLIPBOARD_NONE:
            break;
        }
        return;
    }
    if (shortcut == SHORTCUT_PALETTE) { palette_open(); return; }
    if (shortcut == SHORTCUT_SEARCH) { search_open(); return; }
    if (shortcut == SHORTCUT_TOGGLE_SIDEBAR) {
        wave_config_toggle_sidebar(&g.config);
        config_save();
        return;
    }
    if (shortcut == SHORTCUT_SAVE) {
        if (e->path) editor_save_file(e, &g.watch);
        return;
    }
    if (shortcut == SHORTCUT_TAB_NEXT) { tab_goto(+1); return; }
    if (shortcut == SHORTCUT_TAB_PREV) { tab_goto(-1); return; }
    if (shortcut == SHORTCUT_CLOSE_TAB) { close_tab(tabs_active_index(&g.tabs)); return; }
    if (shortcut == SHORTCUT_UNDO || shortcut == SHORTCUT_REDO) {
        editor_apply_history_action(cur(), shortcut == SHORTCUT_REDO,
                                    g.info, sizeof g.info);
        return;
    }
    if (shortcut == SHORTCUT_ZOOM_IN) { ui_zoom(+1); return; }
    if (shortcut == SHORTCUT_ZOOM_OUT) { ui_zoom(-1); return; }
    if (shortcut == SHORTCUT_ZOOM_RESET) { ui_zoom(0); return; }
    if (shortcut == SHORTCUT_TOGGLE_WRAP) {
        wave_config_toggle_wrap(&g.config);
        config_save();
        wave_config_wrap_text(&g.config, g.info, sizeof g.info);
        return;
    }

    if (overlay_active(&g.overlay) != OVERLAY_NONE) {
        OverlayKind active = overlay_active(&g.overlay);
        OverlayKeyResult overlay_key = overlay_apply_key(
            &g.overlay, g.ws, ws_root(g.ws), overlay_key_from_glfw(key));
        if (overlay_key == OVERLAY_KEY_RESULT_ACCEPT) {
            if (active == OVERLAY_PALETTE) palette_accept();
            else if (active == OVERLAY_SEARCH) search_accept();
        }
        return;
    }

    /* command line */
    if (g.cmd.active) {
        CommandKeyResult cmd_key = command_apply_key(&g.cmd, command_key_from_glfw(key));
        if (cmd_key == COMMAND_KEY_RESULT_ACCEPT) cmd_exec();
        return;
    }

    if (!e->buf) return;

    if (popover_apply_key(&g.pop, popover_key_from_glfw(key),
                          g.modal.mode == MODE_INSERT))
        return;

    if (key == GLFW_KEY_ESCAPE) {
        modal_enter_normal(&g.modal);
        g.info[0] = '\0';
        e->group_open = 0;
        return;
    }

    if (g.modal.mode == MODE_INSERT) {
        editor_apply_insert_key(e, editor_key_from_glfw(key));
        return;
    }

    /* NORMAL / VISUAL: arrows also move (convenience) */
    if (editor_apply_motion_key(e, editor_key_from_glfw(key))) g.info[0] = '\0';
}

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    LayoutScrollTarget target = layout_scroll_target(
        &g.layout, (float)mx * g.fb_scale, dy,
        overlay_active(&g.overlay) == OVERLAY_SEARCH,
        g.pop.active && g.pop.total_rows > g.pop.vis_rows,
        g.ws != NULL, g.config.show_sidebar);

    if (target.kind == LAYOUT_SCROLL_SEARCH) {
        overlay_move(&g.overlay, target.units);
        return;
    }
    if (target.kind == LAYOUT_SCROLL_POPOVER) {
        popover_scroll(&g.pop, target.units);
        return;
    }
    if (target.kind == LAYOUT_SCROLL_SIDEBAR) {
        g.side_scroll += target.pixels;
        if (g.side_scroll < 0) g.side_scroll = 0;
    } else {
        cur()->scroll_y += target.pixels;
        if (cur()->scroll_y < 0) cur()->scroll_y = 0;
    }
}

static void on_cursor_pos(GLFWwindow *w, double mx, double my) {
    (void)w;
    if (!g.mouse_down) return;
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;

    if (!g.mouse_dragging &&
        !layout_drag_should_start(g.mouse_x, g.mouse_y, x, y, g.fb_scale))
        return;

    Editor *e = cur();
    if (!e->buf) return;

    if (!editor_apply_drag_selection(
            e, g.mouse_anchor, x, y, g.layout.text_x, g.layout.top_pad,
            g.layout.line_h, g.layout.adv,
            layout_text_autoscroll_delta(&g.layout, y)))
        return;
    g.mouse_dragging = 1;
    modal_enter_visual(&g.modal);
    g.last_activity = glfwGetTime();
}

static void on_mouse(GLFWwindow *w, int button, int action, int mods) {
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        g.mouse_down = 0;
        g.mouse_dragging = 0;
        return;
    }
    if (action != GLFW_PRESS) return;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;

    if (g.pop.active) popover_close(&g.pop); /* a click anywhere dismisses the popover */

    LayoutClickTarget click = layout_click_target(
        &g.layout, x, y, g.side_scroll, button == GLFW_MOUSE_BUTTON_RIGHT,
        g.ws != NULL, g.config.show_sidebar);

    /* Title-bar band: our GL view covers it, so reproduce the native gestures —
     * drag to move, double-click to zoom/minimise (per System Settings), and
     * right-/control-click for the document path menu. */
    if (click.kind == LAYOUT_CLICK_TITLEBAR) {
        void *nw = glfwGetCocoaWindow(w);
        if (click.titlebar_right) { mac_window_titlebar_menu(nw); return; }
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        double now = glfwGetTime();
        if (layout_point_double_click(&g.title_click, now, x, y, 0.4, 6.0f))
            mac_window_titlebar_doubleclick(nw);
        else mac_window_drag(nw);
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    g.last_activity = glfwGetTime();

    if (click.kind == LAYOUT_CLICK_SIDEBAR) {
        const WsEntry *e = ws_visible(g.ws, (size_t)click.row);
        if (!e) return;
        /* single click peeks; double click on the same file row pins it. */
        double now = glfwGetTime();
        int dbl = !e->is_dir &&
                  layout_index_double_click(&g.sidebar_click, now, click.row, 0.4);
        WsClickAction action = ws_click_visible(g.ws, click.row, dbl);
        if (action.kind == WS_CLICK_OPEN_FILE && action.entry) {
            char *full = ws_fullpath(g.ws, action.entry->rel);
            open_path_mode(full, action.preview);
            free(full);
        }
        return;
    }

    /* tab strip: click to switch; click the trailing 'x' to close */
    if (click.kind == LAYOUT_CLICK_TAB) {
        int idx = click.tab_index;
        if (idx >= 0 && idx < tabs_count(&g.tabs)) {
            if (click.tab_close) close_tab(idx);
            else { tabs_set_active(&g.tabs, idx); modal_enter_normal(&g.modal); }
        }
        return;
    }

    /* click in the text area: map (visual row, x) back to a byte offset */
    if (cur()->buf && cur()->vstart && click.kind == LAYOUT_CLICK_TEXT) {
        Editor *e = cur();
        size_t off = 0;
        if (!editor_apply_click_position(e, x, y, g.layout.text_x,
                                         g.layout.top_pad, g.layout.line_h,
                                         g.layout.adv, &off))
            return;
        g.mouse_down = 1;
        g.mouse_dragging = 0;
        g.mouse_anchor = off;
        g.mouse_x = x;
        g.mouse_y = y;
        if (g.modal.mode == MODE_VISUAL) modal_enter_normal(&g.modal);
    }
}

/* ---- drawing ---- */
#define MAXSPANS 4096

static void draw_frame(int fb_w, int fb_h) {
    Font *font = g.font;
    Renderer *r = g.rend;
    Editor *e = cur();

    /* keep the native title path menu pointed at the active file (cheap pointer
     * guard so we only touch Cocoa when the front tab actually changes) */
    static const char *shown_path = (const char *)-1;
    if (g.config.native_titlebar && e->path != shown_path) {
        mac_window_set_file(glfwGetCocoaWindow(g.win), e->path);
        shown_path = e->path;
    }

    float line_h = font_line_height(font);
    float ascent = font_ascent(font);
    float adv = font_advance(font);

    LayoutFrameMetrics metrics = layout_frame_metrics(
        &g.layout, line_h, adv, ascent, fb_h, g.ws != NULL, g.config.show_sidebar,
        g.config.side_cells, g.config.native_titlebar, g.fb_scale, tabs_count(&g.tabs));
    float side_px = metrics.side_px;
    float gutter = metrics.gutter;
    float pad = metrics.pad;
    float text_x = metrics.text_x;
    float tab_h = metrics.tab_h;
    float header_h = metrics.header_h;
    float tab_strip = metrics.tab_strip;
    float top_pad = metrics.top_pad;

    renderer_begin(r, fb_w, fb_h, 0.11f, 0.12f, 0.14f, g.config.opacity);

    if (side_px > 0)
        draw_sidebar_panel(g.ws, e->path, g.config.side_cells, g.side_scroll,
                           fb_h, font, r, adv, line_h, ascent, side_px,
                           header_h, g.layout.side_pad, g.config.opacity);
    if (tab_strip > 0)
        g.layout.tab_w = draw_tabs_panel(&g.tabs, fb_w, font, r, side_px, adv,
                                         ascent, tab_h, header_h,
                                         g.config.opacity);
    if (header_h > 0)
        draw_header_panel(g.ws ? ws_root(g.ws) : NULL, e ? e->path : NULL,
                          fb_w, font, r, adv, ascent, header_h, g.fb_scale,
                          g.config.opacity);

    if (!e->buf) { /* empty state: a folder is open but no file has been chosen */
        ViewEmptyState empty = view_empty_state(g.ws != NULL);
        ViewEmptyLayout empty_layout = view_empty_layout(
            (float)fb_w, (float)fb_h, side_px, top_pad, adv, line_h, &empty);
        draw_text_run(font, r, empty.title, (int)strlen(empty.title),
                      empty_layout.title_x, empty_layout.title_y,
                      (Color){0.62f, 0.66f, 0.72f});
        draw_text_run(font, r, empty.hint, (int)strlen(empty.hint),
                      empty_layout.hint_x, empty_layout.hint_y,
                      (Color){0.42f, 0.46f, 0.52f});
        /* The Cmd-P palette and Cmd-Shift-F search are reachable from the empty
         * state too — the primary ways to open a file here — so draw whichever
         * is active on top before bailing. */
        if (overlay_active(&g.overlay) == OVERLAY_PALETTE)
            draw_palette_panel(&g.overlay, g.ws, fb_w, font, r, adv, line_h, ascent);
        else if (overlay_active(&g.overlay) == OVERLAY_SEARCH)
            draw_search_panel(&g.overlay, fb_w, font, r, adv, line_h, ascent);
        renderer_flush(r);
        return;
    }

    TextFrameView text;
    if (!text_view_prepare(e, g.modal.mode == MODE_VISUAL, g.config.wrap,
                           (float)fb_w, text_x, adv, (float)fb_h, top_pad,
                           line_h, layout_status_bar_h(&g.layout), &text))
        return;
    const PieceTable *pt = text.pt;
    static HighlightSpan spans[MAXSPANS];
    size_t nspans = e->hl ? hl_spans(e->hl, text.window.byte_start,
                                      text.window.byte_end, spans, MAXSPANS) : 0;
    if (nspans > MAXSPANS) nspans = MAXSPANS; /* never index past the buffer */
    static Diagnostic diags[MAXDIAG];
    size_t ndiag = e->hl ? hl_diagnostics(e->hl, diags, MAXDIAG) : 0;
    if (ndiag > MAXDIAG) ndiag = MAXDIAG;

    /* If a language server has reported on this file, it is authoritative —
     * replace the tree-sitter syntax diagnostics with the server's. */
    if (e->path) {
        static LspDiag ld[MAXDIAG];
        int published = 0;
        size_t nl = lsp_manager_diagnostics(&g.lsp, e, ld, MAXDIAG, &published);
        ndiag = diagnostics_apply_lsp(diags, ndiag, MAXDIAG, ld, nl, published);
    }

    /* cursor blink: solid for the first half-second after any input, then a
     * ~1Hz on/off. Always solid when blinking is disabled (snapshot mode). */
    int cursor_on = view_cursor_visible(g.blink, glfwGetTime(), g.last_activity);

    draw_editor_text_panel(e, &text, spans, nspans, diags, ndiag,
                           g.modal.mode == MODE_INSERT, cursor_on, fb_h,
                           font, r, side_px, gutter, text_x, top_pad,
                           line_h, adv, ascent);

    /* status / command line */
    float bar_h = line_h + 6.0f;
    renderer_rect(r, 0, (float)fb_h - bar_h, (float)fb_w, bar_h, 0.07f, 0.08f, 0.10f, g.config.opacity);
    char status[400];
    size_t crow, ccol;
    pt_offset_to_rowcol(pt, e->cursor, &crow, &ccol);
    ViewStatusLine status_line = view_status_line(
        status, sizeof status, e, g.cmd.active ? command_text(&g.cmd) : NULL,
        g.info, mode_name(g.modal.mode), crow, ccol, ndiag,
        tabs_active_index(&g.tabs), tabs_count(&g.tabs));
    draw_text_run(font, r, status, (int)strlen(status), pad,
                  (float)fb_h - bar_h + ascent + 2.0f,
                  (Color){status_line.r, status_line.g, status_line.b});

    /* the `gh` popover floats over the text, anchored to the cursor (kept on
     * screen even when the cursor sits near an edge). Hidden under the palette. */
    if (overlay_active(&g.overlay) == OVERLAY_NONE) {
        ViewPoint anchor = view_popover_anchor(text_x, top_pad, (float)fb_h, bar_h,
                                               adv, line_h, text.cursor.vrow,
                                               text.cursor.xcol, e->scroll_y);
        draw_popover_panel(&g.pop, fb_w, fb_h, font, r, adv, line_h, ascent,
                           top_pad, bar_h, anchor.x, anchor.y,
                           g.layout.side_px, g.fb_scale);
    }

    if (overlay_active(&g.overlay) == OVERLAY_PALETTE)
        draw_palette_panel(&g.overlay, g.ws, fb_w, font, r, adv, line_h, ascent);
    else if (overlay_active(&g.overlay) == OVERLAY_SEARCH)
        draw_search_panel(&g.overlay, fb_w, font, r, adv, line_h, ascent);

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
    lsp_manager_init(&g.lsp, snapshot && !getenv("WAVE_LSP"));
    g.blink = (snapshot == NULL); /* keep the cursor solid for deterministic snapshots */

    modal_init(&g.modal);
    overlay_init(&g.overlay);
    wave_config_defaults(&g.config);
    watch_service_init(&g.watch);
    g.persist = (snapshot == NULL) || getenv("WAVE_PERSIST"); /* off in snapshots unless forced */
    if (g.persist) wave_config_load(&g.config); /* saved prefs override defaults */
    { const char *sc = getenv("WAVE_SCALE"); if (sc) { float v = (float)atof(sc); if (v > 0.1f) g.config.ui_scale = v; } }

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

    if (g.config.blur) mac_set_blur(glfwGetCocoaWindow(win), 1);
    if (g.config.native_titlebar) mac_use_native_titlebar(glfwGetCocoaWindow(win), 1);

    g.font = font_load(FONT_PATH, g.config.base_pt * g.fb_scale * g.config.ui_scale);
    if (!g.font) { fprintf(stderr, "font load failed\n"); return 1; }

    if (arg) {
        WsOpenContext opened = ws_open_context(arg);
        g.ws = opened.workspace;
        if (g.ws) {
            lsp_manager_set_root_path(&g.lsp, ws_root(g.ws));
            workspace_watch_start();
            if (opened.kind == WS_OPEN_FILE) open_path(opened.file);
        }
    }
    if (tabs_count(&g.tabs) == 0) {
        /* Keep cur() valid even for a workspace empty state; buf stays NULL so
         * draw_frame renders the placeholder until the user opens a file. */
        tabs_new(&g.tabs);
        if (!g.ws) {
            cur()->buf = buffer_new();
            modal_enter_insert(&g.modal);
        }
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
            modal_enter_insert(&g.modal);
            for (const char *p = typed; *p; p++) {
                if (*p == '\\' && p[1] == 'n') { ed_insert(cur(), "\n", 1); p++; }
                else ed_insert(cur(), p, 1);
            }
        }
        if (cur()->hl) hl_update(cur()->hl);
        /* WAVE_KEYS: a run of NORMAL-mode keystrokes (e.g. "ggwgd"). */
        const char *keys = getenv("WAVE_KEYS");
        if (keys && cur()->buf) {
            modal_enter_normal(&g.modal);
            for (const char *p = keys; *p; p++) {
                if (cur()->hl) hl_update(cur()->hl);
                vim_normal_char((unsigned int)(unsigned char)*p);
            }
        }
        if (getenv("WAVE_PALETTE")) palette_open();
        /* WAVE_QUERY: prefill the palette query to verify fuzzy ranking. */
        const char *pq = getenv("WAVE_QUERY");
        if (pq && overlay_active(&g.overlay) == OVERLAY_PALETTE) {
            overlay_set_palette_query(&g.overlay, g.ws, pq);
        }
        /* WAVE_SEARCH: open the content-search overlay, run the query, and pump
         * ripgrep to completion so the single-frame snapshot shows real hits. */
        const char *sq = getenv("WAVE_SEARCH");
        if (sq) {
            search_open();
            overlay_set_search_query(&g.overlay, ws_root(g.ws), sq);
            for (int i = 0; i < 500 && overlay_search_running(&g.overlay); i++) {
                overlay_poll_search(&g.overlay);
                if (!overlay_search_running(&g.overlay)) break;
                usleep(10000);
            }
            overlay_poll_search(&g.overlay);
            const char *ss = getenv("WAVE_SEARCHSEL");
            if (ss) g.overlay.search.sel = atoi(ss);
        }
        /* WAVE_POPTEST: force-open the popover with given text (\n = newline,
         * | = paragraph break) to verify max-height / scroll / placement. */
        const char *poptest = getenv("WAVE_POPTEST");
        if (poptest && cur()->buf) {
            popover_set_encoded_base(&g.pop, poptest);
            popover_set_loading(&g.pop, 0);
            popover_compose(&g.pop, NULL);
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
    glfwSetCursorPosCallback(win, on_cursor_pos);
    glfwSetMouseButtonCallback(win, on_mouse);

    while (!glfwWindowShouldClose(win)) {
        process_file_watchers();
        process_workspace_watchers();
        Editor *e = cur();
        if (g.mouse_down && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
            g.mouse_down = 0;
            g.mouse_dragging = 0;
        }
        /* re-assert the blur each frame: macOS clears it on some window events */
        if (g.config.blur) mac_set_blur(glfwGetCocoaWindow(win), 1);
        if (e->hl && (e->dirty || buffer_pending_edits(e->buf) > 0)) {
            hl_update(e->hl);
            e->dirty = 0;
        }

        /* drain language tooling and apply any UI-facing results */
        LspLocation loc;
        char hv[1024];
        int lsp_events = lsp_manager_poll(&g.lsp, &loc, hv, sizeof hv);
        if (lsp_events & LSP_MANAGER_EVENT_DEFINITION) {
            if (open_path(loc.path) == 0) {
                Editor *te = cur();
                if (editor_move_to_lsp_position(te, loc.line, loc.col,
                                                g.info, sizeof g.info))
                    center_cursor(te);
            }
        }
        if (lsp_events & LSP_MANAGER_EVENT_HOVER) {
            popover_show_hover(&g.pop, hv);
        }

        /* push a full-document didChange once the active file has edits pending */
        lsp_manager_push_change(&g.lsp, e);

        /* drain any pending ripgrep output for the live search overlay */
        overlay_poll_search(&g.overlay);

        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        draw_frame(fb_w, fb_h);
        glfwSwapBuffers(win);
        /* poll faster while a server or a search is active so results stream in */
        glfwWaitEventsTimeout(
            (lsp_manager_active(&g.lsp) || overlay_search_running(&g.overlay)) ? 0.03 : 0.1);
    }

    renderer_free(g.rend);
    lsp_manager_shutdown(&g.lsp);
    overlay_free(&g.overlay);
    tabs_free(&g.tabs);
    watch_service_shutdown(&g.watch);
    if (g.ws) ws_free(g.ws);
    font_free(g.font);
    yank_free(&g.yank);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
