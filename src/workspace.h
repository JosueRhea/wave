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

/* Open `root` and scan it. Returns NULL if it isn't a readable directory. */
Workspace *ws_open(const char *root);
void ws_free(Workspace *w);

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

/* Join the root with a relative path. Caller frees. */
char *ws_fullpath(const Workspace *w, const char *rel);

#endif /* WAVE_WORKSPACE_H */
