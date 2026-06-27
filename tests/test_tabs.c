/* test_tabs.c — tab set behavior independent of the GLFW shell. */
#include "test.h"
#include "tabs.h"

#include <stdlib.h>
#include <string.h>

static char *dupstr(const char *s) {
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
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

    TEST_REPORT();
}
