/* test_workspace.c — scanning order and the collapse/expand visible view. */
#include "test.h"
#include "workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Build a small tree under a fresh temp dir:
 *   root/
 *     a/           (dir)
 *       a1.txt
 *       sub/       (dir)
 *         deep.txt
 *     b/           (dir)
 *       b1.txt
 *     top.txt
 */
static char *make_tree(void) {
    static char root[256];
    snprintf(root, sizeof root, "/tmp/wave_ws_test_%d", (int)getpid());
    char p[512];
    mkdir(root, 0755);
    snprintf(p, sizeof p, "%s/a", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/a/sub", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/b", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/a/a1.txt", root); fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/a/sub/deep.txt", root); fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/b/b1.txt", root); fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/top.txt", root); fclose(fopen(p, "w"));
    return root;
}

/* find the visible row whose name matches, or -1 */
static int row_of(Workspace *w, const char *name) {
    for (size_t i = 0; i < ws_visible_count(w); i++)
        if (!strcmp(ws_visible(w, i)->name, name)) return (int)i;
    return -1;
}

int main(void) {
    char *root = make_tree();
    Workspace *w = ws_open(root);
    CHECK(w != NULL);

    /* entries (pre-order): a, a/a1.txt, a/sub, a/sub/deep.txt, b, b/b1.txt,
     * top.txt = 7 are all scanned... */
    CHECK_EQ(ws_count(w), 7);
    /* ...but folders start collapsed, so only the top-level rows show:
     * a, b, top.txt = 3 */
    CHECK_EQ(ws_visible_count(w), 3);
    CHECK_STR(ws_visible(w, 0)->name, "a");
    CHECK_EQ(ws_visible(w, 0)->is_dir, 1);
    CHECK_EQ(ws_visible(w, 0)->collapsed, 1);
    CHECK_EQ(row_of(w, "a1.txt"), -1);
    CHECK_EQ(row_of(w, "b1.txt"), -1);
    CHECK_EQ(row_of(w, "deep.txt"), -1);

    /* expand "a": its direct children appear (a1.txt, sub), but "sub" is itself
     * still collapsed so deep.txt stays hidden. -> a, a1.txt, sub, b, top.txt */
    int ra = row_of(w, "a");
    CHECK(ra >= 0);
    ws_visible_toggle(w, (size_t)ra);
    CHECK_EQ(ws_visible_count(w), 5);
    CHECK_EQ(ws_visible(w, (size_t)row_of(w, "a"))->collapsed, 0);
    CHECK(row_of(w, "a1.txt") >= 0);
    CHECK(row_of(w, "sub") >= 0);
    CHECK_EQ(row_of(w, "deep.txt"), -1);
    CHECK(row_of(w, "b") >= 0);
    CHECK_EQ(row_of(w, "b1.txt"), -1);          /* b still collapsed */

    /* expand "sub": deep.txt finally appears. -> 6 visible */
    ws_visible_toggle(w, (size_t)row_of(w, "sub"));
    CHECK_EQ(ws_visible_count(w), 6);
    CHECK(row_of(w, "deep.txt") >= 0);

    /* collapse "a" again: its whole subtree (a1.txt, sub, deep.txt) disappears,
     * "b" and "top.txt" remain. -> a, b, top.txt = 3 */
    ws_visible_toggle(w, (size_t)row_of(w, "a"));
    CHECK_EQ(ws_visible_count(w), 3);
    CHECK_EQ(row_of(w, "a1.txt"), -1);
    CHECK_EQ(row_of(w, "deep.txt"), -1);

    /* collapsing a file is a no-op */
    int rt = row_of(w, "top.txt");
    ws_visible_toggle(w, (size_t)rt);
    CHECK_EQ(ws_visible_count(w), 3);

    /* expand "b": its child shows up independently of "a" */
    ws_visible_toggle(w, (size_t)row_of(w, "b"));
    CHECK(row_of(w, "b1.txt") >= 0);
    CHECK_EQ(row_of(w, "a1.txt"), -1);          /* a still collapsed */

    /* sidebar click actions: folders toggle in-place; files tell the caller
     * whether to open a preview or pinned tab. */
    int rb = row_of(w, "b");
    CHECK(rb >= 0);
    WsClickAction click = ws_click_visible(w, rb, 0);
    CHECK_EQ(click.kind, WS_CLICK_TOGGLE_DIR);
    CHECK_EQ(row_of(w, "b1.txt"), -1);
    click = ws_click_visible(w, rb, 0);
    CHECK_EQ(click.kind, WS_CLICK_TOGGLE_DIR);
    int rb1 = row_of(w, "b1.txt");
    CHECK(rb1 >= 0);
    click = ws_click_visible(w, rb1, 0);
    CHECK_EQ(click.kind, WS_CLICK_OPEN_FILE);
    CHECK(click.entry != NULL);
    CHECK_STR(click.entry->rel, "b/b1.txt");
    CHECK_EQ(click.preview, 1);
    click = ws_click_visible(w, rb1, 1);
    CHECK_EQ(click.kind, WS_CLICK_OPEN_FILE);
    CHECK_EQ(click.preview, 0);
    click = ws_click_visible(w, -1, 0);
    CHECK_EQ(click.kind, WS_CLICK_NONE);
    WsSidebarClickState sidebar_click = {0};
    click = ws_click_visible_timed(w, &sidebar_click, rb1, 10.0, 0.4);
    CHECK_EQ(click.kind, WS_CLICK_OPEN_FILE);
    CHECK_EQ(click.preview, 1);
    CHECK(sidebar_click.seen);
    click = ws_click_visible_timed(w, &sidebar_click, rb1, 10.2, 0.4);
    CHECK_EQ(click.kind, WS_CLICK_OPEN_FILE);
    CHECK_EQ(click.preview, 0);
    click = ws_click_visible_timed(w, &sidebar_click, rb1, 11.0, 0.4);
    CHECK_EQ(click.kind, WS_CLICK_OPEN_FILE);
    CHECK_EQ(click.preview, 1);
    click = ws_click_visible_timed(w, &sidebar_click, -1, 11.1, 0.4);
    CHECK_EQ(click.kind, WS_CLICK_NONE);
    CHECK(sidebar_click.seen);

    /* reload after external tree changes: new files appear, removed files go
     * away, and expanded directories stay expanded. */
    char p[512];
    snprintf(p, sizeof p, "%s/b/new.txt", root); fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/top.txt", root); unlink(p);
    CHECK_EQ(ws_reload(w), 0);
    CHECK(row_of(w, "b1.txt") >= 0);            /* b stayed expanded */
    CHECK(row_of(w, "new.txt") >= 0);
    CHECK_EQ(row_of(w, "top.txt"), -1);
    CHECK_EQ(row_of(w, "a1.txt"), -1);          /* a stayed collapsed */
    WsReloadEffect reload = ws_apply_reload(w);
    CHECK(reload.ok);
    CHECK(reload.refilter_palette);
    CHECK_STR(reload.message, "workspace updated");
    reload = ws_apply_watch_event(w, 0);
    CHECK(!reload.ok);
    CHECK(!reload.refilter_palette);
    CHECK_STR(reload.message, "");
    reload = ws_apply_watch_event(w, 1);
    CHECK(reload.ok);
    CHECK(reload.refilter_palette);
    CHECK_STR(reload.message, "workspace updated");

    ws_free(w);

    reload = ws_apply_reload(NULL);
    CHECK(!reload.ok);
    CHECK(!reload.refilter_palette);
    CHECK_STR(reload.message, "workspace unavailable: ");
    reload = ws_apply_watch_event(NULL, 1);
    CHECK(!reload.ok);
    CHECK(!reload.refilter_palette);
    CHECK_STR(reload.message, "");

    WsOpenContext ctx = ws_open_context(root);
    CHECK_EQ(ctx.kind, WS_OPEN_WORKSPACE);
    CHECK(ctx.workspace != NULL);
    CHECK_STR(ws_root(ctx.workspace), root);
    CHECK_STR(ctx.file, "");
    ws_free(ctx.workspace);

    snprintf(p, sizeof p, "%s/a/a1.txt", root);
    ctx = ws_open_context(p);
    CHECK_EQ(ctx.kind, WS_OPEN_FILE);
    CHECK(ctx.workspace != NULL);
    char parent[512];
    snprintf(parent, sizeof parent, "%s/a", root);
    CHECK_STR(ws_root(ctx.workspace), parent);
    CHECK_STR(ctx.file, p);
    ws_free(ctx.workspace);

    ctx = ws_open_context(NULL);
    CHECK_EQ(ctx.kind, WS_OPEN_NONE);
    CHECK(ctx.workspace == NULL);

    TEST_REPORT();
}
