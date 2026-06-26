/* render.c — see render.h. */
#define GL_SILENCE_DEPRECATION
#include "render.h"

#include <OpenGL/gl3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    float x, y, u, v, r, g, b, a;
} Vertex;

struct Renderer {
    GLuint prog, vao, vbo, atlas_tex;
    GLint u_screen, u_atlas;
    Vertex *verts;
    size_t count, cap;
    int screen_w, screen_h;
};

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform vec2 uScreen;\n"
    "out vec2 vUV; out vec4 vColor;\n"
    "void main(){\n"
    "  float x = aPos.x / uScreen.x * 2.0 - 1.0;\n"
    "  float y = 1.0 - aPos.y / uScreen.y * 2.0;\n"
    "  gl_Position = vec4(x, y, 0.0, 1.0);\n"
    "  vUV = aUV; vColor = aColor;\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec2 vUV; in vec4 vColor;\n"
    "uniform sampler2D uAtlas;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  float a = (vUV.x < 0.0) ? 1.0 : texture(uAtlas, vUV).r;\n"
    "  frag = vec4(vColor.rgb, vColor.a * a);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(void) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "program link failed: %s\n", log);
        return 0;
    }
    return p;
}

Renderer *renderer_new(const Font *font) {
    Renderer *r = calloc(1, sizeof(Renderer));
    r->prog = link_program();
    if (!r->prog) {
        free(r);
        return NULL;
    }
    r->u_screen = glGetUniformLocation(r->prog, "uScreen");
    r->u_atlas = glGetUniformLocation(r->prog, "uAtlas");

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, r));

    /* upload the R8 font atlas */
    int aw, ah;
    const unsigned char *pixels = font_atlas(font, &aw, &ah);
    glGenTextures(1, &r->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE,
                 pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return r;
}

void renderer_free(Renderer *r) {
    if (!r) return;
    glDeleteTextures(1, &r->atlas_tex);
    glDeleteBuffers(1, &r->vbo);
    glDeleteVertexArrays(1, &r->vao);
    glDeleteProgram(r->prog);
    free(r->verts);
    free(r);
}

void renderer_begin(Renderer *r, int w, int h, float cr, float cg, float cb,
                    float ca) {
    r->screen_w = w;
    r->screen_h = h;
    r->count = 0;
    glViewport(0, 0, w, h);
    glClearColor(cr, cg, cb, ca);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void push_vertex(Renderer *r, float x, float y, float u, float v,
                        float cr, float cg, float cb, float ca) {
    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 4096;
        r->verts = realloc(r->verts, r->cap * sizeof(Vertex));
    }
    r->verts[r->count++] = (Vertex){x, y, u, v, cr, cg, cb, ca};
}

/* Two triangles for the rect (x0,y0)-(x1,y1) with UVs (s0,t0)-(s1,t1). */
static void push_quad(Renderer *r, float x0, float y0, float x1, float y1,
                      float s0, float t0, float s1, float t1, float cr,
                      float cg, float cb, float ca) {
    push_vertex(r, x0, y0, s0, t0, cr, cg, cb, ca);
    push_vertex(r, x1, y0, s1, t0, cr, cg, cb, ca);
    push_vertex(r, x1, y1, s1, t1, cr, cg, cb, ca);
    push_vertex(r, x0, y0, s0, t0, cr, cg, cb, ca);
    push_vertex(r, x1, y1, s1, t1, cr, cg, cb, ca);
    push_vertex(r, x0, y1, s0, t1, cr, cg, cb, ca);
}

void renderer_glyph(Renderer *r, float x0, float y0, float x1, float y1,
                    float s0, float t0, float s1, float t1, float cr, float cg,
                    float cb, float ca) {
    push_quad(r, x0, y0, x1, y1, s0, t0, s1, t1, cr, cg, cb, ca);
}

void renderer_rect(Renderer *r, float x, float y, float w, float h, float cr,
                   float cg, float cb, float ca) {
    /* negative UVs flag "solid" in the fragment shader */
    push_quad(r, x, y, x + w, y + h, -1.0f, -1.0f, -1.0f, -1.0f, cr, cg, cb, ca);
}

void renderer_flush(Renderer *r) {
    if (r->count == 0) return;
    glUseProgram(r->prog);
    glUniform2f(r->u_screen, (float)r->screen_w, (float)r->screen_h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas_tex);
    glUniform1i(r->u_atlas, 0);

    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, r->count * sizeof(Vertex), r->verts,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)r->count);
    r->count = 0;
}

int renderer_save_ppm(const char *path, int w, int h) {
    unsigned char *px = malloc((size_t)w * h * 3);
    if (!px) return -1;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(px); return -1; }
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    /* glReadPixels origin is bottom-left; flip vertically to top-left. */
    for (int y = h - 1; y >= 0; y--)
        fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, fp);
    fclose(fp);
    free(px);
    return 0;
}
