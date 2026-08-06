#include "standard.h"

#include "buffer.h"
#include "piece_table.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Selection bookkeeping.
 *
 * The editor carries exactly two positions — `cursor` and `anchor` — and a
 * selection is the span between them. MODE_VISUAL is the flag that says the
 * span is live; without it a stale anchor would paint a selection that the user
 * never made (text_view_selection and wave_line_selection both gate on it).
 * So every caret move here ends in one of two states:
 *
 *   selection:  mode == MODE_VISUAL, cursor != anchor
 *   no selection: mode == MODE_INSERT, cursor == anchor
 *
 * Keeping those in lockstep is what sync_selection() is for. */
static void sync_selection(Editor *e, ModalState *m) {
    if (!e || !m) return;
    if (e->cursor == e->anchor) {
        if (m->mode == MODE_VISUAL) modal_enter_insert(m);
    } else if (m->mode != MODE_VISUAL) {
        m->mode = MODE_VISUAL;
    }
}

int standard_has_selection(const Editor *e, const ModalState *m) {
    if (!e || !m) return 0;
    return m->mode == MODE_VISUAL && e->cursor != e->anchor;
}

static size_t doc_end(const Editor *e) {
    return pt_length(buffer_pt(e->buf));
}

/* Where a motion lands, ignoring selection entirely. Vertical motion is the
 * exception: move_vert() works on the editor's own cursor (it maintains the
 * remembered goal column), so it is applied in place by the caller. */
static size_t motion_target(Editor *e, StdMotion motion) {
    const PieceTable *pt = buffer_pt(e->buf);
    switch (motion) {
    case STD_MOTION_LEFT:       return prev_boundary(pt, e->cursor);
    case STD_MOTION_RIGHT:      return next_boundary(pt, e->cursor);
    case STD_MOTION_WORD_LEFT:  return word_prev(pt, e->cursor);
    case STD_MOTION_WORD_RIGHT: return word_next(pt, e->cursor);
    case STD_MOTION_LINE_START: return line_start_of(pt, e->cursor);
    case STD_MOTION_LINE_END:   return line_end_of(pt, e->cursor);
    case STD_MOTION_DOC_START:  return 0;
    case STD_MOTION_DOC_END:    return pt_length(pt);
    default:                    return e->cursor;
    }
}

static int is_vertical(StdMotion motion) {
    return motion == STD_MOTION_UP || motion == STD_MOTION_DOWN;
}

int standard_motion(Editor *e, ModalState *m, StdMotion motion, int extend) {
    if (!e || !m || !e->buf || motion == STD_MOTION_NONE) return 0;
    e->group_open = 0; /* a caret move ends the current undo group */

    size_t before = e->cursor;
    int had_selection = standard_has_selection(e, m);

    if (!extend && had_selection &&
        (motion == STD_MOTION_LEFT || motion == STD_MOTION_RIGHT)) {
        /* ←/→ out of a selection collapses to its edge and does not move the
         * caret a further character — the standard behaviour, and the reason
         * this is not just "clear, then move". */
        EditorRange r;
        if (editor_visual_range(e, &r)) {
            e->cursor = (motion == STD_MOTION_LEFT) ? r.start : r.end;
            e->anchor = e->cursor;
            modal_enter_insert(m);
            return 1;
        }
    }

    if (extend && !had_selection) e->anchor = e->cursor;

    if (is_vertical(motion)) {
        move_vert(e, motion == STD_MOTION_UP ? -1 : +1);
    } else {
        e->cursor = motion_target(e, motion);
    }

    if (!extend) e->anchor = e->cursor;
    sync_selection(e, m);
    return e->cursor != before || had_selection != standard_has_selection(e, m);
}

int standard_delete_selection(Editor *e, ModalState *m) {
    if (!standard_has_selection(e, m)) return 0;
    EditorRange r;
    if (!editor_visual_range(e, &r)) return 0;
    ed_delete_range(e, r.start, r.end);
    e->cursor = r.start;
    e->anchor = r.start;
    modal_enter_insert(m);
    return 1;
}

