/* highlight.c — see highlight.h. */
#include "highlight.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

struct Highlighter {
    Buffer *buf;
    TSParser *parser;
    TSTree *tree;
    TSQuery *query;

    /* Read scratch: tree-sitter asks for bytes at an offset and we copy a
     * chunk out of the piece table into this reusable buffer. */
    char *chunk;
    size_t chunk_cap;
};

/* TSInput read callback: serve a run of bytes starting at byte_index by
 * copying out of the piece table. Returns a pointer + length; tree-sitter
 * calls repeatedly until *bytes_read == 0 (EOF). */
static const char *read_cb(void *payload, uint32_t byte_index,
                           TSPoint position, uint32_t *bytes_read) {
    (void)position;
    Highlighter *h = payload;
    const PieceTable *pt = buffer_pt(h->buf);
    size_t total = pt_length(pt);
    if (byte_index >= total) {
        *bytes_read = 0;
        return "";
    }
    size_t want = total - byte_index;
    if (want > 4096) want = 4096; /* serve in chunks */
    if (want > h->chunk_cap) {
        h->chunk = realloc(h->chunk, want);
        h->chunk_cap = want;
    }
    size_t got = pt_read(pt, byte_index, want, h->chunk);
    *bytes_read = (uint32_t)got;
    return h->chunk;
}

static void reparse(Highlighter *h) {
    TSInput input = {h, read_cb, TSInputEncodingUTF8};
    TSTree *next = ts_parser_parse(h->parser, h->tree, input);
    if (h->tree) ts_tree_delete(h->tree);
    h->tree = next;
}

Highlighter *hl_new(Buffer *buf, const TSLanguage *lang, const char *query_src) {
    Highlighter *h = calloc(1, sizeof(Highlighter));
    h->buf = buf;
    h->parser = ts_parser_new();
    ts_parser_set_language(h->parser, lang);

    uint32_t err_off = 0;
    TSQueryError err_type = TSQueryErrorNone;
    h->query = ts_query_new(lang, query_src,
                            (uint32_t)strlen(query_src), &err_off, &err_type);
    if (!h->query) {
        fprintf(stderr, "query error type %d at byte offset %u\n",
                (int)err_type, err_off);
        ts_parser_delete(h->parser);
        free(h);
        return NULL;
    }
    reparse(h);
    return h;
}

void hl_free(Highlighter *h) {
    if (!h) return;
    if (h->query) ts_query_delete(h->query);
    if (h->tree) ts_tree_delete(h->tree);
    if (h->parser) ts_parser_delete(h->parser);
    free(h->chunk);
    free(h);
}

void hl_update(Highlighter *h) {
    size_t pending = buffer_pending_edits(h->buf);
    if (pending == 0) return;

    BufferEdit *edits = malloc(pending * sizeof(BufferEdit));
    size_t n = buffer_take_edits(h->buf, edits, pending);
    for (size_t i = 0; i < n; i++) {
        BufferEdit *e = &edits[i];
        TSInputEdit te = {
            .start_byte = (uint32_t)e->start_byte,
            .old_end_byte = (uint32_t)e->old_end_byte,
            .new_end_byte = (uint32_t)e->new_end_byte,
            .start_point = {(uint32_t)e->start_row, (uint32_t)e->start_col},
            .old_end_point = {(uint32_t)e->old_end_row, (uint32_t)e->old_end_col},
            .new_end_point = {(uint32_t)e->new_end_row, (uint32_t)e->new_end_col},
        };
        ts_tree_edit(h->tree, &te);
    }
    free(edits);
    reparse(h);
}

size_t hl_spans(Highlighter *h, size_t start_byte, size_t end_byte,
                HighlightSpan *out, size_t max) {
    TSQueryCursor *cur = ts_query_cursor_new();
    ts_query_cursor_set_byte_range(cur, (uint32_t)start_byte, (uint32_t)end_byte);
    ts_query_cursor_exec(cur, h->query, ts_tree_root_node(h->tree));

    size_t produced = 0;
    TSQueryMatch match;
    while (produced < max && ts_query_cursor_next_match(cur, &match)) {
        for (uint16_t i = 0; i < match.capture_count && produced < max; i++) {
            TSQueryCapture cap = match.captures[i];
            uint32_t name_len = 0;
            const char *name =
                ts_query_capture_name_for_id(h->query, cap.index, &name_len);
            out[produced].start_byte = ts_node_start_byte(cap.node);
            out[produced].end_byte = ts_node_end_byte(cap.node);
            out[produced].name = name; /* stable for query lifetime */
            produced++;
        }
    }
    ts_query_cursor_delete(cur);
    return produced; /* never exceeds `max`: the buffer the caller passed */
}

int hl_node_at(Highlighter *h, size_t byte, char *type, size_t cap,
               size_t *start, size_t *end) {
    if (!h->tree || !type || cap == 0) return 0;
    TSNode root = ts_tree_root_node(h->tree);
    uint32_t b = (uint32_t)byte;
    TSNode n = ts_node_named_descendant_for_byte_range(root, b, b);
    if (ts_node_is_null(n)) return 0;
    const char *t = ts_node_type(n);
    snprintf(type, cap, "%s", t ? t : "?");
    if (start) *start = ts_node_start_byte(n);
    if (end) *end = ts_node_end_byte(n);
    return 1;
}

