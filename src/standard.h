#ifndef WAVE_STANDARD_H
#define WAVE_STANDARD_H

/* Standard (non-vim) editing: the always-inserting, shift-to-select model that
 * VS Code, TextEdit and every other non-modal editor use. It is what the `vim=0`
 * setting selects; vim remains the default.
 *
 * There is no separate "standard mode" in ModalState. The editor is simply held
 * in MODE_INSERT, and MODE_VISUAL is borrowed to mean "there is a selection" —
 * which is exactly what it already means to the renderer, to Cmd-C and to paste.
 * Reusing it is what keeps selection painting, copy and replace-on-type working
 * here without a second code path through those.
 *
 * The one thing that does differ is where a selection *ends*: vim's VISUAL is
 * inclusive of the character under the cursor, standard selection is caret-to-
 * caret. editor_set_selection_exclusive() switches that, and the session flips
 * it with the setting. */

#include "editor.h"
#include "mode.h"

typedef enum {
    STD_MOTION_NONE,
    STD_MOTION_LEFT,
    STD_MOTION_RIGHT,
    STD_MOTION_UP,
    STD_MOTION_DOWN,
    STD_MOTION_WORD_LEFT,   /* ⌥← */
    STD_MOTION_WORD_RIGHT,  /* ⌥→ */
    STD_MOTION_LINE_START,  /* ⌘← / Home */
    STD_MOTION_LINE_END,    /* ⌘→ / End */
    STD_MOTION_DOC_START,   /* ⌘↑ */
    STD_MOTION_DOC_END      /* ⌘↓ */
} StdMotion;

/* Non-zero while a non-empty selection is up. */
int standard_has_selection(const Editor *e, const ModalState *m);

/* Move the caret. `extend` is the shift key: it grows the selection from the
 * existing anchor, dropping one if the caret lands back on it. Without it the
 * selection collapses — and for LEFT/RIGHT it collapses *to the edge* without
 * moving, which is what every standard editor does. Returns non-zero if
 * anything moved or the selection changed. */
int standard_motion(Editor *e, ModalState *m, StdMotion motion, int extend);

/* Delete the selection, if any, leaving the caret where it started. */
int standard_delete_selection(Editor *e, ModalState *m);

/* Type a character: replaces the selection when there is one. */
int standard_text_input(Editor *e, ModalState *m, unsigned int cp);

/* Backspace / Delete / Enter / Tab, with selection handling. Arrow keys are
 * routed to standard_motion() instead, so they can carry `extend`. */
int standard_editor_key(Editor *e, ModalState *m, EditorKey key);

/* ⌘A. */
int standard_select_all(Editor *e, ModalState *m);

/* Select the word under the caret — what a double-click does. Vim reaches this
 * with `viw`, which standard editing cannot use: those are three characters of
 * text here, not a command. */
int standard_select_word(Editor *e, ModalState *m);

/* Escape: drop the selection. Returns non-zero if there was one to drop, so a
 * caller can fall through to its other Escape behaviour when there was not.
 * Never leaves MODE_INSERT — that is the whole point of the mode. */
int standard_escape(Editor *e, ModalState *m);

/* Put a session into standard editing (MODE_INSERT, caret-to-caret selection)
 * or back into vim's initial NORMAL. */
void standard_set_enabled(Editor *e, ModalState *m, int enabled);

/* ---- line commands ----
 *
 * All of these work on "the span": the lines the selection touches, or the
 * caret's own line when there is no selection. That is the rule every standard
 * editor uses, and it is why ⌘X with nothing selected cuts the whole line
 * rather than doing nothing. */

/* Copy: the selection, or the whole line (including its newline) when there is
 * none. Returns malloc'd text the caller frees, or NULL when there is nothing
 * to copy. */
char *standard_copy(Editor *e, ModalState *m);

/* Cut: same text as standard_copy(), and removes it. */
char *standard_cut(Editor *e, ModalState *m);

/* Delete the span outright (⇧⌘K). */
int standard_delete_line(Editor *e, ModalState *m);

/* Duplicate the span below itself (⇧⌥↓). */
int standard_duplicate_line(Editor *e, ModalState *m);

/* Move the span one line up (dir < 0) or down (dir > 0) — ⌥↑ / ⌥↓. */
int standard_move_line(Editor *e, ModalState *m, int dir);

/* Delete from the caret back to the first non-space on its line, then to the
 * line start — what ⌘⌫ does in a standard editor. */
int standard_delete_to_line_start(Editor *e, ModalState *m);

/* Toggle `// ` on every line of the span (⌘/). Commented lines are uncommented
 * only when *all* of them are commented, so a partly-commented block ends up
 * fully commented rather than inverted line by line. */
int standard_toggle_comment(Editor *e, ModalState *m);

/* Tab / ⇧Tab over a selection that spans lines: indent or outdent the whole
 * block by one level, keeping the selection on it. Returns 0 when there is no
 * multi-line selection, so the caller can fall back to inserting a plain tab —
 * which is what Tab must still do with a caret or a within-line selection. */
int standard_indent(Editor *e, ModalState *m, int outdent);

/* ⌥⌫ / ⌥⌦ — delete a word either side of the caret. With a selection they
 * delete that instead, like every other destructive key here. */
int standard_delete_word_left(Editor *e, ModalState *m);
int standard_delete_word_right(Editor *e, ModalState *m);

/* ⌘⌦ — delete from the caret to the end of the line. */
int standard_delete_to_line_end(Editor *e, ModalState *m);

/* ⌘L — select the span's whole lines, newline included, so a repeat extends
 * downward the way it does elsewhere. */
int standard_select_line(Editor *e, ModalState *m);

/* ⌘⏎ / ⇧⌘⏎ — open a blank line below (below != 0) or above and put the caret
 * on it, without splitting the current line wherever the caret happens to be. */
int standard_insert_line(Editor *e, ModalState *m, int below);

/* ---- multiple carets ----
 *
 * The editor proper carries one cursor and one anchor; these are the *extra*
 * ones, kept beside it. Text insertion, delete keys, and motions honour them;
 * other commands clear them first — which keeps ⌘D useful without every LSP
 * request and highlight path having to learn about N carets.
 *
 * Each extra caret has its own anchor, so ⌘D can carry a selection per caret. */
#define STANDARD_MAX_CARETS EDITOR_MAX_EXTRA_CARETS

/* Number of extra carets (not counting the editor's own). */
size_t standard_caret_count(const Editor *e);

/* Drop every extra caret. Cheap and idempotent; call it from anything that does
 * not explicitly support them. */
void standard_clear_carets(Editor *e);

/* Add a caret with its own selection. Ignores duplicates and anything past the
 * cap. Returns non-zero if one was added. */
int standard_add_caret(Editor *e, size_t anchor, size_t cursor);

/* ⌘D — select the word under the caret if nothing is selected yet, otherwise
 * find the next occurrence of the selected text and put a caret on it. Wraps to
 * the top. Returns non-zero if a caret or selection was added. */
int standard_select_next_occurrence(Editor *e, ModalState *m);

/* Apply to every caret, primary included. These are the only edits that do. */
int standard_multi_text_input(Editor *e, ModalState *m, unsigned int cp);
int standard_multi_editor_key(Editor *e, ModalState *m, EditorKey key);

/* Move every caret with the same motion. Shift (`extend`) grows each caret's
 * own selection. With no extras this is exactly standard_motion(). */
int standard_multi_motion(Editor *e, ModalState *m, StdMotion motion, int extend);

/* Read back extra caret i (0-based), for painting. */
int standard_caret_at(const Editor *e, size_t i, size_t *anchor, size_t *cursor);

#endif
