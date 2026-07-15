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

typedef struct {
    int active;
    int percentage; /* -1 when the server reports indeterminate progress */
    char title[128];
    char message[256];
} LspProgress;

#define LSP_MAX_COMPLETIONS 2048
#define LSP_MAX_ADDITIONAL_EDITS 4
#define LSP_COMPLETION_TEXT_CAP 256

typedef struct {
    int start_line, start_col, end_line, end_col;
    char new_text[LSP_COMPLETION_TEXT_CAP];
} LspTextEdit;

/* One entry from a textDocument/completion reply. `kind` is the raw LSP
 * CompletionItemKind number (0 if the server didn't send one) — callers map
 * it onto their own vocabulary (see complete_kind_tag()/CompleteKind). */
typedef struct {
    char label[96];
    char insert_text[LSP_COMPLETION_TEXT_CAP]; /* textEdit.newText, else insertText, else label */
    char detail[256];
    char sort_text[32];
    int kind;
    int resolve_id; /* TypeScript LS data.cacheId; 0 when resolve is unavailable. */
    int resolved;
    int nadditional_edits;
    LspTextEdit additional_edits[LSP_MAX_ADDITIONAL_EDITS];
} LspCompletionItem;

typedef struct {
    char label[1024];
    int active_parameter;
    int active_signature;
    int signature_count;
} LspSignatureHelp;

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
void lsp_completion(Lsp *l, const char *uri, int line, int col);
void lsp_completion_triggered(Lsp *l, const char *uri, int line, int col,
                              char trigger_character);
void lsp_completion_resolve(Lsp *l, const LspCompletionItem *item);
void lsp_signature_help(Lsp *l, const char *uri, int line, int col,
                        char trigger_character, int retrigger);

/* Read whatever the server has sent and dispatch complete messages. Cheap to
 * call every frame; does nothing if no bytes are waiting. */
void lsp_poll(Lsp *l);

/* Retrieve-and-clear the most recent definition / hover result. Each returns 1
 * and fills its out-param if a fresh result was pending, else 0. */
int lsp_take_definition(Lsp *l, LspLocation *out);
int lsp_take_hover(Lsp *l, char *buf, size_t cap);
/* Copy up to `max` items from the most recent completion reply into `out`
 * and clear it; `*n` (optional) is set to the count copied. Returns 1 once a
 * fresh reply was pending (even if it held zero items), else 0. */
int lsp_take_completions(Lsp *l, LspCompletionItem *out, size_t max, size_t *n);
int lsp_take_resolved_completion(Lsp *l, LspCompletionItem *out);
int lsp_take_signature_help(Lsp *l, LspSignatureHelp *out);

/* Copy the server's latest work-done progress state. Returns 1 after the
 * server has emitted at least one progress event; `active` distinguishes an
 * in-progress task from its completed final state. */
int lsp_progress(const Lsp *l, LspProgress *out);

/* Convert LSP Markdown hover contents into readable plain text while keeping
 * signature/documentation paragraph breaks. */
void lsp_format_hover_text(const char *markdown, char *out, size_t cap);

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
