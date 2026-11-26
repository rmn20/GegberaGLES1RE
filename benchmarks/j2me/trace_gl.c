/* GLES command capture with draw-time copies of client-memory arrays. */
#include <GLES/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const void *pointer;
    GLint components;
    GLenum type;
    GLsizei stride;
    unsigned id;
    int enabled;
} ClientArray;

static FILE *events, *payload;
static unsigned frame, next_array_id, client_unit;
static int target_transfer;
static GLint unpack_alignment = 4, pack_alignment = 4;
static ClientArray vertex, normal, color, texcoord[2];

void bench_trace_source(const char *file)
{
    const char *name = file;
    for (const char *p = file; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    /* This unit uploads the memory target's backing image and reads it back. */
    target_transfer = strcmp(name, "m3g_rendercontext.inl") == 0;
}

static void fail(const char *message)
{
    fprintf(stderr, "GLES capture: %s\n", message);
    exit(3);
}

static void initialize(void)
{
    if (events) return;
    const char *event_name = getenv("BENCH_TRACE_EVENTS");
    const char *payload_name = getenv("BENCH_TRACE_PAYLOAD");
    if (!event_name || !payload_name) fail("set BENCH_TRACE_EVENTS and BENCH_TRACE_PAYLOAD");
    events = fopen(event_name, "wb");
    payload = fopen(payload_name, "wb");
    if (!events || !payload) fail("cannot open trace output");
}

static uint32_t float_word(float value)
{
    uint32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

static long write_blob(const void *data, size_t size)
{
    long offset = ftell(payload);
    if (offset < 0 || fwrite(data, 1, size, payload) != size) fail("cannot write payload");
    return offset;
}

static void begin_event(const char *name, size_t count, const uint32_t *args)
{
    initialize();
    fprintf(events, "{\"fn\":\"%s\",\"frame\":%u,\"args\":[", name, frame);
    for (size_t i = 0; i < count; ++i) fprintf(events, "%s%u", i ? "," : "", args[i]);
    fputs("]", events);
    if (target_transfer && name[0] == 'g' && name[1] == 'l')
        fputs(",\"stage\":\"target-transfer\"", events);
}

static void end_event(void)
{
    if (fputs("}\n", events) < 0) fail("cannot write events");
}

static void record(const char *name, size_t count, const uint32_t *args,
                   int blob_arg, const void *data, size_t size)
{
    begin_event(name, count, args);
    if (data && size) fprintf(events, ",\"blob\":[%d,%ld,%zu]", blob_arg, write_blob(data, size), size);
    end_event();
}

void bench_trace_mark(void)
{
    record("capture", 0, NULL, -1, NULL, 0);
}

void bench_trace_frame_end(void)
{
    record("frame_end", 0, NULL, -1, NULL, 0);
    if (fflush(events) || fflush(payload)) fail("cannot flush trace");
    ++frame;
}

static size_t scalar_size(GLenum type)
{
    switch (type) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_FIXED: case GL_FLOAT: return 4;
    default: fail("unsupported array scalar type"); return 0;
    }
}

static size_t pixel_size(GLsizei width, GLsizei height, GLenum format, GLenum type, GLint alignment)
{
    if (width <= 0 || height <= 0) return 0;
    size_t components;
    switch (format) {
    case GL_ALPHA: case GL_LUMINANCE: components = 1; break;
    case GL_LUMINANCE_ALPHA: components = 2; break;
    case GL_RGB: components = 3; break;
    case GL_RGBA: components = 4; break;
    default: fail("unsupported pixel format"); return 0;
    }
    size_t bytes;
    switch (type) {
    case GL_UNSIGNED_BYTE: bytes = components; break;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_4_4_4_4:
    case GL_UNSIGNED_SHORT_5_5_5_1: bytes = 2; break;
    default: fail("unsupported pixel type"); return 0;
    }
    size_t row = (size_t)width * bytes;
    size_t stride = (row + alignment - 1) & ~(size_t)(alignment - 1);
    return (size_t)(height - 1) * stride + row;
}

static size_t texture_size(GLsizei width, GLsizei height, GLenum format, GLenum type)
{
    return pixel_size(width, height, format, type, unpack_alignment);
}

void trace_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    uint32_t args[] = {(uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height, format, type, 0};
    begin_event("glReadPixels", 7, args);
    fprintf(events, ",\"output\":[6,%zu]", pixel_size(width, height, format, type, pack_alignment));
    end_event();
    glReadPixels(x, y, width, height, format, type, pixels);
}

static ClientArray *array_for(GLenum token)
{
    switch (token) {
    case GL_VERTEX_ARRAY: return &vertex;
    case GL_NORMAL_ARRAY: return &normal;
    case GL_COLOR_ARRAY: return &color;
    case GL_TEXTURE_COORD_ARRAY: return &texcoord[client_unit];
    default: fail("unsupported client array"); return NULL;
    }
}

static void array_pointer(const char *name, ClientArray *array, GLint components,
                          GLenum type, GLsizei stride, const void *pointer)
{
    if (components < 1 || components > 4 || stride < 0) fail("invalid client array layout");
    array->pointer = pointer;
    array->components = components;
    array->type = type;
    array->stride = stride;
    array->id = ++next_array_id;
    uint32_t args[] = {(uint32_t)components, type, (uint32_t)stride, 0};
    begin_event(name, array == &normal ? 3 : 4, args + (array == &normal));
    fprintf(events, ",\"array_id\":%u", array->id);
    end_event();
}

static void snapshot_arrays(size_t vertices)
{
    ClientArray *arrays[] = {&vertex, &normal, &color, &texcoord[0], &texcoord[1]};
    int comma = 0;
    fputs(",\"arrays\":[", events);
    for (size_t a = 0; a < sizeof(arrays) / sizeof(arrays[0]); ++a) {
        ClientArray *array = arrays[a];
        if (!array->enabled || !vertices) continue;
        if (!array->pointer || !array->id) fail("enabled client array has no pointer");
        size_t element = scalar_size(array->type) * (size_t)array->components;
        size_t stride = array->stride ? (size_t)array->stride : element;
        if (vertices > 1000000 || stride > 4096) fail("client array exceeds capture limits");
        size_t size = (vertices - 1) * stride + element;
        unsigned char *copy = calloc(1, size);
        if (!copy) fail("cannot allocate client array snapshot");
        /* Preserve the original stride without storing unused padding bytes. */
        for (size_t i = 0; i < vertices; ++i)
            memcpy(copy + i * stride, (const unsigned char *)array->pointer + i * stride, element);
        fprintf(events, "%s[%u,%ld,%zu]", comma ? "," : "", array->id, write_blob(copy, size), size);
        comma = 1;
        free(copy);
    }
    fputs("]", events);
}

void trace_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    array_pointer("glVertexPointer", &vertex, size, type, stride, pointer);
    glVertexPointer(size, type, stride, pointer);
}