int standard_text_input(Editor *e, ModalState *m, unsigned int cp) {
    if (!e || !m || !e->buf) return 0;
    /* Typing over a selection replaces it. Done as one undo group so a single
     * ⌘Z puts the replaced text back, rather than resurrecting it empty. */
    if (standard_has_selection(e, m)) {
        e->group_open = 0;
        standard_delete_selection(e, m);
        e->group_open = 1;
    }
    return editor_apply_text_input(e, cp);
}

int standard_editor_key(Editor *e, ModalState *m, EditorKey key) {
    if (!e || !m || !e->buf) return 0;

    /* Backspace and Delete take the selection when there is one, instead of
     * eating the neighbouring character. */
    if ((key == EDITOR_KEY_BACKSPACE || key == EDITOR_KEY_DELETE) &&
        standard_has_selection(e, m)) {
        standard_delete_selection(e, m);
        e->group_open = 1;
        return 1;
    }

    /* Enter and Tab replace a selection, matching typing. */
    if ((key == EDITOR_KEY_ENTER || key == EDITOR_KEY_TAB) &&
        standard_has_selection(e, m)) {
        e->group_open = 0;
        standard_delete_selection(e, m);
    }

    int handled = editor_apply_insert_key(e, key);
    e->anchor = e->cursor;
    sync_selection(e, m);
    return handled;
}

int standard_select_all(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    size_t end = doc_end(e);
    if (end == 0) return 0;
    e->anchor = 0;
    e->cursor = end;
    e->group_open = 0;
    sync_selection(e, m);
    return 1;
}

static int is_word_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

int standard_select_word(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);
    size_t pos = e->cursor;
    /* A caret sitting just past the end of a word still selects that word,
     * which is where it lands after double-clicking the last character. */
    if (pos >= len || !is_word_byte(byte_at(pt, pos))) {
        if (pos > 0 && is_word_byte(byte_at(pt, pos - 1))) pos--;
        else return 0;
    }
    size_t a = pos, b = pos;
    while (a > 0 && is_word_byte(byte_at(pt, a - 1))) a--;
    while (b < len && is_word_byte(byte_at(pt, b))) b++;
    if (b <= a) return 0;
    e->anchor = a;
    e->cursor = b;
    e->group_open = 0;
    sync_selection(e, m);
    return 1;
}

int standard_escape(Editor *e, ModalState *m) {
    if (!e || !m) return 0;
    int had = standard_has_selection(e, m);
    e->anchor = e->cursor;
    modal_enter_insert(m);
    editor_cancel_group(e);
    return had;
}

/* ---- line commands ---- */

/* The byte span of the lines the selection touches, or of the caret's own line.
 * `end` is the offset of the line's terminating newline (or EOF); `end_nl` is
 * one past it, i.e. what to delete to take the line away entirely. */
typedef struct {
    size_t start;
    size_t end;
    size_t end_nl;
} LineSpan;

static LineSpan line_span(Editor *e, ModalState *m) {
    const PieceTable *pt = buffer_pt(e->buf);
    size_t a = e->cursor, b = e->cursor;
    if (standard_has_selection(e, m)) {
        EditorRange r;
        if (editor_visual_range(e, &r)) {
            a = r.start;
            /* A selection that stops exactly on a line start does not make that
             * line part of the span — otherwise selecting one whole line would
             * take two. */
            b = r.end > r.start ? r.end - 1 : r.start;
        }
    }
    LineSpan s;
    s.start = line_start_of(pt, a);
    s.end = line_end_of(pt, b);
    s.end_nl = s.end < pt_length(pt) ? s.end + 1 : s.end;
    return s;
}

char *standard_copy(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return NULL;
    if (standard_has_selection(e, m)) {
        EditorRange r;
        if (!editor_visual_range(e, &r)) return NULL;
        return editor_range_text(e, r.start, r.end);
    }
    LineSpan s = line_span(e, m);
    if (s.end_nl <= s.start) return NULL;
    return editor_range_text(e, s.start, s.end_nl);
}

char *standard_cut(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return NULL;
    char *text = standard_copy(e, m);
    if (!text) return NULL;
    e->group_open = 0;
    if (standard_has_selection(e, m)) {
        standard_delete_selection(e, m);
    } else {
        LineSpan s = line_span(e, m);
        ed_delete_range(e, s.start, s.end_nl);
        e->cursor = e->anchor = s.start;
        sync_selection(e, m);
    }
    return text;
}

