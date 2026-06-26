/* search.c — async ripgrep-backed content search (see search.h). */
#include "search.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SEARCH_MAX_HITS 1000 /* cap the result set; ripgrep is killed past this */

struct Search {
    char rg[1024];
    char root[1024];
    char pattern[256];

    pid_t pid;   /* >0 while a child is running */
    int out_fd;  /* read end of the child's stdout, or -1 */
    int running;
    int truncated;

    char *buf; /* incomplete trailing line accumulates here */
    size_t blen, bcap;

    SearchHit *hits;
    size_t nhits, cap_hits;
};

Search *search_new(const char *rg_path, const char *root) {
    Search *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    rg_path = rg_path ? rg_path : "rg";
    /* The child chdir()s into the search root before exec, so a relative rg
     * path (e.g. "vendor/rg/rg") would no longer resolve. Make it absolute up
     * front. A bare name with no slash is left alone for PATH lookup. */
    if (strchr(rg_path, '/') && rg_path[0] != '/') {
        char abs[1024];
        if (realpath(rg_path, abs)) rg_path = abs;
        snprintf(s->rg, sizeof s->rg, "%s", rg_path);
    } else {
        snprintf(s->rg, sizeof s->rg, "%s", rg_path);
    }
    snprintf(s->root, sizeof s->root, "%s", root ? root : ".");
    s->out_fd = -1;
    return s;
}

/* Stop the running child (if any) and release its pipe. Keeps results. */
static void search_stop(Search *s) {
    if (s->pid > 0) {
        kill(s->pid, SIGTERM);
        waitpid(s->pid, NULL, 0);
        s->pid = 0;
    }
    if (s->out_fd >= 0) {
        close(s->out_fd);
        s->out_fd = -1;
    }
    s->running = 0;
    s->blen = 0;
}

void search_free(Search *s) {
    if (!s) return;
    search_stop(s);
    free(s->buf);
    free(s->hits);
    free(s);
}

void search_query(Search *s, const char *pattern) {
    if (!s) return;
    search_stop(s);
    s->nhits = 0;
    s->truncated = 0;
    snprintf(s->pattern, sizeof s->pattern, "%s", pattern ? pattern : "");
    if (s->pattern[0] == '\0') return; /* nothing to search */

    static int sigpipe_off = 0;
    if (!sigpipe_off) { signal(SIGPIPE, SIG_IGN); sigpipe_off = 1; }

    int out[2];
    if (pipe(out) != 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(out[0]); close(out[1]); return; }
    if (pid == 0) { /* child: ripgrep, stdout → pipe, cwd = search root */
        dup2(out[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(out[0]);
        close(out[1]);
        if (chdir(s->root) != 0) _exit(127);
        /* --column gives byte columns; --smart-case = case-insensitive unless
         * the pattern has an uppercase letter; -F = fixed string (literal). */
        const char *argv[] = {
            s->rg, "--line-number", "--column", "--no-heading",
            "--color=never", "--smart-case", "-F", "--", s->pattern, ".", NULL,
        };
        execvp(s->rg, (char *const *)argv);
        _exit(127);
    }

    close(out[1]);
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    s->pid = pid;
    s->out_fd = out[0];
    s->running = 1;
}

/* Parse one `./path:line:col:text` line into a hit. Returns 1 on success. */
static int parse_line(Search *s, char *line) {
    /* path: everything up to the first ':' that is followed by digits ':' */
    char *p = line;
    char *c1 = strchr(p, ':');
    if (!c1) return 0;
    char *c2 = strchr(c1 + 1, ':');
    if (!c2) return 0;
    char *c3 = strchr(c2 + 1, ':');
    if (!c3) return 0;

    *c1 = *c2 = *c3 = '\0';
    const char *path = p;
    if (path[0] == '.' && path[1] == '/') path += 2; /* strip the "./" prefix */
    int ln = atoi(c1 + 1);
    int col = atoi(c2 + 1);
    const char *text = c3 + 1;
    if (ln <= 0) return 0;

    if (s->nhits == s->cap_hits) {
        size_t nc = s->cap_hits ? s->cap_hits * 2 : 128;
        SearchHit *nh = realloc(s->hits, nc * sizeof *nh);
        if (!nh) return 0;
        s->hits = nh;
        s->cap_hits = nc;
    }
    SearchHit *h = &s->hits[s->nhits++];
    snprintf(h->path, sizeof h->path, "%s", path);
    h->line = ln;
    h->col = col;
    /* trim leading whitespace so deeply-indented matches read cleanly */
    while (*text == ' ' || *text == '\t') text++;
    snprintf(h->text, sizeof h->text, "%s", text);
    return 1;
}

void search_poll(Search *s) {
    if (!s || !s->running || s->out_fd < 0) return;

    char tmp[4096];
    for (;;) {
        ssize_t r = read(s->out_fd, tmp, sizeof tmp);
        if (r > 0) {
            if (s->blen + (size_t)r + 1 > s->bcap) {
                size_t nc = s->bcap ? s->bcap * 2 : 8192;
                while (nc < s->blen + (size_t)r + 1) nc *= 2;
                char *nb = realloc(s->buf, nc);
                if (!nb) break;
                s->buf = nb;
                s->bcap = nc;
            }
            memcpy(s->buf + s->blen, tmp, (size_t)r);
            s->blen += (size_t)r;

            /* consume complete lines */
            size_t start = 0;
            for (size_t i = 0; i < s->blen; i++) {
                if (s->buf[i] != '\n') continue;
                s->buf[i] = '\0';
                if (s->nhits < SEARCH_MAX_HITS)
                    parse_line(s, s->buf + start);
                start = i + 1;
            }
            if (start > 0) {
                memmove(s->buf, s->buf + start, s->blen - start);
                s->blen -= start;
            }
            if (s->nhits >= SEARCH_MAX_HITS) { /* enough — stop ripgrep */
                s->truncated = 1;
                search_stop(s);
                return;
            }
            continue;
        }
        if (r == 0) { /* EOF: ripgrep finished */
            search_stop(s);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return; /* nothing more yet */
        if (errno == EINTR) continue;
        search_stop(s); /* read error */
        return;
    }
}

int search_running(const Search *s) { return s && s->running; }
size_t search_count(const Search *s) { return s ? s->nhits : 0; }
const SearchHit *search_hit(const Search *s, size_t i) {
    return (s && i < s->nhits) ? &s->hits[i] : NULL;
}
const char *search_pattern(const Search *s) { return s ? s->pattern : ""; }
int search_truncated(const Search *s) { return s && s->truncated; }
