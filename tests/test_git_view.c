#include "test.h"
#include "git_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void run_cmd(const char *cmd) {
    int rc = system(cmd);
    CHECK_EQ(rc, 0);
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

int main(void) {
    char root_template[] = "/tmp/wave-git-view-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);

    char repo[4096];
    char file[4096];
    char cmd[8192];
    snprintf(repo, sizeof repo, "%s/child", root);
    snprintf(file, sizeof file, "%s/readme.txt", repo);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s' && git -C '%s' init -q", repo, repo);
    run_cmd(cmd);
    snprintf(cmd, sizeof cmd, "git -C '%s' config user.email wave@example.com && git -C '%s' config user.name Wave", repo, repo);
    run_cmd(cmd);
    write_file(file, "hello\n");
    snprintf(cmd, sizeof cmd, "git -C '%s' add readme.txt && git -C '%s' commit -q -m initial", repo, repo);
    run_cmd(cmd);
    write_file(file, "hello\nworld\n");

    GitView view;
    CHECK(git_view_open(&view, root));
    CHECK(git_view_repo_selecting(&view));
    CHECK_EQ(view.repo_count, 1);
    CHECK(git_view_accept(&view));
    CHECK_EQ(view.mode, GIT_VIEW_CHANGES);
    CHECK_EQ(view.file_count, 1);
    CHECK_STR(view.files[0].path, "readme.txt");
    CHECK(view.diff_count > 0);
    CHECK(git_view_select_file(&view, 0));
    CHECK(!git_view_select_file(&view, 99));
    int diff_line = -1;
    for (int i = 0; i < view.diff_count; i++) {
        if (strstr(view.diff[i], "world")) {
            diff_line = i;
            break;
        }
    }
    CHECK(diff_line >= 0);
    git_view_diff_selection_begin(&view, diff_line, 1);
    git_view_diff_selection_update(&view, diff_line, 6);
    git_view_diff_selection_end(&view);
    int start_col = 0, end_col = 0;
    CHECK(git_view_diff_selection_span(&view, diff_line, &start_col, &end_col));
    CHECK_EQ(start_col, 1);
    CHECK_EQ(end_col, 6);
    char *selected = git_view_copy_diff_selection(&view);
    CHECK(selected != NULL);
    CHECK_STR(selected, "world");
    free(selected);

    CHECK(git_view_stage_toggle(&view));
    CHECK(view.files[0].code[0] != ' ');
    CHECK(git_view_begin_commit(&view));
    CHECK_EQ(view.mode, GIT_VIEW_COMMIT_INPUT);
    CHECK(git_view_insert_text(&view, "update readme"));
    CHECK(git_view_backspace(&view));
    CHECK(git_view_insert_char(&view, 'e'));
    CHECK(git_view_commit(&view));
    CHECK_EQ(view.mode, GIT_VIEW_CHANGES);
    CHECK_EQ(view.file_count, 0);
    CHECK(view.history_count > 0);
    CHECK(strstr(view.history[0], "update readme") != NULL);

    git_view_free(&view);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    run_cmd(cmd);

    TEST_REPORT();
}
