#include "command.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

void command_open(CommandLine *cmd) {
    if (!cmd) return;
    cmd->active = 1;
    cmd->len = 0;
    cmd->text[0] = '\0';
}

void command_close(CommandLine *cmd) {
    if (!cmd) return;
    cmd->active = 0;
    cmd->len = 0;
    cmd->text[0] = '\0';
}

const char *command_text(CommandLine *cmd) {
    if (!cmd) return "";
    cmd->text[cmd->len] = '\0';
    return cmd->text;
}

void command_insert_char(CommandLine *cmd, unsigned int cp) {
    if (!cmd || cp < 32 || cp >= 127 || cmd->len >= (int)sizeof(cmd->text) - 1)
        return;
    cmd->text[cmd->len++] = (char)cp;
    cmd->text[cmd->len] = '\0';
}

void command_insert_text(CommandLine *cmd, const char *text) {
    if (!cmd || !text) return;
    while (*text)
        command_insert_char(cmd, (unsigned char)*text++);
}

void command_backspace(CommandLine *cmd) {
    if (!cmd || cmd->len <= 0) return;
    cmd->text[--cmd->len] = '\0';
}

CommandKeyResult command_apply_key(CommandLine *cmd, CommandKey key) {
    if (!cmd || !cmd->active) return COMMAND_KEY_RESULT_NONE;
    switch (key) {
    case COMMAND_KEY_ESCAPE:
        command_close(cmd);
        return COMMAND_KEY_RESULT_HANDLED;
    case COMMAND_KEY_ACCEPT:
        return COMMAND_KEY_RESULT_ACCEPT;
    case COMMAND_KEY_BACKSPACE:
        command_backspace(cmd);
        return COMMAND_KEY_RESULT_HANDLED;
    case COMMAND_KEY_NONE:
    default:
        return COMMAND_KEY_RESULT_NONE;
    }
}

CommandAction command_parse(const char *text) {
    if (!text) return (CommandAction){COMMAND_NONE, 0};
    if (!strcmp(text, "w")) return (CommandAction){COMMAND_WRITE, 0};
    if (!strcmp(text, "q") || !strcmp(text, "q!"))
        return (CommandAction){COMMAND_QUIT, 0};
    if (!strcmp(text, "wq") || !strcmp(text, "x"))
        return (CommandAction){COMMAND_WRITE_QUIT, 0};
    if (!strcmp(text, "qa") || !strcmp(text, "qa!") || !strcmp(text, "qall"))
        return (CommandAction){COMMAND_QUIT_ALL, 0};
    if (!strcmp(text, "wrap") || !strcmp(text, "set wrap"))
        return (CommandAction){COMMAND_SET_WRAP, 0};
    if (!strcmp(text, "nowrap") || !strcmp(text, "set nowrap"))
        return (CommandAction){COMMAND_SET_NOWRAP, 0};
    if (!strcmp(text, "config"))
        return (CommandAction){COMMAND_SAVE_CONFIG, 0};
    if (!strncmp(text, "opacity", 7)) {
        float v = (float)atof(text + 7);
        return (CommandAction){COMMAND_SET_OPACITY, v};
    }
    if (!strcmp(text, "blur"))
        return (CommandAction){COMMAND_TOGGLE_BLUR, 0};
    if (!strcmp(text, "titlebar"))
        return (CommandAction){COMMAND_TOGGLE_TITLEBAR, 0};
    return (CommandAction){COMMAND_NONE, 0};
}

CommandEffect command_effect(CommandAction action, WaveConfig *config) {
    CommandEffect effect = {0};
    switch (action.kind) {
    case COMMAND_WRITE:
        effect.write = 1;
        break;
    case COMMAND_QUIT:
        effect.quit = 1;
        break;
    case COMMAND_WRITE_QUIT:
        effect.write = 1;
        effect.quit = 1;
        break;
    case COMMAND_QUIT_ALL:
        effect.quit = 1;
        effect.quit_all = 1;
        break;
    case COMMAND_SET_WRAP:
        if (config) config->wrap = 1;
        effect.save_config = 1;
        break;
    case COMMAND_SET_NOWRAP:
        if (config) config->wrap = 0;
        effect.save_config = 1;
        break;
    case COMMAND_SAVE_CONFIG:
        effect.save_config = 1;
        effect.info = COMMAND_INFO_CONFIG_PATH;
        break;
    case COMMAND_SET_OPACITY:
        if (config && action.value >= 0.2f && action.value <= 1.0f) {
            config->opacity = action.value;
            effect.save_config = 1;
        }
        effect.info = COMMAND_INFO_OPACITY;
        break;
    case COMMAND_TOGGLE_BLUR:
        if (config) config->blur = !config->blur;
        effect.save_config = 1;
        effect.apply_blur = 1;
        effect.info = COMMAND_INFO_BLUR;
        break;
    case COMMAND_TOGGLE_TITLEBAR:
        if (config) config->native_titlebar = !config->native_titlebar;
        effect.save_config = 1;
        effect.apply_titlebar = 1;
        effect.info = COMMAND_INFO_TITLEBAR;
        break;
    case COMMAND_NONE:
    default:
        break;
    }
    return effect;
}

int command_info_text(CommandInfoKind info, const WaveConfig *config,
                      const char *config_path, char *out, int cap) {
    if (!out || cap <= 0) return 0;
    switch (info) {
    case COMMAND_INFO_CONFIG_PATH:
        snprintf(out, (size_t)cap, "config saved: %s", config_path ? config_path : "");
        return 1;
    case COMMAND_INFO_OPACITY:
        snprintf(out, (size_t)cap, "opacity %.2f", config ? config->opacity : 0.0f);
        return 1;
    case COMMAND_INFO_BLUR:
        snprintf(out, (size_t)cap, "blur %s", (config && config->blur) ? "on" : "off");
        return 1;
    case COMMAND_INFO_TITLEBAR:
        snprintf(out, (size_t)cap, "titlebar %s",
                 (config && config->native_titlebar) ? "native" : "default");
        return 1;
    case COMMAND_INFO_NONE:
    default:
        out[0] = '\0';
        return 0;
    }
}

CommandRun command_run(const char *text, WaveConfig *config,
                       const char *config_path) {
    CommandRun run;
    memset(&run, 0, sizeof run);
    run.effect = command_effect(command_parse(text), config);
    command_info_text(run.effect.info, config, config_path,
                      run.info, (int)sizeof run.info);
    return run;
}

CommandCloseAction command_close_action(CommandEffect effect) {
    if (!effect.quit) return COMMAND_CLOSE_NONE;
    return effect.quit_all ? COMMAND_CLOSE_WINDOW : COMMAND_CLOSE_TAB;
}

CommandAppPlan command_app_plan(CommandEffect effect, int editor_has_path) {
    CommandAppPlan plan = {0};
    plan.save_config = effect.save_config;
    plan.apply_blur = effect.apply_blur;
    plan.apply_titlebar = effect.apply_titlebar;
    plan.write_file = effect.write && editor_has_path;
    plan.close = command_close_action(effect);
    return plan;
}
