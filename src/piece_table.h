/* piece_table.h — append-only piece table for editor text storage.
 *
 * Original text is kept read-only (mmap'd for files); all inserts append to a
 * growable "add" buffer and are recorded as pieces. Edits never copy the bulk
 * of the document, so large files open and edit in O(1)-ish time.
 *
 * NOTE: pieces are stored in a flat dynamic array for v1. Insert/delete are
 * O(#pieces) due to array shifts. The interface is intentionally stable so the
 * backing store can be upgraded to a balanced piece tree (red-black, keyed by
 * byte offset) without touching callers.
 */
#ifndef WAVE_PIECE_TABLE_H
#define WAVE_PIECE_TABLE_H

#include <stddef.h>

typedef struct PieceTable PieceTable;

/* Construct from an in-memory string (copied into the original buffer). */
PieceTable *pt_new_from_string(const char *text, size_t len);

/* Construct by mmap'ing a file read-only. Returns NULL on failure. */
PieceTable *pt_new_from_file(const char *path);

void pt_free(PieceTable *pt);

/* Total byte length of the current document. */
size_t pt_length(const PieceTable *pt);

/* Total line count (number of '\n' + 1). */
size_t pt_line_count(const PieceTable *pt);

/* Insert `len` bytes at byte offset `pos`. pos is clamped to [0, length]. */
void pt_insert(PieceTable *pt, size_t pos, const char *text, size_t len);

/* Delete `len` bytes starting at byte offset `pos`. Range is clamped. */
void pt_delete(PieceTable *pt, size_t pos, size_t len);

/* Copy [pos, pos+len) into caller-provided `out` (must hold len bytes).
 * Returns the number of bytes actually copied (clamped to document end). */
size_t pt_read(const PieceTable *pt, size_t pos, size_t len, char *out);

/* Allocate and return a NUL-terminated copy of the whole document.
 * Caller frees. *out_len receives the byte length (excluding NUL). */
char *pt_to_cstring(const PieceTable *pt, size_t *out_len);

/* Byte offset of the start of 0-based line `line`. Clamped to line_count. */
size_t pt_line_start(const PieceTable *pt, size_t line);

/* Convert a byte offset to 0-based (row, col) where col is a byte column. */
void pt_offset_to_rowcol(const PieceTable *pt, size_t offset,
                         size_t *row, size_t *col);

#endif /* WAVE_PIECE_TABLE_H */
