/* test_yank.c — yank register storage independent of the GUI clipboard. */
#include "test.h"
#include "buffer.h"
#include "yank.h"

#include <string.h>

int main(void) {
    YankRegister y = {0};

    CHECK(yank_empty(&y));
    CHECK_EQ(yank_set(&y, "hello", 5, 0), 0);
    CHECK(!yank_empty(&y));
    CHECK_STR(y.text, "hello");
    CHECK_EQ(y.len, 5);
    CHECK_EQ(y.line_wise, 0);

    CHECK_EQ(yank_set(&y, "line\n", 5, 1), 0);
    CHECK_STR(y.text, "line\n");
    CHECK_EQ(y.len, 5);
    CHECK_EQ(y.line_wise, 1);

    Buffer *b = buffer_new();
    buffer_insert(b, 0, "alpha beta gamma", strlen("alpha beta gamma"));
    CHECK(yank_from_range(&y, buffer_pt(b), 6, 10, 0));
    CHECK_STR(y.text, "beta");
    CHECK_EQ(y.len, 4);
    CHECK_EQ(y.line_wise, 0);
    buffer_free(b);

    yank_free(&y);
    CHECK(yank_empty(&y));

    TEST_REPORT();
}
