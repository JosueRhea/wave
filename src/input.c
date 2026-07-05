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
        case INPUT_KEY_J: return SHORTCUT_NEW_TERMINAL;
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

InputTextPlan input_text_plan(int command_modifier, int overlay_active,
                              int command_active, int editor_available,
                              int insert_mode, unsigned int cp) {
    InputTextPlan plan = {0};
    plan.target = input_text_target(command_modifier, overlay_active,
                                    command_active, editor_available,
                                    insert_mode);
    if ((plan.target == INPUT_TEXT_OVERLAY || plan.target == INPUT_TEXT_COMMAND) &&
        cp >= 32 && cp < 127) {
        plan.has_text = 1;
        plan.text[0] = (char)cp;
        plan.text[1] = '\0';
    }
    return plan;
}

InputClipboardTarget input_clipboard_target(int overlay_active,
                                            int command_active,
                                            int editor_available) {
    if (overlay_active) return INPUT_CLIPBOARD_OVERLAY;
    if (command_active) return INPUT_CLIPBOARD_COMMAND;
    if (editor_available) return INPUT_CLIPBOARD_EDITOR;
    return INPUT_CLIPBOARD_NONE;
}

InputKeyTarget input_key_target(WaveShortcut shortcut, int overlay_active,
                                int command_active, int editor_available) {
    if (shortcut != SHORTCUT_NONE) return INPUT_KEY_TARGET_SHORTCUT;
    if (overlay_active) return INPUT_KEY_TARGET_OVERLAY;
    if (command_active) return INPUT_KEY_TARGET_COMMAND;
    if (editor_available) return INPUT_KEY_TARGET_EDITOR;
    return INPUT_KEY_TARGET_NONE;
}

InputShortcutAction input_shortcut_action(WaveShortcut shortcut,
                                          int editor_has_path) {
    switch (shortcut) {
    case SHORTCUT_COPY: return INPUT_SHORTCUT_ACTION_COPY;
    case SHORTCUT_PASTE: return INPUT_SHORTCUT_ACTION_PASTE;
    case SHORTCUT_PALETTE: return INPUT_SHORTCUT_ACTION_OPEN_PALETTE;
    case SHORTCUT_SEARCH: return INPUT_SHORTCUT_ACTION_OPEN_SEARCH;
    case SHORTCUT_TOGGLE_SIDEBAR: return INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR;
    case SHORTCUT_NEW_TERMINAL:
        return INPUT_SHORTCUT_ACTION_NEW_TERMINAL;
    case SHORTCUT_SAVE:
        return editor_has_path ? INPUT_SHORTCUT_ACTION_SAVE
                               : INPUT_SHORTCUT_ACTION_NONE;
    case SHORTCUT_TAB_NEXT: return INPUT_SHORTCUT_ACTION_TAB_NEXT;
    case SHORTCUT_TAB_PREV: return INPUT_SHORTCUT_ACTION_TAB_PREV;
    case SHORTCUT_CLOSE_TAB: return INPUT_SHORTCUT_ACTION_CLOSE_TAB;
    case SHORTCUT_UNDO: return INPUT_SHORTCUT_ACTION_UNDO;
    case SHORTCUT_REDO: return INPUT_SHORTCUT_ACTION_REDO;
    case SHORTCUT_ZOOM_IN: return INPUT_SHORTCUT_ACTION_ZOOM_IN;
    case SHORTCUT_ZOOM_OUT: return INPUT_SHORTCUT_ACTION_ZOOM_OUT;
    case SHORTCUT_ZOOM_RESET: return INPUT_SHORTCUT_ACTION_ZOOM_RESET;
    case SHORTCUT_TOGGLE_WRAP: return INPUT_SHORTCUT_ACTION_TOGGLE_WRAP;
    case SHORTCUT_NONE:
    default:
        return INPUT_SHORTCUT_ACTION_NONE;
    }
}

