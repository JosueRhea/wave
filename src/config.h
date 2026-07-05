#ifndef WAVE_CONFIG_H
#define WAVE_CONFIG_H

#include <stddef.h>

typedef struct {
    int show_sidebar;
    int side_cells;
    int wrap;
    float base_pt;
    float ui_scale;
    float opacity;
    float radius;
    int blur;
    int native_titlebar;
} WaveConfig;

void wave_config_defaults(WaveConfig *cfg);
void wave_config_path(char *out, size_t cap);
void wave_config_load(WaveConfig *cfg);
int wave_config_save(const WaveConfig *cfg);
int wave_config_zoom(WaveConfig *cfg, int dir);
int wave_config_toggle_sidebar(WaveConfig *cfg);
int wave_config_toggle_wrap(WaveConfig *cfg);
int wave_config_zoom_text(const WaveConfig *cfg, char *out, size_t cap);
int wave_config_wrap_text(const WaveConfig *cfg, char *out, size_t cap);

#endif
