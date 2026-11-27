/* grRasterizer.c - per-triangle gradient setup (port of the decompiled
 * grRasterizer.inl gradient block).
 *
 * grSetupTriangleGradients converts the three vertex records of a queued
 * triangle (render stack vertex buffer, 128-byte records) into per-pixel
 * step rates in the pixel-pipe record: reference[] holds the attribute value
 * at screen origin, xAdder[]/yAdder[] the per-pixel steps, both indexed by
 * GR_GRADIENT_*. Depth and fog go through grSetupGradient (which also
 * rebuilds the reference from the v0 screen offset), texcoord gradients go
 * through grComputeDeltas + grSetupTextureUnit, and
 * colors use grSetupGradient or the flat-shade direct writes depending on
 * GR_CAPABILITY_SMOOTH_SHADING.
 *
 * Assert sites keep the original file/line strings.
 */
#include "gerbera/grState.h"
#include "gerbera/grDefs.h"
#include "imath/imApi.h"
#include "hg/hgInt32.h"
#include <string.h>

int grSubpixelTrunc(int i);   /* grClipper.c (grUtil.h; DLL 004214A0) */

#define GR_VERTEX_BUFFER(state) \
    ((_grVertex *)((unsigned char *)(state)->renderStack + 1332))

#define GR_PIXEL_PIPE(state) ((_grPixelPipe *)(state)->pixelPipeline)

/* --- grSetupGradient (grRasterizer.inl 500; DLL 25988-26026) ---------------- */

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3,no-ipa-sra")))
#endif
void grSetupGradient(const _grGradientInfo * restrict info, int gradient, int g01, int g02,
                     int reference)
{
    _grPixelPipe * restrict pPP = info->pPP;
    int xAdd32 = 0;
    int yAdd32 = 0;

    if (hgInRange32(info->shift, 0, 31) == 0)
        imAssert("hgInRange32(info->shift, 0, 31)", "grRasterizer.inl", 500);
    if (info == NULL)
        imAssert("info", "grRasterizer.inl", 501);
    if (g01 != 0 || g02 != 0)   /* __PAIR64__(g02, g01) != 0 */
    {
        HGint64 xRaw = hgMadd64(hgMul64(info->v01syPerArea, g02), info->v02syPerArea,
                                (int)(0u - (unsigned int)g01));
        HGint64 yRaw = hgMadd64(hgMul64(info->v02sxPerArea, g01), info->v01sxPerArea,
                                (int)(0u - (unsigned int)g02));
        int sh = info->shift + 14;
        int xHi, yHi, refSub;

        if (__builtin_expect(sh < 32, 1))
        {
            int rsh = 32 - sh;
            xAdd32 = (int)(((unsigned int)hgGet64l(xRaw) >> sh) |
                           ((unsigned int)hgGet64h(xRaw) << rsh));
            xHi = hgGet64h(xRaw) >> sh;
            yAdd32 = (int)(((unsigned int)hgGet64l(yRaw) >> sh) |
                           ((unsigned int)hgGet64h(yRaw) << rsh));
            yHi = hgGet64h(yRaw) >> sh;
        }
        else
        {
            int sh2 = sh - 32;
            xAdd32 = hgGet64h(xRaw) >> sh2;
            xHi = hgGet64h(xRaw) >> 31;
            yAdd32 = hgGet64h(yRaw) >> sh2;
            yHi = hgGet64h(yRaw) >> 31;
        }
#if defined(__GNUC__)
        if (__builtin_expect(((xHi ^ (xAdd32 >> 31)) | (yHi ^ (yAdd32 >> 31))) == 0, 1))
#else
        if (((xHi ^ (xAdd32 >> 31)) | (yHi ^ (yAdd32 >> 31))) == 0)
#endif
        {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
            unsigned int lo;
            int hi;
            __asm__("smull %0, %1, %2, %3\n\t"
                    "smlal %0, %1, %4, %5"
                    : "=&r"(lo), "=&r"(hi)
                    : "r"(xAdd32), "r"(info->v0sx), "r"(yAdd32), "r"(info->v0sy));
            refSub = (int)(((unsigned int)hi << 20) | (lo >> 12));
#else
            HGint64 s = hgMadd64(hgMul64(xAdd32, info->v0sx), yAdd32, info->v0sy);
            refSub = hgLsl64h_0_31(s, 20);
#endif
        }
        else
        {
            HGint64 xAdd64a = hgAsr64(xRaw, sh);
            HGint64 yAdd64a = hgAsr64(yRaw, sh);
            HGint64 xAdd64b = hgMul64x32(xAdd64a, info->v0sx);
            HGint64 yAdd64b = hgMul64x32(yAdd64a, info->v0sy);
            refSub = hgLsl64h_0_31(hgAdd64(xAdd64b, yAdd64b), 20);
        }
        reference = (int)((unsigned int)reference - (unsigned int)refSub);
    }
    pPP->reference[gradient] = reference;
    pPP->xAdder[gradient] = xAdd32;
    pPP->yAdder[gradient] = yAdd32;
}

/* --- grComputeDeltas (grRasterizer.inl 550; DLL 26029-26061) -----------------
 * Same plane-gradient solve as grSetupGradient but keeps full precision in
 * the adders (the mip-level estimate in grSetupTextureUnit consumes them
 * before they are re-shifted per wrap mode). The >= 32 / < 32 split computes
 * (syDelta >> (shift+14)) from the high word with a compensating left shift. */

void grComputeDeltas(const _grGradientInfo *info, int gradient, int g01, int g02)
{
    int xAdder = 0;
    int yAdder = 0;

    if (hgInRange32(info->shift, 0, 31) == 0)
        imAssert("hgInRange32(info->shift, 0, 31)", "grRasterizer.inl", 550);
    if (info == NULL)
        imAssert("info", "grRasterizer.inl", 551);
    if (g01 != 0 || g02 != 0)
    {
        HGint64 syDelta = hgMul64(info->v01syPerArea, g02);
        HGint64 sxDelta;

        syDelta = hgMadd64(syDelta, info->v02syPerArea,
                           (int)(0u - (unsigned int)g01));
        sxDelta = hgMul64(info->v02sxPerArea, g01);
        sxDelta = hgMadd64(sxDelta, info->v01sxPerArea,
                           (int)(0u - (unsigned int)g02));
        if (__builtin_expect(info->shift < 18, 1))
        {
            int sh = 18 - info->shift; /* 1..18 because info->shift in 0..17 */
            int rsh = 32 - sh;         /* 14..31 */
            xAdder = (int)(((unsigned int)hgGet64h(syDelta) << sh) |
                           ((unsigned int)hgGet64l(syDelta) >> rsh));
            yAdder = (int)(((unsigned int)hgGet64h(sxDelta) << sh) |
                           ((unsigned int)hgGet64l(sxDelta) >> rsh));
        }
        else
        {
            int sh = info->shift - 18;
            xAdder = hgGet64h(syDelta) >> sh;
            yAdder = hgGet64h(sxDelta) >> sh;
        }
    }
    info->pPP->xAdder[gradient] = xAdder;
    info->pPP->yAdder[gradient] = yAdder;
}

