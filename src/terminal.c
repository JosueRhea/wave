#include "terminal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#ifdef WAVE_USE_GHOSTTY_VT
static void terminal_ghostty_configure_effects(Terminal *t);
static void terminal_ghostty_sync_cells(Terminal *t);
#endif

static char *term_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void terminal_line_style_free(TerminalLineStyle *style) {
    if (!style) return;
    free(style->cells);
    style->cells = NULL;
    style->ncells = 0;
    style->cap_cells = 0;
}

#ifdef WAVE_USE_GHOSTTY_VT
static int terminal_line_style_push(TerminalLineStyle *style, size_t col_start,
                                    size_t col_len, size_t byte_start,
                                    size_t byte_len, TerminalColor fg,
                                    TerminalColor bg, int has_fg, int has_bg) {
    if (!style || col_len == 0) return 1;
    if (style->ncells == style->cap_cells) {
        size_t nc = style->cap_cells ? style->cap_cells * 2 : 64;
        TerminalCellStyle *next = realloc(style->cells, nc * sizeof *next);
        if (!next) return 0;
        style->cells = next;
        style->cap_cells = nc;
    }
    style->cells[style->ncells++] = (TerminalCellStyle){
        .col_start = col_start,
        .col_len = col_len,
        .byte_start = byte_start,
        .byte_len = byte_len,
        .fg = fg,
        .bg = bg,
        .has_fg = has_fg ? 1 : 0,
        .has_bg = has_bg ? 1 : 0,
    };
    return 1;
}
#endif

void terminal_init(Terminal *t) {
    if (!t) return;
    memset(t, 0, sizeof *t);
    t->fd = -1;
    t->max_lines = 2000;
    t->cols = 80;
    t->rows = 24;
    t->cursor_visible = 0;
#ifdef WAVE_USE_GHOSTTY_VT
    GhosttyTerminalOptions opts = {
        .cols = (uint16_t)t->cols,
        .rows = (uint16_t)t->rows,
        .max_scrollback = t->max_lines,
    };
    if (ghostty_terminal_new(NULL, &t->ghostty_terminal, opts) == GHOSTTY_SUCCESS &&
        ghostty_render_state_new(NULL, &t->ghostty_render_state) == GHOSTTY_SUCCESS &&
        ghostty_render_state_row_iterator_new(NULL, &t->ghostty_row_iter) == GHOSTTY_SUCCESS &&
        ghostty_render_state_row_cells_new(NULL, &t->ghostty_row_cells) == GHOSTTY_SUCCESS) {
        terminal_ghostty_configure_effects(t);
        t->ghostty_enabled = 1;
    } else {
        if (t->ghostty_row_cells) {
            ghostty_render_state_row_cells_free(t->ghostty_row_cells);
            t->ghostty_row_cells = NULL;
        }
        if (t->ghostty_row_iter) {
            ghostty_render_state_row_iterator_free(t->ghostty_row_iter);
            t->ghostty_row_iter = NULL;
        }
        if (t->ghostty_render_state) {
            ghostty_render_state_free(t->ghostty_render_state);
            t->ghostty_render_state = NULL;
        }
        if (t->ghostty_terminal) {
            ghostty_terminal_free(t->ghostty_terminal);
            t->ghostty_terminal = NULL;
        }
    }
#endif
}

static void terminal_clear_lines(Terminal *t) {
    if (!t) return;
    for (size_t i = 0; i < t->nlines; i++) free(t->lines[i]);
    for (size_t i = 0; i < t->nlines; i++)
        terminal_line_style_free(&t->line_styles[i]);
    free(t->lines);
    free(t->line_styles);
    t->lines = NULL;
    t->line_styles = NULL;
    t->nlines = 0;
    t->cap_lines = 0;
    t->scroll = 0;
    t->current_len = 0;
    t->current[0] = '\0';
}

