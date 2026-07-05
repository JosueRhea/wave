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
    CHECK_EQ(input_shortcut(INPUT_KEY_J, 1, 0, 0, 0), SHORTCUT_NEW_TERMINAL);
    CHECK_EQ(input_shortcut(INPUT_KEY_S, 1, 0, 0, 0), SHORTCUT_SAVE);
    CHECK_EQ(input_shortcut(INPUT_KEY_RIGHT_BRACKET, 1, 0, 0, 0), SHORTCUT_TAB_NEXT);
    CHECK_EQ(input_shortcut(INPUT_KEY_LEFT_BRACKET, 1, 0, 0, 0), SHORTCUT_TAB_PREV);
    CHECK_EQ(input_shortcut(INPUT_KEY_TAB, 0, 1, 0, 0), SHORTCUT_TAB_NEXT);
    CHECK_EQ(input_shortcut(INPUT_KEY_TAB, 0, 1, 0, 1), SHORTCUT_TAB_PREV);
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

    InputTextPlan text = input_text_plan(1, 1, 1, 1, 1, 'a');
    CHECK_EQ(text.target, INPUT_TEXT_IGNORE);
    CHECK(!text.has_text);
    text = input_text_plan(0, 1, 0, 1, 0, 'x');
    CHECK_EQ(text.target, INPUT_TEXT_OVERLAY);
    CHECK(text.has_text);
    CHECK_STR(text.text, "x");
    text = input_text_plan(0, 1, 0, 1, 0, '\n');
    CHECK_EQ(text.target, INPUT_TEXT_OVERLAY);
    CHECK(!text.has_text);
    text = input_text_plan(0, 0, 1, 1, 0, ':');
    CHECK_EQ(text.target, INPUT_TEXT_COMMAND);
    CHECK(text.has_text);
    CHECK_STR(text.text, ":");
    text = input_text_plan(0, 0, 1, 1, 0, 0x80);
    CHECK_EQ(text.target, INPUT_TEXT_COMMAND);
    CHECK(!text.has_text);
    text = input_text_plan(0, 0, 0, 1, 1, 0x263A);
    CHECK_EQ(text.target, INPUT_TEXT_EDITOR_INSERT);
    CHECK(!text.has_text);
    text = input_text_plan(0, 0, 0, 1, 0, 'd');
    CHECK_EQ(text.target, INPUT_TEXT_EDITOR_COMMAND);
    CHECK(!text.has_text);

    CHECK_EQ(input_clipboard_target(1, 1, 1), INPUT_CLIPBOARD_OVERLAY);
    CHECK_EQ(input_clipboard_target(0, 1, 1), INPUT_CLIPBOARD_COMMAND);
    CHECK_EQ(input_clipboard_target(0, 0, 1), INPUT_CLIPBOARD_EDITOR);
    CHECK_EQ(input_clipboard_target(0, 0, 0), INPUT_CLIPBOARD_NONE);

    CHECK_EQ(input_key_target(SHORTCUT_SAVE, 1, 1, 1), INPUT_KEY_TARGET_SHORTCUT);
    CHECK_EQ(input_key_target(SHORTCUT_NONE, 1, 1, 1), INPUT_KEY_TARGET_OVERLAY);
    CHECK_EQ(input_key_target(SHORTCUT_NONE, 0, 1, 1), INPUT_KEY_TARGET_COMMAND);
    CHECK_EQ(input_key_target(SHORTCUT_NONE, 0, 0, 1), INPUT_KEY_TARGET_EDITOR);
    CHECK_EQ(input_key_target(SHORTCUT_NONE, 0, 0, 0), INPUT_KEY_TARGET_NONE);

    CHECK_EQ(input_shortcut_action(SHORTCUT_COPY, 0), INPUT_SHORTCUT_ACTION_COPY);
    CHECK_EQ(input_shortcut_action(SHORTCUT_PASTE, 0), INPUT_SHORTCUT_ACTION_PASTE);
    CHECK_EQ(input_shortcut_action(SHORTCUT_PALETTE, 0), INPUT_SHORTCUT_ACTION_OPEN_PALETTE);
    CHECK_EQ(input_shortcut_action(SHORTCUT_SEARCH, 0), INPUT_SHORTCUT_ACTION_OPEN_SEARCH);
    CHECK_EQ(input_shortcut_action(SHORTCUT_TOGGLE_SIDEBAR, 0), INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR);
    CHECK_EQ(input_shortcut_action(SHORTCUT_NEW_TERMINAL, 0), INPUT_SHORTCUT_ACTION_NEW_TERMINAL);
    CHECK_EQ(input_shortcut_action(SHORTCUT_SAVE, 0), INPUT_SHORTCUT_ACTION_NONE);
    CHECK_EQ(input_shortcut_action(SHORTCUT_SAVE, 1), INPUT_SHORTCUT_ACTION_SAVE);
    CHECK_EQ(input_shortcut_action(SHORTCUT_TAB_NEXT, 0), INPUT_SHORTCUT_ACTION_TAB_NEXT);
    CHECK_EQ(input_shortcut_action(SHORTCUT_TAB_PREV, 0), INPUT_SHORTCUT_ACTION_TAB_PREV);
    CHECK_EQ(input_shortcut_action(SHORTCUT_CLOSE_TAB, 0), INPUT_SHORTCUT_ACTION_CLOSE_TAB);
    CHECK_EQ(input_shortcut_action(SHORTCUT_UNDO, 0), INPUT_SHORTCUT_ACTION_UNDO);
    CHECK_EQ(input_shortcut_action(SHORTCUT_REDO, 0), INPUT_SHORTCUT_ACTION_REDO);
    CHECK_EQ(input_shortcut_action(SHORTCUT_ZOOM_IN, 0), INPUT_SHORTCUT_ACTION_ZOOM_IN);
    CHECK_EQ(input_shortcut_action(SHORTCUT_ZOOM_OUT, 0), INPUT_SHORTCUT_ACTION_ZOOM_OUT);
    CHECK_EQ(input_shortcut_action(SHORTCUT_ZOOM_RESET, 0), INPUT_SHORTCUT_ACTION_ZOOM_RESET);
    CHECK_EQ(input_shortcut_action(SHORTCUT_TOGGLE_WRAP, 0), INPUT_SHORTCUT_ACTION_TOGGLE_WRAP);
    CHECK_EQ(input_shortcut_action(SHORTCUT_NONE, 1), INPUT_SHORTCUT_ACTION_NONE);

    InputShortcutEffect effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_OPEN_PALETTE);
    CHECK(effect.open_palette);
    CHECK(!effect.open_search);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_TOGGLE_SIDEBAR);
    CHECK(effect.toggle_sidebar);
    CHECK(!effect.save_file);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_NEW_TERMINAL);
    CHECK(effect.new_terminal);
    CHECK(!effect.toggle_sidebar);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_SAVE);
    CHECK(effect.save_file);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_TAB_NEXT);
    CHECK_EQ(effect.tab_delta, +1);
    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_TAB_PREV);
    CHECK_EQ(effect.tab_delta, -1);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_CLOSE_TAB);
    CHECK(effect.close_tab);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_UNDO);
    CHECK(effect.history);
    CHECK(!effect.history_redo);
    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_REDO);
    CHECK(effect.history);
    CHECK(effect.history_redo);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_ZOOM_IN);
    CHECK(effect.zoom);
    CHECK_EQ(effect.zoom_dir, +1);
    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_ZOOM_OUT);
    CHECK(effect.zoom);
    CHECK_EQ(effect.zoom_dir, -1);
    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_ZOOM_RESET);
    CHECK(effect.zoom);
    CHECK_EQ(effect.zoom_dir, 0);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_TOGGLE_WRAP);
    CHECK(effect.toggle_wrap);

    effect = input_shortcut_effect(INPUT_SHORTCUT_ACTION_COPY);
    CHECK(!effect.open_palette);
    CHECK(!effect.history);
    CHECK(!effect.zoom);

    InputKeyPlan plan = input_key_plan(SHORTCUT_COPY, 1, 1, 1, 1);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_SHORTCUT);
    CHECK_EQ(plan.shortcut_action, INPUT_SHORTCUT_ACTION_COPY);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_OVERLAY);

    plan = input_key_plan(SHORTCUT_SAVE, 0, 0, 1, 0);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_SHORTCUT);
    CHECK_EQ(plan.shortcut_action, INPUT_SHORTCUT_ACTION_NONE);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_EDITOR);

    plan = input_key_plan(SHORTCUT_SAVE, 0, 0, 1, 1);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_SHORTCUT);
    CHECK_EQ(plan.shortcut_action, INPUT_SHORTCUT_ACTION_SAVE);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_EDITOR);

    plan = input_key_plan(SHORTCUT_NONE, 1, 1, 1, 1);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_OVERLAY);
    CHECK_EQ(plan.shortcut_action, INPUT_SHORTCUT_ACTION_NONE);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_OVERLAY);

    plan = input_key_plan(SHORTCUT_NONE, 0, 1, 1, 1);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_COMMAND);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_COMMAND);

    plan = input_key_plan(SHORTCUT_NONE, 0, 0, 1, 0);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_EDITOR);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_EDITOR);

    plan = input_key_plan(SHORTCUT_NONE, 0, 0, 0, 0);
    CHECK_EQ(plan.target, INPUT_KEY_TARGET_NONE);
    CHECK_EQ(plan.clipboard_target, INPUT_CLIPBOARD_NONE);

    LayoutClickTarget click = {0};
    InputMousePlan mouse = input_mouse_plan(click, 1, 0, 0, 1, 1, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_RELEASE_DRAG);
    CHECK(!mouse.dismiss_popover);
    CHECK(!mouse.record_activity);

    click.kind = LAYOUT_CLICK_TITLEBAR;
    mouse = input_mouse_plan(click, 0, 1, 1, 0, 1, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_TITLEBAR_MENU);
    CHECK(mouse.dismiss_popover);
    CHECK(!mouse.record_activity);

    click.titlebar_right = 0;
    mouse = input_mouse_plan(click, 1, 0, 1, 0, 0, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_TITLEBAR_LEFT);
    CHECK(!mouse.dismiss_popover);
    CHECK(!mouse.record_activity);

    click.kind = LAYOUT_CLICK_SIDEBAR;
    mouse = input_mouse_plan(click, 1, 0, 1, 0, 0, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_SIDEBAR);
    CHECK(mouse.record_activity);

    click.kind = LAYOUT_CLICK_TAB;
    mouse = input_mouse_plan(click, 1, 0, 1, 0, 0, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_TAB);
    CHECK(mouse.record_activity);

    click.kind = LAYOUT_CLICK_TEXT;
    mouse = input_mouse_plan(click, 1, 0, 1, 0, 0, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_NONE);
    CHECK(mouse.record_activity);
    mouse = input_mouse_plan(click, 1, 0, 1, 0, 0, 1);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_TEXT);
    CHECK(mouse.record_activity);

    click.kind = LAYOUT_CLICK_SIDEBAR;
    mouse = input_mouse_plan(click, 0, 1, 1, 0, 0, 0);
    CHECK_EQ(mouse.action, INPUT_MOUSE_ACTION_NONE);
    CHECK(!mouse.record_activity);

    TEST_REPORT();
}
