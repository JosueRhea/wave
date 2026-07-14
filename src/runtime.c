#include "runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

WaveRuntimeOptions wave_runtime_options(const char *snapshot,
                                        const char *wave_lsp,
                                        const char *wave_persist,
                                        const char *wave_scale) {
    WaveRuntimeOptions opt = {0};
    opt.snapshot = snapshot != NULL;
    opt.lsp_disabled = opt.snapshot && !wave_lsp;
    opt.blink = !opt.snapshot;
    opt.persist = !opt.snapshot || wave_persist != NULL;
    if (wave_scale) {
        float v = (float)atof(wave_scale);
        if (v > 0.1f) {
            opt.has_scale_override = 1;
            opt.scale_override = v;
        }
    }
    return opt;
}

WaveRuntimeOpenList wave_runtime_open_list(const char *opens) {
    WaveRuntimeOpenList list = {0};
    if (!opens || !opens[0]) return list;

    const char *start = opens;
    for (const char *p = opens;; p++) {
        if (*p != ':' && *p != '\0') continue;
        size_t len = (size_t)(p - start);
        if (len > 0) {
            if (list.count >= WAVE_RUNTIME_MAX_OPEN_PATHS) {
                list.truncated = 1;
            } else {
                if (len >= WAVE_RUNTIME_PATH_CAP) {
                    len = WAVE_RUNTIME_PATH_CAP - 1;
                    list.truncated = 1;
                }
                memcpy(list.paths[list.count], start, len);
                list.paths[list.count][len] = '\0';
                list.count++;
            }
        }
        if (*p == '\0') break;
        start = p + 1;
    }
    return list;
}

static int positive_int(const char *text, int *out) {
    if (!text || !text[0] || !out) return 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (*end != '\0' || value < 1 || value > 2147483647L) return 0;
    *out = (int)value;
    return 1;
}

WaveRuntimeOpenRequest wave_runtime_open_request(int argc, char **argv) {
    WaveRuntimeOpenRequest request = {0};
    request.line = 1;
    request.column = 1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--line") == 0 || strcmp(arg, "-l") == 0) {
            if (++i >= argc || !positive_int(argv[i], &request.line)) return request;
            request.has_location = 1;
        } else if (strcmp(arg, "--column") == 0 || strcmp(arg, "-c") == 0) {
            if (++i >= argc || !positive_int(argv[i], &request.column)) return request;
            request.has_location = 1;
        } else if (arg[0] == '-') {
            return request;
        } else if (request.path) {
            return request;
        } else {
            request.path = arg;
        }
    }

    if (!request.path && request.has_location) return request;
    request.valid = 1;
    return request;
}

WaveRuntimeSnapshotScript wave_runtime_snapshot_script(
    const char *opens, const char *typed, const char *keys,
    const char *palette, const char *palette_query,
    const char *search_query, const char *search_selection,
    const char *popover_text, const char *popover_scroll) {
    WaveRuntimeSnapshotScript script = {0};
    script.opens = wave_runtime_open_list(opens);
    script.typed = typed;
    script.keys = keys;
    script.palette = palette != NULL;
    script.palette_query = palette_query;
    script.search_query = search_query;
    script.search_selection = wave_runtime_int_value(search_selection);
    script.popover_text = popover_text;
    script.popover_scroll = wave_runtime_int_value(popover_scroll);
    return script;
}

WaveRuntimeSnapshotPlan wave_runtime_snapshot_plan(WaveRuntimeSnapshotScript script,
                                                   int editor_has_buffer,
                                                   int palette_active,
                                                   int workspace_open) {
    WaveRuntimeSnapshotPlan plan = {0};
    plan.open_count = script.opens.count;
    plan.type_text = script.typed && editor_has_buffer;
    plan.normal_keys = script.keys && editor_has_buffer;
    plan.open_palette = script.palette;
    plan.set_palette_query = script.palette_query && palette_active;
    plan.run_search = script.search_query != NULL && workspace_open;
    plan.show_popover = script.popover_text && editor_has_buffer;
    return plan;
}