int standard_delete_line(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    LineSpan s = line_span(e, m);
    if (s.end_nl <= s.start) return 0;
    e->group_open = 0;
    ed_delete_range(e, s.start, s.end_nl);
    e->cursor = e->anchor = s.start;
    sync_selection(e, m);
    return 1;
}

int standard_duplicate_line(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    LineSpan s = line_span(e, m);
    if (s.end <= s.start && s.end_nl <= s.start) return 0;
    char *text = editor_range_text(e, s.start, s.end);
    if (!text) return 0;
    size_t n = strlen(text);
    e->group_open = 0;
    /* Insert *after* the span's newline, so the copy lands on its own line.
     * A last line with no trailing newline needs one added first. */
    if (s.end_nl > s.end) {
        ed_insert_at(e, s.end_nl, text, n);
        ed_insert_at(e, s.end_nl + n, "\n", 1);
    } else {
        ed_insert_at(e, s.end, "\n", 1);
        ed_insert_at(e, s.end + 1, text, n);
    }
    /* Caret onto the copy, keeping the column. */
    size_t offset = e->cursor - s.start;
    e->cursor = e->anchor = s.end_nl + offset;
    modal_enter_insert(m);
    free(text);
    return 1;
}

int standard_move_line(Editor *e, ModalState *m, int dir) {
    if (!e || !m || !e->buf || dir == 0) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    LineSpan s = line_span(e, m);
    size_t caret_off = e->cursor >= s.start ? e->cursor - s.start : 0;
    size_t anchor_off = e->anchor >= s.start ? e->anchor - s.start : 0;
    int had_selection = standard_has_selection(e, m);

    /* The span's text without its trailing newline; it is re-joined by hand so
     * a final line that has no newline still moves correctly. */
    char *block = editor_range_text(e, s.start, s.end);
    if (!block) return 0;
    size_t block_n = strlen(block);
    e->group_open = 0;

    if (dir < 0) {
        if (s.start == 0) { free(block); return 0; }
        size_t prev_start = line_start_of(pt, s.start - 1);
        /* Take the span with its own trailing newline, then reinsert it above
         * the previous line. A last line that has no newline of its own takes
         * the one *before* it instead — otherwise the line it displaces keeps a
         * trailing newline the file never had, and the move silently appends
         * one every time. */
        int span_has_nl = s.end_nl > s.end;
        ed_delete_range(e, span_has_nl ? s.start : s.start - 1,
                        span_has_nl ? s.end_nl : s.end);
        ed_insert_at(e, prev_start, block, block_n);
        ed_insert_at(e, prev_start + block_n, "\n", 1);
        e->cursor = prev_start + caret_off;
        e->anchor = had_selection ? prev_start + anchor_off : e->cursor;
    } else {
        if (s.end_nl >= pt_length(pt)) { free(block); return 0; }
        size_t next_end = line_end_of(pt, s.end_nl);
        size_t next_end_nl = next_end < pt_length(pt) ? next_end + 1 : next_end;
        size_t next_len = next_end_nl - s.end_nl;
        int next_has_nl = next_end_nl > next_end;
        ed_delete_range(e, s.start, s.end_nl);
        /* The following line has shifted up into `s.start`; drop the span in
         * after it. If that line had no newline of its own, one is added so the
         * moved block still starts on a fresh line. */
        size_t at = s.start + next_len;
        if (!next_has_nl) {
            ed_insert_at(e, at, "\n", 1);
            at += 1;
        }
        ed_insert_at(e, at, block, block_n);
        if (next_has_nl) ed_insert_at(e, at + block_n, "\n", 1);
        e->cursor = at + caret_off;
        e->anchor = had_selection ? at + anchor_off : e->cursor;
    }
    free(block);
    sync_selection(e, m);
    return 1;
}

int standard_delete_to_line_start(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    if (standard_has_selection(e, m)) return standard_delete_selection(e, m);
    const PieceTable *pt = buffer_pt(e->buf);
    size_t start = line_start_of(pt, e->cursor);
    /* First stop is the indent, so ⌘⌫ on an indented line clears the text and
     * leaves the indentation — a second press then takes the indent too. */
    size_t indent = start;
    while (indent < e->cursor) {
        unsigned char c = byte_at(pt, indent);
        if (c != ' ' && c != '\t') break;
        indent++;
    }
    size_t to = (e->cursor > indent) ? indent : start;
    if (to >= e->cursor) return 0;
    e->group_open = 0;
    ed_delete_range(e, to, e->cursor);
    e->cursor = e->anchor = to;
    sync_selection(e, m);
    return 1;
}