/* --- grSetupTriangleGradients (grRasterizer.inl 603; DLL 26064-26248) --------
 *
 * gradientMask bits (state->activeGradientMask convention): 0x1 depth,
 * 0x2 fog, 0x3C RGBA (4<<comp), 0xC0 tex0 s/t, 0x300 tex1 s/t, 0x20 alpha
 * (inside the 0x3C walk). The texcoord walk runs texIndex 1..0 and, per
 * unit, coord 1..0 (T then S) - gradient index = coord + 2*texIndex + 6.
 */

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
int grSetupTriangleGradientsVisibleVerts(Gerbera_s *state, _grClipTriangle *tri,
                                         _grVertex *v0, _grVertex *v1, _grVertex *v2,
                                         int flatShadeIndex, unsigned int gradientMask, int rejectDepth)
{
    int ok = 1;

    if ((state == NULL || tri == NULL) && grAssert("state && tri", "grRasterizer.inl", 603) != 0)
        ok = 0;
    if (ok == 0)
        grDie(0);
    /* compiled bound is the literal 67 (= 64 + 4 - 1 in the rasterizer TU's
     * header scope; the clipper TU pins the rasterizer cache at 28 records
     * with its temp base at 92 - the original headers disagree on the macro,
     * the value wins for behavior, the string stays verbatim) */
    if (hgInRange32(flatShadeIndex, 0, 67) == 0 &&
        grAssert("hgInRange32(flatShadeIndex, 0, GR_VERTEX_CACHE_SIZE + GR_NUM_RASTERIZER_CACHE_VERTICES - 1)",
                 "grRasterizer.inl", 606) != 0)
        grDie(0);
    if (gradientMask != 0)
    {
        _grPixelPipe *pixelPipe = GR_PIXEL_PIPE(state);
        _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
        HGint64 reciprocal;
        int recipShift;
        int signedRecip;
        int v0sx;
        int v0sy;
        _grGradientInfo info;
        int texIndex;
        int coord;

        ok = 1;
        if (tri->area == 0 && grAssert("tri->area != 0", "grRasterizer.inl", 628) != 0)
            ok = 0;
        if (ok == 0)
            grDie(0);

        /* imRcpu(abs(area)): low word = normalization shift, high word =
         * reciprocal mantissa (0.32) */
        reciprocal = imRcpu_inline((unsigned int)hgAbs32(tri->area));
        recipShift = hgGet64l(reciprocal);
        signedRecip = hgCopySign32(hgGet64h(reciprocal), -tri->area);
        info.pPP = pixelPipe;
        info.shift = 31 - recipShift;
        if (hgInRange32(recipShift, 0, 31) == 0 &&
            grAssert("hgInRange32(reciprocal.shift, 0, 31)",
                     "grRasterizer.inl", 641) != 0)
            grDie(0);

        /* edge derivatives: 16 * screen delta * signed reciprocal (high
         * half); v0 screen coords keep the raw record values for the deltas
         * and the -4096-centered ones for the reference rebuild */
        info.v01sxPerArea = hgMul64h(16 * (v1->screenPosition[0] - v0->screenPosition[0]), signedRecip);
        info.v01syPerArea = hgMul64h(16 * (v1->screenPosition[1] - v0->screenPosition[1]), signedRecip);
        info.v02sxPerArea = hgMul64h(16 * (v2->screenPosition[0] - v0->screenPosition[0]), signedRecip);
        info.v02syPerArea = hgMul64h(16 * (v2->screenPosition[1] - v0->screenPosition[1]), signedRecip);
        v0sx = v0->screenPosition[0] - 4096;
        v0sy = v0->screenPosition[1] - 4096;
        info.v0sx = v0sx;
        info.v0sy = v0sy;

        /* depth gradient (+ depth offset when the polygon-offset capability
         * for the current primitive class is on) */
        if ((gradientMask & 1) != 0)
        {
            int d0 = v0->screenPosition[2];
            int d1 = v1->screenPosition[2];
            int d2 = v2->screenPosition[2];

            if (d0 == d1 && d0 == d2)
            {
                pixelPipe->reference[GR_GRADIENT_DEPTH] = d0;
                pixelPipe->xAdder[GR_GRADIENT_DEPTH] = 0;
                pixelPipe->yAdder[GR_GRADIENT_DEPTH] = 0;
            }
            else
            {
                grSetupGradient(&info, GR_GRADIENT_DEPTH, d1 - d0, d2 - d0, d0);
            }
            if (grIsDepthOffsetEnabled(state, state->primitiveType) != 0)
                grComputeDepthOffset(state);
        }
        /* Test the exact depth plane before preparing shading attributes. */
        if (rejectDepth && (gradientMask & (1u << GR_GRADIENT_DEPTH)))
        {
            unsigned int depthRef = (unsigned int)pixelPipe->reference[GR_GRADIENT_DEPTH] +
                (unsigned int)pixelPipe->yAdder[GR_GRADIENT_DEPTH] * (unsigned int)pixelPipe->yStart;
            if (grDepthSpansOccludedFastRef(pixelPipe, rejectDepth >> 1, depthRef))
                return 0;
        }

        /* texcoord gradients (texIndex 1..0) */
        for (texIndex = GR_GRADIENT_FOG; texIndex >= GR_GRADIENT_DEPTH; --texIndex)
        {
            if (((192 << (2 * texIndex)) & gradientMask) != 0)
            {
                int baseValue[2];   /* shifted v0 texcoord per coord */
                int preShift[2];    /* clamped normalization shift - 1 */
                int *tc0 = &v0->texCoord[texIndex].x;
                int *tc1 = &v1->texCoord[texIndex].x;
                int *tc2 = &v2->texCoord[texIndex].x;
                int pairs[2][2];

                for (coord = GR_GRADIENT_FOG; coord >= GR_GRADIENT_DEPTH; --coord)
                {
                    int d0 = tc0[coord];
                    int d1 = tc1[coord];
                    int d2 = tc2[coord];
                    int shift;

                    shift = hgClz32(hgAbs32(d2) | (hgAbs32(d0) | hgAbs32(d1))) - 1;
                    {
                        int clamped = hgClamp32_0_x(shift, 11);
                        d0 = (int)((unsigned int)d0 << clamped);
                        d1 = (int)((unsigned int)d1 << clamped);
                        d2 = (int)((unsigned int)d2 << clamped);
                        d0 >>= 1;
                        d1 >>= 1;
                        d2 >>= 1;
                        preShift[coord] = clamped - 1;
                    }
                    grComputeDeltas(&info, coord + 2 * texIndex + 6,
                                    d1 - d0, d2 - d0);
                    baseValue[coord] = d0;
                }
                pairs[0][0] = baseValue[0];
                pairs[0][1] = preShift[0];
                pairs[1][0] = baseValue[1];
                pairs[1][1] = preShift[1];
                grSetupTextureUnit(state, texIndex, pairs, v0sx, v0sy);
            }
        }

        /* fog gradient (fog rides the back-alpha slot, record +54) */
        if ((gradientMask & 2) != 0)
        {
            unsigned short rf0 = v0->color.final[1][3];
            unsigned short rf1 = v1->color.final[1][3];
            unsigned short rf2 = v2->color.final[1][3];
            int f0 = (int)rf0 << 8;

            if (rf0 == rf1 && rf0 == rf2)
            {
                pixelPipe->reference[GR_GRADIENT_FOG] = f0 + 128;
                pixelPipe->xAdder[GR_GRADIENT_FOG] = 0;
                pixelPipe->yAdder[GR_GRADIENT_FOG] = 0;
            }
            else
            {
                grSetupGradient(&info, GR_GRADIENT_FOG,
                                ((int)rf1 << 8) - f0, ((int)rf2 << 8) - f0,
                                f0 + 128);
            }
        }

        /* color gradients: per-vertex when smooth shading is on, constant
         * from the flat-shade vertex otherwise */
        if ((gradientMask & 0x3C) != 0)
        {
            int side = tri->side;

            ok = 1;
            if (side != GR_SIDE_FRONT && side != GR_SIDE_BACK &&
                grAssert("side==GR_SIDE_FRONT || side==GR_SIDE_BACK",
                         "grRasterizer.inl", 749) != 0)
                ok = 0;
            if (ok == 0)
                grDie(0);
            if (grTestCapability(state, GR_CAPABILITY_SMOOTH_SHADING) != 0)
            {
                if (side == GR_SIDE_FRONT && (gradientMask & 0x1Cu) == 0x1Cu)
                {
                    unsigned int rg0, rg1, rg2;
                    __builtin_memcpy(&rg0, &v0->color.final[0][0], sizeof(rg0));
                    __builtin_memcpy(&rg1, &v1->color.final[0][0], sizeof(rg1));
                    __builtin_memcpy(&rg2, &v2->color.final[0][0], sizeof(rg2));
                    if (rg0 == rg1 && rg0 == rg2)
                    {
                        const unsigned short *c = &v0->color.final[0][0];
                        int sameAll;
                        if (gradientMask & 0x20u)
                        {
                            unsigned int ba0, ba1, ba2;
                            __builtin_memcpy(&ba0, &v0->color.final[0][2], sizeof(ba0));
                            __builtin_memcpy(&ba1, &v1->color.final[0][2], sizeof(ba1));
                            __builtin_memcpy(&ba2, &v2->color.final[0][2], sizeof(ba2));
                            sameAll = (ba0 == ba1 && ba0 == ba2);
                            if (sameAll)
                            {
                                pixelPipe->reference[GR_GRADIENT_COLOR_A] = ((int)c[3] << 8) + 0x8000;
                                pixelPipe->xAdder[GR_GRADIENT_COLOR_A] = 0;
                                pixelPipe->yAdder[GR_GRADIENT_COLOR_A] = 0;
                            }
                        }
                        else
                        {
                            sameAll = (c[2] == v1->color.final[0][2] && c[2] == v2->color.final[0][2]);
                        }
                        if (sameAll)
                        {
                            pixelPipe->reference[GR_GRADIENT_COLOR_R] = ((int)c[0] << 8) + 0x8000;
                            pixelPipe->reference[GR_GRADIENT_COLOR_G] = ((int)c[1] << 8) + 0x8000;
                            pixelPipe->reference[GR_GRADIENT_COLOR_B] = ((int)c[2] << 8) + 0x8000;
                            pixelPipe->xAdder[GR_GRADIENT_COLOR_R] = 0;
                            pixelPipe->xAdder[GR_GRADIENT_COLOR_G] = 0;
                            pixelPipe->xAdder[GR_GRADIENT_COLOR_B] = 0;
                            pixelPipe->yAdder[GR_GRADIENT_COLOR_R] = 0;
                            pixelPipe->yAdder[GR_GRADIENT_COLOR_G] = 0;
                            pixelPipe->yAdder[GR_GRADIENT_COLOR_B] = 0;
                            return 1;
                        }
                    }
                }
                for (coord = GR_GRADIENT_COLOR_R; coord >= GR_GRADIENT_DEPTH; --coord)
                {
                    if (((4 << coord) & gradientMask) != 0)
                    {
                        unsigned short rc0 = v0->color.final[side][coord];
                        unsigned short rc1 = v1->color.final[side][coord];
                        unsigned short rc2 = v2->color.final[side][coord];
                        int c0 = (int)rc0 << 8;

                        if (rc0 == rc1 && rc0 == rc2)
                        {
                            pixelPipe->reference[coord + 2] = c0 + 0x8000;
                            pixelPipe->xAdder[coord + 2] = 0;
                            pixelPipe->yAdder[coord + 2] = 0;
                        }
                        else
                        {
                            grSetupGradient(&info, coord + 2,
                                            ((int)rc1 << 8) - c0, ((int)rc2 << 8) - c0,
                                            c0 + 0x8000);
                        }
                    }
                }
                if ((gradientMask & 0x20) != 0)
                {
                    /* alpha always interpolates the FRONT alpha (+46) */
                    unsigned short ra0 = v0->color.final[0][3];
                    unsigned short ra1 = v1->color.final[0][3];
                    unsigned short ra2 = v2->color.final[0][3];
                    int a0 = (int)ra0 << 8;

                    if (ra0 == ra1 && ra0 == ra2)
                    {
                        pixelPipe->reference[GR_GRADIENT_COLOR_A] = a0 + 0x8000;
                        pixelPipe->xAdder[GR_GRADIENT_COLOR_A] = 0;
                        pixelPipe->yAdder[GR_GRADIENT_COLOR_A] = 0;
                    }
                    else
                    {
                        grSetupGradient(&info, GR_GRADIENT_COLOR_A,
                                        ((int)ra1 << 8) - a0, ((int)ra2 << 8) - a0,
                                        a0 + 0x8000);
                    }
                }
            }
            else
            {
                _grVertex *flat = vertexBuffer + flatShadeIndex;
                const unsigned short *flatRGB = &flat->color.final[side][0];

                for (coord = GR_GRADIENT_DEPTH; coord < GR_GRADIENT_COLOR_G; ++coord)
                {
                    pixelPipe->reference[coord + 2] =
                        ((int)flatRGB[coord] << 8) + 0x8000;
                    pixelPipe->xAdder[coord + 2] = 0;
                    pixelPipe->yAdder[coord + 2] = 0;
                }
                pixelPipe->reference[GR_GRADIENT_COLOR_A] =
                    ((int)flat->color.final[0][3] << 8) + 0x8000;
                pixelPipe->xAdder[GR_GRADIENT_COLOR_A] = 0;
                pixelPipe->yAdder[GR_GRADIENT_COLOR_A] = 0;
            }
        }
    }
    return 1;
}

