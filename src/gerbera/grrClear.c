/* grrClear.c - the render-side clear family (port of the decompiled
 * grrClear.inl) plus the flush side grFlushBufferClears (grApiFast.inl).
 *
 * Assert sites keep the original file/line strings. The fill dispatch goes
 * through the DLL tables (fillFunctions / off_479274): entries 0/1/3 are
 * the 1/2/4-byte fills, entry 2 is unreferenced (the decompile shows NULL,
 * and a 3-byte surface would hit the fillFunc assert).
 */
#include "gerbera/grState.h"
#include "gerbera/grUtil.h"
#include "gerbera/grRGBA.h"
#include "hg/hgInt32.h"
#include <string.h>

#define GR_GRR_CHECK(cond, file, line)           \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, (file), (line)) != 0) \
            grDie(0);                            \
    } while (0)

/* the draw surface doubles as the descriptor view (its first fields ARE the
 * descriptor - grGetDrawBufferDescriptor moves 4-byte chunks between them) */
#define GRR_DESC(surface) ((_grSurfaceDescriptor *)(surface))

/* --- fill dispatch tables (DLL 479264 / 479274) ---------------------------- */

typedef void (*grFill1Fn)(void *dst, unsigned int value, unsigned int mask, int size);
typedef void (*grFill4Fn)(void *dst, unsigned int a, unsigned int b, unsigned int c,
                          unsigned int d, unsigned int mask, int size);

/* the double cast through void(*)(void) mirrors the DLL's generic table slot
 * type without tripping -Wcast-function-type */
#define FILL1(fn) ((grFill1Fn)(void (*)(void))(fn))
#define FILL4(fn) ((grFill4Fn)(void (*)(void))(fn))

static const grFill1Fn fillFunctions1x[4] =
{
    FILL1(grFill8Masked), FILL1(grFill16Masked), NULL, FILL1(grFill32Masked)
};

static const grFill4Fn fillFunctions4x[4] =
{
    FILL4(grFill4x8), FILL4(grFill4x16), NULL, FILL4(grFill4x32)
};

/* --- grFlushBufferClears (grApiFast.inl 26; DLL 00430AE0) ------------------ */

void grFlushBufferClears(Gerbera_s *state)
{
    int i;

    GR_GRR_CHECK(state != NULL, "grApiFast.inl", 33);
    GR_GRR_CHECK(hgInRange32(state->clearInfoIndex, 0, GR_CLEAR_BUFFER_SIZE), "grApiFast.inl", 34);
    if (state->clearInfoIndex != 0)
    {
        grrBeginRendering(state);
        for (i = 0; i < state->clearInfoIndex; ++i)
            grrClearBuffers(&state->drawSurface, &state->clearInfos[i]);
        grrEndRendering(state);
        state->clearInfoIndex = 0;
    }
    GR_GRR_CHECK(state->clearInfoIndex == 0, "grApiFast.inl", 48);
}

/* --- grrClearBuffer (grrClear.inl 133; DLL 0043E3A0) ------------------------ */

static void grrClearBuffer(_grDrawSurface *surface, _grSurfaceBuffer *buffer,
                           _grClearInfo *info, int bytesPerPixel,
                           unsigned int fillValue, unsigned int fillMask)
{
    grFill1Fn fillFunc;
    unsigned char *ptr, *p;
    int width, height, stride;

    GR_GRR_CHECK(surface != NULL && buffer != NULL && info != NULL, "grrClear.inl", 141);
    GR_GRR_CHECK(hgIsPowerOfTwo32(bytesPerPixel) && hgInRange32(bytesPerPixel, 0, 4), "grrClear.inl", 142);
    if (buffer->data != NULL && bytesPerPixel != 0)
    {
        ptr = (unsigned char *)buffer->data;
        height = hgMin32(info->scissor.y1 - info->scissor.y0, surface->height);
        width = hgMin32(info->scissor.x1 - info->scissor.x0, surface->width);
        fillFunc = fillFunctions1x[bytesPerPixel - 1];
        stride = buffer->stride;
        GR_GRR_CHECK(fillFunc != NULL, "grrClear.inl", 153);
        if (grIsRectangleEmpty(&info->scissor) == 0)
        {
            p = &ptr[stride * info->scissor.y0 + bytesPerPixel * info->scissor.x0];
            if (stride == bytesPerPixel * width)
            {
                fillFunc(p, fillValue, fillMask, width * height);
            }
            else
            {
                while (height-- != 0)
                {
                    fillFunc(p, fillValue, fillMask, width);
                    p += stride;
                }
            }
        }
    }
}

/* --- grrClearDitheredColorBuffer (grrClear.inl 62; DLL 0043E220) ----------- */

