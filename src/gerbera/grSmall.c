/* grSmall.c - core queries, gradient helpers, and triangle filling. */
#include "gerbera/grState.h"
#include "gerbera/grDefs.h"
#include "hg/hgInt32.h"
#include <stdint.h>
#ifdef __SYMBIAN32__
#include "symbian_os.h"
#endif

/* grIsAligned - verbatim (docs/libGLES_CM.dll.c:13072-13075, 3 TU copies) */
int grIsAligned(const void *ptr, int alignment)
{
    return ((uintptr_t)(alignment - 1) & (uintptr_t)ptr) == 0;
}

/* grIsValidBool - verbatim (docs/libGLES_CM.dll.c:9187-9195, 2 TU copies) */
int grIsValidBool(int v)
{
    if (v == 1)
        return 1;
    return v == 0;
}

/* --- grComputeGradientReference (grSmall.c 153; DLL 25334-25342) ----------- */

int grComputeGradientReference(int v0sx, int v0sy, int xAdd, int yAdd)
{
    HGint64 v4 = hgMul64(v0sx, xAdd);
    HGint64 v5 = hgMadd64(v4, v0sy, yAdd);

    return (int)(0u - (unsigned int)hgLsl64h_0_31(v5, 20));
}

/* --- grComputeDepthOffset (grSmall.c 165; DLL 25902-25940) ------------------
 * Adjusts the depth-gradient reference for polygon offset: units scale with
 * the depth-buffer format (byte at drawSurface+10 = depthBytes), factor
 * scales with the steeper of the two depth adders. */
void grComputeDepthOffset(Gerbera_s *state)
{
    _grPixelPipe *pixelPipeline = (_grPixelPipe *)state->pixelPipeline;
    int units = state->depthOffsetUnits;
    unsigned int ref = (unsigned int)units + (unsigned int)pixelPipeline->reference[0];
    int factor = state->depthOffsetFactor;

    switch (((unsigned char *)&state->drawSurface)[10])   /* depthBytes */
    {
        case 1:
            ref += (unsigned int)units << 8;
            break;
        case 2:
            ref += (unsigned int)units;
            break;
        case 4:
            ref += (unsigned int)(units >> 16);
            break;
        default:
            break;
    }
    if (factor != 0)
    {
        int xAdder = hgAbs32(pixelPipeline->xAdder[0]);
        int yAdder = hgAbs32(pixelPipeline->yAdder[0]);
        int v1 = hgMax32(xAdder, yAdder);
        HGint64 v2 = hgMul64(factor, v1);

        ref += (unsigned int)hgLsl64h(v2, 16);
    }
    pixelPipeline->reference[0] = (int)ref;
}

/* --- g_mutableParamsMask (data.fragments.c 33) -------------------------------
 * Identifies mutable slots among the 78 pixel-pipe parameters passed to
 * HC_compile. Also selects words in the debug poison scratch array. */

const unsigned char g_mutableParamsMask[12] =
{
    0xFE, 0xFF, 0xFE, 0xFF, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00
};

/* A row is hidden when its nearest interpolated depth is behind every
 * stored sample. Validate the endpoint range before using that bound. */
#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6 && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
typedef char gr_check_depth_stride_[offsetof(_grPixelPipe, checked.depthBufferStride) == 136 ? 1 : -1];
typedef char gr_check_depth_row_[offsetof(_grPixelPipe, depthRow) == 172 ? 1 : -1];
typedef char gr_check_depth_n_[offsetof(_grPixelPipe, numScanlines) == 180 ? 1 : -1];
typedef char gr_check_depth_scan_[offsetof(_grPixelPipe, scanlineInfo) == 184 ? 1 : -1];
typedef char gr_check_depth_xadd_[offsetof(_grPixelPipe, xAdder) == 232 ? 1 : -1];
typedef char gr_check_depth_yadd_[offsetof(_grPixelPipe, yAdder) == 272 ? 1 : -1];

