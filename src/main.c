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
#include "input_glfw.h"
#include "layout.h"
#include "lsp.h"
#include "lsp_manager.h"
#include "mode.h"
#include "overlay.h"
#include "popover.h"
#include "render.h"
#include "runtime.h"
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
    LayoutDragState mouse_drag;
    LayoutPointClick title_click;
    WsSidebarClickState sidebar_click;

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
    TabActionEffect effect = tabs_close_with_effect(&g.tabs, i);
    if (effect.close_window) glfwSetWindowShouldClose(g.win, GLFW_TRUE);
    if (effect.reset_mode) modal_enter_normal(&g.modal);
}

static void tab_goto(int delta) {
    TabActionEffect effect = tabs_goto_with_effect(&g.tabs, delta);
    if (effect.reset_mode) modal_enter_normal(&g.modal);
}

static void process_file_watchers(void) {
    TabWatchEffect effect = tabs_process_file_watchers(
        &g.tabs, &g.watch, glfwGetTime(), &g.next_watch, 0.5);
    if (effect.reset_mode) modal_enter_normal(&g.modal);
    if (effect.has_message) snprintf(g.info, sizeof g.info, "%s", effect.message);
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
    WsReloadEffect effect = ws_apply_watch_event(g.ws, watch_workspace_consume(&g.watch));
    if (!effect.ok && !effect.message[0]) return;
    if (effect.refilter_palette && overlay_active(&g.overlay) == OVERLAY_PALETTE)
        app_palette_refilter();
    snprintf(g.info, sizeof g.info, "%s", effect.message);
}

/* Open `path` (absolute), focusing it if already open. `preview` makes it a
 * transient peek tab (see Editor.preview): the next preview-open reuses the
 * slot instead of stacking another tab. */
static int open_path_mode(const char *path, int preview) {
    TabOpenResult result = tabs_open_file(&g.tabs, path, preview, &g.watch);
    if (!result.ok) {
        fprintf(stderr, "could not open %s\n", path);
        return -1;
    }
    if (result.loaded_file) lsp_manager_open_editor(&g.lsp, result.editor);
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

    if (effect.write && editor_has_path(cur())) {
        editor_save_file(cur(), &g.watch);
    }
    command_close(&g.cmd);
    switch (command_close_action(effect)) {
    case COMMAND_CLOSE_WINDOW:
        glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        break;
    case COMMAND_CLOSE_TAB:
        close_tab(tabs_active_index(&g.tabs)); /* closes the window when the last tab goes */
        break;
    case COMMAND_CLOSE_NONE:
    default:
        break;
    }
}

#define MAXDIAG 256

static void show_info(void) {
    Editor *e = cur();
    char base[sizeof g.pop.base];
    LspManagerHoverInfo info = lsp_manager_hover_info(&g.lsp, e, base, sizeof base);
    if (info.ok) popover_show_base(&g.pop, base, info.loading);
}

