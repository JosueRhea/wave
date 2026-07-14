/* test_lsp.c — end-to-end LSP client tests against real language servers.
 *
 * Unlike the other suites (pure, headless), this one spawns an actual language
 * server (clangd for C, typescript-language-server for TypeScript) and drives
 * the real JSON-RPC client over its stdio. Each language proves the two things
 * the editor promises with a server running:
 *
 *   1. cross-file go-to-definition — a use in one file resolves to the
 *      definition in *another* file, and
 *   2. real diagnostics — the server's own error report surfaces through the
 *      client.
 *
 * A server that isn't installed is skipped cleanly (no failures) so `make test`
 * still passes without it — the same graceful degradation the editor relies on.
 */
#include "lsp.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* server warm-up + reply latency: poll for up to ~12s. */
#define POLL_TRIES 600
#define POLL_SLEEP_US 20000

/* Is `name` an executable on PATH? (Plain name, no slash.) */
static int have_server(const char *name) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    char buf[2048];
    size_t nlen = strlen(name);
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 1 + nlen + 1 < sizeof buf) {
            memcpy(buf, path, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, name, nlen + 1);
            if (access(buf, X_OK) == 0) return 1;
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

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    b[n] = '\0';
    fclose(f);
    return b;
}

static char *mk_tmpdir(void) {
    char tmpl[] = "/tmp/wave_lsp_XXXXXX";
    char *d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); exit(2); }
    return strdup(d);
}

static void rmrf(const char *dir) {
    char rm[1200];
    snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    if (system(rm) != 0) { /* ignore */ }
}

/* 0-based column of the first `token` occurrence on 0-based `line` of `text`,
 * nudged `inside` chars into the token so the request lands on the identifier. */
static int col_of(const char *text, int line, const char *token, int inside) {
    const char *ls = text;
    for (int k = 0; k < line && ls; k++)
        ls = strchr(ls, '\n') ? strchr(ls, '\n') + 1 : NULL;
    if (!ls) return 0;
    const char *id = strstr(ls, token);
    return id ? (int)(id - ls) + inside : 0;
}

/* Wait for the handshake, returning 1 if the server became ready. */
static int wait_ready(Lsp *l) {
    for (int i = 0; i < POLL_TRIES && !lsp_ready(l); i++) {
        lsp_poll(l);
        usleep(POLL_SLEEP_US);
    }
    return lsp_ready(l);
}

/* Poll until the server publishes a non-empty diagnostic set for `uri`. */
static size_t wait_diagnostics(Lsp *l, const char *uri, LspDiag *out, size_t max,
                               int *published) {
    size_t nd = 0;
    for (int i = 0; i < POLL_TRIES; i++) {
        lsp_poll(l);
        nd = lsp_diagnostics(l, uri, out, max, published);
        if (*published && nd > 0) break;
        usleep(POLL_SLEEP_US);
    }
    return nd;
}

/* Re-issue a completion request until the server returns at least one item
 * (servers occasionally answer with an empty list while still warming up). */
static size_t wait_completions(Lsp *l, const char *uri, int line, int col,
                               LspCompletionItem *out, size_t max) {
    for (int round = 0; round < 40; round++) {
        lsp_completion(l, uri, line, col);
        for (int i = 0; i < 60; i++) {
            lsp_poll(l);
            size_t n = 0;
            if (lsp_take_completions(l, out, max, &n) && n > 0) return n;
            usleep(POLL_SLEEP_US);
        }
        usleep(100000);
    }
    return 0;
}

/* Re-issue a definition request until it lands in a file basenamed `want_base`
 * (servers may first answer from the AST before their index is warm). Fills
 * `*out` with the last result; returns 1 if it ever reached `want_base`. */
static int resolve_definition(Lsp *l, const char *uri, int line, int col,
                              const char *want_base, LspLocation *out, int *got) {
    *got = 0;
    for (int round = 0; round < 40; round++) {
        lsp_definition(l, uri, line, col);
        int answered = 0;
        for (int i = 0; i < 60; i++) {
            lsp_poll(l);
            if (lsp_take_definition(l, out)) { *got = 1; answered = 1; break; }
            usleep(POLL_SLEEP_US);
        }
        if (answered) {
            const char *base = strrchr(out->path, '/');
            base = base ? base + 1 : out->path;
            if (!strcmp(base, want_base)) return 1;
        }
        usleep(100000); /* let the index advance, then retry */
    }
    return 0;
}

