#ifndef WAVE_EDIT_COMMAND_H
#define WAVE_EDIT_COMMAND_H

#include <stddef.h>

#include "editor.h"
#include "mode.h"
#include "yank.h"

typedef enum {
    EDIT_COMMAND_NONE = 0,
    EDIT_COMMAND_YANKED = 1 << 0,
    EDIT_COMMAND_SHOW_INFO = 1 << 1,
    EDIT_COMMAND_GOTO_DEFINITION = 1 << 2,
    EDIT_COMMAND_TAB_NEXT = 1 << 3,
    EDIT_COMMAND_TAB_PREV = 1 << 4,
    EDIT_COMMAND_OPEN_COMMAND_LINE = 1 << 5,
    EDIT_COMMAND_UNDO_AT_OLDEST = 1 << 6,
    EDIT_COMMAND_OPEN_BUFFER_SEARCH = 1 << 7,
    EDIT_COMMAND_SEARCH_NEXT = 1 << 8,
    EDIT_COMMAND_SEARCH_PREV = 1 << 9,
    EDIT_COMMAND_SEARCH_WORD = 1 << 10,
} EditCommandFlags;

typedef struct {
    unsigned flags;
} EditCommandResult;

EditCommandResult edit_command_apply(Editor *e, ModalState *modal,
                                     YankRegister *yank, unsigned int cp);
int edit_command_status_text(EditCommandResult result, char *out, size_t cap);

#endif
