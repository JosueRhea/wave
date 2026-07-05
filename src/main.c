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
int mac_sidebar_context_menu(void *nswindow, const char *target_name,
                             int is_dir, int has_target, int has_clipboard);
int mac_open_panel(void *nswindow, int folders, char *out, size_t cap);
int mac_confirm_delete(void *nswindow, const char *path);
int mac_clipboard_has_image(void);
typedef void (*MacUpdateCallback)(int state, const char *version,
                                  const char *detail, double progress);
void mac_install_app_menu(void (*open_file)(void), void (*open_folder)(void),
                          void (*check_updates)(void), void (*next_tab)(void),
                          void (*prev_tab)(void));
void mac_check_for_updates(const char *current_version, int manual,
                           MacUpdateCallback callback);
#else
static void mac_set_blur(void *nswindow, int enable) { (void)nswindow; (void)enable; }
static void mac_use_native_titlebar(void *nswindow, int enable) { (void)nswindow; (void)enable; }
static void mac_window_drag(void *nswindow) { (void)nswindow; }
static void mac_window_titlebar_doubleclick(void *nswindow) { (void)nswindow; }
static void mac_window_titlebar_menu(void *nswindow) { (void)nswindow; }
static void mac_window_set_file(void *nswindow, const char *path) { (void)nswindow; (void)path; }
static void *glfwGetCocoaWindow(GLFWwindow *w) { (void)w; return 0; }
static int mac_sidebar_context_menu(void *nswindow, const char *target_name,
                                    int is_dir, int has_target, int has_clipboard) {
    (void)nswindow; (void)target_name; (void)is_dir; (void)has_target; (void)has_clipboard;
    return 0;
}
static int mac_open_panel(void *nswindow, int folders, char *out, size_t cap) {
    (void)nswindow; (void)folders; (void)out; (void)cap; return 0;
}
static int mac_confirm_delete(void *nswindow, const char *path) {
    (void)nswindow; (void)path; return 0;
}
static int mac_clipboard_has_image(void) { return 0; }
typedef void (*MacUpdateCallback)(int state, const char *version,
                                  const char *detail, double progress);
static void mac_install_app_menu(void (*open_file)(void), void (*open_folder)(void),
                                 void (*check_updates)(void),
                                 void (*next_tab)(void), void (*prev_tab)(void)) {
    (void)open_file; (void)open_folder; (void)check_updates;
    (void)next_tab; (void)prev_tab;
}
static void mac_check_for_updates(const char *current_version, int manual,
                                  MacUpdateCallback callback) {
    (void)current_version; (void)manual;
    if (callback) callback(5, "", "updates unavailable on this platform", 0.0);
}
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
#include "recent.h"
#include "render.h"
#include "runtime.h"
#include "tabs.h"
#include "terminal.h"
#include "text_view.h"
#include "theme.h"
#include "updater.h"
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

    /* command line ( : ) and current-buffer search ( / ) */
    CommandLine cmd;
    CommandLine buf_search;
    char last_buf_search[256];

    /* command palette (Cmd-P) and content search (Cmd-Shift-F). */
    OverlayState overlay;

    /* no-folder startup project launcher */
    RecentProjects recent;

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
    float side_scrollbar_hover;
    float editor_scrollbar_hover;
    double last_frame_time;
    int scrollbar_drag;
    float scrollbar_drag_grab_y;
    float scrollbar_drag_max;
    char file_clipboard[4096];
    int file_clipboard_cut;
    int create_active;
    int create_is_dir;
    int create_row;
    int create_depth;
    char create_parent[4096];
    char create_text[1024];
    int update_state;
    int update_show_progress;
    double update_toast_until;
    float update_progress;
    char update_title[128];
    char update_detail[256];
    char update_latest[64];
    /* cached layout from the last frame, for click hit-testing */
    LayoutState layout;
} App;

static App g;

/* The active editor (the front tab). Always valid once a tab exists, so
 * callers never have to null-check the slot itself. */
static Editor *cur(void) { return tabs_current(&g.tabs); }

static int project_launcher_active(void) {
    return g.ws == NULL && cur() && !editor_has_buffer(cur()) &&
           overlay_active(&g.overlay) == OVERLAY_NONE && !g.cmd.active &&
           !g.create_active;
}

static const char *FONT_PATH = "/System/Library/Fonts/SFNSMono.ttf";
#ifndef WAVE_VERSION
#define WAVE_VERSION "0.0.0-dev"
#endif

static void app_palette_refilter(void);

enum {
    UPDATE_STATE_CHECKING = 1,
    UPDATE_STATE_CURRENT = 2,
    UPDATE_STATE_AVAILABLE = 3,
    UPDATE_STATE_DOWNLOADING = 4,
    UPDATE_STATE_ERROR = 5,
    UPDATE_STATE_DOWNLOADED = 6
};

enum {
    SCROLLBAR_DRAG_NONE = 0,
    SCROLLBAR_DRAG_SIDEBAR = 1,
    SCROLLBAR_DRAG_EDITOR = 2
};

static void config_path(char *out, size_t cap) {
    wave_config_path(out, cap);
}

static Terminal *cur_term(void) { return tabs_current_terminal(&g.tabs); }

static const char *terminal_cwd(void) {
    if (g.ws) return ws_root(g.ws);
    Editor *e = cur();
    const char *path = e ? editor_path(e) : NULL;
    if (path && path[0]) {
        static char dir[4096];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) *slash = '\0';
        else snprintf(dir, sizeof dir, ".");
        return dir;
    }
    return ".";
}

static Terminal *open_terminal_tab(const char *label, const char *const argv[]) {
    if (!argv || !argv[0]) return NULL;
    Terminal *t = tabs_new_terminal(&g.tabs, label, terminal_cwd(), argv);
    modal_enter_normal(&g.modal);
    return t;
}

static Terminal *open_shell_tab(void) {
    const char *shell = getenv("SHELL");
    if (!shell || !shell[0]) shell = "/bin/zsh";
    const char *term_argv[] = {shell, NULL};
    return open_terminal_tab("term", term_argv);
}

static Terminal *open_codex_tab(void) {
    const char *codex_argv[] = {"codex", NULL};
    return open_terminal_tab("codex", codex_argv);
}

static Terminal *open_claude_tab(void) {
    const char *claude_argv[] = {"claude", NULL};
    return open_terminal_tab("claude", claude_argv);
}

static int terminal_command(const char *text) {
    if (!text) return 0;
    if (!strcmp(text, "term") || !strcmp(text, "terminal")) {
        open_shell_tab();
        snprintf(g.info, sizeof g.info, "terminal opened");
        return 1;
    }
    if (!strcmp(text, "codex")) {
        open_codex_tab();
        snprintf(g.info, sizeof g.info, "codex opened");
        return 1;
    }
    if (!strcmp(text, "claude")) {
        open_claude_tab();
        snprintf(g.info, sizeof g.info, "claude opened");
        return 1;
    }
    return 0;
}

static void terminal_resize_active(int fb_w, int fb_h, float side_px,
                                   float top_pad, float line_h, float adv) {
    Terminal *t = cur_term();
    if (!t || line_h <= 0.0f || adv <= 0.0f) return;
    float bar_h = line_h + 6.0f;
    float w = (float)fb_w - side_px;
    float h = (float)fb_h - top_pad - bar_h;
    int rows = (int)((h - line_h - 28.0f) / line_h);
    int cols = (int)((w - 28.0f) / adv);
    terminal_resize(t, rows, cols);
}

