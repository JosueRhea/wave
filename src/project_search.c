#include "project_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

void project_search_init(ProjectSearch *ps) {
    memset(ps, 0, sizeof *ps);
}

void project_search_free(ProjectSearch *ps) {
    if (!ps) return;
    if (ps->search) search_free(ps->search);
    project_search_init(ps);
}

void project_search_open(ProjectSearch *ps) {
    if (!ps) return;
    ps->sel = 0;
    ps->scroll = 0;
}

void project_search_run(ProjectSearch *ps, const char *root) {
    if (!ps || !root) return;
    ps->unavailable = 0;
    if (!ps->search) {
        char rg[1024];
        if (!wave_rg_binary(rg, sizeof rg)) {
            ps->unavailable = 1;
            return;
        }
        ps->search = search_new(rg, root);
        if (!ps->search) {
            ps->unavailable = 1;
            return;
        }
    }
    ps->query[ps->query_len] = '\0';
    search_query(ps->search, ps->query);
    ps->sel = 0;
    ps->scroll = 0;
}

void project_search_set_query(ProjectSearch *ps, const char *root, const char *query) {
    if (!ps) return;
    snprintf(ps->query, sizeof ps->query, "%s", query ? query : "");
    ps->query_len = (int)strlen(ps->query);
    project_search_run(ps, root);
}

void project_search_insert_text(ProjectSearch *ps, const char *root, const char *text) {
    if (!ps || !text) return;
    while (*text && ps->query_len < (int)sizeof ps->query - 1) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 32 && c < 127)
            ps->query[ps->query_len++] = (char)c;
    }
    ps->query[ps->query_len] = '\0';
    project_search_run(ps, root);
}

void project_search_backspace(ProjectSearch *ps, const char *root) {
    if (!ps || ps->query_len <= 0) return;
    ps->query[--ps->query_len] = '\0';
    project_search_run(ps, root);
}

void project_search_move(ProjectSearch *ps, int delta) {
    if (!ps) return;
    int n = (int)project_search_count(ps);
    ps->sel += delta;
    if (ps->sel < 0) ps->sel = 0;
    if (ps->sel >= n) ps->sel = n ? n - 1 : 0;
}

void project_search_poll(ProjectSearch *ps) {
    if (ps && ps->search) search_poll(ps->search);
}

int project_search_running(const ProjectSearch *ps) {
    return ps && search_running(ps->search);
}

size_t project_search_count(const ProjectSearch *ps) {
    return ps ? search_count(ps->search) : 0;
}

const SearchHit *project_search_hit(const ProjectSearch *ps, size_t i) {
    return ps ? search_hit(ps->search, i) : NULL;
}

int project_search_truncated(const ProjectSearch *ps) {
    return ps && search_truncated(ps->search);
}
