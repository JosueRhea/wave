/* test_standard.c — non-vim (standard/VS Code-style) editing.
 *
 * The invariant every case here leans on: a live selection is exactly
 * "mode == MODE_VISUAL && cursor != anchor", and standard selection is
 * caret-to-caret rather than vim's inclusive VISUAL. */
#include "test.h"
#include "standard.h"

#include <stdlib.h>
#include <string.h>

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    e->anchor = 0;
    editor_clear_history(e);
    e->modified = 0;
}

static char *text_of(Editor *e) { return editor_text(e); }

static void check_text(Editor *e, const char *want) {
    char *got = text_of(e);
    CHECK_STR(got, want);
    free(got);
}

int main(void) {
    Editor e;
    ModalState m;

    /* --- entering standard mode --- */
    modal_init(&m);
    fill(&e, "hello world\nsecond line\n");
    standard_set_enabled(&e, &m, 1);
    CHECK_EQ(m.mode, MODE_INSERT);      /* never NORMAL: keys must type */
    CHECK(editor_selection_exclusive());
    CHECK(!standard_has_selection(&e, &m));

    /* --- plain motion moves and holds no selection --- */
    CHECK(standard_motion(&e, &m, STD_MOTION_RIGHT, 0));
    CHECK_EQ(e.cursor, 1);
    CHECK_EQ(e.anchor, 1);
    CHECK(!standard_has_selection(&e, &m));
    CHECK_EQ(m.mode, MODE_INSERT);

    /* --- shift-motion selects, caret-to-caret --- */
    e.cursor = e.anchor = 0;
    modal_enter_insert(&m);
    CHECK(standard_motion(&e, &m, STD_MOTION_RIGHT, 1));
    CHECK(standard_has_selection(&e, &m));
    CHECK_EQ(m.mode, MODE_VISUAL);
    EditorRange r;
    CHECK(editor_visual_range(&e, &r));
    /* Exclusive: one shift-right selects exactly one character, not two. */
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, 1);

    /* Shrinking back onto the anchor drops the selection entirely. */
    CHECK(standard_motion(&e, &m, STD_MOTION_LEFT, 1));
    CHECK(!standard_has_selection(&e, &m));
    CHECK_EQ(m.mode, MODE_INSERT);

    /* --- unshifted arrow collapses to the selection edge without moving --- */
    e.cursor = e.anchor = 0;
    modal_enter_insert(&m);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);   /* selects "hel" */
    CHECK_EQ(e.cursor, 3);
    standard_motion(&e, &m, STD_MOTION_LEFT, 0);
    CHECK_EQ(e.cursor, 0);                          /* to the start edge... */
    CHECK(!standard_has_selection(&e, &m));

    e.cursor = e.anchor = 0;
    modal_enter_insert(&m);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 1);
    standard_motion(&e, &m, STD_MOTION_RIGHT, 0);
    CHECK_EQ(e.cursor, 3);                          /* ...and to the end edge */
    CHECK(!standard_has_selection(&e, &m));

    /* --- word and line motions --- */
    e.cursor = e.anchor = 0;
    modal_enter_insert(&m);
    standard_motion(&e, &m, STD_MOTION_WORD_RIGHT, 0);
    CHECK_EQ(e.cursor, 6);                          /* "hello |world" */
    standard_motion(&e, &m, STD_MOTION_LINE_START, 0);
    CHECK_EQ(e.cursor, 0);
    standard_motion(&e, &m, STD_MOTION_LINE_END, 0);
    CHECK_EQ(e.cursor, strlen("hello world"));
    standard_motion(&e, &m, STD_MOTION_DOC_END, 0);
    CHECK_EQ(e.cursor, strlen("hello world\nsecond line\n"));
    standard_motion(&e, &m, STD_MOTION_DOC_START, 0);
    CHECK_EQ(e.cursor, 0);

    /* --- vertical motion keeps selection semantics --- */
    e.cursor = e.anchor = 0;
    modal_enter_insert(&m);
    CHECK(standard_motion(&e, &m, STD_MOTION_DOWN, 1));
    CHECK(standard_has_selection(&e, &m));
    CHECK_EQ(e.anchor, 0);
    editor_close(&e);

    /* --- typing replaces the selection --- */
    modal_init(&m);
    fill(&e, "hello world\n");
    standard_set_enabled(&e, &m, 1);
    standard_select_all(&e, &m);
    CHECK(standard_has_selection(&e, &m));
    standard_text_input(&e, &m, 'X');
    check_text(&e, "X");
    CHECK(!standard_has_selection(&e, &m));
    editor_close(&e);

    /* --- backspace takes the selection, not the neighbour --- */
    modal_init(&m);
    fill(&e, "abcdef\n");
    standard_set_enabled(&e, &m, 1);
    e.anchor = 1;
    e.cursor = 4;
    m.mode = MODE_VISUAL;
    CHECK(standard_editor_key(&e, &m, EDITOR_KEY_BACKSPACE));
    check_text(&e, "aef\n");
    CHECK_EQ(e.cursor, 1);
    CHECK(!standard_has_selection(&e, &m));

    /* With no selection it is an ordinary backspace. */
    CHECK(standard_editor_key(&e, &m, EDITOR_KEY_BACKSPACE));
    check_text(&e, "ef\n");
    editor_close(&e);

    /* --- delete key on a selection --- */
    modal_init(&m);
    fill(&e, "abcdef\n");
    standard_set_enabled(&e, &m, 1);
    e.anchor = 0;
    e.cursor = 3;
    m.mode = MODE_VISUAL;
    CHECK(standard_editor_key(&e, &m, EDITOR_KEY_DELETE));
    check_text(&e, "def\n");
    editor_close(&e);

    /* --- enter replaces a selection --- */
    modal_init(&m);
    fill(&e, "abcdef\n");
    standard_set_enabled(&e, &m, 1);
    e.anchor = 1;
    e.cursor = 5;
    m.mode = MODE_VISUAL;
    standard_editor_key(&e, &m, EDITOR_KEY_ENTER);
    check_text(&e, "a\nf\n");
    CHECK(!standard_has_selection(&e, &m));
    editor_close(&e);

    /* --- select all / escape --- */
    modal_init(&m);
    fill(&e, "one\ntwo\n");
    standard_set_enabled(&e, &m, 1);
    CHECK(standard_select_all(&e, &m));
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, strlen("one\ntwo\n"));   /* exclusive: exactly the buffer */
    CHECK(standard_escape(&e, &m));          /* had a selection to drop */
    CHECK(!standard_has_selection(&e, &m));
    CHECK_EQ(m.mode, MODE_INSERT);           /* escape never leaves insert */
    CHECK(!standard_escape(&e, &m));         /* nothing left to drop */
    CHECK_EQ(m.mode, MODE_INSERT);
    editor_close(&e);

    /* --- leaving standard mode restores vim --- */
    modal_init(&m);
    fill(&e, "abc\n");
    standard_set_enabled(&e, &m, 1);
    CHECK(editor_selection_exclusive());
    standard_set_enabled(&e, &m, 0);
    CHECK_EQ(m.mode, MODE_NORMAL);
    CHECK(!editor_selection_exclusive());

    /* vim's VISUAL is inclusive again: anchor == cursor still covers one char */
    e.anchor = e.cursor = 0;
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, 1);
    editor_close(&e);

    /* Word select (double-click). */
    modal_init(&m);
    fill(&e, "foo bar_baz qux\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 5;                 /* inside "bar_baz" */
    CHECK(standard_select_word(&e, &m));
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 4);
    CHECK_EQ(r.end, 11);                     /* underscore is part of the word */
    /* A caret just past a word still takes that word. */
    e.cursor = e.anchor = 3;
    CHECK(standard_select_word(&e, &m));
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, 3);
    /* On whitespace with no word behind it, nothing is selected. */
    e.cursor = e.anchor = 11;                /* the space before "qux" */
    e.cursor = 11;
    CHECK(standard_select_word(&e, &m));     /* "bar_baz" ends here */
    editor_close(&e);

    /* ---- line commands ---- */

    /* Copy / cut with no selection take the whole line, newline included. */
    modal_init(&m);
    fill(&e, "one\ntwo\nthree\n");
    standard_set_enabled(&e, &m, 1);
    char *c = standard_copy(&e, &m);
    CHECK_STR(c, "one\n");
    free(c);
    check_text(&e, "one\ntwo\nthree\n");   /* copy does not modify */
    c = standard_cut(&e, &m);
    CHECK_STR(c, "one\n");
    free(c);
    check_text(&e, "two\nthree\n");
    CHECK_EQ(e.cursor, 0);

    /* With a selection they take exactly the selection. */
    e.anchor = 0; e.cursor = 3; m.mode = MODE_VISUAL;
    c = standard_copy(&e, &m);
    CHECK_STR(c, "two");
    free(c);
    c = standard_cut(&e, &m);
    CHECK_STR(c, "two");
    free(c);
    check_text(&e, "\nthree\n");
    editor_close(&e);

    /* Delete line. */
    modal_init(&m);
    fill(&e, "a\nb\nc\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 2;                /* on "b" */
    CHECK(standard_delete_line(&e, &m));
    check_text(&e, "a\nc\n");
    editor_close(&e);

    /* Duplicate line. */
    modal_init(&m);
    fill(&e, "a\nb\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(standard_duplicate_line(&e, &m));
    check_text(&e, "a\na\nb\n");
    editor_close(&e);

    /* Move line down, then back up — round-trips to the original. */
    modal_init(&m);
    fill(&e, "one\ntwo\nthree\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;                /* on "one" */
    CHECK(standard_move_line(&e, &m, +1));
    check_text(&e, "two\none\nthree\n");
    CHECK(standard_move_line(&e, &m, -1));
    check_text(&e, "one\ntwo\nthree\n");
    /* At the edges it is a no-op rather than a corruption. */
    e.cursor = e.anchor = 0;
    CHECK(!standard_move_line(&e, &m, -1));
    check_text(&e, "one\ntwo\nthree\n");
    editor_close(&e);

    /* Moving the last line, which has no trailing newline of its own. */
    modal_init(&m);
    fill(&e, "a\nb");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 2;                /* on "b" */
    CHECK(standard_move_line(&e, &m, -1));
    check_text(&e, "b\na");
    editor_close(&e);

    /* ⌘⌫ — to the indent first, then to the line start. */
    modal_init(&m);
    fill(&e, "    hello\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = strlen("    hello");
    CHECK(standard_delete_to_line_start(&e, &m));
    check_text(&e, "    \n");
    CHECK(standard_delete_to_line_start(&e, &m));
    check_text(&e, "\n");
    CHECK(!standard_delete_to_line_start(&e, &m));   /* nothing left */
    editor_close(&e);

    /* Comment toggle, single line. */
    modal_init(&m);
    fill(&e, "int x;\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(standard_toggle_comment(&e, &m));
    check_text(&e, "// int x;\n");
    CHECK(standard_toggle_comment(&e, &m));
    check_text(&e, "int x;\n");
    editor_close(&e);

    /* Indentation is preserved: the token goes after it, not at column 0. */
    modal_init(&m);
    fill(&e, "    deep();\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    standard_toggle_comment(&e, &m);
    check_text(&e, "    // deep();\n");
    standard_toggle_comment(&e, &m);
    check_text(&e, "    deep();\n");
    editor_close(&e);

    /* A multi-line selection: a partly-commented block comments fully rather
     * than inverting line by line, and blank lines are left alone. */
    modal_init(&m);
    fill(&e, "a();\n// b();\n\nc();\n");
    standard_set_enabled(&e, &m, 1);
    e.anchor = 0;
    e.cursor = strlen("a();\n// b();\n\nc();");
    m.mode = MODE_VISUAL;
    standard_toggle_comment(&e, &m);
    check_text(&e, "// a();\n// // b();\n\n// c();\n");
    editor_close(&e);

    /* All-commented uncomments. */
    modal_init(&m);
    fill(&e, "// a();\n// b();\n");
    standard_set_enabled(&e, &m, 1);
    e.anchor = 0;
    e.cursor = strlen("// a();\n// b();");
    m.mode = MODE_VISUAL;
    standard_toggle_comment(&e, &m);
    check_text(&e, "a();\nb();\n");
    editor_close(&e);

    /* ---- indent / outdent ---- */

    /* Tab with a caret is not a block indent — the caller inserts a literal
     * tab instead, which is what returning 0 signals. */
    modal_init(&m);
    fill(&e, "a();\nb();\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(!standard_indent(&e, &m, 0));
    check_text(&e, "a();\nb();\n");

    /* A selection spanning lines indents the block and keeps the selection. */
    e.anchor = 0;
    e.cursor = strlen("a();\nb();");
    m.mode = MODE_VISUAL;
    CHECK(standard_indent(&e, &m, 0));
    check_text(&e, "    a();\n    b();\n");
    CHECK(standard_has_selection(&e, &m));
    CHECK(standard_indent(&e, &m, 1));
    check_text(&e, "a();\nb();\n");
    editor_close(&e);

    /* ⇧Tab outdents a single line too, and a partial indent is not over-eaten. */
    modal_init(&m);
    fill(&e, "  x();\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 4;
    CHECK(standard_indent(&e, &m, 1));
    check_text(&e, "x();\n");
    CHECK(!standard_indent(&e, &m, 1));      /* nothing left to remove */
    editor_close(&e);

    /* ---- word / line deletes ---- */
    modal_init(&m);
    fill(&e, "alpha beta gamma\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = strlen("alpha beta");
    CHECK(standard_delete_word_left(&e, &m));
    check_text(&e, "alpha  gamma\n");
    /* Word-right deletes through the trailing whitespace to the next word,
     * which is what ⌥⌦ does on macOS. */
    e.cursor = e.anchor = 0;
    CHECK(standard_delete_word_right(&e, &m));
    check_text(&e, "gamma\n");
    editor_close(&e);

    modal_init(&m);
    fill(&e, "keep this\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 4;
    CHECK(standard_delete_to_line_end(&e, &m));
    check_text(&e, "keep\n");
    editor_close(&e);

    /* ⌘L takes the whole line; a repeat extends to the next. */
    modal_init(&m);
    fill(&e, "one\ntwo\nthree\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 1;
    CHECK(standard_select_line(&e, &m));
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, 4);
    CHECK(standard_select_line(&e, &m));
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.end, 8);
    editor_close(&e);

    /* Insert line below / above, from mid-line. */
    modal_init(&m);
    fill(&e, "first\nsecond\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 2;                 /* inside "first" */
    CHECK(standard_insert_line(&e, &m, 1));
    check_text(&e, "first\n\nsecond\n");     /* line not split */
    editor_close(&e);

    /* ---- multiple carets ---- */
    modal_init(&m);
    fill(&e, "foo\nfoo\nfoo\n");
    standard_set_enabled(&e, &m, 1);
    CHECK_EQ(standard_caret_count(&e), 0u);

    /* First ⌘D selects the word, the next two add carets on the occurrences. */
    e.cursor = e.anchor = 0;
    CHECK(standard_select_next_occurrence(&e, &m));   /* selects "foo" */
    CHECK(standard_has_selection(&e, &m));
    CHECK_EQ(standard_caret_count(&e), 0u);
    CHECK(standard_select_next_occurrence(&e, &m));
    CHECK_EQ(standard_caret_count(&e), 1u);
    CHECK(standard_select_next_occurrence(&e, &m));
    CHECK_EQ(standard_caret_count(&e), 2u);

    /* Typing replaces every occurrence at once. */
    standard_multi_text_input(&e, &m, 'X');
    check_text(&e, "X\nX\nX\n");
    /* One ⌘Z reverts every caret's insert — not just the primary. */
    CHECK(editor_undo(&e));
    check_text(&e, "foo\nfoo\nfoo\n");
    CHECK_EQ(standard_caret_count(&e), 2u);
    CHECK(editor_redo(&e));
    check_text(&e, "X\nX\nX\n");
    CHECK_EQ(standard_caret_count(&e), 2u);
    editor_close(&e);

    /* Multi-type then undo restores the pre-type caret offsets. */
    modal_init(&m);
    fill(&e, "aa\nbb\ncc\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(standard_add_caret(&e, 3, 3));
    CHECK(standard_add_caret(&e, 6, 6));
    standard_multi_text_input(&e, &m, 'Z');
    check_text(&e, "Zaa\nZbb\nZcc\n");
    CHECK(editor_undo(&e));
    check_text(&e, "aa\nbb\ncc\n");
    CHECK_EQ(e.cursor, 0u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 3u);
        CHECK(standard_caret_at(&e, 1, &ca, &cc));
        CHECK_EQ(cc, 6u);
    }
    editor_close(&e);

    /* Backspace at every caret. */
    modal_init(&m);
    fill(&e, "ab\nab\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 2;                 /* after the first "ab" */
    CHECK(standard_add_caret(&e, 5, 5));     /* after the second */
    CHECK_EQ(standard_caret_count(&e), 1u);
    standard_multi_editor_key(&e, &m, EDITOR_KEY_BACKSPACE);
    check_text(&e, "a\na\n");
    editor_close(&e);

    /* Carets are capped, deduplicated, and cleared on demand. */
    modal_init(&m);
    fill(&e, "abc\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(standard_add_caret(&e, 1, 1));
    CHECK(!standard_add_caret(&e, 1, 1));    /* duplicate ignored */
    CHECK(!standard_add_caret(&e, 0, 0));    /* the primary is not an extra */
    CHECK_EQ(standard_caret_count(&e), 1u);
    size_t ca = 0, cc = 0;
    CHECK(standard_caret_at(&e, 0, &ca, &cc));
    CHECK_EQ(cc, 1u);
    CHECK(!standard_caret_at(&e, 1, &ca, &cc));
    standard_clear_carets(&e);
    CHECK_EQ(standard_caret_count(&e), 0u);
    /* With no extras the multi- paths behave exactly like the single ones. */
    standard_multi_text_input(&e, &m, 'Z');
    check_text(&e, "Zabc\n");
    editor_close(&e);

    /* Motions move every caret, not just the primary. */
    modal_init(&m);
    fill(&e, "abcd\nefgh\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;                 /* 'a' */
    CHECK(standard_add_caret(&e, 5, 5));     /* 'e' */
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_RIGHT, 0));
    CHECK_EQ(e.cursor, 1u);                  /* 'b' */
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 6u);                    /* 'f' */
    }
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_LEFT, 0));
    CHECK_EQ(e.cursor, 0u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 5u);
    }
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_DOWN, 0));
    CHECK_EQ(e.cursor, 5u);                  /* 'e' */
    CHECK_EQ(standard_caret_count(&e), 1u);  /* second caret still present */
    /* Shift+Right extends each selection independently. */
    e.cursor = e.anchor = 0;
    e.extra_n = 0;
    CHECK(standard_add_caret(&e, 5, 5));
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_RIGHT, 1));
    CHECK(standard_has_selection(&e, &m));
    CHECK_EQ(e.anchor, 0u);
    CHECK_EQ(e.cursor, 1u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(ca, 5u);
        CHECK_EQ(cc, 6u);
    }
    editor_close(&e);

    /* Word and line-start move both carets. */
    modal_init(&m);
    fill(&e, "aa bb\ncc dd\n");
    standard_set_enabled(&e, &m, 1);
    e.cursor = e.anchor = 0;
    CHECK(standard_add_caret(&e, 6, 6));
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_WORD_RIGHT, 0));
    CHECK_EQ(e.cursor, 3u);                  /* start of "bb" */
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 9u);                    /* start of "dd" */
    }
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_LINE_START, 0));
    CHECK_EQ(e.cursor, 0u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 6u);
    }
    standard_clear_carets(&e);
    CHECK_EQ(standard_caret_count(&e), 0u);
    editor_close(&e);

    /* Restore the default so a later test in the same binary is unaffected. */
    editor_set_selection_exclusive(0);
    TEST_REPORT();
}
