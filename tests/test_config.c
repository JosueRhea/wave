/* test_config.c - preference defaults and small mutation helpers. */
#include "test.h"
#include "config.h"

int main(void) {
    WaveConfig cfg;
    wave_config_defaults(&cfg);

    CHECK_EQ(cfg.show_sidebar, 1);
    CHECK_EQ(cfg.side_cells, 26);
    CHECK_EQ(cfg.wrap, 1);
    CHECK_EQ((int)(cfg.ui_scale * 100.0f + 0.5f), 100);

    CHECK_EQ(wave_config_zoom(&cfg, +1), 110);
    char info[32];
    CHECK(wave_config_zoom_text(&cfg, info, sizeof info));
    CHECK_STR(info, "zoom 110%");
    CHECK_EQ(wave_config_zoom(&cfg, 0), 100);
    CHECK_EQ(wave_config_zoom(&cfg, -1), 91);

    cfg.ui_scale = 0.49f;
    CHECK_EQ(wave_config_zoom(&cfg, -1), 50);
    cfg.ui_scale = 3.1f;
    CHECK_EQ(wave_config_zoom(&cfg, +1), 300);
    CHECK_EQ(wave_config_zoom(NULL, +1), 0);

    CHECK_EQ(wave_config_toggle_sidebar(&cfg), 0);
    CHECK_EQ(cfg.show_sidebar, 0);
    CHECK_EQ(wave_config_toggle_sidebar(&cfg), 1);
    CHECK_EQ(cfg.show_sidebar, 1);

    CHECK_EQ(wave_config_toggle_wrap(&cfg), 0);
    CHECK_EQ(cfg.wrap, 0);
    CHECK(wave_config_wrap_text(&cfg, info, sizeof info));
    CHECK_STR(info, "word wrap off");
    CHECK_EQ(wave_config_toggle_wrap(&cfg), 1);
    CHECK(wave_config_wrap_text(&cfg, info, sizeof info));
    CHECK_STR(info, "word wrap on");

    TEST_REPORT();
}
