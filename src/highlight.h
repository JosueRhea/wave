/* highlight.h — tree-sitter syntax highlighting over a Buffer.
 *
 * Owns a TSParser + TSTree and reparses incrementally: edits drained from the
 * Buffer are applied with ts_tree_edit() before re-running the parser, so a
 * one-character change in a huge file costs almost nothing. Highlight spans are
 * produced by running a TSQuery (a tree-sitter "highlights" query) over the
 * tree and reporting each capture as a colored byte range.
 *
 * Parsing reads directly from the piece table via a TSInput callback — the
 * document is never flattened into a contiguous string to parse it.
 */
#ifndef WAVE_HIGHLIGHT_H
#define WAVE_HIGHLIGHT_H

#include <stddef.h>
#include "buffer.h"

/* A contiguous run of text with one highlight name (e.g. "keyword",
 * "string", "function"). Spans are non-overlapping and in document order. */
typedef struct {
    size_t start_byte;
    size_t end_byte;
    const char *name; /* points into the query's capture-name table; not owned */
} HighlightSpan;

/* A syntax diagnostic derived from the parse tree: an ERROR node (a syntax
 * error tree-sitter recovered from) or a MISSING node (a token the grammar
 * expected but the source lacks). */
typedef struct {
    size_t start_byte, end_byte;
    size_t start_row, start_col;
    size_t end_row, end_col;
    int is_missing;        /* 1 = missing token, 0 = error region */
    const char *message;   /* static string; not owned */
} Diagnostic;

typedef struct Highlighter Highlighter;

/* tree-sitter's language handle; forward-declared so callers don't have to
 * include the tree-sitter headers just to name a grammar. */
typedef struct TSLanguage TSLanguage;

/* Create a highlighter for `buf` using grammar `lang`. `query_src` is a
 * tree-sitter highlights query (the contents of a highlights.scm). Performs
 * the initial parse. Returns NULL if the query fails to compile. */
Highlighter *hl_new(Buffer *buf, const TSLanguage *lang, const char *query_src);
void hl_free(Highlighter *h);

/* Apply any pending buffer edits to the tree and reparse incrementally. */
void hl_update(Highlighter *h);

/* Collect highlight spans intersecting [start_byte, end_byte) — typically the
 * visible viewport. Writes up to `max` spans into `out`, returns the number
 * that would have been produced (may exceed max; grow and retry if so). */
size_t hl_spans(Highlighter *h, size_t start_byte, size_t end_byte,
                HighlightSpan *out, size_t max);

/* Walk the current tree for ERROR and MISSING nodes. Writes up to `max`
 * diagnostics into `out`, returns the total number found. */
size_t hl_diagnostics(Highlighter *h, Diagnostic *out, size_t max);

/* Describe the smallest *named* node covering `byte`: copies its grammar node
 * type (e.g. "identifier", "call_expression") into `type` (up to `cap` bytes,
 * NUL-terminated) and reports its byte range via `start`/`end` (either may be
 * NULL). Returns 1 if a node was found, 0 otherwise. Powers the `gh` info pop. */
int hl_node_at(Highlighter *h, size_t byte, char *type, size_t cap,
               size_t *start, size_t *end);

/* Inspect the concrete syntax tokens surrounding a cursor. Reports whether
 * the byte before `cursor` opens a syntactic scope and whether the byte at the
 * cursor is its matching closer. This keeps indentation decisions grounded in
 * the active tab's parse tree instead of treating braces in strings/comments
 * as scopes. */
int hl_scope_at(Highlighter *h, size_t cursor, int *opens, int *balanced);

/* A coarse guess at what an identifier-shaped leaf node is, from its grammar
 * node type alone (no semantic analysis — a "type_identifier" is a type
 * reference, a "property_identifier"/"field_identifier" is a member name,
 * anything else tagged "identifier" is a plain binding/reference). */
typedef enum {
    HL_IDENT_PLAIN,
    HL_IDENT_TYPE,
    HL_IDENT_PROPERTY
} HlIdentKind;

typedef struct {
    char text[64];
    HlIdentKind kind;
} HlIdent;

/* Collect distinct identifier-shaped leaf tokens whose text starts with
 * `prefix` (case-insensitive; pass "" or NULL for no filter), skipping
 * strings/comments for free since tree-sitter never tags their contents as
 * identifier nodes. Powers local (no-LSP) completion. Capped at `max`; the
 * walk stops as soon as `max` matches are found. */
size_t hl_identifiers(Highlighter *h, const char *prefix, HlIdent *out, size_t max);

#endif /* WAVE_HIGHLIGHT_H */
