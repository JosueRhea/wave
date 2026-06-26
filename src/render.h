/* render.h — a tiny batched OpenGL renderer for text + solid rects.
 *
 * Everything drawn in a frame is accumulated into one vertex buffer and issued
 * as a single glDrawArrays call on flush. Glyphs sample the font atlas; solid
 * rects (cursor, underlines, gutter) reuse the same shader via negative UVs.
 *
 * Pixel coordinate space: origin top-left, +x right, +y down. */
#ifndef WAVE_RENDER_H
#define WAVE_RENDER_H

#include "font.h"

typedef struct Renderer Renderer;

/* Create the renderer; uploads the font atlas as a texture. Requires a current
 * OpenGL 3.3 core context. Returns NULL on shader/link failure. */
Renderer *renderer_new(const Font *font);
void renderer_free(Renderer *r);

/* Start a frame: set the viewport to `w`x`h` and clear to (cr,cg,cb,ca). The
 * clear alpha < 1 makes the window translucent (needs a transparent
 * framebuffer). */
void renderer_begin(Renderer *r, int w, int h, float cr, float cg, float cb,
                    float ca);

/* Queue a textured glyph quad (screen rect + atlas UVs) with an RGBA tint. */
void renderer_glyph(Renderer *r, float x0, float y0, float x1, float y1,
                    float s0, float t0, float s1, float t1, float cr, float cg,
                    float cb, float ca);

/* Queue a solid-color rectangle. */
void renderer_rect(Renderer *r, float x, float y, float w, float h, float cr,
                   float cg, float cb, float ca);

/* Draw everything queued this frame in one call and reset the batch. */
void renderer_flush(Renderer *r);

/* Read the current framebuffer back and write it as a binary PPM (P6) — used
 * for headless verification of what was rendered. Returns 0 on success. */
int renderer_save_ppm(const char *path, int w, int h);

#endif /* WAVE_RENDER_H */