void terminal_free(Terminal *t) {
    if (!t) return;
    terminal_stop(t);
    terminal_clear_lines(t);
#ifdef WAVE_USE_GHOSTTY_VT
    if (t->ghostty_row_cells) {
        ghostty_render_state_row_cells_free(t->ghostty_row_cells);
        t->ghostty_row_cells = NULL;
    }
    if (t->ghostty_row_iter) {
        ghostty_render_state_row_iterator_free(t->ghostty_row_iter);
        t->ghostty_row_iter = NULL;
    }
    if (t->ghostty_render_state) {
        ghostty_render_state_free(t->ghostty_render_state);
        t->ghostty_render_state = NULL;
    }
    if (t->ghostty_terminal) {
        ghostty_terminal_free(t->ghostty_terminal);
        t->ghostty_terminal = NULL;
    }
    t->ghostty_enabled = 0;
#endif
}

static void terminal_append_line_styled(Terminal *t, const char *line,
                                        TerminalLineStyle *style) {
    if (!t) return;
    if (t->nlines == t->cap_lines) {
        size_t nc = t->cap_lines ? t->cap_lines * 2 : 128;
        char **nl = calloc(nc, sizeof *nl);
        TerminalLineStyle *ns = calloc(nc, sizeof *ns);
        if (!nl || !ns) {
            free(nl);
            free(ns);
            return;
        }
        if (t->lines)
            memcpy(nl, t->lines, t->nlines * sizeof *nl);
        if (t->line_styles)
            memcpy(ns, t->line_styles, t->nlines * sizeof *ns);
        free(t->lines);
        free(t->line_styles);
        t->lines = nl;
        t->line_styles = ns;
        t->cap_lines = nc;
    }
    if (t->max_lines && t->nlines >= t->max_lines) {
        free(t->lines[0]);
        terminal_line_style_free(&t->line_styles[0]);
        memmove(t->lines, t->lines + 1, (t->nlines - 1) * sizeof *t->lines);
        memmove(t->line_styles, t->line_styles + 1,
                (t->nlines - 1) * sizeof *t->line_styles);
        t->nlines--;
        memset(&t->line_styles[t->nlines], 0, sizeof t->line_styles[t->nlines]);
    }
    t->lines[t->nlines++] = term_strdup(line ? line : "");
    if (style) {
        t->line_styles[t->nlines - 1] = *style;
        memset(style, 0, sizeof *style);
    } else {
        memset(&t->line_styles[t->nlines - 1], 0,
               sizeof t->line_styles[t->nlines - 1]);
    }
}

static void terminal_append_line(Terminal *t, const char *line) {
    terminal_append_line_styled(t, line, NULL);
}

static void terminal_finish_current(Terminal *t) {
    t->current[t->current_len] = '\0';
    terminal_append_line(t, t->current);
    t->current_len = 0;
    t->current[0] = '\0';
}

static int terminal_ansi_skip(const char *s, size_t n, size_t *i) {
    size_t p = *i;
    if (p >= n || s[p] != '\033') return 0;
    if (p + 1 >= n) return 1;
    p++;
    if (s[p] == '[') {
        p++;
        while (p < n && !isalpha((unsigned char)s[p]) && s[p] != '~') p++;
        *i = p < n ? p : n - 1;
        return 1;
    }
    if (s[p] == ']') {
        p++;
        while (p < n && s[p] != '\a') p++;
        *i = p < n ? p : n - 1;
        return 1;
    }
    *i = p;
    return 1;
}

static void terminal_feed(Terminal *t, const char *s, size_t n) {
#ifdef WAVE_USE_GHOSTTY_VT
    if (t && t->ghostty_enabled && t->ghostty_terminal) {
        ghostty_terminal_vt_write(t->ghostty_terminal, (const uint8_t *)s, n);
        terminal_ghostty_sync_cells(t);
        return;
    }
#endif
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\033' && terminal_ansi_skip(s, n, &i)) continue;
        if (c == '\r' && i + 1 < n && s[i + 1] == '\n') {
            continue;
        }
        if (c == '\r') {
            t->current_len = 0;
            t->current[0] = '\0';
            continue;
        }
        if (c == '\n') {
            terminal_finish_current(t);
            continue;
        }
        if (c == '\b' || c == 127) {
            if (t->current_len > 0) t->current[--t->current_len] = '\0';
            continue;
        }
        if (c < 32 && c != '\t') continue;
        if (t->current_len + 1 >= sizeof t->current) terminal_finish_current(t);
        t->current[t->current_len++] = (char)c;
        t->current[t->current_len] = '\0';
    }
}

