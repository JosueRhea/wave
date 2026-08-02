/* test_theme.c - the built-in theme registry and its lookups. */
#include "test.h"
#include "theme.h"

#include <string.h>

static int byte_of(float v) { return (int)(v * 255.0f + 0.5f); }

int main(void) {
    CHECK(theme_count() >= 2);
    CHECK(theme_count() <= THEME_MAX);

    /* The first theme is the default and stays selected until asked otherwise. */
    CHECK_STR(theme_name(0), "wave");
    CHECK_STR(theme_active(), "wave");
    CHECK_EQ(theme_active_index(), 0);

    /* Every theme has both a config name and a display label, and no two
     * themes share a name — the config stores the name, so a duplicate would
     * make one of them unreachable. */
    for (int i = 0; i < theme_count(); i++) {
        CHECK(theme_name(i)[0] != '\0');
        CHECK(theme_label(i)[0] != '\0');
        for (int j = i + 1; j < theme_count(); j++)
            CHECK(strcmp(theme_name(i), theme_name(j)) != 0);
    }

    /* Out-of-range indices are empty strings, not reads off the end. */
    CHECK_STR(theme_name(-1), "");
    CHECK_STR(theme_name(theme_count()), "");
    CHECK_STR(theme_label(-1), "");
    CHECK_STR(theme_label(theme_count()), "");

    /* wave's default foreground is the pre-theme palette, 0xd9dbe0. */
    Color fg = theme_color(NULL);
    CHECK_EQ(byte_of(fg.r), 0xd9);
    CHECK_EQ(byte_of(fg.g), 0xdb);
    CHECK_EQ(byte_of(fg.b), 0xe0);
    /* An unknown capture falls back to that same default rather than black. */
    Color unknown = theme_color("no-such-capture");
    CHECK_EQ(byte_of(unknown.r), byte_of(fg.r));
    CHECK_EQ(byte_of(unknown.g), byte_of(fg.g));
    CHECK_EQ(byte_of(unknown.b), byte_of(fg.b));

    CHECK_EQ((int)theme_ui("bg"), 0x1a1c20);
    CHECK_EQ((int)theme_ui("selection"), 0x2f4f6f);
    /* An unknown slot returns fg: visible, never invisible-on-invisible. */
    CHECK_EQ((int)theme_ui("no-such-slot"), (int)theme_ui("fg"));
    CHECK_EQ((int)theme_ui(NULL), (int)theme_ui("fg"));

    /* Switching repaints both halves: captures and chrome. */
    CHECK(theme_set("gruvbox-dark"));
    CHECK_STR(theme_active(), "gruvbox-dark");
    CHECK_EQ((int)theme_ui("bg"), 0x282828);
    Color keyword = theme_color("keyword");
    CHECK_EQ(byte_of(keyword.r), 0xfb);
    CHECK_EQ(byte_of(keyword.g), 0x49);
    CHECK_EQ(byte_of(keyword.b), 0x34);

    /* A name the build does not have leaves the current theme alone, so a
     * stale config cannot land the editor on an empty palette. */
    CHECK_EQ(theme_set("nope"), 0);
    CHECK_EQ(theme_set(NULL), 0);
    CHECK_STR(theme_active(), "gruvbox-dark");

    /* Light themes exist, and are actually light — the chrome slots are what
     * the front-end paints, so a light theme with a dark bg would be a typo. */
    CHECK(theme_set("gruvbox-light"));
    unsigned bg = theme_ui("bg");
    CHECK(((bg >> 16) & 0xff) > 0x80);
    CHECK((int)theme_ui("fg") < 0x808080);

    CHECK(theme_set("wave"));
    CHECK_EQ(theme_active_index(), 0);

    TEST_REPORT();
}
