/* test_command.c — command-line input and parser behavior. */
#include "test.h"
#include "command.h"

int main(void) {
    CommandLine cmd = {0};
    command_open(&cmd);
    command_insert_text(&cmd, "wq");
    CHECK(cmd.active);
    CHECK_STR(command_text(&cmd), "wq");
    CHECK_EQ(command_parse(command_text(&cmd)).kind, COMMAND_WRITE_QUIT);

    command_backspace(&cmd);
    CHECK_STR(command_text(&cmd), "w");
    CHECK_EQ(command_parse(command_text(&cmd)).kind, COMMAND_WRITE);
    CHECK_EQ(command_apply_key(&cmd, COMMAND_KEY_BACKSPACE),
             COMMAND_KEY_RESULT_HANDLED);
    CHECK_STR(command_text(&cmd), "");
    CHECK_EQ(command_apply_key(&cmd, COMMAND_KEY_ACCEPT),
             COMMAND_KEY_RESULT_ACCEPT);
    CHECK(cmd.active);
    CHECK_EQ(command_apply_key(&cmd, COMMAND_KEY_NONE),
             COMMAND_KEY_RESULT_NONE);
    CHECK_EQ(command_apply_key(&cmd, COMMAND_KEY_ESCAPE),
             COMMAND_KEY_RESULT_HANDLED);
    CHECK(!cmd.active);
    CHECK_EQ(command_apply_key(&cmd, COMMAND_KEY_ESCAPE),
             COMMAND_KEY_RESULT_NONE);
    command_open(&cmd);
    command_insert_text(&cmd, "w");
    command_close(&cmd);
    CHECK(!cmd.active);
    CHECK_STR(command_text(&cmd), "");

    CHECK_EQ(command_parse("q!").kind, COMMAND_QUIT);
    CHECK_EQ(command_parse("qa").kind, COMMAND_QUIT_ALL);
    CHECK_EQ(command_parse("set wrap").kind, COMMAND_SET_WRAP);
    CHECK_EQ(command_parse("set nowrap").kind, COMMAND_SET_NOWRAP);
    CHECK_EQ(command_parse("config").kind, COMMAND_SAVE_CONFIG);
    CHECK_EQ(command_parse("blur").kind, COMMAND_TOGGLE_BLUR);
    CHECK_EQ(command_parse("titlebar").kind, COMMAND_TOGGLE_TITLEBAR);
    CommandAction opacity = command_parse("opacity 0.75");
    CHECK_EQ(opacity.kind, COMMAND_SET_OPACITY);
    CHECK(opacity.value > 0.74f && opacity.value < 0.76f);
    CHECK_EQ(command_parse("not-a-command").kind, COMMAND_NONE);

    WaveConfig cfg;
    wave_config_defaults(&cfg);
    CommandEffect eff = command_effect((CommandAction){COMMAND_WRITE_QUIT, 0}, &cfg);
    CHECK(eff.write);
    CHECK(eff.quit);
    CHECK(!eff.quit_all);
    CHECK_EQ(command_close_action(eff), COMMAND_CLOSE_TAB);

    eff = command_effect((CommandAction){COMMAND_QUIT_ALL, 0}, &cfg);
    CHECK_EQ(command_close_action(eff), COMMAND_CLOSE_WINDOW);

    eff = command_effect((CommandAction){COMMAND_SET_NOWRAP, 0}, &cfg);
    CHECK(!cfg.wrap);
    CHECK(eff.save_config);
    CHECK_EQ(command_close_action(eff), COMMAND_CLOSE_NONE);

    eff = command_effect((CommandAction){COMMAND_SET_OPACITY, 0.75f}, &cfg);
    CHECK(eff.save_config);
    CHECK_EQ(eff.info, COMMAND_INFO_OPACITY);
    CHECK(cfg.opacity > 0.74f && cfg.opacity < 0.76f);

    eff = command_effect((CommandAction){COMMAND_SET_OPACITY, 1.5f}, &cfg);
    CHECK(!eff.save_config);
    CHECK_EQ(eff.info, COMMAND_INFO_OPACITY);
    CHECK(cfg.opacity > 0.74f && cfg.opacity < 0.76f);

    int blur = cfg.blur;
    eff = command_effect((CommandAction){COMMAND_TOGGLE_BLUR, 0}, &cfg);
    CHECK_EQ(cfg.blur, !blur);
    CHECK(eff.save_config);
    CHECK(eff.apply_blur);
    CHECK_EQ(eff.info, COMMAND_INFO_BLUR);

    eff = command_effect((CommandAction){COMMAND_SAVE_CONFIG, 0}, &cfg);
    CHECK(eff.save_config);
    CHECK_EQ(eff.info, COMMAND_INFO_CONFIG_PATH);

    char info[128];
    CHECK(command_info_text(COMMAND_INFO_CONFIG_PATH, &cfg, "/tmp/wave/config",
                            info, sizeof info));
    CHECK_STR(info, "config saved: /tmp/wave/config");
    CHECK(command_info_text(COMMAND_INFO_OPACITY, &cfg, NULL, info, sizeof info));
    CHECK_STR(info, "opacity 0.75");
    cfg.blur = 1;
    CHECK(command_info_text(COMMAND_INFO_BLUR, &cfg, NULL, info, sizeof info));
    CHECK_STR(info, "blur on");
    cfg.native_titlebar = 1;
    CHECK(command_info_text(COMMAND_INFO_TITLEBAR, &cfg, NULL, info, sizeof info));
    CHECK_STR(info, "titlebar native");
    snprintf(info, sizeof info, "stale");
    CHECK(!command_info_text(COMMAND_INFO_NONE, &cfg, NULL, info, sizeof info));
    CHECK_STR(info, "");

    wave_config_defaults(&cfg);
    CommandRun run = command_run("nowrap", &cfg, "/tmp/wave/config");
    CHECK(!cfg.wrap);
    CHECK(run.effect.save_config);
    CHECK_STR(run.info, "");

    run = command_run("config", &cfg, "/tmp/wave/config");
    CHECK(run.effect.save_config);
    CHECK_STR(run.info, "config saved: /tmp/wave/config");

    run = command_run("blur", &cfg, "/tmp/wave/config");
    CHECK(run.effect.apply_blur);
    CHECK_STR(run.info, cfg.blur ? "blur on" : "blur off");

    run = command_run("not-a-command", &cfg, "/tmp/wave/config");
    CHECK(!run.effect.write);
    CHECK(!run.effect.quit);
    CHECK_STR(run.info, "");

    TEST_REPORT();
}