int terminal_spawn(Terminal *t, const char *title, const char *cwd,
                   const char *const argv[]) {
    if (!t || !argv || !argv[0]) return 0;
    terminal_stop(t);
    terminal_clear_lines(t);
    snprintf(t->title, sizeof t->title, "%s", title ? title : argv[0]);
    snprintf(t->cwd, sizeof t->cwd, "%s", cwd ? cwd : ".");
#ifdef WAVE_USE_GHOSTTY_VT
    if (t->ghostty_enabled && t->ghostty_terminal) {
        ghostty_terminal_reset(t->ghostty_terminal);
        ghostty_terminal_resize(t->ghostty_terminal, (uint16_t)t->cols,
                                (uint16_t)t->rows, 0, 0);
    }
#endif

    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)(t->cols > 0 ? t->cols : 80);
    ws.ws_row = (unsigned short)(t->rows > 0 ? t->rows : 24);

    pid_t pid = forkpty(&t->fd, NULL, NULL, &ws);
    if (pid < 0) {
        t->fd = -1;
        terminal_append_line(t, "failed to start terminal");
        return 0;
    }
    if (pid == 0) {
        if (cwd && chdir(cwd) != 0) _exit(127);
        setenv("TERM", "xterm-256color", 0);
        execvp(argv[0], (char *const *)argv);
        fprintf(stderr, "wave: command not found: %s\r\n", argv[0]);
        _exit(127);
    }

    fcntl(t->fd, F_SETFL, O_NONBLOCK);
    t->pid = pid;
    t->running = 1;
    t->exit_status = 0;
    return 1;
}

void terminal_stop(Terminal *t) {
    if (!t) return;
    if (t->pid > 0) {
        kill(t->pid, SIGTERM);
        waitpid(t->pid, NULL, WNOHANG);
    }
    if (t->fd >= 0) close(t->fd);
    t->pid = 0;
    t->fd = -1;
    t->running = 0;
}

