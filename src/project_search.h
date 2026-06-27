#ifndef WAVE_PROJECT_SEARCH_H
#define WAVE_PROJECT_SEARCH_H

#include <stddef.h>

#include "search.h"

typedef struct {
    Search *search;
    char query[256];
    int query_len;
    int sel, scroll;
    int unavailable;
} ProjectSearch;

void project_search_init(ProjectSearch *ps);
void project_search_free(ProjectSearch *ps);
void project_search_open(ProjectSearch *ps);
void project_search_run(ProjectSearch *ps, const char *root);
void project_search_set_query(ProjectSearch *ps, const char *root, const char *query);
void project_search_insert_text(ProjectSearch *ps, const char *root, const char *text);
void project_search_backspace(ProjectSearch *ps, const char *root);
void project_search_move(ProjectSearch *ps, int delta);
void project_search_poll(ProjectSearch *ps);
int project_search_running(const ProjectSearch *ps);
size_t project_search_count(const ProjectSearch *ps);
const SearchHit *project_search_hit(const ProjectSearch *ps, size_t i);
int project_search_truncated(const ProjectSearch *ps);

#endif
