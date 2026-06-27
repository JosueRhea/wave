#ifndef WAVE_MODE_H
#define WAVE_MODE_H

typedef enum { MODE_NORMAL, MODE_INSERT, MODE_VISUAL } Mode;

typedef struct {
    Mode mode;
    char pending;
    int op_g;
    int count;
} ModalState;

void modal_init(ModalState *m);
void modal_enter_normal(ModalState *m);
void modal_enter_insert(ModalState *m);
void modal_enter_visual(ModalState *m);
void modal_toggle_visual(ModalState *m);
void modal_clear_pending(ModalState *m);
void modal_set_pending(ModalState *m, char pending);
char modal_take_pending(ModalState *m);
void modal_wait_g_motion(ModalState *m, char op);
int modal_push_count(ModalState *m, unsigned int cp);
int modal_count_or_one(const ModalState *m);
const char *mode_name(Mode mode);

#endif
