#ifndef WAVE_RECENT_H
#define WAVE_RECENT_H

#include <stddef.h>

#define RECENT_PROJECTS_MAX 32
#define RECENT_PROJECT_PATH_MAX 4096

typedef struct {
    char paths[RECENT_PROJECTS_MAX][RECENT_PROJECT_PATH_MAX];
    size_t count;
    char query[256];
    int query_len;
    size_t filtered[RECENT_PROJECTS_MAX];
    size_t filtered_count;
    int selected;
} RecentProjects;

void recent_projects_init(RecentProjects *r);
void recent_projects_path(char *out, size_t cap);
int recent_projects_load(RecentProjects *r);
int recent_projects_save(const RecentProjects *r);
int recent_projects_add(RecentProjects *r, const char *path);
void recent_projects_set_query(RecentProjects *r, const char *query);
void recent_projects_insert_text(RecentProjects *r, const char *text);
void recent_projects_backspace(RecentProjects *r);
void recent_projects_move(RecentProjects *r, int delta);
const char *recent_projects_selected(const RecentProjects *r);
const char *recent_projects_filtered_path(const RecentProjects *r, size_t index);

#endif
