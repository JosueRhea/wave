/* piece_table.c — see piece_table.h for the contract. */
#include "piece_table.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { SRC_ORIGINAL, SRC_ADD } PieceSource;

typedef struct {
    PieceSource source;
    size_t start;    /* byte offset into the source buffer */
    size_t length;   /* byte length of this run */
    size_t newlines; /* count of '\n' within this run (cached) */
} Piece;

struct PieceTable {
    const char *original; /* read-only base text (mmap'd or owned copy) */
    size_t original_len;
    int original_mmapped; /* munmap vs free on destroy */

    char *add; /* append-only buffer for inserted text */
    size_t add_len;
    size_t add_cap;

    Piece *pieces;
    size_t piece_count;
    size_t piece_cap;

    size_t total_len;
    size_t total_newlines;

    /* Cached line-start index: line_starts[k] is the byte offset where line k
     * begins (line_starts[0] == 0), holding total_newlines+1 entries. Built
     * lazily and invalidated on every edit, it turns pt_line_start() into an
     * O(1) lookup and pt_offset_to_rowcol() into an O(log n) search — without
     * it both are O(document length), which makes the editor's per-frame wrap
     * layout O(n^2) and large files freeze. */
    size_t *line_starts;
    size_t line_starts_cap;
    int line_index_valid;
};

/* ---- small helpers ---- */

static size_t count_newlines(const char *s, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n') n++;
    return n;
}

static const char *src_ptr(const PieceTable *pt, const Piece *p) {
    return (p->source == SRC_ORIGINAL ? pt->original : pt->add) + p->start;
}

static void pieces_reserve(PieceTable *pt, size_t want) {
    if (want <= pt->piece_cap) return;
    size_t cap = pt->piece_cap ? pt->piece_cap * 2 : 8;
    while (cap < want) cap *= 2;
    pt->pieces = realloc(pt->pieces, cap * sizeof(Piece));
    pt->piece_cap = cap;
}

/* Insert a piece at array index `idx`, shifting the tail right. */
static void pieces_insert_at(PieceTable *pt, size_t idx, Piece pc) {
    pieces_reserve(pt, pt->piece_count + 1);
    memmove(&pt->pieces[idx + 1], &pt->pieces[idx],
            (pt->piece_count - idx) * sizeof(Piece));
    pt->pieces[idx] = pc;
    pt->piece_count++;
}

static void pieces_remove_at(PieceTable *pt, size_t idx) {
    memmove(&pt->pieces[idx], &pt->pieces[idx + 1],
            (pt->piece_count - idx - 1) * sizeof(Piece));
    pt->piece_count--;
}

/* Append text to the add buffer, returning its starting offset. */
static size_t add_append(PieceTable *pt, const char *text, size_t len) {
    if (pt->add_len + len > pt->add_cap) {
        size_t cap = pt->add_cap ? pt->add_cap : 256;
        while (cap < pt->add_len + len) cap *= 2;
        pt->add = realloc(pt->add, cap);
        pt->add_cap = cap;
    }
    size_t off = pt->add_len;
    memcpy(pt->add + off, text, len);
    pt->add_len += len;
    return off;
}

/* Locate the piece containing logical byte offset `pos`.
 * On return: *idx is the piece index, *piece_off is the offset of that piece's
 * start within the document. If pos == total_len, returns idx == piece_count. */
static void locate(const PieceTable *pt, size_t pos, size_t *idx,
                   size_t *piece_off) {
    size_t acc = 0;
    for (size_t i = 0; i < pt->piece_count; i++) {
        if (pos < acc + pt->pieces[i].length) {
            *idx = i;
            *piece_off = acc;
            return;
        }
        acc += pt->pieces[i].length;
    }
    *idx = pt->piece_count;
    *piece_off = acc;
}

/* ---- construction ---- */

static PieceTable *pt_alloc(void) {
    PieceTable *pt = calloc(1, sizeof(PieceTable));
    return pt;
}

