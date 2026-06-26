/* test_search.c — content-search client tests against a real ripgrep.
 *
 * Drives the async search client (search.c) end to end: spawn ripgrep over a
 * temp folder, pump its output, and assert the parsed hits (path / line / col /
 * text). If no `rg` is available — neither the vendored vendor/rg/rg nor one on
 * PATH — the suite skips cleanly, the same graceful degradation the editor uses.
 */
#include "search.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define POLL_TRIES 500
#define POLL_SLEEP_US 10000

/* Locate a ripgrep binary: the vendored copy first, then PATH. */
static int find_rg(char *out, size_t cap) {
    if (access("vendor/rg/rg", X_OK) == 0) {
        snprintf(out, cap, "vendor/rg/rg");
        return 1;
    }
    const char *path = getenv("PATH");
    if (!path) return 0;
    char buf[2048];
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 4 < sizeof buf) {
            memcpy(buf, path, dlen);
            snprintf(buf + dlen, sizeof buf - dlen, "/rg");
            if (access(buf, X_OK) == 0) { snprintf(out, cap, "%s", buf); return 1; }
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

static void write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fputs(contents, f);
    fclose(f);
}

/* Run a query to completion (or timeout). */
static void run_to_completion(Search *s, const char *pattern) {
    search_query(s, pattern);
    for (int i = 0; i < POLL_TRIES && search_running(s); i++) {
        search_poll(s);
        if (!search_running(s)) break;
        usleep(POLL_SLEEP_US);
    }
    search_poll(s); /* final drain */
}

/* Find the hit for a given path, or NULL. */
static const SearchHit *hit_for(Search *s, const char *path) {
    for (size_t i = 0; i < search_count(s); i++) {
        const SearchHit *h = search_hit(s, i);
        if (!strcmp(h->path, path)) return h;
    }
    return NULL;
}

int main(void) {
    char rg[1024];
    if (!find_rg(rg, sizeof rg)) {
        printf("   (ripgrep not found — skipping search tests)\n");
        return 0;
    }

    char root[] = "/tmp/wave_search_XXXXXX";
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }

    char a[1100], b[1100], sub[1100], c[1100];
    snprintf(a, sizeof a, "%s/alpha.c", root);
    snprintf(b, sizeof b, "%s/beta.txt", root);
    snprintf(sub, sizeof sub, "%s/nested", root);
    snprintf(c, sizeof c, "%s/nested/gamma.c", root);

    write_file(a, "int main(void) {\n    return needle_value;\n}\n");
    write_file(b, "no match on this line\nanother plain line\n");
    mkdir(sub, 0755);
    write_file(c, "    int needle_value = 42;\nint other = 0;\n");

    Search *s = search_new(rg, root);
    CHECK(s != NULL);

    /* 1. a term present in two files, one nested, surfaces both with correct
     *    line/column and the matched (whitespace-trimmed) text. */
    run_to_completion(s, "needle_value");
    CHECK(!search_running(s));
    CHECK_EQ(search_count(s), 2);

    const SearchHit *ha = hit_for(s, "alpha.c");
    const SearchHit *hc = hit_for(s, "nested/gamma.c");
    CHECK(ha != NULL);
    CHECK(hc != NULL);
    if (ha) {
        CHECK_EQ(ha->line, 2);
        CHECK_EQ(ha->col, 12); /* "    return " = 11 cols, match at col 12 */
        CHECK_STR(ha->text, "return needle_value;");
    }
    if (hc) {
        CHECK_EQ(hc->line, 1);
        CHECK_STR(hc->text, "int needle_value = 42;"); /* leading indent trimmed */
    }

    /* 2. the displayed pattern reflects the current query. */
    CHECK_STR(search_pattern(s), "needle_value");

    /* 3. a no-match query yields an empty, completed result set. */
    run_to_completion(s, "zzz_no_such_token_zzz");
    CHECK(!search_running(s));
    CHECK_EQ(search_count(s), 0);

    /* 4. a fixed-string query is literal: regex metacharacters match verbatim
     *    and don't error out. */
    run_to_completion(s, "main(void)");
    CHECK_EQ(search_count(s), 1);

    /* 5. an empty pattern clears without spawning. */
    search_query(s, "");
    CHECK(!search_running(s));
    CHECK_EQ(search_count(s), 0);

    search_free(s);

    /* clean up the temp tree */
    unlink(a); unlink(b); unlink(c);
    rmdir(sub); rmdir(root);

    TEST_REPORT();
}
