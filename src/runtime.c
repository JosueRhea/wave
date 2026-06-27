#include "runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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
