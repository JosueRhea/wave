#ifndef WAVE_RUNTIME_H
#define WAVE_RUNTIME_H

#include <stddef.h>

#define WAVE_RUNTIME_MAX_OPEN_PATHS 32
#define WAVE_RUNTIME_PATH_CAP 4096

typedef struct {
    int snapshot;
    int lsp_disabled;
    int blink;
    int persist;
    int has_scale_override;
    float scale_override;
} WaveRuntimeOptions;

typedef struct {
    int count;
    int truncated;
    char paths[WAVE_RUNTIME_MAX_OPEN_PATHS][WAVE_RUNTIME_PATH_CAP];
} WaveRuntimeOpenList;

typedef struct {
    WaveRuntimeOpenList opens;
    const char *typed;
    const char *keys;
    int palette;
    const char *palette_query;
    const char *search_query;
    int search_selection;
    const char *popover_text;
    int popover_scroll;
} WaveRuntimeSnapshotScript;

typedef struct {
    int open_count;
    int type_text;
    int normal_keys;
    int open_palette;
    int set_palette_query;
    int run_search;
    int show_popover;
} WaveRuntimeSnapshotPlan;

WaveRuntimeOptions wave_runtime_options(const char *snapshot,
                                        const char *wave_lsp,
                                        const char *wave_persist,
                                        const char *wave_scale);
WaveRuntimeOpenList wave_runtime_open_list(const char *opens);
WaveRuntimeSnapshotScript wave_runtime_snapshot_script(
    const char *opens, const char *typed, const char *keys,
    const char *palette, const char *palette_query,
    const char *search_query, const char *search_selection,
    const char *popover_text, const char *popover_scroll);
WaveRuntimeSnapshotPlan wave_runtime_snapshot_plan(WaveRuntimeSnapshotScript script,
                                                   int editor_has_buffer,
                                                   int palette_active,
                                                   int workspace_open);
int wave_runtime_int_value(const char *text);
double wave_runtime_wait_timeout(int lsp_active, int search_running);
int wave_runtime_periodic_due(double now, double *next_time, double interval);
char *path_to_uri(const char *path);
int find_on_path(const char *name, char *out, size_t cap);
int wave_rg_binary(char *out, size_t cap);
const char *lsp_language_id(const char *name);
const char *lsp_server_argv(const char *name, const char *argv[5],
                            char *rt, size_t rtcap, char *cli, size_t clicap);

#endif