int grSetupTriangleGradientsVisible(Gerbera_s *state, _grClipTriangle *tri,
                                    int flatShadeIndex, unsigned int gradientMask, int rejectDepth)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    _grVertex *v0 = vertexBuffer + grUnpackIndex(tri->indices, 0);
    _grVertex *v1 = vertexBuffer + grUnpackIndex(tri->indices, 1);
    _grVertex *v2 = vertexBuffer + grUnpackIndex(tri->indices, 2);
    return grSetupTriangleGradientsVisibleVerts(state, tri, v0, v1, v2, flatShadeIndex, gradientMask, rejectDepth);
}

void grSetupTriangleGradients(Gerbera_s *state, _grClipTriangle *tri,
                              int flatShadeIndex, unsigned int gradientMask)
{
    (void)grSetupTriangleGradientsVisible(state, tri, flatShadeIndex, gradientMask, 0);
}

/* --- grDivsu (grScanConvert.inl 29; DLL 24922-24932) ------------------------- */

#if defined(__GNUC__)
__attribute__((noinline))
#endif
int grDivsu(int a, int b)
{
    if (b <= 0 && grAssert("b > 0", "grScanConvert.inl", 29) != 0)
        grDie(0);
    return a / b;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static unsigned int grFastRecip32(unsigned int d)
{
    if (d == 1u)
        return 0xFFFFFFFFu;
    {
        HGint64 rr = imRcpu_inline(d);
        unsigned int sh = (unsigned int)rr;
        unsigned int recip = (unsigned int)(rr >> 32);
        unsigned int q = recip >> (30u - sh);
        if (0xFFFFFFFFu - q * d >= d)
            ++q;
        return q;
    }
}

#if defined(__GNUC__) && defined(__arm__)
__attribute__((always_inline))
#endif
static inline int grDivideEdgeSlopeCached(Gerbera_s *state, int numerator, int denominator)
{
    unsigned int divisor, sign, magnitude, reciprocal, quotient, slot;

    if (denominator <= 0)
        return grDivsu(numerator, denominator);
    if (numerator == 0)
        return 0;
    if (denominator == 1)
        return numerator;
    divisor = (unsigned int)denominator;
    slot = (divisor ^ (divisor >> 8)) & (GR_EDGE_SLOPE_CACHE_SIZE - 1u);
    reciprocal = state->edgeSlopeCache[slot].reciprocal;
    if (state->edgeSlopeCache[slot].divisor != divisor)
    {
        reciprocal = grFastRecip32(divisor);
        state->edgeSlopeCache[slot].divisor = divisor;
        state->edgeSlopeCache[slot].reciprocal = reciprocal;
    }
    sign = (unsigned int)(numerator >> 31);
    magnitude = ((unsigned int)numerator ^ sign) - sign;
    /* The high word of n * floor((2^32-1)/d) is n/d or one less. */
    quotient = hgMulu64h(magnitude, reciprocal);
    quotient += magnitude - quotient * divisor >= divisor;
    return (int)((quotient ^ sign) - sign);
}

int grDivideEdgeSlope(Gerbera_s *state, int numerator, int denominator)
{
    return grDivideEdgeSlopeCached(state, numerator, denominator);
}

#if defined(__arm__) && !defined(__ARM_FEATURE_IDIV)
#define GR_DIVIDE_EDGE(state, a, b) grDivideEdgeSlopeCached(state, a, b)
#else
#define GR_DIVIDE_EDGE(state, a, b) grDivsu(a, b)
#endif

/* --- grScanConvertWithXScissoring (grScanConvert.inl 55; DLL 24935-25025) -----
 * Emits one u32 per scanline (left | numPixels << 16) for the top half and
 * then the bottom half, clamping the span to the scissor X range. The edge
 * x values are 18.14 (pixel << 14); the left edge is xStart[0], the right
 * edge xStart[1]. */

static void grScanConvertScissorHalf(unsigned int **scanPtr,
                                     int leftX, int leftAdder,
                                     int rightX, int rightAdder,
                                     int x0, int x1, int height)
{
    unsigned int *scanlineInfo = *scanPtr;
    do
    {
        int left = hgMax32(leftX >> 14, x0);
        int right = hgMin32(rightX >> 14, x1);
        int numPixels = hgMaxZero32(right - left);

        *scanlineInfo++ = (unsigned int)left + ((unsigned int)numPixels << 16);
        if (hgInRange32(left, 0, 0xFFFF) == 0 &&
            grAssert("hgInRange32(left, 0, 0xFFFF)", "grScanConvert.inl", 72) != 0)
            grDie(0);
        if (hgInRange32(numPixels, 0, 0xFFFF) == 0 &&
            grAssert("hgInRange32(numPixels, 0, 0xFFFF)", "grScanConvert.inl", 73) != 0)
            grDie(0);
        leftX += leftAdder;
        rightX += rightAdder;
    }
    while (--height != 0);
    *scanPtr = scanlineInfo;
}

static void grScanConvertNoScissorHalf(unsigned int **scanPtr,
                                       int leftX, int leftAdder,
                                       int rightX, int rightAdder,
                                       int height)
{
    unsigned int *scanlineInfo = *scanPtr;
    do
    {
        int left = leftX >> 14;
        int right = rightX >> 14;
        int numPixels = hgMaxZero32(right - left);

        *scanlineInfo++ = (unsigned int)left + ((unsigned int)numPixels << 16);
        if (((unsigned int)left | (unsigned int)numPixels) > 0xFFFFu)
        {
            if (hgInRange32(left, 0, 0xFFFF) == 0 &&
                grAssert("hgInRange32(left, 0, 0xFFFF)", "grScanConvert.inl", 135) != 0)
                grDie(0);
            if (hgInRange32(numPixels, 0, 0xFFFF) == 0 &&
                grAssert("hgInRange32(numPixels, 0, 0xFFFF)", "grScanConvert.inl", 136) != 0)
                grDie(0);
        }
        leftX += leftAdder;
        rightX += rightAdder;
    }
    while (--height != 0);
    *scanPtr = scanlineInfo;
}

void grScanConvertWithXScissoring(_grEdgePair *edgePairs,
                                  unsigned int *scanlineInfo,
                                  _grRect *scissor, int topHeight,
                                  int btmHeight)
{
    int ok = (edgePairs != NULL && scanlineInfo != NULL && scissor != NULL);
    int x0 = scissor->x0;
    int x1 = scissor->x1;

    if (ok == 0 &&
        grAssert("edgePairs && scanlineInfo && scissor", "grScanConvert.inl", 55) != 0)
        grDie(0);
    if (topHeight != 0)
        grScanConvertScissorHalf(&scanlineInfo,
                                 edgePairs[0].xStart[0], edgePairs[0].xAdder[0],
                                 edgePairs[0].xStart[1], edgePairs[0].xAdder[1],
                                 x0, x1, topHeight);
    if (btmHeight != 0)
        grScanConvertScissorHalf(&scanlineInfo,
                                 edgePairs[1].xStart[0], edgePairs[1].xAdder[0],
                                 edgePairs[1].xStart[1], edgePairs[1].xAdder[1],
                                 x0, x1, btmHeight);
}

/* --- grScanConvertWithoutXScissoring (grScanConvert.inl 118; DLL 25028-25111) - */

void grScanConvertWithoutXScissoring(_grEdgePair *edgePairs,
                                     unsigned int *scanlineInfo,
                                     int topHeight, int btmHeight)
{
    int ok = 1;

    if ((edgePairs == NULL || scanlineInfo == NULL) &&
        grAssert("edgePairs && scanlineInfo", "grScanConvert.inl", 118) != 0)
        ok = 0;
    if (ok == 0)
        grDie(0);
    if (topHeight != 0)
        grScanConvertNoScissorHalf(&scanlineInfo,
                                   edgePairs[0].xStart[0], edgePairs[0].xAdder[0],
                                   edgePairs[0].xStart[1], edgePairs[0].xAdder[1],
                                   topHeight);
    if (btmHeight != 0)
        grScanConvertNoScissorHalf(&scanlineInfo,
                                   edgePairs[1].xStart[0], edgePairs[1].xAdder[0],
                                   edgePairs[1].xStart[1], edgePairs[1].xAdder[1],
                                   btmHeight);
}

/* --- grScanConvertTriangle (grScanConvert.inl 249; DLL 25114-25331) -----------
 * p0/p1/p2 are y-sorted screen positions (20.12: pixel<<12, low emptyBits
 * bits zero). Splits the triangle at the middle vertex into a top half
 * (topHeight scanlines) and a bottom half (btmHeight), builds per-half
 * {left, right} edge pairs from the long edge (p0->p2) plus the two short
 * edges, y-clamps against the scissor, emits scanline words into the render
 * stack buffer at +15668, trims empty rows top/bottom and stores
 * {yStart, numScanlines, scanlineInfo} into the pixel pipe (+188/+180/+184).
 * Returns the final scanline count (0 when the triangle degenerates). */

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
int grScanConvertTriangle(Gerbera_s *state, _grRasterPoint *p0,
                          _grRasterPoint *p1, _grRasterPoint *p2,
                          int emptyBits)
{
    _grRect *scissor = &state->renderScissor;
    if ((p0->y >> 12) >= (p2->y >> 12) ||
        (p2->y >> 12) <= scissor->y0 || (p0->y >> 12) >= scissor->y1)
        return 0;
    int v0yT = grSubpixelTrunc(p0->y);
    int v1yT = grSubpixelTrunc(p1->y);
    int v2yT = grSubpixelTrunc(p2->y);
    int v0yTop = hgMin32(v0yT, scissor->y1);
    int v1yTop = hgMin32(v1yT, scissor->y1);
    int v2yTop = hgMin32(v2yT, scissor->y1);
    int topHeight = v1yTop - v0yTop;           /* scanlines above the middle */
    int btmHeight = v2yTop - v1yTop;           /* scanlines below the middle */
    int d20x = (p2->x - p0->x) >> emptyBits;
    int d20y = (p2->y - p0->y) >> emptyBits;
    int v0xT = grSubpixelTrunc(p0->x);         /* pixel-rounded vertices */
    int v1xT = grSubpixelTrunc(p1->x);
    int v2xT = grSubpixelTrunc(p2->x);
    int v0yBottom = hgMax32(v0yT, scissor->y0);
    int minX = hgMin32(hgMin32(v0xT, v1xT), v2xT);
    int maxX = hgMax32(hgMax32(v0xT, v1xT), v2xT);
    int needsXScissoring = minX < scissor->x0 || maxX > scissor->x1;
    int fracV0y = 4095 - (p0->y & 0xFFF);      /* distance to the next
                                                * scanline (0.12, -1 LSB) */
    int fracV1y = 4095 - (p1->y & 0xFFF);
    int slope02 = GR_DIVIDE_EDGE(state, imLsl32OF(d20x, 14), d20y);  /* long-edge slope,
                                                * 2.14 per scanline */
    int longAtV0 = 4 * p0->x + (int)(((long long)fracV0y * slope02) >> 12);
    int longAtV1 = longAtV0 + slope02 * (v1yT - v0yT);  /* UNCLAMPED middle
                                                * delta (decompile p4.xAdder[1]
                                                * uses the raw truncs) */
    int longMid = longAtV1 - (((long long)fracV1y * slope02) >> 12);
    int longOnRight = (int)((unsigned int)(4 * p1->x - longMid) >> 31);
    int top0X = 0, top0A = 0, top1X = 0, top1A = 0;
    int btm0X = 0, btm0A = 0, btm1X = 0, btm1A = 0;
    int yStart = v0yBottom;
    int total;
    unsigned int *scanInfo;

    if (!(p0->y <= p1->y && p1->y <= p2->y) &&
        grAssert("v0sy <= v1sy && v1sy <= v2sy", "grScanConvert.inl", 249) != 0)
        grDie(0);
    if ((topHeight < 0 || btmHeight < 0) &&
        grAssert("topHeight >= 0 && btmHeight >= 0", "grScanConvert.inl", 250) != 0)
        grDie(0);
    if ((((1 << emptyBits) - 1) & (p0->x | p0->y | p1->x | p1->y | p2->x | p2->y)) != 0)
    {
        if (((1 << emptyBits) - 1) & p0->x &&
            grAssert("!(v0sx & ((1<<emptyBits)-1))", "grScanConvert.inl", 252) != 0)
            grDie(0);
        if (((1 << emptyBits) - 1) & p0->y &&
            grAssert("!(v0sy & ((1<<emptyBits)-1))", "grScanConvert.inl", 253) != 0)
            grDie(0);
        if (((1 << emptyBits) - 1) & p1->x &&
            grAssert("!(v1sx & ((1<<emptyBits)-1))", "grScanConvert.inl", 254) != 0)
            grDie(0);
        if (((1 << emptyBits) - 1) & p1->y &&
            grAssert("!(v1sy & ((1<<emptyBits)-1))", "grScanConvert.inl", 255) != 0)
            grDie(0);
        if (((1 << emptyBits) - 1) & p2->x &&
            grAssert("!(v2sx & ((1<<emptyBits)-1))", "grScanConvert.inl", 256) != 0)
            grDie(0);
        if (((1 << emptyBits) - 1) & p2->y &&
            grAssert("!(v2sy & ((1<<emptyBits)-1))", "grScanConvert.inl", 257) != 0)
            grDie(0);
    }

    {
        int topAdvance = 0;
        int btmAdvance = 0;

        if (v0yTop < scissor->y0)
        {
            topAdvance = scissor->y0 - v0yTop;
            topHeight = hgMaxZero32(topHeight - topAdvance);
            if (v1yTop < scissor->y0)
            {
                btmAdvance = scissor->y0 - v1yTop;
                btmHeight = hgMaxZero32(btmHeight - btmAdvance);
            }
        }
        if (topHeight > 0)
        {
            int d10x = (p1->x - p0->x) >> emptyBits;
            int d10y = (p1->y - p0->y) >> emptyBits;
            int slope01 = GR_DIVIDE_EDGE(state, imLsl32OF(d10x, 14), d10y);
            int shortAtV0 = 4 * p0->x + (int)(((long long)fracV0y * slope01) >> 12);
            int lx0 = longOnRight ? shortAtV0 : longAtV0;
            int la0 = longOnRight ? slope01 : slope02;
            int rx0 = longOnRight ? longAtV0 : shortAtV0;
            int ra0 = longOnRight ? slope02 : slope01;
            if (topAdvance != 0)
            {
                lx0 = (int)((unsigned int)lx0 + (unsigned int)la0 * (unsigned int)topAdvance);
                rx0 = (int)((unsigned int)rx0 + (unsigned int)ra0 * (unsigned int)topAdvance);
            }
            top0X = lx0;
            top0A = la0;
            top1X = rx0;
            top1A = ra0;
        }
        if (btmHeight > 0)
        {
            int d21x = (p2->x - p1->x) >> emptyBits;
            int d21y = (p2->y - p1->y) >> emptyBits;
            int slope12 = GR_DIVIDE_EDGE(state, imLsl32OF(d21x, 14), d21y);
            int shortAtV1 = 4 * p1->x + (int)(((long long)fracV1y * slope12) >> 12);
            int lx1 = longOnRight ? shortAtV1 : longAtV1;
            int la1 = longOnRight ? slope12 : slope02;
            int rx1 = longOnRight ? longAtV1 : shortAtV1;
            int ra1 = longOnRight ? slope02 : slope12;
            if (btmAdvance != 0)
            {
                lx1 = (int)((unsigned int)lx1 + (unsigned int)la1 * (unsigned int)btmAdvance);
                rx1 = (int)((unsigned int)rx1 + (unsigned int)ra1 * (unsigned int)btmAdvance);
            }
            btm0X = lx1;
            btm0A = la1;
            btm1X = rx1;
            btm1A = ra1;
        }
    }
    total = btmHeight + topHeight;
    if (total != 0)
    {
        unsigned int *scanCursor;

        scanInfo = (unsigned int *)((unsigned char *)state->renderStack +
                                    GR_RENDER_STACK_SCANLINE_INFO);
        scanCursor = scanInfo;
#if defined(__GNUC__)
        if (__builtin_expect(needsXScissoring, 0))
#else
        if (needsXScissoring)
#endif
        {
            int x0 = scissor->x0;
            int x1 = scissor->x1;
            if (topHeight != 0)
                grScanConvertScissorHalf(&scanCursor, top0X, top0A, top1X, top1A,
                                         x0, x1, topHeight);
            if (btmHeight != 0)
                grScanConvertScissorHalf(&scanCursor, btm0X, btm0A, btm1X, btm1A,
                                         x0, x1, btmHeight);
        }
        else
        {
            if (topHeight != 0)
                grScanConvertNoScissorHalf(&scanCursor, top0X, top0A, top1X, top1A,
                                           topHeight);
            if (btmHeight != 0)
                grScanConvertNoScissorHalf(&scanCursor, btm0X, btm0A, btm1X, btm1A,
                                           btmHeight);
        }
        while (total != 0 &&
               (int)(scanInfo[total - 1] >> 16) == 0)
            --total;
        while (total != 0 && (int)(*scanInfo >> 16) == 0)
        {
            --total;
            ++scanInfo;
            ++yStart;
        }
        if (yStart + total > scissor->y1 &&
            grAssert("yStart + numScanlines <= scissor->y1",
                     "grScanConvert.inl", 359) != 0)
            grDie(0);
        if (hgInRange32(yStart, scissor->y0, scissor->y1) == 0 &&
            grAssert("hgInRange32(yStart, scissor->y0, scissor->y1)",
                     "grScanConvert.inl", 360) != 0)
            grDie(0);
        if (hgInRange32(total, 0, GR_MAX_DISPLAY_DIMENSION) == 0 &&
            grAssert("hgInRange32(numScanlines, 0, GR_MAX_DISPLAY_DIMENSION)",
                     "grScanConvert.inl", 361) != 0)
            grDie(0);
        _grPixelPipe *pipe = (_grPixelPipe *)state->pixelPipeline;

        pipe->yStart = yStart;
        pipe->numScanlines = total;
        pipe->scanlineInfo = scanInfo;
    }
    return total;
}

/* --- grInterpolateVertexGradients (grRasterizer.inl 251; DLL 25530-25899) -----
 * Interpolates the per-vertex attributes of a clip-generated vertex from the
 * triangle's three source records. The barycentric weights ride in the new
 * vertex's normal[0..1] (the clipFactor alias); c = ONE-a-b is computed
 * locally. Attributes use (a*t0 + b*t1 + c*t2) >> 29 with IUNITSCALAR_ONE
 * weights. The trivial case (a==ONE, b==ONE, or a==b==0) copies attributes
 * from the corresponding source vertex.
 * Depth (mask bit 0) is NOT handled here - it is a per-scanline gradient. */

void grInterpolateVertexGradients(Gerbera_s *state, _grVertex *vertex,
                                  unsigned int origIndices, int side)
{
    _grVertex *v0 = GR_VERTEX_BUFFER(state) + grUnpackIndex(origIndices, 0);
    _grVertex *v1 = GR_VERTEX_BUFFER(state) + grUnpackIndex(origIndices, 1);
    _grVertex *v2 = GR_VERTEX_BUFFER(state) + grUnpackIndex(origIndices, 2);
    unsigned int gradientMask = (unsigned int)state->activeGradientMask;
    int a = vertex->normal[0];
    int b = vertex->normal[1];

    if ((state == NULL || vertex == NULL) &&
        grAssert("state && vertex", "grRasterizer.inl", 251) != 0)
        grDie(0);
    if ((vertex->stateMask & 0x40) == 0 &&
        grAssert("vertex->stateMask & GR_VERTEXMASK_INTERPOLATE_GRADIENTS",
                 "grRasterizer.inl", 253) != 0)
        grDie(0);
    if ((vertex->stateMask & 0xC) == 0 &&
        grAssert("vertex->stateMask & (GR_VERTEXMASK_TRANSFORMED | GR_VERTEXMASK_VIEWPORT_TRANSFORMED)",
                 "grRasterizer.inl", 254) != 0)
        grDie(0);
    if ((vertex->stateMask & 3) == 0 &&
        grAssert("vertex->stateMask & (GR_VERTEXMASK_SURVIVED_2D_REJECTION | GR_VERTEXMASK_SURVIVED_3D_REJECTION)",
                 "grRasterizer.inl", 255) != 0)
        grDie(0);
    vertex->stateMask = (unsigned short)((vertex->stateMask & ~0x40u) | 0x20u);
    if (hgInRange32(a, 0, IUNITSCALAR_ONE) == 0 &&
        grAssert("hgInRange32(a, 0, IUNITSCALAR_ONE)", "grRasterizer.inl", 259) != 0)
        grDie(0);
    if (hgInRange32(b, 0, IUNITSCALAR_ONE) == 0 &&
        grAssert("hgInRange32(b, 0, IUNITSCALAR_ONE)", "grRasterizer.inl", 260) != 0)
        grDie(0);
    if (hgInRange32(a + b, 0, IUNITSCALAR_ONE) == 0 &&
        grAssert("hgInRange32(a+b, 0, IUNITSCALAR_ONE)", "grRasterizer.inl", 261) != 0)
        grDie(0);
    if (((b | a) & 0x1FFFFFFF) != 0)
    {
        int c = IUNITSCALAR_ONE - a - b;

        if ((a == IUNITSCALAR_ONE || b == IUNITSCALAR_ONE || (a == 0 && b == 0)) &&
            grAssert("a!=IUNITSCALAR_ONE && b!=IUNITSCALAR_ONE && (a!=0 || b!=0)",
                     "grRasterizer.inl", 303) != 0)
            grDie(0);
#define GR_INTERP_TEXCOORD(u_)                                                  \
        do {                                                                    \
            int t0y = v0->texCoord[u_].y;                                       \
            int t1y = v1->texCoord[u_].y;                                       \
            int t2y = v2->texCoord[u_].y;                                       \
            int gy = (t0y == t1y && t0y == t2y) ? t0y :                         \
                (int)imLsl64h_0_31OF(                                           \
                    hgMadd64(hgMadd64(hgMul64(a, t0y), b, t1y), c, t2y), 3);    \
            int t0x = v0->texCoord[u_].x;                                       \
            int t1x = v1->texCoord[u_].x;                                       \
            int t2x = v2->texCoord[u_].x;                                       \
            int gx = (t0x == t1x && t0x == t2x) ? t0x :                         \
                (int)imLsl64h_0_31OF(                                           \
                    hgMadd64(hgMadd64(hgMul64(a, t0x), b, t1x), c, t2x), 3);    \
            if (hgInRange32(gy, hgMin32(hgMin32(t0y, t1y), t2y),                \
                            hgMax32(hgMax32(t0y, t1y), t2y)) == 0 &&            \
                grAssert("hgInRange32(g, hgMin32(hgMin32(t0, t1), t2), hgMax32(hgMax32(t0, t1), t2))", \
                         "grRasterizer.inl", 332) != 0)                         \
                grDie(0);                                                       \
            if (hgInRange32(gx, hgMin32(hgMin32(t0x, t1x), t2x),                \
                            hgMax32(hgMax32(t0x, t1x), t2x)) == 0 &&            \
                grAssert("hgInRange32(g, hgMin32(hgMin32(t0, t1), t2), hgMax32(hgMax32(t0, t1), t2))", \
                         "grRasterizer.inl", 332) != 0)                         \
                grDie(0);                                                       \
            vertex->texCoord[u_].y = gy;                                        \
            vertex->texCoord[u_].x = gx;                                        \
        } while (0)

        if ((gradientMask & 0xC0u) != 0)
            GR_INTERP_TEXCOORD(0);
        if ((gradientMask & 0x300u) != 0)
            GR_INTERP_TEXCOORD(1);
#undef GR_INTERP_TEXCOORD
        if ((gradientMask & 2) != 0)
        {
            int t0 = v0->color.final[1][3];
            int t1 = v1->color.final[1][3];
            int t2 = v2->color.final[1][3];
            int g = (t0 == t1 && t0 == t2) ? t0 :
                (int)imLsl64h_0_31OF(hgMadd64(hgMadd64(hgMul64(a, t0), b, t1), c, t2), 3);
            int rangeMin = hgMin32(hgMin32(t0, t1), t2);
            int rangeMax = hgMax32(hgMax32(t0, t1), t2);

            if (hgInRange32(g, 0, 0xFFFF) == 0 &&
                grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 341) != 0)
                grDie(0);
            if (hgInRange32(g, rangeMin, rangeMax) == 0 &&
                grAssert("hgInRange32(g, hgMin32(hgMin32(v0->color.final[1][3], v1->color.final[1][3]), v2->color.final[1][3]), hgMax32(hgMax32(v0->color.final[1][3], v1->color.final[1][3]), v2->color.final[1][3]))",
                         "grRasterizer.inl", 343) != 0)
                grDie(0);
            vertex->color.final[1][3] = (unsigned short)g;
        }
        if ((gradientMask & 0x1C) != 0)
        {
            const unsigned short *c0 = v0->color.final[side];
            const unsigned short *c1 = v1->color.final[side];
            const unsigned short *c2 = v2->color.final[side];
            unsigned short *cd = vertex->color.final[side];
#define GR_INTERP_COMP(comp_)                                                   \
            if ((gradientMask & (4u << (comp_))) != 0)                          \
            {                                                                   \
                int t0 = c0[comp_];                                             \
                int t1 = c1[comp_];                                             \
                int t2 = c2[comp_];                                             \
                int g = (t0 == t1 && t0 == t2) ? t0 :                           \
                    (int)imLsl64h_0_31OF(                                       \
                        hgMadd64(hgMadd64(hgMul64(a, t0), b, t1), c, t2), 3);   \
                int rangeMin = hgMin32(hgMin32(t0, t1), t2);                    \
                int rangeMax = hgMax32(hgMax32(t0, t1), t2);                    \
                if (hgInRange32(g, 0, 0xFFFF) == 0 &&                           \
                    grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 371) != 0) \
                    grDie(0);                                                   \
                if (hgInRange32(g, rangeMin, rangeMax) == 0 &&                  \
                    grAssert("hgInRange32(g, hgMin32(hgMin32(g0, g1), g2), hgMax32(hgMax32(g0, g1), g2))", \
                             "grRasterizer.inl", 373) != 0)                     \
                    grDie(0);                                                   \
                cd[comp_] = (unsigned short)g;                                  \
            }
            GR_INTERP_COMP(2);
            GR_INTERP_COMP(1);
            GR_INTERP_COMP(0);
#undef GR_INTERP_COMP
        }
        if ((gradientMask & 0x20) != 0)
        {
            int t0 = v0->color.final[0][3];
            int t1 = v1->color.final[0][3];
            int t2 = v2->color.final[0][3];
            int g = (t0 == t1 && t0 == t2) ? t0 :
                (int)imLsl64h_0_31OF(hgMadd64(hgMadd64(hgMul64(a, t0), b, t1), c, t2), 3);
            int rangeMin = hgMin32(hgMin32(t0, t1), t2);
            int rangeMax = hgMax32(hgMax32(t0, t1), t2);

            if (hgInRange32(g, 0, 0xFFFF) == 0 &&
                grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 384) != 0)
                grDie(0);
            if (hgInRange32(g, rangeMin, rangeMax) == 0 &&
                grAssert("hgInRange32(g, hgMin32(hgMin32(v0->color.final[0][3], v1->color.final[0][3]), v2->color.final[0][3]), hgMax32(hgMax32(v0->color.final[0][3], v1->color.final[0][3]), v2->color.final[0][3]))",
                         "grRasterizer.inl", 386) != 0)
                grDie(0);
            vertex->color.final[0][3] = (unsigned short)g;
        }
    }
    else
    {
        _grVertex *rec;

        if (a == IUNITSCALAR_ONE)
            rec = v0;
        else if (b == IUNITSCALAR_ONE)
            rec = v1;
        else
            rec = v2;
        if ((a != IUNITSCALAR_ONE && b != IUNITSCALAR_ONE && (a != 0 || b != 0)) &&
            grAssert("a==IUNITSCALAR_ONE || b==IUNITSCALAR_ONE || (a==0 && b==0)",
                     "grRasterizer.inl", 277) != 0)
            grDie(0);
        vertex->color = rec->color;
        vertex->texCoord[0].x = rec->texCoord[0].x;
        vertex->texCoord[0].y = rec->texCoord[0].y;
        vertex->texCoord[1].x = rec->texCoord[1].x;
        vertex->texCoord[1].y = rec->texCoord[1].y;
    }
}