__attribute__((noinline, naked))
int grDepthSpansOccludedFastRef(const _grPixelPipe *pipe, int strictLess, unsigned int reference)
{
    __asm__ volatile(
        "push    {r4-r11, lr}\n\t"
        "ldr     r7, [r0, #180]\n\t"      /* n = pipe->numScanlines */
        "ldr     r3, [r0, #172]\n\t"      /* row = pipe->depthRow */
        "ldr     r4, [r0, #232]\n\t"      /* dx = pipe->xAdder[0] */
        "ldr     r5, [r0, #272]\n\t"      /* yAdd = pipe->yAdder[0] */
        "ldr     r6, [r0, #136]\n\t"      /* stride = depthBufferStride */
        "ldr     r0, [r0, #184]\n\t"      /* scan = scanlineInfo */
    "1:\n\t"
        "ldr     r8, [r0], #4\n\t"        /* span = *scan++ */
        "lsrs    r9, r8, #16\n\t"         /* count = span >> 16 */
        "beq     8f\n\t"
        "uxth    r8, r8\n\t"              /* x = span & 0xffff */
        "mla     r10, r4, r8, r2\n\t"     /* first = reference + dx * x */
        "add     r8, r3, r8, lsl #1\n\t"  /* depth = row + 2 * x */
        "ldrh    r12, [r8], #2\n\t"       /* d0 = *depth++ (overlaps cmp/add) */
        "cmp     r10, #0x40000000\n\t"    /* first > 0x3FFFFFFF ? */
        "bhs     9f\n\t"
        "add     r11, r1, r10, lsr #14\n\t" /* fBound = (first >> 14) + strictLess */
        "cmp     r12, r11\n\t"
        "bhs     9f\n\t"
        "subs    r9, r9, #1\n\t"          /* rem = count - 1 */
        "beq     8f\n\t"
        "cmp     r4, #0\n\t"              /* dx == 0? */
        "beq     3f\n\t"
        "smull   r12, lr, r4, r9\n\t"     /* (int64_t)dx * (int)rem */
        "adds    r12, r12, r10\n\t"
        "adc     lr, lr, #0\n\t"
        "orrs    lr, lr, r12, lsr #30\n\t"
        "bne     9f\n\t"
        "cmp     r4, #0\n\t"              /* dx < 0? */
        "bge     3f\n\t"
        "ldrh    lr, [r8, #-2]\n\t"       /* d0 */
        "add     r11, r1, r12, lsr #14\n\t"
        "cmp     lr, r11\n\t"
        "bhs     9f\n\t"
    "3:\n\t"
        "cmp     r9, #4\n\t"
        "bhs     5f\n\t"
    "4:\n\t"
        "ldrh    r12, [r8], #2\n\t"
        "cmp     r12, r11\n\t"
        "bhs     9f\n\t"
        "subs    r9, r9, #1\n\t"
        "bne     4b\n\t"
    "8:\n\t"
        "subs    r7, r7, #1\n\t"
        "addne   r3, r3, r6\n\t"
        "addne   r2, r2, r5\n\t"
        "bne     1b\n\t"
        "mov     r0, #1\n\t"
        "pop     {r4-r11, pc}\n\t"
    "5:\n\t"
        "cmp     r11, #65536\n\t"
        "bhs     9f\n\t"
        "sub     r11, r11, #1\n\t"
        "tst     r8, #2\n\t"
        "beq     6f\n\t"
        "ldrh    r12, [r8], #2\n\t"
        "cmp     r12, r11\n\t"
        "bhi     9f\n\t"
        "sub     r9, r9, #1\n\t"
        "cmp     r9, #3\n\t"
        "bls     7f\n\t"
    "6:\n\t"
        "orr     r11, r11, r11, lsl #16\n\t"
    "61:\n\t"
        "ldmia   r8!, {r12, lr}\n\t"
        "sub     r9, r9, #4\n\t"
        "uqsub16 r12, r12, r11\n\t"
        "uqsub16 lr, lr, r11\n\t"
        "orrs    r12, r12, lr\n\t"
        "bne     9f\n\t"
        "cmp     r9, #3\n\t"
        "bhi     61b\n\t"
        "cmp     r9, #0\n\t"
        "beq     8b\n\t"
        "uxth    r11, r11\n\t"
    "7:\n\t"
        "ldrh    r12, [r8], #2\n\t"
        "cmp     r12, r11\n\t"
        "bhi     9f\n\t"
        "subs    r9, r9, #1\n\t"
        "bne     7b\n\t"
        "b       8b\n\t"
    "9:\n\t"
        "mov     r0, #0\n\t"
        "pop     {r4-r11, pc}\n\t");
}
#else
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((noinline, optimize("O3")))
#endif
int grDepthSpansOccludedFastRef(const _grPixelPipe *pipe, int strictLess, unsigned int reference)
{
    enum { GR_DEPTH16_FRACTION_BITS = 14 };
    const unsigned int maxDepth = ((unsigned int)UINT16_MAX << GR_DEPTH16_FRACTION_BITS) |
                                  ((1u << GR_DEPTH16_FRACTION_BITS) - 1u);
    const unsigned int *scan = pipe->scanlineInfo;
    const unsigned char *row = (const unsigned char *)pipe->depthRow;
    int dx = pipe->xAdder[GR_GRADIENT_DEPTH];
    int yAdd = pipe->yAdder[GR_GRADIENT_DEPTH];
    int stride = pipe->checked.depthBufferStride;
    int n = pipe->numScanlines;

    for (;;)
    {
        unsigned int span = *scan++;
        unsigned int count = span >> 16;
        if (count != 0)
        {
            unsigned int x = span & 65535u;
            unsigned int first = reference + (unsigned int)dx * x;
            const unsigned short *depth = (const unsigned short *)row + x;
            unsigned int fBound, d0;
            if (first > maxDepth)
                return 0;
            fBound = (first >> GR_DEPTH16_FRACTION_BITS) + (unsigned int)strictLess;
            d0 = *depth++;
            if (d0 >= fBound)
                return 0;
            if (count > 1u)
            {
                unsigned int rem = count - 1u;
                if (dx != 0)
                {
                    uint64_t last = (uint64_t)((int64_t)dx * (int)rem) + first;
                    if ((((unsigned int)(last >> 32)) | ((unsigned int)last >> 30)) != 0u)
                        return 0;
                    if (dx < 0)
                    {
                        fBound = ((unsigned int)last >> GR_DEPTH16_FRACTION_BITS) + (unsigned int)strictLess;
                        if (d0 >= fBound)
                            return 0;
                    }
                }
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__) && \
    !defined(__BIG_ENDIAN__) && (!defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                if (rem < 4u)
                {
                    do
                    {
                        if (*depth++ >= fBound)
                            return 0;
                    } while (--rem);
                }
                else
                {
                    unsigned int tmpBound = fBound;
                    unsigned int tmp1, tmp2;
#if defined(__ARM_ARCH) && __ARM_ARCH >= 6
                    __asm__ volatile(
                        "cmp   %[fbound], #65536\n\t"
                        "movhs %[rem], #0\n\t"
                        "bhs   3f\n\t"
                        "sub   %[fbound], %[fbound], #1\n\t"
                        "tst   %[depth], #2\n\t"
                        "beq   0f\n\t"
                        "ldrh  %[tmp1], [%[depth]], #2\n\t"
                        "cmp   %[tmp1], %[fbound]\n\t"
                        "bhi   3f\n\t"
                        "sub   %[rem], %[rem], #1\n\t"
                        "cmp   %[rem], #3\n\t"
                        "bls   4f\n\t"
                    "0:\n\t"
                        "orr   %[fbound], %[fbound], %[fbound], lsl #16\n\t"
                    "1:\n\t"
                        "ldmia %[depth]!, {%[tmp1], %[tmp2]}\n\t"
                        "uqsub16 %[tmp1], %[tmp1], %[fbound]\n\t"
                        "uqsub16 %[tmp2], %[tmp2], %[fbound]\n\t"
                        "orrs  %[tmp1], %[tmp1], %[tmp2]\n\t"
                        "bne   3f\n\t"
                        "sub   %[rem], %[rem], #4\n\t"
                        "cmp   %[rem], #3\n\t"
                        "bhi   1b\n\t"
                        "cmp   %[rem], #0\n\t"
                        "beq   3f\n\t"
                        "uxth  %[fbound], %[fbound]\n\t"
                    "4:\n\t"
                        "ldrh  %[tmp1], [%[depth]], #2\n\t"
                        "cmp   %[tmp1], %[fbound]\n\t"
                        "bhi   3f\n\t"
                        "subs  %[rem], %[rem], #1\n\t"
                        "bne   4b\n\t"
                    "3:\n\t"
                        : [depth] "+r"(depth), [rem] "+r"(rem), [fbound] "+r"(tmpBound),
                          [tmp1] "=&r"(tmp1), [tmp2] "=&r"(tmp2)
                        :
                        : "cc", "memory");
#else
                    __asm__ volatile(
                        "cmp   %[fbound], #65536\n\t"
                        "movhs %[rem], #0\n\t"
                        "bhs   3f\n\t"
                        "sub   %[fbound], %[fbound], #1\n\t"
                        "tst   %[depth], #2\n\t"
                        "beq   1f\n\t"
                        "ldrh  %[tmp1], [%[depth]], #2\n\t"
                        "cmp   %[tmp1], %[fbound]\n\t"
                        "bhi   3f\n\t"
                        "sub   %[rem], %[rem], #1\n\t"
                    "1:\n\t"
                        "ldr   %[tmp1], [%[depth]], #4\n\t"
                        "mov   %[tmp2], %[tmp1], lsl #16\n\t"
                        "cmp   %[fbound], %[tmp2], lsr #16\n\t"
                        "cmphs %[fbound], %[tmp1], lsr #16\n\t"
                        "blo   3f\n\t"
                        "sub   %[rem], %[rem], #2\n\t"
                        "cmp   %[rem], #1\n\t"
                        "bhi   1b\n\t"
                        "cmp   %[rem], #0\n\t"
                        "beq   3f\n\t"
                        "ldrh  %[tmp1], [%[depth]], #2\n\t"
                        "cmp   %[tmp1], %[fbound]\n\t"
                        "bhi   3f\n\t"
                        "mov   %[rem], #0\n\t"
                    "3:\n\t"
                        : [depth] "+r"(depth), [rem] "+r"(rem), [fbound] "+r"(tmpBound),
                          [tmp1] "=&r"(tmp1), [tmp2] "=&r"(tmp2)
                        :
                        : "cc", "memory");
#endif
                    if (rem != 0u)
                        return 0;
                }
#else
                {
                    const unsigned short *end = depth + rem;
                    do
                    {
                        if (*depth++ >= fBound)
                            return 0;
                    } while (depth != end);
                }
#endif
            }
        }
        if (--n == 0)
            break;
        row += stride;
        reference += (unsigned int)yAdd;
    }
    return 1;
}
#endif

