#include "edit_command.h"

#include "standard.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void enter_insert(Editor *e, ModalState *modal) {
    modal_enter_insert(modal);
    if (e) e->group_open = 0;
}

static size_t first_nonblank(const PieceTable *pt, size_t off) {
    size_t s = line_start_of(pt, off), le = line_end_of(pt, off);
    while (s < le && (byte_at(pt, s) == ' ' || byte_at(pt, s) == '\t')) s++;
    return s;
}

static size_t last_real_row(const PieceTable *pt) {
    size_t lines = pt_line_count(pt);
    if (lines == 0) return 0;
    size_t row = lines - 1;
    if (row > 0 && pt_line_start(pt, row) == pt_length(pt)) row--;
    return row;
}

static int yank_range(Editor *e, YankRegister *yank, size_t a, size_t b,
                      int line_wise, EditCommandResult *res) {
    if (!e || !e->buf || !yank) return 0;
    if (!yank_from_range(yank, buffer_pt(e->buf), a, b, line_wise)) return 0;
    if (res) res->flags |= EDIT_COMMAND_YANKED;
    return 1;
}

/* Place carets at `col` on every line of the block (clamped to the line), enter
 * insert, and keep the extras so subsequent typing edits the column. */
static void block_insert_at_col(Editor *e, ModalState *modal, size_t row0,
                                size_t row1, size_t col) {
    const PieceTable *pt = buffer_pt(e->buf);
    standard_clear_carets(e);
    int first = 1;
    for (size_t row = row0; row <= row1; row++) {
        size_t start = pt_line_start(pt, row);
        size_t end = line_end_of(pt, start);
        size_t len = end - start;
        size_t at = start + (col < len ? col : len);
        if (first) {
            e->cursor = e->anchor = at;
            first = 0;
        } else {
            standard_add_caret(e, at, at);
        }
    }
    enter_insert(e, modal);
}

static int visual_block_command(Editor *e, ModalState *modal, YankRegister *yank,
                                unsigned int cp, EditCommandResult *res) {
    size_t row0, row1, col0, col1;
    if (!editor_block_bounds(e, &row0, &row1, &col0, &col1)) return 0;

    if (cp == 'I') {
        block_insert_at_col(e, modal, row0, row1, col0);
        return 1;
    }
    if (cp == 'A') {
        block_insert_at_col(e, modal, row0, row1, col1);
        return 1;
    }
    if (cp != 'd' && cp != 'x' && cp != 'y' && cp != 'c') return 0;

    /* Build yank text top-down; delete bottom-up so offsets stay valid. */
    size_t cap = 0;
    for (size_t row = row0; row <= row1; row++) {
        EditorRange r;
        if (editor_block_line_range(e, row, col0, col1, &r))
            cap += r.end - r.start;
        cap += 1; /* newline between rows (and a final one is fine / trimmed) */
    }
    char *buf = malloc(cap + 1);
    size_t n = 0;
    if (buf) {
        for (size_t row = row0; row <= row1; row++) {
            if (row > row0) buf[n++] = '\n';
            EditorRange r;
            if (editor_block_line_range(e, row, col0, col1, &r)) {
                char *slice = editor_range_text(e, r.start, r.end);
                if (slice) {
                    size_t sn = strlen(slice);
                    memcpy(buf + n, slice, sn);
                    n += sn;
                    free(slice);
                }
            }
        }
        buf[n] = '\0';
        if (yank_set(yank, buf, n, 0) == 0) res->flags |= EDIT_COMMAND_YANKED;
        free(buf);
    }

    if (cp == 'y') {
        e->cursor = e->anchor;
        modal_enter_normal(modal);
        return 1;
    }

    for (size_t row = row1;;) {
        EditorRange r;
        if (editor_block_line_range(e, row, col0, col1, &r))
            ed_delete_range(e, r.start, r.end);
        if (row == row0) break;
        row--;
    }

    if (cp == 'c') {
        block_insert_at_col(e, modal, row0, row1, col0);
    } else {
        /* Land on the top-left of the former block. */
        const PieceTable *pt = buffer_pt(e->buf);
        size_t start = pt_line_start(pt, row0);
        size_t end = line_end_of(pt, start);
        size_t len = end - start;
        e->cursor = e->anchor = start + (col0 < len ? col0 : len);
        standard_clear_carets(e);
        modal_enter_normal(modal);
    }
    return 1;
}

