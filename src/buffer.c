/* buffer.c — see buffer.h. */
#include "buffer.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Buffer {
    PieceTable *pt;
    char *path; /* owned; may be NULL for unsaved buffers */

    BufferEdit *edits;
    size_t edit_count;
    size_t edit_cap;
};

static void edits_push(Buffer *b, BufferEdit e) {
    if (b->edit_count == b->edit_cap) {
        b->edit_cap = b->edit_cap ? b->edit_cap * 2 : 8;
        b->edits = realloc(b->edits, b->edit_cap * sizeof(BufferEdit));
    }
    b->edits[b->edit_count++] = e;
}

Buffer *buffer_new(void) {
    Buffer *b = calloc(1, sizeof(Buffer));
    b->pt = pt_new_from_string("", 0);
    return b;
}

Buffer *buffer_open(const char *path) {
    PieceTable *pt = pt_new_from_file(path);
    if (!pt) return NULL;
    Buffer *b = calloc(1, sizeof(Buffer));
    b->pt = pt;
    b->path = strdup(path);
    return b;
}

void buffer_free(Buffer *b) {
    if (!b) return;
    pt_free(b->pt);
    free(b->path);
    free(b->edits);
    free(b);
}

int buffer_save(Buffer *b, const char *path) {
    const char *target = path ? path : b->path;
    if (!target) return -1;
    int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t len;
    char *data = pt_to_cstring(b->pt, &len);
    ssize_t w = write(fd, data, len);
    free(data);
    close(fd);
    if (w < 0 || (size_t)w != len) return -1;
    if (path && path != b->path) {
        free(b->path);
        b->path = strdup(path);
    }
    return 0;
}

const PieceTable *buffer_pt(const Buffer *b) { return b->pt; }
size_t buffer_length(const Buffer *b) { return pt_length(b->pt); }
size_t buffer_line_count(const Buffer *b) { return pt_line_count(b->pt); }

void buffer_insert(Buffer *b, size_t pos, const char *text, size_t len) {
    if (len == 0) return;
    if (pos > pt_length(b->pt)) pos = pt_length(b->pt);

    BufferEdit e;
    e.start_byte = pos;
    e.old_end_byte = pos;
    e.new_end_byte = pos + len;
    pt_offset_to_rowcol(b->pt, e.start_byte, &e.start_row, &e.start_col);
    e.old_end_row = e.start_row;
    e.old_end_col = e.start_col;

    pt_insert(b->pt, pos, text, len);
    pt_offset_to_rowcol(b->pt, e.new_end_byte, &e.new_end_row, &e.new_end_col);
    edits_push(b, e);
}

void buffer_delete(Buffer *b, size_t pos, size_t len) {
    size_t total = pt_length(b->pt);
    if (len == 0 || pos >= total) return;
    if (pos + len > total) len = total - pos;

    BufferEdit e;
    e.start_byte = pos;
    e.old_end_byte = pos + len;
    e.new_end_byte = pos;
    pt_offset_to_rowcol(b->pt, e.start_byte, &e.start_row, &e.start_col);
    pt_offset_to_rowcol(b->pt, e.old_end_byte, &e.old_end_row, &e.old_end_col);

    pt_delete(b->pt, pos, len);
    e.new_end_row = e.start_row;
    e.new_end_col = e.start_col;
    edits_push(b, e);
}

size_t buffer_pending_edits(const Buffer *b) { return b->edit_count; }

size_t buffer_take_edits(Buffer *b, BufferEdit *out, size_t max) {
    size_t n = b->edit_count;
    if (out) {
        size_t copy = n < max ? n : max;
        memcpy(out, b->edits, copy * sizeof(BufferEdit));
    }
    b->edit_count = 0;
    return n;
}