int grDepthSpansOccludedFast(const _grPixelPipe *pipe, int strictLess)
{
    return grDepthSpansOccludedFastRef(pipe, strictLess, (unsigned int)pipe->reference[GR_GRADIENT_DEPTH]);
}

int grDepthSpansOccluded(const _grPixelPipe *pipe, int strictLess)
{
    const unsigned char *row = (const unsigned char *)pipe->depthRow;
    int stride = pipe->checked.depthBufferStride;
    int n = pipe->numScanlines;

    if (n <= 0 || row == NULL || stride <= 0 ||
        (((uintptr_t)row | (unsigned int)stride) & 1u))
        return 0;
    return grDepthSpansOccludedFastRef(pipe, strictLess, (unsigned int)pipe->reference[GR_GRADIENT_DEPTH]);
}

/* --- grDrawClippedTriangle (grSmall.c 254; DLL 26251-26388) ------------------
 * y-sorts the triangle's three vertex records, scan-converts them, then per
 * draw: sets the color/depth/stencil row pointers for the first scanline,
 * pre-advances the active gradients to yStart and runs the compiled pixel
 * pipeline over the scanlines. emptyBits = the number of LOW ZERO bits in
 * the 20.12 screen coordinates (8 from grrDrawClippedTriangle's affine
 * path: the viewport transform leaves 8 pad bits). */

