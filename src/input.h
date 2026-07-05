#ifndef WAVE_INPUT_H
#define WAVE_INPUT_H

#include "layout.h"

typedef enum {
    INPUT_KEY_NONE,
    INPUT_KEY_C,
    INPUT_KEY_V,
    INPUT_KEY_P,
    INPUT_KEY_F,
    INPUT_KEY_B,
    INPUT_KEY_J,
    INPUT_KEY_S,
    INPUT_KEY_RIGHT_BRACKET,
    INPUT_KEY_LEFT_BRACKET,
    INPUT_KEY_W,
    INPUT_KEY_Z,
    INPUT_KEY_R,
    INPUT_KEY_EQUAL,
    INPUT_KEY_ADD,
    INPUT_KEY_MINUS,
    INPUT_KEY_SUBTRACT,
    INPUT_KEY_0,
    INPUT_KEY_KP_0,
    INPUT_KEY_TAB
} InputKey;

typedef enum {
    SHORTCUT_NONE,
    SHORTCUT_COPY,
    SHORTCUT_PASTE,
    SHORTCUT_PALETTE,
    SHORTCUT_SEARCH,
    SHORTCUT_TOGGLE_SIDEBAR,
    SHORTCUT_NEW_TERMINAL,
    SHORTCUT_SAVE,
    SHORTCUT_TAB_NEXT,
    SHORTCUT_TAB_PREV,
    SHORTCUT_CLOSE_TAB,
    SHORTCUT_UNDO,
    SHORTCUT_REDO,
    SHORTCUT_ZOOM_IN,
    SHORTCUT_ZOOM_OUT,
    SHORTCUT_ZOOM_RESET,
    SHORTCUT_TOGGLE_WRAP
} WaveShortcut;

typedef enum {
    INPUT_TEXT_IGNORE,
    INPUT_TEXT_NONE,
    INPUT_TEXT_OVERLAY,
    INPUT_TEXT_COMMAND,
    INPUT_TEXT_EDITOR_INSERT,
    INPUT_TEXT_EDITOR_COMMAND
} InputTextTarget;

typedef struct {
    InputTextTarget target;
    int has_text;
    char text[2];
} InputTextPlan;

typedef enum {
    INPUT_CLIPBOARD_NONE,
    INPUT_CLIPBOARD_OVERLAY,
    INPUT_CLIPBOARD_COMMAND,
    INPUT_CLIPBOARD_EDITOR
} InputClipboardTarget;

typedef enum {
    INPUT_KEY_TARGET_NONE,
    INPUT_KEY_TARGET_SHORTCUT,
    INPUT_KEY_TARGET_OVERLAY,
    INPUT_KEY_TARGET_COMMAND,
    INPUT_KEY_TARGET_EDITOR
} InputKeyTarget;

typedef enum {
    INPUT_SHORTCUT_ACTION_NONE,
    INPUT_SHORTCUT_ACTION_COPY,
    INPUT_SHORTCUT_ACTION_PASTE,
    INPUT_SHORTCUT_ACTION_OPEN_PALETTE,
    INPUT_SHORTCUT_ACTION_OPEN_SEARCH,
    INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR,
    INPUT_SHORTCUT_ACTION_NEW_TERMINAL,
    INPUT_SHORTCUT_ACTION_SAVE,
    INPUT_SHORTCUT_ACTION_TAB_NEXT,
    INPUT_SHORTCUT_ACTION_TAB_PREV,
    INPUT_SHORTCUT_ACTION_CLOSE_TAB,
    INPUT_SHORTCUT_ACTION_UNDO,
    INPUT_SHORTCUT_ACTION_REDO,
    INPUT_SHORTCUT_ACTION_ZOOM_IN,
    INPUT_SHORTCUT_ACTION_ZOOM_OUT,
    INPUT_SHORTCUT_ACTION_ZOOM_RESET,
    INPUT_SHORTCUT_ACTION_TOGGLE_WRAP
} InputShortcutAction;

typedef struct {
    InputKeyTarget target;
    InputShortcutAction shortcut_action;
    InputClipboardTarget clipboard_target;
} InputKeyPlan;

typedef struct {
    int open_palette;
    int open_search;
    int toggle_sidebar;
    int new_terminal;
    int save_file;
    int tab_delta;
    int close_tab;
    int history;
    int history_redo;
    int zoom;
    int zoom_dir;
    int toggle_wrap;
} InputShortcutEffect;

typedef enum {
    INPUT_MOUSE_ACTION_NONE,
    INPUT_MOUSE_ACTION_RELEASE_DRAG,
    INPUT_MOUSE_ACTION_TITLEBAR_MENU,
    INPUT_MOUSE_ACTION_TITLEBAR_LEFT,
    INPUT_MOUSE_ACTION_SIDEBAR,
    INPUT_MOUSE_ACTION_TAB,
    INPUT_MOUSE_ACTION_TEXT
} InputMouseAction;

typedef struct {
    InputMouseAction action;
    int dismiss_popover;
    int record_activity;
} InputMousePlan;

WaveShortcut input_shortcut(InputKey key, int command, int control,
                            int alt, int shift);
InputTextTarget input_text_target(int command_modifier, int overlay_active,
                                  int command_active, int editor_available,
                                  int insert_mode);
InputTextPlan input_text_plan(int command_modifier, int overlay_active,
                              int command_active, int editor_available,
                              int insert_mode, unsigned int cp);
InputClipboardTarget input_clipboard_target(int overlay_active,
                                            int command_active,
                                            int editor_available);
InputKeyTarget input_key_target(WaveShortcut shortcut, int overlay_active,
                                int command_active, int editor_available);
InputShortcutAction input_shortcut_action(WaveShortcut shortcut,
                                          int editor_has_path);
InputShortcutEffect input_shortcut_effect(InputShortcutAction action);
InputKeyPlan input_key_plan(WaveShortcut shortcut, int overlay_active,
                            int command_active, int editor_available,
                            int editor_has_path);
InputMousePlan input_mouse_plan(LayoutClickTarget click, int left_button,
                                int right_button, int press, int release,
                                int popover_active, int editor_selectable);

#endif