void grrClearDitheredColorBuffer(_grDrawSurface *surface, _grClearInfo *info, unsigned int fillMask)
{
    grFill4Fn fillFunc;
    unsigned char *ptr;
    unsigned int c0, c1, c2, c3;
    int width, height, x0, y0, bpp;
    int i;

    GR_GRR_CHECK(surface != NULL && info != NULL, "grrClear.inl", 69);
    bpp = surface->colorBytes;
    GR_GRR_CHECK(hgIsPowerOfTwo32(bpp) && hgInRange32(bpp, 0, 4), "grrClear.inl", 72);
    if (surface->buffers[0].data != NULL && bpp != 0)
    {
        fillFunc = fillFunctions4x[bpp - 1];
        GR_GRR_CHECK(fillFunc != NULL, "grrClear.inl", 87);
        x0 = info->scissor.x0;
        y0 = info->scissor.y0;
        height = hgMin32(info->scissor.y1 - info->scissor.y0, surface->height);
        width = hgMin32(info->scissor.x1 - info->scissor.x0, surface->width);
        if (width != 0)
        {
            ptr = (unsigned char *)surface->buffers[0].data
                + surface->buffers[0].stride * y0 + bpp * x0;
            for (i = 0; i < height; ++i)
            {
                c0 = grDitherColor(info->clearColor, GRR_DESC(surface), x0, y0);
                c1 = grDitherColor(info->clearColor, GRR_DESC(surface), x0 + 1, y0);
                c2 = grDitherColor(info->clearColor, GRR_DESC(surface), x0 + 2, y0);
                c3 = grDitherColor(info->clearColor, GRR_DESC(surface), x0 + 3, y0);
                fillFunc(ptr, c0, c1, c2, c3, fillMask, width);
                ptr += surface->buffers[0].stride;
                ++y0;
            }
        }
    }
}

/* --- grrClearColorBuffer (grrClear.inl 180; DLL 0043E620) ------------------ */

void grrClearColorBuffer(_grDrawSurface *surface, _grClearInfo *info)
{
    unsigned char colorMask;
    unsigned int fillValue;
    int fillMask;

    GR_GRR_CHECK(surface != NULL && info != NULL, "grrClear.inl", 187);
    colorMask = info->colorMask;
    fillMask = -1;
    GR_GRR_CHECK((colorMask & 0xF0u) == 0, "grrClear.inl", 196);
    if (surface->colorMask != (unsigned char)(colorMask & surface->colorMask))
        fillMask = (int)grGetPackedColorWriteMask(GRR_DESC(surface), colorMask);
    fillValue = grPackColor(info->clearColor, GRR_DESC(surface));
    if (info->useDithering != 0 && info->clearColor != 0)
        grrClearDitheredColorBuffer(surface, info, (unsigned int)fillMask);
    else
        grrClearBuffer(surface, surface->buffers, info, surface->colorBytes,
                       fillValue, (unsigned int)fillMask);
}

/* --- grrClearDepthBuffer (grrClear.inl 215; DLL 0043E7C0) ------------------ */

void grrClearDepthBuffer(_grDrawSurface *surface, _grClearInfo *info)
{
    unsigned int clearValue;

    GR_GRR_CHECK(surface != NULL && info != NULL, "grrClear.inl", 222);
    if (info->depthMask != 0)
    {
        clearValue = (unsigned int)info->clearDepth;
        if (surface->depthBytes != 4)
            clearValue >>= 30 - 8 * surface->depthBytes;
        grrClearBuffer(surface, &surface->buffers[1], info, surface->depthBytes,
                       clearValue, 0xFFFFFFFFu);
    }
}

/* --- grrClearStencilBuffer (grrClear.inl 239; DLL 0043E8B0) ---------------- */

void grrClearStencilBuffer(_grDrawSurface *surface, _grClearInfo *info)
{
    GR_GRR_CHECK(surface != NULL && info != NULL, "grrClear.inl", 246);
    if (info->stencilWriteMask != 0)
        grrClearBuffer(surface, &surface->buffers[2], info, surface->stencilBytes,
                       info->clearStencil, info->stencilWriteMask);
}

/* --- grrClearBuffers (grrClear.inl 259; DLL 0043E980) ---------------------- */

void grrClearBuffers(_grDrawSurface *surface, _grClearInfo *info)
{
    GR_GRR_CHECK(surface != NULL && info != NULL, "grrClear.inl", 266);
    if ((info->bufferMask & 1) != 0)
        grrClearColorBuffer(surface, info);
    if ((info->bufferMask & 2) != 0)
        grrClearDepthBuffer(surface, info);
    if ((info->bufferMask & 4) != 0)
        grrClearStencilBuffer(surface, info);
}
