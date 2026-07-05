/* test_terminal.c - PTY-backed terminal smoke coverage. */
#include "test.h"
#include "terminal.h"

#include <string.h>
#include <unistd.h>

static int terminal_contains(const Terminal *t, const char *needle) {
    if (!t || !needle) return 0;
    for (size_t i = 0; i < t->nlines; i++) {
        if (strstr(terminal_line(t, i), needle)) return 1;
    }
    return t->current_len > 0 && strstr(t->current, needle);
}

#ifdef WAVE_USE_GHOSTTY_VT
static int terminal_contains_red_style(const Terminal *t, const char *needle) {
    if (!t || !needle) return 0;
    for (size_t i = 0; i < t->nlines; i++) {
        const char *line = terminal_line(t, i);
        const char *hit = strstr(line, needle);
        if (!hit) continue;
        size_t offset = (size_t)(hit - line);
        const TerminalLineStyle *style = terminal_line_style(t, i);
        if (!style) return 0;
        for (size_t c = 0; c < style->ncells; c++) {
            const TerminalCellStyle *cell = &style->cells[c];
            if (offset < cell->byte_start ||
                offset >= cell->byte_start + cell->byte_len)
                continue;
            return cell->has_fg && cell->fg.r > 0.70f &&
                   cell->fg.r > cell->fg.g && cell->fg.r > cell->fg.b;
        }
    }
    return 0;
}
#endif

int main(void) {
    char seq[32];
    CHECK_EQ(terminal_key_sequence(265, 0, 0, 0, seq, sizeof seq), 3);
    CHECK(!strcmp(seq, "\033[A"));
    CHECK_EQ(terminal_key_sequence(258, 1, 0, 0, seq, sizeof seq), 3);
    CHECK(!strcmp(seq, "\033[Z"));
    CHECK_EQ(terminal_key_sequence(262, 0, 1, 0, seq, sizeof seq), 6);
    CHECK(!strcmp(seq, "\033[1;3C"));
    CHECK_EQ(terminal_key_sequence(263, 0, 0, 1, seq, sizeof seq), 6);
    CHECK(!strcmp(seq, "\033[1;5D"));
    CHECK_EQ(terminal_key_sequence(261, 0, 0, 0, seq, sizeof seq), 4);
    CHECK(!strcmp(seq, "\033[3~"));
    CHECK_EQ(terminal_key_sequence(74, 0, 0, 1, seq, sizeof seq), 1);
    CHECK_EQ((unsigned char)seq[0], 10);

    Terminal t;
    terminal_init(&t);
    const char *argv[] = {"/bin/sh", "-lc", "printf hello", NULL};
    CHECK(terminal_spawn(&t, "test", ".", argv));

    for (int i = 0; i < 100 && t.running; i++) {
        terminal_poll(&t);
        usleep(10000);
    }
    terminal_poll(&t);

    CHECK(terminal_contains(&t, "hello"));

    terminal_free(&t);

#ifdef WAVE_USE_GHOSTTY_VT
    terminal_init(&t);
    const char *color_argv[] = {
        "/bin/sh", "-lc", "printf '\\033[31mred\\033[0m'", NULL
    };
    CHECK(terminal_spawn(&t, "color", ".", color_argv));
    for (int i = 0; i < 100 && t.running; i++) {
        terminal_poll(&t);
        usleep(10000);
    }
    terminal_poll(&t);
    CHECK(terminal_contains_red_style(&t, "red"));
    terminal_free(&t);
#endif

    TEST_REPORT();
}