#define GR_STACK_CHECK_MAGIC0 (-559038737)
#define GR_STACK_CHECK_MAGIC1 (-1073628482)
#define GR_MUTABLE_PARAM_COUNT 78

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
void grDrawClippedTriangle(Gerbera_s *state, _grClipTriangle *tri,
                           unsigned int origIndices, int emptyBits)
{
    _grVertex *vertexBuffer = (_grVertex *)((unsigned char *)state->renderStack + 1332);
    _grVertex *v0 = vertexBuffer + grUnpackIndex(tri->indices, 0);
    _grVertex *v1 = vertexBuffer + grUnpackIndex(tri->indices, 1);
    _grVertex *v2 = vertexBuffer + grUnpackIndex(tri->indices, 2);
    _grVertex *s0 = v0, *s1 = v1, *s2 = v2;
    int y0 = v0->screenPosition[1];
    int y1 = v1->screenPosition[1];
    int y2 = v2->screenPosition[1];
    unsigned int magic0 = GR_STACK_CHECK_MAGIC0;
    unsigned int magic1 = GR_STACK_CHECK_MAGIC1;
    unsigned int activeGradientMask;
    int i;

    (void)magic0;
    (void)magic1;
    /* y-sort (record +32 = screenPosition[1]); stable 3-sort keeping the
     * original order for equal y */
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; _grVertex *tv = s0; s0 = s1; s1 = tv; }
    if (y2 < y0) { int t = y0; y0 = y2; y2 = t; _grVertex *tv = s0; s0 = s2; s2 = tv; }
    if (y2 < y1) { _grVertex *tv = s1; s1 = s2; s2 = tv; }

    if (grScanConvertTriangle(state, (_grRasterPoint *)&s0->screenPosition[0],
                              (_grRasterPoint *)&s1->screenPosition[0],
                              (_grRasterPoint *)&s2->screenPosition[0],
                              emptyBits) != 0)
    {
        _grPixelPipe *pixelPipe = state->pixelPipeline;
        int yStart = pixelPipe->yStart;
        int flatShadeIndex = (int)(grUnpackIndex(origIndices, 0));

        for (i = 0; i < 3; ++i)
        {
            unsigned short stateMask =
                vertexBuffer[grUnpackIndex(tri->indices, i)].stateMask;

            if ((stateMask & 0x2C) == 0 &&
                grAssert("stateMask & (GR_VERTEXMASK_TRANSFORMED | GR_VERTEXMASK_VIEWPORT_TRANSFORMED | GR_VERTEXMASK_COLOR_COMPUTED)",
                         "grRasterizer.inl", 853) != 0)
                grDie(0);
            if ((stateMask & 0xC0) != 0 &&
                grAssert("!(stateMask & (GR_VERTEXMASK_INTERPOLATE_GRADIENTS | GR_VERTEXMASK_FORCE_CULL))",
                         "grRasterizer.inl", 854) != 0)
                grDie(0);
        }
        activeGradientMask = (unsigned int)state->activeGradientMask;
        pixelPipe->depthRow = (void *)((char *)state->drawSurface.buffers[1].data +
                                       state->drawSurface.buffers[1].stride * yStart);
#if GERBERA_ARMV6_JIT
        int rejectDepth = state->fastDepthReject;
        if (!grSetupTriangleGradientsVisibleVertsAtY(state, tri, v0, v1, v2,
                                                     flatShadeIndex, activeGradientMask,
                                                     rejectDepth, yStart))
            goto done;
#else
        (void)grSetupTriangleGradientsVisibleVertsAtY(state, tri, v0, v1, v2,
                                                      flatShadeIndex, activeGradientMask,
                                                      0, yStart);
#endif

        pixelPipe->colorRow = (void *)((char *)state->drawSurface.buffers[0].data +
                                       state->drawSurface.buffers[0].stride * yStart);
        pixelPipe->stencilRow = (void *)((char *)state->drawSurface.buffers[2].data +
                                         state->drawSurface.buffers[2].stride * yStart);
        grExecutePixelPipeline(state);
#if GERBERA_ARMV6_JIT
done:
        (void)0;
#endif
#if HG_ASSERT_ENABLED
        for (i = 0; i < GR_MUTABLE_PARAM_COUNT; ++i)
        {
            if ((g_mutableParamsMask[i >> 3] & (1 << (i & 7))) != 0)
                pixelPipe->mutableParams[i] = -623195394;
        }
#endif
    }
}