void terminal_poll(Terminal *t) {
    if (!t || t->fd < 0) return;
    char buf[4096];
    for (;;) {
        ssize_t n = read(t->fd, buf, sizeof buf);
        if (n > 0) {
            terminal_feed(t, buf, (size_t)n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n == 0 || (n < 0 && errno != EINTR)) {
            close(t->fd);
            t->fd = -1;
            break;
        }
    }
    if (t->pid > 0) {
        int status = 0;
        pid_t got = waitpid(t->pid, &status, WNOHANG);
        if (got == t->pid) {
            t->pid = 0;
            t->running = 0;
            t->exit_status = status;
            terminal_append_line(t, "");
            terminal_append_line(t, "[process exited]");
        }
    }
}

void terminal_write(Terminal *t, const char *data, size_t len) {
    if (!t || t->fd < 0 || !data || !len) return;
    (void)write(t->fd, data, len);
}

static int terminal_modifier_param(int shift, int alt, int control) {
    int param = 1;
    if (shift) param += 1;
    if (alt) param += 2;
    if (control) param += 4;
    return param;
}

static size_t terminal_copy_seq(const char *seq, char *out, size_t cap) {
    size_t n = seq ? strlen(seq) : 0;
    if (out && cap > 0) {
        size_t copy = n < cap - 1 ? n : cap - 1;
        if (copy) memcpy(out, seq, copy);
        out[copy] = '\0';
    }
    return n;
}

static size_t terminal_copy_bytes(const char *bytes, size_t n, char *out, size_t cap) {
    if (out && cap > 0) {
        size_t copy = n < cap - 1 ? n : cap - 1;
        if (copy) memcpy(out, bytes, copy);
        out[copy] = '\0';
    }
    return n;
}

size_t terminal_key_sequence(int key, int shift, int alt, int control,
                             char *out, size_t cap) {
    char seq[32];
    const char *plain = NULL;
    int mod = terminal_modifier_param(shift, alt, control);

    if (control) {
        size_t prefix = 0;
        if (alt) seq[prefix++] = '\033';
        if (key >= 65 && key <= 90) {
            seq[prefix++] = (char)(key - 65 + 1);
            return terminal_copy_bytes(seq, prefix, out, cap);
        }
        switch (key) {
        case 32: seq[prefix++] = '\0'; return terminal_copy_bytes(seq, prefix, out, cap);
        case 47: seq[prefix++] = 0x1f; return terminal_copy_bytes(seq, prefix, out, cap);
        case 91: seq[prefix++] = 0x1b; return terminal_copy_bytes(seq, prefix, out, cap);
        case 92: seq[prefix++] = 0x1c; return terminal_copy_bytes(seq, prefix, out, cap);
        case 93: seq[prefix++] = 0x1d; return terminal_copy_bytes(seq, prefix, out, cap);
        case 45: seq[prefix++] = 0x1f; return terminal_copy_bytes(seq, prefix, out, cap);
        default: break;
        }
    }

    switch (key) {
    case 256: plain = "\033"; break;
    case 257: plain = "\r"; break;
    case 335: plain = "\r"; break;
    case 258: plain = shift ? "\033[Z" : "\t"; break;
    case 259: plain = "\177"; break;
    case 260:
        if (mod == 1) plain = "\033[2~";
        else { snprintf(seq, sizeof seq, "\033[2;%d~", mod); plain = seq; }
        break;
    case 261:
        if (mod == 1) plain = "\033[3~";
        else { snprintf(seq, sizeof seq, "\033[3;%d~", mod); plain = seq; }
        break;
    case 262:
    case 263:
    case 264:
    case 265: {
        char final = key == 262 ? 'C' : key == 263 ? 'D' : key == 264 ? 'B' : 'A';
        if (mod == 1) snprintf(seq, sizeof seq, "\033[%c", final);
        else snprintf(seq, sizeof seq, "\033[1;%d%c", mod, final);
        plain = seq;
        break;
    }
    case 266:
        if (mod == 1) plain = "\033[5~";
        else { snprintf(seq, sizeof seq, "\033[5;%d~", mod); plain = seq; }
        break;
    case 267:
        if (mod == 1) plain = "\033[6~";
        else { snprintf(seq, sizeof seq, "\033[6;%d~", mod); plain = seq; }
        break;
    case 268:
        if (mod == 1) plain = "\033[H";
        else { snprintf(seq, sizeof seq, "\033[1;%dH", mod); plain = seq; }
        break;
    case 269:
        if (mod == 1) plain = "\033[F";
        else { snprintf(seq, sizeof seq, "\033[1;%dF", mod); plain = seq; }
        break;
    case 290:
    case 291:
    case 292:
    case 293: {
        static const char finals[] = {'P', 'Q', 'R', 'S'};
        int idx = key - 290;
        if (mod == 1) snprintf(seq, sizeof seq, "\033O%c", finals[idx]);
        else snprintf(seq, sizeof seq, "\033[1;%d%c", mod, finals[idx]);
        plain = seq;
        break;
    }
    default: break;
    }

    if (!plain && key >= 294 && key <= 301) {
        static const int nums[] = {15, 17, 18, 19, 20, 21, 23, 24};
        int num = nums[key - 294];
        if (mod == 1) snprintf(seq, sizeof seq, "\033[%d~", num);
        else snprintf(seq, sizeof seq, "\033[%d;%d~", num, mod);
        plain = seq;
    }

    if (!plain) return 0;
    return terminal_copy_seq(plain, out, cap);
}

void terminal_send_key_mods(Terminal *t, int key, int shift, int alt, int control) {
    char seq[32];
    size_t n = terminal_key_sequence(key, shift, alt, control, seq, sizeof seq);
    if (n) terminal_write(t, seq, n);
}

void terminal_send_key(Terminal *t, int key) {
    terminal_send_key_mods(t, key, 0, 0, 0);
}

void terminal_resize(Terminal *t, int rows, int cols) {
    if (!t) return;
    if (rows < 2) rows = 2;
    if (cols < 10) cols = 10;
    if (t->rows == rows && t->cols == cols) return;
    t->rows = rows;
    t->cols = cols;
#ifdef WAVE_USE_GHOSTTY_VT
    if (t->ghostty_enabled && t->ghostty_terminal) {
        ghostty_terminal_resize(t->ghostty_terminal, (uint16_t)t->cols,
                                (uint16_t)t->rows, 0, 0);
        terminal_ghostty_sync_cells(t);
    }
#endif
    if (t->fd < 0) return;
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ioctl(t->fd, TIOCSWINSZ, &ws);
}

void terminal_scroll(Terminal *t, int units) {
    if (!t) return;
#ifdef WAVE_USE_GHOSTTY_VT
    if (t->ghostty_enabled && t->ghostty_terminal) {
        GhosttyTerminalScrollViewport scroll = {
            .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
            .value = { .delta = -units },
        };
        ghostty_terminal_scroll_viewport(t->ghostty_terminal, scroll);
        terminal_ghostty_sync_cells(t);
        return;
    }
#endif
    if (units > 0) {
        t->scroll += (size_t)units;
    } else if (units < 0) {
        size_t u = (size_t)(-units);
        t->scroll = u > t->scroll ? 0 : t->scroll - u;
    }
    if (t->scroll > t->nlines) t->scroll = t->nlines;
}

size_t terminal_visible_start(const Terminal *t, int rows) {
    if (!t || rows <= 0) return 0;
    size_t total = t->nlines + (t->current_len ? 1u : 0u);
    size_t end = total > t->scroll ? total - t->scroll : 0;
    if (end <= (size_t)rows) return 0;
    return end - (size_t)rows;
}

const char *terminal_line(const Terminal *t, size_t index) {
    if (!t) return "";
    if (index < t->nlines) return t->lines[index] ? t->lines[index] : "";
    if (index == t->nlines) return t->current;
    return "";
}

const TerminalLineStyle *terminal_line_style(const Terminal *t, size_t index) {
    if (!t || index >= t->nlines || !t->line_styles) return NULL;
    return &t->line_styles[index];
}

const char *terminal_status(const Terminal *t) {
    if (!t) return "idle";
    return t->running ? "running" : "stopped";
}

#ifdef WAVE_USE_GHOSTTY_VT
static TerminalColor terminal_ghostty_rgb(GhosttyColorRgb rgb) {
    return (TerminalColor){
        (float)rgb.r / 255.0f,
        (float)rgb.g / 255.0f,
        (float)rgb.b / 255.0f,
    };
}

static TerminalColor terminal_ghostty_default_fg(void) {
    return (TerminalColor){0.84f, 0.86f, 0.88f};
}

static TerminalColor terminal_ghostty_default_bg(void) {
    return (TerminalColor){0.075f, 0.085f, 0.105f};
}

static void terminal_ghostty_copy_string(char *dst, size_t cap,
                                         GhosttyString s) {
    if (!dst || !cap) return;
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (s.ptr && n > 0) memcpy(dst, s.ptr, n);
    dst[n] = '\0';
}

static void terminal_ghostty_on_write_pty(GhosttyTerminal terminal,
                                          void *userdata,
                                          const uint8_t *data, size_t len) {
    (void)terminal;
    Terminal *t = (Terminal *)userdata;
    if (!t || !data || !len || t->fd < 0) return;
    (void)write(t->fd, data, len);
}

static void terminal_ghostty_on_title_changed(GhosttyTerminal terminal,
                                              void *userdata) {
    Terminal *t = (Terminal *)userdata;
    if (!t) return;
    GhosttyString title = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title) !=
        GHOSTTY_SUCCESS)
        return;
    if (title.len > 0) terminal_ghostty_copy_string(t->title, sizeof t->title, title);
}