/* Most languages Wave highlights (C, JavaScript, TypeScript, TSX, JSON) use
 * `//`. Dotenv uses `#`, so if comment toggling is ever made per-language, this
 * is the place — for now `//` remains the fixed default. */
#define STD_COMMENT "// "

static size_t line_indent_of(const PieceTable *pt, size_t start, size_t end) {
    size_t i = start;
    while (i < end) {
        unsigned char c = byte_at(pt, i);
        if (c != ' ' && c != '\t') break;
        i++;
    }
    return i;
}

static int line_is_commented(const PieceTable *pt, size_t start, size_t end) {
    size_t i = line_indent_of(pt, start, end);
    if (i + 2 > end) return 0;
    return byte_at(pt, i) == '/' && byte_at(pt, i + 1) == '/';
}

static int line_is_blank(const PieceTable *pt, size_t start, size_t end) {
    return line_indent_of(pt, start, end) >= end;
}

int standard_toggle_comment(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    LineSpan s = line_span(e, m);

    /* Uncomment only when every non-blank line is already commented. */
    int all_commented = 1, any_line = 0;
    for (size_t ls = s.start; ls <= s.end;) {
        size_t le = line_end_of(pt, ls);
        if (!line_is_blank(pt, ls, le)) {
            any_line = 1;
            if (!line_is_commented(pt, ls, le)) { all_commented = 0; break; }
        }
        if (le >= s.end) break;
        ls = le + 1;
    }
    if (!any_line) return 0;

    e->group_open = 0;
    size_t caret = e->cursor;
    /* Top-down, carrying the running byte delta each edit introduces. Walking
     * bottom-up would avoid the bookkeeping but needs the line starts collected
     * first, and a fixed-size collector would silently truncate a large
     * selection — commenting only part of what the user selected. */
    size_t token_n = strlen(STD_COMMENT);
    size_t span_end = s.end;
    for (size_t ls = s.start; ls <= span_end;) {
        size_t le = line_end_of(pt, ls);
        if (line_is_blank(pt, ls, le)) {
            if (le >= span_end) break;
            ls = le + 1;
            continue;
        }
        size_t at = line_indent_of(pt, ls, le);
        if (all_commented) {
            size_t cut = at + 2;
            if (cut < le && byte_at(pt, cut) == ' ') cut++;
            size_t removed = cut - at;
            ed_delete_range(e, at, cut);
            if (caret > at) caret = caret > at + removed ? caret - removed : at;
            span_end -= removed;
            le -= removed;
        } else {
            ed_insert_at(e, at, STD_COMMENT, token_n);
            if (caret >= at) caret += token_n;
            span_end += token_n;
            le += token_n;
        }
        if (le >= span_end) break;
        ls = le + 1;
    }
    e->cursor = caret;
    e->anchor = caret;
    modal_enter_insert(m);
    return 1;
}

#define STD_INDENT "    "
#define STD_INDENT_N 4