/* Seed the table with a single original-spanning piece (if non-empty). */
static void pt_seed(PieceTable *pt) {
    pt->total_len = pt->original_len;
    pt->total_newlines = count_newlines(pt->original, pt->original_len);
    if (pt->original_len > 0) {
        pieces_reserve(pt, 1);
        pt->pieces[0] = (Piece){SRC_ORIGINAL, 0, pt->original_len,
                                pt->total_newlines};
        pt->piece_count = 1;
    }
}

PieceTable *pt_new_from_string(const char *text, size_t len) {
    PieceTable *pt = pt_alloc();
    if (!pt) return NULL;
    char *copy = malloc(len ? len : 1);
    if (len) memcpy(copy, text, len);
    pt->original = copy;
    pt->original_len = len;
    pt->original_mmapped = 0;
    pt_seed(pt);
    return pt;
}

PieceTable *pt_new_from_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }
    PieceTable *pt = pt_alloc();
    if (!pt) {
        close(fd);
        return NULL;
    }
    if (st.st_size > 0) {
        void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED) {
            free(pt);
            close(fd);
            return NULL;
        }
        pt->original = map;
        pt->original_len = (size_t)st.st_size;
        pt->original_mmapped = 1;
    }
    close(fd); /* mapping survives close */
    pt_seed(pt);
    return pt;
}

void pt_free(PieceTable *pt) {
    if (!pt) return;
    if (pt->original) {
        if (pt->original_mmapped)
            munmap((void *)pt->original, pt->original_len);
        else
            free((void *)pt->original);
    }
    free(pt->add);
    free(pt->pieces);
    free(pt->line_starts);
    free(pt);
}

/* ---- queries ---- */

size_t pt_length(const PieceTable *pt) { return pt->total_len; }

size_t pt_line_count(const PieceTable *pt) { return pt->total_newlines + 1; }

/* ---- mutation ---- */

void pt_insert(PieceTable *pt, size_t pos, const char *text, size_t len) {
    if (len == 0) return;
    if (pos > pt->total_len) pos = pt->total_len;

    size_t add_off = add_append(pt, text, len);
    Piece np = {SRC_ADD, add_off, len, count_newlines(text, len)};

    size_t idx, poff;
    locate(pt, pos, &idx, &poff);

    if (idx == pt->piece_count) {
        /* append at end */
        pieces_insert_at(pt, idx, np);
    } else if (pos == poff) {
        /* boundary insert, before piece idx */
        pieces_insert_at(pt, idx, np);
    } else {
        /* split piece idx into [left | new | right] */
        Piece *p = &pt->pieces[idx];
        size_t left_len = pos - poff;
        const char *base = src_ptr(pt, p);
        size_t left_nl = count_newlines(base, left_len);

        Piece right = {p->source, p->start + left_len, p->length - left_len,
                       p->newlines - left_nl};
        /* shrink current to the left part */
        p->length = left_len;
        p->newlines = left_nl;
        pieces_insert_at(pt, idx + 1, np);
        pieces_insert_at(pt, idx + 2, right);
    }

    pt->total_len += len;
    pt->total_newlines += np.newlines;
    pt->line_index_valid = 0;
}

void pt_delete(PieceTable *pt, size_t pos, size_t len) {
    if (len == 0 || pos >= pt->total_len) return;
    if (pos + len > pt->total_len) len = pt->total_len - pos;

    /* Walk left-to-right consuming `remaining` bytes. `acc` is the document
     * offset of pieces[i]'s start in the document as it shrinks; because we
     * only ever delete at or after `pos`, earlier pieces keep their offsets. */
    size_t remaining = len;
    size_t acc = 0, i = 0;
    while (i < pt->piece_count && acc + pt->pieces[i].length <= pos) {
        acc += pt->pieces[i].length;
        i++;
    }

    while (remaining > 0 && i < pt->piece_count) {
        Piece *p = &pt->pieces[i];
        const char *base = src_ptr(pt, p);
        size_t off = pos > acc ? pos - acc : 0; /* start within this piece */
        size_t avail = p->length - off;
        size_t take = avail < remaining ? avail : remaining;

        if (off == 0 && take == p->length) {
            /* whole piece removed; next piece slides into index i at offset acc */
            pt->total_newlines -= p->newlines;
            pieces_remove_at(pt, i);
        } else if (off == 0) {
            /* trim front (deletion ends inside this piece) */
            size_t nl = count_newlines(base, take);
            p->start += take;
            p->length -= take;
            p->newlines -= nl;
            pt->total_newlines -= nl;
        } else if (off + take == p->length) {
            /* trim tail; continue into following pieces at this offset */
            size_t nl = count_newlines(base + off, take);
            p->length = off;
            p->newlines -= nl;
            pt->total_newlines -= nl;
            acc += p->length;
            i++;
        } else {
            /* delete a middle chunk: split into left + right */
            size_t mid_nl = count_newlines(base + off, take);
            size_t left_nl = count_newlines(base, off);
            Piece right = {p->source, p->start + off + take,
                           p->length - off - take,
                           p->newlines - left_nl - mid_nl};
            p->length = off;
            p->newlines = left_nl;
            pieces_insert_at(pt, i + 1, right);
            pt->total_newlines -= mid_nl;
        }
        remaining -= take;
    }

    pt->total_len -= len;
    pt->line_index_valid = 0;
}