InputShortcutEffect input_shortcut_effect(InputShortcutAction action) {
    InputShortcutEffect effect = {0};
    switch (action) {
    case INPUT_SHORTCUT_ACTION_OPEN_PALETTE:
        effect.open_palette = 1;
        break;
    case INPUT_SHORTCUT_ACTION_OPEN_SEARCH:
        effect.open_search = 1;
        break;
    case INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR:
        effect.toggle_sidebar = 1;
        break;
    case INPUT_SHORTCUT_ACTION_NEW_TERMINAL:
        effect.new_terminal = 1;
        break;
    case INPUT_SHORTCUT_ACTION_SAVE:
        effect.save_file = 1;
        break;
    case INPUT_SHORTCUT_ACTION_TAB_NEXT:
        effect.tab_delta = +1;
        break;
    case INPUT_SHORTCUT_ACTION_TAB_PREV:
        effect.tab_delta = -1;
        break;
    case INPUT_SHORTCUT_ACTION_CLOSE_TAB:
        effect.close_tab = 1;
        break;
    case INPUT_SHORTCUT_ACTION_UNDO:
        effect.history = 1;
        effect.history_redo = 0;
        break;
    case INPUT_SHORTCUT_ACTION_REDO:
        effect.history = 1;
        effect.history_redo = 1;
        break;
    case INPUT_SHORTCUT_ACTION_ZOOM_IN:
        effect.zoom = 1;
        effect.zoom_dir = +1;
        break;
    case INPUT_SHORTCUT_ACTION_ZOOM_OUT:
        effect.zoom = 1;
        effect.zoom_dir = -1;
        break;
    case INPUT_SHORTCUT_ACTION_ZOOM_RESET:
        effect.zoom = 1;
        effect.zoom_dir = 0;
        break;
    case INPUT_SHORTCUT_ACTION_TOGGLE_WRAP:
        effect.toggle_wrap = 1;
        break;
    case INPUT_SHORTCUT_ACTION_NONE:
    case INPUT_SHORTCUT_ACTION_COPY:
    case INPUT_SHORTCUT_ACTION_PASTE:
    default:
        break;
    }
    return effect;
}

InputKeyPlan input_key_plan(WaveShortcut shortcut, int overlay_active,
                            int command_active, int editor_available,
                            int editor_has_path) {
    InputKeyPlan plan;
    plan.target = input_key_target(shortcut, overlay_active,
                                  command_active, editor_available);
    plan.shortcut_action = input_shortcut_action(shortcut, editor_has_path);
    plan.clipboard_target = input_clipboard_target(overlay_active,
                                                   command_active,
                                                   editor_available);
    return plan;
}

InputMousePlan input_mouse_plan(LayoutClickTarget click, int left_button,
                                int right_button, int press, int release,
                                int popover_active, int editor_selectable) {
    InputMousePlan plan = {0};
    if (left_button && release) {
        plan.action = INPUT_MOUSE_ACTION_RELEASE_DRAG;
        return plan;
    }
    if (!press) return plan;

    plan.dismiss_popover = popover_active;
    if (click.kind == LAYOUT_CLICK_TITLEBAR) {
        if (click.titlebar_right || right_button) {
            plan.action = INPUT_MOUSE_ACTION_TITLEBAR_MENU;
            return plan;
        }
        if (left_button) plan.action = INPUT_MOUSE_ACTION_TITLEBAR_LEFT;
        return plan;
    }

    if (!left_button) return plan;
    plan.record_activity = 1;
    switch (click.kind) {
    case LAYOUT_CLICK_SIDEBAR:
        plan.action = INPUT_MOUSE_ACTION_SIDEBAR;
        break;
    case LAYOUT_CLICK_TAB:
        plan.action = INPUT_MOUSE_ACTION_TAB;
        break;
    case LAYOUT_CLICK_TEXT:
        plan.action = editor_selectable ? INPUT_MOUSE_ACTION_TEXT
                                        : INPUT_MOUSE_ACTION_NONE;
        break;
    case LAYOUT_CLICK_NONE:
    case LAYOUT_CLICK_TITLEBAR:
    default:
        break;
    }
    return plan;
}