int standard_indent(Editor *e, ModalState *m, int outdent) {
    if (!e || !m || !e->buf) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    LineSpan s = line_span(e, m);
    /* The span covers more than one line when the first line ends before it. */
    int multiline = line_end_of(pt, s.start) < s.end;

    /* Tab only becomes a block indent when a selection actually spans lines.
     * With a caret, or a selection inside one line, it stays a literal tab —
     * reported by returning 0 so the caller inserts one.
     *
     * ⇧Tab always outdents, including on a single line: there is nothing else
     * for it to mean in standard editing (tab cycling is vim-mode only). */
    if (!outdent && !(standard_has_selection(e, m) && multiline)) return 0;

    e->group_open = 0;
    size_t caret = e->cursor, anchor = e->anchor;
    size_t span_end = s.end;
    int changed = 0;
    for (size_t ls = s.start; ls <= span_end;) {
        size_t le = line_end_of(pt, ls);
        if (outdent) {
            /* Take up to one indent's worth of leading whitespace, and a lone
             * tab counts as a full level. */
            size_t n = 0;
            while (n < STD_INDENT_N && ls + n < le) {
                unsigned char c = byte_at(pt, ls + n);
                if (c == '\t') { n++; break; }
                if (c != ' ') break;
                n++;
            }
            if (n) {
                ed_delete_range(e, ls, ls + n);
                if (caret > ls) caret = caret > ls + n ? caret - n : ls;
                if (anchor > ls) anchor = anchor > ls + n ? anchor - n : ls;
                span_end -= n;
                le -= n;
                changed = 1;
            }
        } else {
            ed_insert_at(e, ls, STD_INDENT, STD_INDENT_N);
            if (caret >= ls) caret += STD_INDENT_N;
            if (anchor >= ls) anchor += STD_INDENT_N;
            span_end += STD_INDENT_N;
            le += STD_INDENT_N;
            changed = 1;
        }
        if (le >= span_end) break;
        ls = le + 1;
    }
    e->cursor = caret;
    e->anchor = anchor;
    sync_selection(e, m);
    /* An outdent of already-flush lines removed nothing; say so, so ⇧Tab does
     * not claim to have handled a keystroke it ignored. */
    return changed;
}

int standard_delete_word_left(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    if (standard_has_selection(e, m)) return standard_delete_selection(e, m);
    if (e->cursor == 0) return 0;
    const PieceTable *pt = buffer_pt(e->buf);
    size_t to = word_prev(pt, e->cursor);
    if (to >= e->cursor) return 0;
    e->group_open = 0;
    ed_delete_range(e, to, e->cursor);
    e->cursor = e->anchor = to;
    sync_selection(e, m);
    return 1;
}

int standard_delete_word_right(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    if (standard_has_selection(e, m)) return standard_delete_selection(e, m);
    const PieceTable *pt = buffer_pt(e->buf);
    if (e->cursor >= pt_length(pt)) return 0;
    size_t to = word_next(pt, e->cursor);
    if (to <= e->cursor) return 0;
    e->group_open = 0;
    ed_delete_range(e, e->cursor, to);
    e->anchor = e->cursor;
    sync_selection(e, m);
    return 1;
}

int standard_delete_to_line_end(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    if (standard_has_selection(e, m)) return standard_delete_selection(e, m);
    const PieceTable *pt = buffer_pt(e->buf);
    size_t end = line_end_of(pt, e->cursor);
    if (end <= e->cursor) return 0;
    e->group_open = 0;
    ed_delete_range(e, e->cursor, end);
    e->anchor = e->cursor;
    sync_selection(e, m);
    return 1;
}

int standard_select_line(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    LineSpan s = line_span(e, m);
    const PieceTable *pt = buffer_pt(e->buf);
    size_t end = s.end_nl;
    /* A repeat takes the next line too, which is what makes ⌘L ⌘L ⌘L work. */
    if (standard_has_selection(e, m) && e->anchor == s.start && e->cursor == end &&
        end < pt_length(pt)) {
        size_t next_end = line_end_of(pt, end);
        end = next_end < pt_length(pt) ? next_end + 1 : next_end;
    }
    if (end <= s.start) return 0;
    e->anchor = s.start;
    e->cursor = end;
    e->group_open = 0;
    sync_selection(e, m);
    return 1;
}

int standard_insert_line(Editor *e, ModalState *m, int below) {
    if (!e || !m || !e->buf) return 0;
    e->group_open = 0;
    modal_enter_insert(m);
    e->anchor = e->cursor;
    int ok = editor_open_line(e, below);
    if (ok) {
        e->anchor = e->cursor;
        sync_selection(e, m);
    }
    return ok;
}

/* ---- multiple carets ---- */

size_t standard_caret_count(const Editor *e) { return e ? e->extra_n : 0; }

void standard_clear_carets(Editor *e) {
    if (e) e->extra_n = 0;
}

int standard_caret_at(const Editor *e, size_t i, size_t *anchor, size_t *cursor) {
    if (!e || i >= e->extra_n) return 0;
    if (anchor) *anchor = e->extra_anchor[i];
    if (cursor) *cursor = e->extra_cursor[i];
    return 1;
}

