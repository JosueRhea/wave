/* lsp.c — see lsp.h.
 *
 * Three parts: (1) a tiny growable string builder + JSON value parser, enough
 * to compose requests and read replies; (2) child-process plumbing (fork/exec
 * with non-blocking stdio pipes and Content-Length framing); (3) the protocol
 * itself — initialize handshake, document sync, and definition/hover/diagnostic
 * handling.
 */
#include "lsp.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ===== string builder ===== */
typedef struct { char *p; size_t n, cap; } Sbuf;

static void sb_ensure(Sbuf *s, size_t add) {
    if (s->n + add + 1 > s->cap) {
        s->cap = (s->n + add + 1) * 2;
        s->p = realloc(s->p, s->cap);
    }
}
static void sb_raw(Sbuf *s, const char *d, size_t n) {
    sb_ensure(s, n);
    memcpy(s->p + s->n, d, n);
    s->n += n;
    s->p[s->n] = '\0';
}
static void sb_str(Sbuf *s, const char *d) { sb_raw(s, d, strlen(d)); }
static void sb_json_str(Sbuf *s, const char *d) {
    sb_raw(s, "\"", 1);
    for (; *d; d++) {
        unsigned char c = (unsigned char)*d;
        switch (c) {
        case '"': sb_str(s, "\\\""); break;
        case '\\': sb_str(s, "\\\\"); break;
        case '\n': sb_str(s, "\\n"); break;
        case '\r': sb_str(s, "\\r"); break;
        case '\t': sb_str(s, "\\t"); break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); sb_str(s, b); }
            else { char cc = (char)c; sb_raw(s, &cc, 1); }
        }
    }
    sb_raw(s, "\"", 1);
}

/* ===== JSON parser ===== */
enum { JNULL, JBOOL, JNUM, JSTR, JARR, JOBJ };
typedef struct JVal JVal;
struct JVal {
    int kind;
    double num;
    int boolean;
    char *str;       /* JSTR (decoded, owned) */
    JVal **items;    /* JARR / JOBJ values */
    char **keys;     /* JOBJ keys */
    int count;
};

typedef struct { const char *p, *end; } JP;

static void jskip(JP *j) {
    while (j->p < j->end && isspace((unsigned char)*j->p)) j->p++;
}
static JVal *jparse(JP *j);

static void jpush_utf8(Sbuf *s, unsigned int cp) {
    char b[4];
    int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0|(cp>>6)); b[1] = (char)(0x80|(cp&0x3F)); n = 2; }
    else { b[0] = (char)(0xE0|(cp>>12)); b[1] = (char)(0x80|((cp>>6)&0x3F)); b[2] = (char)(0x80|(cp&0x3F)); n = 3; }
    sb_raw(s, b, (size_t)n);
}

static char *jparse_str_raw(JP *j) {
    if (*j->p != '"') return NULL;
    j->p++;
    Sbuf s = {0};
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char e = *j->p++;
            switch (e) {
            case 'n': sb_str(&s, "\n"); break;
            case 't': sb_str(&s, "\t"); break;
            case 'r': sb_str(&s, "\r"); break;
            case 'b': sb_str(&s, "\b"); break;
            case 'f': sb_str(&s, "\f"); break;
            case '/': sb_str(&s, "/"); break;
            case '"': sb_str(&s, "\""); break;
            case '\\': sb_str(&s, "\\"); break;
            case 'u': {
                unsigned int cp = 0;
                for (int i = 0; i < 4 && j->p < j->end; i++) {
                    char h = *j->p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                /* surrogate pair */
                if (cp >= 0xD800 && cp <= 0xDBFF && j->p + 6 <= j->end &&
                    j->p[0] == '\\' && j->p[1] == 'u') {
                    j->p += 2;
                    unsigned int lo = 0;
                    for (int i = 0; i < 4 && j->p < j->end; i++) {
                        char h = *j->p++;
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    char b[4]; int n;
                    b[0]=(char)(0xF0|(cp>>18)); b[1]=(char)(0x80|((cp>>12)&0x3F));
                    b[2]=(char)(0x80|((cp>>6)&0x3F)); b[3]=(char)(0x80|(cp&0x3F)); n=4;
                    sb_raw(&s, b, (size_t)n);
                } else {
                    jpush_utf8(&s, cp);
                }
                break;
            }
            default: { char cc = e; sb_raw(&s, &cc, 1); }
            }
        } else {
            sb_raw(&s, &c, 1);
        }
    }
    if (j->p < j->end && *j->p == '"') j->p++;
    if (!s.p) { s.p = malloc(1); s.p[0] = '\0'; }
    return s.p;
}

