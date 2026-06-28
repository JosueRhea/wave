#include "input_glfw.h"

#include <GLFW/glfw3.h>

EditorKey wave_editor_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_BACKSPACE: return EDITOR_KEY_BACKSPACE;
    case GLFW_KEY_DELETE: return EDITOR_KEY_DELETE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return EDITOR_KEY_ENTER;
    case GLFW_KEY_TAB: return EDITOR_KEY_TAB;
    case GLFW_KEY_LEFT: return EDITOR_KEY_LEFT;
    case GLFW_KEY_RIGHT: return EDITOR_KEY_RIGHT;
    case GLFW_KEY_UP: return EDITOR_KEY_UP;
    case GLFW_KEY_DOWN: return EDITOR_KEY_DOWN;
    default: return EDITOR_KEY_NONE;
    }
}

CommandKey wave_command_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return COMMAND_KEY_ESCAPE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return COMMAND_KEY_ACCEPT;
    case GLFW_KEY_BACKSPACE: return COMMAND_KEY_BACKSPACE;
    default: return COMMAND_KEY_NONE;
    }
}

OverlayKey wave_overlay_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return OVERLAY_KEY_ESCAPE;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return OVERLAY_KEY_ACCEPT;
    case GLFW_KEY_UP: return OVERLAY_KEY_UP;
    case GLFW_KEY_DOWN: return OVERLAY_KEY_DOWN;
    case GLFW_KEY_BACKSPACE: return OVERLAY_KEY_BACKSPACE;
    default: return OVERLAY_KEY_NONE;
    }
}

PopoverKey wave_popover_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE: return POPOVER_KEY_ESCAPE;
    case GLFW_KEY_UP: return POPOVER_KEY_UP;
    case GLFW_KEY_DOWN: return POPOVER_KEY_DOWN;
    default: return POPOVER_KEY_NONE;
    }
}

InputKey wave_input_key_from_glfw(int key) {
    switch (key) {
    case GLFW_KEY_C: return INPUT_KEY_C;
    case GLFW_KEY_V: return INPUT_KEY_V;
    case GLFW_KEY_P: return INPUT_KEY_P;
    case GLFW_KEY_F: return INPUT_KEY_F;
    case GLFW_KEY_B: return INPUT_KEY_B;
    case GLFW_KEY_S: return INPUT_KEY_S;
    case GLFW_KEY_RIGHT_BRACKET: return INPUT_KEY_RIGHT_BRACKET;
    case GLFW_KEY_LEFT_BRACKET: return INPUT_KEY_LEFT_BRACKET;
    case GLFW_KEY_W: return INPUT_KEY_W;
    case GLFW_KEY_Z: return INPUT_KEY_Z;
    case GLFW_KEY_R: return INPUT_KEY_R;
    case GLFW_KEY_EQUAL: return INPUT_KEY_EQUAL;
    case GLFW_KEY_KP_ADD: return INPUT_KEY_ADD;
    case GLFW_KEY_MINUS: return INPUT_KEY_MINUS;
    case GLFW_KEY_KP_SUBTRACT: return INPUT_KEY_SUBTRACT;
    case GLFW_KEY_0: return INPUT_KEY_0;
    case GLFW_KEY_KP_0: return INPUT_KEY_KP_0;
    default: return INPUT_KEY_NONE;
    }
}