void trace_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    array_pointer("glNormalPointer", &normal, 3, type, stride, pointer);
    glNormalPointer(type, stride, pointer);
}

void trace_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    array_pointer("glColorPointer", &color, size, type, stride, pointer);
    glColorPointer(size, type, stride, pointer);
}

void trace_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    array_pointer("glTexCoordPointer", &texcoord[client_unit], size, type, stride, pointer);
    glTexCoordPointer(size, type, stride, pointer);
}

void trace_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (first < 0 || count < 0) fail("invalid draw range");
    uint32_t args[] = {mode, (uint32_t)first, (uint32_t)count};
    begin_event("glDrawArrays", 3, args);
    snapshot_arrays(count ? (size_t)first + count : 0);
    end_event();
    glDrawArrays(mode, first, count);
}

void trace_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (count < 0 || (count && !indices)) fail("invalid index array");
    size_t vertices = 0;
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT) fail("unsupported index type");
    size_t step = scalar_size(type);
    for (GLsizei i = 0; i < count; ++i) {
        unsigned index;
        if (step == 1) index = ((const unsigned char *)indices)[i];
        else { uint16_t value; memcpy(&value, (const unsigned char *)indices + 2 * i, 2); index = value; }
        if (vertices <= index) vertices = index + 1;
    }
    uint32_t args[] = {mode, (uint32_t)count, type, 0};
    begin_event("glDrawElements", 4, args);
    snapshot_arrays(vertices);
    if (count) fprintf(events, ",\"blob\":[3,%ld,%zu]", write_blob(indices, count * step), count * step);
    end_event();
    glDrawElements(mode, count, type, indices);
}

void trace_glGenTextures(GLsizei n, GLuint *textures)
{
    glGenTextures(n, textures);
    uint32_t args[] = {(uint32_t)n, 0};
    begin_event("glGenTextures", 2, args);
    fputs(",\"generated\":[", events);
    for (GLsizei i = 0; i < n; ++i) fprintf(events, "%s%u", i ? "," : "", textures[i]);
    fputs("]", events);
    end_event();
}

#define TRACE(name, signature, call, ...) \
    void trace_##name signature { \
        uint32_t args[] = {__VA_ARGS__}; \
        record(#name, sizeof(args) / sizeof(args[0]), args, -1, NULL, 0); \
        name call; \
    }
#define TRACE0(name) \
    void trace_##name(void) { record(#name, 0, NULL, -1, NULL, 0); name(); }

