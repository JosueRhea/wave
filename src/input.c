#include "input.h"

WaveShortcut input_shortcut(InputKey key, int command, int control,
                            int alt, int shift) {
    if (command) {
        switch (key) {
        case INPUT_KEY_C: return SHORTCUT_COPY;
        case INPUT_KEY_V: return SHORTCUT_PASTE;
        case INPUT_KEY_P: return SHORTCUT_PALETTE;
        case INPUT_KEY_F: return shift ? SHORTCUT_SEARCH : SHORTCUT_NONE;
        case INPUT_KEY_B: return SHORTCUT_TOGGLE_SIDEBAR;
        case INPUT_KEY_S: return SHORTCUT_SAVE;
        case INPUT_KEY_RIGHT_BRACKET: return SHORTCUT_TAB_NEXT;
        case INPUT_KEY_LEFT_BRACKET: return SHORTCUT_TAB_PREV;
        case INPUT_KEY_W: return SHORTCUT_CLOSE_TAB;
        case INPUT_KEY_Z: return shift ? SHORTCUT_REDO : SHORTCUT_UNDO;
        case INPUT_KEY_EQUAL:
        case INPUT_KEY_ADD:
            return SHORTCUT_ZOOM_IN;
        case INPUT_KEY_MINUS:
        case INPUT_KEY_SUBTRACT:
            return SHORTCUT_ZOOM_OUT;
        case INPUT_KEY_0:
        case INPUT_KEY_KP_0:
            return SHORTCUT_ZOOM_RESET;
        default:
            break;
        }
    }
    if (control && key == INPUT_KEY_R) return SHORTCUT_REDO;
    if (alt && key == INPUT_KEY_Z) return SHORTCUT_TOGGLE_WRAP;
    return SHORTCUT_NONE;
}

InputTextTarget input_text_target(int command_modifier, int overlay_active,
                                  int command_active, int editor_available,
                                  int insert_mode) {
    if (command_modifier) return INPUT_TEXT_IGNORE;
    if (overlay_active) return INPUT_TEXT_OVERLAY;
    if (command_active) return INPUT_TEXT_COMMAND;
    if (!editor_available) return INPUT_TEXT_NONE;
    return insert_mode ? INPUT_TEXT_EDITOR_INSERT : INPUT_TEXT_EDITOR_COMMAND;
}

InputClipboardTarget input_clipboard_target(int overlay_active,
                                            int command_active,
                                            int editor_available) {
    if (overlay_active) return INPUT_CLIPBOARD_OVERLAY;
    if (command_active) return INPUT_CLIPBOARD_COMMAND;
    if (editor_available) return INPUT_CLIPBOARD_EDITOR;
    return INPUT_CLIPBOARD_NONE;
}
