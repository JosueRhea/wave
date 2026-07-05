#include "../src/buffer.h"
#include "../src/highlight.h"
#include "../src/langs.h"
#include "test.h"

#include <string.h>

static void check_language_query(const char *path, const char *src) {
    const Language *lang = lang_detect(path);
    CHECK(lang != NULL);
    CHECK(lang->query != NULL);
    if (!lang || !lang->query) return;

    Buffer *buf = buffer_new();
    buffer_insert(buf, 0, src, strlen(src));
    Highlighter *hl = hl_new(buf, lang->lang, lang->query);
    CHECK(hl != NULL);
    if (hl) {
        hl_update(hl);
        HighlightSpan spans[128];
        CHECK(hl_spans(hl, 0, buffer_length(buf), spans, 128) > 0);
        hl_free(hl);
    }
    buffer_free(buf);
}

int main(void) {
    check_language_query("sample.c", "int main(void) { return 0; }\n");
    check_language_query("sample.js", "function main() { return 1; }\n");
    check_language_query("sample.ts", "type T = number;\nfunction main(): T { return 1; }\n");
    check_language_query("sample.tsx",
                         "type Props = {name: string};\n"
                         "function View(props: Props) { return <div>{props.name}</div>; }\n");

    TEST_REPORT();
}
