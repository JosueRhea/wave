#ifndef WAVE_INPUT_H
#define WAVE_INPUT_H

typedef enum {
    INPUT_KEY_NONE,
    INPUT_KEY_C,
    INPUT_KEY_V,
    INPUT_KEY_P,
    INPUT_KEY_F,
    INPUT_KEY_B,
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
    INPUT_KEY_KP_0
} InputKey;

typedef enum {
    SHORTCUT_NONE,
    SHORTCUT_COPY,
    SHORTCUT_PASTE,
    SHORTCUT_PALETTE,
    SHORTCUT_SEARCH,
    SHORTCUT_TOGGLE_SIDEBAR,
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

typedef enum {
    INPUT_CLIPBOARD_NONE,
    INPUT_CLIPBOARD_OVERLAY,
    INPUT_CLIPBOARD_COMMAND,
    INPUT_CLIPBOARD_EDITOR
} InputClipboardTarget;

WaveShortcut input_shortcut(InputKey key, int command, int control,
                            int alt, int shift);
InputTextTarget input_text_target(int command_modifier, int overlay_active,
                                  int command_active, int editor_available,
                                  int insert_mode);
InputClipboardTarget input_clipboard_target(int overlay_active,
                                            int command_active,
                                            int editor_available);

#endif
