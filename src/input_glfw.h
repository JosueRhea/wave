#ifndef WAVE_INPUT_GLFW_H
#define WAVE_INPUT_GLFW_H

#include "command.h"
#include "editor.h"
#include "input.h"
#include "overlay.h"
#include "popover.h"

EditorKey wave_editor_key_from_glfw(int key);
CommandKey wave_command_key_from_glfw(int key);
OverlayKey wave_overlay_key_from_glfw(int key);
PopoverKey wave_popover_key_from_glfw(int key);
InputKey wave_input_key_from_glfw(int key);

#endif
