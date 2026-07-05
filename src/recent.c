#include "recent.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int recent_project_dir_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *recent_project_base(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static int recent_project_match(const char *query, const char *path) {
    if (!query || !query[0]) return 1;
    const char *base = recent_project_base(path);
    const char *haystacks[2] = {base, path ? path : ""};
    for (int h = 0; h < 2; h++) {
        const char *q = query;
        for (const char *s = haystacks[h]; *s && *q; s++) {
            if (tolower((unsigned char)*s) == tolower((unsigned char)*q)) q++;
        }
        if (!*q) return 1;
    }
    return 0;
}

static void recent_projects_refilter(RecentProjects *r) {
    if (!r) return;
    r->filtered_count = 0;
    r->query[r->query_len] = '\0';
    for (size_t i = 0; i < r->count; i++) {
        if (!recent_project_match(r->query, r->paths[i])) continue;
        r->filtered[r->filtered_count++] = i;
    }
    if (r->selected < 0) r->selected = 0;
    if (r->selected >= (int)r->filtered_count)
        r->selected = r->filtered_count ? (int)r->filtered_count - 1 : 0;
}

static void recent_projects_config_dir(char *out, size_t cap) {
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/.config/wave", home ? home : ".");
}

void recent_projects_init(RecentProjects *r) {
    if (!r) return;
    memset(r, 0, sizeof *r);
}

void recent_projects_path(char *out, size_t cap) {
    char dir[1024];
    recent_projects_config_dir(dir, sizeof dir);
    snprintf(out, cap, "%s/recent-projects", dir);
}

int recent_projects_load(RecentProjects *r) {
    if (!r) return -1;
    r->count = 0;
    r->query[0] = '\0';
    r->query_len = 0;
    r->selected = 0;

    char path[1100];
    recent_projects_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) {
        recent_projects_refilter(r);
        return -1;
    }

    char line[RECENT_PROJECT_PATH_MAX + 8];
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        if (!recent_project_dir_exists(line)) continue;
        int duplicate = 0;
        for (size_t i = 0; i < r->count; i++) {
            if (!strcmp(r->paths[i], line)) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate && r->count < RECENT_PROJECTS_MAX)
            snprintf(r->paths[r->count++], sizeof r->paths[0], "%s", line);
    }
    fclose(f);
    recent_projects_refilter(r);
    return 0;
}

int recent_projects_save(const RecentProjects *r) {
    if (!r) return -1;
    char dir[1024];
    recent_projects_config_dir(dir, sizeof dir);
    char parent[1024];
    const char *home = getenv("HOME");
    snprintf(parent, sizeof parent, "%s/.config", home ? home : ".");
    mkdir(parent, 0755);
    mkdir(dir, 0755);

    char path[1100];
    recent_projects_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# wave recent projects\n");
    for (size_t i = 0; i < r->count; i++) fprintf(f, "%s\n", r->paths[i]);
    fclose(f);
    return 0;
}

int recent_projects_add(RecentProjects *r, const char *path) {
    if (!r || !recent_project_dir_exists(path)) return 0;

    char normalized[RECENT_PROJECT_PATH_MAX];
    if (!realpath(path, normalized)) snprintf(normalized, sizeof normalized, "%s", path);

    size_t existing = RECENT_PROJECTS_MAX;
    for (size_t i = 0; i < r->count; i++) {
        if (!strcmp(r->paths[i], normalized)) {
            existing = i;
            break;
        }
    }

    if (existing == 0) {
        recent_projects_refilter(r);
        return 1;
    }
    int is_new = existing == RECENT_PROJECTS_MAX;
    size_t end = r->count < RECENT_PROJECTS_MAX ? r->count : RECENT_PROJECTS_MAX - 1;
    if (existing < RECENT_PROJECTS_MAX) end = existing;
    for (size_t i = end; i > 0; i--)
        snprintf(r->paths[i], sizeof r->paths[i], "%s", r->paths[i - 1]);
    snprintf(r->paths[0], sizeof r->paths[0], "%s", normalized);
    if (is_new && r->count < RECENT_PROJECTS_MAX) r->count++;
    recent_projects_refilter(r);
    return 1;
}

void recent_projects_set_query(RecentProjects *r, const char *query) {
    if (!r) return;
    snprintf(r->query, sizeof r->query, "%s", query ? query : "");
    r->query_len = (int)strlen(r->query);
    recent_projects_refilter(r);
}

void recent_projects_insert_text(RecentProjects *r, const char *text) {
    if (!r || !text) return;
    while (*text && r->query_len < (int)sizeof r->query - 1) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32 && c < 127)
            r->query[r->query_len++] = (char)c;
    }
    r->query[r->query_len] = '\0';
    recent_projects_refilter(r);
}

void recent_projects_backspace(RecentProjects *r) {
    if (!r || r->query_len <= 0) return;
    r->query[--r->query_len] = '\0';
    recent_projects_refilter(r);
}

void recent_projects_move(RecentProjects *r, int delta) {
    if (!r) return;
    r->selected += delta;
    if (r->selected < 0) r->selected = 0;
    if (r->selected >= (int)r->filtered_count)
        r->selected = r->filtered_count ? (int)r->filtered_count - 1 : 0;
}

const char *recent_projects_selected(const RecentProjects *r) {
    if (!r || r->selected < 0 || r->selected >= (int)r->filtered_count) return NULL;
    return r->paths[r->filtered[r->selected]];
}

const char *recent_projects_filtered_path(const RecentProjects *r, size_t index) {
    if (!r || index >= r->filtered_count) return NULL;
    return r->paths[r->filtered[index]];
}
