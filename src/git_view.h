#ifndef WAVE_GIT_VIEW_H
#define WAVE_GIT_VIEW_H

#include <stddef.h>

#define GIT_VIEW_MAX_REPOS 64
#define GIT_VIEW_MAX_FILES 256
#define GIT_VIEW_MAX_LINES 2048
#define GIT_VIEW_MAX_PATH 4096
#define GIT_VIEW_MAX_STATUS 8

typedef enum {
    GIT_VIEW_REPO_SELECT,
    GIT_VIEW_CHANGES,
    GIT_VIEW_COMMIT_INPUT
} GitViewMode;

typedef struct {
    char path[GIT_VIEW_MAX_PATH];
    char label[256];
} GitRepoChoice;

typedef struct {
    char code[GIT_VIEW_MAX_STATUS];
    char path[GIT_VIEW_MAX_PATH];
} GitFileChange;

typedef struct {
    GitViewMode mode;
    char root[GIT_VIEW_MAX_PATH];
    char repo[GIT_VIEW_MAX_PATH];
    GitRepoChoice repos[GIT_VIEW_MAX_REPOS];
    int repo_count;
    int selected_repo;
    GitFileChange files[GIT_VIEW_MAX_FILES];
    int file_count;
    int selected_file;
    char diff[GIT_VIEW_MAX_LINES][512];
    int diff_count;
    char history[32][256];
    int history_count;
    char message[256];
    int message_len;
    char info[256];
    int scroll;
    int diff_scroll;
    int diff_selection_active;
    int diff_selection_dragging;
    int diff_selection_anchor_line;
    int diff_selection_head_line;
    int diff_selection_anchor_col;
    int diff_selection_head_col;
} GitView;

void git_view_init(GitView *view);
void git_view_free(GitView *view);
int git_view_open(GitView *view, const char *root);
int git_view_refresh(GitView *view);
void git_view_move(GitView *view, int delta);
int git_view_select_repo(GitView *view, int index);
int git_view_select_file(GitView *view, int index);
void git_view_diff_scroll(GitView *view, int delta);
int git_view_accept(GitView *view);
int git_view_stage_toggle(GitView *view);
int git_view_begin_commit(GitView *view);
int git_view_commit(GitView *view);
void git_view_cancel_input(GitView *view);
int git_view_insert_char(GitView *view, unsigned int cp);
int git_view_insert_text(GitView *view, const char *text);
int git_view_backspace(GitView *view);
int git_view_repo_selecting(const GitView *view);
const GitFileChange *git_view_selected_file(const GitView *view);
void git_view_diff_selection_clear(GitView *view);
void git_view_diff_selection_begin(GitView *view, int line, int col);
void git_view_diff_selection_update(GitView *view, int line, int col);
void git_view_diff_selection_end(GitView *view);
int git_view_diff_selection_span(const GitView *view, int line, int *start_col,
                                 int *end_col);
char *git_view_copy_diff_selection(const GitView *view);

#endif