TRACE(glAlphaFunc, (GLenum func, GLclampf ref),
      (func, ref), (uint32_t)func, float_word(ref))

TRACE(glColor4f, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha),
      (red, green, blue, alpha), float_word(red), float_word(green), float_word(blue), float_word(alpha))

TRACE(glDepthRangef, (GLclampf zNear, GLclampf zFar),
      (zNear, zFar), float_word(zNear), float_word(zFar))

TRACE(glFogf, (GLenum pname, GLfloat param),
      (pname, param), (uint32_t)pname, float_word(param))

TRACE(glLightModelf, (GLenum pname, GLfloat param),
      (pname, param), (uint32_t)pname, float_word(param))

void trace_glLightModelfv(GLenum pname, const GLfloat *params)
{
    uint32_t args[] = {(uint32_t)pname, 0};
    record("glLightModelfv", 2, args, 1, params, pname == GL_LIGHT_MODEL_AMBIENT ? 16 : 4);
    glLightModelfv(pname, params);
}

TRACE(glLightf, (GLenum light, GLenum pname, GLfloat param),
      (light, pname, param), (uint32_t)light, (uint32_t)pname, float_word(param))

void trace_glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    uint32_t args[] = {(uint32_t)light, (uint32_t)pname, 0};
    record("glLightfv", 3, args, 2, params, pname == GL_SPOT_DIRECTION ? 12 : (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION ? 16 : 4));
    glLightfv(light, pname, params);
}

void trace_glLoadMatrixf(const GLfloat *m)
{
    uint32_t args[] = {0};
    record("glLoadMatrixf", 1, args, 0, m, 64);
    glLoadMatrixf(m);
}

TRACE(glMaterialf, (GLenum face, GLenum pname, GLfloat param),
      (face, pname, param), (uint32_t)face, (uint32_t)pname, float_word(param))

void trace_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    uint32_t args[] = {(uint32_t)face, (uint32_t)pname, 0};
    record("glMaterialfv", 3, args, 2, params, pname == GL_SHININESS ? 4 : 16);
    glMaterialfv(face, pname, params);
}

void trace_glMultMatrixf(const GLfloat *m)
{
    uint32_t args[] = {0};
    record("glMultMatrixf", 1, args, 0, m, 64);
    glMultMatrixf(m);
}

TRACE(glPolygonOffset, (GLfloat factor, GLfloat units),
      (factor, units), float_word(factor), float_word(units))

TRACE(glScalef, (GLfloat x, GLfloat y, GLfloat z),
      (x, y, z), float_word(x), float_word(y), float_word(z))

void trace_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    uint32_t args[] = {(uint32_t)target, (uint32_t)pname, 0};
    record("glTexEnvfv", 3, args, 2, params, pname == GL_TEXTURE_ENV_COLOR ? 16 : 4);
    glTexEnvfv(target, pname, params);
}

TRACE(glTranslatef, (GLfloat x, GLfloat y, GLfloat z),
      (x, y, z), float_word(x), float_word(y), float_word(z))

TRACE(glActiveTexture, (GLenum texture),
      (texture), (uint32_t)texture)

TRACE(glBindTexture, (GLenum target, GLuint texture),
      (target, texture), (uint32_t)target, (uint32_t)texture)

TRACE(glBlendFunc, (GLenum sfactor, GLenum dfactor),
      (sfactor, dfactor), (uint32_t)sfactor, (uint32_t)dfactor)

TRACE(glClear, (GLbitfield mask),
      (mask), (uint32_t)mask)

TRACE(glClearColorx, (GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha),
      (red, green, blue, alpha), (uint32_t)red, (uint32_t)green, (uint32_t)blue, (uint32_t)alpha)

TRACE(glClearDepthx, (GLclampx depth),
      (depth), (uint32_t)depth)

void trace_glClientActiveTexture(GLenum texture)
{
    if (texture < GL_TEXTURE0 || texture > GL_TEXTURE1) fail("unsupported texture unit");
    client_unit = texture - GL_TEXTURE0;
    uint32_t args[] = {(uint32_t)texture};
    record("glClientActiveTexture", 1, args, -1, NULL, 0);
    glClientActiveTexture(texture);
}

TRACE(glColor4x, (GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha),
      (red, green, blue, alpha), (uint32_t)red, (uint32_t)green, (uint32_t)blue, (uint32_t)alpha)

TRACE(glColorMask, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha),
      (red, green, blue, alpha), (uint32_t)red, (uint32_t)green, (uint32_t)blue, (uint32_t)alpha)

TRACE(glCopyTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border),
      (target, level, internalformat, x, y, width, height, border), (uint32_t)target, (uint32_t)level, (uint32_t)internalformat, (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height, (uint32_t)border)

