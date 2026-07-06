#include "git_view.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void git_quote(const char *s, char *out, size_t cap) {
    if (!out || cap == 0) return;
    size_t n = 0;
    out[n++] = '\'';
    if (s) {
        for (const char *p = s; *p && n + 5 < cap; p++) {
            if (*p == '\'') {
                memcpy(out + n, "'\\''", 4);
                n += 4;
            } else {
                out[n++] = *p;
            }
        }
    }
    if (n + 1 < cap) out[n++] = '\'';
    out[n < cap ? n : cap - 1] = '\0';
}

static int path_is_dir(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static int repo_marker_exists(const char *path) {
    char marker[GIT_VIEW_MAX_PATH];
    snprintf(marker, sizeof marker, "%s/.git", path ? path : "");
    return path_exists(marker);
}

static const char *base_name(const char *path) {
    if (!path || !path[0]) return "";
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static int run_capture_status(const char *cmd, char lines[][512], int max_lines,
                              int *status) {
    if (status) *status = -1;
    if (!cmd || !lines || max_lines <= 0) return 0;
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    int n = 0;
    char buf[1024];
    while (n < max_lines && fgets(buf, sizeof buf, fp)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        snprintf(lines[n++], 512, "%s", buf);
    }
    int rc = pclose(fp);
    if (status) {
        if (rc != -1 && WIFEXITED(rc)) *status = WEXITSTATUS(rc);
        else *status = -1;
    }
    return n;
}

static int run_capture(const char *cmd, char lines[][512], int max_lines) {
    return run_capture_status(cmd, lines, max_lines, NULL);
}

static int run_status(const char *cmd) {
    if (!cmd) return -1;
    int rc = system(cmd);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

static int git_command(GitView *view, const char *args, char *out, size_t cap) {
    char repo_q[GIT_VIEW_MAX_PATH + 16];
    char cmd[8192];
    git_quote(view ? view->repo : "", repo_q, sizeof repo_q);
    snprintf(cmd, sizeof cmd, "git -C %s %s 2>&1", repo_q, args ? args : "");
    char lines[64][512];
    int status = -1;
    int n = run_capture_status(cmd, lines, 64, &status);
    if (out && cap > 0) {
        out[0] = '\0';
        for (int i = 0; i < n; i++) {
            size_t used = strlen(out);
            if (used + strlen(lines[i]) + 2 >= cap) break;
            snprintf(out + used, cap - used, "%s%s", i ? " " : "", lines[i]);
        }
    }
    return status == 0 ? 0 : -1;
}

static int git_status_command(const char *repo, const char *args) {
    char repo_q[GIT_VIEW_MAX_PATH + 16];
    char cmd[8192];
    git_quote(repo, repo_q, sizeof repo_q);
    snprintf(cmd, sizeof cmd, "git -C %s %s >/dev/null 2>&1", repo_q, args ? args : "");
    return run_status(cmd);
}

static int git_repo_valid(const char *path) {
    return path && path[0] && git_status_command(path, "rev-parse --git-dir") == 0;
}

static void add_repo(GitView *view, const char *path) {
    if (!view || !path || view->repo_count >= GIT_VIEW_MAX_REPOS) return;
    for (int i = 0; i < view->repo_count; i++)
        if (!strcmp(view->repos[i].path, path)) return;
    snprintf(view->repos[view->repo_count].path,
             sizeof view->repos[view->repo_count].path, "%s", path);
    snprintf(view->repos[view->repo_count].label,
             sizeof view->repos[view->repo_count].label, "%s", base_name(path));
    view->repo_count++;
}

static int discover_repos(GitView *view, const char *root) {
    if (!view || !root || !root[0]) return 0;
    if (repo_marker_exists(root) || git_repo_valid(root)) add_repo(view, root);
    if (view->repo_count > 0) return view->repo_count;

    DIR *dir = opendir(root);
    if (!dir) return 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) && view->repo_count < GIT_VIEW_MAX_REPOS) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        char child[GIT_VIEW_MAX_PATH];
        snprintf(child, sizeof child, "%s/%s", root, ent->d_name);
        if (!path_is_dir(child)) continue;
        if (repo_marker_exists(child) || git_repo_valid(child)) add_repo(view, child);
    }
    closedir(dir);
    return view->repo_count;
}