static void terminal_ghostty_on_pwd_changed(GhosttyTerminal terminal,
                                            void *userdata) {
    Terminal *t = (Terminal *)userdata;
    if (!t) return;
    GhosttyString pwd = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_PWD, &pwd) !=
        GHOSTTY_SUCCESS)
        return;
    if (pwd.len > 0) terminal_ghostty_copy_string(t->cwd, sizeof t->cwd, pwd);
}

static void terminal_ghostty_configure_effects(Terminal *t) {
    if (!t || !t->ghostty_terminal) return;
    (void)ghostty_terminal_set(t->ghostty_terminal, GHOSTTY_TERMINAL_OPT_USERDATA,
                               t);
    (void)ghostty_terminal_set(t->ghostty_terminal,
                               GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                               (const void *)terminal_ghostty_on_write_pty);
    (void)ghostty_terminal_set(t->ghostty_terminal,
                               GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                               (const void *)terminal_ghostty_on_title_changed);
    (void)ghostty_terminal_set(t->ghostty_terminal,
                               GHOSTTY_TERMINAL_OPT_PWD_CHANGED,
                               (const void *)terminal_ghostty_on_pwd_changed);
}

static int terminal_line_push_bytes(char **line, size_t *len, size_t *cap,
                                    const char *bytes, size_t n) {
    if (!line || !len || !cap || !bytes) return 0;
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 128;
        while (*len + n + 1 > nc) nc *= 2;
        char *next = realloc(*line, nc);
        if (!next) return 0;
        *line = next;
        *cap = nc;
    }
    memcpy(*line + *len, bytes, n);
    *len += n;
    (*line)[*len] = '\0';
    return 1;
}

