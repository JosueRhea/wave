/* langs.c — see langs.h. */
#include "langs.h"

#include <string.h>

/* Grammar entry points, linked from the vendored parsers. */
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_javascript(void);
const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);

/* ---- highlights queries ----
 * The bare `(identifier) @default` capture comes first on purpose: the
 * renderer lets a later span win on overlap, so the specific captures
 * (function/type) that follow override it. */

static const char *C_QUERY =
    "[\"if\" \"else\" \"for\" \"while\" \"do\" \"switch\" \"case\" \"break\"\n"
    " \"continue\" \"return\" \"goto\" \"sizeof\" \"struct\" \"union\" \"enum\"\n"
    " \"typedef\" \"const\" \"static\" \"extern\"] @keyword\n"
    "(primitive_type) @type\n"
    "(type_identifier) @type\n"
    "(comment) @comment\n"
    "(string_literal) @string\n"
    "(char_literal) @string\n"
    "(system_lib_string) @string\n"
    "(number_literal) @number\n"
    "(preproc_directive) @keyword\n"
    "(identifier) @default\n"
    "(call_expression function: (identifier) @function)\n";

/* JavaScript / JSX. */
static const char *JS_QUERY =
    "(identifier) @default\n"
    "[\"if\" \"else\" \"for\" \"while\" \"do\" \"switch\" \"case\" \"default\"\n"
    " \"break\" \"continue\" \"return\" \"function\" \"const\" \"let\" \"var\"\n"
    " \"class\" \"extends\" \"new\" \"delete\" \"typeof\" \"instanceof\" \"in\"\n"
    " \"of\" \"import\" \"export\" \"from\" \"as\" \"async\" \"await\" \"yield\"\n"
    " \"throw\" \"try\" \"catch\" \"finally\" \"void\" \"get\" \"set\"\n"
    " \"static\"] @keyword\n"
    "[(true) (false) (null) (undefined)] @constant\n"
    "(comment) @comment\n"
    "(string) @string\n"
    "(template_string) @string\n"
    "(regex) @string\n"
    "(number) @number\n"
    "(property_identifier) @property\n"
    "(call_expression function: (identifier) @function)\n"
    "(function_declaration name: (identifier) @function)\n"
    "(method_definition name: (property_identifier) @function)\n";

/* TypeScript / TSX — the JS captures plus types and TS-only keywords. */
static const char *TS_QUERY =
    "(identifier) @default\n"
    "[\"if\" \"else\" \"for\" \"while\" \"do\" \"switch\" \"case\" \"default\"\n"
    " \"break\" \"continue\" \"return\" \"function\" \"const\" \"let\" \"var\"\n"
    " \"class\" \"extends\" \"new\" \"delete\" \"typeof\" \"instanceof\" \"in\"\n"
    " \"of\" \"import\" \"export\" \"from\" \"as\" \"async\" \"await\" \"yield\"\n"
    " \"throw\" \"try\" \"catch\" \"finally\" \"void\" \"get\" \"set\" \"static\"\n"
    " \"interface\" \"type\" \"enum\" \"namespace\" \"declare\" \"abstract\"\n"
    " \"implements\" \"readonly\" \"public\" \"private\" \"protected\" \"keyof\"\n"
    " \"infer\" \"is\" \"satisfies\" \"override\"] @keyword\n"
    "[(true) (false) (null) (undefined)] @constant\n"
    "(comment) @comment\n"
    "(string) @string\n"
    "(template_string) @string\n"
    "(number) @number\n"
    "(predefined_type) @type\n"
    "(type_identifier) @type\n"
    "(property_identifier) @property\n"
    "(call_expression function: (identifier) @function)\n"
    "(function_declaration name: (identifier) @function)\n"
    "(method_definition name: (property_identifier) @function)\n";

/* Lazily-initialised registry (grammar pointers come from functions). */
static Language registry[4];
static int registry_ready;

static void registry_init(void) {
    registry[0] = (Language){"c", tree_sitter_c(), C_QUERY};
    registry[1] = (Language){"javascript", tree_sitter_javascript(), JS_QUERY};
    registry[2] = (Language){"typescript", tree_sitter_typescript(), TS_QUERY};
    registry[3] = (Language){"tsx", tree_sitter_tsx(), TS_QUERY};
    registry_ready = 1;
}

static const char *ext_of(const char *path) {
    const char *dot = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') dot = NULL;       /* reset on path separators */
        else if (*p == '.') dot = p + 1;
    }
    return dot;
}

const Language *lang_detect(const char *path) {
    if (!path) return NULL;
    if (!registry_ready) registry_init();
    const char *ext = ext_of(path);
    if (!ext) return NULL;

    if (!strcmp(ext, "c") || !strcmp(ext, "h")) return &registry[0];
    if (!strcmp(ext, "js") || !strcmp(ext, "jsx") || !strcmp(ext, "mjs") ||
        !strcmp(ext, "cjs"))
        return &registry[1];
    if (!strcmp(ext, "ts") || !strcmp(ext, "mts") || !strcmp(ext, "cts"))
        return &registry[2];
    if (!strcmp(ext, "tsx")) return &registry[3];
    return NULL;
}