static void load_history(GitView *view) {
    view->history_count = 0;
    if (!view->repo[0]) return;
    char repo_q[GIT_VIEW_MAX_PATH + 16];
    char cmd[8192];
    git_quote(view->repo, repo_q, sizeof repo_q);
    snprintf(cmd, sizeof cmd, "git -C %s log --oneline -20 2>&1", repo_q);
    char lines[32][512];
    int n = run_capture(cmd, lines, 32);
    for (int i = 0; i < n && i < 32; i++)
        snprintf(view->history[view->history_count++], sizeof view->history[0], "%s", lines[i]);
}

static void load_files(GitView *view) {
    view->file_count = 0;
    view->selected_file = 0;
    if (!view->repo[0]) return;
    char repo_q[GIT_VIEW_MAX_PATH + 16];
    char cmd[8192];
    git_quote(view->repo, repo_q, sizeof repo_q);
    snprintf(cmd, sizeof cmd, "git -C %s status --porcelain 2>&1", repo_q);
    char lines[GIT_VIEW_MAX_FILES][512];
    int n = run_capture(cmd, lines, GIT_VIEW_MAX_FILES);
    for (int i = 0; i < n && view->file_count < GIT_VIEW_MAX_FILES; i++) {
        if ((int)strlen(lines[i]) < 4) continue;
        GitFileChange *f = &view->files[view->file_count++];
        snprintf(f->code, sizeof f->code, "%c%c", lines[i][0], lines[i][1]);
        const char *path = lines[i] + 3;
        const char *rename = strstr(path, " -> ");
        if (rename) path = rename + 4;
        snprintf(f->path, sizeof f->path, "%s", path);
    }
}

static int is_staged_code(const char *code) {
    return code && code[0] != '\0' && code[0] != ' ' && code[0] != '?';
}

const GitFileChange *git_view_selected_file(const GitView *view) {
    if (!view || view->selected_file < 0 || view->selected_file >= view->file_count)
        return NULL;
    return &view->files[view->selected_file];
}

static void load_diff(GitView *view) {
    view->diff_count = 0;
    view->diff_scroll = 0;
    git_view_diff_selection_clear(view);
    const GitFileChange *file = git_view_selected_file(view);
    if (!view->repo[0] || !file) return;
    char repo_q[GIT_VIEW_MAX_PATH + 16];
    char path_q[GIT_VIEW_MAX_PATH + 16];
    char cmd[8192];
    git_quote(view->repo, repo_q, sizeof repo_q);
    git_quote(file->path, path_q, sizeof path_q);
    const char *diff_kind = is_staged_code(file->code) ? "diff --cached --" : "diff --";
    snprintf(cmd, sizeof cmd, "git -C %s %s %s 2>&1", repo_q, diff_kind, path_q);
    view->diff_count = run_capture(cmd, view->diff, GIT_VIEW_MAX_LINES);
    if (view->diff_count == 0 && file->code[0] == '?' && file->code[1] == '?') {
        snprintf(view->diff[view->diff_count++], sizeof view->diff[0],
                 "Untracked file. Press Space to stage it.");
    }
}

void git_view_init(GitView *view) {
    if (!view) return;
    memset(view, 0, sizeof *view);
}

void git_view_free(GitView *view) {
    if (!view) return;
    git_view_init(view);
}

int git_view_open(GitView *view, const char *root) {
    if (!view) return 0;
    git_view_init(view);
    snprintf(view->root, sizeof view->root, "%s", root && root[0] ? root : ".");
    int root_is_repo = repo_marker_exists(view->root) || git_repo_valid(view->root);
    if (!discover_repos(view, view->root)) {
        view->mode = GIT_VIEW_REPO_SELECT;
        snprintf(view->info, sizeof view->info, "no git repositories found");
        return 0;
    }
    if (root_is_repo && view->repo_count == 1) {
        snprintf(view->repo, sizeof view->repo, "%s", view->repos[0].path);
        view->mode = GIT_VIEW_CHANGES;
        return git_view_refresh(view);
    }
    view->mode = GIT_VIEW_REPO_SELECT;
    snprintf(view->info, sizeof view->info, "select a repository");
    return 1;
}

int git_view_refresh(GitView *view) {
    if (!view) return 0;
    if (!view->repo[0] && view->repo_count == 1)
        snprintf(view->repo, sizeof view->repo, "%s", view->repos[0].path);
    if (!view->repo[0]) return 0;
    load_files(view);
    load_history(view);
    load_diff(view);
    snprintf(view->info, sizeof view->info, "%s",
             view->file_count ? "changes refreshed" : "working tree clean");
    return 1;
}

