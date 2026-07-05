/* font.h — a baked monospace glyph atlas via stb_truetype.
 *
 * Rasterizes once at load into a single 8-bit (R8) atlas the renderer uploads
 * as a texture. Per-glyph quad geometry is produced on demand from cached
 * metrics — no per-frame rasterization. */
#ifndef WAVE_FONT_H
#define WAVE_FONT_H

typedef struct Font Font;

/* Load a TTF/TTC at the given pixel height. Returns NULL on failure. */
Font *font_load(const char *path, float pixel_height);
void font_free(Font *f);

/* The baked atlas: an R8 bitmap of `*w` x `*h` bytes (alpha coverage). */
const unsigned char *font_atlas(const Font *f, int *w, int *h);

/* Vertical metrics in pixels. */
float font_line_height(const Font *f);
float font_ascent(const Font *f);

/* Horizontal advance of a monospace cell, in pixels. */
float font_advance(const Font *f);

/* Compute the screen quad + atlas UVs for `codepoint` at pen position
 * (*x, *y) where *y is the baseline. Advances *x by one cell. Returns 1 if a
 * visible glyph was produced, 0 for a blank/space (pen still advanced). */
int font_quad(const Font *f, int codepoint, float *x, float *y, float *x0,
              float *y0, float *x1, float *y1, float *s0, float *t0, float *s1,
              float *t1);

#endif /* WAVE_FONT_H */
