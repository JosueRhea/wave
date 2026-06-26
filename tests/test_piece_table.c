#include "../src/piece_table.h"
#include "test.h"

static char *dup_doc(const PieceTable *pt) {
    return pt_to_cstring(pt, NULL);
}

int main(void) {
    /* basic construction + read */
    {
        PieceTable *pt = pt_new_from_string("hello world", 11);
        CHECK_EQ(pt_length(pt), 11);
        char *s = dup_doc(pt);
        CHECK_STR(s, "hello world");
        free(s);
        pt_free(pt);
    }

    /* insert at start, middle, end */
    {
        PieceTable *pt = pt_new_from_string("BD", 2);
        pt_insert(pt, 1, "C", 1); /* B C D */
        pt_insert(pt, 0, "A", 1); /* A B C D */
        pt_insert(pt, 4, "E", 1); /* A B C D E */
        char *s = dup_doc(pt);
        CHECK_STR(s, "ABCDE");
        CHECK_EQ(pt_length(pt), 5);
        free(s);
        pt_free(pt);
    }

    /* delete: whole piece, front trim, tail trim, middle split */
    {
        PieceTable *pt = pt_new_from_string("0123456789", 10);
        pt_delete(pt, 3, 4); /* remove "3456" -> "012789" */
        char *s = dup_doc(pt);
        CHECK_STR(s, "012789");
        CHECK_EQ(pt_length(pt), 6);
        free(s);

        pt_delete(pt, 0, 2); /* front -> "2789" */
        s = dup_doc(pt);
        CHECK_STR(s, "2789");
        free(s);

        pt_delete(pt, 2, 2); /* tail -> "27" */
        s = dup_doc(pt);
        CHECK_STR(s, "27");
        free(s);
        pt_free(pt);
    }

    /* interleaved insert/delete sanity */
    {
        PieceTable *pt = pt_new_from_string("the cat", 7);
        pt_insert(pt, 4, "fat ", 4); /* "the fat cat" */
        char *s = dup_doc(pt);
        CHECK_STR(s, "the fat cat");
        free(s);
        pt_delete(pt, 4, 4); /* back to "the cat" */
        s = dup_doc(pt);
        CHECK_STR(s, "the cat");
        free(s);
        pt_free(pt);
    }

    /* partial read into a fixed buffer */
    {
        PieceTable *pt = pt_new_from_string("abcdefgh", 8);
        char buf[4];
        size_t n = pt_read(pt, 2, 3, buf);
        CHECK_EQ(n, 3);
        CHECK(memcmp(buf, "cde", 3) == 0);
        /* clamped at EOF */
        n = pt_read(pt, 6, 10, buf);
        CHECK_EQ(n, 2);
        CHECK(memcmp(buf, "gh", 2) == 0);
        pt_free(pt);
    }

    /* line counting + line starts across edits */
    {
        PieceTable *pt = pt_new_from_string("a\nbb\nccc\n", 9);
        CHECK_EQ(pt_line_count(pt), 4); /* 3 newlines -> 4 lines */
        CHECK_EQ(pt_line_start(pt, 0), 0);
        CHECK_EQ(pt_line_start(pt, 1), 2);
        CHECK_EQ(pt_line_start(pt, 2), 5);
        CHECK_EQ(pt_line_start(pt, 3), 9);

        pt_insert(pt, 2, "X\n", 2); /* "a\nX\nbb\nccc\n" */
        CHECK_EQ(pt_line_count(pt), 5);
        CHECK_EQ(pt_line_start(pt, 2), 4);
        pt_free(pt);
    }

    /* offset -> (row,col) */
    {
        PieceTable *pt = pt_new_from_string("ab\ncd\nef", 8);
        size_t row, col;
        pt_offset_to_rowcol(pt, 0, &row, &col);
        CHECK_EQ(row, 0); CHECK_EQ(col, 0);
        pt_offset_to_rowcol(pt, 4, &row, &col); /* 'd' on line 1 */
        CHECK_EQ(row, 1); CHECK_EQ(col, 1);
        pt_offset_to_rowcol(pt, 7, &row, &col); /* 'f' on line 2 */
        CHECK_EQ(row, 2); CHECK_EQ(col, 1);
        pt_free(pt);
    }

    /* cached line index: stays correct across queries interleaved with edits,
     * and at boundaries (this is what makes wrap layout O(n), not O(n^2)). */
    {
        PieceTable *pt = pt_new_from_string("l0\nl1\nl2", 8);
        size_t row, col;
        /* query first (builds the index), then mutate, then re-query */
        CHECK_EQ(pt_line_start(pt, 2), 6);
        pt_offset_to_rowcol(pt, 7, &row, &col);
        CHECK_EQ(row, 2); CHECK_EQ(col, 1);

        pt_insert(pt, 0, "X\n", 2); /* "X\nl0\nl1\nl2": index must rebuild */
        CHECK_EQ(pt_line_count(pt), 4);
        CHECK_EQ(pt_line_start(pt, 0), 0);
        CHECK_EQ(pt_line_start(pt, 1), 2);
        CHECK_EQ(pt_line_start(pt, 3), 8);
        pt_offset_to_rowcol(pt, 8, &row, &col); /* start of "l2" */
        CHECK_EQ(row, 3); CHECK_EQ(col, 0);

        pt_delete(pt, 0, 2); /* back to "l0\nl1\nl2": index must rebuild again */
        CHECK_EQ(pt_line_count(pt), 3);
        CHECK_EQ(pt_line_start(pt, 1), 3);
        /* boundaries: line 0, line beyond EOF, and offset == length */
        CHECK_EQ(pt_line_start(pt, 0), 0);
        CHECK_EQ(pt_line_start(pt, 99), pt_length(pt));
        pt_offset_to_rowcol(pt, pt_length(pt), &row, &col);
        CHECK_EQ(row, 2); CHECK_EQ(col, 2);
        pt_free(pt);
    }

    TEST_REPORT();
}