EditCommandResult edit_command_apply(Editor *e, ModalState *modal,
                                     YankRegister *yank, unsigned int cp) {
    EditCommandResult res = {0};
    if (!e || !e->buf || !modal) return res;

    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);
    e->group_open = 0;

    if (modal_push_count(modal, cp)) return res;
    int count = modal_count_or_one(modal);
    int had_count = modal->count != 0;

    if (modal->pending) {
        char op = modal_take_pending(modal);
        if (op == 'g') {
            switch (cp) {
            case 'g': e->cursor = 0; break;
            case 'h': res.flags |= EDIT_COMMAND_SHOW_INFO; break;
            case 'd': res.flags |= EDIT_COMMAND_GOTO_DEFINITION; break;
            case 't': res.flags |= EDIT_COMMAND_TAB_NEXT; break;
            case 'T': res.flags |= EDIT_COMMAND_TAB_PREV; break;
            default: break;
            }
            return res;
        }

        if (op == 'd' || op == 'c' || op == 'y') {
            size_t a = e->cursor, b = e->cursor;
            int line_wise = 0, ok = 1;
            size_t row, col;
            pt_offset_to_rowcol(pt, e->cursor, &row, &col);
            size_t lines = pt_line_count(pt);

            if (modal->op_g) {
                modal->op_g = 0;
                if (cp == 'g') {
                    a = 0;
                    size_t le = line_end_of(pt, e->cursor);
                    b = (le < len) ? le + 1 : le;
                    line_wise = 1;
                } else ok = 0;
            } else if (cp == 'g') {
                modal_wait_g_motion(modal, op);
                return res;
            } else if (cp == (unsigned int)(unsigned char)op) {
                a = line_start_of(pt, e->cursor);
                size_t endrow = row + (size_t)count - 1;
                if (endrow >= lines) endrow = lines ? lines - 1 : 0;
                size_t le = line_end_of(pt, pt_line_start(pt, endrow));
                b = (le < len) ? le + 1 : le;
                line_wise = 1;
                if (op == 'c') {
                    a = line_start_of(pt, e->cursor);
                    b = le;
                }
            } else if (cp == 'j' || cp == 'k' || cp == 'G') {
                size_t lo = row, hi = row;
                if (cp == 'j') hi = row + (size_t)count;
                else if (cp == 'k') lo = row >= (size_t)count ? row - (size_t)count : 0;
                else {
                    hi = had_count ? (size_t)count - 1 : (lines ? lines - 1 : 0);
                    if (hi < row) { lo = hi; hi = row; }
                }
                if (hi >= lines) hi = lines ? lines - 1 : 0;
                a = pt_line_start(pt, lo);
                size_t le = line_end_of(pt, pt_line_start(pt, hi));
                b = (le < len) ? le + 1 : le;
                line_wise = 1;
            } else if (cp == 'w') {
                for (int i = 0; i < count; i++) b = word_next(pt, b);
            } else if (cp == 'b') {
                for (int i = 0; i < count; i++) a = word_prev(pt, a);
            } else if (cp == 'e') {
                for (int i = 0; i < count; i++) b = word_end(pt, b) + 1;
            } else if (cp == 'h') {
                for (int i = 0; i < count; i++) a = prev_boundary(pt, a);
            } else if (cp == 'l') {
                for (int i = 0; i < count; i++) b = next_boundary(pt, b);
            } else if (cp == '$') {
                b = line_end_of(pt, e->cursor);
            } else if (cp == '0') {
                a = line_start_of(pt, e->cursor);
            } else if (cp == '^') {
                a = first_nonblank(pt, e->cursor);
            } else {
                ok = 0;
            }

            if (ok && b > a) {
                if (op == 'y') {
                    if (yank_range(e, yank, a, b, line_wise, &res)) e->cursor = a;
                } else {
                    yank_range(e, yank, a, b, line_wise, &res);
                    ed_delete_range(e, a, b);
                    if (op == 'c') enter_insert(e, modal);
                }
            } else if (ok && op == 'c') {
                enter_insert(e, modal);
            }
        }
        return res;
    }

    if (modal->mode == MODE_VISUAL_BLOCK) {
        if (visual_block_command(e, modal, yank, cp, &res)) {
            modal->count = 0;
            return res;
        }
    }

    if (modal->mode == MODE_VISUAL && (cp == 'd' || cp == 'x' || cp == 'y' || cp == 'c')) {
        EditorRange r;
        if (!editor_visual_range(e, &r)) return res;
        if (cp == 'y') {
            if (yank_range(e, yank, r.start, r.end, 0, &res)) e->cursor = r.start;
        } else {
            yank_range(e, yank, r.start, r.end, 0, &res);
            ed_delete_range(e, r.start, r.end);
            if (cp == 'c') enter_insert(e, modal);
        }
        if (cp != 'c') modal_enter_normal(modal);
        else modal->count = 0;
        return res;
    }

    switch (cp) {
    case 'h': for (int i = 0; i < count; i++) e->cursor = prev_boundary(pt, e->cursor); break;
    case 'l': for (int i = 0; i < count; i++) e->cursor = next_boundary(pt, e->cursor); break;
    case 'k': for (int i = 0; i < count; i++) move_vert(e, -1); break;
    case 'j': for (int i = 0; i < count; i++) move_vert(e, +1); break;
    case 'w': for (int i = 0; i < count; i++) e->cursor = word_next(pt, e->cursor); break;
    case 'b': for (int i = 0; i < count; i++) e->cursor = word_prev(pt, e->cursor); break;
    case 'e': for (int i = 0; i < count; i++) e->cursor = word_end(pt, e->cursor); break;
    case '0': e->cursor = line_start_of(pt, e->cursor); break;
    case '^': e->cursor = first_nonblank(pt, e->cursor); break;
    case '$': e->cursor = line_end_of(pt, e->cursor); break;
    case 'G': {
        if (had_count) {
            size_t row = (size_t)count - 1;
            size_t last = last_real_row(pt);
            if (row > last) row = last;
            e->cursor = pt_line_start(pt, row);
        } else {
            e->cursor = len;
        }
        break;
    }
    case 'g': modal_set_pending(modal, 'g'); return res;

    case 'i': enter_insert(e, modal); break;
    case 'a': {
        size_t le = line_end_of(pt, e->cursor);
        if (e->cursor < le) {
            size_t next = next_boundary(pt, e->cursor);
            e->cursor = next < le ? next : le;
        }
        enter_insert(e, modal);
        break;
    }
    case 'I': e->cursor = first_nonblank(pt, e->cursor); enter_insert(e, modal); break;
    case 'A': e->cursor = line_end_of(pt, e->cursor); enter_insert(e, modal); break;
    case 'o': {
        if (editor_open_line(e, 1)) enter_insert(e, modal);
        break;
    }
    case 'O': {
        if (editor_open_line(e, 0)) enter_insert(e, modal);
        break;
    }

    case 'x': {
        size_t nx = next_boundary(pt, e->cursor);
        if (nx > e->cursor) {
            yank_range(e, yank, e->cursor, nx, 0, &res);
            ed_delete_range(e, e->cursor, nx);
        }
        break;
    }
    case 's': {
        size_t nx = next_boundary(pt, e->cursor);
        if (nx > e->cursor) ed_delete_range(e, e->cursor, nx);
        enter_insert(e, modal);
        break;
    }
    case 'D': {
        size_t le = line_end_of(pt, e->cursor);
        yank_range(e, yank, e->cursor, le, 0, &res);
        ed_delete_range(e, e->cursor, le);
        break;
    }
    case 'C': {
        size_t le = line_end_of(pt, e->cursor);
        ed_delete_range(e, e->cursor, le);
        enter_insert(e, modal);
        break;
    }
    case 'd': case 'c': case 'y': modal_set_pending(modal, (char)cp); return res;

    case 'p': case 'P':
        if (!yank_empty(yank)) {
            if (yank->line_wise) {
                size_t at = (cp == 'p') ? (line_end_of(pt, e->cursor) < len ? line_end_of(pt, e->cursor) + 1 : len)
                                        : line_start_of(pt, e->cursor);
                ed_insert_at(e, at, yank->text, yank->len);
                e->cursor = at;
            } else {
                size_t at = (cp == 'p') ? next_boundary(pt, e->cursor) : e->cursor;
                ed_insert_at(e, at, yank->text, yank->len);
                e->cursor = at + yank->len - (yank->len ? 1 : 0);
            }
        }
        break;

    case 'u':
        if (!editor_undo(e)) res.flags |= EDIT_COMMAND_UNDO_AT_OLDEST;
        break;

    case 'v':
        if (modal->mode == MODE_VISUAL) modal_enter_normal(modal);
        else if (modal->mode == MODE_VISUAL_BLOCK) {
            /* Keep the corners; switch to character-wise visual. */
            modal_enter_visual(modal);
        } else {
            modal_enter_visual(modal);
            e->anchor = e->cursor;
        }
        break;

    case ':':
        res.flags |= EDIT_COMMAND_OPEN_COMMAND_LINE;
        break;
    case '/':
        res.flags |= EDIT_COMMAND_OPEN_BUFFER_SEARCH;
        break;
    case 'n':
        res.flags |= EDIT_COMMAND_SEARCH_NEXT;
        break;
    case 'N':
        res.flags |= EDIT_COMMAND_SEARCH_PREV;
        break;
    case '*':
        res.flags |= EDIT_COMMAND_SEARCH_WORD;
        break;

    default:
        break;
    }

    modal->count = 0;
    return res;
}

int edit_command_status_text(EditCommandResult result, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    if (result.flags & EDIT_COMMAND_UNDO_AT_OLDEST) {
        snprintf(out, cap, "already at oldest change");
        return 1;
    }
    out[0] = '\0';
    return 0;
}
