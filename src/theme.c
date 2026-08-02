#include "theme.h"

#include <string.h>

/* Colors are written as 0xRRGGBB here — the form every palette upstream
 * publishes — and unpacked to floats on the way out, so a theme can be
 * transcribed without arithmetic. "wave" is the palette theme_color() returned
 * before themes existed, to within a rounding step. */

enum {
    CAP_DEFAULT, CAP_KEYWORD, CAP_TYPE, CAP_STRING, CAP_NUMBER,
    CAP_CONSTANT, CAP_COMMENT, CAP_FUNCTION, CAP_PROPERTY, CAP_COUNT
};

enum {
    UI_BG, UI_SIDEBAR_BG, UI_STATUS_BG, UI_TAB_BG, UI_TAB_ACTIVE_BG,
    UI_BORDER, UI_FG, UI_MUTED, UI_DIM, UI_GUTTER, UI_CURSOR, UI_SELECTION,
    UI_MATCH, UI_ACCENT, UI_DIAGNOSTIC, UI_DIR, UI_ADDED, UI_REMOVED, UI_COUNT
};

typedef struct {
    const char *name;
    const char *label;
    unsigned cap[CAP_COUNT];
    unsigned ui[UI_COUNT];
} Theme;

/* Order here is menu order, and "wave" stays first: it is the default. */
static const Theme THEMES[] = {
    {
        "wave", "Wave",
        {
            /* default */ 0xd9dbe0, /* keyword  */ 0xdb82c7,
            /* type    */ 0x75b5f5, /* string   */ 0x9ecc6b,
            /* number  */ 0xe69e5c, /* constant */ 0xe69e5c,
            /* comment */ 0x6b7585, /* function */ 0xebcc73,
            /* property */ 0x99c7e6,
        },
        {
            /* bg         */ 0x1a1c20, /* sidebar_bg */ 0x16181c,
            /* status_bg  */ 0x22252b, /* tab_bg     */ 0x16181c,
            /* tab_active */ 0x1a1c20, /* border     */ 0x33383f,
            /* fg         */ 0xd9dbe0, /* muted      */ 0xb5bac4,
            /* dim        */ 0x6b7280, /* gutter     */ 0x4a505c,
            /* cursor     */ 0xd9dbe0, /* selection  */ 0x2f4f6f,
            /* match      */ 0x5a4a1f, /* accent     */ 0xe6c07b,
            /* diagnostic */ 0xd9534f, /* dir        */ 0x89b4fa,
            /* added      */ 0x7fb069, /* removed    */ 0xd9534f,
        },
    },
    {
        "gruvbox-dark", "Gruvbox Dark",
        {
            0xebdbb2, 0xfb4934, 0x8ec07c, 0xb8bb26, 0xd3869b,
            0xd3869b, 0x928374, 0xfabd2f, 0x83a598,
        },
        {
            0x282828, 0x1d2021, 0x32302f, 0x1d2021, 0x282828,
            0x504945, 0xebdbb2, 0xd5c4a1, 0x928374, 0x7c6f64, 0xebdbb2,
            0x504945, 0x665c54, 0xfabd2f, 0xfb4934, 0x83a598, 0xb8bb26, 0xfb4934,
        },
    },
    {
        "gruvbox-light", "Gruvbox Light",
        {
            0x3c3836, 0x9d0006, 0x427b58, 0x79740e, 0x8f3f71,
            0x8f3f71, 0x928374, 0xb57614, 0x076678,
        },
        {
            0xfbf1c7, 0xf2e5bc, 0xebdbb2, 0xf2e5bc, 0xfbf1c7,
            0xd5c4a1, 0x3c3836, 0x504945, 0x7c6f64, 0xa89984, 0x3c3836,
            0xd5c4a1, 0xe8d5a3, 0xb57614, 0x9d0006, 0x076678, 0x79740e, 0x9d0006,
        },
    },
    {
        "nord", "Nord",
        {
            0xd8dee9, 0x81a1c1, 0x8fbcbb, 0xa3be8c, 0xb48ead,
            0xb48ead, 0x616e88, 0x88c0d0, 0x8fbcbb,
        },
        {
            0x2e3440, 0x292e39, 0x3b4252, 0x292e39, 0x2e3440,
            0x434c5e, 0xd8dee9, 0xb8c2d0, 0x7b88a1, 0x4c566a, 0xd8dee9,
            0x434c5e, 0x4c566a, 0xebcb8b, 0xbf616a, 0x88c0d0, 0xa3be8c, 0xbf616a,
        },
    },
    {
        "solarized-dark", "Solarized Dark",
        {
            0x93a1a1, 0x859900, 0xb58900, 0x2aa198, 0xd33682,
            0xd33682, 0x586e75, 0x268bd2, 0x93a1a1,
        },
        {
            0x002b36, 0x002128, 0x073642, 0x002128, 0x002b36,
            0x0d4a55, 0x93a1a1, 0x839496, 0x586e75, 0x586e75, 0x93a1a1,
            0x0f4a52, 0x14545c, 0xb58900, 0xdc322f, 0x268bd2, 0x859900, 0xdc322f,
        },
    },
    {
        "tokyo-night", "Tokyo Night",
        {
            0xc0caf5, 0xbb9af7, 0x2ac3de, 0x9ece6a, 0xff9e64,
            0xff9e64, 0x565f89, 0x7aa2f7, 0x7dcfff,
        },
        {
            0x1a1b26, 0x16161e, 0x1f2335, 0x16161e, 0x1a1b26,
            0x2f334d, 0xc0caf5, 0xa9b1d6, 0x565f89, 0x3b4261, 0xc0caf5,
            0x283457, 0x3d59a1, 0xe0af68, 0xf7768e, 0x7aa2f7, 0x9ece6a, 0xf7768e,
        },
    },
};

