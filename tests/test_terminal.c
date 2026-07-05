/* test_terminal.c - PTY-backed terminal smoke coverage. */
#include "test.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int terminal_contains(const Terminal *t, const char *needle) {
    if (!t || !needle) return 0;
    for (size_t i = 0; i < t->nlines; i++) {
        if (strstr(terminal_line(t, i), needle)) return 1;
    }
    return t->current_len > 0 && strstr(t->current, needle);
}

static void poll_terminal_until_done(Terminal *t) {
    for (int i = 0; i < 160 && t->running; i++) {
        terminal_poll(t);
        usleep(10000);
    }
    terminal_poll(t);
}

static void restore_env_value(const char *name, const char *value) {
    if (value) setenv(name, value, 1);
    else unsetenv(name);
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

    poll_terminal_until_done(&t);

    CHECK(terminal_contains(&t, "hello"));

    terminal_free(&t);

    if (access("/bin/zsh", X_OK) == 0) {
        char root[] = "/tmp/wave-terminal-test-XXXXXX";
        char *tmp = mkdtemp(root);
        CHECK(tmp != NULL);
        if (tmp) {
            char bin_dir[512];
            char script_path[512];
            char zshrc_path[512];
            snprintf(bin_dir, sizeof bin_dir, "%s/bin", tmp);
            snprintf(script_path, sizeof script_path, "%s/wave-shell-path-test", bin_dir);
            snprintf(zshrc_path, sizeof zshrc_path, "%s/.zshrc", tmp);
            CHECK_EQ(mkdir(bin_dir, 0700), 0);

            FILE *script = fopen(script_path, "w");
            CHECK(script != NULL);
            if (script) {
                fprintf(script, "#!/bin/sh\nprintf shell-path-ok\n");
                fclose(script);
                CHECK_EQ(chmod(script_path, 0700), 0);
            }

            FILE *zshrc = fopen(zshrc_path, "w");
            CHECK(zshrc != NULL);
            if (zshrc) {
                fprintf(zshrc, "export PATH=\"%s:$PATH\"\n", bin_dir);
                fclose(zshrc);
            }

            char *old_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
            char *old_zdotdir = getenv("ZDOTDIR") ? strdup(getenv("ZDOTDIR")) : NULL;
            char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;

            setenv("PATH", "/usr/bin:/bin", 1);
            setenv("ZDOTDIR", tmp, 1);
            setenv("HOME", tmp, 1);

            terminal_init(&t);
            const char *shell_path_argv[] = {"wave-shell-path-test", NULL};
            CHECK(terminal_spawn(&t, "path", ".", shell_path_argv));
            poll_terminal_until_done(&t);
            CHECK(terminal_contains(&t, "shell-path-ok"));
            terminal_free(&t);

            restore_env_value("PATH", old_path);
            restore_env_value("ZDOTDIR", old_zdotdir);
            restore_env_value("HOME", old_home);
            free(old_path);
            free(old_zdotdir);
            free(old_home);
        }
    }

#ifdef WAVE_USE_GHOSTTY_VT
    terminal_init(&t);
    const char *color_argv[] = {
        "/bin/sh", "-lc", "printf '\\033[31mred\\033[0m'", NULL
    };
    CHECK(terminal_spawn(&t, "color", ".", color_argv));
    poll_terminal_until_done(&t);
    CHECK(terminal_contains_red_style(&t, "red"));
    terminal_free(&t);
#endif

    TEST_REPORT();
}
