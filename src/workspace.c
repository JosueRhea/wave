/* workspace.c — see workspace.h. */
#include "workspace.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct Workspace {
    char *root;
    WsEntry *entries;
    size_t count, cap;
    size_t *visible;       /* indices into entries, in display order */
    size_t vcount, vcap;
};

static const char *IGNORE[] = {
    ".git", "node_modules", "build", "dist", ".cache", ".next",
    "vendor", ".DS_Store", ".idea", ".vscode", "target", NULL,
};

static int ignored(const char *name) {
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
        return 1; /* "." and ".." */
    for (int i = 0; IGNORE[i]; i++)
        if (!strcmp(name, IGNORE[i])) return 1;
    return 0;
}

static void push(Workspace *w, const char *rel, int depth, int is_dir) {
    if (w->count == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 64;
        w->entries = realloc(w->entries, w->cap * sizeof(WsEntry));
    }
    WsEntry *e = &w->entries[w->count++];
    e->rel = strdup(rel);
    char *slash = strrchr(e->rel, '/');
    e->name = slash ? slash + 1 : e->rel;
    e->depth = depth;
    e->is_dir = is_dir;
    e->collapsed = is_dir; /* folders start collapsed; only top level shows */
}

/* Rebuild the visible list: walk the pre-order tree, dropping every entry that
 * lives under a collapsed directory (i.e. anything deeper than a collapsed dir
 * until the depth returns to that dir's level). */
static void rebuild_visible(Workspace *w) {
    if (w->vcap < w->count) {
        w->vcap = w->count;
        w->visible = realloc(w->visible, w->vcap * sizeof(size_t));
    }
    w->vcount = 0;
    int skip_depth = -1; /* when >=0, hide entries deeper than this */
    for (size_t i = 0; i < w->count; i++) {
        WsEntry *e = &w->entries[i];
        if (skip_depth >= 0) {
            if (e->depth > skip_depth) continue; /* inside a collapsed subtree */
            skip_depth = -1;                     /* back out to a visible level */
        }
        w->visible[w->vcount++] = i;
        if (e->is_dir && e->collapsed) skip_depth = e->depth;
    }
}

/* one directory's children, for sorting: dirs first, then case-folded name. */
typedef struct { char *name; int is_dir; } Child;

static int child_cmp(const void *a, const void *b) {
    const Child *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir; /* dirs first */
    return strcasecmp(x->name, y->name);
}

static void scan(Workspace *w, const char *abs, const char *rel, int depth) {
    DIR *d = opendir(abs);
    if (!d) return;

    Child *kids = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (ignored(de->d_name)) continue;
        char child_abs[4096];
        snprintf(child_abs, sizeof child_abs, "%s/%s", abs, de->d_name);
        struct stat st;
        if (stat(child_abs, &st) != 0) continue;
        int is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            kids = realloc(kids, cap * sizeof(Child));
        }
        kids[n].name = strdup(de->d_name);
        kids[n].is_dir = is_dir;
        n++;
    }
    closedir(d);
    qsort(kids, n, sizeof(Child), child_cmp);

    for (size_t i = 0; i < n; i++) {
        char child_rel[4096], child_abs[4096];
        if (rel[0])
            snprintf(child_rel, sizeof child_rel, "%s/%s", rel, kids[i].name);
        else
            snprintf(child_rel, sizeof child_rel, "%s", kids[i].name);
        snprintf(child_abs, sizeof child_abs, "%s/%s", abs, kids[i].name);
        push(w, child_rel, depth, kids[i].is_dir);
        if (kids[i].is_dir) scan(w, child_abs, child_rel, depth + 1);
        free(kids[i].name);
    }
    free(kids);
}

Workspace *ws_open(const char *root) {
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;

    Workspace *w = calloc(1, sizeof(Workspace));
    /* normalise: drop a trailing slash so joins are clean */
    w->root = strdup(root);
    size_t rl = strlen(w->root);
    while (rl > 1 && w->root[rl - 1] == '/') w->root[--rl] = '\0';
    scan(w, w->root, "", 0);
    rebuild_visible(w);
    return w;
}

void ws_free(Workspace *w) {
    if (!w) return;
    for (size_t i = 0; i < w->count; i++) free(w->entries[i].rel);
    free(w->entries);
    free(w->visible);
    free(w->root);
    free(w);
}

const char *ws_root(const Workspace *w) { return w->root; }
size_t ws_count(const Workspace *w) { return w->count; }

const WsEntry *ws_entry(const Workspace *w, size_t i) {
    return i < w->count ? &w->entries[i] : NULL;
}

size_t ws_visible_count(const Workspace *w) { return w->vcount; }

const WsEntry *ws_visible(const Workspace *w, size_t vi) {
    return vi < w->vcount ? &w->entries[w->visible[vi]] : NULL;
}

void ws_visible_toggle(Workspace *w, size_t vi) {
    if (vi >= w->vcount) return;
    WsEntry *e = &w->entries[w->visible[vi]];
    if (!e->is_dir) return;
    e->collapsed = !e->collapsed;
    rebuild_visible(w);
}

char *ws_fullpath(const Workspace *w, const char *rel) {
    size_t n = strlen(w->root) + 1 + strlen(rel) + 1;
    char *p = malloc(n);
    snprintf(p, n, "%s/%s", w->root, rel);
    return p;
}
