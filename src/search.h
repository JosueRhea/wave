/* search.h — project-wide content search backed by ripgrep.
 *
 * Spawns `rg` as a child process and streams its `path:line:col:text` output
 * back asynchronously, the same non-blocking way the LSP client talks to a
 * language server: search_query() fires a search (cancelling any in flight),
 * search_poll() drains whatever ripgrep has emitted so far, and the UI reads
 * the accumulated hits. A missing `rg` just yields zero results.
 */
#ifndef WAVE_SEARCH_H
#define WAVE_SEARCH_H

#include <stddef.h>

typedef struct Search Search;

/* One matching line. `path` is relative to the search root; line/col are
 * 1-based (ripgrep coordinates). `text` is the matched line, trimmed. */
typedef struct {
    char path[1024];
    int line, col;
    char text[512];
} SearchHit;

/* Create a searcher that runs ripgrep binary `rg_path` over folder `root`.
 * Both are copied. Returns NULL only on allocation failure. */
Search *search_new(const char *rg_path, const char *root);
void search_free(Search *s);

/* Start searching for `pattern` (a fixed string, smart-case). Cancels any
 * in-flight search and clears prior results. An empty pattern just clears. */
void search_query(Search *s, const char *pattern);

/* Drain whatever ripgrep has written; cheap to call every frame. Parses
 * complete lines into hits and reaps the child on EOF. */
void search_poll(Search *s);

/* 1 while a ripgrep child is still producing results for the current query. */
int search_running(const Search *s);

/* The current query's results. */
size_t search_count(const Search *s);
const SearchHit *search_hit(const Search *s, size_t i);

/* The pattern currently displayed/searched (never NULL). */
const char *search_pattern(const Search *s);

/* 1 if the result set was capped (more matches exist than were collected). */
int search_truncated(const Search *s);

#endif /* WAVE_SEARCH_H */