int standard_add_caret(Editor *e, size_t anchor, size_t cursor) {
    if (!e || e->extra_n >= EDITOR_MAX_EXTRA_CARETS) return 0;
    if (cursor == e->cursor && anchor == e->anchor) return 0;
    for (size_t i = 0; i < e->extra_n; i++)
        if (e->extra_cursor[i] == cursor && e->extra_anchor[i] == anchor) return 0;
    e->extra_anchor[e->extra_n] = anchor;
    e->extra_cursor[e->extra_n] = cursor;
    e->extra_n++;
    return 1;
}

/* Byte compare against the buffer, so a match test needs no copy. */
static int matches_at(const PieceTable *pt, size_t pos, const char *needle,
                      size_t n) {
    if (pos + n > pt_length(pt)) return 0;
    for (size_t i = 0; i < n; i++)
        if (byte_at(pt, pos + i) != (unsigned char)needle[i]) return 0;
    return 1;
}

int standard_select_next_occurrence(Editor *e, ModalState *m) {
    if (!e || !m || !e->buf) return 0;
    /* First press with no selection just takes the word — the same thing a
     * double-click does — so ⌘D ⌘D means "this word, and the next one". */
    if (!standard_has_selection(e, m)) return standard_select_word(e, m);

    EditorRange r;
    if (!editor_visual_range(e, &r)) return 0;
    size_t n = r.end - r.start;
    if (n == 0) return 0;
    char *needle = editor_range_text(e, r.start, r.end);
    if (!needle) return 0;

    const PieceTable *pt = buffer_pt(e->buf);
    size_t len = pt_length(pt);
    /* Search forward from the last caret, wrapping once. */
    size_t from = r.end;
    for (size_t i = 0; i < e->extra_n; i++) {
        size_t c = e->extra_cursor[i];
        if (c > from) from = c;
    }
    size_t found = (size_t)-1;
    for (size_t p = from; p + n <= len; p++)
        if (matches_at(pt, p, needle, n)) { found = p; break; }
    if (found == (size_t)-1) {
        for (size_t p = 0; p + n <= len && p < from; p++)
            if (matches_at(pt, p, needle, n)) { found = p; break; }
    }
    free(needle);
    if (found == (size_t)-1) return 0;
    return standard_add_caret(e, found, found + n);
}

/* Every caret's (anchor, cursor) pair, primary included, sorted by cursor
 * ascending. Editing low→high records undo positions that LIFO-undo cleanly;
 * later (higher) carets are shifted when an earlier edit grows/shrinks the
 * buffer. */
typedef struct {
    size_t anchor;
    size_t cursor;
    int primary;
} CaretRef;

static size_t collect_carets(Editor *e, CaretRef *out) {
    size_t n = 0;
    out[n].anchor = e->anchor;
    out[n].cursor = e->cursor;
    out[n].primary = 1;
    n++;
    for (size_t i = 0; i < e->extra_n; i++) {
        out[n].anchor = e->extra_anchor[i];
        out[n].cursor = e->extra_cursor[i];
        out[n].primary = 0;
        n++;
    }
    for (size_t i = 1; i < n; i++) {  /* insertion sort, ascending by cursor */
        CaretRef key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].cursor > key.cursor) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return n;
}

/* Write the carets back after an edit, keeping the primary as the editor's own
 * and dropping any that collided onto the same offset. */
static void store_carets(Editor *e, ModalState *m, CaretRef *c, size_t n) {
    e->extra_n = 0;
    for (size_t i = 0; i < n; i++) {
        if (c[i].primary) {
            e->cursor = c[i].cursor;
            e->anchor = c[i].anchor;
        } else if (e->extra_n < EDITOR_MAX_EXTRA_CARETS) {
            int dup = (c[i].cursor == e->cursor);
            for (size_t j = 0; !dup && j < e->extra_n; j++)
                if (e->extra_cursor[j] == c[i].cursor) dup = 1;
            if (!dup) {
                e->extra_anchor[e->extra_n] = c[i].anchor;
                e->extra_cursor[e->extra_n] = c[i].cursor;
                e->extra_n++;
            }
        }
    }
    sync_selection(e, m);
}

/* After editing at `at`, bump not-yet-processed carets (indices > i). Carets
 * are walked low→high so recorded undo positions stay valid under LIFO undo;
 * a lower edit still shifts the higher carets waiting their turn. */
