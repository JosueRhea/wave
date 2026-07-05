/* font.c — see font.h. */
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_truetype.h"

#define ATLAS_W 2048
#define ATLAS_H 2048
#define MAX_GLYPHS 1536
#define FACE_MAIN 0
#define FACE_SYMBOLS 1
#define FACE_UNICODE 2
#define FACE_COUNT 3

typedef struct {
    unsigned char *ttf;
    stbtt_fontinfo info;
    float scale;
    int loaded;
} FontFace;

typedef struct {
    int codepoint;
    int visible;
    float xoff, yoff, w, h;
    float s0, t0, s1, t1;
} FontGlyph;

struct Font {
    FontFace faces[FACE_COUNT];
    unsigned char *atlas; /* ATLAS_W * ATLAS_H, R8 */
    FontGlyph glyphs[MAX_GLYPHS];
    int nglyphs;
    int pen_x, pen_y, row_h;
    float pixel_height;
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

static int font_load_face(FontFace *face, const char *path, float pixel_height) {
    long len;
    unsigned char *ttf = read_file(path, &len);
    if (!ttf) return 0;
    int off = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (off < 0 || !stbtt_InitFont(&face->info, ttf, off)) {
        free(ttf);
        return 0;
    }
    face->ttf = ttf;
    face->scale = stbtt_ScaleForPixelHeight(&face->info, pixel_height);
    face->loaded = 1;
    return 1;
}

static void font_free_face(FontFace *face) {
    free(face->ttf);
    memset(face, 0, sizeof(*face));
}

static FontGlyph *font_find_glyph(const Font *f, int codepoint) {
    for (int i = 0; i < f->nglyphs; i++)
        if (f->glyphs[i].codepoint == codepoint)
            return (FontGlyph *)&f->glyphs[i];
    return NULL;
}

static int font_select_face(Font *f, int codepoint) {
    if (f->faces[FACE_MAIN].loaded &&
        stbtt_FindGlyphIndex(&f->faces[FACE_MAIN].info, codepoint))
        return FACE_MAIN;
    if (f->faces[FACE_SYMBOLS].loaded &&
        stbtt_FindGlyphIndex(&f->faces[FACE_SYMBOLS].info, codepoint))
        return FACE_SYMBOLS;
    if (f->faces[FACE_UNICODE].loaded &&
        stbtt_FindGlyphIndex(&f->faces[FACE_UNICODE].info, codepoint))
        return FACE_UNICODE;
    return -1;
}

static int font_push_blank(Font *f, int codepoint) {
    if (f->nglyphs >= MAX_GLYPHS) return 0;
    FontGlyph *g = &f->glyphs[f->nglyphs++];
    memset(g, 0, sizeof(*g));
    g->codepoint = codepoint;
    return 1;
}

static int font_pack_glyph(Font *f, int codepoint) {
    if (font_find_glyph(f, codepoint)) return 1;
    if (codepoint == 0x00A0) codepoint = ' ';
    if (codepoint == ' ' || codepoint == '\t' ||
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F))
        return font_push_blank(f, codepoint);

    int face_i = font_select_face(f, codepoint);
    if (face_i < 0) return 0;
    FontFace *face = &f->faces[face_i];

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&face->info, codepoint, face->scale,
                                face->scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0;
    int gh = y1 - y0;
    if (gw <= 0 || gh <= 0) return font_push_blank(f, codepoint);

    const int pad = 2;
    if (f->pen_x + gw + pad > ATLAS_W) {
        f->pen_x = pad;
        f->pen_y += f->row_h + pad;
        f->row_h = 0;
    }
    if (f->pen_y + gh + pad > ATLAS_H || f->nglyphs >= MAX_GLYPHS)
        return 0;

    unsigned char *dst = f->atlas + f->pen_y * ATLAS_W + f->pen_x;
    stbtt_MakeCodepointBitmap(&face->info, dst, gw, gh, ATLAS_W,
                              face->scale, face->scale, codepoint);

    FontGlyph *g = &f->glyphs[f->nglyphs++];
    g->codepoint = codepoint;
    g->visible = 1;
    g->xoff = (float)x0;
    g->yoff = (float)y0;
    g->w = (float)gw;
    g->h = (float)gh;
    g->s0 = (float)f->pen_x / (float)ATLAS_W;
    g->t0 = (float)f->pen_y / (float)ATLAS_H;
    g->s1 = (float)(f->pen_x + gw) / (float)ATLAS_W;
    g->t1 = (float)(f->pen_y + gh) / (float)ATLAS_H;