TRACE(glCullFace, (GLenum mode),
      (mode), (uint32_t)mode)

void trace_glDeleteTextures(GLsizei n, const GLuint *textures)
{
    uint32_t args[] = {(uint32_t)n, 0};
    record("glDeleteTextures", 2, args, 1, textures, n > 0 ? (size_t)n * 4 : 0);
    glDeleteTextures(n, textures);
}

TRACE(glDepthFunc, (GLenum func),
      (func), (uint32_t)func)

TRACE(glDepthMask, (GLboolean flag),
      (flag), (uint32_t)flag)

TRACE(glDisable, (GLenum cap),
      (cap), (uint32_t)cap)

void trace_glDisableClientState(GLenum array)
{
    array_for(array)->enabled = 0;
    uint32_t args[] = {(uint32_t)array};
    record("glDisableClientState", 1, args, -1, NULL, 0);
    glDisableClientState(array);
}

TRACE(glEnable, (GLenum cap),
      (cap), (uint32_t)cap)

void trace_glEnableClientState(GLenum array)
{
    array_for(array)->enabled = 1;
    uint32_t args[] = {(uint32_t)array};
    record("glEnableClientState", 1, args, -1, NULL, 0);
    glEnableClientState(array);
}

TRACE0(glFinish)

void trace_glFogxv(GLenum pname, const GLfixed *params)
{
    uint32_t args[] = {(uint32_t)pname, 0};
    record("glFogxv", 2, args, 1, params, pname == GL_FOG_COLOR ? 16 : 4);
    glFogxv(pname, params);
}

TRACE(glFrontFace, (GLenum mode),
      (mode), (uint32_t)mode)

TRACE(glHint, (GLenum target, GLenum mode),
      (target, mode), (uint32_t)target, (uint32_t)mode)

TRACE0(glLoadIdentity)

TRACE(glMatrixMode, (GLenum mode),
      (mode), (uint32_t)mode)

TRACE(glOrthox, (GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar),
      (left, right, bottom, top, zNear, zFar), (uint32_t)left, (uint32_t)right, (uint32_t)bottom, (uint32_t)top, (uint32_t)zNear, (uint32_t)zFar)

void trace_glPixelStorei(GLenum pname, GLint param)
{
    if (pname == GL_UNPACK_ALIGNMENT || pname == GL_PACK_ALIGNMENT) {
        if (param != 1 && param != 2 && param != 4 && param != 8) fail("invalid pixel alignment");
        if (pname == GL_UNPACK_ALIGNMENT) unpack_alignment = param;
        else pack_alignment = param;
    }
    uint32_t args[] = {(uint32_t)pname, (uint32_t)param};
    record("glPixelStorei", 2, args, -1, NULL, 0);
    glPixelStorei(pname, param);
}

TRACE0(glPopMatrix)

TRACE0(glPushMatrix)

TRACE(glScissor, (GLint x, GLint y, GLsizei width, GLsizei height),
      (x, y, width, height), (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height)

TRACE(glShadeModel, (GLenum mode),
      (mode), (uint32_t)mode)

TRACE(glTexEnvx, (GLenum target, GLenum pname, GLfixed param),
      (target, pname, param), (uint32_t)target, (uint32_t)pname, (uint32_t)param)

void trace_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    uint32_t args[] = {(uint32_t)target, (uint32_t)level, (uint32_t)internalformat, (uint32_t)width, (uint32_t)height, (uint32_t)border, (uint32_t)format, (uint32_t)type, 0};
    record("glTexImage2D", 9, args, 8, pixels, texture_size(width, height, format, type));
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

TRACE(glTexParameterx, (GLenum target, GLenum pname, GLfixed param),
      (target, pname, param), (uint32_t)target, (uint32_t)pname, (uint32_t)param)

TRACE(glViewport, (GLint x, GLint y, GLsizei width, GLsizei height),
      (x, y, width, height), (uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height)

void trace_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data)
{
    uint32_t args[] = {target, (uint32_t)level, internalformat, (uint32_t)width, (uint32_t)height, (uint32_t)border, (uint32_t)imageSize, 0};
    record("glCompressedTexImage2D", 8, args, 7, data, imageSize > 0 ? (size_t)imageSize : 0);
    glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

TRACE(glTexParameteri, (GLenum target, GLenum pname, GLint param),
      (target, pname, param), target, pname, (uint32_t)param)

void trace_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    uint32_t args[] = {target, (uint32_t)level, (uint32_t)xoffset, (uint32_t)yoffset, (uint32_t)width, (uint32_t)height, format, type, 0};
    record("glTexSubImage2D", 9, args, 8, pixels, texture_size(width, height, format, type));
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}
