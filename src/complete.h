/* complete.h — a headless insert-mode completion engine.
 *
 * Owns the completion menu's state (the item pool, the filtered/sorted view,
 * selection, scroll) but never touches a Buffer or Editor directly: callers
 * feed it items from whichever source is available (LSP, tree-sitter, or a
 * plain word scan — see lsp_manager.h / highlight.h / complete_collect_*
 * below) and it hands back an edit plan on accept. This mirrors popover.h /
 * overlay.h, which keep the same separation between state and the GUI shell.
 *
 * A generation counter guards against stale async results: complete_begin()
 * bumps it, and complete_set_items() silently drops a reply stamped with an
 * older generation (the menu moved on — the cursor advanced, the word
 * boundary changed, or the menu was closed — since that request was sent).
 */
#ifndef WAVE_COMPLETE_H
#define WAVE_COMPLETE_H

#include <stddef.h>
#include "piece_table.h"

#define COMPLETE_MAX_ITEMS 128
#define COMPLETE_LABEL_CAP 96
#define COMPLETE_DETAIL_CAP 64
#define COMPLETE_SORT_CAP 32
#define COMPLETE_MAX_VISIBLE_ROWS 10

/* A coarse guess at what an item is, for the kind tag drawn beside its
 * label. Sources map their own vocabulary onto this. */
typedef enum {
    COMPLETE_KIND_TEXT,
    COMPLETE_KIND_VARIABLE,
    COMPLETE_KIND_FUNCTION,
    COMPLETE_KIND_TYPE,
    COMPLETE_KIND_KEYWORD,
    COMPLETE_KIND_FIELD,
    COMPLETE_KIND_MODULE
} CompleteKind;

typedef struct {
    char label[COMPLETE_LABEL_CAP];        /* shown in the menu */
    char insert_text[COMPLETE_LABEL_CAP];  /* inserted on accept; label if empty */
    char detail[COMPLETE_DETAIL_CAP];      /* short type/signature hint, may be empty */
    char sort_text[COMPLETE_SORT_CAP];     /* server sort key, may be empty (falls back to label) */
    CompleteKind kind;
} CompleteItem;

typedef struct {
    int active;
    unsigned int generation;
    int loading; /* an async (LSP) request for the current generation is in flight */

    size_t word_start;             /* byte offset the replaced range starts at */
    char prefix[COMPLETE_LABEL_CAP]; /* identifier text typed so far, word_start..cursor */

    CompleteItem items[COMPLETE_MAX_ITEMS];
    int nitems;

    int filtered[COMPLETE_MAX_ITEMS]; /* indices into items[], filtered + sorted */
    int nfiltered;
    int sel;
    int scroll;
} CompleteState;

/* The edit to apply on accept: replace [start, end) with text. */
typedef struct {
    size_t start;
    size_t end;
    char text[COMPLETE_LABEL_CAP];
} CompleteEdit;

void complete_init(CompleteState *c);
void complete_close(CompleteState *c);
int complete_is_active(const CompleteState *c);

/* Byte offset where the identifier ending at `cursor` begins (== cursor if
 * the byte immediately before it isn't an identifier character). */
size_t complete_prefix_start(const PieceTable *pt, size_t cursor);

/* Start (or restart) a completion session anchored at `word_start`, bumping
 * the generation counter so replies to earlier requests get dropped. Returns
 * the new generation. */
unsigned int complete_begin(CompleteState *c, size_t word_start, const char *prefix);

/* Mark `generation` as waiting on an async source (drives the "Loading…"
 * affordance); a no-op if `generation` is already stale. */
void complete_set_loading(CompleteState *c, unsigned int generation);

/* Replace the item pool for `generation` and re-filter. Ignored (returns 0)
 * if `generation` is stale or the menu isn't active. */
int complete_set_items(CompleteState *c, unsigned int generation,
                       const CompleteItem *items, int n);

/* Re-filter the existing item pool against a new prefix, without discarding
 * items or bumping the generation — for keystrokes within the same word,
 * where the already-fetched item pool is still valid. */
void complete_set_prefix(CompleteState *c, const char *prefix);

void complete_move(CompleteState *c, int delta);
/* Clamp scroll so the selection stays within `visible_rows` of it. */
void complete_set_view(CompleteState *c, int visible_rows);

/* Produce the buffer edit for accepting the current selection. Returns 0 (no
 * edit) if the menu is inactive or has nothing selected. */
int complete_accept(const CompleteState *c, size_t cursor, CompleteEdit *out);

const char *complete_kind_tag(CompleteKind kind);

/* Last-resort source for buffers with no tree-sitter grammar: a capped,
 * deduplicated, prefix-filtered scan of identifier-shaped words in `text`
 * (the flattened document, e.g. from editor_text()). Returns the count
 * written to `out` (max COMPLETE_MAX_ITEMS-sized buffer, capped by `max`). */
int complete_collect_buffer_words(const char *text, const char *prefix,
                                  CompleteItem *out, int max);

/* Cursor-anchored menu geometry, computed the same way as PopoverLayout:
 * flips above the cursor line when there isn't room below, clamped to the
 * framebuffer and the sidebar edge. Returns 0 (nothing to draw) if the menu
 * is inactive or has no filtered items. Also updates the visible-row window
 * via complete_set_view(). */
typedef struct {
    float x, y, w, h;
    float padx, pady;
    float border;
    int visible_rows;
    int max_label_cells;
    int scrollable;
} CompleteLayout;

int complete_layout(CompleteState *c, int fb_w, int fb_h, float adv, float line_h,
                    float top_pad, float bar_h, float anchor_x, float cur_top,
                    float left_limit, float fb_scale, CompleteLayout *out);

#endif /* WAVE_COMPLETE_H */
