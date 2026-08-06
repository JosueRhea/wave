/* langs.h - map a file path to a tree-sitter grammar + highlights query.
 *
 * The editor is language-aware by file extension (and basename for dotenv):
 * C, JavaScript/JSX, TypeScript, TSX, JSON, and dotenv (.env*). Each entry
 * pairs the grammar entry point with a highlights query loaded from
 * queries/<language>/highlights.scm. Query capture names line up with the
 * editor theme ("keyword", "type", "string", "number", "comment", "function",
 * "property", "constant").
 */
#ifndef WAVE_LANGS_H
#define WAVE_LANGS_H

typedef struct TSLanguage TSLanguage;

typedef struct {
    const char *name;        /* "c", "javascript", "typescript", "tsx",
                              * "json", "dotenv" */
    const TSLanguage *lang;  /* grammar entry point */
    const char *query;       /* loaded highlights query source, or NULL */
} Language;

/* Detect the language for `path` by its extension (or basename for .env files).
 * Returns NULL if the file type is unknown (the editor then opens it as plain,
 * unhighlighted text). */
const Language *lang_detect(const char *path);

#endif /* WAVE_LANGS_H */