static void adjust_later_carets(CaretRef *c, size_t i, size_t n, size_t at,
                                ptrdiff_t delta) {
    if (delta == 0) return;
    for (size_t j = i + 1; j < n; j++) {
        if (c[j].cursor >= at) c[j].cursor = (size_t)((ptrdiff_t)c[j].cursor + delta);
        if (c[j].anchor >= at) c[j].anchor = (size_t)((ptrdiff_t)c[j].anchor + delta);
    }
}

int standard_multi_text_input(Editor *e, ModalState *m, unsigned int cp) {
    if (!e || !m || !e->buf) return 0;
    if (e->extra_n == 0) return standard_text_input(e, m, cp);

    CaretRef c[EDITOR_MAX_EXTRA_CARETS + 1];
    size_t n = collect_carets(e, c);
    editor_begin_txn(e);
    for (size_t i = 0; i < n; i++) {
        /* One caret at a time, low→high: point the editor at it, reuse the
         * single-caret path, then read back where it ended up. */
        e->cursor = c[i].cursor;
        e->anchor = c[i].anchor;
        m->mode = (c[i].cursor != c[i].anchor) ? MODE_VISUAL : MODE_INSERT;
        size_t at = e->cursor < e->anchor ? e->cursor : e->anchor;
        size_t before = pt_length(buffer_pt(e->buf));
        standard_text_input(e, m, cp);
        ptrdiff_t delta = (ptrdiff_t)pt_length(buffer_pt(e->buf)) - (ptrdiff_t)before;
        c[i].cursor = c[i].anchor = e->cursor;
        adjust_later_carets(c, i, n, at, delta);
    }
    store_carets(e, m, c, n);
    editor_end_txn(e);
    return 1;
}

int standard_multi_editor_key(Editor *e, ModalState *m, EditorKey key) {
    if (!e || !m || !e->buf) return 0;
    if (e->extra_n == 0) return standard_editor_key(e, m, key);

    CaretRef c[EDITOR_MAX_EXTRA_CARETS + 1];
    size_t n = collect_carets(e, c);
    editor_begin_txn(e);
    int handled = 0;
    for (size_t i = 0; i < n; i++) {
        e->cursor = c[i].cursor;
        e->anchor = c[i].anchor;
        m->mode = (c[i].cursor != c[i].anchor) ? MODE_VISUAL : MODE_INSERT;
        size_t at = e->cursor < e->anchor ? e->cursor : e->anchor;
        size_t before = pt_length(buffer_pt(e->buf));
        handled |= standard_editor_key(e, m, key);
        ptrdiff_t delta = (ptrdiff_t)pt_length(buffer_pt(e->buf)) - (ptrdiff_t)before;
        c[i].cursor = c[i].anchor = e->cursor;
        adjust_later_carets(c, i, n, at, delta);
    }
    store_carets(e, m, c, n);
    editor_end_txn(e);
    return handled;
}

int standard_multi_motion(Editor *e, ModalState *m, StdMotion motion, int extend) {
    if (!e || !m || !e->buf || motion == STD_MOTION_NONE) return 0;
    if (e->extra_n == 0) return standard_motion(e, m, motion, extend);

    CaretRef c[EDITOR_MAX_EXTRA_CARETS + 1];
    size_t n = collect_carets(e, c);
    e->group_open = 0;
    int moved = 0;
    for (size_t i = 0; i < n; i++) {
        e->cursor = c[i].cursor;
        e->anchor = c[i].anchor;
        m->mode = (c[i].cursor != c[i].anchor) ? MODE_VISUAL : MODE_INSERT;
        moved |= standard_motion(e, m, motion, extend);
        c[i].cursor = e->cursor;
        c[i].anchor = e->anchor;
    }
    store_carets(e, m, c, n);
    return moved;
}

void standard_set_enabled(Editor *e, ModalState *m, int enabled) {
    editor_set_selection_exclusive(enabled);
    if (!m) return;
    if (enabled) {
        /* Straight into insert: in standard editing there is nothing else to be
         * in, and leaving it in NORMAL would swallow the first keystroke as a
         * vim command. */
        modal_enter_insert(m);
        if (e) e->anchor = e->cursor;
    } else {
        modal_enter_normal(m);
    }
}
