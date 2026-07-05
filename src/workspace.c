/* workspace.c — see workspace.h. */
#include "workspace.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void clear_entries(Workspace *w) {
    for (size_t i = 0; i < w->count; i++) free(w->entries[i].rel);
    free(w->entries);
    free(w->visible);
    w->entries = NULL;
    w->visible = NULL;
    w->count = w->cap = 0;
    w->vcount = w->vcap = 0;
}

static int strptr_cmp(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static int rel_in_sorted(char **rels, size_t n, const char *rel) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(rels[mid], rel);
        if (c == 0) return 1;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
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
    clear_entries(w);
    free(w->root);
    free(w);
}

int ws_reload(Workspace *w) {
    if (!w) return -1;
    struct stat st;
    if (stat(w->root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;

    char **expanded = NULL;
    size_t expanded_n = 0, expanded_cap = 0;
    for (size_t i = 0; i < w->count; i++) {
        WsEntry *e = &w->entries[i];
        if (!e->is_dir || e->collapsed) continue;
        if (expanded_n == expanded_cap) {
            expanded_cap = expanded_cap ? expanded_cap * 2 : 16;
            expanded = realloc(expanded, expanded_cap * sizeof(char *));
        }
        expanded[expanded_n++] = strdup(e->rel);
    }
    if (expanded_n > 0)
        qsort(expanded, expanded_n, sizeof(char *), strptr_cmp);

    clear_entries(w);
    scan(w, w->root, "", 0);
    for (size_t i = 0; i < w->count; i++) {
        WsEntry *e = &w->entries[i];
        if (e->is_dir && rel_in_sorted(expanded, expanded_n, e->rel))
            e->collapsed = 0;
    }
    rebuild_visible(w);

    for (size_t i = 0; i < expanded_n; i++) free(expanded[i]);
    free(expanded);
    return 0;
}

WsReloadEffect ws_apply_reload(Workspace *w) {
    WsReloadEffect effect = {0};
    if (ws_reload(w) == 0) {
        effect.ok = 1;
        effect.refilter_palette = 1;
        snprintf(effect.message, sizeof effect.message, "workspace updated");
    } else {
        snprintf(effect.message, sizeof effect.message, "workspace unavailable: %s",
                 w ? ws_root(w) : "");
    }
    return effect;
}

WsReloadEffect ws_apply_watch_event(Workspace *w, int pending) {
    WsReloadEffect effect = {0};
    if (!pending || !w) return effect;
    return ws_apply_reload(w);
}

WsOpenContext ws_open_context(const char *arg) {
    WsOpenContext ctx = {0};
    if (!arg || !*arg) return ctx;

    struct stat st;
    if (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
        ctx.workspace = ws_open(arg);
        ctx.kind = ctx.workspace ? WS_OPEN_WORKSPACE : WS_OPEN_NONE;
        return ctx;
    }

    char dir[4096];
    snprintf(dir, sizeof dir, "%s", arg);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        ctx.workspace = ws_open(dir[0] ? dir : "/");
    } else {
        ctx.workspace = ws_open(".");
    }
    snprintf(ctx.file, sizeof ctx.file, "%s", arg);
    ctx.kind = ctx.workspace ? WS_OPEN_FILE : WS_OPEN_NONE;
    return ctx;
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

WsClickAction ws_click_visible(Workspace *w, int row, int double_click) {
    WsClickAction action = { WS_CLICK_NONE, NULL, row, 0 };
    if (!w || row < 0) return action;
    const WsEntry *e = ws_visible(w, (size_t)row);
    if (!e) return action;

    if (e->is_dir) {
        ws_visible_toggle(w, (size_t)row);
        action.kind = WS_CLICK_TOGGLE_DIR;
        return action;
    }

    action.kind = WS_CLICK_OPEN_FILE;
    action.entry = e;
    action.preview = !double_click;
    return action;
}

WsClickAction ws_click_visible_timed(Workspace *w, WsSidebarClickState *state,
                                     int row, double now, double max_delay) {
    WsClickAction action = { WS_CLICK_NONE, NULL, row, 0 };
    if (!w || row < 0) return action;
    const WsEntry *e = ws_visible(w, (size_t)row);
    if (!e) return action;
    if (e->is_dir) return ws_click_visible(w, row, 0);

    int dbl = state && state->seen && now - state->time < max_delay &&
              row == state->row;
    if (state) {
        state->seen = 1;
        state->time = now;
        state->row = row;
    }
    return ws_click_visible(w, row, dbl);
}

char *ws_fullpath(const Workspace *w, const char *rel) {
    size_t n = strlen(w->root) + 1 + strlen(rel) + 1;
    char *p = malloc(n);
    snprintf(p, n, "%s/%s", w->root, rel);
    return p;
}

static WsFileEffect ws_file_error(const char *prefix, const char *path) {
    WsFileEffect effect = {0};
    snprintf(effect.path, sizeof effect.path, "%s", path ? path : "");
    snprintf(effect.message, sizeof effect.message, "%s: %s",
             prefix ? prefix : "file action failed", strerror(errno));
    return effect;
}

static WsFileEffect ws_file_ok(const char *message, const char *path) {
    WsFileEffect effect = {0};
    effect.ok = 1;
    snprintf(effect.path, sizeof effect.path, "%s", path ? path : "");
    snprintf(effect.message, sizeof effect.message, "%s", message ? message : "");
    return effect;
}

static int invalid_name(const char *name) {
    return !name || !name[0] || name[0] == '/' || strstr(name, "/../") ||
           !strcmp(name, ".") || !strcmp(name, "..") ||
           !strncmp(name, "../", 3) || strstr(name, "/..") ||
           strstr(name, "//");
}

static void join_under_root(char *out, size_t cap, const Workspace *w,
                            const char *dir_rel, const char *name) {
    if (dir_rel && dir_rel[0])
        snprintf(out, cap, "%s/%s/%s", w->root, dir_rel, name ? name : "");
    else
        snprintf(out, cap, "%s/%s", w->root, name ? name : "");
}

static int mkdirs_for_file(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    return 0;
}

WsFileEffect ws_create_file_in(Workspace *w, const char *dir_rel,
                               const char *name) {
    if (!w || invalid_name(name)) {
        errno = EINVAL;
        return ws_file_error("invalid file name", name);
    }
    char path[4096];
    join_under_root(path, sizeof path, w, dir_rel, name);
    char parented[4096];
    snprintf(parented, sizeof parented, "%s", path);
    if (mkdirs_for_file(parented) != 0)
        return ws_file_error("could not create parent folder", path);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return ws_file_error("could not create file", path);
    close(fd);
    ws_reload(w);
    return ws_file_ok("file created", path);
}

WsFileEffect ws_create_dir_in(Workspace *w, const char *dir_rel,
                              const char *name) {
    if (!w || invalid_name(name)) {
        errno = EINVAL;
        return ws_file_error("invalid folder name", name);
    }
    char path[4096];
    join_under_root(path, sizeof path, w, dir_rel, name);
    if (mkdirs_for_file(path) != 0)
        return ws_file_error("could not create parent folder", path);
    if (mkdir(path, 0755) != 0)
        return ws_file_error("could not create folder", path);
    ws_reload(w);
    return ws_file_ok("folder created", path);
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[32768];
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0) {
            close(in);
            close(out);
            return -1;
        }
        if (n == 0) break;
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w <= 0) {
                close(in);
                close(out);
                return -1;
            }
            p += w;
            n -= w;
        }
    }
    close(in);
    close(out);
    return 0;
}

