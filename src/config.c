#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void wave_config_defaults(WaveConfig *cfg) {
    if (!cfg) return;
    cfg->show_sidebar = 1;
    cfg->side_cells = 26;
    cfg->wrap = 1;
    cfg->base_pt = 15.0f;
    cfg->ui_scale = 1.0f;
    cfg->opacity = 1.0f;
    cfg->blur = 0;
    cfg->native_titlebar = 1;
}

void wave_config_path(char *out, size_t cap) {
    const char *home = getenv("HOME");
    snprintf(out, cap, "%s/.config/wave/config", home ? home : ".");
}

int wave_config_save(const WaveConfig *cfg) {
    if (!cfg) return -1;
    const char *home = getenv("HOME");
    if (!home) return -1;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/.config", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/.config/wave", home);
    mkdir(dir, 0755);

    char path[1100];
    wave_config_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# wave config\n");
    fprintf(f, "wrap=%d\n", cfg->wrap);
    fprintf(f, "scale=%.3f\n", cfg->ui_scale);
    fprintf(f, "sidebar=%d\n", cfg->show_sidebar);
    fprintf(f, "side_cells=%d\n", cfg->side_cells);
    fprintf(f, "opacity=%.3f\n", cfg->opacity);
    fprintf(f, "blur=%d\n", cfg->blur);
    fprintf(f, "titlebar=%d\n", cfg->native_titlebar);
    fclose(f);
    return 0;
}

int wave_config_zoom(WaveConfig *cfg, int dir) {
    if (!cfg) return 0;
    if (dir == 0) cfg->ui_scale = 1.0f;
    else cfg->ui_scale *= (dir > 0) ? 1.1f : (1.0f / 1.1f);
    if (cfg->ui_scale < 0.5f) cfg->ui_scale = 0.5f;
    if (cfg->ui_scale > 3.0f) cfg->ui_scale = 3.0f;
    return (int)(cfg->ui_scale * 100.0f + 0.5f);
}

int wave_config_toggle_sidebar(WaveConfig *cfg) {
    if (!cfg) return 0;
    cfg->show_sidebar = !cfg->show_sidebar;
    return cfg->show_sidebar;
}

int wave_config_toggle_wrap(WaveConfig *cfg) {
    if (!cfg) return 0;
    cfg->wrap = !cfg->wrap;
    return cfg->wrap;
}

int wave_config_zoom_text(const WaveConfig *cfg, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    int percent = cfg ? (int)(cfg->ui_scale * 100.0f + 0.5f) : 0;
    snprintf(out, cap, "zoom %d%%", percent);
    return 1;
}

int wave_config_wrap_text(const WaveConfig *cfg, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    snprintf(out, cap, "word wrap %s", (cfg && cfg->wrap) ? "on" : "off");
    return 1;
}

void wave_config_load(WaveConfig *cfg) {
    if (!cfg) return;
    char path[1100];
    wave_config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        char *nl = strpbrk(val, "\r\n");
        if (nl) *nl = '\0';

        if (!strcmp(key, "wrap")) cfg->wrap = atoi(val) != 0;
        else if (!strcmp(key, "scale")) {
            float v = (float)atof(val);
            if (v >= 0.5f && v <= 3.0f) cfg->ui_scale = v;
        } else if (!strcmp(key, "sidebar")) cfg->show_sidebar = atoi(val) != 0;
        else if (!strcmp(key, "side_cells")) {
            int v = atoi(val);
            if (v >= 8 && v <= 80) cfg->side_cells = v;
        } else if (!strcmp(key, "opacity")) {
            float v = (float)atof(val);
            if (v >= 0.2f && v <= 1.0f) cfg->opacity = v;
        } else if (!strcmp(key, "blur")) cfg->blur = atoi(val) != 0;
        else if (!strcmp(key, "titlebar")) cfg->native_titlebar = atoi(val) != 0;
    }
    fclose(f);
}
