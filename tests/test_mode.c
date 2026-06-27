/* test_mode.c — modal state transitions independent of editor commands. */
#include "test.h"
#include "mode.h"

int main(void) {
    ModalState m;
    modal_init(&m);
    CHECK_EQ(m.mode, MODE_NORMAL);
    CHECK_EQ(m.pending, 0);
    CHECK_EQ(modal_count_or_one(&m), 1);

    CHECK(modal_push_count(&m, '2'));
    CHECK(modal_push_count(&m, '0'));
    CHECK_EQ(m.count, 20);
    CHECK_EQ(modal_count_or_one(&m), 20);
    CHECK(!modal_push_count(&m, 'x'));

    modal_set_pending(&m, 'd');
    m.op_g = 1;
    modal_enter_insert(&m);
    CHECK_EQ(m.mode, MODE_INSERT);
    CHECK_EQ(m.pending, 0);
    CHECK_EQ(m.count, 0);
    CHECK_EQ(m.op_g, 0);

    modal_set_pending(&m, 'g');
    m.count = 4;
    CHECK_EQ(modal_take_pending(&m), 'g');
    CHECK_EQ(m.pending, 0);
    CHECK_EQ(m.count, 0);

    modal_wait_g_motion(&m, 'd');
    CHECK_EQ(m.pending, 'd');
    CHECK_EQ(m.op_g, 1);

    modal_toggle_visual(&m);
    CHECK_EQ(m.mode, MODE_VISUAL);
    modal_toggle_visual(&m);
    CHECK_EQ(m.mode, MODE_NORMAL);

    m.pending = 'g';
    m.op_g = 1;
    m.count = 3;
    modal_clear_pending(&m);
    CHECK_EQ(m.pending, 0);
    CHECK_EQ(m.op_g, 0);
    CHECK_EQ(m.count, 0);

    CHECK_STR(mode_name(MODE_INSERT), "INSERT");
    CHECK_STR(mode_name(MODE_VISUAL), "VISUAL");
    CHECK_STR(mode_name(MODE_NORMAL), "NORMAL");

    TEST_REPORT();
}