/* ---- reads ---- */

size_t pt_read(const PieceTable *pt, size_t pos, size_t len, char *out) {
    if (pos >= pt->total_len) return 0;
    if (pos + len > pt->total_len) len = pt->total_len - pos;

    size_t acc = 0, written = 0, remaining = len;
    for (size_t i = 0; i < pt->piece_count && remaining > 0; i++) {
        const Piece *p = &pt->pieces[i];
        size_t p_end = acc + p->length;
        if (p_end > pos) {
            size_t off = pos > acc ? pos - acc : 0;
            size_t avail = p->length - off;
            size_t n = avail < remaining ? avail : remaining;
            memcpy(out + written, src_ptr(pt, p) + off, n);
            written += n;
            remaining -= n;
            pos += n;
        }
        acc = p_end;
    }
    return written;
}

char *pt_to_cstring(const PieceTable *pt, size_t *out_len) {
    char *buf = malloc(pt->total_len + 1);
    size_t n = pt_read(pt, 0, pt->total_len, buf);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ---- line index ---- */

/* (Re)build the cached line-start index by a single linear scan of the document
 * (O(n), once per edit). `pt` is logically const to callers — the index is a
 * memoized view of immutable-until-next-edit state — so we cast away const. */
static void ensure_line_index(const PieceTable *cpt) {
    if (cpt->line_index_valid) return;
    PieceTable *pt = (PieceTable *)cpt;

    size_t want = pt->total_newlines + 1;
    if (want > pt->line_starts_cap) {
        size_t cap = pt->line_starts_cap ? pt->line_starts_cap : 256;
        while (cap < want) cap *= 2;
        pt->line_starts = realloc(pt->line_starts, cap * sizeof(size_t));
        pt->line_starts_cap = cap;
    }

    size_t idx = 0, acc = 0;
    pt->line_starts[idx++] = 0; /* line 0 starts at offset 0 */
    for (size_t i = 0; i < pt->piece_count; i++) {
        const Piece *p = &pt->pieces[i];
        if (p->newlines == 0) { acc += p->length; continue; }
        const char *base = src_ptr(pt, p);
        for (size_t j = 0; j < p->length; j++)
            if (base[j] == '\n') pt->line_starts[idx++] = acc + j + 1;
        acc += p->length;
    }
    pt->line_index_valid = 1;
}

size_t pt_line_start(const PieceTable *pt, size_t line) {
    if (line == 0) return 0;
    if (line > pt->total_newlines) return pt->total_len; /* line beyond EOF */
    ensure_line_index(pt);
    return pt->line_starts[line];
}

void pt_offset_to_rowcol(const PieceTable *pt, size_t offset, size_t *row,
                         size_t *col) {
    if (offset > pt->total_len) offset = pt->total_len;
    ensure_line_index(pt);
    /* largest line index whose start is <= offset (binary search) */
    size_t lo = 0, hi = pt->total_newlines;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (pt->line_starts[mid] <= offset) lo = mid; else hi = mid - 1;
    }
    if (row) *row = lo;
    if (col) *col = offset - pt->line_starts[lo];
}
