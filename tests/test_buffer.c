#include "../src/buffer.h"
#include "test.h"

int main(void) {
    /* edits produce correctly-shaped tree-sitter edit records */
    {
        Buffer *b = buffer_new();
        buffer_insert(b, 0, "int x;", 6);
        CHECK_EQ(buffer_length(b), 6);
        CHECK_EQ(buffer_pending_edits(b), 1);

        BufferEdit e;
        size_t n = buffer_take_edits(b, &e, 1);
        CHECK_EQ(n, 1);
        CHECK_EQ(e.start_byte, 0);
        CHECK_EQ(e.old_end_byte, 0);
        CHECK_EQ(e.new_end_byte, 6);
        CHECK_EQ(e.new_end_col, 6);
        CHECK_EQ(buffer_pending_edits(b), 0); /* drained */
        buffer_free(b);
    }

    /* a delete reports old > new end bytes */
    {
        Buffer *b = buffer_new();
        buffer_insert(b, 0, "hello", 5);
        buffer_take_edits(b, NULL, 0); /* clear */
        buffer_delete(b, 1, 3);        /* "ho" */
        CHECK_EQ(buffer_length(b), 2);

        BufferEdit e;
        buffer_take_edits(b, &e, 1);
        CHECK_EQ(e.start_byte, 1);
        CHECK_EQ(e.old_end_byte, 4);
        CHECK_EQ(e.new_end_byte, 1);
        buffer_free(b);
    }

    /* multi-line edit tracks row/col */
    {
        Buffer *b = buffer_new();
        buffer_insert(b, 0, "a\nb", 3);
        buffer_take_edits(b, NULL, 0);
        buffer_insert(b, 3, "\nc\nd", 4); /* append two lines */

        BufferEdit e;
        buffer_take_edits(b, &e, 1);
        CHECK_EQ(e.start_row, 1);   /* started on line 1 (after "a\n") */
        CHECK_EQ(e.start_col, 1);   /* after the 'b' */
        CHECK_EQ(e.new_end_row, 3); /* now three newlines deep */
        buffer_free(b);
    }

    /* save round-trips through the filesystem */
    {
        const char *path = "/tmp/wave_buffer_test.txt";
        Buffer *b = buffer_new();
        buffer_insert(b, 0, "persisted\n", 10);
        CHECK_EQ(buffer_save(b, path), 0);
        buffer_free(b);

        Buffer *b2 = buffer_open(path);
        CHECK(b2 != NULL);
        CHECK_EQ(buffer_length(b2), 10);
        char *s = pt_to_cstring(buffer_pt(b2), NULL);
        CHECK_STR(s, "persisted\n");
        free(s);
        buffer_free(b2);
    }

    TEST_REPORT();
}