/* --- DLL data tables (data.fragments.c 56-66) --------------------------------
 * Corner signs and point-sprite texcoords for the 4-corner quads built by
 * grDrawClippedLine / grDrawClippedPoint (corners 64..67; xSign/ySign give
 * the quadrant per corner). 2097151 = 0x1FFFFF max texcoord. */

static const signed char g_xSign[4] = { -1, -1, 1, 1 };
static const signed char g_ySign[4] = { -1, 1, 1, -1 };
static const signed char g_lineSign[4] = { 1, -1, -1, 1 };
static const int g_xTexCoord[4] = { 0, 0, 2097151, 2097151 };
static const int g_yTexCoord[4] = { 0, 2097151, 2097151, 0 };

/* corner index bytes of 1128415552 (0x43424140): vertex-cache slots 64..67 */
#define GR_QUAD_CORNER_BYTES 1128415552u

/* --- grDrawClippedLine (grRasterizer.inl 931; DLL 26391-26556) ----------------
 * Builds the thick-line quad in the vertex cache (slots 64..67): each
 * endpoint contributes two corners offset by the rounded half-width, with a
 * slope term slanting the near/far ends and a +-2048 (half-pixel) snap along
 * the major axis. Width displaces only the minor axis: x86 00425C69 and
 * ARM 00016F4C initialize the major-axis displacement to zero. */