static void goto_definition(void) {
    Editor *e = cur();
    if (!editor_has_buffer(e)) return;

    /* prefer the language server; the async reply is applied in the main loop */
    if (lsp_manager_request_definition_at_cursor(&g.lsp, e, g.info, sizeof g.info))
        return;

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
    edit_command_status_text(res, g.info, sizeof g.info);
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
        editor_has_buffer(cur()),
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

static void on_key(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc;
    g.mods = mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    g.last_activity = glfwGetTime();
    Editor *e = cur();

    WaveShortcut shortcut = input_shortcut(
        wave_input_key_from_glfw(key),
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0,
        (mods & GLFW_MOD_CONTROL) != 0,
        (mods & GLFW_MOD_ALT) != 0,
        (mods & GLFW_MOD_SHIFT) != 0);

    InputKeyTarget target = input_key_target(
        shortcut, overlay_active(&g.overlay) != OVERLAY_NONE,
        g.cmd.active, editor_has_buffer(e));
    InputShortcutAction shortcut_action =
        input_shortcut_action(shortcut, editor_has_path(e));

    if (target == INPUT_KEY_TARGET_SHORTCUT &&
        shortcut_action == INPUT_SHORTCUT_ACTION_COPY) {
        switch (input_clipboard_target(overlay_active(&g.overlay) != OVERLAY_NONE,
                                       g.cmd.active, editor_has_buffer(e))) {
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
    if (target == INPUT_KEY_TARGET_SHORTCUT &&
        shortcut_action == INPUT_SHORTCUT_ACTION_PASTE) {
        const char *clip = glfwGetClipboardString(g.win);
        if (!clip) return;
        switch (input_clipboard_target(overlay_active(&g.overlay) != OVERLAY_NONE,
                                       g.cmd.active, editor_has_buffer(e))) {
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
    if (target == INPUT_KEY_TARGET_SHORTCUT) {
        switch (shortcut_action) {
        case INPUT_SHORTCUT_ACTION_OPEN_PALETTE:
            palette_open();
            return;
        case INPUT_SHORTCUT_ACTION_OPEN_SEARCH:
            search_open();
            return;
        case INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR:
            wave_config_toggle_sidebar(&g.config);
            config_save();
            return;
        case INPUT_SHORTCUT_ACTION_SAVE:
            editor_save_file(e, &g.watch);
            return;
        case INPUT_SHORTCUT_ACTION_TAB_NEXT:
            tab_goto(+1);
            return;
        case INPUT_SHORTCUT_ACTION_TAB_PREV:
            tab_goto(-1);
            return;
        case INPUT_SHORTCUT_ACTION_CLOSE_TAB:
            close_tab(tabs_active_index(&g.tabs));
            return;
        case INPUT_SHORTCUT_ACTION_UNDO:
        case INPUT_SHORTCUT_ACTION_REDO:
            editor_apply_history_action(e, shortcut_action == INPUT_SHORTCUT_ACTION_REDO,
                                        g.info, sizeof g.info);
            return;
        case INPUT_SHORTCUT_ACTION_ZOOM_IN:
            ui_zoom(+1);
            return;
        case INPUT_SHORTCUT_ACTION_ZOOM_OUT:
            ui_zoom(-1);
            return;
        case INPUT_SHORTCUT_ACTION_ZOOM_RESET:
            ui_zoom(0);
            return;
        case INPUT_SHORTCUT_ACTION_TOGGLE_WRAP:
            wave_config_toggle_wrap(&g.config);
            config_save();
            wave_config_wrap_text(&g.config, g.info, sizeof g.info);
            return;
        case INPUT_SHORTCUT_ACTION_NONE:
        case INPUT_SHORTCUT_ACTION_COPY:
        case INPUT_SHORTCUT_ACTION_PASTE:
        default:
            return;
        }
    }

    if (target == INPUT_KEY_TARGET_OVERLAY) {
        OverlayKind active = overlay_active(&g.overlay);
        OverlayKeyResult overlay_key = overlay_apply_key(
            &g.overlay, g.ws, ws_root(g.ws), wave_overlay_key_from_glfw(key));
        if (overlay_key == OVERLAY_KEY_RESULT_ACCEPT) {
            if (active == OVERLAY_PALETTE) palette_accept();
            else if (active == OVERLAY_SEARCH) search_accept();
        }
        return;
    }

    /* command line */
    if (target == INPUT_KEY_TARGET_COMMAND) {
        CommandKeyResult cmd_key = command_apply_key(&g.cmd, wave_command_key_from_glfw(key));
        if (cmd_key == COMMAND_KEY_RESULT_ACCEPT) cmd_exec();
        return;
    }

    if (target != INPUT_KEY_TARGET_EDITOR) return;

    if (popover_apply_key(&g.pop, wave_popover_key_from_glfw(key),
                          g.modal.mode == MODE_INSERT))
        return;

    if (key == GLFW_KEY_ESCAPE) {
        modal_enter_normal(&g.modal);
        g.info[0] = '\0';
        editor_cancel_group(e);
        return;
    }

    if (g.modal.mode == MODE_INSERT) {
        editor_apply_insert_key(e, wave_editor_key_from_glfw(key));
        return;
    }

    /* NORMAL / VISUAL: arrows also move (convenience) */
    if (editor_apply_motion_key(e, wave_editor_key_from_glfw(key))) g.info[0] = '\0';
}

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    LayoutScrollTarget target = layout_scroll_target(
        &g.layout, (float)mx * g.fb_scale, dy,
        overlay_active(&g.overlay) == OVERLAY_SEARCH,
        popover_is_scrollable(&g.pop),
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
        g.side_scroll = layout_scroll_offset(g.side_scroll, target.pixels);
    } else {
        editor_set_scroll_y(cur(), layout_scroll_offset(editor_scroll_y(cur()), target.pixels));
    }
}

static void on_cursor_pos(GLFWwindow *w, double mx, double my) {
    (void)w;
    if (!g.mouse_drag.down) return;
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;

    if (!layout_drag_update(&g.mouse_drag, x, y, g.fb_scale)) return;

    Editor *e = cur();
    if (!editor_has_buffer(e)) return;

    if (!editor_apply_drag_selection(
            e, g.mouse_drag.anchor, x, y, g.layout.text_x, g.layout.top_pad,
            g.layout.line_h, g.layout.adv,
            layout_text_autoscroll_delta(&g.layout, y)))
        return;
    modal_enter_visual(&g.modal);
    g.last_activity = glfwGetTime();
}

static void on_mouse(GLFWwindow *w, int button, int action, int mods) {
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        layout_drag_release(&g.mouse_drag);
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
        WsClickAction action = ws_click_visible_timed(
            g.ws, &g.sidebar_click, click.row, glfwGetTime(), 0.4);
        if (action.kind == WS_CLICK_OPEN_FILE && action.entry) {
            char *full = ws_fullpath(g.ws, action.entry->rel);
            open_path_mode(full, action.preview);
            free(full);
        }
        return;
    }

    /* tab strip: click to switch; click the trailing 'x' to close */
    if (click.kind == LAYOUT_CLICK_TAB) {
        TabActionEffect effect = tabs_click_with_effect(
            &g.tabs, click.tab_index, click.tab_close);
        if (effect.close_window) glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        if (effect.reset_mode) modal_enter_normal(&g.modal);
        return;
    }

    /* click in the text area: map (visual row, x) back to a byte offset */
    if (editor_has_buffer(cur()) && editor_has_visual_rows(cur()) &&
        click.kind == LAYOUT_CLICK_TEXT) {
        Editor *e = cur();
        size_t off = 0;
        if (!editor_apply_click_position(e, x, y, g.layout.text_x,
                                         g.layout.top_pad, g.layout.line_h,
                                         g.layout.adv, &off))
            return;
        layout_drag_begin(&g.mouse_drag, off, x, y);
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
    if (g.config.native_titlebar && editor_path(e) != shown_path) {
        mac_window_set_file(glfwGetCocoaWindow(g.win), editor_path(e));
        shown_path = editor_path(e);
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
        draw_sidebar_panel(g.ws, editor_path(e), g.config.side_cells, g.side_scroll,
                           fb_h, font, r, adv, line_h, ascent, side_px,
                           header_h, g.layout.side_pad, g.config.opacity);
    if (tab_strip > 0)
        g.layout.tab_w = draw_tabs_panel(&g.tabs, fb_w, font, r, side_px, adv,
                                         ascent, tab_h, header_h,
                                         g.config.opacity);
    if (header_h > 0)
        draw_header_panel(g.ws ? ws_root(g.ws) : NULL, editor_path(e),
                          fb_w, font, r, adv, ascent, header_h, g.fb_scale,
                          g.config.opacity);

    if (!editor_has_buffer(e)) { /* empty state: a folder is open but no file has been chosen */
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
    static HighlightSpan spans[MAXSPANS];
    size_t nspans = editor_highlight_spans(e, text.window.byte_start,
                                           text.window.byte_end, spans, MAXSPANS);

    static Diagnostic diags[MAXDIAG];
    size_t ndiag = lsp_manager_editor_diagnostics(&g.lsp, e, diags, MAXDIAG);

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
    ViewStatusLine status_line = view_editor_status_line(
        status, sizeof status, e, g.cmd.active ? command_text(&g.cmd) : NULL,
        g.info, mode_name(g.modal.mode), ndiag,
        tabs_active_index(&g.tabs), tabs_count(&g.tabs));
    draw_text_run(font, r, status, (int)strlen(status), pad,
                  (float)fb_h - bar_h + ascent + 2.0f,
                  (Color){status_line.r, status_line.g, status_line.b});

    /* the `gh` popover floats over the text, anchored to the cursor (kept on
     * screen even when the cursor sits near an edge). Hidden under the palette. */
    if (overlay_active(&g.overlay) == OVERLAY_NONE) {
        ViewPoint anchor = view_popover_anchor(text_x, top_pad, (float)fb_h, bar_h,
                                               adv, line_h, text.cursor.vrow,
                                               text.cursor.xcol, editor_scroll_y(e));
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

int main(int argc, char **argv) {
    const char *arg = argc > 1 ? argv[1] : NULL;

    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

    const char *snapshot = getenv("WAVE_SNAPSHOT");
    WaveRuntimeOptions runtime = wave_runtime_options(
        snapshot, getenv("WAVE_LSP"), getenv("WAVE_PERSIST"), getenv("WAVE_SCALE"));
    if (runtime.snapshot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    lsp_manager_init(&g.lsp, runtime.lsp_disabled);
    g.blink = runtime.blink;

    modal_init(&g.modal);
    overlay_init(&g.overlay);
    wave_config_defaults(&g.config);
    watch_service_init(&g.watch);
    g.persist = runtime.persist;
    if (g.persist) wave_config_load(&g.config); /* saved prefs override defaults */
    if (runtime.has_scale_override) g.config.ui_scale = runtime.scale_override;

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
        TabStartupEffect startup = tabs_ensure_startup(&g.tabs, g.ws != NULL);
        if (startup.enter_insert) modal_enter_insert(&g.modal);
    }

    g.rend = renderer_new(g.font);
    if (!g.rend) { fprintf(stderr, "renderer init failed\n"); return 1; }

    if (runtime.snapshot) {
        WaveRuntimeSnapshotScript script = wave_runtime_snapshot_script(
            getenv("WAVE_OPEN"), getenv("WAVE_TYPE"), getenv("WAVE_KEYS"),
            getenv("WAVE_PALETTE"), getenv("WAVE_QUERY"), getenv("WAVE_SEARCH"),
            getenv("WAVE_SEARCHSEL"), getenv("WAVE_POPTEST"), getenv("WAVE_POPSCROLL"));

        for (int i = 0; i < script.opens.count; i++) open_path(script.opens.paths[i]);
        if (script.typed && editor_has_buffer(cur())) {
            modal_enter_insert(&g.modal);
            editor_insert_encoded_text(cur(), script.typed);
        }
        editor_refresh_highlighter(cur());
        if (script.keys && editor_has_buffer(cur())) {
            modal_enter_normal(&g.modal);
            for (const char *p = script.keys; *p; p++) {
                editor_refresh_highlighter(cur());
                vim_normal_char((unsigned int)(unsigned char)*p);
            }
        }
        if (script.palette) palette_open();
        if (script.palette_query && overlay_active(&g.overlay) == OVERLAY_PALETTE) {
            overlay_set_palette_query(&g.overlay, g.ws, script.palette_query);
        }
        if (script.search_query) {
            search_open();
            overlay_set_search_query(&g.overlay, ws_root(g.ws), script.search_query);
            overlay_settle_search(&g.overlay);
            overlay_set_search_selection(&g.overlay, script.search_selection);
        }
        if (script.popover_text && editor_has_buffer(cur())) {
            popover_show_encoded_base(&g.pop, script.popover_text, script.popover_scroll);
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
        if (g.mouse_drag.down && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
            layout_drag_release(&g.mouse_drag);
        /* re-assert the blur each frame: macOS clears it on some window events */
        if (g.config.blur) mac_set_blur(glfwGetCocoaWindow(win), 1);
        editor_update_highlighter(e);

        /* drain language tooling and apply any UI-facing results */
        LspManagerUpdate lsp_update = lsp_manager_update_ui(&g.lsp, e);
        if (lsp_update.has_definition) {
            if (open_path(lsp_update.definition.path) == 0) {
                Editor *te = cur();
                if (editor_move_to_lsp_position(te, lsp_update.definition.line,
                                                lsp_update.definition.col,
                                                g.info, sizeof g.info))
                    center_cursor(te);
            }
        }
        if (lsp_update.has_hover) {
            popover_show_hover(&g.pop, lsp_update.hover);
        }

        /* drain any pending ripgrep output for the live search overlay */
        overlay_poll_search(&g.overlay);

        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        draw_frame(fb_w, fb_h);
        glfwSwapBuffers(win);
        /* poll faster while a server or a search is active so results stream in */
        glfwWaitEventsTimeout(wave_runtime_wait_timeout(
            lsp_manager_active(&g.lsp), overlay_search_running(&g.overlay)));
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
