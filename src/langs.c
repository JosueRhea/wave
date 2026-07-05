/* langs.c - see langs.h. */
#include "langs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* Grammar entry points, linked from the vendored parsers. */
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_javascript(void);
const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);

static Language registry[4];
static int registry_ready;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
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

static char *load_query_for(const char *name) {
    char path[4096];

    snprintf(path, sizeof path, "queries/%s/highlights.scm", name);
    char *query = read_file(path);
    if (query) return query;

    char dir[4096];
    if (!exe_dir(dir, sizeof dir)) return NULL;
    static const char *rel[] = {
        "../Resources/queries/%s/highlights.scm",
        "queries/%s/highlights.scm",
        "../queries/%s/highlights.scm",
        NULL,
    };
    for (int i = 0; rel[i]; i++) {
        snprintf(path, sizeof path, "%s/", dir);
        size_t off = strlen(path);
        snprintf(path + off, sizeof path - off, rel[i], name);
        query = read_file(path);
        if (query) return query;
    }
    return NULL;
}

static void registry_init(void) {
    registry[0] = (Language){"c", tree_sitter_c(), NULL};
    registry[1] = (Language){"javascript", tree_sitter_javascript(), NULL};
    registry[2] = (Language){"typescript", tree_sitter_typescript(), NULL};
    registry[3] = (Language){"tsx", tree_sitter_tsx(), NULL};
    registry_ready = 1;
}

static const char *ext_of(const char *path) {
    const char *dot = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') dot = NULL;
        else if (*p == '.') dot = p + 1;
    }
    return dot;
}

static Language *ensure_query(Language *lang) {
    if (lang && !lang->query) lang->query = load_query_for(lang->name);
    return lang;
}

const Language *lang_detect(const char *path) {
    if (!path) return NULL;
    if (!registry_ready) registry_init();
    const char *ext = ext_of(path);
    if (!ext) return NULL;

    if (!strcmp(ext, "c") || !strcmp(ext, "h")) return ensure_query(&registry[0]);
    if (!strcmp(ext, "js") || !strcmp(ext, "jsx") || !strcmp(ext, "mjs") ||
        !strcmp(ext, "cjs"))
        return ensure_query(&registry[1]);
    if (!strcmp(ext, "ts") || !strcmp(ext, "mts") || !strcmp(ext, "cts"))
        return ensure_query(&registry[2]);
    if (!strcmp(ext, "tsx")) return ensure_query(&registry[3]);
    return NULL;
}
