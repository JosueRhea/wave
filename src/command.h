#ifndef WAVE_COMMAND_H
#define WAVE_COMMAND_H

#include "config.h"

typedef struct {
    int active;
    char text[256];
    int len;
} CommandLine;

typedef enum {
    COMMAND_NONE,
    COMMAND_WRITE,
    COMMAND_QUIT,
    COMMAND_WRITE_QUIT,
    COMMAND_QUIT_ALL,
    COMMAND_SET_WRAP,
    COMMAND_SET_NOWRAP,
    COMMAND_SAVE_CONFIG,
    COMMAND_SET_OPACITY,
    COMMAND_TOGGLE_BLUR,
    COMMAND_TOGGLE_TITLEBAR,
} CommandKind;

typedef struct {
    CommandKind kind;
    float value;
} CommandAction;

typedef enum {
    COMMAND_INFO_NONE,
    COMMAND_INFO_CONFIG_PATH,
    COMMAND_INFO_OPACITY,
    COMMAND_INFO_BLUR,
    COMMAND_INFO_TITLEBAR,
} CommandInfoKind;

typedef struct {
    int write;
    int quit;
    int quit_all;
    int save_config;
    int apply_blur;
    int apply_titlebar;
    CommandInfoKind info;
} CommandEffect;

typedef struct {
    CommandEffect effect;
    char info[256];
} CommandRun;

typedef enum {
    COMMAND_CLOSE_NONE,
    COMMAND_CLOSE_TAB,
    COMMAND_CLOSE_WINDOW
} CommandCloseAction;

typedef struct {
    int save_config;
    int apply_blur;
    int apply_titlebar;
    int write_file;
    CommandCloseAction close;
} CommandAppPlan;

typedef enum {
    COMMAND_KEY_NONE,
    COMMAND_KEY_ESCAPE,
    COMMAND_KEY_ACCEPT,
    COMMAND_KEY_BACKSPACE
} CommandKey;

typedef enum {
    COMMAND_KEY_RESULT_NONE,
    COMMAND_KEY_RESULT_HANDLED,
    COMMAND_KEY_RESULT_ACCEPT
} CommandKeyResult;

void command_open(CommandLine *cmd);
void command_close(CommandLine *cmd);
const char *command_text(CommandLine *cmd);
void command_insert_char(CommandLine *cmd, unsigned int cp);
void command_insert_text(CommandLine *cmd, const char *text);
void command_backspace(CommandLine *cmd);
CommandKeyResult command_apply_key(CommandLine *cmd, CommandKey key);
CommandAction command_parse(const char *text);
CommandEffect command_effect(CommandAction action, WaveConfig *config);
int command_info_text(CommandInfoKind info, const WaveConfig *config,
                      const char *config_path, char *out, int cap);
CommandRun command_run(const char *text, WaveConfig *config,
                       const char *config_path);
CommandCloseAction command_close_action(CommandEffect effect);
CommandAppPlan command_app_plan(CommandEffect effect, int editor_has_path);

#endif
