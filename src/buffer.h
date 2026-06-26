/* buffer.h — a document: text storage plus the edit-event stream that the
 * syntax layer (tree-sitter) needs to reparse incrementally.
 *
 * Every mutation records a BufferEdit describing the byte range that changed,
 * in exactly the shape tree-sitter's TSInputEdit wants. The highlight layer
 * drains these and feeds ts_tree_edit() before reparsing.
 */
#ifndef WAVE_BUFFER_H
#define WAVE_BUFFER_H

#include <stddef.h>
#include "piece_table.h"

/* One edit in tree-sitter's coordinate vocabulary. Byte offsets plus the
 * (row,col) points tree-sitter requires. column is a byte column. */
typedef struct {
    size_t start_byte;
    size_t old_end_byte;
    size_t new_end_byte;
    size_t start_row, start_col;
    size_t old_end_row, old_end_col;
    size_t new_end_row, new_end_col;
} BufferEdit;

typedef struct Buffer Buffer;

Buffer *buffer_new(void);
Buffer *buffer_open(const char *path); /* NULL on failure */
void buffer_free(Buffer *b);

/* Persist to `path` (or the path it was opened with if path is NULL). */
int buffer_save(Buffer *b, const char *path);

const PieceTable *buffer_pt(const Buffer *b);
size_t buffer_length(const Buffer *b);
size_t buffer_line_count(const Buffer *b);

/* Edits. Each records a BufferEdit into the pending queue. */
void buffer_insert(Buffer *b, size_t pos, const char *text, size_t len);
void buffer_delete(Buffer *b, size_t pos, size_t len);

/* Drain pending edits in order. Returns count; copies up to `max` into `out`
 * and clears the queue. Pass out=NULL,max=0 to just clear. */
size_t buffer_take_edits(Buffer *b, BufferEdit *out, size_t max);
size_t buffer_pending_edits(const Buffer *b);

#endif /* WAVE_BUFFER_H */