int wave_runtime_int_value(const char *text) {
    return text ? atoi(text) : 0;
}

double wave_runtime_wait_timeout(int lsp_active, int search_running) {
    return (lsp_active || search_running) ? 0.03 : 0.1;
}

int wave_runtime_periodic_due(double now, double *next_time, double interval) {
    if (!next_time) return 1;
    if (now < *next_time) return 0;
    *next_time = now + interval;
    return 1;
}

char *path_to_uri(const char *path) {
    char abs[4096];
    if (path[0] == '/') snprintf(abs, sizeof abs, "%s", path);
    else if (!realpath(path, abs)) snprintf(abs, sizeof abs, "%s", path);
    size_t n = strlen(abs);
    char *u = malloc(n * 3 + 8);
    strcpy(u, "file://");
    size_t o = 7;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)abs[i];
        if (c == '/' || isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            u[o++] = (char)c;
        else { sprintf(u + o, "%%%02X", c); o += 3; }
    }
    u[o] = '\0';
    return u;
}

static int exe_dir(char *out, size_t cap) {
    char buf[4096];
#ifdef __APPLE__
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) != 0) return 0;
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
#endif
    char real[4096];
    if (!realpath(buf, real)) snprintf(real, sizeof real, "%s", buf);
    char *slash = strrchr(real, '/');
    if (slash) *slash = '\0';
    snprintf(out, cap, "%s", real);
    return 1;
}

int find_on_path(const char *name, char *out, size_t cap) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    size_t nlen = strlen(name);
    while (*path) {
        const char *colon = strchr(path, ':');
        size_t dlen = colon ? (size_t)(colon - path) : strlen(path);
        if (dlen + 1 + nlen + 1 < cap) {
            memcpy(out, path, dlen);
            out[dlen] = '/';
            memcpy(out + dlen + 1, name, nlen + 1);
            if (access(out, X_OK) == 0) return 1;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

static int ts_server_cli(char *out, size_t cap) {
    char dir[4096];
    if (!exe_dir(dir, sizeof dir)) return 0;
    static const char *rel[] = {
        "../Resources/vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        "vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        "../vendor/lsp/node_modules/typescript-language-server/lib/cli.mjs",
        NULL,
    };
    for (int i = 0; rel[i]; i++) {
        snprintf(out, cap, "%s/%s", dir, rel[i]);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

int wave_rg_binary(char *out, size_t cap) {
    char dir[4096];
    if (exe_dir(dir, sizeof dir)) {
        static const char *rel[] = {
            "../Resources/vendor/rg/rg",
            "vendor/rg/rg",
            "../vendor/rg/rg",
            NULL,
        };
        for (int i = 0; rel[i]; i++) {
            snprintf(out, cap, "%s/%s", dir, rel[i]);
            if (access(out, X_OK) == 0) return 1;
        }
    }
    return find_on_path("rg", out, cap);
}

const char *lsp_language_id(const char *name) {
    if (!strcmp(name, "tsx")) return "typescriptreact";
    if (!strcmp(name, "javascript")) return "javascript";
    if (!strcmp(name, "typescript")) return "typescript";
    return name;
}

const char *lsp_server_argv(const char *name, const char *argv[5],
                            char *rt, size_t rtcap, char *cli, size_t clicap) {
    if (!strcmp(name, "c")) {
        argv[0] = "clangd";
        argv[1] = "--background-index";
        argv[2] = "--header-insertion=never";
        argv[3] = "--log=error";
        argv[4] = NULL;
        return "clangd";
    }
    if ((find_on_path("bun", rt, rtcap) || find_on_path("node", rt, rtcap)) &&
        ts_server_cli(cli, clicap)) {
        argv[0] = rt;
        argv[1] = cli;
        argv[2] = "--stdio";
        argv[3] = NULL;
    } else {
        argv[0] = "typescript-language-server";
        argv[1] = "--stdio";
        argv[2] = NULL;
    }
    return "tsls";
}
