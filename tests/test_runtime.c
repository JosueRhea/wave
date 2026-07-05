/* test_runtime.c - startup/runtime option policy. */
#include "test.h"
#include "runtime.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    WaveRuntimeOptions opt = wave_runtime_options(NULL, NULL, NULL, NULL);
    CHECK(!opt.snapshot);
    CHECK(!opt.lsp_disabled);
    CHECK(opt.blink);
    CHECK(opt.persist);
    CHECK(!opt.has_scale_override);

    opt = wave_runtime_options("/tmp/out.ppm", NULL, NULL, NULL);
    CHECK(opt.snapshot);
    CHECK(opt.lsp_disabled);
    CHECK(!opt.blink);
    CHECK(!opt.persist);

    opt = wave_runtime_options("/tmp/out.ppm", "1", "1", "1.25");
    CHECK(opt.snapshot);
    CHECK(!opt.lsp_disabled);
    CHECK(!opt.blink);
    CHECK(opt.persist);
    CHECK(opt.has_scale_override);
    CHECK_EQ((int)(opt.scale_override * 100.0f + 0.5f), 125);

    opt = wave_runtime_options(NULL, NULL, NULL, "0.05");
    CHECK(!opt.has_scale_override);
    opt = wave_runtime_options(NULL, NULL, NULL, "bogus");
    CHECK(!opt.has_scale_override);

    WaveRuntimeOpenList opens = wave_runtime_open_list(NULL);
    CHECK_EQ(opens.count, 0);
    CHECK(!opens.truncated);

    opens = wave_runtime_open_list("/a:/b::/c:");
    CHECK_EQ(opens.count, 3);
    CHECK_STR(opens.paths[0], "/a");
    CHECK_STR(opens.paths[1], "/b");
    CHECK_STR(opens.paths[2], "/c");
    CHECK(!opens.truncated);

    char many[1024] = "";
    for (int i = 0; i < WAVE_RUNTIME_MAX_OPEN_PATHS + 2; i++) {
        size_t n = strlen(many);
        snprintf(many + n, sizeof many - n, "%sp%d", n ? ":" : "", i);
    }
    opens = wave_runtime_open_list(many);
    CHECK_EQ(opens.count, WAVE_RUNTIME_MAX_OPEN_PATHS);
    CHECK(opens.truncated);
    CHECK_STR(opens.paths[0], "p0");

    char long_path[WAVE_RUNTIME_PATH_CAP + 32];
    memset(long_path, 'x', sizeof long_path);
    long_path[sizeof long_path - 1] = '\0';
    opens = wave_runtime_open_list(long_path);
    CHECK_EQ(opens.count, 1);
    CHECK(opens.truncated);
    CHECK_EQ(strlen(opens.paths[0]), WAVE_RUNTIME_PATH_CAP - 1);

    WaveRuntimeSnapshotScript script = wave_runtime_snapshot_script(
        "/a:/b", "x\\ny", "gg", "1", "main", "needle", "3", "hello|world", "2");
    CHECK_EQ(script.opens.count, 2);
    CHECK_STR(script.opens.paths[0], "/a");
    CHECK_STR(script.opens.paths[1], "/b");
    CHECK_STR(script.typed, "x\\ny");
    CHECK_STR(script.keys, "gg");
    CHECK(script.palette);
    CHECK_STR(script.palette_query, "main");
    CHECK_STR(script.search_query, "needle");
    CHECK_EQ(script.search_selection, 3);
    CHECK_STR(script.popover_text, "hello|world");
    CHECK_EQ(script.popover_scroll, 2);
    script = wave_runtime_snapshot_script(NULL, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL, NULL);
    CHECK_EQ(script.opens.count, 0);
    CHECK(!script.palette);
    CHECK(script.typed == NULL);
    CHECK_EQ(script.search_selection, 0);
    CHECK_EQ(script.popover_scroll, 0);

    script = wave_runtime_snapshot_script(
        "/a:/b", "typed", "dd", "1", "main", "needle", "2", "hover", "4");
    WaveRuntimeSnapshotPlan plan = wave_runtime_snapshot_plan(script, 0, 0, 0);
    CHECK_EQ(plan.open_count, 2);
    CHECK(!plan.type_text);
    CHECK(!plan.normal_keys);
    CHECK(plan.open_palette);
    CHECK(!plan.set_palette_query);
    CHECK(!plan.run_search);
    CHECK(!plan.show_popover);

    plan = wave_runtime_snapshot_plan(script, 1, 1, 1);
    CHECK_EQ(plan.open_count, 2);
    CHECK(plan.type_text);
    CHECK(plan.normal_keys);
    CHECK(plan.open_palette);
    CHECK(plan.set_palette_query);
    CHECK(plan.run_search);
    CHECK(plan.show_popover);

    plan = wave_runtime_snapshot_plan(script, 1, 0, 1);
    CHECK(!plan.set_palette_query);
    CHECK(plan.run_search);

    CHECK_EQ(wave_runtime_int_value(NULL), 0);
    CHECK_EQ(wave_runtime_int_value("12"), 12);
    CHECK_EQ(wave_runtime_int_value("-4"), -4);
    CHECK_EQ(wave_runtime_int_value("7px"), 7);
    CHECK_EQ(wave_runtime_int_value("bogus"), 0);

    CHECK_EQ((int)(wave_runtime_wait_timeout(0, 0) * 100.0 + 0.5), 10);
    CHECK_EQ((int)(wave_runtime_wait_timeout(1, 0) * 100.0 + 0.5), 3);
    CHECK_EQ((int)(wave_runtime_wait_timeout(0, 1) * 100.0 + 0.5), 3);
    CHECK_EQ((int)(wave_runtime_wait_timeout(1, 1) * 100.0 + 0.5), 3);

    double next = 0.0;
    CHECK(wave_runtime_periodic_due(0.0, &next, 0.5));
    CHECK_EQ((int)(next * 10.0 + 0.5), 5);
    CHECK(!wave_runtime_periodic_due(0.25, &next, 0.5));
    CHECK_EQ((int)(next * 10.0 + 0.5), 5);
    CHECK(wave_runtime_periodic_due(0.5, &next, 0.5));
    CHECK_EQ((int)(next * 10.0 + 0.5), 10);
    CHECK(wave_runtime_periodic_due(3.0, NULL, 0.5));

    TEST_REPORT();
}
