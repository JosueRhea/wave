/* test_popover.c - popover state and snapshot text helpers. */
#include "test.h"
#include "popover.h"

#include <string.h>

int main(void) {
    Popover p;
    popover_init(&p);

    popover_set_encoded_base(&p, "one\\ntwo|three");
    CHECK_STR(p.base, "one\ntwo\n\nthree");
    CHECK_EQ(p.scroll, 0);

    p.scroll = 4;
    popover_set_encoded_base(&p, NULL);
    CHECK_STR(p.base, "");
    CHECK_EQ(p.scroll, 0);

    popover_set_encoded_base(NULL, "ignored");

    char encoded[5000];
    memset(encoded, 'a', sizeof encoded);
    encoded[sizeof encoded - 1] = '\0';
    popover_set_encoded_base(&p, encoded);
    CHECK_EQ(strlen(p.base), sizeof p.base - 2);
    CHECK_EQ(p.base[sizeof p.base - 2], '\0');

    popover_set_encoded_base(&p, "alpha|beta");
    popover_set_loading(&p, 0);
    popover_compose(&p, NULL);
    CHECK(p.active);
    CHECK_STR(p.text, "alpha\n\nbeta");

    popover_show_base(&p, "base", 1);
    CHECK(p.active);
    CHECK(p.loading);
    CHECK_STR(p.text, "base\n\nLoading...");
    popover_show_hover(&p, "hover");
    CHECK(p.active);
    CHECK(!p.loading);
    CHECK_STR(p.text, "hover\n\nbase");
    CHECK_STR(p.base, "hover");
    popover_set_scroll(&p, 3);
    CHECK_EQ(p.scroll, 3);
    popover_set_scroll(&p, 0);

    popover_close(&p);
    popover_show_hover(&p, "ignored");
    CHECK(!p.active);

    popover_show_base(&p, "node: identifier  [8 bytes]", 1);
    popover_show_hover(&p, "fn add(value: number): number\n\nAdds a value.");
    CHECK_STR(p.text, "fn add(value: number): number\n\nAdds a value.");
    CHECK(!strstr(p.text, "node: identifier"));

    popover_show_base(&p, "node: identifier  [8 bytes]", 1);
    popover_show_hover(&p, "");
    CHECK(!p.loading);
    CHECK_STR(p.text, "node: identifier  [8 bytes]");

    popover_show_encoded_base(&p, "snap\\nshot|text", 5);
    CHECK(p.active);
    CHECK(!p.loading);
    CHECK_EQ(p.scroll, 5);
    CHECK_STR(p.base, "snap\nshot\n\ntext");
    CHECK_STR(p.text, "snap\nshot\n\ntext");
    popover_show_encoded_base(NULL, "ignored", 1);

    popover_set_scroll(&p, 0);
    popover_compose(&p, NULL);
    CHECK(!popover_is_scrollable(&p));
    CHECK(!popover_is_scrollable(NULL));
    popover_set_view(&p, 5, 2);
    CHECK(popover_is_scrollable(&p));
    CHECK_EQ(popover_apply_normal_char(&p, 'j', 0), 1);
    CHECK_EQ(p.scroll, 1);
    CHECK_EQ(popover_apply_normal_char(&p, 'k', 0), 1);
    CHECK_EQ(p.scroll, 0);
    CHECK_EQ(popover_apply_normal_char(&p, 'g', 0), 0);
    CHECK(p.active);
    CHECK_EQ(popover_apply_normal_char(&p, 'x', 1), 0);
    CHECK(p.active);
    CHECK_EQ(popover_apply_normal_char(&p, 'x', 0), 0);
    CHECK(!p.active);
    CHECK(!popover_is_scrollable(&p));

    popover_compose(&p, "hover");
    popover_set_view(&p, 4, 2);
    CHECK_EQ(popover_apply_key(&p, POPOVER_KEY_DOWN, 0), 1);
    CHECK_EQ(p.scroll, 1);
    CHECK_EQ(popover_apply_key(&p, POPOVER_KEY_UP, 0), 1);
    CHECK_EQ(p.scroll, 0);
    CHECK_EQ(popover_apply_key(&p, POPOVER_KEY_DOWN, 1), 0);
    CHECK_EQ(p.scroll, 0);
    CHECK(p.active);
    CHECK_EQ(popover_apply_key(&p, POPOVER_KEY_ESCAPE, 0), 1);
    CHECK(!p.active);
    CHECK_EQ(popover_apply_key(&p, POPOVER_KEY_ESCAPE, 0), 0);
    CHECK_EQ(popover_apply_normal_char(NULL, 'j', 0), 0);
    CHECK_EQ(popover_apply_key(NULL, POPOVER_KEY_DOWN, 0), 0);

    TEST_REPORT();
}