static void terminal_poll_tabs(void) {
    for (int i = 0; i < tabs_count(&g.tabs); i++) {
        Terminal *t = tabs_terminal_at(&g.tabs, i);
        if (t) terminal_poll(t);
    }
}

static void snapshot_settle_terminal(int millis) {
    if (millis <= 0) return;
    double end = glfwGetTime() + (double)millis / 1000.0;
    while (glfwGetTime() < end) {
        terminal_poll_tabs();
        glfwPollEvents();
        usleep(10000);
    }
}

static void snapshot_terminal_input(const char *input) {
    Terminal *term = cur_term();
    if (!term || !input || !input[0]) return;
    if (!strcmp(input, "shift-tab")) {
        terminal_send_key_mods(term, GLFW_KEY_TAB, 1, 0, 0);
    } else if (!strcmp(input, "tab")) {
        terminal_send_key(term, GLFW_KEY_TAB);
    } else if (!strcmp(input, "enter")) {
        terminal_send_key(term, GLFW_KEY_ENTER);
    } else {
        terminal_write(term, input, strlen(input));
    }
}

static int terminal_tab_running(void) {
    for (int i = 0; i < tabs_count(&g.tabs); i++) {
        Terminal *t = tabs_terminal_at(&g.tabs, i);
        if (t && t->running) return 1;
    }
    return 0;
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

static void app_menu_next_tab(void) { tab_goto(+1); }
static void app_menu_prev_tab(void) { tab_goto(-1); }

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

static void app_menu_open_file(void);
static void app_menu_open_folder(void);
static void app_menu_check_updates(void);

enum {
    SIDEBAR_MENU_NONE = 0,
    SIDEBAR_MENU_OPEN_FILE = 1,
    SIDEBAR_MENU_OPEN_FOLDER = 2,
    SIDEBAR_MENU_NEW_FILE = 3,
    SIDEBAR_MENU_NEW_FOLDER = 4,
    SIDEBAR_MENU_COPY = 5,
    SIDEBAR_MENU_CUT = 6,
    SIDEBAR_MENU_PASTE = 7,
    SIDEBAR_MENU_DELETE = 8
};

static void open_context_path(const char *path) {
    WsOpenContext opened = ws_open_context(path);
    if (!opened.workspace) {
        snprintf(g.info, sizeof g.info, "could not open %s", path ? path : "");
        return;
    }
    if (g.ws) ws_free(g.ws);
    g.ws = opened.workspace;
    lsp_manager_set_root_path(&g.lsp, ws_root(g.ws));
    workspace_watch_start();
    if (recent_projects_add(&g.recent, ws_root(g.ws)) && g.persist)
        recent_projects_save(&g.recent);
    if (opened.kind == WS_OPEN_FILE) open_path(opened.file);
    snprintf(g.info, sizeof g.info, "%s", opened.kind == WS_OPEN_FILE ? "file opened" : "folder opened");
}

static void app_menu_open_file(void) {
    char path[4096];
    if (mac_open_panel(glfwGetCocoaWindow(g.win), 0, path, sizeof path))
        open_context_path(path);
}

static void app_menu_open_folder(void) {
    char path[4096];
    if (mac_open_panel(glfwGetCocoaWindow(g.win), 1, path, sizeof path))
        open_context_path(path);
}

static void update_toast(const char *title, const char *detail,
                         float progress, int show_progress, double seconds) {
    snprintf(g.update_title, sizeof g.update_title, "%s", title ? title : "");
    snprintf(g.update_detail, sizeof g.update_detail, "%s", detail ? detail : "");
    g.update_progress = progress;
    g.update_show_progress = show_progress;
    g.update_toast_until = seconds <= 0.0 ? 0.0 : glfwGetTime() + seconds;
}

static void update_callback(int state, const char *version,
                            const char *detail, double progress) {
    g.update_state = state;
    if (version && version[0]) snprintf(g.update_latest, sizeof g.update_latest, "%s", version);

    switch (state) {
    case UPDATE_STATE_CHECKING:
        update_toast("Checking for updates",
                     version && version[0] ? version : WAVE_VERSION,
                     0.0f, 0, 12.0);
        break;
    case UPDATE_STATE_CURRENT:
        update_toast("Wave is up to date",
                     version && version[0] ? version : WAVE_VERSION,
                     0.0f, 0, 5.0);
        snprintf(g.info, sizeof g.info, "Wave %s is up to date",
                 version && version[0] ? version : WAVE_VERSION);
        break;
    case UPDATE_STATE_AVAILABLE: {
        char text[160];
        snprintf(text, sizeof text, "%s -> %s",
                 detail && detail[0] ? detail : WAVE_VERSION,
                 version && version[0] ? version : "new version");
        update_toast("Update available", text, 0.0f, 0, 8.0);
        break;
    }
    case UPDATE_STATE_DOWNLOADING:
        update_toast("Downloading update", version && version[0] ? version : detail,
                     (float)progress, 1, 20.0);
        break;
    case UPDATE_STATE_DOWNLOADED:
        update_toast("Installing update", detail && detail[0] ? detail : "restarting Wave",
                     1.0f, 1, 8.0);
        snprintf(g.info, sizeof g.info, "downloaded Wave %s", version ? version : "");
        break;
    case UPDATE_STATE_ERROR:
    default:
        update_toast("Update check failed", detail && detail[0] ? detail : "try again later",
                     0.0f, 0, 8.0);
        break;
    }
}

static void app_check_updates(int manual) {
    if (manual) update_callback(UPDATE_STATE_CHECKING, WAVE_VERSION, NULL, 0.0);
    mac_check_for_updates(WAVE_VERSION, manual, update_callback);
}

static void app_menu_check_updates(void) {
    app_check_updates(1);
}

static void parent_rel(const char *rel, char *out, size_t cap) {
    snprintf(out, cap, "%s", rel ? rel : "");
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    else out[0] = '\0';
}

static void apply_file_effect(WsFileEffect effect) {
    snprintf(g.info, sizeof g.info, "%s", effect.message);
    if (overlay_active(&g.overlay) == OVERLAY_PALETTE) app_palette_refilter();
}

static size_t sidebar_display_rows(void) {
    return ws_visible_count(g.ws) + (g.create_active ? 1u : 0u);
}

static void sidebar_create_cancel(void) {
    g.create_active = 0;
    g.create_text[0] = '\0';
    g.create_parent[0] = '\0';
}

static void sidebar_create_start(const char *parent, int row, int depth,
                                 int is_dir) {
    g.create_active = 1;
    g.create_is_dir = is_dir != 0;
    g.create_row = row < 0 ? 0 : row;
    g.create_depth = depth < 0 ? 0 : depth;
    snprintf(g.create_parent, sizeof g.create_parent, "%s", parent ? parent : "");
    g.create_text[0] = '\0';
    overlay_close(&g.overlay);
    command_close(&g.cmd);
    popover_close(&g.pop);
    modal_enter_normal(&g.modal);
}

static void sidebar_create_accept(void) {
    if (!g.create_active) return;
    if (!g.create_text[0]) {
        sidebar_create_cancel();
        return;
    }
    WsFileEffect effect = g.create_is_dir
        ? ws_create_dir_in(g.ws, g.create_parent, g.create_text)
        : ws_create_file_in(g.ws, g.create_parent, g.create_text);
    sidebar_create_cancel();
    apply_file_effect(effect);
    if (effect.ok && !g.create_is_dir) open_path(effect.path);
}

static int sidebar_create_insert_char(unsigned int cp) {
    if (!g.create_active || cp < 32 || cp > 126) return 0;
    size_t n = strlen(g.create_text);
    if (n + 1 >= sizeof g.create_text) return 1;
    g.create_text[n] = (char)cp;
    g.create_text[n + 1] = '\0';
    return 1;
}

static int sidebar_create_key(int key) {
    if (!g.create_active) return 0;
    if (key == GLFW_KEY_ESCAPE) {
        sidebar_create_cancel();
        return 1;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        sidebar_create_accept();
        return 1;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        size_t n = strlen(g.create_text);
        if (n > 0) g.create_text[n - 1] = '\0';
        return 1;
    }
    return 1;
}

static void sidebar_context_menu(GLFWwindow *w, LayoutClickTarget click) {
    const WsEntry *entry = (click.kind == LAYOUT_CLICK_SIDEBAR)
        ? ws_visible(g.ws, (size_t)click.row) : NULL;
    int action = mac_sidebar_context_menu(
        glfwGetCocoaWindow(w), entry ? entry->name : NULL,
        entry ? entry->is_dir : 0, entry != NULL, g.file_clipboard[0] != '\0');
    if (action == SIDEBAR_MENU_NONE) return;

    char target_dir[4096] = "";
    if (entry) {
        if (entry->is_dir) snprintf(target_dir, sizeof target_dir, "%s", entry->rel);
        else parent_rel(entry->rel, target_dir, sizeof target_dir);
    }

    if (action == SIDEBAR_MENU_OPEN_FILE) {
        if (entry && !entry->is_dir) {
            char *full = ws_fullpath(g.ws, entry->rel);
            open_path(full);
            free(full);
        } else {
            char path[4096];
            if (mac_open_panel(glfwGetCocoaWindow(w), 0, path, sizeof path))
                open_context_path(path);
        }
        return;
    }
    if (action == SIDEBAR_MENU_OPEN_FOLDER) {
        if (entry && entry->is_dir) {
            char *full = ws_fullpath(g.ws, entry->rel);
            open_context_path(full);
            free(full);
        } else {
            char path[4096];
            if (mac_open_panel(glfwGetCocoaWindow(w), 1, path, sizeof path))
                open_context_path(path);
        }
        return;
    }
    if (action == SIDEBAR_MENU_NEW_FILE) {
        int row = entry ? click.row + 1 : (int)ws_visible_count(g.ws);
        int depth = entry ? (entry->is_dir ? entry->depth + 1 : entry->depth) : 0;
        if (entry && entry->is_dir && entry->collapsed) ws_visible_toggle(g.ws, (size_t)click.row);
        sidebar_create_start(target_dir, row, depth, 0);
        return;
    }
    if (action == SIDEBAR_MENU_NEW_FOLDER) {
        int row = entry ? click.row + 1 : (int)ws_visible_count(g.ws);
        int depth = entry ? (entry->is_dir ? entry->depth + 1 : entry->depth) : 0;
        if (entry && entry->is_dir && entry->collapsed) ws_visible_toggle(g.ws, (size_t)click.row);
        sidebar_create_start(target_dir, row, depth, 1);
        return;
    }
    if ((action == SIDEBAR_MENU_COPY || action == SIDEBAR_MENU_CUT) && entry) {
        char *full = ws_fullpath(g.ws, entry->rel);
        snprintf(g.file_clipboard, sizeof g.file_clipboard, "%s", full);
        g.file_clipboard_cut = action == SIDEBAR_MENU_CUT;
        snprintf(g.info, sizeof g.info, "%s", g.file_clipboard_cut ? "cut" : "copied");
        free(full);
        return;
    }
    if (action == SIDEBAR_MENU_PASTE) {
        WsFileEffect effect = ws_paste_path_into(
            g.ws, g.file_clipboard, target_dir, g.file_clipboard_cut);
        if (effect.ok && g.file_clipboard_cut) {
            g.file_clipboard[0] = '\0';
            g.file_clipboard_cut = 0;
        }
        apply_file_effect(effect);
        return;
    }
    if (action == SIDEBAR_MENU_DELETE && entry) {
        char *full = ws_fullpath(g.ws, entry->rel);
        int confirm = mac_confirm_delete(glfwGetCocoaWindow(w), full);
        free(full);
        if (confirm) apply_file_effect(ws_delete_path(g.ws, entry->rel));
    }
}

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

static void buffer_search_open(void) {
    if (!editor_has_buffer(cur())) return;
    command_open(&g.buf_search);
    g.info[0] = '\0';
    modal_enter_normal(&g.modal);
}

static void buffer_search_close(void) {
    command_close(&g.buf_search);
}

static int buffer_search_jump(const char *query, int reverse) {
    if (!query || !query[0]) return 0;
    Editor *e = cur();
    if (!editor_search_text(e, query, reverse, g.info, sizeof g.info))
        return 0;
    center_cursor(e);
    return 1;
}

static void buffer_search_accept(void) {
    const char *query = command_text(&g.buf_search);
    if (query[0]) {
        snprintf(g.last_buf_search, sizeof g.last_buf_search, "%s", query);
        buffer_search_jump(g.last_buf_search, 0);
    }
    buffer_search_close();
}

static void buffer_search_repeat(int reverse) {
    if (!g.last_buf_search[0]) {
        snprintf(g.info, sizeof g.info, "no previous search");
        return;
    }
    buffer_search_jump(g.last_buf_search, reverse);
}

static void buffer_search_word(void) {
    char word[256];
    if (!editor_word_under_cursor(cur(), word, sizeof word)) {
        snprintf(g.info, sizeof g.info, "no word under cursor");
        return;
    }
    snprintf(g.last_buf_search, sizeof g.last_buf_search, "%s", word);
    buffer_search_jump(g.last_buf_search, 0);
}

/* ---- command line ( :w :q :wq ) ---- */
static void cmd_exec(void) {
    char path[1100];
    config_path(path, sizeof path);
    const char *text = command_text(&g.cmd);
    if (terminal_command(text)) {
        command_close(&g.cmd);
        return;
    }
    CommandRun run = command_run(text, &g.config, path);
    CommandEffect effect = run.effect;
    CommandAppPlan plan = command_app_plan(effect, editor_has_path(cur()));
    if (plan.save_config) config_save();
    if (plan.apply_blur)
        mac_set_blur(glfwGetCocoaWindow(g.win), g.config.blur);
    if (plan.apply_titlebar)
        mac_use_native_titlebar(glfwGetCocoaWindow(g.win), g.config.native_titlebar);

    snprintf(g.info, sizeof g.info, "%s", run.info);

    if (plan.write_file) {
        editor_save_file(cur(), &g.watch);
    }
    command_close(&g.cmd);
    switch (plan.close) {
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
    if (res.flags & EDIT_COMMAND_OPEN_BUFFER_SEARCH) buffer_search_open();
    if (res.flags & EDIT_COMMAND_SEARCH_NEXT) buffer_search_repeat(0);
    if (res.flags & EDIT_COMMAND_SEARCH_PREV) buffer_search_repeat(1);
    if (res.flags & EDIT_COMMAND_SEARCH_WORD) buffer_search_word();
    char edit_status[256];
    if (edit_command_status_text(res, edit_status, sizeof edit_status))
        snprintf(g.info, sizeof g.info, "%s", edit_status);
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

static int project_launcher_accept(void) {
    const char *path = recent_projects_selected(&g.recent);
    if (!path) return 0;
    open_context_path(path);
    recent_projects_set_query(&g.recent, "");
    return 1;
}

static int project_launcher_key(int key) {
    if (!project_launcher_active()) return 0;
    switch (key) {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        project_launcher_accept();
        return 1;
    case GLFW_KEY_UP:
        recent_projects_move(&g.recent, -1);
        return 1;
    case GLFW_KEY_DOWN:
        recent_projects_move(&g.recent, +1);
        return 1;
    case GLFW_KEY_BACKSPACE:
        recent_projects_backspace(&g.recent);
        return 1;
    case GLFW_KEY_ESCAPE:
        recent_projects_set_query(&g.recent, "");
        return 1;
    default:
        return 0;
    }
}

static int terminal_put_codepoint(Terminal *term, unsigned int cp, int alt) {
    if (!term) return 0;
    char buf[8];
    size_t n = 0;
    if (alt) buf[n++] = '\033';
    if (cp <= 0x7F) {
        if (cp < 32) return 0;
        buf[n++] = (char)cp;
    } else if (cp <= 0x7FF) {
        buf[n++] = (char)(0xC0 | (cp >> 6));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        buf[n++] = (char)(0xE0 | (cp >> 12));
        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        buf[n++] = (char)(0xF0 | (cp >> 18));
        buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        return 0;
    }
    terminal_write(term, buf, n);
    return 1;
}

static void terminal_paste_clipboard(Terminal *term) {
    if (!term) return;
    const char *clip = glfwGetClipboardString(g.win);
    if (clip && clip[0]) {
        terminal_write(term, clip, strlen(clip));
        return;
    }

    if (mac_clipboard_has_image()) {
        terminal_send_key_mods(term, GLFW_KEY_V, 0, 0, 1);
    }
}

static int glfw_key_is_down(GLFWwindow *w, int key) {
    int state = glfwGetKey(w, key);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

/* ---- GLFW callbacks ---- */
static void on_char(GLFWwindow *w, unsigned int cp) {
    g.last_activity = glfwGetTime();
    int control_down = (g.mods & GLFW_MOD_CONTROL) ||
        glfw_key_is_down(w, GLFW_KEY_LEFT_CONTROL) ||
        glfw_key_is_down(w, GLFW_KEY_RIGHT_CONTROL);
    int shift_down = (g.mods & GLFW_MOD_SHIFT) ||
        glfw_key_is_down(w, GLFW_KEY_LEFT_SHIFT) ||
        glfw_key_is_down(w, GLFW_KEY_RIGHT_SHIFT);
    if (cp == '\t' && control_down && glfw_key_is_down(w, GLFW_KEY_TAB)) {
        tab_goto(shift_down ? -1 : +1);
        return;
    }
    Terminal *term = cur_term();
    if (g.buf_search.active) {
        char text[2] = {0};
        if (cp >= 32 && cp < 127) {
            text[0] = (char)cp;
            command_insert_text(&g.buf_search, text);
        }
        return;
    }
    if (term && overlay_active(&g.overlay) == OVERLAY_NONE && !g.cmd.active &&
        !g.create_active && (g.mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) == 0) {
        terminal_put_codepoint(term, cp, (g.mods & GLFW_MOD_ALT) != 0);
        return;
    }
    if (sidebar_create_insert_char(cp)) return;
    if (project_launcher_active()) {
        if (cp == ':') {
            command_open(&g.cmd);
            return;
        }
        char text[2] = {0};
        if (cp >= 32 && cp < 127) {
            text[0] = (char)cp;
            recent_projects_insert_text(&g.recent, text);
        }
        return;
    }
    if (cp == ':' && overlay_active(&g.overlay) == OVERLAY_NONE &&
        !g.cmd.active && !g.create_active && !editor_has_buffer(cur()) &&
        (g.mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) == 0) {
        command_open(&g.cmd);
        return;
    }
    InputTextPlan plan = input_text_plan(
        (g.mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0,
        overlay_active(&g.overlay) != OVERLAY_NONE,
        g.cmd.active,
        editor_has_buffer(cur()),
        g.modal.mode == MODE_INSERT,
        cp);

    if (plan.target == INPUT_TEXT_IGNORE || plan.target == INPUT_TEXT_NONE) return;
    if (plan.target == INPUT_TEXT_OVERLAY) {
        if (plan.has_text)
            overlay_insert_text(&g.overlay, g.ws, ws_root(g.ws), plan.text);
        return;
    }
    if (plan.target == INPUT_TEXT_COMMAND) {
        if (plan.has_text)
            command_insert_text(&g.cmd, plan.text);
        return;
    }
    if (plan.target == INPUT_TEXT_EDITOR_INSERT) {
        editor_apply_text_input(cur(), cp);
        return;
    }
    vim_normal_char(cp); /* NORMAL or VISUAL */
}

static void on_key(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)w;
    g.mods = mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    g.last_activity = glfwGetTime();
    if (sidebar_create_key(key)) return;
    Editor *e = cur();

    InputKey input_key = wave_input_key_from_glfw(key);
#ifdef __APPLE__
    if (input_key == INPUT_KEY_NONE && sc == 48)
        input_key = INPUT_KEY_TAB;
#else
    (void)sc;
#endif
    WaveShortcut shortcut = input_shortcut(
        input_key,
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0,
        (mods & GLFW_MOD_CONTROL) != 0,
        (mods & GLFW_MOD_ALT) != 0,
        (mods & GLFW_MOD_SHIFT) != 0);

    if (shortcut == SHORTCUT_TAB_NEXT) {
        tab_goto(+1);
        return;
    }
    if (shortcut == SHORTCUT_TAB_PREV) {
        tab_goto(-1);
        return;
    }

    if (g.buf_search.active) {
        if (shortcut == SHORTCUT_PASTE) {
            const char *clip = glfwGetClipboardString(g.win);
            if (clip) command_insert_text(&g.buf_search, clip);
            return;
        }
        CommandKeyResult search_key = command_apply_key(
            &g.buf_search, wave_command_key_from_glfw(key));
        if (search_key == COMMAND_KEY_RESULT_ACCEPT) buffer_search_accept();
        if (search_key != COMMAND_KEY_RESULT_NONE) return;
        return;
    }

    Terminal *term = cur_term();
    if (term && overlay_active(&g.overlay) == OVERLAY_NONE && !g.cmd.active &&
        !g.create_active) {
        if ((mods & GLFW_MOD_SUPER) && shortcut == SHORTCUT_NEW_TERMINAL) {
            open_shell_tab();
            return;
        }
        if ((mods & GLFW_MOD_SUPER) && shortcut == SHORTCUT_PASTE) {
            terminal_paste_clipboard(term);
            return;
        }
        if (mods & GLFW_MOD_SUPER) {
            /* Command stays with Wave; Control/Alt belong to the PTY. */
        } else {
            switch (key) {
            case GLFW_KEY_ESCAPE:
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER:
            case GLFW_KEY_TAB:
            case GLFW_KEY_BACKSPACE:
            case GLFW_KEY_INSERT:
            case GLFW_KEY_DELETE:
            case GLFW_KEY_LEFT:
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_UP:
            case GLFW_KEY_DOWN:
            case GLFW_KEY_PAGE_UP:
            case GLFW_KEY_PAGE_DOWN:
            case GLFW_KEY_HOME:
            case GLFW_KEY_END:
            case GLFW_KEY_F1:
            case GLFW_KEY_F2:
            case GLFW_KEY_F3:
            case GLFW_KEY_F4:
            case GLFW_KEY_F5:
            case GLFW_KEY_F6:
            case GLFW_KEY_F7:
            case GLFW_KEY_F8:
            case GLFW_KEY_F9:
            case GLFW_KEY_F10:
            case GLFW_KEY_F11:
            case GLFW_KEY_F12:
                terminal_send_key_mods(term, key, (mods & GLFW_MOD_SHIFT) != 0,
                                       (mods & GLFW_MOD_ALT) != 0,
                                       (mods & GLFW_MOD_CONTROL) != 0);
                return;
            default:
                if (mods & GLFW_MOD_CONTROL) {
                    terminal_send_key_mods(term, key, (mods & GLFW_MOD_SHIFT) != 0,
                                           (mods & GLFW_MOD_ALT) != 0, 1);
                    return;
                }
                return;
            }
        }
    }

    if (shortcut == SHORTCUT_NEW_TERMINAL) {
        open_shell_tab();
        return;
    }

    if (project_launcher_active()) {
        if (shortcut == SHORTCUT_PASTE) {
            const char *clip = glfwGetClipboardString(g.win);
            if (clip) recent_projects_insert_text(&g.recent, clip);
            return;
        }
        if (project_launcher_key(key)) return;
    }

    InputKeyPlan plan = input_key_plan(
        shortcut, overlay_active(&g.overlay) != OVERLAY_NONE,
        g.cmd.active, editor_has_buffer(e), editor_has_path(e));

    if (plan.target == INPUT_KEY_TARGET_SHORTCUT &&
        plan.shortcut_action == INPUT_SHORTCUT_ACTION_COPY) {
        switch (plan.clipboard_target) {
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
    if (plan.target == INPUT_KEY_TARGET_SHORTCUT &&
        plan.shortcut_action == INPUT_SHORTCUT_ACTION_PASTE) {
        const char *clip = glfwGetClipboardString(g.win);
        if (!clip) return;
        switch (plan.clipboard_target) {
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
    if (plan.target == INPUT_KEY_TARGET_SHORTCUT) {
        InputShortcutEffect effect = input_shortcut_effect(plan.shortcut_action);
        if (effect.open_palette)
            palette_open();
        if (effect.open_search)
            search_open();
        if (effect.toggle_sidebar) {
            wave_config_toggle_sidebar(&g.config);
            config_save();
        }
        if (effect.new_terminal)
            open_shell_tab();
        if (effect.save_file)
            editor_save_file(e, &g.watch);
        if (effect.tab_delta)
            tab_goto(effect.tab_delta);
        if (effect.close_tab)
            close_tab(tabs_active_index(&g.tabs));
        if (effect.history)
            editor_apply_history_action(e, effect.history_redo, g.info, sizeof g.info);
        if (effect.zoom)
            ui_zoom(effect.zoom_dir);
        if (effect.toggle_wrap) {
            wave_config_toggle_wrap(&g.config);
            config_save();
            wave_config_wrap_text(&g.config, g.info, sizeof g.info);
        }
        return;
    }

    if (plan.target == INPUT_KEY_TARGET_OVERLAY) {
        OverlayKind active = overlay_active(&g.overlay);
        OverlayKeyResult overlay_key = overlay_apply_key(
            &g.overlay, g.ws, ws_root(g.ws), wave_overlay_key_from_glfw(key));
        switch (overlay_accept_action(active, overlay_key)) {
        case OVERLAY_ACCEPT_PALETTE:
            palette_accept();
            break;
        case OVERLAY_ACCEPT_SEARCH:
            search_accept();
            break;
        case OVERLAY_ACCEPT_NONE:
        default:
            break;
        }
        return;
    }

    /* command line */
    if (plan.target == INPUT_KEY_TARGET_COMMAND) {
        CommandKeyResult cmd_key = command_apply_key(&g.cmd, wave_command_key_from_glfw(key));
        if (cmd_key == COMMAND_KEY_RESULT_ACCEPT) cmd_exec();
        return;
    }

    if (plan.target != INPUT_KEY_TARGET_EDITOR) return;

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
    Terminal *term = cur_term();
    if (term) {
        terminal_scroll(term, dy > 0.0 ? 3 : -3);
        return;
    }
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
        float max_scroll = layout_sidebar_max_scroll(&g.layout, sidebar_display_rows());
        g.side_scroll = layout_scroll_offset_clamped(g.side_scroll, target.pixels,
                                                     max_scroll);
    } else {
        Editor *e = cur();
        if (!editor_has_buffer(e) || g.layout.adv <= 0.0f ||
            g.layout.line_h <= 0.0f)
            return;
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(w, &fb_w, &fb_h);
        int cols = text_view_wrap_cols(g.config.wrap, (float)fb_w,
                                       g.layout.text_x, g.layout.adv);
        wrap_build(e, cols);
        float view_h = (float)fb_h - g.layout.top_pad - layout_status_bar_h(&g.layout);
        float max_scroll = layout_max_scroll((float)e->vstart[pt_line_count(buffer_pt(e->buf))] *
                                             g.layout.line_h, view_h);
        editor_set_scroll_y(e, layout_scroll_offset_clamped(editor_scroll_y(e),
                                                            target.pixels,
                                                            max_scroll));
    }
}

static LayoutScrollbar current_sidebar_scrollbar(float *max_scroll) {
    if (max_scroll) *max_scroll = 0.0f;
    if (!g.ws || !g.config.show_sidebar || g.layout.line_h <= 0.0f) return (LayoutScrollbar){0};
    float bottom = (float)g.layout.fb_h - layout_status_bar_h(&g.layout);
    float content_h = (float)sidebar_display_rows() * g.layout.line_h + g.layout.side_pad;
    float viewport_h = bottom - g.layout.header_h;
    if (max_scroll) *max_scroll = layout_max_scroll(content_h, viewport_h);
    return layout_scrollbar(g.layout.side_px - 5.6f, g.layout.header_h + 4.0f,
                            3.6f, viewport_h - 8.0f, content_h, viewport_h,
                            g.side_scroll);
}

static int current_editor_scrollbar(GLFWwindow *w, LayoutScrollbar *bar,
                                    float *max_scroll) {
    if (bar) *bar = (LayoutScrollbar){0};
    if (max_scroll) *max_scroll = 0.0f;
    Editor *e = cur();
    if (!editor_has_buffer(e) || g.layout.adv <= 0.0f || g.layout.line_h <= 0.0f)
        return 0;

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(w, &fb_w, &fb_h);
    int cols = text_view_wrap_cols(g.config.wrap, (float)fb_w,
                                   g.layout.text_x, g.layout.adv);
    wrap_build(e, cols);
    float view_h = (float)fb_h - g.layout.top_pad - layout_status_bar_h(&g.layout);
    float content_h = (float)e->vstart[pt_line_count(buffer_pt(e->buf))] *
                      g.layout.line_h;
    float max = layout_max_scroll(content_h, view_h);
    if (max_scroll) *max_scroll = max;
    if (bar)
        *bar = layout_scrollbar((float)fb_w - 6.6f, g.layout.top_pad + 4.0f,
                                3.6f, view_h - 8.0f, content_h, view_h,
                                editor_scroll_y(e));
    return 1;
}

static void on_cursor_pos(GLFWwindow *w, double mx, double my) {
    (void)w;
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;

    if (g.scrollbar_drag == SCROLLBAR_DRAG_SIDEBAR) {
        LayoutScrollbar bar = current_sidebar_scrollbar(NULL);
        g.side_scroll = layout_scrollbar_drag_scroll(
            bar, y, g.scrollbar_drag_grab_y, g.scrollbar_drag_max);
        return;
    }
    if (g.scrollbar_drag == SCROLLBAR_DRAG_EDITOR) {
        LayoutScrollbar bar;
        if (current_editor_scrollbar(w, &bar, NULL)) {
            Editor *e = cur();
            editor_set_scroll_y(e, layout_scrollbar_drag_scroll(
                                     bar, y, g.scrollbar_drag_grab_y,
                                     g.scrollbar_drag_max));
        }
        return;
    }

    if (!g.mouse_drag.down) return;

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
    LayoutClickTarget click = {0};
    InputMousePlan plan = input_mouse_plan(
        click, button == GLFW_MOUSE_BUTTON_LEFT, button == GLFW_MOUSE_BUTTON_RIGHT,
        action == GLFW_PRESS, action == GLFW_RELEASE, g.pop.active, 0);
    if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT &&
        g.scrollbar_drag != SCROLLBAR_DRAG_NONE) {
        g.scrollbar_drag = SCROLLBAR_DRAG_NONE;
        return;
    }
    if (plan.action == INPUT_MOUSE_ACTION_RELEASE_DRAG) {
        layout_drag_release(&g.mouse_drag);
        return;
    }
    if (action != GLFW_PRESS) return;

    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    float x = (float)mx * g.fb_scale, y = (float)my * g.fb_scale;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        float max_scroll = 0.0f;
        LayoutScrollbar bar = current_sidebar_scrollbar(&max_scroll);
        if (layout_scrollbar_thumb_hit(bar, x, y, 6.0f)) {
            g.scrollbar_drag = SCROLLBAR_DRAG_SIDEBAR;
            g.scrollbar_drag_grab_y = y - bar.thumb_y;
            g.scrollbar_drag_max = max_scroll;
            g.side_scrollbar_hover = 1.0f;
            popover_close(&g.pop);
            return;
        }

        if (current_editor_scrollbar(w, &bar, &max_scroll) &&
            layout_scrollbar_thumb_hit(bar, x, y, 6.0f)) {
            g.scrollbar_drag = SCROLLBAR_DRAG_EDITOR;
            g.scrollbar_drag_grab_y = y - bar.thumb_y;
            g.scrollbar_drag_max = max_scroll;
            g.editor_scrollbar_hover = 1.0f;
            popover_close(&g.pop);
            return;
        }
    }

    click = layout_click_target(
        &g.layout, x, y, g.side_scroll, button == GLFW_MOUSE_BUTTON_RIGHT,
        g.ws != NULL, g.config.show_sidebar);
    if (button == GLFW_MOUSE_BUTTON_RIGHT &&
        layout_in_sidebar(&g.layout, x, g.ws != NULL, g.config.show_sidebar)) {
        sidebar_context_menu(w, click);
        popover_close(&g.pop);
        return;
    }
    plan = input_mouse_plan(
        click, button == GLFW_MOUSE_BUTTON_LEFT, button == GLFW_MOUSE_BUTTON_RIGHT,
        1, 0, g.pop.active,
        editor_has_buffer(cur()) && editor_has_visual_rows(cur()));

    if (plan.dismiss_popover) popover_close(&g.pop); /* a click anywhere dismisses the popover */
    if (plan.record_activity) g.last_activity = glfwGetTime();

    /* Title-bar band: our GL view covers it, so reproduce the native gestures —
     * drag to move, double-click to zoom/minimise (per System Settings), and
     * right-/control-click for the document path menu. */
    switch (plan.action) {
    case INPUT_MOUSE_ACTION_TITLEBAR_MENU:
        mac_window_titlebar_menu(glfwGetCocoaWindow(w));
        return;
    case INPUT_MOUSE_ACTION_TITLEBAR_LEFT: {
        void *nw = glfwGetCocoaWindow(w);
        double now = glfwGetTime();
        if (layout_point_double_click(&g.title_click, now, x, y, 0.4, 6.0f))
            mac_window_titlebar_doubleclick(nw);
        else mac_window_drag(nw);
        return;
    }
    case INPUT_MOUSE_ACTION_SIDEBAR: {
        WsClickAction action = ws_click_visible_timed(
            g.ws, &g.sidebar_click, click.row, glfwGetTime(), 0.4);
        if (action.kind == WS_CLICK_OPEN_FILE && action.entry) {
            char *full = ws_fullpath(g.ws, action.entry->rel);
            open_path_mode(full, action.preview);
            free(full);
        }
        return;
    }
    case INPUT_MOUSE_ACTION_TAB: {
        /* tab strip: click to switch; click the trailing 'x' to close */
        TabActionEffect effect = tabs_click_with_effect(
            &g.tabs, click.tab_index, click.tab_close);
        if (effect.close_window) glfwSetWindowShouldClose(g.win, GLFW_TRUE);
        if (effect.reset_mode) modal_enter_normal(&g.modal);
        return;
    }
    case INPUT_MOUSE_ACTION_TEXT: {
        /* click in the text area: map (visual row, x) back to a byte offset */
        Editor *e = cur();
        size_t off = 0;
        if (!editor_apply_click_position(e, x, y, g.layout.text_x,
                                         g.layout.top_pad, g.layout.line_h,
                                         g.layout.adv, &off))
            return;
        layout_drag_begin(&g.mouse_drag, off, x, y);
        if (g.modal.mode == MODE_VISUAL) modal_enter_normal(&g.modal);
        break;
    }
    case INPUT_MOUSE_ACTION_NONE:
    case INPUT_MOUSE_ACTION_RELEASE_DRAG:
    default:
        break;
    }
}

/* ---- drawing ---- */
#define MAXSPANS 4096

static float hover_step(float current, int hot, double dt) {
    float target = hot ? 1.0f : 0.0f;
    float step = (float)dt * 14.0f;
    if (step > 1.0f) step = 1.0f;
    if (step < 0.0f) step = 0.0f;
    return current + (target - current) * step;
}

static int hover_animating(float value) {
    return value > 0.01f && value < 0.99f;
}

static void draw_status_line(Renderer *r, Font *font, Editor *e, float fb_w,
                             float fb_h, float pad, float ascent, float line_h,
                             size_t diagnostics) {
    float bar_h = line_h + 6.0f;
    renderer_rect(r, 0, fb_h - bar_h, fb_w, bar_h, 0.07f, 0.08f, 0.10f,
                  g.config.opacity);
    char status[400];
    ViewStatusLine status_line;
    if (g.buf_search.active) {
        snprintf(status, sizeof status, "/%s", command_text(&g.buf_search));
        status_line = (ViewStatusLine){.kind = VIEW_STATUS_COMMAND,
                                       .lang = "text",
                                       .r = 0.70f,
                                       .g = 0.74f,
                                       .b = 0.80f};
    } else {
        status_line = view_editor_status_line(
            status, sizeof status, e, g.cmd.active ? command_text(&g.cmd) : NULL,
            g.info, mode_name(g.modal.mode), diagnostics,
            tabs_active_index(&g.tabs), tabs_count(&g.tabs));
    }
    draw_text_run(font, r, status, (int)strlen(status), pad,
                  fb_h - bar_h + ascent + 2.0f,
                  (Color){status_line.r, status_line.g, status_line.b});
}

static void draw_frame(int fb_w, int fb_h) {
    Font *font = g.font;
    Renderer *r = g.rend;
    Editor *e = cur();
    Terminal *term = cur_term();
    int editor_fb_w = fb_w;

    /* keep the native title path menu pointed at the active file (cheap pointer
     * guard so we only touch Cocoa when the front tab actually changes) */
    static const char *shown_path = (const char *)-1;
    const char *active_path = e ? editor_path(e) : NULL;
    if (g.config.native_titlebar && active_path != shown_path) {
        mac_window_set_file(glfwGetCocoaWindow(g.win), active_path);
        shown_path = active_path;
    }

    float line_h = font_line_height(font);
    float ascent = font_ascent(font);
    float adv = font_advance(font);
    int launcher_base = g.ws == NULL && e && !editor_has_buffer(e);

    LayoutFrameMetrics metrics = layout_frame_metrics(
        &g.layout, line_h, adv, ascent, fb_h, g.ws != NULL, g.config.show_sidebar,
        g.config.side_cells, g.config.native_titlebar, g.fb_scale,
        launcher_base ? 0 : tabs_count(&g.tabs));
    float side_px = metrics.side_px;
    float gutter = metrics.gutter;
    float pad = metrics.pad;
    float text_x = metrics.text_x;
    float tab_h = metrics.tab_h;
    float header_h = metrics.header_h;
    float tab_strip = metrics.tab_strip;
    float top_pad = metrics.top_pad;
    ViewFramePlan frame = view_frame_plan(
        side_px, tab_strip, header_h, editor_has_buffer(e),
        overlay_active(&g.overlay));
    double mx = -1000.0, my = -1000.0;
    glfwGetCursorPos(g.win, &mx, &my);
    float cursor_x = (float)mx * g.fb_scale;
    float cursor_y = (float)my * g.fb_scale;
    double now = glfwGetTime();
    double dt = g.last_frame_time > 0.0 ? now - g.last_frame_time : 1.0 / 60.0;
    g.last_frame_time = now;
    if (dt > 0.08) dt = 0.08;

    int side_hot = 0;
    int side_hover_row = -1;
    if (frame.sidebar) {
        LayoutScrollbar side_bar = current_sidebar_scrollbar(NULL);
        side_hot = layout_scrollbar_hit(side_bar, cursor_x, cursor_y, 6.0f);
        if (layout_in_sidebar(&g.layout, cursor_x, g.ws != NULL,
                              g.config.show_sidebar) &&
            !layout_in_titlebar(&g.layout, cursor_y) && !side_hot) {
            side_hover_row = layout_sidebar_row(&g.layout, cursor_y, g.side_scroll);
            if (side_hover_row < 0 || (size_t)side_hover_row >= sidebar_display_rows())
                side_hover_row = -1;
        }
    }
    if (g.scrollbar_drag == SCROLLBAR_DRAG_SIDEBAR) side_hot = 1;
    g.side_scrollbar_hover = hover_step(g.side_scrollbar_hover, side_hot, dt);

    renderer_begin(r, fb_w, fb_h, 0.11f, 0.12f, 0.14f, g.config.opacity);

    if (frame.sidebar)
        draw_sidebar_panel(g.ws, active_path, g.config.side_cells, g.side_scroll,
                           fb_h, font, r, adv, line_h, ascent, side_px,
                           header_h, g.layout.side_pad, g.config.opacity,
                           g.side_scrollbar_hover, g.config.radius,
                           g.create_active, g.create_row, g.create_depth,
                           g.create_is_dir, g.create_text, side_hover_row);
    if (frame.tabs)
        g.layout.tab_w = draw_tabs_panel(&g.tabs, editor_fb_w, font, r, side_px, adv,
                                         ascent, tab_h, header_h,
                                         g.config.opacity, g.config.radius);
    if (frame.header)
        draw_header_panel(g.ws ? ws_root(g.ws) : NULL, active_path,
                          editor_fb_w, font, r, adv, ascent, header_h, g.fb_scale,
                          g.config.opacity);

    if (term) {
        terminal_resize_active(fb_w, fb_h, side_px, top_pad, line_h, adv);
        float bar_h = line_h + 6.0f;
        draw_terminal_panel(term, 1, side_px, top_pad,
                            (float)fb_w - side_px,
                            (float)fb_h - top_pad - bar_h,
                            font, r, adv, line_h, ascent, g.config.opacity);
        renderer_rect(r, 0, (float)fb_h - bar_h, (float)fb_w, bar_h,
                      0.07f, 0.08f, 0.10f, g.config.opacity);
        char status[256];
        snprintf(status, sizeof status, "%s  %s  [%d/%d]",
                 term->title[0] ? term->title : "terminal",
                 terminal_status(term), tabs_active_index(&g.tabs) + 1,
                 tabs_count(&g.tabs));
        draw_text_run(font, r, status, (int)strlen(status), pad,
                      (float)fb_h - bar_h + ascent + 2.0f,
                      (Color){0.72f, 0.78f, 0.88f});
        if (frame.overlay == VIEW_OVERLAY_DRAW_PALETTE)
            draw_palette_panel(&g.overlay, g.ws, fb_w, font, r, adv, line_h,
                               ascent, top_pad, g.config.radius);
        else if (frame.overlay == VIEW_OVERLAY_DRAW_SEARCH)
            draw_search_panel(&g.overlay, fb_w, font, r, adv, line_h, ascent,
                              g.config.radius);
        if (g.update_title[0] && g.update_toast_until > glfwGetTime())
            draw_update_toast(g.update_title, g.update_detail, g.update_progress,
                              g.update_show_progress, fb_w, fb_h, font, r,
                              adv, line_h, ascent, g.config.radius);
        renderer_flush(r);
        return;
    }

    if (frame.empty) {
        if (!g.ws) {
            draw_recent_projects_panel(&g.recent, editor_fb_w, fb_h, font, r, adv,
                                       line_h, ascent, top_pad,
                                       g.config.radius);
            draw_status_line(r, font, e, (float)fb_w, (float)fb_h, pad,
                             ascent, line_h, 0);
            if (g.update_title[0] && g.update_toast_until > glfwGetTime())
                draw_update_toast(g.update_title, g.update_detail, g.update_progress,
                                  g.update_show_progress, fb_w, fb_h, font, r,
                                  adv, line_h, ascent, g.config.radius);
            renderer_flush(r);
            return;
        }

        /* empty state: a folder is open but no file has been chosen */
        ViewEmptyState empty = view_empty_state(g.ws != NULL);
        ViewEmptyLayout empty_layout = view_empty_layout(
            (float)editor_fb_w, (float)fb_h, side_px, top_pad, adv, line_h, &empty);
        draw_text_run(font, r, empty.title, (int)strlen(empty.title),
                      empty_layout.title_x, empty_layout.title_y,
                      (Color){0.62f, 0.66f, 0.72f});
        draw_text_run(font, r, empty.hint, (int)strlen(empty.hint),
                      empty_layout.hint_x, empty_layout.hint_y,
                      (Color){0.42f, 0.46f, 0.52f});
        /* The Cmd-P palette and Cmd-Shift-F search are reachable from the empty
         * state too — the primary ways to open a file here — so draw whichever
         * is active on top before bailing. */
        if (frame.overlay == VIEW_OVERLAY_DRAW_PALETTE)
            draw_palette_panel(&g.overlay, g.ws, editor_fb_w, font, r, adv, line_h,
                               ascent, top_pad, g.config.radius);
        else if (frame.overlay == VIEW_OVERLAY_DRAW_SEARCH)
            draw_search_panel(&g.overlay, editor_fb_w, font, r, adv, line_h, ascent,
                              g.config.radius);
        draw_status_line(r, font, e, (float)fb_w, (float)fb_h, pad, ascent,
                         line_h, 0);
        if (g.update_title[0] && g.update_toast_until > glfwGetTime())
            draw_update_toast(g.update_title, g.update_detail, g.update_progress,
                              g.update_show_progress, fb_w, fb_h, font, r,
                              adv, line_h, ascent, g.config.radius);
        renderer_flush(r);
        return;
    }

    TextFrameView text;
    if (!text_view_prepare(e, g.modal.mode == MODE_VISUAL, g.config.wrap,
                           (float)editor_fb_w, text_x, adv, (float)fb_h, top_pad,
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

    float bar_h = line_h + 6.0f;
    float editor_view_h = (float)fb_h - top_pad - bar_h;
    LayoutScrollbar editor_scrollbar = layout_scrollbar(
        (float)editor_fb_w - 6.6f, top_pad + 4.0f, 3.6f, editor_view_h - 8.0f,
        (float)text.total_vrows * line_h, editor_view_h, editor_scroll_y(e));
    int editor_hot = layout_scrollbar_hit(editor_scrollbar, cursor_x, cursor_y, 6.0f);
    if (g.scrollbar_drag == SCROLLBAR_DRAG_EDITOR) editor_hot = 1;
    g.editor_scrollbar_hover = hover_step(g.editor_scrollbar_hover, editor_hot, dt);
    editor_scrollbar = layout_scrollbar_expand(editor_scrollbar,
                                               g.editor_scrollbar_hover, 6.0f);
    draw_scrollbar(r, editor_scrollbar, g.config.opacity, g.config.radius);

    /* status / command line */
    draw_status_line(r, font, e, (float)fb_w, (float)fb_h, pad, ascent,
                     line_h, ndiag);

    /* the `gh` popover floats over the text, anchored to the cursor (kept on
     * screen even when the cursor sits near an edge). Hidden under the palette. */
    if (frame.popover) {
        ViewPoint anchor = view_popover_anchor(text_x, top_pad, (float)fb_h, bar_h,
                                               adv, line_h, text.cursor.vrow,
                                               text.cursor.xcol, editor_scroll_y(e));
        draw_popover_panel(&g.pop, fb_w, fb_h, font, r, adv, line_h, ascent,
                           top_pad, bar_h, anchor.x, anchor.y,
                           g.layout.side_px, g.fb_scale, g.config.radius);
    }

    if (frame.overlay == VIEW_OVERLAY_DRAW_PALETTE)
        draw_palette_panel(&g.overlay, g.ws, editor_fb_w, font, r, adv, line_h,
                           ascent, top_pad, g.config.radius);
    else if (frame.overlay == VIEW_OVERLAY_DRAW_SEARCH)
        draw_search_panel(&g.overlay, editor_fb_w, font, r, adv, line_h, ascent,
                          g.config.radius);

    if (g.update_title[0] && g.update_toast_until > glfwGetTime())
        draw_update_toast(g.update_title, g.update_detail, g.update_progress,
                          g.update_show_progress, fb_w, fb_h, font, r,
                          adv, line_h, ascent, g.config.radius);

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
    recent_projects_init(&g.recent);
    wave_config_defaults(&g.config);
    watch_service_init(&g.watch);
    g.persist = runtime.persist;
    if (g.persist) {
        wave_config_load(&g.config); /* saved prefs override defaults */
        recent_projects_load(&g.recent);
    }
    if (runtime.has_scale_override) g.config.ui_scale = runtime.scale_override;

    /* a transparent framebuffer lets opacity<1 (and the macOS blur) show through */
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow *win = glfwCreateWindow(1100, 720, "Wave", NULL, NULL);
    if (!win) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    g.win = win;
    mac_install_app_menu(app_menu_open_file, app_menu_open_folder,
                         app_menu_check_updates, app_menu_next_tab,
                         app_menu_prev_tab);
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
            if (recent_projects_add(&g.recent, ws_root(g.ws)) && g.persist)
                recent_projects_save(&g.recent);
            if (opened.kind == WS_OPEN_FILE) open_path(opened.file);
        }
    }
    if (tabs_count(&g.tabs) == 0) {
        TabStartupEffect startup = tabs_ensure_startup(&g.tabs, 1);
        if (startup.enter_insert) modal_enter_insert(&g.modal);
    }
    if (!runtime.snapshot) app_check_updates(0);

    g.rend = renderer_new(g.font);
    if (!g.rend) { fprintf(stderr, "renderer init failed\n"); return 1; }

    if (runtime.snapshot) {
        const char *snapshot_terminal = getenv("WAVE_TERMINAL");
        if (snapshot_terminal && snapshot_terminal[0]) {
            terminal_command(snapshot_terminal);
            int terminal_settle = wave_runtime_int_value(getenv("WAVE_SETTLE_MS"));
            glfwGetFramebufferSize(win, &fb_w, &fb_h);
            draw_frame(fb_w, fb_h);
            snapshot_settle_terminal(terminal_settle);
            snapshot_terminal_input(getenv("WAVE_TERMINAL_INPUT"));
            snapshot_settle_terminal(terminal_settle > 0 ? terminal_settle : 500);
        }

        WaveRuntimeSnapshotScript script = wave_runtime_snapshot_script(
            getenv("WAVE_OPEN"), getenv("WAVE_TYPE"), getenv("WAVE_KEYS"),
            getenv("WAVE_PALETTE"), getenv("WAVE_QUERY"), getenv("WAVE_SEARCH"),
            getenv("WAVE_SEARCHSEL"), getenv("WAVE_POPTEST"), getenv("WAVE_POPSCROLL"));

        WaveRuntimeSnapshotPlan script_plan = wave_runtime_snapshot_plan(script, 0, 0, g.ws != NULL);
        for (int i = 0; i < script_plan.open_count; i++)
            open_path(script.opens.paths[i]);
        if (script_plan.open_palette) palette_open();

        script_plan = wave_runtime_snapshot_plan(
            script, editor_has_buffer(cur()),
            overlay_active(&g.overlay) == OVERLAY_PALETTE, g.ws != NULL);
        if (script_plan.type_text) {
            modal_enter_insert(&g.modal);
            editor_insert_encoded_text(cur(), script.typed);
        }
        editor_refresh_highlighter(cur());
        if (script_plan.normal_keys) {
            modal_enter_normal(&g.modal);
            for (const char *p = script.keys; *p; p++) {
                editor_refresh_highlighter(cur());
                vim_normal_char((unsigned int)(unsigned char)*p);
            }
        }
        if (script_plan.set_palette_query) {
            overlay_set_palette_query(&g.overlay, g.ws, script.palette_query);
        }
        if (script_plan.run_search) {
            search_open();
            overlay_set_search_query(&g.overlay, ws_root(g.ws), script.search_query);
            overlay_settle_search(&g.overlay);
            overlay_set_search_selection(&g.overlay, script.search_selection);
        }
        if (script_plan.show_popover) {
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
        LspManagerUiPlan lsp_plan = lsp_manager_ui_plan(
            lsp_manager_update_ui(&g.lsp, e));
        if (lsp_plan.open_definition) {
            if (open_path(lsp_plan.definition.path) == 0) {
                Editor *te = cur();
                if (editor_move_to_lsp_position(te, lsp_plan.definition.line,
                                                lsp_plan.definition.col,
                                                g.info, sizeof g.info))
                    center_cursor(te);
            }
        }
        if (lsp_plan.show_hover) {
            popover_show_hover(&g.pop, lsp_plan.hover);
        }

        /* drain any pending ripgrep output for the live search overlay */
        overlay_poll_search(&g.overlay);
        terminal_poll_tabs();

        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        draw_frame(fb_w, fb_h);
        glfwSwapBuffers(win);
        /* poll faster while a server or a search is active so results stream in */
        glfwWaitEventsTimeout(wave_runtime_wait_timeout(
            lsp_manager_active(&g.lsp) ||
                terminal_tab_running() ||
                hover_animating(g.side_scrollbar_hover) ||
                hover_animating(g.editor_scrollbar_hover),
            overlay_search_running(&g.overlay)));
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
