#include "../src/buffer.h"
#include "../src/highlight.h"
#include "test.h"

const TSLanguage *tree_sitter_c(void);

/* A minimal C highlights query exercising a few capture kinds. */
static const char *QUERY =
    "\"return\" @keyword\n"
    "(comment) @comment\n"
    "(string_literal) @string\n"
    "(primitive_type) @type\n"
    "(number_literal) @number\n";

/* Find the first span whose name matches and whose covered text equals `text`. */
static int has_span(HighlightSpan *spans, size_t n, const char *name,
                    const char *doc, const char *text) {
    size_t tlen = strlen(text);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(spans[i].name, name) != 0) continue;
        size_t len = spans[i].end_byte - spans[i].start_byte;
        if (len == tlen && memcmp(doc + spans[i].start_byte, text, tlen) == 0)
            return 1;
    }
    return 0;
}

int main(void) {
    const char *src =
        "int main() {\n"
        "    // hi\n"
        "    char *s = \"x\";\n"
        "    return 42;\n"
        "}\n";

    Buffer *b = buffer_new();
    buffer_insert(b, 0, src, strlen(src));

    Highlighter *h = hl_new(b, tree_sitter_c(), QUERY);
    CHECK(h != NULL);
    if (!h) TEST_REPORT();
    hl_update(h); /* drain the seed edit, reparse */

    HighlightSpan spans[64];
    size_t n = hl_spans(h, 0, strlen(src), spans, 64);
    CHECK(n > 0);

    CHECK(has_span(spans, n, "type", src, "int"));
    CHECK(has_span(spans, n, "type", src, "char"));
    CHECK(has_span(spans, n, "comment", src, "// hi"));
    CHECK(has_span(spans, n, "string", src, "\"x\""));
    CHECK(has_span(spans, n, "keyword", src, "return"));
    CHECK(has_span(spans, n, "number", src, "42"));

    /* incremental edit: rename the number, reparse, re-query */
    size_t pos = (size_t)(strstr(src, "42") - src);
    buffer_delete(b, pos, 2);
    buffer_insert(b, pos, "1000", 4);
    hl_update(h);

    /* document changed; rebuild a fresh copy to validate against */
    char *doc = pt_to_cstring(buffer_pt(b), NULL);
    n = hl_spans(h, 0, buffer_length(b), spans, 64);
    CHECK(has_span(spans, n, "number", doc, "1000"));
    CHECK(has_span(spans, n, "keyword", doc, "return"));
    free(doc);

    hl_free(h);
    buffer_free(b);

    /* diagnostics: a clean parse yields none; a broken one is detected */
    {
        Buffer *ok = buffer_new();
        const char *good = "int main(void) { return 0; }\n";
        buffer_insert(ok, 0, good, strlen(good));
        Highlighter *hok = hl_new(ok, tree_sitter_c(), QUERY);
        hl_update(hok);
        Diagnostic d[16];
        CHECK_EQ(hl_diagnostics(hok, d, 16), 0);
        hl_free(hok);
        buffer_free(ok);

        Buffer *bad = buffer_new();
        const char *broken = "int main(void) { return ; \n"; /* missing expr + brace */
        buffer_insert(bad, 0, broken, strlen(broken));
        Highlighter *hbad = hl_new(bad, tree_sitter_c(), QUERY);
        hl_update(hbad);
        size_t nd = hl_diagnostics(hbad, d, 16);
        CHECK(nd > 0);
        hl_free(hbad);
        buffer_free(bad);
    }

    /* hl_identifiers: distinct identifier-shaped leaves, prefix-filtered,
     * deduplicated, capped. */
    {
        const char *idsrc =
            "int compute(int alpha, int beta) {\n"
            "    int alphabet = alpha + beta;\n"
            "    return alphabet;\n"
            "}\n";
        Buffer *ib = buffer_new();
        buffer_insert(ib, 0, idsrc, strlen(idsrc));
        Highlighter *ih = hl_new(ib, tree_sitter_c(), QUERY);
        hl_update(ih);

        HlIdent ids[32];
        /* "alphabet" appears twice in source but only once here (deduped);
         * "compute"/"beta" are excluded by the "al" prefix. */
        size_t nid = hl_identifiers(ih, "al", ids, 32);
        CHECK_EQ(nid, 2);
        int has_alpha = 0, has_alphabet = 0;
        for (size_t i = 0; i < nid; i++) {
            CHECK_EQ(ids[i].kind, HL_IDENT_PLAIN);
            if (!strcmp(ids[i].text, "alpha")) has_alpha = 1;
            if (!strcmp(ids[i].text, "alphabet")) has_alphabet = 1;
        }
        CHECK(has_alpha);
        CHECK(has_alphabet);

        /* case-insensitive prefix */
        CHECK_EQ(hl_identifiers(ih, "AL", ids, 32), 2);

        /* no filter: every distinct identifier, "alphabet" still deduped */
        nid = hl_identifiers(ih, NULL, ids, 32);
        CHECK(nid >= 4); /* compute, alpha, beta, alphabet */

        /* a prefix matching nothing yields nothing */
        CHECK_EQ(hl_identifiers(ih, "zzz", ids, 32), 0);

        /* the walk stops as soon as the cap is hit */
        CHECK_EQ(hl_identifiers(ih, NULL, ids, 1), 1);

        hl_free(ih);
        buffer_free(ib);
    }

    TEST_REPORT();
}