#define THEME_N ((int)(sizeof THEMES / sizeof THEMES[0]))

static const Theme *active = &THEMES[0];

static int capture_index(const char *name) {
    if (!name) return CAP_DEFAULT;
    if (!strcmp(name, "keyword")) return CAP_KEYWORD;
    if (!strcmp(name, "type")) return CAP_TYPE;
    if (!strcmp(name, "string")) return CAP_STRING;
    if (!strcmp(name, "number")) return CAP_NUMBER;
    if (!strcmp(name, "constant")) return CAP_CONSTANT;
    if (!strcmp(name, "comment")) return CAP_COMMENT;
    if (!strcmp(name, "function")) return CAP_FUNCTION;
    if (!strcmp(name, "property")) return CAP_PROPERTY;
    return CAP_DEFAULT;
}

static int ui_index(const char *slot) {
    if (!slot) return -1;
    if (!strcmp(slot, "bg")) return UI_BG;
    if (!strcmp(slot, "sidebar_bg")) return UI_SIDEBAR_BG;
    if (!strcmp(slot, "status_bg")) return UI_STATUS_BG;
    if (!strcmp(slot, "tab_bg")) return UI_TAB_BG;
    if (!strcmp(slot, "tab_active_bg")) return UI_TAB_ACTIVE_BG;
    if (!strcmp(slot, "border")) return UI_BORDER;
    if (!strcmp(slot, "fg")) return UI_FG;
    if (!strcmp(slot, "muted")) return UI_MUTED;
    if (!strcmp(slot, "dim")) return UI_DIM;
    if (!strcmp(slot, "gutter")) return UI_GUTTER;
    if (!strcmp(slot, "cursor")) return UI_CURSOR;
    if (!strcmp(slot, "selection")) return UI_SELECTION;
    if (!strcmp(slot, "match")) return UI_MATCH;
    if (!strcmp(slot, "accent")) return UI_ACCENT;
    if (!strcmp(slot, "diagnostic")) return UI_DIAGNOSTIC;
    if (!strcmp(slot, "dir")) return UI_DIR;
    if (!strcmp(slot, "added")) return UI_ADDED;
    if (!strcmp(slot, "removed")) return UI_REMOVED;
    return -1;
}

static Color unpack(unsigned rgb) {
    Color c;
    c.r = (float)((rgb >> 16) & 0xffu) / 255.0f;
    c.g = (float)((rgb >> 8) & 0xffu) / 255.0f;
    c.b = (float)(rgb & 0xffu) / 255.0f;
    return c;
}

Color theme_color(const char *capture) {
    return unpack(active->cap[capture_index(capture)]);
}

unsigned theme_ui(const char *slot) {
    int i = ui_index(slot);
    return i < 0 ? active->ui[UI_FG] : active->ui[i];
}

int theme_count(void) { return THEME_N; }

const char *theme_name(int index) {
    return (index >= 0 && index < THEME_N) ? THEMES[index].name : "";
}

const char *theme_label(int index) {
    return (index >= 0 && index < THEME_N) ? THEMES[index].label : "";
}

int theme_set(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < THEME_N; i++) {
        if (!strcmp(THEMES[i].name, name)) {
            active = &THEMES[i];
            return 1;
        }
    }
    return 0;
}

const char *theme_active(void) { return active->name; }

int theme_active_index(void) {
    for (int i = 0; i < THEME_N; i++)
        if (&THEMES[i] == active) return i;
    return 0;
}
