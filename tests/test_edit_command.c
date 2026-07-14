/* test_edit_command.c — vim-style edit commands without the GUI shell. */
#include "test.h"
#include "edit_command.h"

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
