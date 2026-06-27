#include "mode.h"

void modal_init(ModalState *m) {
    if (!m) return;
    m->mode = MODE_NORMAL;
    m->pending = 0;
    m->op_g = 0;
    m->count = 0;
}

void modal_enter_normal(ModalState *m) {
    if (!m) return;
    m->mode = MODE_NORMAL;
    modal_clear_pending(m);
}

void modal_enter_insert(ModalState *m) {
    if (!m) return;
    m->mode = MODE_INSERT;
    m->pending = 0;
    m->op_g = 0;
    m->count = 0;
}

void modal_enter_visual(ModalState *m) {
    if (!m) return;
    m->mode = MODE_VISUAL;
    m->pending = 0;
    m->count = 0;
}

void modal_toggle_visual(ModalState *m) {
    if (!m) return;
    if (m->mode == MODE_VISUAL)
        modal_enter_normal(m);
    else
        modal_enter_visual(m);
}

void modal_clear_pending(ModalState *m) {
    if (!m) return;
    m->pending = 0;
    m->op_g = 0;
    m->count = 0;
}

void modal_set_pending(ModalState *m, char pending) {
    if (!m) return;
    m->pending = pending;
}

char modal_take_pending(ModalState *m) {
    if (!m) return 0;
    char pending = m->pending;
    m->pending = 0;
    m->count = 0;
    return pending;
}

void modal_wait_g_motion(ModalState *m, char op) {
    if (!m) return;
    m->pending = op;
    m->op_g = 1;
}

int modal_push_count(ModalState *m, unsigned int cp) {
    if (!m) return 0;
    if (cp >= '1' && cp <= '9') {
        m->count = m->count * 10 + (int)(cp - '0');
        return 1;
    }
    if (cp == '0' && m->count > 0) {
        m->count *= 10;
        return 1;
    }
    return 0;
}

int modal_count_or_one(const ModalState *m) {
    return m && m->count ? m->count : 1;
}

const char *mode_name(Mode mode) {
    switch (mode) {
    case MODE_INSERT: return "INSERT";
    case MODE_VISUAL: return "VISUAL";
    case MODE_NORMAL:
    default: return "NORMAL";
    }
}
