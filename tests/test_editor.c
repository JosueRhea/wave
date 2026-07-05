/* test_editor.c — editor model helpers that do not need the GUI shell. */
#include "test.h"
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fill(Editor *e, const char *text) {
    editor_init(e);
    e->buf = buffer_new();
    ed_insert(e, text, strlen(text));
    e->cursor = 0;
    editor_clear_history(e);
    e->modified = 0;
}

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

int main(void) {
    Editor e;
    char word[32];
    size_t out = 999;
    int strong = 0;

    editor_init(&e);
    CHECK(!editor_has_buffer(NULL));
    CHECK(!editor_has_buffer(&e));
    CHECK(!editor_has_path(NULL));
    CHECK(!editor_has_path(&e));
    CHECK(editor_path(NULL) == NULL);
    CHECK(editor_path(&e) == NULL);
    CHECK(!editor_has_visual_rows(NULL));
    CHECK(!editor_has_visual_rows(&e));
    CHECK_EQ((int)editor_scroll_y(NULL), 0);
    CHECK_EQ((int)editor_scroll_y(&e), 0);
    editor_set_scroll_y(&e, 12.0f);
    CHECK_EQ((int)editor_scroll_y(&e), 12);
    editor_set_scroll_y(&e, -3.0f);
    CHECK_EQ((int)editor_scroll_y(&e), 0);
    editor_set_scroll_y(NULL, 4.0f);
    CHECK(!editor_refresh_highlighter(NULL));
    editor_close(&e);

    fill(&e, "let alpha = 1;\nalpha();\n");
    CHECK(editor_has_buffer(&e));
    e.cursor = strlen("let alpha = 1;\n") + 2;
    CHECK(editor_find_definition(&e, word, sizeof word, &out, &strong));
    CHECK_STR(word, "alpha");
    CHECK_EQ(out, 4);
    CHECK_EQ(strong, 1);
    char msg[128];
    CHECK(editor_goto_local_definition(&e, msg, sizeof msg));
    CHECK_EQ(e.cursor, 4);
    CHECK_STR(msg, "alpha -> Ln 1, Col 5");
    editor_close(&e);

    fill(&e, "alpha();\nalpha();\n");
    e.cursor = strlen("alpha();\n") + 2;
    CHECK(editor_find_definition(&e, word, sizeof word, &out, &strong));
    CHECK_STR(word, "alpha");
    CHECK_EQ(out, 0);
    CHECK_EQ(strong, 0);
    CHECK(editor_goto_local_definition(&e, msg, sizeof msg));
    CHECK_EQ(e.cursor, 0);
    CHECK_STR(msg, "alpha -> Ln 1, Col 1 (first use)");
    editor_close(&e);

    fill(&e, "let alpha = 1;\n");
    word[0] = 'x';
    e.cursor = strlen("let alpha ");
    CHECK(!editor_find_definition(&e, word, sizeof word, &out, &strong));
    CHECK_STR(word, "");
    CHECK(!editor_goto_local_definition(&e, msg, sizeof msg));
    CHECK_STR(msg, "no identifier here");
    editor_close(&e);

    fill(&e, "abcdef\nxyz\n");
    CHECK(!editor_has_visual_rows(&e));
    wrap_build(&e, WRAP_NOWRAP_COLS);
    CHECK(editor_has_visual_rows(&e));
    out = 999;
    CHECK(editor_offset_at_point(&e, 120.0f, 50.0f, 100.0f, 50.0f,
                                 20.0f, 10.0f, &out));
    CHECK_EQ(out, 2);
    editor_close(&e);

    fill(&e, "abcdef\nxyz\n");
    wrap_build(&e, 3);
    out = 999;
    CHECK(editor_offset_at_point(&e, 110.0f, 70.0f, 100.0f, 50.0f,
                                 20.0f, 10.0f, &out));
    CHECK_EQ(out, 4);
    editor_close(&e);

    fill(&e, "0\n1\n2\n3\n4\n5\n");
    e.cursor = strlen("0\n1\n2\n3\n4\n");
    e.scroll_y = 0.0f;
    editor_center_cursor(&e, 10.0f, 30.0f);
    CHECK_EQ((int)e.scroll_y, 35);
    CHECK(editor_move_to_line_col(&e, 2, 1));
    CHECK_EQ(e.cursor, 2);
    CHECK(editor_move_to_line_col(&e, 99, 99));
    CHECK_EQ(e.cursor, buffer_length(e.buf));
    CHECK(editor_move_to_lsp_position(&e, 1, 1, msg, sizeof msg));
    CHECK_EQ(e.cursor, 3);
    CHECK_STR(msg, "def -> Ln 2, Col 2");
    CHECK(editor_move_to_lsp_position(&e, 99, 99, msg, sizeof msg));
    CHECK_EQ(e.cursor, buffer_length(e.buf));
    CHECK_STR(msg, "def -> Ln 100, Col 100");
    editor_close(&e);

    fill(&e, "abcdef\n");
    e.anchor = 1;
    e.cursor = 3;
    EditorRange r;
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 1);
    CHECK_EQ(r.end, 4);
    char *slice = editor_range_text(&e, r.start, r.end);
    CHECK_STR(slice, "bcd");
    free(slice);
    e.anchor = 3;
    e.cursor = 1;
    CHECK(editor_visual_range(&e, &r));
    CHECK_EQ(r.start, 1);
    CHECK_EQ(r.end, 4);
    editor_close(&e);

    fill(&e, "abcdef\n");
    e.cursor = 2;
    CHECK(editor_line_range(&e, &r));
    CHECK_EQ(r.start, 0);
    CHECK_EQ(r.end, 7);
    editor_close(&e);

    fill(&e, "abcdef\n");
    e.anchor = 1;
    e.cursor = 3;
    CHECK(editor_replace_visual_selection(&e, "XY", 2));
    CHECK_EQ(e.cursor, 3);
    char *replaced = editor_text(&e);
    CHECK_STR(replaced, "aXYef\n");
    free(replaced);
    editor_close(&e);

    fill(&e, "abcdef\nsecond\n");
    e.cursor = 2;
    char *copied = editor_copy_text(&e, 0);
    CHECK_STR(copied, "abcdef\n");
    free(copied);
    e.anchor = 1;
    e.cursor = 3;
    copied = editor_copy_text(&e, 1);
    CHECK_STR(copied, "bcd");
    free(copied);
    CHECK_EQ(editor_paste_text(&e, "XY", 1), EDITOR_PASTE_REPLACED_VISUAL);
    CHECK(editor_paste_enters_insert(EDITOR_PASTE_REPLACED_VISUAL));
    char *pasted = editor_text(&e);
    CHECK_STR(pasted, "aXYef\nsecond\n");
    free(pasted);
    CHECK_EQ(e.cursor, 3);
    CHECK_EQ(e.group_open, 0);
    CHECK_EQ(editor_paste_text(&e, "!", 0), EDITOR_PASTE_INSERTED);
    CHECK(!editor_paste_enters_insert(EDITOR_PASTE_INSERTED));
    pasted = editor_text(&e);
    CHECK_STR(pasted, "aXY!ef\nsecond\n");
    free(pasted);
    CHECK_EQ(e.cursor, 4);
    CHECK_EQ(editor_paste_text(&e, "", 0), EDITOR_PASTE_NONE);
    CHECK(!editor_paste_enters_insert(EDITOR_PASTE_NONE));
    editor_close(&e);

    fill(&e, "one two one\nthree two\n");
    e.cursor = 0;
    out = 999;
    CHECK(editor_find_text(&e, "two", 0, 0, &out));
    CHECK_EQ(out, 4);
    CHECK(editor_search_text(&e, "two", 0, msg, sizeof msg));
    CHECK_EQ(e.cursor, 4);
    CHECK_STR(msg, "/two");
    CHECK(editor_search_text(&e, "two", 0, msg, sizeof msg));
    CHECK_EQ(e.cursor, strlen("one two one\nthree "));
    CHECK(editor_search_text(&e, "two", 0, msg, sizeof msg));
    CHECK_EQ(e.cursor, 4);
    CHECK(editor_search_text(&e, "one", 1, msg, sizeof msg));
    CHECK_EQ(e.cursor, 0);
    CHECK(editor_search_text(&e, "one", 1, msg, sizeof msg));
    CHECK_EQ(e.cursor, 8);
    CHECK(!editor_search_text(&e, "missing", 0, msg, sizeof msg));
    CHECK_STR(msg, "pattern not found: missing");
    CHECK(!editor_find_text(&e, "", 0, 0, &out));
    char search_word[32];
    e.cursor = 5;
    CHECK(editor_word_under_cursor(&e, search_word, sizeof search_word));
    CHECK_STR(search_word, "two");
    CHECK(editor_search_text(&e, search_word, 0, msg, sizeof msg));
    CHECK_EQ(e.cursor, strlen("one two one\nthree "));
    e.cursor = 7;
    CHECK(editor_word_under_cursor(&e, search_word, sizeof search_word));
    CHECK_STR(search_word, "one");
    e.cursor = 3;
    CHECK(editor_word_under_cursor(&e, search_word, sizeof search_word));
    CHECK_STR(search_word, "two");
    editor_close(&e);

    fill(&e, "");
    char history_msg[64] = "stale";
    CHECK(!editor_apply_history_action(&e, 0, history_msg, sizeof history_msg));
    CHECK_STR(history_msg, "already at oldest change");
    CHECK(editor_apply_text_input(&e, 'x'));
    CHECK(editor_apply_history_action(&e, 0, history_msg, sizeof history_msg));
    CHECK_STR(history_msg, "");
    char *history_text = editor_text(&e);
    CHECK_STR(history_text, "");
    free(history_text);
    CHECK(editor_apply_history_action(&e, 1, history_msg, sizeof history_msg));
    CHECK_STR(history_msg, "");
    history_text = editor_text(&e);
    CHECK_STR(history_text, "x");
    free(history_text);
    CHECK(!editor_apply_history_action(&e, 1, history_msg, sizeof history_msg));
    CHECK_STR(history_msg, "already at newest change");
    editor_close(&e);

    fill(&e, "");
    CHECK(editor_apply_text_input(&e, 'A'));
    CHECK(editor_apply_text_input(&e, 0x00E9));
    char *typed_text = editor_text(&e);
    CHECK_STR(typed_text, "Aé");
    free(typed_text);
    CHECK_EQ(e.cursor, 3);
    CHECK_EQ(e.group_open, 1);
    editor_close(&e);

    fill(&e, "");
    CHECK(!editor_insert_encoded_text(NULL, "ignored"));
    Editor no_buf;
    editor_init(&no_buf);
    CHECK(!editor_insert_encoded_text(&no_buf, "ignored"));
    CHECK(editor_insert_encoded_text(&e, "one\\ntwo\\\\n"));
    char *encoded_text = editor_text(&e);
    CHECK_STR(encoded_text, "one\ntwo\\\n");
    free(encoded_text);
    CHECK_EQ(e.cursor, strlen("one\ntwo\\\n"));
    CHECK(!editor_insert_encoded_text(&e, NULL));
    editor_close(&e);
    editor_close(&no_buf);

    fill(&e, "ab\ncd\n");
    e.cursor = 2;
    CHECK(editor_apply_insert_key(&e, EDITOR_KEY_ENTER));
    char *insert_key_text = editor_text(&e);
    CHECK_STR(insert_key_text, "ab\n\ncd\n");
    free(insert_key_text);
    CHECK_EQ(e.cursor, 3);
    CHECK_EQ(e.group_open, 1);
    CHECK(editor_apply_insert_key(&e, EDITOR_KEY_TAB));
    insert_key_text = editor_text(&e);
    CHECK_STR(insert_key_text, "ab\n    \ncd\n");
    free(insert_key_text);
    CHECK_EQ(e.cursor, 7);
    CHECK(editor_apply_insert_key(&e, EDITOR_KEY_BACKSPACE));
    CHECK_EQ(e.cursor, 6);
    CHECK(editor_apply_insert_key(&e, EDITOR_KEY_DELETE));
    insert_key_text = editor_text(&e);
    CHECK_STR(insert_key_text, "ab\n   cd\n");
    free(insert_key_text);
    editor_close(&e);

    fill(&e, "abc\ndef\n");
    e.cursor = 1;
    CHECK(editor_apply_motion_key(&e, EDITOR_KEY_RIGHT));
    CHECK_EQ(e.cursor, 2);
    CHECK(editor_apply_motion_key(&e, EDITOR_KEY_LEFT));
    CHECK_EQ(e.cursor, 1);
    CHECK(editor_apply_motion_key(&e, EDITOR_KEY_DOWN));
    CHECK_EQ(e.cursor, 5);
    CHECK(editor_apply_motion_key(&e, EDITOR_KEY_UP));
    CHECK_EQ(e.cursor, 1);
    CHECK(!editor_apply_motion_key(&e, EDITOR_KEY_TAB));
    e.group_open = 1;
    editor_cancel_group(&e);
    CHECK_EQ(e.group_open, 0);
    editor_cancel_group(NULL);
    editor_close(&e);

    fill(&e, "abcdef\nxyz\n");
    wrap_build(&e, WRAP_NOWRAP_COLS);
    e.cursor = 0;
    out = 999;
    CHECK(editor_apply_click_position(&e, 140.0f, 50.0f,
                                      100.0f, 50.0f, 20.0f, 10.0f,
                                      &out));
    CHECK_EQ(out, 4);
    CHECK_EQ(e.cursor, 4);
    CHECK(editor_apply_click_position(&e, 100.0f, 70.0f,
                                      100.0f, 50.0f, 20.0f, 10.0f,
                                      NULL));
    CHECK_EQ(e.cursor, 7);
    editor_close(&e);

    fill(&e, "abcdef\nxyz\n");
    wrap_build(&e, WRAP_NOWRAP_COLS);
    e.cursor = 0;
    e.group_open = 1;
    e.scroll_y = 0.0f;
    CHECK(editor_apply_drag_selection(&e, 1, 130.0f, 50.0f,
                                      100.0f, 50.0f, 20.0f, 10.0f,
                                      -40.0f));
    CHECK_EQ(e.anchor, 1);
    CHECK_EQ(e.cursor, 3);
    CHECK_EQ(e.group_open, 0);
    CHECK_EQ((int)e.scroll_y, 0);
    e.scroll_y = 10.0f;
    CHECK(editor_apply_drag_selection(&e, 2, 120.0f, 70.0f,
                                      100.0f, 50.0f, 20.0f, 10.0f,
                                      20.0f));
    CHECK_EQ(e.anchor, 2);
    CHECK_EQ(e.cursor, 11);
    CHECK_EQ((int)e.scroll_y, 30);
    editor_close(&e);

    const char *open_path = "/tmp/wave_editor_open_test.txt";
    FILE *f = fopen(open_path, "w");
    CHECK(f != NULL);
    fputs("opened\n", f);
    fclose(f);
    editor_init(&e);
    CHECK_EQ(editor_open_file(&e, open_path, 1, NULL), 0);
    CHECK(e.buf != NULL);
    CHECK_STR(e.path, open_path);
    CHECK_EQ(e.preview, 1);
    char *opened = editor_text(&e);
    CHECK_STR(opened, "opened\n");
    free(opened);
    CHECK_EQ(editor_open_file(&e, "/tmp/does-not-exist-wave", 0, NULL), -1);
    CHECK_STR(e.path, open_path);
    CHECK(!editor_update_highlighter(NULL));
    CHECK(!editor_update_highlighter(&e));
    editor_close(&e);

    const char *highlight_path = "/tmp/wave_editor_highlight_test.c";
    write_file(highlight_path, "int x;\n");
    editor_init(&e);
    CHECK_EQ(editor_open_file(&e, highlight_path, 0, NULL), 0);
    CHECK(editor_has_path(&e));
    CHECK_STR(editor_path(&e), highlight_path);
    CHECK(e.hl != NULL);
    CHECK(editor_refresh_highlighter(&e));
    CHECK(!editor_update_highlighter(&e));
    HighlightSpan spans[64];
    CHECK(editor_highlight_spans(&e, 0, buffer_length(e.buf), spans, 64) > 0);
    CHECK_EQ(editor_highlight_spans(NULL, 0, 1, spans, 64), 0);
    CHECK_EQ(editor_highlight_spans(&e, 0, 1, NULL, 64), 0);
    CHECK_EQ(editor_highlight_spans(&e, 0, 1, spans, 0), 0);
    ed_insert(&e, "int main(void) { return 0; }\n", 29);
    CHECK(editor_update_highlighter(&e));
    CHECK_EQ(e.dirty, 0);
    CHECK_EQ(buffer_pending_edits(e.buf), 0);
    CHECK(!editor_update_highlighter(&e));
    editor_close(&e);

    const char *path = "/tmp/wave_editor_reload_test.txt";
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("old text\n", f);
    fclose(f);
    editor_init(&e);
    e.buf = buffer_open(path);
    e.path = dupstr(path);
    e.cursor = 500;
    e.scroll_y = 42.0f;
    e.modified = 1;
    e.external_changed = 1;
    ed_insert(&e, "dirty", 5);
    f = fopen(path, "w");
    CHECK(f != NULL);
    fputs("new\n", f);
    fclose(f);
    struct stat st;
    CHECK_EQ(stat(path, &st), 0);
    CHECK_EQ(editor_reload_file(&e, NULL, &st), 0);
    CHECK_EQ(e.modified, 0);
    CHECK_EQ(e.external_changed, 0);
    CHECK_EQ(e.cursor, buffer_length(e.buf));
    CHECK_EQ((int)e.scroll_y, 42);
    char *text = editor_text(&e);
    CHECK_STR(text, "new\n");
    free(text);
    editor_close(&e);

    const char *missing_path = "/tmp/wave_editor_disk_missing_test.txt";
    write_file(missing_path, "gone\n");
    editor_init(&e);
    CHECK_EQ(editor_open_file(&e, missing_path, 0, NULL), 0);
    CHECK(e.watch.seen);
    unlink(missing_path);
    CHECK_EQ(editor_apply_disk_change(&e, NULL), EDITOR_DISK_MISSING);
    CHECK_EQ(e.external_changed, 1);
    CHECK_EQ(editor_apply_disk_change(&e, NULL), EDITOR_DISK_NOOP);
    editor_close(&e);

    const char *conflict_path = "/tmp/wave_editor_disk_conflict_test.txt";
    write_file(conflict_path, "old\n");
    editor_init(&e);
    CHECK_EQ(editor_open_file(&e, conflict_path, 0, NULL), 0);
    ed_insert(&e, "local", 5);
    write_file(conflict_path, "new remote text\n");
    CHECK_EQ(editor_apply_disk_change(&e, NULL), EDITOR_DISK_DIRTY_CONFLICT);
    CHECK_EQ(e.modified, 1);
    CHECK_EQ(e.external_changed, 1);
    editor_close(&e);

    const char *clean_path = "/tmp/wave_editor_disk_reload_test.txt";
    write_file(clean_path, "old\n");
    editor_init(&e);
    CHECK_EQ(editor_open_file(&e, clean_path, 0, NULL), 0);
    write_file(clean_path, "new remote text\n");
    CHECK_EQ(editor_apply_disk_change(&e, NULL), EDITOR_DISK_RELOADED);
    CHECK_EQ(e.modified, 0);
    CHECK_EQ(e.external_changed, 0);
    char *disk_reloaded = editor_text(&e);
    CHECK_STR(disk_reloaded, "new remote text\n");
    free(disk_reloaded);
    editor_close(&e);

    editor_init(&e);
    e.path = dupstr("/tmp/wave_status.txt");
    CHECK(editor_disk_change_message(&e, EDITOR_DISK_MISSING, msg, sizeof msg));
    CHECK_STR(msg, "file missing on disk: /tmp/wave_status.txt");
    CHECK(editor_disk_change_message(&e, EDITOR_DISK_DIRTY_CONFLICT, msg, sizeof msg));
    CHECK_STR(msg, "external change; save would overwrite: /tmp/wave_status.txt");
    CHECK(editor_disk_change_message(&e, EDITOR_DISK_RELOADED, msg, sizeof msg));
    CHECK_STR(msg, "reloaded /tmp/wave_status.txt");
    CHECK(editor_disk_change_message(&e, EDITOR_DISK_RELOAD_FAILED, msg, sizeof msg));
    CHECK_STR(msg, "could not reload /tmp/wave_status.txt");
    CHECK(!editor_disk_change_message(&e, EDITOR_DISK_NOOP, msg, sizeof msg));
    editor_close(&e);

    TEST_REPORT();
}