/* --- grICacheFlush (grSmall.c; DLL 42318-42321) -------------------------------
 * Synchronizes freshly written JIT code through User::IMB_Range on Symbian
 * and the compiler cache-flush builtin on other 32-bit ARM builds. */

void grICacheFlush(void *start, unsigned int numBytes)
{
#ifdef __SYMBIAN32__
    if (start != NULL && numBytes != 0u)
        sym_User_IMB_Range(start, (unsigned char *)start + numBytes);
#elif defined(__arm__) && defined(__GNUC__)
    if (start != NULL && numBytes != 0u)
        __builtin___clear_cache((char *)start, (char *)start + numBytes);
#else
    (void)start;
    (void)numBytes;
#endif
}

/* --- grSmall.c prefix getters (DLL 34729-34780) -------------------------------
 * Plain state-field readers used by grEvaluatePixelPipeline and the API
 * query paths. The decompile declares them returning the enum types; the
 * port returns int. */

int grGetAlphaFunc(Gerbera_s *state)
{
    return state->alphaFunc;
}

int grGetSrcBlendFactor(Gerbera_s *state)
{
    return state->srcBlendFactor;
}

int grGetDstBlendFactor(Gerbera_s *state)
{
    return state->dstBlendFactor;
}

int grGetStencilFunc(Gerbera_s *state)
{
    return state->stencilFunc;
}

int grGetStencilFail(Gerbera_s *state)
{
    return state->stencilFail;
}

int grGetStencilZFail(Gerbera_s *state)
{
    return state->stencilZFail;
}

int grGetStencilZPass(Gerbera_s *state)
{
    return state->stencilZPass;
}

int grGetLogicOp(Gerbera_s *state)
{
    return state->logicOp;
}

int grGetRectGradients(Gerbera_s *state)
{
    return state->screenRectGradients;
}

/* Work memory holds the aligned context and its pipeline-cache entries.
 * Retain the original minimum when those structures fit in less space. */

int grGetMinWorkMemorySize(void)
{
    int stateBytes = ((int)sizeof(Gerbera_s) + 7) & ~7;
    int entryBytes = GR_PIPELINE_CACHE_ENTRIES * (((int)sizeof(_grCacheEntry) + 7) & ~7);

    return hgMax32(9712, stateBytes + entryBytes);
}

int grGetMinSharedMemorySize(void)
{
    return GR_RENDER_STACK_SIZE;
}
