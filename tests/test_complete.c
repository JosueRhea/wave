/* test_complete.c — the headless completion engine: prefix extraction,
 * filter/sort tiers, stale-generation rejection, and the accept edit-plan. */
#include "test.h"
#include "complete.h"
#include "editor.h"

#include <string.h>

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    editor_clear_history(e);
    e->modified = 0;
}

static CompleteItem item(const char *label, const char *sort_text) {
    CompleteItem it;
    memset(&it, 0, sizeof it);
    snprintf(it.label, sizeof it.label, "%s", label);
    snprintf(it.insert_text, sizeof it.insert_text, "%s", label);
    if (sort_text) snprintf(it.sort_text, sizeof it.sort_text, "%s", sort_text);
    it.kind = COMPLETE_KIND_VARIABLE;
    return it;
}

int main(void) {
    /* ----- prefix extraction ----- */
    Editor e;
    fill(&e, "  foo_bar baz");
    const PieceTable *pt = buffer_pt(e.buf);
    CHECK_EQ(complete_prefix_start(pt, 2), 2);  /* just past leading spaces */
    CHECK_EQ(complete_prefix_start(pt, 9), 2);  /* end of "foo_bar" */
    CHECK_EQ(complete_prefix_start(pt, 5), 2);  /* mid-identifier */
    CHECK_EQ(complete_prefix_start(pt, 0), 0);  /* start of buffer */
    CHECK_EQ(complete_prefix_start(pt, 13), 10); /* end of "baz" */
    CHECK_EQ(complete_prefix_start(NULL, 5), 5);

    /* ----- lifecycle / generation ----- */
    CompleteState c;
    complete_init(&c);
    CHECK(!complete_is_active(&c));

    unsigned int gen1 = complete_begin(&c, 2, "fo");
    CHECK(complete_is_active(&c));
    CHECK_EQ(c.word_start, 2);
    CHECK_STR(c.prefix, "fo");

    CompleteItem items[3] = {
        item("foo_bar", NULL), item("foobar", NULL), item("baz", NULL),
    };
    /* a stale generation is dropped */
    CHECK_EQ(complete_set_items(&c, gen1 - 1, items, 3), 0);
    CHECK_EQ(c.nitems, 0);
    /* the current generation is applied */
    CHECK_EQ(complete_set_items(&c, gen1, items, 3), 1);
    CHECK_EQ(c.nitems, 3);

    /* ----- filter tiers: prefix match beats subsequence match ----- */
    CHECK_EQ(c.nfiltered, 2); /* foo_bar, foobar match "fo"; baz doesn't */
    CHECK_STR(c.items[c.filtered[0]].label, "foo_bar"); /* alpha before "foobar" */
    CHECK_STR(c.items[c.filtered[1]].label, "foobar");

    /* re-triggering bumps the generation; the old one is now stale */
    unsigned int gen2 = complete_begin(&c, 2, "ba");
    CHECK(gen2 != gen1);
    CHECK_EQ(complete_set_items(&c, gen1, items, 3), 0); /* dropped: stale */
    CHECK_EQ(c.nitems, 0);

    CompleteItem items2[2] = { item("baz", "0"), item("bazaar", "1") };
    CHECK_EQ(complete_set_items(&c, gen2, items2, 2), 1);
    CHECK_EQ(c.nfiltered, 2);
    CHECK_STR(c.items[c.filtered[0]].label, "baz");    /* sort_text "0" < "1" */
    CHECK_STR(c.items[c.filtered[1]].label, "bazaar");

    /* subsequence fallback tier: "bz" isn't a prefix of either but matches
     * as a subsequence of both. */
    complete_set_prefix(&c, "bz");
    CHECK_EQ(c.nfiltered, 2);

    /* a prefix matching nothing empties the filtered set without closing */
    complete_set_prefix(&c, "zzz");
    CHECK_EQ(c.nfiltered, 0);
    CHECK(complete_is_active(&c)); /* engine itself doesn't auto-close; caller decides */

    /* ----- move / selection wraps ----- */
    complete_set_prefix(&c, "ba");
    CHECK_EQ(c.nfiltered, 2);
    CHECK_EQ(c.sel, 0);
    complete_move(&c, -1);
    CHECK_EQ(c.sel, 1); /* wraps to the last item */
    complete_move(&c, +1);
    CHECK_EQ(c.sel, 0);

    /* ----- accept produces the edit plan ----- */
    CompleteEdit edit;
    CHECK_EQ(complete_accept(&c, 5, &edit), 1);
    CHECK_EQ(edit.start, c.word_start);
    CHECK_EQ(edit.end, 5);
    CHECK_STR(edit.text, "baz");

    complete_close(&c);
    CHECK(!complete_is_active(&c));
    CHECK_EQ(c.nitems, 0);
    CHECK_EQ(c.nfiltered, 0);
    CHECK_EQ(complete_accept(&c, 5, &edit), 0); /* nothing to accept once inactive */

    /* a reply to a generation from before the close is still stale */
    CHECK_EQ(complete_set_items(&c, gen2, items2, 2), 0);

    /* insert_text overrides label when accepting */
    complete_begin(&c, 0, "f");
    CompleteItem snip = item("foo", NULL);
    snprintf(snip.insert_text, sizeof snip.insert_text, "foo()");
    complete_set_items(&c, c.generation, &snip, 1);
    CHECK_EQ(complete_accept(&c, 1, &edit), 1);
    CHECK_STR(edit.text, "foo()");

    /* ----- plain-text word scan fallback ----- */
    CompleteItem words[COMPLETE_MAX_ITEMS];
    int n = complete_collect_buffer_words("let alpha = alpha2 + beta; // 9lives",
                                          "al", words, COMPLETE_MAX_ITEMS);
    CHECK_EQ(n, 2); /* alpha, alpha2 — deduplicated, digit-led token excluded */
    CHECK_STR(words[0].label, "alpha");
    CHECK_STR(words[1].label, "alpha2");
    CHECK_EQ(words[0].kind, COMPLETE_KIND_TEXT);

    n = complete_collect_buffer_words("cat cat cat dog", NULL, words, COMPLETE_MAX_ITEMS);
    CHECK_EQ(n, 2); /* cat deduplicated, dog included; no prefix filter */

    n = complete_collect_buffer_words("9lives lives", NULL, words, COMPLETE_MAX_ITEMS);
    CHECK_EQ(n, 1); /* the digit-led token is dropped whole, not split */
    CHECK_STR(words[0].label, "lives");

    n = complete_collect_buffer_words("one two three", "x", words, COMPLETE_MAX_ITEMS);
    CHECK_EQ(n, 0);

    n = complete_collect_buffer_words(NULL, "a", words, COMPLETE_MAX_ITEMS);
    CHECK_EQ(n, 0);

    /* the cap is respected even when more words are available */
    char many[4096];
    size_t off = 0;
    for (int i = 0; i < 50 && off + 8 < sizeof many; i++)
        off += (size_t)snprintf(many + off, sizeof many - off, "word%02d ", i);
    n = complete_collect_buffer_words(many, NULL, words, 5);
    CHECK_EQ(n, 5);

    /* ----- kind tags ----- */
    CHECK_STR(complete_kind_tag(COMPLETE_KIND_FUNCTION), "fn");
    CHECK_STR(complete_kind_tag(COMPLETE_KIND_VARIABLE), "var");
    CHECK_STR(complete_kind_tag(COMPLETE_KIND_TEXT), "txt");

    /* ----- layout: nothing to draw when inactive or empty ----- */
    CompleteLayout lay;
    CompleteState empty;
    complete_init(&empty);
    CHECK_EQ(complete_layout(&empty, 1000, 800, 8.0f, 16.0f, 4.0f, 20.0f,
                             100.0f, 100.0f, 0.0f, 1.0f, &lay), 0);

    complete_begin(&empty, 0, "f");
    CompleteItem one[1] = { item("foobar_long_identifier_name", NULL) };
    complete_set_items(&empty, empty.generation, one, 1);
    CHECK_EQ(complete_layout(&empty, 1000, 800, 8.0f, 16.0f, 4.0f, 20.0f,
                             100.0f, 100.0f, 0.0f, 1.0f, &lay), 1);
    CHECK(lay.w > 0.0f);
    CHECK(lay.h > 0.0f);
    CHECK_EQ(lay.visible_rows, 1);

    TEST_REPORT();
}
