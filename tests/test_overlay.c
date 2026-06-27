/* test_overlay.c — palette/search overlay routing without GLFW. */
#include "test.h"
#include "overlay.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    OverlayState overlay;
    overlay_init(&overlay);

    CHECK_EQ(overlay_active(&overlay), OVERLAY_NONE);
    CHECK(!overlay_open_palette(&overlay, NULL));
    CHECK_EQ(overlay_active(&overlay), OVERLAY_NONE);

    CHECK(overlay_open_search(&overlay));
    CHECK_EQ(overlay_active(&overlay), OVERLAY_SEARCH);
    overlay_insert_text(&overlay, NULL, NULL, "abc\x01!");
    CHECK_STR(overlay_query(&overlay), "abc!");
    overlay_backspace(&overlay, NULL, NULL);
    CHECK_STR(overlay_query(&overlay), "abc");
    overlay_move(&overlay, 1);
    CHECK_EQ(overlay.search.sel, 0);
    CHECK(!overlay_search_running(&overlay));

    overlay_close(&overlay);
    CHECK_EQ(overlay_active(&overlay), OVERLAY_NONE);

    overlay.active = OVERLAY_PALETTE;
    overlay_insert_text(&overlay, NULL, NULL, "pal");
    CHECK_STR(overlay_query(&overlay), "pal");
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_BACKSPACE),
             OVERLAY_KEY_RESULT_HANDLED);
    CHECK_STR(overlay_query(&overlay), "pa");
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_DOWN),
             OVERLAY_KEY_RESULT_HANDLED);
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_UP),
             OVERLAY_KEY_RESULT_HANDLED);
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_ACCEPT),
             OVERLAY_KEY_RESULT_ACCEPT);
    CHECK_EQ(overlay_active(&overlay), OVERLAY_PALETTE);
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_NONE),
             OVERLAY_KEY_RESULT_NONE);
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_ESCAPE),
             OVERLAY_KEY_RESULT_HANDLED);
    CHECK_EQ(overlay_active(&overlay), OVERLAY_NONE);
    CHECK_EQ(overlay_apply_key(&overlay, NULL, NULL, OVERLAY_KEY_ESCAPE),
             OVERLAY_KEY_RESULT_NONE);

    char root[256];
    char file[512];
    snprintf(root, sizeof root, "/tmp/wave_overlay_test_%d", (int)getpid());
    mkdir(root, 0755);
    snprintf(file, sizeof file, "%s/target.txt", root);
    fclose(fopen(file, "w"));

    Workspace *ws = ws_open(root);
    CHECK(ws != NULL);
    CHECK(overlay_open_palette(&overlay, ws));
    overlay_set_palette_query(&overlay, ws, "target");
    OverlayAcceptTarget target = overlay_accept_target(&overlay, ws);
    CHECK(target.has_target);
    CHECK(!target.has_location);
    CHECK_STR(target.path, file);
    overlay_accept_target_free(&target);
    CHECK(!target.has_target);
    CHECK(target.path == NULL);
    ws_free(ws);

    target = overlay_accept_target(&overlay, NULL);
    CHECK(!target.has_target);

    overlay_free(&overlay);
    CHECK_EQ(overlay_active(&overlay), OVERLAY_NONE);

    TEST_REPORT();
}
