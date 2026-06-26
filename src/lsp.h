/* lsp.h — a small Language Server Protocol client.
 *
 * Spawns a language server (clangd, typescript-language-server, …) as a child
 * process and speaks JSON-RPC over its stdio pipes. The client is asynchronous
 * and non-blocking: requests are fired off, and replies are picked up later by
 * lsp_poll() + the lsp_take_* accessors, so the UI never stalls on the server.
 *
 * Everything degrades gracefully: if the server binary isn't installed,
 * lsp_start() returns NULL and the editor simply runs without LSP (its
 * tree-sitter heuristics still provide go-to-definition / info / diagnostics).
 */
#ifndef WAVE_LSP_H
#define WAVE_LSP_H

#include <stddef.h>

typedef struct Lsp Lsp;

/* A resolved definition target. line/col are 0-based (LSP coordinates). */
typedef struct {
    char path[1024];
    int line, col;
} LspLocation;

/* A diagnostic published by the server. Positions are 0-based. */
typedef struct {
    int start_line, start_col, end_line, end_col;
    int severity; /* 1=error 2=warning 3=info 4=hint */
    char message[256];
} LspDiag;

/* Start a server. `argv` is NULL-terminated (argv[0] is the binary, looked up
 * on PATH). `root_uri` is a file:// URI for the workspace root. Returns NULL if
 * the binary can't be found or the child can't be spawned. */
Lsp *lsp_start(const char *const argv[], const char *root_uri);
void lsp_stop(Lsp *l);

/* Document lifecycle. `text` is the full current document (the client uses
 * full-document sync for simplicity and correctness). */
void lsp_did_open(Lsp *l, const char *uri, const char *language_id,
                  const char *text);
void lsp_did_change(Lsp *l, const char *uri, int version, const char *text);

/* Asynchronous requests. Results surface through lsp_take_* after lsp_poll(). */
void lsp_definition(Lsp *l, const char *uri, int line, int col);
void lsp_hover(Lsp *l, const char *uri, int line, int col);

/* Read whatever the server has sent and dispatch complete messages. Cheap to
 * call every frame; does nothing if no bytes are waiting. */
void lsp_poll(Lsp *l);

/* Retrieve-and-clear the most recent definition / hover result. Each returns 1
 * and fills its out-param if a fresh result was pending, else 0. */
int lsp_take_definition(Lsp *l, LspLocation *out);
int lsp_take_hover(Lsp *l, char *buf, size_t cap);

/* Copy the diagnostics most recently published for `uri` (up to `max`); returns
 * the count. `*published` (optional) is set to 1 if the server has ever sent a
 * diagnostics report for this uri (so an empty result means "clean", not
 * "unknown"). */
size_t lsp_diagnostics(Lsp *l, const char *uri, LspDiag *out, size_t max,
                       int *published);

/* 1 once the server's `initialize` handshake has completed. */
int lsp_ready(const Lsp *l);
/* 1 while the child process is still alive. */
int lsp_alive(const Lsp *l);

#endif /* WAVE_LSP_H */
