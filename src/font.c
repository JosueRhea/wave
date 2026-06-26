/* font.c — see font.h. */
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_truetype.h"

#define ATLAS_W 512
#define ATLAS_H 512
#define FIRST_CH 32
#define NUM_CH 95 /* 32..126 inclusive */

struct Font {
    unsigned char *ttf;
    stbtt_fontinfo info;
    unsigned char *atlas; /* ATLAS_W * ATLAS_H, R8 */
    stbtt_bakedchar chars[NUM_CH];
    float pixel_height;
    float scale;
    float ascent, descent, linegap; /* pixels */
    float advance;                  /* pixels, monospace */
};

static unsigned char *read_file(const char *path, long *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *buf = malloc(n > 0 ? n : 1);
    if (fread(buf, 1, n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = n;
    return buf;
}

Font *font_load(const char *path, float pixel_height) {
    long len;
    unsigned char *ttf = read_file(path, &len);
    if (!ttf) return NULL;

    Font *f = calloc(1, sizeof(Font));
    f->ttf = ttf;
    f->pixel_height = pixel_height;

    int off = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (off < 0 || !stbtt_InitFont(&f->info, ttf, off)) {
        free(ttf);
        free(f);
        return NULL;
    }

    f->scale = stbtt_ScaleForPixelHeight(&f->info, pixel_height);
    int a, d, g;
    stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
    f->ascent = a * f->scale;
    f->descent = d * f->scale;
    f->linegap = g * f->scale;

    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->info, 'M', &adv, &lsb);
    f->advance = adv * f->scale;

    f->atlas = calloc(ATLAS_W * ATLAS_H, 1);
    int r = stbtt_BakeFontBitmap(ttf, off, pixel_height, f->atlas, ATLAS_W,
                                 ATLAS_H, FIRST_CH, NUM_CH, f->chars);
    if (r == 0) { /* 0 rows used == failure */
        free(f->atlas);
        free(ttf);
        free(f);
        return NULL;
    }
    return f;
}

void font_free(Font *f) {
    if (!f) return;
    free(f->atlas);
    free(f->ttf);
    free(f);
}

const unsigned char *font_atlas(const Font *f, int *w, int *h) {
    if (w) *w = ATLAS_W;
    if (h) *h = ATLAS_H;
    return f->atlas;
}

float font_line_height(const Font *f) {
    return f->ascent - f->descent + f->linegap;
}
float font_ascent(const Font *f) { return f->ascent; }
float font_advance(const Font *f) { return f->advance; }

int font_quad(const Font *f, int codepoint, float *x, float *y, float *x0,
              float *y0, float *x1, float *y1, float *s0, float *t0, float *s1,
              float *t1) {
    if (codepoint < FIRST_CH || codepoint >= FIRST_CH + NUM_CH) {
        *x += f->advance; /* unknown glyph: advance, draw nothing */
        return 0;
    }
    stbtt_aligned_quad q;
    /* opengl_fillrule = 0: y-down coordinates, matching our top-left ortho. */
    stbtt_GetBakedQuad(f->chars, ATLAS_W, ATLAS_H, codepoint - FIRST_CH, x, y,
                       &q, 0);
    *x0 = q.x0; *y0 = q.y0; *x1 = q.x1; *y1 = q.y1;
    *s0 = q.s0; *t0 = q.t0; *s1 = q.s1; *t1 = q.t1;
    return codepoint != ' ';
}