static void terminal_ghostty_cell_style(Terminal *t, TerminalLineStyle *style,
                                        size_t col_start, size_t col_len,
                                        size_t byte_start, size_t byte_len) {
    if (!t || !style || !t->ghostty_row_cells || col_len == 0) return;
    TerminalColor fg = terminal_ghostty_default_fg();
    TerminalColor bg = terminal_ghostty_default_bg();
    int has_fg = 0;
    int has_bg = 0;

    GhosttyColorRgb rgb = {0};
    if (ghostty_render_state_row_cells_get(
            t->ghostty_row_cells,
            GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &rgb) ==
        GHOSTTY_SUCCESS) {
        fg = terminal_ghostty_rgb(rgb);
        has_fg = 1;
    }
    if (ghostty_render_state_row_cells_get(
            t->ghostty_row_cells,
            GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &rgb) ==
        GHOSTTY_SUCCESS) {
        bg = terminal_ghostty_rgb(rgb);
        has_bg = 1;
    }

    GhosttyStyle cell_style;
    memset(&cell_style, 0, sizeof cell_style);
    cell_style.size = sizeof cell_style;
    if (ghostty_render_state_row_cells_get(
            t->ghostty_row_cells,
            GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &cell_style) ==
        GHOSTTY_SUCCESS) {
        if (cell_style.inverse) {
            TerminalColor old_fg = has_fg ? fg : terminal_ghostty_default_fg();
            fg = has_bg ? bg : terminal_ghostty_default_bg();
            bg = old_fg;
            has_fg = 1;
            has_bg = 1;
        }
        if (cell_style.bold && has_fg) {
            fg.r = fg.r * 1.15f > 1.0f ? 1.0f : fg.r * 1.15f;
            fg.g = fg.g * 1.15f > 1.0f ? 1.0f : fg.g * 1.15f;
            fg.b = fg.b * 1.15f > 1.0f ? 1.0f : fg.b * 1.15f;
        }
        if (cell_style.faint && has_fg) {
            fg.r *= 0.68f;
            fg.g *= 0.68f;
            fg.b *= 0.68f;
        }
        if (cell_style.invisible) has_fg = 0;
    }

    (void)terminal_line_style_push(style, col_start, col_len, byte_start,
                                   byte_len, fg, bg, has_fg, has_bg);
}

static void terminal_suppress_shortcuts_marker(char *line,
                                               TerminalLineStyle *style) {
    if (!line || !strstr(line, "for shortcuts")) return;

    for (char *p = line; *p; p++) {
        if (*p == ' ') continue;
        if (*p == '?') {
            size_t offset = (size_t)(p - line);
            *p = ' ';
            if (style) {
                for (size_t i = 0; i < style->ncells; i++) {
                    TerminalCellStyle *cell = &style->cells[i];
                    if (cell->byte_start == offset) {
                        cell->has_fg = 0;
                        cell->has_bg = 0;
                        break;
                    }
                }
            }
        }
        return;
    }
}

