/* test_font.c - terminal glyph atlas coverage. */
#include "test.h"
#include "font.h"

int main(void) {
    Font *font = font_load("/System/Library/Fonts/SFNSMono.ttf", 18.0f);
    CHECK(font != 0);

    float x = 0.0f, y = 0.0f;
    float x0, y0, x1, y1, s0, t0, s1, t1;
    CHECK(font_quad(font, 0x23F5, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));
    CHECK(font_quad(font, 0x26A0, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));
    CHECK(font_quad(font, 0x2733, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));
    CHECK(font_quad(font, 0x00BF, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));
    CHECK(font_quad(font, 0x00E9, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));
    CHECK(font_quad(font, 0x00F1, &x, &y, &x0, &y0, &x1, &y1, &s0, &t0, &s1, &t1));

    font_free(font);
    TEST_REPORT();
}