static JVal *jnew(int kind) { JVal *v = calloc(1, sizeof *v); v->kind = kind; return v; }

static JVal *jparse(JP *j) {
    jskip(j);
    if (j->p >= j->end) return NULL;
    char c = *j->p;
    if (c == '"') {
        JVal *v = jnew(JSTR);
        v->str = jparse_str_raw(j);
        return v;
    }
    if (c == '{') {
        j->p++;
        JVal *v = jnew(JOBJ);
        jskip(j);
        if (j->p < j->end && *j->p == '}') { j->p++; return v; }
        for (;;) {
            jskip(j);
            char *key = jparse_str_raw(j);
            jskip(j);
            if (j->p < j->end && *j->p == ':') j->p++;
            JVal *val = jparse(j);
            v->items = realloc(v->items, (size_t)(v->count + 1) * sizeof *v->items);
            v->keys = realloc(v->keys, (size_t)(v->count + 1) * sizeof *v->keys);
            v->keys[v->count] = key;
            v->items[v->count] = val;
            v->count++;
            jskip(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            break;
        }
        jskip(j);
        if (j->p < j->end && *j->p == '}') j->p++;
        return v;
    }
    if (c == '[') {
        j->p++;
        JVal *v = jnew(JARR);
        jskip(j);
        if (j->p < j->end && *j->p == ']') { j->p++; return v; }
        for (;;) {
            JVal *val = jparse(j);
            v->items = realloc(v->items, (size_t)(v->count + 1) * sizeof *v->items);
            v->items[v->count++] = val;
            jskip(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            break;
        }
        jskip(j);
        if (j->p < j->end && *j->p == ']') j->p++;
        return v;
    }
    if (c == 't' || c == 'f') {
        JVal *v = jnew(JBOOL);
        v->boolean = (c == 't');
        while (j->p < j->end && isalpha((unsigned char)*j->p)) j->p++;
        return v;
    }
    if (c == 'n') {
        while (j->p < j->end && isalpha((unsigned char)*j->p)) j->p++;
        return jnew(JNULL);
    }
    /* number */
    char *endp = NULL;
    double d = strtod(j->p, &endp);
    JVal *v = jnew(JNUM);
    v->num = d;
    if (endp && endp <= j->end) j->p = endp;
    else j->p = j->end;
    return v;
}

static void jfree(JVal *v) {
    if (!v) return;
    free(v->str);
    for (int i = 0; i < v->count; i++) {
        jfree(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

static JVal *jget(JVal *o, const char *key) {
    if (!o || o->kind != JOBJ) return NULL;
    for (int i = 0; i < o->count; i++)
        if (o->keys[i] && !strcmp(o->keys[i], key)) return o->items[i];
    return NULL;
}
static JVal *jidx(JVal *a, int i) {
    if (!a || a->kind != JARR || i < 0 || i >= a->count) return NULL;
    return a->items[i];
}
static const char *jstr(JVal *v) { return (v && v->kind == JSTR) ? v->str : NULL; }
static int jint(JVal *v) { return (v && v->kind == JNUM) ? (int)v->num : 0; }

/* ===== client state ===== */
typedef struct { long id; int kind; } Pending; /* kind: 1=def 2=hover */
typedef struct { char *uri, *lang, *text; } OpenReq;
typedef struct { char uri[1024]; LspDiag *d; size_t n; } DiagSet;

struct Lsp {
    pid_t pid;
    int in_fd;   /* write to server stdin */
    int out_fd;  /* read from server stdout (non-blocking) */
    int alive;
    int ready;
    long next_id;
    long init_id;

    char *rbuf;  /* unparsed bytes from the server */
    size_t rlen, rcap;

    /* Outbound queue: requests are appended here and drained with non-blocking
     * writes by lsp_flush_out() (called every poll). The write fd is itself
     * non-blocking, so a slow server can never stall the UI thread — critical
     * because full-document didChange resends the whole file on every edit. */
    char *wbuf;
    size_t wlen, wcap, woff;

    Pending *pend;
    int npend, cap_pend;

    OpenReq *opens; /* queued until ready */
    int nopens;

    DiagSet *diags;
    size_t ndiags;

    LspLocation def; int def_ready;
    char hover[1024]; int hover_ready;
};

/* ----- low-level io ----- */

/* Drain as much of the outbound queue as the pipe will accept right now,
 * without blocking. EAGAIN just means "try again next poll". */
static void lsp_flush_out(Lsp *l) {
    if (!l->alive) return;
    while (l->woff < l->wlen) {
        ssize_t w = write(l->in_fd, l->wbuf + l->woff, l->wlen - l->woff);
        if (w > 0) { l->woff += (size_t)w; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return; /* later */
        if (w < 0 && errno == EINTR) continue;
        l->alive = 0; /* EPIPE etc. — server gone */
        return;
    }
    if (l->woff == l->wlen) l->woff = l->wlen = 0; /* fully drained: reset */
}

/* Append a framed message to the outbound queue, then try to flush. Never
 * blocks: anything the pipe can't take yet stays queued for the next poll. */
static void lsp_write(Lsp *l, const char *body, size_t len) {
    if (!l->alive) return;
    char hdr[64];
    int hn = snprintf(hdr, sizeof hdr, "Content-Length: %zu\r\n\r\n", len);

    /* compact consumed bytes off the front before measuring free space */
    if (l->woff > 0) {
        memmove(l->wbuf, l->wbuf + l->woff, l->wlen - l->woff);
        l->wlen -= l->woff;
        l->woff = 0;
    }
    size_t need = l->wlen + (size_t)hn + len;
    if (need > l->wcap) {
        size_t cap = l->wcap ? l->wcap : 8192;
        while (cap < need) cap *= 2;
        l->wbuf = realloc(l->wbuf, cap);
        l->wcap = cap;
    }
    memcpy(l->wbuf + l->wlen, hdr, (size_t)hn);
    l->wlen += (size_t)hn;
    memcpy(l->wbuf + l->wlen, body, len);
    l->wlen += len;

    lsp_flush_out(l);
}

static long lsp_send_request(Lsp *l, const char *method, Sbuf *params) {
    long id = l->next_id++;
    Sbuf s = {0};
    sb_str(&s, "{\"jsonrpc\":\"2.0\",\"id\":");
    char idb[32]; snprintf(idb, sizeof idb, "%ld", id); sb_str(&s, idb);
    sb_str(&s, ",\"method\":");
    sb_json_str(&s, method);
    sb_str(&s, ",\"params\":");
    sb_raw(&s, params->p ? params->p : "{}", params->p ? params->n : 2);
    sb_str(&s, "}");
    lsp_write(l, s.p, s.n);
    free(s.p);
    return id;
}
static void lsp_send_notify(Lsp *l, const char *method, Sbuf *params) {
    Sbuf s = {0};
    sb_str(&s, "{\"jsonrpc\":\"2.0\",\"method\":");
    sb_json_str(&s, method);
    sb_str(&s, ",\"params\":");
    sb_raw(&s, params->p ? params->p : "{}", params->p ? params->n : 2);
    sb_str(&s, "}");
    lsp_write(l, s.p, s.n);
    free(s.p);
}

static void pend_add(Lsp *l, long id, int kind) {
    if (l->npend == l->cap_pend) {
        l->cap_pend = l->cap_pend ? l->cap_pend * 2 : 16;
        l->pend = realloc(l->pend, (size_t)l->cap_pend * sizeof *l->pend);
    }
    l->pend[l->npend++] = (Pending){id, kind};
}
static int pend_take(Lsp *l, long id) {
    for (int i = 0; i < l->npend; i++)
        if (l->pend[i].id == id) {
            int kind = l->pend[i].kind;
            l->pend[i] = l->pend[--l->npend];
            return kind;
        }
    return 0;
}

/* ----- PATH lookup so a missing binary fails fast (clean fallback) ----- */
static int which_exists(const char *name) {
    if (strchr(name, '/')) return access(name, X_OK) == 0;
    const char *path = getenv("PATH");
    if (!path) return 0;
    char buf[2048];
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 1 + strlen(name) + 1 < sizeof buf) {
            memcpy(buf, path, dlen);
            buf[dlen] = '/';
            strcpy(buf + dlen + 1, name);
            if (access(buf, X_OK) == 0) return 1;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

/* ----- initialize ----- */
static void send_initialize(Lsp *l, const char *root_uri) {
    Sbuf p = {0};
    sb_str(&p, "{\"processId\":");
    char pidb[32]; snprintf(pidb, sizeof pidb, "%ld", (long)getpid()); sb_str(&p, pidb);
    sb_str(&p, ",\"rootUri\":");
    if (root_uri) sb_json_str(&p, root_uri); else sb_str(&p, "null");
    sb_str(&p,
        ",\"capabilities\":{\"textDocument\":{"
        "\"synchronization\":{\"dynamicRegistration\":false},"
        "\"definition\":{\"dynamicRegistration\":false},"
        "\"hover\":{\"dynamicRegistration\":false,\"contentFormat\":[\"plaintext\",\"markdown\"]},"
        "\"publishDiagnostics\":{}}},"
        "\"workspaceFolders\":null}");
    l->init_id = lsp_send_request(l, "initialize", &p);
    free(p.p);
}

Lsp *lsp_start(const char *const argv[], const char *root_uri) {
    if (!argv || !argv[0] || !which_exists(argv[0])) return NULL;

    static int sigpipe_off = 0;
    if (!sigpipe_off) { signal(SIGPIPE, SIG_IGN); sigpipe_off = 1; }

    int inpipe[2], outpipe[2];
    if (pipe(inpipe) != 0) return NULL;
    if (pipe(outpipe) != 0) { close(inpipe[0]); close(inpipe[1]); return NULL; }

    pid_t pid = fork();
    if (pid < 0) {
        close(inpipe[0]); close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        return NULL;
    }
    if (pid == 0) { /* child */
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(inpipe[0]); close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    /* parent */
    close(inpipe[0]);
    close(outpipe[1]);

    Lsp *l = calloc(1, sizeof *l);
    l->pid = pid;
    l->in_fd = inpipe[1];
    l->out_fd = outpipe[0];
    l->alive = 1;
    l->next_id = 1;
    fcntl(l->out_fd, F_SETFL, O_NONBLOCK);
    fcntl(l->in_fd, F_SETFL, O_NONBLOCK); /* writes queue, never block the UI */

    send_initialize(l, root_uri);
    return l;
}

void lsp_stop(Lsp *l) {
    if (!l) return;
    if (l->alive) {
        Sbuf empty = {0};
        lsp_send_request(l, "shutdown", &empty);
        lsp_send_notify(l, "exit", &empty);
    }
    close(l->in_fd);
    close(l->out_fd);
    if (l->pid > 0) { kill(l->pid, SIGTERM); waitpid(l->pid, NULL, WNOHANG); }
    for (int i = 0; i < l->nopens; i++) {
        free(l->opens[i].uri); free(l->opens[i].lang); free(l->opens[i].text);
    }
    free(l->opens);
    for (size_t i = 0; i < l->ndiags; i++) free(l->diags[i].d);
    free(l->diags);
    free(l->pend);
    free(l->rbuf);
    free(l->wbuf);
    free(l);
}

int lsp_ready(const Lsp *l) { return l && l->ready; }
int lsp_alive(const Lsp *l) { return l && l->alive; }

/* ----- document sync ----- */
static void send_did_open(Lsp *l, const char *uri, const char *lang, const char *text) {
    Sbuf p = {0};
    sb_str(&p, "{\"textDocument\":{\"uri\":");
    sb_json_str(&p, uri);
    sb_str(&p, ",\"languageId\":");
    sb_json_str(&p, lang);
    sb_str(&p, ",\"version\":1,\"text\":");
    sb_json_str(&p, text);
    sb_str(&p, "}}");
    lsp_send_notify(l, "textDocument/didOpen", &p);
    free(p.p);
}

void lsp_did_open(Lsp *l, const char *uri, const char *language_id, const char *text) {
    if (!l) return;
    if (!l->ready) { /* queue until the handshake completes */
        l->opens = realloc(l->opens, (size_t)(l->nopens + 1) * sizeof *l->opens);
        l->opens[l->nopens].uri = strdup(uri);
        l->opens[l->nopens].lang = strdup(language_id);
        l->opens[l->nopens].text = strdup(text);
        l->nopens++;
        return;
    }
    send_did_open(l, uri, language_id, text);
}

void lsp_did_change(Lsp *l, const char *uri, int version, const char *text) {
    if (!l || !l->ready) return;
    Sbuf p = {0};
    sb_str(&p, "{\"textDocument\":{\"uri\":");
    sb_json_str(&p, uri);
    char vb[32]; snprintf(vb, sizeof vb, ",\"version\":%d}", version);
    sb_str(&p, vb);
    sb_str(&p, ",\"contentChanges\":[{\"text\":");
    sb_json_str(&p, text);
    sb_str(&p, "}]}");
    lsp_send_notify(l, "textDocument/didChange", &p);
    free(p.p);
}

static void send_position_request(Lsp *l, const char *method, const char *uri,
                                  int line, int col, int kind) {
    if (!l || !l->ready) return;
    Sbuf p = {0};
    sb_str(&p, "{\"textDocument\":{\"uri\":");
    sb_json_str(&p, uri);
    char pos[64];
    snprintf(pos, sizeof pos, "},\"position\":{\"line\":%d,\"character\":%d}}", line, col);
    sb_str(&p, pos);
    long id = lsp_send_request(l, method, &p);
    pend_add(l, id, kind);
    free(p.p);
}
void lsp_definition(Lsp *l, const char *uri, int line, int col) {
    send_position_request(l, "textDocument/definition", uri, line, col, 1);
}
void lsp_hover(Lsp *l, const char *uri, int line, int col) {
    send_position_request(l, "textDocument/hover", uri, line, col, 2);
}

/* ----- uri <-> path ----- */
static void uri_to_path(const char *uri, char *out, size_t cap) {
    const char *p = uri;
    if (!strncmp(p, "file://", 7)) {
        p += 7;
        /* skip an optional host component: file://host/path */
        if (*p && *p != '/') { const char *slash = strchr(p, '/'); if (slash) p = slash; }
    }
    size_t o = 0;
    while (*p && o + 1 < cap) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = p[1], lo = p[2];
            int h = (hi>='0'&&hi<='9')?hi-'0':(tolower(hi)-'a'+10);
            int lw = (lo>='0'&&lo<='9')?lo-'0':(tolower(lo)-'a'+10);
            out[o++] = (char)((h << 4) | lw);
            p += 3;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

/* ----- result handling ----- */
static int parse_location(JVal *loc, LspLocation *out) {
    if (!loc || loc->kind != JOBJ) return 0;
    JVal *uri = jget(loc, "uri");
    JVal *range = jget(loc, "range");
    if (!uri) { /* LocationLink */
        uri = jget(loc, "targetUri");
        range = jget(loc, "targetSelectionRange");
        if (!range) range = jget(loc, "targetRange");
    }
    if (!uri || !jstr(uri) || !range) return 0;
    JVal *start = jget(range, "start");
    out->line = jint(jget(start, "line"));
    out->col = jint(jget(start, "character"));
    uri_to_path(jstr(uri), out->path, sizeof out->path);
    return out->path[0] != '\0';
}

static void handle_definition(Lsp *l, JVal *result) {
    if (!result) return;
    LspLocation loc;
    int ok = 0;
    if (result->kind == JARR) { if (result->count > 0) ok = parse_location(jidx(result, 0), &loc); }
    else if (result->kind == JOBJ) ok = parse_location(result, &loc);
    if (ok) { l->def = loc; l->def_ready = 1; }
}

static void extract_hover_text(JVal *contents, char *out, size_t cap) {
    out[0] = '\0';
    if (!contents) return;
    const char *s = NULL;
    if (contents->kind == JSTR) s = jstr(contents);
    else if (contents->kind == JOBJ) {
        JVal *val = jget(contents, "value");
        s = jstr(val);
    } else if (contents->kind == JARR && contents->count > 0) {
        JVal *first = jidx(contents, 0);
        if (first->kind == JSTR) s = jstr(first);
        else s = jstr(jget(first, "value"));
    }
    if (!s) return;
    /* flatten to a single status-bar line */
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        char c = (*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s;
        if (c == ' ' && o > 0 && out[o-1] == ' ') continue; /* squeeze spaces */
        out[o++] = c;
    }
    while (o > 0 && out[o-1] == ' ') o--;
    out[o] = '\0';
}

static void store_diagnostics(Lsp *l, const char *uri, JVal *arr) {
    DiagSet *set = NULL;
    for (size_t i = 0; i < l->ndiags; i++)
        if (!strcmp(l->diags[i].uri, uri)) { set = &l->diags[i]; break; }
    if (!set) {
        l->diags = realloc(l->diags, (l->ndiags + 1) * sizeof *l->diags);
        set = &l->diags[l->ndiags++];
        memset(set, 0, sizeof *set);
        snprintf(set->uri, sizeof set->uri, "%s", uri);
    }
    free(set->d);
    set->d = NULL;
    set->n = 0;
    if (!arr || arr->kind != JARR) return;
    set->d = calloc((size_t)arr->count ? (size_t)arr->count : 1, sizeof *set->d);
    for (int i = 0; i < arr->count; i++) {
        JVal *dg = jidx(arr, i);
        JVal *range = jget(dg, "range");
        JVal *start = jget(range, "start");
        JVal *end = jget(range, "end");
        LspDiag *d = &set->d[set->n];
        d->start_line = jint(jget(start, "line"));
        d->start_col = jint(jget(start, "character"));
        d->end_line = jint(jget(end, "line"));
        d->end_col = jint(jget(end, "character"));
        JVal *sev = jget(dg, "severity");
        d->severity = sev ? jint(sev) : 1;
        const char *m = jstr(jget(dg, "message"));
        snprintf(d->message, sizeof d->message, "%s", m ? m : "");
        set->n++;
    }
}

static void dispatch(Lsp *l, JVal *msg) {
    if (!msg || msg->kind != JOBJ) return;
    JVal *id = jget(msg, "id");
    JVal *method = jget(msg, "method");

    if (method) { /* notification or server->client request */
        const char *m = jstr(method);
        if (m && !strcmp(m, "textDocument/publishDiagnostics")) {
            JVal *params = jget(msg, "params");
            const char *uri = jstr(jget(params, "uri"));
            if (uri) store_diagnostics(l, uri, jget(params, "diagnostics"));
        }
        /* server-initiated requests (with an id) get a benign empty reply */
        if (id && id->kind == JNUM) {
            Sbuf s = {0};
            char idb[32]; snprintf(idb, sizeof idb, "%ld", (long)id->num);
            sb_str(&s, "{\"jsonrpc\":\"2.0\",\"id\":");
            sb_str(&s, idb);
            sb_str(&s, ",\"result\":null}");
            lsp_write(l, s.p, s.n);
            free(s.p);
        }
        return;
    }

    if (id && id->kind == JNUM) { /* response to one of our requests */
        long rid = (long)id->num;
        JVal *result = jget(msg, "result");
        if (rid == l->init_id && !l->ready) {
            l->ready = 1;
            Sbuf empty = {0};
            lsp_send_notify(l, "initialized", &empty);
            for (int i = 0; i < l->nopens; i++) {
                send_did_open(l, l->opens[i].uri, l->opens[i].lang, l->opens[i].text);
                free(l->opens[i].uri); free(l->opens[i].lang); free(l->opens[i].text);
            }
            free(l->opens); l->opens = NULL; l->nopens = 0;
            return;
        }
        int kind = pend_take(l, rid);
        if (kind == 1) handle_definition(l, result);
        else if (kind == 2) { extract_hover_text(jget(msg, "result") ? jget(jget(msg,"result"),"contents") : NULL,
                                                  l->hover, sizeof l->hover);
                              if (l->hover[0]) l->hover_ready = 1; }
    }
}

/* ----- read pump ----- */
void lsp_poll(Lsp *l) {
    if (!l || !l->alive) return;
    lsp_flush_out(l); /* keep the outbound queue moving */
    char tmp[8192];
    for (;;) {
        ssize_t r = read(l->out_fd, tmp, sizeof tmp);
        if (r > 0) {
            if (l->rlen + (size_t)r + 1 > l->rcap) {
                l->rcap = (l->rlen + (size_t)r + 1) * 2;
                l->rbuf = realloc(l->rbuf, l->rcap);
            }
            memcpy(l->rbuf + l->rlen, tmp, (size_t)r);
            l->rlen += (size_t)r;
            l->rbuf[l->rlen] = '\0';
            continue;
        }
        if (r == 0) { /* EOF: server exited */
            l->alive = 0;
            waitpid(l->pid, NULL, WNOHANG);
        }
        break; /* r<0 (EAGAIN) or EOF */
    }

    /* frame and dispatch every complete message in the buffer */
    size_t off = 0;
    for (;;) {
        char *hdr_end = NULL;
        for (size_t i = off; i + 3 < l->rlen; i++)
            if (!memcmp(l->rbuf + i, "\r\n\r\n", 4)) { hdr_end = l->rbuf + i + 4; break; }
        if (!hdr_end) break;

        /* find Content-Length within [off, hdr_end) */
        size_t clen = 0;
        char *cl = NULL;
        for (size_t i = off; i + 15 < (size_t)(hdr_end - l->rbuf); i++)
            if (!strncasecmp(l->rbuf + i, "Content-Length:", 15)) { cl = l->rbuf + i + 15; break; }
        if (cl) clen = strtoul(cl, NULL, 10);

        size_t body_off = (size_t)(hdr_end - l->rbuf);
        if (l->rlen - body_off < clen) break; /* body not fully arrived yet */

        JP jp = { l->rbuf + body_off, l->rbuf + body_off + clen };
        JVal *msg = jparse(&jp);
        dispatch(l, msg);
        jfree(msg);

        off = body_off + clen;
    }
    if (off > 0) {
        memmove(l->rbuf, l->rbuf + off, l->rlen - off);
        l->rlen -= off;
        if (l->rbuf) l->rbuf[l->rlen] = '\0';
    }
}

int lsp_take_definition(Lsp *l, LspLocation *out) {
    if (!l || !l->def_ready) return 0;
    *out = l->def;
    l->def_ready = 0;
    return 1;
}
int lsp_take_hover(Lsp *l, char *buf, size_t cap) {
    if (!l || !l->hover_ready) return 0;
    snprintf(buf, cap, "%s", l->hover);
    l->hover_ready = 0;
    return 1;
}

size_t lsp_diagnostics(Lsp *l, const char *uri, LspDiag *out, size_t max,
                       int *published) {
    if (published) *published = 0;
    if (!l) return 0;
    for (size_t i = 0; i < l->ndiags; i++)
        if (!strcmp(l->diags[i].uri, uri)) {
            if (published) *published = 1;
            size_t n = l->diags[i].n < max ? l->diags[i].n : max;
            for (size_t k = 0; k < n; k++) out[k] = l->diags[i].d[k];
            return l->diags[i].n;
        }
    return 0;
}
