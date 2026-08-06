#ifndef WAVE_THEME_H
#define WAVE_THEME_H

typedef struct { float r, g, b; } Color;

/* A theme is one row of colors: the tree-sitter capture colors the front-end
 * draws text with, plus the chrome slots GPUI paints around it.
 *
 * Slots are looked up by name rather than by index so a front-end never has to
 * track this enum's order; theme_color()/theme_ui() take the same strings the
 * highlight queries and the UI already use. */
#define THEME_MAX 8

Color theme_color(const char *capture);

/* 0xRRGGBB for a chrome slot, one of:
 *   bg sidebar_bg status_bg tab_bg tab_active_bg border fg muted dim gutter
 *   cursor selection match accent diagnostic dir added removed
 * An unknown slot returns the theme's fg, which is visible but not a crash. */
unsigned theme_ui(const char *slot);

/* The built-in themes, in menu order. `name` is the config key
 * ("gruvbox-dark"); `label` is what a picker shows ("Gruvbox Dark"). */
int theme_count(void);
const char *theme_name(int index);
const char *theme_label(int index);

/* Select by name. Returns 0 and keeps the current theme if the name is
 * unknown, so a hand-edited config cannot leave the editor colorless. */
int theme_set(const char *name);
const char *theme_active(void);
int theme_active_index(void);

#endif