void git_view_move(GitView *view, int delta) {
    if (!view || delta == 0) return;
    int *selected = view->mode == GIT_VIEW_REPO_SELECT ? &view->selected_repo
                                                       : &view->selected_file;
    int count = view->mode == GIT_VIEW_REPO_SELECT ? view->repo_count
                                                   : view->file_count;
    if (count <= 0) return;
    *selected += delta;
    if (*selected < 0) *selected = 0;
    if (*selected >= count) *selected = count - 1;
    if (view->mode == GIT_VIEW_CHANGES) load_diff(view);
}

int git_view_select_repo(GitView *view, int index) {
    if (!view || index < 0 || index >= view->repo_count) return 0;
    view->selected_repo = index;
    return git_view_accept(view);
}

int git_view_select_file(GitView *view, int index) {
    if (!view || index < 0 || index >= view->file_count) return 0;
    view->selected_file = index;
    load_diff(view);
    return 1;
}

void git_view_diff_scroll(GitView *view, int delta) {
    if (!view || delta == 0) return;
    view->diff_scroll += delta;
    if (view->diff_scroll < 0) view->diff_scroll = 0;
    if (view->diff_scroll >= view->diff_count) view->diff_scroll = view->diff_count - 1;
    if (view->diff_scroll < 0) view->diff_scroll = 0;
}

int git_view_accept(GitView *view) {
    if (!view || view->mode != GIT_VIEW_REPO_SELECT) return 0;
    if (view->selected_repo < 0 || view->selected_repo >= view->repo_count) return 0;
    snprintf(view->repo, sizeof view->repo, "%s", view->repos[view->selected_repo].path);
    view->mode = GIT_VIEW_CHANGES;
    return git_view_refresh(view);
}

int git_view_stage_toggle(GitView *view) {
    const GitFileChange *file = git_view_selected_file(view);
    if (!view || !file) return 0;
    char path_q[GIT_VIEW_MAX_PATH + 16];
    char args[8192];
    git_quote(file->path, path_q, sizeof path_q);
    if (is_staged_code(file->code))
        snprintf(args, sizeof args, "restore --staged -- %s", path_q);
    else
        snprintf(args, sizeof args, "add -- %s", path_q);
    if (git_status_command(view->repo, args) == 0) {
        snprintf(view->info, sizeof view->info, "%s",
                 is_staged_code(file->code) ? "unstaged file" : "staged file");
        return git_view_refresh(view);
    }
    snprintf(view->info, sizeof view->info, "git stage failed");
    return 0;
}

int git_view_begin_commit(GitView *view) {
    if (!view || !view->repo[0]) return 0;
    view->mode = GIT_VIEW_COMMIT_INPUT;
    view->message_len = 0;
    view->message[0] = '\0';
    snprintf(view->info, sizeof view->info, "commit message");
    return 1;
}

int git_view_commit(GitView *view) {
    if (!view || !view->repo[0] || view->message_len <= 0) return 0;
    char msg_q[512];
    char out[512];
    char args[1024];
    git_quote(view->message, msg_q, sizeof msg_q);
    snprintf(args, sizeof args, "commit -m %s", msg_q);
    int ok = git_command(view, args, out, sizeof out) == 0;
    if (ok) {
        view->mode = GIT_VIEW_CHANGES;
        git_view_refresh(view);
    }
    snprintf(view->info, sizeof view->info, "%s",
             out[0] ? out : (ok ? "commit complete" : "commit failed"));
    return ok;
}

void git_view_cancel_input(GitView *view) {
    if (!view) return;
    if (view->mode == GIT_VIEW_COMMIT_INPUT) {
        view->mode = GIT_VIEW_CHANGES;
        view->message_len = 0;
        view->message[0] = '\0';
        snprintf(view->info, sizeof view->info, "commit cancelled");
    }
}

int git_view_insert_char(GitView *view, unsigned int cp) {
    if (!view || view->mode != GIT_VIEW_COMMIT_INPUT) return 0;
    if (cp < 32 || cp >= 127 || view->message_len >= (int)sizeof view->message - 1)
        return 1;
    view->message[view->message_len++] = (char)cp;
    view->message[view->message_len] = '\0';
    return 1;
}

int git_view_insert_text(GitView *view, const char *text) {
    if (!view || !text) return 0;
    int handled = 0;
    while (*text) handled |= git_view_insert_char(view, (unsigned char)*text++);
    return handled;
}

int git_view_backspace(GitView *view) {
    if (!view || view->mode != GIT_VIEW_COMMIT_INPUT) return 0;
    if (view->message_len > 0) view->message[--view->message_len] = '\0';
    return 1;
}

