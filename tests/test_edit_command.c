/* test_edit_command.c — vim-style edit commands without the GUI shell. */
#include "test.h"
#include "edit_command.h"
#include "standard.h"

#include <stdlib.h>
#include <string.h>

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    editor_clear_history(e);
    e->modified = 0;
}

int main(void) {
    Editor e;
    ModalState m;
    YankRegister y = {0};
    EditCommandResult r;

    modal_init(&m);
    fill(&e, "alpha beta\nnext\n");
    r = edit_command_apply(&e, &m, &y, 'w');
    CHECK_EQ(r.flags, 0);
    CHECK_EQ(e.cursor, 6);
    r = edit_command_apply(&e, &m, &y, 'x');
    CHECK(r.flags & EDIT_COMMAND_YANKED);
    CHECK_STR(y.text, "b");
    char *text = editor_text(&e);
    CHECK_STR(text, "alpha eta\nnext\n");
    free(text);
    editor_close(&e);

    modal_init(&m);
    fill(&e, "one\ntwo\n");
    e.cursor = strlen("one");
    edit_command_apply(&e, &m, &y, 'a');
    CHECK_EQ(m.mode, MODE_INSERT);
    CHECK_EQ(e.cursor, strlen("one"));
    CHECK(editor_apply_text_input(&e, '!'));
    text = editor_text(&e);
    CHECK_STR(text, "one!\ntwo\n");
    free(text);
    editor_close(&e);

    modal_init(&m);
    fill(&e, "one\ntwo\n");
    e.cursor = 0;
    edit_command_apply(&e, &m, &y, 'A');
    CHECK_EQ(m.mode, MODE_INSERT);
    CHECK_EQ(e.cursor, strlen("one"));
    editor_close(&e);

    modal_init(&m);
    fill(&e, "if (ok) {\n  run();\n}\n");
    e.cursor = strlen("if (ok) {\n  ru");
    edit_command_apply(&e, &m, &y, 'o');
    CHECK_EQ(m.mode, MODE_INSERT);
    text = editor_text(&e);
    CHECK_STR(text, "if (ok) {\n  run();\n  \n}\n");
    free(text);
    CHECK_EQ(e.cursor, strlen("if (ok) {\n  run();\n  "));
    editor_close(&e);

    modal_init(&m);
    fill(&e, "if (ok) {\n  run();\n}\n");
    e.cursor = strlen("if (ok) {\n  ru");
    edit_command_apply(&e, &m, &y, 'O');
    CHECK_EQ(m.mode, MODE_INSERT);
    text = editor_text(&e);
    CHECK_STR(text, "if (ok) {\n  \n  run();\n}\n");
    free(text);
    CHECK_EQ(e.cursor, strlen("if (ok) {\n  "));
    editor_close(&e);
    yank_free(&y);

    modal_init(&m);
    fill(&e, "one\ntwo\nthree\n");
    edit_command_apply(&e, &m, &y, 'd');
    r = edit_command_apply(&e, &m, &y, 'd');
    CHECK(r.flags & EDIT_COMMAND_YANKED);
    CHECK_EQ(m.mode, MODE_NORMAL);
    CHECK_STR(y.text, "one\n");
    text = editor_text(&e);
    CHECK_STR(text, "two\nthree\n");
    free(text);
    editor_close(&e);
    yank_free(&y);

    modal_init(&m);
    fill(&e, "abcdef\n");
    e.anchor = 1;
    e.cursor = 3;
    modal_enter_visual(&m);
    r = edit_command_apply(&e, &m, &y, 'x');
    CHECK(r.flags & EDIT_COMMAND_YANKED);
    CHECK_EQ(m.mode, MODE_NORMAL);
    CHECK_STR(y.text, "bcd");
    text = editor_text(&e);
    CHECK_STR(text, "aef\n");
    free(text);
    editor_close(&e);
    yank_free(&y);

    /* ---- visual-block (Ctrl+V) ---- */
    editor_set_selection_exclusive(0);
    modal_init(&m);
    fill(&e, "abcd\nefgh\nijkl\n");
    e.cursor = e.anchor = 1; /* 'b' */
    modal_enter_visual_block(&m);
    CHECK_EQ(m.mode, MODE_VISUAL_BLOCK);
    edit_command_apply(&e, &m, &y, 'j'); /* down to 'f' */
    edit_command_apply(&e, &m, &y, 'l'); /* right to 'g' */
    {
        size_t r0, r1, c0, c1;
        CHECK(editor_block_bounds(&e, &r0, &r1, &c0, &c1));
        CHECK_EQ(r0, 0u);
        CHECK_EQ(r1, 1u);
        CHECK_EQ(c0, 1u);
        CHECK_EQ(c1, 3u); /* exclusive end */
    }
    r = edit_command_apply(&e, &m, &y, 'd');
    CHECK(r.flags & EDIT_COMMAND_YANKED);
    CHECK_EQ(m.mode, MODE_NORMAL);
    CHECK_STR(y.text, "bc\nfg");
    text = editor_text(&e);
    CHECK_STR(text, "ad\neh\nijkl\n");
    free(text);
    editor_close(&e);
    yank_free(&y);

    modal_init(&m);
    fill(&e, "aaa\nbbb\nccc\n");
    e.cursor = e.anchor = 0;
    modal_enter_visual_block(&m);
    edit_command_apply(&e, &m, &y, 'j');
    edit_command_apply(&e, &m, &y, 'j');
    r = edit_command_apply(&e, &m, &y, 'I');
    CHECK_EQ(m.mode, MODE_INSERT);
    CHECK_EQ(standard_caret_count(&e), 2u);
    CHECK(standard_multi_text_input(&e, &m, 'X'));
    text = editor_text(&e);
    CHECK_STR(text, "Xaaa\nXbbb\nXccc\n");
    free(text);
    CHECK_EQ(e.cursor, 1u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 6u); /* after X on line 1 (ascending store order) */
        CHECK(standard_caret_at(&e, 1, &ca, &cc));
        CHECK_EQ(cc, 11u); /* after X on line 2 */
    }
    CHECK(standard_multi_motion(&e, &m, STD_MOTION_RIGHT, 0));
    CHECK_EQ(e.cursor, 2u);
    {
        size_t ca = 0, cc = 0;
        CHECK(standard_caret_at(&e, 0, &ca, &cc));
        CHECK_EQ(cc, 7u);
        CHECK(standard_caret_at(&e, 1, &ca, &cc));
        CHECK_EQ(cc, 12u);
    }
    CHECK(editor_undo(&e));
    text = editor_text(&e);
    CHECK_STR(text, "aaa\nbbb\nccc\n");
    free(text);
    editor_close(&e);

    modal_init(&m);
    fill(&e, "aaa\nbbb\n");
    e.cursor = e.anchor = 1;
    modal_enter_visual_block(&m);
    edit_command_apply(&e, &m, &y, 'j');
    r = edit_command_apply(&e, &m, &y, 'c');
    CHECK_EQ(m.mode, MODE_INSERT);
    CHECK(standard_multi_text_input(&e, &m, 'Z'));
    text = editor_text(&e);
    CHECK_STR(text, "aZa\nbZb\n");
    free(text);
    editor_close(&e);
    yank_free(&y);

    modal_init(&m);
    fill(&e, "one\ntwo\nthree\n");
    edit_command_apply(&e, &m, &y, '2');
    r = edit_command_apply(&e, &m, &y, 'G');
    CHECK_EQ(r.flags, 0);
    CHECK_EQ(e.cursor, strlen("one\n"));
    edit_command_apply(&e, &m, &y, '9');
    r = edit_command_apply(&e, &m, &y, 'G');
    CHECK_EQ(r.flags, 0);
    CHECK_EQ(e.cursor, strlen("one\ntwo\n"));
    editor_close(&e);

    modal_init(&m);
    fill(&e, "abc\n");
    edit_command_apply(&e, &m, &y, 'g');
    r = edit_command_apply(&e, &m, &y, 'd');
    CHECK(r.flags & EDIT_COMMAND_GOTO_DEFINITION);
    r = edit_command_apply(&e, &m, &y, ':');
    CHECK(r.flags & EDIT_COMMAND_OPEN_COMMAND_LINE);
    r = edit_command_apply(&e, &m, &y, '/');
    CHECK(r.flags & EDIT_COMMAND_OPEN_BUFFER_SEARCH);
    r = edit_command_apply(&e, &m, &y, 'n');
    CHECK(r.flags & EDIT_COMMAND_SEARCH_NEXT);
    r = edit_command_apply(&e, &m, &y, 'N');
    CHECK(r.flags & EDIT_COMMAND_SEARCH_PREV);
    r = edit_command_apply(&e, &m, &y, '*');
    CHECK(r.flags & EDIT_COMMAND_SEARCH_WORD);
    editor_close(&e);

    char msg[64] = "stale";
    CHECK(edit_command_status_text((EditCommandResult){EDIT_COMMAND_UNDO_AT_OLDEST},
                                   msg, sizeof msg));
    CHECK_STR(msg, "already at oldest change");
    CHECK(!edit_command_status_text((EditCommandResult){0}, msg, sizeof msg));
    CHECK_STR(msg, "");
    CHECK(!edit_command_status_text((EditCommandResult){EDIT_COMMAND_UNDO_AT_OLDEST},
                                    NULL, sizeof msg));

    TEST_REPORT();
}
