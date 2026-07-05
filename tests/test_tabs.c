/* test_tabs.c — tab set behavior independent of the GLFW shell. */
#include "test.h"
#include "tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

int main(void) {
    TabSet tabs = {0};

    CHECK_EQ(tabs_count(&tabs), 0);
    Editor *a = tabs_new(&tabs);
    CHECK(a != NULL);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    CHECK(tabs_current(&tabs) == a);
    CHECK(a->buf == NULL);

    tabs_free(&tabs);
    memset(&tabs, 0, sizeof tabs);
    const char *term_argv[] = {"/bin/sh", "-lc", "printf tabterm", NULL};
    Terminal *term = tabs_new_terminal(&tabs, "term", ".", term_argv);
    CHECK(term != NULL);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK_EQ(tabs_current_kind(&tabs), TAB_ITEM_TERMINAL);
    CHECK(tabs_current_terminal(&tabs) == term);
    CHECK(tabs_current(&tabs) == NULL);
    char label[64];
    tabs_label(&tabs, 0, label, sizeof label);
    CHECK_STR(label, "term");
    CHECK_EQ(tabs_close(&tabs, 0), 0);

    memset(&tabs, 0, sizeof tabs);
    a = tabs_new(&tabs);
    CHECK(a != NULL);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    CHECK(tabs_current(&tabs) == a);
    CHECK(a->buf == NULL);

    a->buf = buffer_new();
    CHECK(tabs_find_empty_scratch(&tabs) == a);
    CHECK(tabs_use_empty_scratch(&tabs) == a);
    CHECK(a->buf == NULL);
    a->buf = buffer_new();
    a->path = dupstr("/tmp/a.txt");
    CHECK_EQ(tabs_find_path(&tabs, "/tmp/a.txt"), 0);
    CHECK_EQ(tabs_find_path(&tabs, "/tmp/missing.txt"), -1);

    Editor *b = tabs_new(&tabs);
    CHECK(b != NULL);
    b->path = dupstr("/tmp/b.txt");
    CHECK_EQ(tabs_count(&tabs), 2);
    CHECK_EQ(tabs_active_index(&tabs), 1);
    CHECK(tabs_set_active(&tabs, 0));
    CHECK_EQ(tabs_active_index(&tabs), 0);
    CHECK(!tabs_set_active(&tabs, 9));
    CHECK_EQ(tabs_active_index(&tabs), 0);
    CHECK(tabs_set_active(&tabs, 1));
    tabs_goto(&tabs, -1);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    TabActionEffect action = tabs_goto_with_effect(&tabs, +1);
    CHECK(action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_active_index(&tabs), 1);
    action = tabs_set_active_with_effect(&tabs, 0);
    CHECK(action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    action = tabs_set_active_with_effect(&tabs, 99);
    CHECK(!action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    action = tabs_click_with_effect(&tabs, 1, 0);
    CHECK(action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_active_index(&tabs), 1);
    action = tabs_click_with_effect(&tabs, 99, 0);
    CHECK(!action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_active_index(&tabs), 1);

    CHECK(tabs_set_active(&tabs, 0));
    a->watch.native_fd = 42;
    b->watch.native_fd = 77;
    CHECK(tabs_find_file_watch(&tabs, 42) == a);
    CHECK(tabs_find_file_watch(&tabs, 77) == b);
    CHECK(tabs_find_file_watch(&tabs, 123) == NULL);
    TabDiskChange event = {0};
    CHECK(tabs_apply_file_watch_event(&tabs, NULL, 42, &event));
    CHECK(event.editor == a);
    CHECK_EQ(event.change, EDITOR_DISK_NOOP);
    char msg[128] = "stale";
    TabDiskChangeEffect effect = tabs_describe_disk_change(&tabs, &event,
                                                           msg, sizeof msg);
    CHECK(!effect.has_message);
    CHECK(!effect.reset_active_mode);
    event.change = EDITOR_DISK_RELOADED;
    effect = tabs_describe_disk_change(&tabs, &event, msg, sizeof msg);
    CHECK(effect.has_message);
    CHECK(effect.reset_active_mode);
    CHECK_STR(msg, "reloaded /tmp/a.txt");
    event.editor = b;
    effect = tabs_describe_disk_change(&tabs, &event, msg, sizeof msg);
    CHECK(effect.has_message);
    CHECK(!effect.reset_active_mode);
    TabDiskChange events[4];
    CHECK_EQ(tabs_apply_file_watch_poll(&tabs, NULL, events, 4), 0);
    WatchService watch;
    watch_service_init(&watch);
    double next_poll = 10.0;
    TabWatchEffect watch_effect = tabs_process_file_watchers(
        &tabs, &watch, 0.0, &next_poll, 0.5);
    CHECK(!watch_effect.has_message);
    CHECK(!watch_effect.reset_mode);
#ifndef __APPLE__
    CHECK_EQ((int)next_poll, 10);
    watch_effect = tabs_process_file_watchers(&tabs, &watch, 10.0, &next_poll, 0.5);
    CHECK(!watch_effect.has_message);
    CHECK(!watch_effect.reset_mode);
    CHECK_EQ((int)(next_poll * 10.0), 105);
#endif
    watch_service_shutdown(&watch);

    b->preview = 1;
    CHECK(tabs_find_preview(&tabs) == b);
    CHECK_EQ(tabs_active_index(&tabs), 1);
    TabOpenPlan plan = tabs_prepare_open(&tabs, "/tmp/b.txt", 0);
    CHECK(plan.editor == b);
    CHECK_EQ(plan.kind, TAB_OPEN_EXISTING);
    CHECK_EQ(tabs_active_index(&tabs), 1);
    plan = tabs_prepare_open(&tabs, "/tmp/c.txt", 1);
    CHECK(plan.editor == b);
    CHECK_EQ(plan.kind, TAB_OPEN_REUSE_PREVIEW);
    CHECK_EQ(tabs_active_index(&tabs), 1);

    b->preview = 1;
    plan = tabs_prepare_open(&tabs, "/tmp/b.txt", 0);
    CHECK(tabs_apply_existing_open(&plan, 0));
    CHECK_EQ(b->preview, 0);
    b->preview = 1;
    plan = tabs_prepare_open(&tabs, "/tmp/b.txt", 1);
    CHECK(tabs_apply_existing_open(&plan, 1));
    CHECK_EQ(b->preview, 1);

    CHECK_EQ(tabs_close(&tabs, 1), 1);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    CHECK_EQ(tabs_find_path(&tabs, "/tmp/a.txt"), 0);
    b = tabs_new(&tabs);
    b->path = dupstr("/tmp/b2.txt");
    action = tabs_click_with_effect(&tabs, 1, 1);
    CHECK(action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_count(&tabs), 1);
    action = tabs_close_with_effect(&tabs, 42);
    CHECK(action.reset_mode);
    CHECK(!action.close_window);
    CHECK_EQ(tabs_count(&tabs), 1);
    action = tabs_close_with_effect(&tabs, 0);
    CHECK(!action.reset_mode);
    CHECK(action.close_window);
    CHECK_EQ(tabs_count(&tabs), 0);

    tabs_free(&tabs);
    CHECK_EQ(tabs_count(&tabs), 0);

    memset(&tabs, 0, sizeof tabs);
    a = tabs_new(&tabs);
    a->buf = buffer_new();
    plan = tabs_prepare_open(&tabs, "/tmp/new.txt", 0);
    CHECK(plan.editor == a);
    CHECK_EQ(plan.kind, TAB_OPEN_REUSE_SCRATCH);
    CHECK_EQ(tabs_count(&tabs), 1);
    a->path = dupstr("/tmp/new.txt");
    plan = tabs_prepare_open(&tabs, "/tmp/other.txt", 0);
    CHECK(plan.editor != NULL);
    CHECK_EQ(plan.kind, TAB_OPEN_NEW);
    CHECK_EQ(tabs_count(&tabs), 2);
    tabs_cancel_open(&tabs, &plan);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK_EQ(tabs_active_index(&tabs), 0);
    tabs_free(&tabs);

    memset(&tabs, 0, sizeof tabs);
    const char *open_a = "/tmp/wave_tabs_open_a.txt";
    const char *open_b = "/tmp/wave_tabs_open_b.txt";
    const char *open_c = "/tmp/wave_tabs_open_c.txt";
    write_file(open_a, "alpha\n");
    write_file(open_b, "beta\n");
    write_file(open_c, "gamma\n");
    TabOpenResult open = tabs_open_file(&tabs, open_a, 0, NULL);
    CHECK(open.ok);
    CHECK(open.loaded_file);
    CHECK_EQ(open.kind, TAB_OPEN_NEW);
    CHECK(open.editor == tabs_current(&tabs));
    CHECK_STR(editor_path(open.editor), open_a);
    CHECK(!open.editor->preview);
    CHECK_EQ(tabs_count(&tabs), 1);

    open = tabs_open_file(&tabs, open_a, 1, NULL);
    CHECK(open.ok);
    CHECK(!open.loaded_file);
    CHECK_EQ(open.kind, TAB_OPEN_EXISTING);
    CHECK(open.editor == tabs_current(&tabs));
    CHECK(!open.editor->preview);
    CHECK_EQ(tabs_count(&tabs), 1);

    open = tabs_open_file(&tabs, open_b, 1, NULL);
    CHECK(open.ok);
    CHECK(open.loaded_file);
    CHECK_EQ(open.kind, TAB_OPEN_NEW);
    CHECK_STR(editor_path(open.editor), open_b);
    CHECK(open.editor->preview);
    CHECK_EQ(tabs_count(&tabs), 2);

    open = tabs_open_file(&tabs, open_c, 1, NULL);
    CHECK(open.ok);
    CHECK(open.loaded_file);
    CHECK_EQ(open.kind, TAB_OPEN_REUSE_PREVIEW);
    CHECK_STR(editor_path(open.editor), open_c);
    CHECK(open.editor->preview);
    CHECK_EQ(tabs_count(&tabs), 2);

    open = tabs_open_file(&tabs, "/tmp/does-not-exist-wave-tabs", 0, NULL);
    CHECK(!open.ok);
    CHECK(!open.loaded_file);
    CHECK(open.editor == NULL);
    CHECK_EQ(tabs_count(&tabs), 2);
    CHECK_STR(editor_path(tabs_current(&tabs)), open_c);
    tabs_free(&tabs);

    memset(&tabs, 0, sizeof tabs);
    TabStartupEffect startup = tabs_ensure_startup(&tabs, 1);
    CHECK(startup.editor != NULL);
    CHECK(!startup.enter_insert);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK(tabs_current(&tabs) == startup.editor);
    CHECK(startup.editor->buf == NULL);
    tabs_free(&tabs);

    memset(&tabs, 0, sizeof tabs);
    startup = tabs_ensure_startup(&tabs, 0);
    CHECK(startup.editor != NULL);
    CHECK(startup.enter_insert);
    CHECK_EQ(tabs_count(&tabs), 1);
    CHECK(startup.editor->buf != NULL);
    startup = tabs_ensure_startup(&tabs, 0);
    CHECK(tabs_count(&tabs) == 1);
    CHECK(!startup.enter_insert);
    tabs_free(&tabs);

    TEST_REPORT();
}