static int copy_dir(const char *src, const char *dst) {
    if (mkdir(dst, 0755) != 0) return -1;
    DIR *d = opendir(src);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char child_src[4096], child_dst[4096];
        snprintf(child_src, sizeof child_src, "%s/%s", src, de->d_name);
        snprintf(child_dst, sizeof child_dst, "%s/%s", dst, de->d_name);
        struct stat st;
        if (stat(child_src, &st) != 0) {
            closedir(d);
            return -1;
        }
        int ok = S_ISDIR(st.st_mode) ? copy_dir(child_src, child_dst)
                                     : copy_file(child_src, child_dst);
        if (ok != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

static int remove_tree(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char child[4096];
        snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (remove_tree(child) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return rmdir(path);
}

WsFileEffect ws_paste_path_into(Workspace *w, const char *source_abs,
                                const char *dir_rel, int move) {
    if (!w || !source_abs || !source_abs[0]) {
        errno = EINVAL;
        return ws_file_error("nothing to paste", source_abs);
    }
    const char *base = strrchr(source_abs, '/');
    base = base ? base + 1 : source_abs;
    if (invalid_name(base)) {
        errno = EINVAL;
        return ws_file_error("invalid source name", source_abs);
    }
    char dst[4096];
    join_under_root(dst, sizeof dst, w, dir_rel, base);
    if (!strcmp(source_abs, dst)) {
        errno = EEXIST;
        return ws_file_error("already in this folder", dst);
    }
    struct stat st;
    if (stat(source_abs, &st) != 0)
        return ws_file_error("source unavailable", source_abs);
    if (move && rename(source_abs, dst) == 0) {
        ws_reload(w);
        return ws_file_ok("moved", dst);
    }
    if (move && errno != EXDEV)
        return ws_file_error("could not move", dst);
    int ok = S_ISDIR(st.st_mode) ? copy_dir(source_abs, dst)
                                 : copy_file(source_abs, dst);
    if (ok != 0) return ws_file_error(move ? "could not move" : "could not copy", dst);
    if (move && remove_tree(source_abs) != 0)
        return ws_file_error("copied, but could not remove original", source_abs);
    ws_reload(w);
    return ws_file_ok(move ? "moved" : "copied", dst);
}

WsFileEffect ws_delete_path(Workspace *w, const char *rel) {
    if (!w || !rel || !rel[0]) {
        errno = EINVAL;
        return ws_file_error("nothing to delete", rel);
    }
    char *path = ws_fullpath(w, rel);
    WsFileEffect effect;
    if (remove_tree(path) == 0) {
        ws_reload(w);
        effect = ws_file_ok("deleted", path);
    } else {
        effect = ws_file_error("could not delete", path);
    }
    free(path);
    return effect;
}