/* ===== C / clangd ===== */
static void test_c(void) {
    if (!have_server("clangd")) {
        printf("   clangd not found — skipping C LSP test\n");
        return;
    }
    char *dir = mk_tmpdir();
    char p[1100];
    snprintf(p, sizeof p, "%s/util.h", dir);
    write_file(p, "#ifndef UTIL_H\n#define UTIL_H\nint add_numbers(int a, int b);\n#endif\n");
    snprintf(p, sizeof p, "%s/util.c", dir);
    write_file(p, "#include \"util.h\"\nint add_numbers(int a, int b) {\n    return a + b;\n}\n");
    snprintf(p, sizeof p, "%s/main.c", dir);
    write_file(p, "#include \"util.h\"\n"
                  "int main(void) {\n"
                  "    int r = add_numbers(2, 3);\n"
                  "    int c = add_numb;\n"
                  "    return undeclared_xyz(r);\n"
                  "}\n");
    char cc[2400];
    snprintf(cc, sizeof cc,
             "[\n"
             "  {\"directory\":\"%s\",\"file\":\"%s/main.c\",\"command\":\"cc -c main.c\"},\n"
             "  {\"directory\":\"%s\",\"file\":\"%s/util.c\",\"command\":\"cc -c util.c\"}\n"
             "]\n",
             dir, dir, dir, dir);
    snprintf(p, sizeof p, "%s/compile_commands.json", dir);
    write_file(p, cc);

    char root_uri[1100], main_path[1100], main_uri[1200], util_path[1100], util_uri[1200];
    snprintf(root_uri, sizeof root_uri, "file://%s", dir);
    snprintf(main_path, sizeof main_path, "%s/main.c", dir);
    snprintf(main_uri, sizeof main_uri, "file://%s", main_path);
    snprintf(util_path, sizeof util_path, "%s/util.c", dir);
    snprintf(util_uri, sizeof util_uri, "file://%s", util_path);

    const char *const argv[] = {"clangd", "--background-index", "--log=error", NULL};
    Lsp *l = lsp_start(argv, root_uri);
    CHECK(l != NULL);
    if (!l) { rmrf(dir); free(dir); return; }
    CHECK(wait_ready(l));
    CHECK(lsp_alive(l));

    char *text = slurp(main_path);
    char *util_text = slurp(util_path);
    CHECK(text != NULL);
    lsp_did_open(l, main_uri, "c", text ? text : "");
    /* opening util.c too makes the cross-file definition resolve to its body
     * deterministically rather than waiting on the background-index pass. */
    if (util_text) lsp_did_open(l, util_uri, "c", util_text);

    /* real diagnostics: clangd flags the call to `undeclared_xyz`. */
    int published = 0;
    LspDiag diags[64];
    size_t nd = wait_diagnostics(l, main_uri, diags, 64, &published);
    CHECK(published);
    CHECK(nd > 0);
    int saw = 0;
    for (size_t i = 0; i < nd; i++)
        if (strstr(diags[i].message, "undeclared")) saw = 1;
    CHECK(saw);

    /* cross-file go-to-definition: the add_numbers call resolves to util.c. */
    int call_col = text ? col_of(text, 2, "add_numbers", 2) : 0;
    LspLocation loc;
    int got = 0;
    int crossed = resolve_definition(l, main_uri, 2, call_col, "util.c", &loc, &got);
    CHECK(got);
    CHECK(crossed);                                 /* reached the body in util.c */
    if (got) CHECK(strcmp(loc.path, main_path) != 0); /* genuinely cross-file */

    /* completion: a partial "add_numb" offers the real add_numbers as a
     * candidate — the fallback (tree-sitter/word-scan) sources can't do
     * this since add_numbers is only declared, never spelled out, on this
     * line. */
    int comp_col = text ? col_of(text, 3, "add_numb", 8) : 0;
    LspCompletionItem items[64];
    size_t nc = wait_completions(l, main_uri, 3, comp_col, items, 64);
    CHECK(nc > 0);
    /* the label may carry the full signature ("add_numbers(int a, int b)")
     * for display; insert_text is always the bare name actually inserted. */
    int saw_fn = 0;
    for (size_t i = 0; i < nc; i++)
        if (!strcmp(items[i].insert_text, "add_numbers")) saw_fn = 1;
    CHECK(saw_fn);

    lsp_stop(l);
    free(text);
    free(util_text);
    rmrf(dir);
    free(dir);
}