void grDrawClippedLine(Gerbera_s *state, _grClipTriangle *tri,
                       unsigned int origIndices)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    _grVertex *rec0;
    _grVertex *rec1;
    int i0 = grUnpackIndex(tri->indices, 0);
    int i1 = grUnpackIndex(tri->indices, 1);
    int dx, dy, steep, lineWidth, hw, slope, major, minor;
    HGint64 r;
    int sh, snapX, snapY, offX, offY;
    int k;

    if ((state == NULL || tri == NULL) &&
        grAssert("state && tri", "grRasterizer.inl", 931) != 0)
        grDie(0);
    rec0 = vertexBuffer + i0;
    rec1 = vertexBuffer + i1;
    if (grTestCapability(state, GR_CAPABILITY_LINE_SMOOTH) != 0)
        lineWidth = 0x10000;
    else
        lineWidth = hgMax32((state->lineWidth + 0x8000) & 0xFFFF0000, 0x10000);
    dx = rec1->screenPosition[0] - rec0->screenPosition[0];
    dy = rec1->screenPosition[1] - rec0->screenPosition[1];
    steep = hgAbs32(dx) < hgAbs32(dy);
    if (dx != 0 || dy != 0)
    {
        major = steep ? dy : dx;
        minor = steep ? dx : dy;
        r = imRcpu((unsigned int)hgAbs32(major));
        sh = (int)(unsigned int)r;                  /* low: shift */
        slope = (hgLsl64h_0_31(hgMul64((int)(r >> 32), minor), sh) >> 19) &
                (int)0xFFFFFF00u;
        hw = lineWidth >> 5;
        if (steep != 0)
        {
            offX = hw;                              /* perpendicular in X */
            snapX = slope;                          /* slant term on X */
            snapY = 2048;                           /* half-pixel snap on Y */
            offY = 0;
        }
        else
        {
            offY = -hw;                             /* perpendicular in Y */
            snapY = slope;                          /* slant term on Y */
            snapX = 2048;                           /* half-pixel snap on X */
            offX = 0;
        }
        /* the endpoints carry their barycentric weights for the corner
         * copies: orig vertex 0 -> (1,0), orig vertex 1 -> (0,1) */
        vertexBuffer[grUnpackIndex(origIndices, 0)].normal[0] = IUNITSCALAR_ONE;
        vertexBuffer[grUnpackIndex(origIndices, 0)].normal[1] = 0;
        vertexBuffer[grUnpackIndex(origIndices, 1)].normal[0] = 0;
        vertexBuffer[grUnpackIndex(origIndices, 1)].normal[1] = IUNITSCALAR_ONE;
        /* x86 00425D27 / ARM 00016FB8 negate the half-pixel offset when
         * the first endpoint is farther along the major axis. */
        if (grVector3iGet((_grVector3i *)&rec0->screenPosition[0], steep) >
            grVector3iGet((_grVector3i *)&rec1->screenPosition[0], steep))
        {
            snapX = -snapX;
            snapY = -snapY;
        }
        for (k = 3; k >= 0; --k)
        {
            _grVertex *corner = vertexBuffer +
                (unsigned char)(GR_QUAD_CORNER_BYTES >> (8 * k));
            _grVertex *src = ((k & 2) != 0) ? rec1 : rec0;

            corner->normal[0] = src->normal[0];
            corner->normal[1] = src->normal[1];
            corner->stateMask = 79;
            IVector4_copy((_IVector4 *)&corner->clipPosition,
                          (_IVector4 *)&src->clipPosition);
            corner->screenPosition[0] = snapX + src->screenPosition[0] +
                                        offX * g_lineSign[k];
            corner->screenPosition[1] = snapY + src->screenPosition[1] +
                                        offY * g_lineSign[k];
            corner->screenPosition[2] = src->screenPosition[2];
            grInterpolateVertexGradients(state, corner, origIndices, tri->side);
        }
        {
            _grClipTriangle quad;
            HGint64 area64;

            quad.side = tri->side;
            quad.indices = grSetIndices(64, 65, 66);
            area64 = grComputeTriangleArea(vertexBuffer, quad.indices);
            quad.area = grRoundTriangleArea(area64);
            if (quad.area == 0 &&
                grAssert("newTri.area != 0", "grRasterizer.inl", 1067) != 0)
                grDie(0);
            grrDrawClippedTriangle(state, &quad, origIndices);
            quad.indices = grSetIndices(66, 67, 64);
            if (quad.area != grRoundTriangleArea(
                    grComputeTriangleArea(vertexBuffer, quad.indices)) &&
                grAssert("newTri.area == grRoundTriangleArea(grComputeTriangleArea(vertexCache, newTri.indices))",
                         "grRasterizer.inl", 1073) != 0)
                grDie(0);
            grrDrawClippedTriangle(state, &quad, origIndices);
            grDisableCapability(state, GR_CAPABILITY_UNUSED);
        }
    }
}