int git_view_repo_selecting(const GitView *view) {
    return view && view->mode == GIT_VIEW_REPO_SELECT;
}

static void git_view_normalized_diff_selection(const GitView *view, int *start_line,
                                               int *start_col, int *end_line,
                                               int *end_col) {
    int al = view->diff_selection_anchor_line;
    int hl = view->diff_selection_head_line;
    int ac = view->diff_selection_anchor_col;
    int hc = view->diff_selection_head_col;
    if (al > hl || (al == hl && ac > hc)) {
        int tl = al;
        int tc = ac;
        al = hl;
        ac = hc;
        hl = tl;
        hc = tc;
    }
    if (start_line) *start_line = al;
    if (start_col) *start_col = ac;
    if (end_line) *end_line = hl;
    if (end_col) *end_col = hc;
}

void git_view_diff_selection_clear(GitView *view) {
    if (!view) return;
    view->diff_selection_active = 0;
    view->diff_selection_dragging = 0;
    view->diff_selection_anchor_line = view->diff_selection_head_line = 0;
    view->diff_selection_anchor_col = view->diff_selection_head_col = 0;
}

void git_view_diff_selection_begin(GitView *view, int line, int col) {
    if (!view || line < 0 || line >= view->diff_count) return;
    if (col < 0) col = 0;
    view->diff_selection_active = 1;
    view->diff_selection_dragging = 1;
    view->diff_selection_anchor_line = line;
    view->diff_selection_head_line = line;
    view->diff_selection_anchor_col = col;
    view->diff_selection_head_col = col;
}

void git_view_diff_selection_update(GitView *view, int line, int col) {
    if (!view || !view->diff_selection_dragging || view->diff_count <= 0) return;
    if (line < 0) line = 0;
    if (line >= view->diff_count) line = view->diff_count - 1;
    if (col < 0) col = 0;
    view->diff_selection_head_line = line;
    view->diff_selection_head_col = col;
}

void git_view_diff_selection_end(GitView *view) {
    if (!view) return;
    view->diff_selection_dragging = 0;
    if (view->diff_selection_anchor_line == view->diff_selection_head_line &&
        view->diff_selection_anchor_col == view->diff_selection_head_col)
        git_view_diff_selection_clear(view);
}

int git_view_diff_selection_span(const GitView *view, int line, int *start_col,
                                 int *end_col) {
    if (!view || !view->diff_selection_active) return 0;
    int sl = 0, el = 0, sc = 0, ec = 0;
    git_view_normalized_diff_selection(view, &sl, &sc, &el, &ec);
    if (line < sl || line > el) return 0;
    int a = line == sl ? sc : 0;
    int b = line == el ? ec : (int)strlen(view->diff[line]);
    if (b < a) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    if (b <= a) b = a + 1;
    if (start_col) *start_col = a;
    if (end_col) *end_col = b;
    return 1;
}

char *git_view_copy_diff_selection(const GitView *view) {
    if (!view || !view->diff_selection_active) return NULL;
    int sl = 0, el = 0, sc = 0, ec = 0;
    git_view_normalized_diff_selection(view, &sl, &sc, &el, &ec);
    if (sl == el && sc == ec) return NULL;
    if (sl < 0) sl = 0;
    if (el >= view->diff_count) el = view->diff_count - 1;
    if (sl > el || view->diff_count <= 0) return NULL;
    size_t cap = 1;
    for (int line = sl; line <= el; line++) {
        int len = (int)strlen(view->diff[line]);
        int a = line == sl ? sc : 0;
        int b = line == el ? ec : len;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a > len) a = len;
        if (b > len) b = len;
        if (b < a) {
            int tmp = a;
            a = b;
            b = tmp;
        }
        cap += (size_t)(b - a) + 1;
    }
    char *text = malloc(cap);
    if (!text) return NULL;
    size_t used = 0;
    text[0] = '\0';
    for (int line = sl; line <= el; line++) {
        int len = (int)strlen(view->diff[line]);
        int a = line == sl ? sc : 0;
        int b = line == el ? ec : len;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a > len) a = len;
        if (b > len) b = len;
        if (b < a) {
            int tmp = a;
            a = b;
            b = tmp;
        }
        if (b > a) {
            memcpy(text + used, view->diff[line] + a, (size_t)(b - a));
            used += (size_t)(b - a);
        }
        if (line < el) text[used++] = '\n';
        text[used] = '\0';
    }
    return text;
}
