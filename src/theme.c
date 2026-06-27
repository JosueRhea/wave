#include "theme.h"

#include <string.h>

Color theme_color(const char *name) {
    if (!name) return (Color){0.85f, 0.86f, 0.88f};
    if (!strcmp(name, "keyword")) return (Color){0.86f, 0.51f, 0.78f};
    if (!strcmp(name, "type")) return (Color){0.46f, 0.71f, 0.96f};
    if (!strcmp(name, "string")) return (Color){0.62f, 0.80f, 0.42f};
    if (!strcmp(name, "number")) return (Color){0.90f, 0.62f, 0.36f};
    if (!strcmp(name, "constant")) return (Color){0.90f, 0.62f, 0.36f};
    if (!strcmp(name, "comment")) return (Color){0.42f, 0.46f, 0.52f};
    if (!strcmp(name, "function")) return (Color){0.92f, 0.80f, 0.45f};
    if (!strcmp(name, "property")) return (Color){0.60f, 0.78f, 0.90f};
    return (Color){0.85f, 0.86f, 0.88f};
}