/* --- grDrawClippedPoint (grRasterizer.inl 1106; DLL 26559-26698) --------------
 * Builds the point sprite quad (slots 64..67) around the point vertex,
 * sizes it (point size array / attenuation / smooth / clamp / rounding) and
 * draws it as two triangles. Point-sprite texcoords replace the interpolated
 * ones per enabled texture unit. */

void grDrawClippedPoint(Gerbera_s *state, _grClipTriangle *tri,
                        unsigned int origIndices)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    _grVertex *rec;
    int i0;
    int x, y, z;
    int size, odd, half;
    int k, u;

    i0 = grUnpackIndex(tri->indices, 0);
    if ((state == NULL || tri == NULL) &&
        grAssert("state && tri", "grRasterizer.inl", 1106) != 0)
        grDie(0);
    if ((i0 != grUnpackIndex(tri->indices, 1) ||
         i0 != grUnpackIndex(tri->indices, 2)) &&
        grAssert("i0==grUnpackIndex(tri->indices, 1) && i0==grUnpackIndex(tri->indices, 2)",
                 "grRasterizer.inl", 1107) != 0)
        grDie(0);
    if (((vertexBuffer[i0].stateMask & 0x40) != 0) &&
        grAssert("!(vertexCache[i0].stateMask & GR_VERTEXMASK_INTERPOLATE_GRADIENTS)",
                 "grRasterizer.inl", 1108) != 0)
        grDie(0);
    rec = vertexBuffer + i0;
    x = rec->screenPosition[0] - 2048;
    y = rec->screenPosition[1] - 2048;
    z = rec->screenPosition[2];
    if (grTestCapability(state, GR_CAPABILITY_POINT_SMOOTH) != 0)
        size = 0x10000;
    else if (grTestCapability(state, GR_CAPABILITY_POINT_SIZE_ARRAY) != 0 &&
             state->vertexArray[5].data != NULL)
        size = rec->pointSize;
    else
        size = state->pointSize;
    if (state->computePointDistAttn != 0)
    {
        int dist = IVector3_length((_IVector3 *)&rec->position);
        int attn = IScalar_attenuateRsq(dist, &state->pointDistAttn);

        size = IScalar_scalarToFixed(
            IScalar_mul(IScalar_fixedToScalar(size, 16), attn), 16);
    }
    size = hgClamp32(size, state->pointSizeMin, state->pointSizeMax);
    size = hgMin32(size, 0x400000);
    size = hgMax32((size + 0x8000) & 0xFFFF0000, 0x10000);
    odd = (size >> 16) & 1;
    half = (size >> 5) & (int)0xFFFFF800u;
    if (odd != 0)
    {
        x = (x & (int)0xFFFFF000u) + 2048;
        y = (y & (int)0xFFFFF000u) + 2048;
    }
    else
    {
        x = (x + 2048) & (int)0xFFFFF000u;
        y = (y + 2048) & (int)0xFFFFF000u;
    }
    for (k = 3; k >= 0; --k)
    {
        _grVertex *corner = vertexBuffer +
            (unsigned char)(GR_QUAD_CORNER_BYTES >> (8 * k));

        corner->normal[0] = 0;
        corner->normal[1] = 0;
        corner->stateMask = 79;
        corner->screenPosition[0] = x + half * g_xSign[k];
        corner->screenPosition[1] = y + half * g_ySign[k];
        corner->screenPosition[2] = z;
        grInterpolateVertexGradients(state, corner, origIndices, tri->side);
        if (grGetCapability(state, GR_CAPABILITY_POINT_SPRITE) != 0)
        {
            for (u = 1; u >= 0; --u)
            {
                if (state->textureUnitEnabled[u] != 0)
                {
                    corner->texCoord[u].x = g_xTexCoord[k];
                    corner->texCoord[u].y = g_yTexCoord[k];
                }
            }
        }
    }
    {
        _grClipTriangle quad;

        quad.side = tri->side;
        quad.indices = grSetIndices(64, 65, 66);
        quad.area = grRoundTriangleArea(
            grComputeTriangleArea(vertexBuffer, quad.indices));
        grDrawClippedTriangle(state, &quad, origIndices, 8);
        quad.indices = grSetIndices(66, 67, 64);
        grDrawClippedTriangle(state, &quad, origIndices, 8);
    }
}

