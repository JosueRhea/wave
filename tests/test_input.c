/* test_input.c - framework-independent shortcut classification. */
#include "test.h"
#include "input.h"

int main(void) {
    CHECK_EQ(input_shortcut(INPUT_KEY_C, 1, 0, 0, 0), SHORTCUT_COPY);
    CHECK_EQ(input_shortcut(INPUT_KEY_V, 1, 0, 0, 0), SHORTCUT_PASTE);
    CHECK_EQ(input_shortcut(INPUT_KEY_P, 1, 0, 0, 0), SHORTCUT_PALETTE);
    CHECK_EQ(input_shortcut(INPUT_KEY_F, 1, 0, 0, 1), SHORTCUT_SEARCH);
    CHECK_EQ(input_shortcut(INPUT_KEY_F, 1, 0, 0, 0), SHORTCUT_NONE);
    CHECK_EQ(input_shortcut(INPUT_KEY_B, 1, 0, 0, 0), SHORTCUT_TOGGLE_SIDEBAR);
    CHECK_EQ(input_shortcut(INPUT_KEY_S, 1, 0, 0, 0), SHORTCUT_SAVE);
    CHECK_EQ(input_shortcut(INPUT_KEY_RIGHT_BRACKET, 1, 0, 0, 0), SHORTCUT_TAB_NEXT);
    CHECK_EQ(input_shortcut(INPUT_KEY_LEFT_BRACKET, 1, 0, 0, 0), SHORTCUT_TAB_PREV);
    CHECK_EQ(input_shortcut(INPUT_KEY_W, 1, 0, 0, 0), SHORTCUT_CLOSE_TAB);
    CHECK_EQ(input_shortcut(INPUT_KEY_Z, 1, 0, 0, 0), SHORTCUT_UNDO);
    CHECK_EQ(input_shortcut(INPUT_KEY_Z, 1, 0, 0, 1), SHORTCUT_REDO);
    CHECK_EQ(input_shortcut(INPUT_KEY_R, 0, 1, 0, 0), SHORTCUT_REDO);
    CHECK_EQ(input_shortcut(INPUT_KEY_EQUAL, 1, 0, 0, 0), SHORTCUT_ZOOM_IN);
    CHECK_EQ(input_shortcut(INPUT_KEY_ADD, 1, 0, 0, 0), SHORTCUT_ZOOM_IN);
    CHECK_EQ(input_shortcut(INPUT_KEY_MINUS, 1, 0, 0, 0), SHORTCUT_ZOOM_OUT);
    CHECK_EQ(input_shortcut(INPUT_KEY_SUBTRACT, 1, 0, 0, 0), SHORTCUT_ZOOM_OUT);
    CHECK_EQ(input_shortcut(INPUT_KEY_0, 1, 0, 0, 0), SHORTCUT_ZOOM_RESET);
    CHECK_EQ(input_shortcut(INPUT_KEY_KP_0, 1, 0, 0, 0), SHORTCUT_ZOOM_RESET);
    CHECK_EQ(input_shortcut(INPUT_KEY_Z, 0, 0, 1, 0), SHORTCUT_TOGGLE_WRAP);
    CHECK_EQ(input_shortcut(INPUT_KEY_C, 0, 0, 0, 0), SHORTCUT_NONE);
    CHECK_EQ(input_shortcut(INPUT_KEY_NONE, 1, 1, 1, 1), SHORTCUT_NONE);

    CHECK_EQ(input_text_target(1, 1, 1, 1, 1), INPUT_TEXT_IGNORE);
    CHECK_EQ(input_text_target(0, 1, 1, 1, 1), INPUT_TEXT_OVERLAY);
    CHECK_EQ(input_text_target(0, 0, 1, 1, 1), INPUT_TEXT_COMMAND);
    CHECK_EQ(input_text_target(0, 0, 0, 0, 1), INPUT_TEXT_NONE);
    CHECK_EQ(input_text_target(0, 0, 0, 1, 1), INPUT_TEXT_EDITOR_INSERT);
    CHECK_EQ(input_text_target(0, 0, 0, 1, 0), INPUT_TEXT_EDITOR_COMMAND);

    CHECK_EQ(input_clipboard_target(1, 1, 1), INPUT_CLIPBOARD_OVERLAY);
    CHECK_EQ(input_clipboard_target(0, 1, 1), INPUT_CLIPBOARD_COMMAND);
    CHECK_EQ(input_clipboard_target(0, 0, 1), INPUT_CLIPBOARD_EDITOR);
    CHECK_EQ(input_clipboard_target(0, 0, 0), INPUT_CLIPBOARD_NONE);

    TEST_REPORT();
}
