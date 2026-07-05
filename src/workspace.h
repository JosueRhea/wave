/* workspace.h — an opened folder: a recursively scanned, display-ordered list
 * of its files and subdirectories.
 *
 * Entries are produced in pre-order (a directory immediately followed by its
 * children), each tagged with a depth, so a sidebar can render them as an
 * indented tree without any further traversal. Common noise directories
 * (.git, node_modules, build output) are skipped.
 */
#ifndef WAVE_WORKSPACE_H
#define WAVE_WORKSPACE_H

#include <stddef.h>

typedef struct {
    char *rel;      /* path relative to the workspace root (owned) */
    char *name;     /* final path component, for display (points into rel) */
    int depth;      /* 0 for top-level entries */
    int is_dir;     /* 1 = directory, 0 = file */
    int collapsed;  /* dirs only: 1 = children hidden in the visible list */
} WsEntry;

typedef struct Workspace Workspace;

typedef enum {
    WS_CLICK_NONE,
    WS_CLICK_TOGGLE_DIR,
    WS_CLICK_OPEN_FILE,
} WsClickKind;

typedef struct {
    WsClickKind kind;
    const WsEntry *entry;
    int row;
    int preview;
} WsClickAction;

typedef struct {
    int seen;
    double time;
    int row;
} WsSidebarClickState;

typedef enum {
    WS_OPEN_NONE,
    WS_OPEN_WORKSPACE,
    WS_OPEN_FILE,
} WsOpenKind;

typedef struct {
    WsOpenKind kind;
    Workspace *workspace;
    char file[4096];
} WsOpenContext;

typedef struct {
    int ok;
    int refilter_palette;
    char message[256];
} WsReloadEffect;

typedef struct {
    int ok;
    char path[4096];
    char message[256];
} WsFileEffect;

/* Open `root` and scan it. Returns NULL if it isn't a readable directory. */
Workspace *ws_open(const char *root);
void ws_free(Workspace *w);
int ws_reload(Workspace *w);
WsReloadEffect ws_apply_reload(Workspace *w);
WsReloadEffect ws_apply_watch_event(Workspace *w, int pending);
WsOpenContext ws_open_context(const char *arg);

const char *ws_root(const Workspace *w);
size_t ws_count(const Workspace *w);
const WsEntry *ws_entry(const Workspace *w, size_t i);

/* The visible view: the full tree minus the subtrees of collapsed directories,
 * in the same pre-order. `vi` indexes this filtered, display-ordered list. */
size_t ws_visible_count(const Workspace *w);
const WsEntry *ws_visible(const Workspace *w, size_t vi);

/* Toggle the collapsed state of the directory at visible row `vi` (no-op for a
 * file or out-of-range row), then rebuild the visible list. */
void ws_visible_toggle(Workspace *w, size_t vi);

/* Apply the workspace-owned action for a visible sidebar row. Directories are
 * toggled immediately; files report whether the caller should open as a preview
 * (single click) or pinned tab (double click). */
WsClickAction ws_click_visible(Workspace *w, int row, int double_click);
WsClickAction ws_click_visible_timed(Workspace *w, WsSidebarClickState *state,
                                     int row, double now, double max_delay);

/* Join the root with a relative path. Caller frees. */
char *ws_fullpath(const Workspace *w, const char *rel);

WsFileEffect ws_create_file_in(Workspace *w, const char *dir_rel,
                               const char *name);
WsFileEffect ws_create_dir_in(Workspace *w, const char *dir_rel,
                              const char *name);
WsFileEffect ws_paste_path_into(Workspace *w, const char *source_abs,
                                const char *dir_rel, int move);
WsFileEffect ws_delete_path(Workspace *w, const char *rel);

#endif /* WAVE_WORKSPACE_H */