static void terminal_ghostty_sync_cursor(Terminal *t) {
    if (!t || !t->ghostty_render_state) return;
    bool visible = false;
    bool has_value = false;
    uint16_t x = 0, y = 0;
    if (ghostty_render_state_get(t->ghostty_render_state,
                                 GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                                 &visible) != GHOSTTY_SUCCESS)
        visible = false;
    if (ghostty_render_state_get(
            t->ghostty_render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
            &has_value) != GHOSTTY_SUCCESS)
        has_value = false;
    if (has_value) {
        (void)ghostty_render_state_get(
            t->ghostty_render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &x);
        (void)ghostty_render_state_get(
            t->ghostty_render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &y);
    }
    t->cursor_visible = visible && has_value;
    t->cursor_col = (int)x;
    t->cursor_row = (int)y;
}

static void terminal_ghostty_sync_cells(Terminal *t) {
    if (!t || !t->ghostty_terminal || !t->ghostty_render_state ||
        !t->ghostty_row_iter || !t->ghostty_row_cells)
        return;
    if (ghostty_render_state_update(t->ghostty_render_state,
                                    t->ghostty_terminal) != GHOSTTY_SUCCESS)
        return;
    terminal_ghostty_sync_cursor(t);
    if (ghostty_render_state_get(t->ghostty_render_state,
                                 GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                 &t->ghostty_row_iter) != GHOSTTY_SUCCESS)
        return;

    terminal_clear_lines(t);
    while (ghostty_render_state_row_iterator_next(t->ghostty_row_iter)) {
        if (ghostty_render_state_row_get(
                t->ghostty_row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                &t->ghostty_row_cells) != GHOSTTY_SUCCESS)
            continue;

        char *line = NULL;
        size_t len = 0, cap = 0;
        size_t col = 0;
        TerminalLineStyle style = {0};
        while (ghostty_render_state_row_cells_next(t->ghostty_row_cells)) {
            size_t cell_start = len;
            size_t cell_col = col++;
            uint8_t stack[32];
            GhosttyBuffer buf = { .ptr = stack, .cap = sizeof stack, .len = 0 };
            GhosttyResult result = ghostty_render_state_row_cells_get(
                t->ghostty_row_cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buf);
            if (result == GHOSTTY_OUT_OF_SPACE && buf.len > sizeof stack) {
                uint8_t *heap = malloc(buf.len);
                if (heap) {
                    buf.ptr = heap;
                    buf.cap = buf.len;
                    buf.len = 0;
                    result = ghostty_render_state_row_cells_get(
                        t->ghostty_row_cells,
                        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8,
                        &buf);
                    if (result == GHOSTTY_SUCCESS && buf.len > 0) {
                        if (terminal_line_push_bytes(
                                &line, &len, &cap, (const char *)heap,
                                buf.len))
                            terminal_ghostty_cell_style(
                                t, &style, cell_col, 1, cell_start,
                                len - cell_start);
                    }
                    free(heap);
                }
                continue;
            }
            if (result == GHOSTTY_SUCCESS && buf.len > 0) {
                if (terminal_line_push_bytes(&line, &len, &cap,
                                             (const char *)stack, buf.len))
                    terminal_ghostty_cell_style(t, &style, cell_col, 1,
                                                cell_start, len - cell_start);
            } else {
                if (terminal_line_push_bytes(&line, &len, &cap, " ", 1))
                    terminal_ghostty_cell_style(t, &style, cell_col, 1,
                                                cell_start, len - cell_start);
            }
        }

        terminal_suppress_shortcuts_marker(line, &style);
        terminal_append_line_styled(t, line ? line : "", &style);
        terminal_line_style_free(&style);
        free(line);

        bool clean_row = false;
        (void)ghostty_render_state_row_set(
            t->ghostty_row_iter, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY,
            &clean_row);
    }

    GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    (void)ghostty_render_state_set(t->ghostty_render_state,
                                   GHOSTTY_RENDER_STATE_OPTION_DIRTY,
                                   &clean_state);
}
#endif