/* --- grSetupTextureUnit (grRasterizer.inl 69; DLL 25345-25526) ------------- */

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
void grSetupTextureUnit(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
                        int v0sx, int v0sy)
{
    _grPixelPipe * restrict pipe = GR_PIXEL_PIPE(state);
    const _grTexture * restrict texture = state->textureUnit[2 * texIndex].texture;
    unsigned int activeGradMask = (unsigned int)state->activeGradientMask;
    int isMag = 1;
    int mipLevel = 0;
    unsigned char filterByte;
    int linear;
    const _grMipLevel * restrict mipmap;
    _grPixelPipeTextureUnit * restrict tex;
    int log2S, log2T;
    int i;

    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS-1)", "grRasterizer.inl", 69) != 0)
        grDie(0);
    if (texture == NULL &&
        grAssert("texture", "grRasterizer.inl", 70) != 0)
        grDie(0);

    if ((texture->parmMask & 0x20u) != 0 &&
        ((texture->filterMask[GR_FILTERTYPE_MIN] & GR_FILTERMASK_USE_MIPMAPS) != 0 ||
         texture->filterMask[GR_FILTERTYPE_MAG] != texture->filterMask[GR_FILTERTYPE_MIN]))
    {
        HGuint64 maxRho2 = hgToUnsigned64(hgZero64());
        for (i = 1; i >= 0; --i)
        {
            if (((64 << (i + 2 * texIndex)) & (int)activeGradMask) != 0)
            {
                int gradIdx = i + 6 + 2 * texIndex;
                int xAdd = pipe->xAdder[gradIdx];
                int shift = texture->mipmap[0].log2Dim[i];
                int yAdd = pipe->yAdder[gradIdx];
                HGint64 sumSq = hgMadd64(hgMul64(xAdd, xAdd), yAdd, yAdd);
                int rsh = hgClamp32_0_x(2 * (10 - shift + g0[i][1] - 3), 63);
                HGuint64 rho2 = hgToUnsigned64(hgLsr64(sumSq, rsh));

                if (shift > 20 &&
                    grAssert("shift <= 20", "grRasterizer.inl", 106) != 0)
                    grDie(0);
                if (hgLessThan64u(maxRho2, rho2) != 0)
                    maxRho2 = rho2;
            }
        }
        if (hgGet64uh(maxRho2) < 0x8000u)
        {
            unsigned int scaled = (unsigned int)hgLsl64h_0_31(hgToSigned64(maxRho2), 16);
            int adj = (int)((scaled >> 3) + scaled);
            mipLevel = hgMaxZero32((20 - hgClz32((unsigned int)adj)) >> 1);
            if (mipLevel > 0)
            {
                isMag = 0;
                mipLevel = hgMin32(mipLevel, texture->numMipLevels - 1);
            }
        }
        else
        {
            isMag = 0;
            mipLevel = texture->numMipLevels - 1;
        }
        if ((texture->filterMask[GR_FILTERTYPE_MIN] & GR_FILTERMASK_USE_MIPMAPS) == 0)
            mipLevel = 0;
    }

    if (hgInRange32(mipLevel, 0, texture->numMipLevels - 1) == 0 &&
        grAssert("hgInRange32(mipLevel, 0, texture->numMipLevels-1)", "grRasterizer.inl", 139) != 0)
        grDie(0);

    filterByte = texture->filterMask[isMag != 0 ? GR_FILTERTYPE_MAG : GR_FILTERTYPE_MIN];
    linear = (filterByte & GR_FILTERMASK_FILTER) != 0;
    mipmap = &texture->mipmap[mipLevel];
    tex = &pipe->textureUnit[texIndex];
    log2S = mipmap->log2Dim[GR_TEXCOORD_S];
    log2T = mipmap->log2Dim[GR_TEXCOORD_T];

    if (mipmap->pixels == NULL &&
        grAssert("mipmap->pixels", "grRasterizer.inl", 152) != 0)
        grDie(0);
    if (log2S < 0 &&
        grAssert("mipmap->log2Dim[GR_TEXCOORD_S] >= 0", "grRasterizer.inl", 153) != 0)
        grDie(0);
    if (log2T < 0 &&
        grAssert("mipmap->log2Dim[GR_TEXCOORD_T] >= 0", "grRasterizer.inl", 154) != 0)
        grDie(0);

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC unroll 0
#endif
    for (i = 1; i >= 0; --i)
    {
        if (((64 << (i + 2 * texIndex)) & (int)activeGradMask) != 0)
        {
            int gradIdx = i + 6 + 2 * texIndex;
            int l2 = i == 0 ? log2S : log2T;
            if (l2 != 0)
            {
                int bias = 0;
                int xAdd = pipe->xAdder[gradIdx];
                int yAdd = pipe->yAdder[gradIdx];
                int g0val = g0[i][0];
                int g0sh = g0[i][1];
                int ref;
                int lsh;
                int baseShift;
                unsigned int wrap = (unsigned int)texture->wrapMode[i];

                if (linear)
                {
                    int relShift = l2 - g0sh;
                    bias = (int)(relShift >= 0 ? 0x100000u >> relShift : 0x100000u << -relShift);
                }

                {
                    HGint64 v5 = hgMadd64(hgMul64(v0sx, xAdd), v0sy, yAdd);
                    ref = (int)((unsigned int)g0val -
                                (unsigned int)hgLsl64h_0_31(v5, 20) -
                                (unsigned int)bias);
                }
                if (wrap == GR_WRAPMODE_CLAMP_TO_EDGE)
                {
                    lsh = 0;
                    baseShift = g0sh + 21;
                }
                else
                {
                    if (wrap > GR_WRAPMODE_MIRRORED_REPEAT)
                        imAssert("0", "grRasterizer.inl", 183);
                    baseShift = 32 - (int)(wrap >> 1);
                    lsh = baseShift - 21 - g0sh;
                }
                pipe->xAdder[gradIdx] = (int)((unsigned int)xAdd << lsh);
                pipe->yAdder[gradIdx] = (int)((unsigned int)yAdd << lsh);
                pipe->reference[gradIdx] = (int)((unsigned int)ref << lsh);
                tex->rShift[i] = baseShift - l2;
                if ((unsigned int)tex->rShift[i] > 31u &&
                    grAssert("tex->rShift[i] >= 0 && tex->rShift[i] <= 31",
                             "grRasterizer.inl", 202) != 0)
                    grDie(0);
            }
            else
            {
                pipe->xAdder[gradIdx] = 0;
                pipe->yAdder[gradIdx] = 0;
                pipe->reference[gradIdx] = 0;
                tex->rShift[i] = 0;
            }
        }
    }

    tex->pixels = mipmap->pixels;
    tex->sMask = (1 << log2S) - 1;
    tex->tMask = (1 << log2T) - 1;
    tex->log2DimS = log2S;
    tex->linear = linear;
    tex->palette = texture->palette;
}