int hl_scope_at(Highlighter *h, size_t cursor, int *opens, int *balanced) {
    if (opens) *opens = 0;
    if (balanced) *balanced = 0;
    if (!h || !h->tree || cursor == 0) return 0;
    hl_update(h);

    TSNode root = ts_tree_root_node(h->tree);
    TSNode left = ts_node_descendant_for_byte_range(
        root, (uint32_t)(cursor - 1), (uint32_t)cursor);
    if (ts_node_is_null(left)) return 0;
    const char *lt = ts_node_type(left);
    char close = 0;
    if (!strcmp(lt, "{")) close = '}';
    else if (!strcmp(lt, "[")) close = ']';
    else if (!strcmp(lt, "(")) close = ')';
    else return 1;
    if (opens) *opens = 1;

    uint32_t end = ts_node_end_byte(root);
    if (cursor >= end) return 1;
    TSNode right = ts_node_descendant_for_byte_range(
        root, (uint32_t)cursor, (uint32_t)(cursor + 1));
    if (!ts_node_is_null(right)) {
        const char *rt = ts_node_type(right);
        if (balanced && rt && rt[0] == close && rt[1] == '\0') *balanced = 1;
    }
    return 1;
}

/* Iterative pre-order walk collecting ERROR/MISSING nodes. Uses a TSTreeCursor
 * so we don't recurse the C stack on deeply nested trees. */
size_t hl_diagnostics(Highlighter *h, Diagnostic *out, size_t max) {
    if (!h->tree) return 0;
    TSNode root = ts_tree_root_node(h->tree);
    if (!ts_node_has_error(root)) return 0; /* fast path: clean parse */

    size_t produced = 0;
    TSTreeCursor cur = ts_tree_cursor_new(root);
    int descend = 1;
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cur);
        int is_err = ts_node_is_error(node);
        int is_missing = ts_node_is_missing(node);
        if (is_err || is_missing) {
            TSPoint s = ts_node_start_point(node);
            TSPoint e = ts_node_end_point(node);
            out[produced] = (Diagnostic){
                .start_byte = ts_node_start_byte(node),
                .end_byte = ts_node_end_byte(node),
                .start_row = s.row, .start_col = s.column,
                .end_row = e.row, .end_col = e.column,
                .is_missing = is_missing,
                .message = is_missing ? "missing token" : "syntax error",
            };
            if (++produced >= max) break; /* fill no more than the caller's buffer */
        }

        /* Descend only into subtrees that still contain errors, to keep the
         * walk proportional to the number of error regions. */
        if (descend && ts_node_has_error(node) &&
            ts_tree_cursor_goto_first_child(&cur)) {
            continue;
        }
        if (ts_tree_cursor_goto_next_sibling(&cur)) {
            descend = 1;
            continue;
        }
        /* climb until we find an unvisited sibling, or reach the root */
        int climbed = 0;
        while (ts_tree_cursor_goto_parent(&cur)) {
            if (ts_tree_cursor_goto_next_sibling(&cur)) {
                climbed = 1;
                break;
            }
        }
        if (!climbed) break;
        descend = 1;
    }
    ts_tree_cursor_delete(&cur);
    return produced;
}

static HlIdentKind ident_kind_for_type(const char *type) {
    if (!strcmp(type, "type_identifier")) return HL_IDENT_TYPE;
    if (!strcmp(type, "property_identifier") || !strcmp(type, "field_identifier") ||
        !strcmp(type, "shorthand_property_identifier"))
        return HL_IDENT_PROPERTY;
    return HL_IDENT_PLAIN;
}

static int ci_has_prefix(const char *s, const char *prefix) {
    if (!prefix || !prefix[0]) return 1;
    for (; *prefix; s++, prefix++) {
        if (!*s) return 0;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    }
    return 1;
}

static int ident_already_seen(HlIdent *out, size_t n, const char *text) {
    for (size_t i = 0; i < n; i++)
        if (!strcmp(out[i].text, text)) return 1;
    return 0;
}

/* Iterative pre-order walk (same shape as hl_diagnostics) visiting every
 * leaf; collects distinct identifier-shaped tokens matching `prefix`. */
size_t hl_identifiers(Highlighter *h, const char *prefix, HlIdent *out, size_t max) {
    if (!h || !h->tree || max == 0) return 0;
    TSNode root = ts_tree_root_node(h->tree);
    const PieceTable *pt = buffer_pt(h->buf);

    size_t produced = 0;
    TSTreeCursor cur = ts_tree_cursor_new(root);
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cur);
        if (ts_node_child_count(node) == 0) {
            const char *type = ts_node_type(node);
            if (type && strstr(type, "identifier")) {
                uint32_t sb = ts_node_start_byte(node);
                uint32_t eb = ts_node_end_byte(node);
                size_t len = eb - sb;
                if (len > 0 && len < sizeof out[0].text) {
                    char buf[64];
                    pt_read(pt, sb, len, buf);
                    buf[len] = '\0';
                    if (ci_has_prefix(buf, prefix) &&
                        !ident_already_seen(out, produced, buf)) {
                        snprintf(out[produced].text, sizeof out[produced].text, "%s", buf);
                        out[produced].kind = ident_kind_for_type(type);
                        produced++;
                        if (produced >= max) break;
                    }
                }
            }
        }
        if (ts_tree_cursor_goto_first_child(&cur)) continue;
        if (ts_tree_cursor_goto_next_sibling(&cur)) continue;
        int climbed = 0;
        while (ts_tree_cursor_goto_parent(&cur)) {
            if (ts_tree_cursor_goto_next_sibling(&cur)) { climbed = 1; break; }
        }
        if (!climbed) break;
    }
    ts_tree_cursor_delete(&cur);
    return produced;
}