    f->pen_x += gw + pad;
    if (gh > f->row_h) f->row_h = gh;
    return 1;
}

static void font_pack_range(Font *f, int first, int last) {
    for (int cp = first; cp <= last; cp++)
        (void)font_pack_glyph(f, cp);
}

static void font_pack_list(Font *f, const int *codepoints, size_t n) {
    for (size_t i = 0; i < n; i++)
        (void)font_pack_glyph(f, codepoints[i]);
}

static void font_pack_terminal_glyphs(Font *f) {
    static const int misc[] = {
        0x00A0, 0x00B7, 0x2022, 0x2026, 0x2039, 0x203A, 0x2190, 0x2191, 0x2192,
        0x2193, 0x21B5, 0x2318, 0x2325, 0x23CE, 0x25A0, 0x25A1,
        0x23F5, 0x23F8, 0x23F9, 0x23FA, 0x25B6, 0x25C0, 0x25CB,
        0x25CF, 0x25D0, 0x25D1, 0x25D2,
        0x25D3, 0x2605, 0x2606, 0x26A0, 0x2713, 0x2714, 0x2715,
        0x2717, 0x2726, 0x2731, 0x2733, 0x2734, 0x2736, 0x273B,
        0x273D, 0x274B, 0x276F, 0x27E9, 0x2139, 0x2387
    };
    font_pack_range(f, 32, 126);
    font_pack_range(f, 0x00A0, 0x00FF); /* Latin-1 supplement */
    font_pack_range(f, 0x2300, 0x23FF); /* technical symbols */
    font_pack_range(f, 0x2500, 0x257F); /* box drawing */
    font_pack_range(f, 0x2580, 0x259F); /* block elements */
    font_pack_range(f, 0x2800, 0x28FF); /* braille spinners */
    font_pack_list(f, misc, sizeof(misc) / sizeof(misc[0]));
}

Font *font_load(const char *path, float pixel_height) {
    Font *f = calloc(1, sizeof(Font));
    if (!f) return NULL;
    f->pixel_height = pixel_height;
    f->pen_x = 2;
    f->pen_y = 2;

    if (!font_load_face(&f->faces[FACE_MAIN], path, pixel_height)) {
        free(f);
        return NULL;
    }
    (void)font_load_face(&f->faces[FACE_SYMBOLS],
                         "/System/Library/Fonts/Apple Symbols.ttf",
                         pixel_height);
    (void)font_load_face(&f->faces[FACE_UNICODE],
                         "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                         pixel_height);

    int a, d, g;
    stbtt_GetFontVMetrics(&f->faces[FACE_MAIN].info, &a, &d, &g);
    f->ascent = a * f->faces[FACE_MAIN].scale;
    f->descent = d * f->faces[FACE_MAIN].scale;
    f->linegap = g * f->faces[FACE_MAIN].scale;

    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f->faces[FACE_MAIN].info, 'M', &adv, &lsb);
    f->advance = adv * f->faces[FACE_MAIN].scale;

    f->atlas = calloc(ATLAS_W * ATLAS_H, 1);
    if (!f->atlas) {
        font_free(f);
        return NULL;
    }
    font_pack_terminal_glyphs(f);
    return f;
}

void font_free(Font *f) {
    if (!f) return;
    free(f->atlas);
    for (int i = 0; i < FACE_COUNT; i++)
        font_free_face(&f->faces[i]);
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
    if (codepoint == 0x00A0) codepoint = ' ';
    if (codepoint >= 0xFE00 && codepoint <= 0xFE0F) return 0;
    const FontGlyph *g = font_find_glyph(f, codepoint);
    if (!g && codepoint != '?') g = font_find_glyph(f, '?');
    float start_x = *x;
    *x += f->advance;
    if (!g || !g->visible) return 0;

    *x0 = start_x + g->xoff;
    *y0 = *y + g->yoff;
    *x1 = *x0 + g->w;
    *y1 = *y + g->yoff + g->h;
    *s0 = g->s0; *t0 = g->t0; *s1 = g->s1; *t1 = g->t1;
    return 1;
}