/* Absolute path of executable `name` on PATH (into `out`); 1 on success. */
static int which_path(const char *name, char *out, size_t cap) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    size_t nlen = strlen(name);
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 1 + nlen + 1 < cap) {
            memcpy(out, path, dlen);
            out[dlen] = '/';
            memcpy(out + dlen + 1, name, nlen + 1);
            if (access(out, X_OK) == 0) return 1;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

/* The bundled server's cli.mjs, relative to the repo root (where `make` runs
 * the tests) or one level down (build/). 1 + path on success. */
static int vendored_ts_cli(char *out, size_t cap) {
    static const char *cand[] = {
        "vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        "../vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        NULL,
    };
    for (int i = 0; cand[i]; i++) {
        snprintf(out, cap, "%s", cand[i]);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

/* ===== TypeScript — the *vendored* server, run through bun/node, exactly as a
 * shipped wave launches it (no global typescript-language-server needed). ===== */
static void test_ts(void) {
    char rt[2048], cli[2048];
    if (!(which_path("bun", rt, sizeof rt) || which_path("node", rt, sizeof rt))) {
        printf("   no bun/node runtime — skipping TS LSP test\n");
        return;
    }
    if (!vendored_ts_cli(cli, sizeof cli)) {
        printf("   vendor/lsp not installed (run `make lsp`) — skipping TS LSP test\n");
        return;
    }
    char *dir = mk_tmpdir();
    char p[1100];
    snprintf(p, sizeof p, "%s/util.ts", dir);
    write_file(p, "export function addNumbers(a: number, b: number): number {\n"
                  "    return a + b;\n"
                  "}\n");
    snprintf(p, sizeof p, "%s/main.ts", dir);
    /* line 2: the call; line 3: a real type error so we can assert diagnostics. */
    write_file(p, "import { addNumbers } from \"./util\";\n"
                  "\n"
                  "const r = addNumbers(2, 3);\n"
                  "const bad: number = \"not a number\";\n"
                  "const partial = addNum;\n"
                  "console.log(r, bad, partial);\n");
    snprintf(p, sizeof p, "%s/tsconfig.json", dir);
    write_file(p, "{ \"compilerOptions\": { \"strict\": true, \"module\": \"commonjs\", "
                  "\"target\": \"es2020\" } }\n");

    char root_uri[1100], main_path[1100], main_uri[1200], util_path[1100], util_uri[1200];
    snprintf(root_uri, sizeof root_uri, "file://%s", dir);
    snprintf(main_path, sizeof main_path, "%s/main.ts", dir);
    snprintf(main_uri, sizeof main_uri, "file://%s", main_path);
    snprintf(util_path, sizeof util_path, "%s/util.ts", dir);
    snprintf(util_uri, sizeof util_uri, "file://%s", util_path);

    const char *const argv[] = {rt, cli, "--stdio", NULL};
    Lsp *l = lsp_start(argv, root_uri);
    CHECK(l != NULL);
    if (!l) { rmrf(dir); free(dir); return; }
    CHECK(wait_ready(l));
    CHECK(lsp_alive(l));

    char *text = slurp(main_path);
    char *util_text = slurp(util_path);
    CHECK(text != NULL);
    lsp_did_open(l, main_uri, "typescript", text ? text : "");
    if (util_text) lsp_did_open(l, util_uri, "typescript", util_text);

    /* real diagnostics: the string-to-number assignment on line 3. */
    int published = 0;
    LspDiag diags[64];
    size_t nd = wait_diagnostics(l, main_uri, diags, 64, &published);
    CHECK(published);
    CHECK(nd > 0);
    int saw = 0;
    for (size_t i = 0; i < nd; i++)
        if (strstr(diags[i].message, "not assignable")) saw = 1;
    CHECK(saw);

    /* cross-file go-to-definition: the addNumbers call resolves to util.ts. */
    int call_col = text ? col_of(text, 2, "addNumbers", 3) : 0;
    LspLocation loc;
    int got = 0;
    int crossed = resolve_definition(l, main_uri, 2, call_col, "util.ts", &loc, &got);
    CHECK(got);
    CHECK(crossed);                                 /* reached the definition in util.ts */
    if (got) CHECK(strcmp(loc.path, main_path) != 0); /* genuinely cross-file */

    /* completion: a partial "addNum" offers the real addNumbers. */
    int comp_col = text ? col_of(text, 4, "addNum", 6) : 0;
    LspCompletionItem items[64];
    size_t nc = wait_completions(l, main_uri, 4, comp_col, items, 64);
    CHECK(nc > 0);
    int saw_fn = 0;
    for (size_t i = 0; i < nc; i++)
        if (!strcmp(items[i].insert_text, "addNumbers")) saw_fn = 1;
    CHECK(saw_fn);

    lsp_stop(l);
    free(text);
    free(util_text);
    rmrf(dir);
    free(dir);
}

int main(void) {
    test_c();
    test_ts();
    TEST_REPORT();
}
