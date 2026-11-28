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

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
typedef char gr_check_gi_ppp_[offsetof(_grGradientInfo, pPP) == 0 ? 1 : -1];
typedef char gr_check_gi_v0x_[offsetof(_grGradientInfo, v0sx) == 4 ? 1 : -1];
typedef char gr_check_gi_v0y_[offsetof(_grGradientInfo, v0sy) == 8 ? 1 : -1];
typedef char gr_check_gi_01x_[offsetof(_grGradientInfo, v01sxPerArea) == 12 ? 1 : -1];
typedef char gr_check_gi_01y_[offsetof(_grGradientInfo, v01syPerArea) == 16 ? 1 : -1];
typedef char gr_check_gi_02x_[offsetof(_grGradientInfo, v02sxPerArea) == 20 ? 1 : -1];
typedef char gr_check_gi_02y_[offsetof(_grGradientInfo, v02syPerArea) == 24 ? 1 : -1];
typedef char gr_check_gi_sh_[offsetof(_grGradientInfo, shift) == 28 ? 1 : -1];

static void grSetupGradientNZ(const _grGradientInfo * restrict info, int gradient,
                              int g01, int g02, int reference)
    __asm__("grSetupGradientNZ_asm");

__attribute__((naked, noinline, used))
static void grSetupGradientNZ(const _grGradientInfo * restrict info, int gradient,
                              int g01, int g02, int reference)
{
    (void)info;
    (void)gradient;
    (void)g01;
    (void)g02;
    (void)reference;
    __asm__ __volatile__(
        "push  {r4, r5, r6, r7, r8, r9}\n\t"
        "add   ip, r0, #12\n\t"
        "ldmia ip, {r4, r5, r6, r7, r8}\n\t"
        "rsb   ip, r3, #0\n\t"
        "smull ip, r9, r4, ip\n\t"
        "smlal ip, r9, r6, r2\n\t"
        "rsb   r2, r2, #0\n\t"
        "smull r6, r4, r5, r3\n\t"
        "smlal r6, r4, r7, r2\n\t"
        "add   r8, r8, #14\n\t"
        "cmp   r8, #31\n\t"
        "bgt   2f\n\t"
        "rsb   r2, r8, #32\n\t"
        "asr   r5, r4, r8\n\t"
        "lsl   r3, r4, r2\n\t"
        "orr   r3, r3, r6, lsr r8\n\t"
        "asr   r7, r9, r8\n\t"
        "lsl   r2, r9, r2\n\t"
        "orr   r2, r2, ip, lsr r8\n\t"
        "3:\n\t"
        "eor   r5, r5, r3, asr #31\n\t"
        "eor   r7, r7, r2, asr #31\n\t"
        "orrs  r5, r5, r7\n\t"
        "bne   4f\n\t"
        "ldmia r0, {r0, r4, r5}\n\t"
        "smull r7, r8, r3, r4\n\t"
        "smlal r7, r8, r2, r5\n\t"
        "5:\n\t"
        "ldr   r6, [sp, #24]\n\t"
        "lsr   r7, r7, #12\n\t"
        "orr   r7, r7, r8, lsl #20\n\t"
        "sub   r6, r6, r7\n\t"
        "add   r1, r0, r1, lsl #2\n\t"
        "str   r6, [r1, #192]\n\t"
        "str   r3, [r1, #232]\n\t"
        "str   r2, [r1, #272]\n\t"
        "pop   {r4, r5, r6, r7, r8, r9}\n\t"
        "bx    lr\n\t"
        "2:\n\t"
        "sub   r2, r8, #32\n\t"
        "asr   r5, r4, #31\n\t"
        "asr   r3, r4, r2\n\t"
        "asr   r7, r9, #31\n\t"
        "asr   r2, r9, r2\n\t"
        "b     3b\n\t"
        "4:\n\t"
        "cmp   r8, #32\n\t"
        "movge r5, r4, asr #31\n\t"
        "movlt r5, r4, asr r8\n\t"
        "movge r7, r9, asr #31\n\t"
        "movlt r7, r9, asr r8\n\t"
        "ldmia r0, {r0, r4, r6}\n\t"
        "umull r8, r9, r3, r4\n\t"
        "mla   r9, r5, r4, r9\n\t"
        "and   ip, r3, r4, asr #31\n\t"
        "sub   r9, r9, ip\n\t"
        "umull r4, r5, r2, r6\n\t"
        "mla   r5, r7, r6, r5\n\t"
        "and   ip, r2, r6, asr #31\n\t"
        "sub   r5, r5, ip\n\t"
        "adds  r7, r8, r4\n\t"
        "adc   r8, r9, r5\n\t"
        "b     5b\n\t"
    );
}

__attribute__((naked))
void grSetupGradient(const _grGradientInfo * restrict info, int gradient, int g01, int g02,
                     int reference)
{
    (void)info;
    (void)gradient;
    (void)g01;
    (void)g02;
    (void)reference;
    __asm__ __volatile__(
        "orrs  ip, r2, r3\n\t"
        "bne   grSetupGradientNZ_asm\n\t"
        "ldr   ip, [r0]\n\t"
        "ldr   r2, [sp, #0]\n\t"
        "add   r1, ip, r1, lsl #2\n\t"
        "str   r2, [r1, #192]\n\t"
        "str   r3, [r1, #232]\n\t"
        "str   r3, [r1, #272]\n\t"
        "bx    lr\n\t"
    );
}
#else
#define grSetupGradientNZ grSetupGradient
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
#endif

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
        syDelta = hgMadd64(syDelta, info->v02syPerArea,
                           (int)(0u - (unsigned int)g01));
        if (__builtin_expect(info->shift < 18, 1))
        {
            int sh = 18 - info->shift; /* 1..18 because info->shift in 0..17 */
            int rsh = 32 - sh;         /* 14..31 */
            xAdder = (int)(((unsigned int)hgGet64h(syDelta) << sh) |
                           ((unsigned int)hgGet64l(syDelta) >> rsh));
        }
        else
        {
            int sh = info->shift - 18;
            xAdder = hgGet64h(syDelta) >> sh;
        }
        {
            HGint64 sxDelta = hgMul64(info->v02sxPerArea, g01);
            sxDelta = hgMadd64(sxDelta, info->v01sxPerArea,
                               (int)(0u - (unsigned int)g02));
            if (__builtin_expect(info->shift < 18, 1))
            {
                int sh = 18 - info->shift;
                int rsh = 32 - sh;
                yAdder = (int)(((unsigned int)hgGet64h(sxDelta) << sh) |
                               ((unsigned int)hgGet64l(sxDelta) >> rsh));
            }
            else
            {
                int sh = info->shift - 18;
                yAdder = hgGet64h(sxDelta) >> sh;
            }
        }
    }
    info->pPP->xAdder[gradient] = xAdder;
    info->pPP->yAdder[gradient] = yAdder;
}

static inline void grComputeDeltasSeq(const _grGradientInfo *info, int gradient, int g01, int g02)
{
    if (g01 != 0 || g02 != 0)
    {
        int xAdder;
        int yAdder;
        HGint64 syDelta = hgMul64(info->v01syPerArea, g02);
        HGint64 sxDelta;

        syDelta = hgMadd64(syDelta, info->v02syPerArea,
                           (int)(0u - (unsigned int)g01));
        if (__builtin_expect(info->shift < 18, 1))
        {
            int sh = 18 - info->shift;
            int rsh = 32 - sh;
            xAdder = (int)(((unsigned int)hgGet64h(syDelta) << sh) |
                           ((unsigned int)hgGet64l(syDelta) >> rsh));
        }
        else
        {
            xAdder = hgGet64h(syDelta) >> (info->shift - 18);
        }
        sxDelta = hgMul64(info->v02sxPerArea, g01);
        sxDelta = hgMadd64(sxDelta, info->v01sxPerArea,
                           (int)(0u - (unsigned int)g02));
        if (__builtin_expect(info->shift < 18, 1))
        {
            int sh = 18 - info->shift;
            int rsh = 32 - sh;
            yAdder = (int)(((unsigned int)hgGet64h(sxDelta) << sh) |
                           ((unsigned int)hgGet64l(sxDelta) >> rsh));
        }
        else
        {
            yAdder = hgGet64h(sxDelta) >> (info->shift - 18);
        }
        info->pPP->xAdder[gradient] = xAdder;
        info->pPP->yAdder[gradient] = yAdder;
    }
    else
    {
        info->pPP->xAdder[gradient] = 0;
        info->pPP->yAdder[gradient] = 0;
    }
}

/* --- grSetupTriangleGradients (grRasterizer.inl 603; DLL 26064-26248) --------
 *
 * gradientMask bits (state->activeGradientMask convention): 0x1 depth,
 * 0x2 fog, 0x3C RGBA (4<<comp), 0xC0 tex0 s/t, 0x300 tex1 s/t, 0x20 alpha
 * (inside the 0x3C walk). The texcoord walk runs texIndex 1..0 and, per
 * unit, coord 1..0 (T then S) - gradient index = coord + 2*texIndex + 6.
 */

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
static void grSetupTextureUnitCoord(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
                                    int v0sx, int v0sy)
    __asm__("grSetupTextureUnitCoord_asm");
static void grSetupTextureUnit0AndDeltas(Gerbera_s * restrict state, const _grGradientInfo * restrict info,
                                         const _grVertex * restrict v0, const _grVertex * restrict v1,
                                         const _grVertex * restrict v2)
    __asm__("grSetupTextureUnit0AndDeltas_asm");
#else
static void grSetupTextureUnitCoord(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
                                    int v0sx, int v0sy);
#endif

static int grSetupTriangleGradientsVisibleVertsAtY_slow(Gerbera_s *state, _grClipTriangle *tri,
                                                      _grVertex *v0, _grVertex *v1, _grVertex *v2,
                                                      int flatShadeIndex, unsigned int gradientMask,
                                                      int rejectDepth, int advanceY)
    __attribute__((noinline));

static int grSetupTriangleGradientsVisibleVertsAtY_slow(Gerbera_s *state, _grClipTriangle *tri,
                                                      _grVertex *v0, _grVertex *v1, _grVertex *v2,
                                                      int flatShadeIndex, unsigned int gradientMask,
                                                      int rejectDepth, int advanceY)
{
    _grPixelPipe *pixelPipe = GR_PIXEL_PIPE(state);
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    HGint64 reciprocal = imRcpu_inline((unsigned int)hgAbs32(tri->area));
    int recipShift = hgGet64l(reciprocal);
    int signedRecip = hgCopySign32(hgGet64h(reciprocal), -tri->area);
    int v0sx = v0->screenPosition[0] - 4096;
    int v0sy = v0->screenPosition[1] - 4096;
    _grGradientInfo info;
    int texIndex;
    int coord;

    info.pPP = pixelPipe;
    info.shift = 31 - recipShift;
    info.v01sxPerArea = hgMul64h(16 * (v1->screenPosition[0] - v0->screenPosition[0]), signedRecip);
    info.v01syPerArea = hgMul64h(16 * (v1->screenPosition[1] - v0->screenPosition[1]), signedRecip);
    info.v02sxPerArea = hgMul64h(16 * (v2->screenPosition[0] - v0->screenPosition[0]), signedRecip);
    info.v02syPerArea = hgMul64h(16 * (v2->screenPosition[1] - v0->screenPosition[1]), signedRecip);
    info.v0sx = v0sx;
    info.v0sy = v0sy;

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
            grSetupGradientNZ(&info, GR_GRADIENT_DEPTH, d1 - d0, d2 - d0, d0);
            pixelPipe->reference[GR_GRADIENT_DEPTH] =
                (int)((unsigned int)pixelPipe->reference[GR_GRADIENT_DEPTH] +
                      (unsigned int)advanceY * (unsigned int)pixelPipe->yAdder[GR_GRADIENT_DEPTH]);
        }
        if (grIsDepthOffsetEnabled(state, state->primitiveType) != 0)
            grComputeDepthOffset(state);
    }
    if (rejectDepth && (gradientMask & (1u << GR_GRADIENT_DEPTH)))
    {
        if (grDepthSpansOccludedFastRef(pixelPipe, rejectDepth >> 1,
                (unsigned int)pixelPipe->reference[GR_GRADIENT_DEPTH]))
            return 0;
    }
    for (texIndex = GR_GRADIENT_FOG; texIndex >= GR_GRADIENT_DEPTH; --texIndex)
    {
        if (((192 << (2 * texIndex)) & gradientMask) != 0)
        {
            int baseValue[2], preShift[2], pairs[2][2];
            int *tc0 = &v0->texCoord[texIndex].x;
            int *tc1 = &v1->texCoord[texIndex].x;
            int *tc2 = &v2->texCoord[texIndex].x;
            for (coord = GR_GRADIENT_FOG; coord >= GR_GRADIENT_DEPTH; --coord)
            {
                int d0 = tc0[coord], d1 = tc1[coord], d2 = tc2[coord];
                int shift = hgClz32(hgAbs32(d2) | (hgAbs32(d0) | hgAbs32(d1))) - 1;
                int clamped = hgClamp32_0_x(shift, 11);
                d0 = (int)((unsigned int)d0 << clamped) >> 1;
                d1 = (int)((unsigned int)d1 << clamped) >> 1;
                d2 = (int)((unsigned int)d2 << clamped) >> 1;
                preShift[coord] = clamped - 1;
                grComputeDeltas(&info, coord + 2 * texIndex + 6, d1 - d0, d2 - d0);
                baseValue[coord] = d0;
            }
            pairs[0][0] = baseValue[0];
            pairs[0][1] = preShift[0];
            pairs[1][0] = baseValue[1];
            pairs[1][1] = preShift[1];
            grSetupTextureUnit(state, texIndex, pairs, v0sx, v0sy);
            for (coord = 1; coord >= 0; --coord)
            {
                if (((64 << (coord + 2 * texIndex)) & gradientMask) != 0)
                {
                    int gradIdx = coord + 6 + 2 * texIndex;
                    pixelPipe->reference[gradIdx] =
                        (int)((unsigned int)pixelPipe->reference[gradIdx] +
                              (unsigned int)advanceY * (unsigned int)pixelPipe->yAdder[gradIdx]);
                }
            }
        }
    }
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
            grSetupGradientNZ(&info, GR_GRADIENT_FOG,
                              ((int)rf1 << 8) - f0, ((int)rf2 << 8) - f0, f0 + 128);
            pixelPipe->reference[GR_GRADIENT_FOG] =
                (int)((unsigned int)pixelPipe->reference[GR_GRADIENT_FOG] +
                      (unsigned int)advanceY * (unsigned int)pixelPipe->yAdder[GR_GRADIENT_FOG]);
        }
    }
    if ((gradientMask & 0x3C) != 0)
    {
        int side = tri->side;
        if (grTestCapability(state, GR_CAPABILITY_SMOOTH_SHADING) != 0)
        {
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
                        grSetupGradientNZ(&info, coord + 2,
                                          ((int)rc1 << 8) - c0, ((int)rc2 << 8) - c0, c0 + 0x8000);
                        pixelPipe->reference[coord + 2] =
                            (int)((unsigned int)pixelPipe->reference[coord + 2] +
                                  (unsigned int)advanceY * (unsigned int)pixelPipe->yAdder[coord + 2]);
                    }
                }
            }
            if ((gradientMask & 0x20) != 0)
            {
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
                    grSetupGradientNZ(&info, GR_GRADIENT_COLOR_A,
                                      ((int)ra1 << 8) - a0, ((int)ra2 << 8) - a0, a0 + 0x8000);
                    pixelPipe->reference[GR_GRADIENT_COLOR_A] =
                        (int)((unsigned int)pixelPipe->reference[GR_GRADIENT_COLOR_A] +
                              (unsigned int)advanceY * (unsigned int)pixelPipe->yAdder[GR_GRADIENT_COLOR_A]);
                }
            }
        }
        else
        {
            _grVertex *flat = vertexBuffer + flatShadeIndex;
            const unsigned short *flatRGB = &flat->color.final[side][0];
            for (coord = GR_GRADIENT_DEPTH; coord < GR_GRADIENT_COLOR_G; ++coord)
            {
                pixelPipe->reference[coord + 2] = ((int)flatRGB[coord] << 8) + 0x8000;
                pixelPipe->xAdder[coord + 2] = 0;
                pixelPipe->yAdder[coord + 2] = 0;
            }
            pixelPipe->reference[GR_GRADIENT_COLOR_A] =
                ((int)flat->color.final[0][3] << 8) + 0x8000;
            pixelPipe->xAdder[GR_GRADIENT_COLOR_A] = 0;
            pixelPipe->yAdder[GR_GRADIENT_COLOR_A] = 0;
        }
    }
    return 1;
}

static inline unsigned int grLoadU32Aligned4(const unsigned short *p)
{
    unsigned int v;
#if !defined(__ARM_FEATURE_UNALIGNED) && (defined(__GNUC__) || defined(__clang__))
    __builtin_memcpy(&v, __builtin_assume_aligned(p, 4), 4);
#else
    __builtin_memcpy(&v, p, 4);
#endif
    return v;
}

static inline void grStoreU32Aligned4(unsigned short *p, unsigned int v)
{
#if !defined(__ARM_FEATURE_UNALIGNED) && (defined(__GNUC__) || defined(__clang__))
    __builtin_memcpy(__builtin_assume_aligned(p, 4), &v, 4);
#else
    __builtin_memcpy(p, &v, 4);
#endif
}

typedef char gr_check_vcolor_align_[(offsetof(_grVertex, color) & 3) == 0 ? 1 : -1];

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
int grSetupTriangleGradientsVisibleVertsAtY(Gerbera_s * restrict state, _grClipTriangle * restrict tri,
                                            _grVertex * restrict v0, _grVertex * restrict v1, _grVertex * restrict v2,
                                            int flatShadeIndex, unsigned int gradientMask,
                                            int rejectDepth, int advanceY)
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
        ok = 0;
    if (ok == 0)
        grDie(0);
    if (gradientMask != 0)
    {
        _grPixelPipe *pixelPipe = GR_PIXEL_PIPE(state);
        HGint64 reciprocal;
        int recipShift;
        int signedRecip;
        int v0sx;
        int v0sy;
        _grGradientInfo info;
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
        if (__builtin_expect(((unsigned int)v0sy + 0x40000000u) >= 0x80000000u ||
                             (unsigned int)advanceY > 0xFFFFu, 0))
            return grSetupTriangleGradientsVisibleVertsAtY_slow(state, tri, v0, v1, v2,
                                                               flatShadeIndex, gradientMask,
                                                               rejectDepth, advanceY);
        v0sy = (int)((unsigned int)v0sy - ((unsigned int)advanceY << 12));
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
                grSetupGradientNZ(&info, GR_GRADIENT_DEPTH, d1 - d0, d2 - d0, d0);
            }
            if (grIsDepthOffsetEnabled(state, state->primitiveType) != 0)
                grComputeDepthOffset(state);
        }
        /* Test the exact depth plane before preparing shading attributes. */
        if (rejectDepth && (gradientMask & (1u << GR_GRADIENT_DEPTH)))
        {
            if (grDepthSpansOccludedFastRef(pixelPipe, rejectDepth >> 1,
                    (unsigned int)pixelPipe->reference[GR_GRADIENT_DEPTH]))
                return 0;
        }

        /* texcoord gradients (texIndex 1..0) */
        if (__builtin_expect((gradientMask & 0x300u) != 0, 0))
        {
            int *tc0 = &v0->texCoord[1].x;
            int *tc1 = &v1->texCoord[1].x;
            int *tc2 = &v2->texCoord[1].x;
            int pairs[2][2];

            for (coord = GR_GRADIENT_FOG; coord >= GR_GRADIENT_DEPTH; --coord)
            {
                int d0 = tc0[coord];
                int d1 = tc1[coord];
                int d2 = tc2[coord];
                int shift = hgClz32(hgAbs32(d2) | (hgAbs32(d0) | hgAbs32(d1))) - 1;
                int clamped = hgClamp32_0_x(shift, 11);

                d0 = (int)((unsigned int)d0 << clamped) >> 1;
                d1 = (int)((unsigned int)d1 << clamped) >> 1;
                d2 = (int)((unsigned int)d2 << clamped) >> 1;
                pairs[coord][1] = clamped - 1;
                grComputeDeltasSeq(&info, coord + 8, d1 - d0, d2 - d0);
                pairs[coord][0] = d0;
            }
            grSetupTextureUnitCoord(state, 1, pairs, v0sx, v0sy);
        }
        if ((gradientMask & 0xC0u) != 0)
        {
#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
            if (__builtin_expect((gradientMask & 0xC0u) == 0xC0u &&
                                 ((unsigned int)state->activeGradientMask & 0xC0u) == 0xC0u, 1))
            {
                grSetupTextureUnit0AndDeltas(state, &info, v0, v1, v2);
            }
            else
#endif
            {
                int *tc0 = &v0->texCoord[0].x;
                int *tc1 = &v1->texCoord[0].x;
                int *tc2 = &v2->texCoord[0].x;
                int pairs[2][2];

#if defined(__GNUC__) && !defined(__clang__)
                #pragma GCC unroll 2
#endif
                for (coord = GR_GRADIENT_FOG; coord >= GR_GRADIENT_DEPTH; --coord)
                {
                    int d0 = tc0[coord];
                    int d1 = tc1[coord];
                    int d2 = tc2[coord];
                    int shift = hgClz32(hgAbs32(d2) | (hgAbs32(d0) | hgAbs32(d1))) - 1;
                    int clamped = hgClamp32_0_x(shift, 11);

                    d0 = (int)((unsigned int)d0 << clamped) >> 1;
                    d1 = (int)((unsigned int)d1 << clamped) >> 1;
                    d2 = (int)((unsigned int)d2 << clamped) >> 1;
                    pairs[coord][1] = clamped - 1;
                    grComputeDeltasSeq(&info, coord + 6, d1 - d0, d2 - d0);
                    pairs[coord][0] = d0;
                }
                grSetupTextureUnitCoord(state, 0, pairs, v0sx, v0sy);
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
                grSetupGradientNZ(&info, GR_GRADIENT_FOG,
                                  ((int)rf1 << 8) - f0, ((int)rf2 << 8) - f0,
                                  f0 + 128);
            }
        }

        /* color gradients: per-vertex when smooth shading is on, constant
         * from the flat-shade vertex otherwise */
        if ((gradientMask & 0x3C) != 0)
        {
            int side = tri->side;

            if (side != GR_SIDE_FRONT && side != GR_SIDE_BACK &&
                grAssert("side==GR_SIDE_FRONT || side==GR_SIDE_BACK",
                         "grRasterizer.inl", 749) != 0)
                grDie(0);
            if (grTestCapability(state, GR_CAPABILITY_SMOOTH_SHADING) != 0)
            {
                if (side == GR_SIDE_FRONT && (gradientMask & 0x1Cu) == 0x1Cu)
                {
                    unsigned int rg0 = grLoadU32Aligned4(&v0->color.final[0][0]);
                    unsigned int rg1 = grLoadU32Aligned4(&v1->color.final[0][0]);
                    unsigned int rg2 = grLoadU32Aligned4(&v2->color.final[0][0]);
                    if (__builtin_expect(rg0 == rg1 && rg0 == rg2, 1))
                    {
                        const unsigned short *c = &v0->color.final[0][0];
                        int sameAll;
                        if (gradientMask & 0x20u)
                        {
                            unsigned int ba0 = grLoadU32Aligned4(&v0->color.final[0][2]);
                            unsigned int ba1 = grLoadU32Aligned4(&v1->color.final[0][2]);
                            unsigned int ba2 = grLoadU32Aligned4(&v2->color.final[0][2]);
                            sameAll = (ba0 == ba1 && ba0 == ba2);
                            if (__builtin_expect(sameAll, 1))
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
                        if (__builtin_expect(sameAll, 1))
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
                            grSetupGradientNZ(&info, coord + 2,
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
                        grSetupGradientNZ(&info, GR_GRADIENT_COLOR_A,
                                          ((int)ra1 << 8) - a0, ((int)ra2 << 8) - a0,
                                          a0 + 0x8000);
                    }
                }
            }
            else
            {
                _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
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

int grSetupTriangleGradientsVisibleVerts(Gerbera_s *state, _grClipTriangle *tri,
                                         _grVertex *v0, _grVertex *v1, _grVertex *v2,
                                         int flatShadeIndex, unsigned int gradientMask, int rejectDepth)
{
    return grSetupTriangleGradientsVisibleVertsAtY(state, tri, v0, v1, v2,
                                                   flatShadeIndex, gradientMask, rejectDepth, 0);
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
__attribute__((noinline, used))
#endif
static unsigned int grFastRecip32(unsigned int d)
#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    __asm__("grFastRecip32_asm")
#endif
    ;

#if defined(__GNUC__)
__attribute__((noinline, used))
#endif
int grDivsu(int a, int b)
{
    if (b <= 0 && grAssert("b > 0", "grScanConvert.inl", 29) != 0)
        grDie(0);
    return a / b;
}

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
__attribute__((noinline, used))
static int grDivsu_arm(int a, int b) __asm__("grDivsu_arm");
static int grDivsu_arm(int a, int b)
{
    return grDivsu(a, b);
}
#endif

#if defined(__GNUC__)
__attribute__((noinline, used))
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
#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__)
    unsigned int left, diff;
    __asm__ volatile(
        "1:\n\t"
        "mov   %[left], %[lx], asr #14\n\t"
        "rsbs  %[diff], %[left], %[rx], asr #14\n\t"
        "orrgt %[left], %[left], %[diff], lsl #16\n\t"
        "add   %[lx], %[lx], %[la]\n\t"
        "add   %[rx], %[rx], %[ra]\n\t"
        "str   %[left], [%[scan]], #4\n\t"
        "subs  %[h], %[h], #1\n\t"
        "bne   1b\n\t"
        : [scan] "+r"(scanlineInfo), [lx] "+r"(leftX), [rx] "+r"(rightX),
          [h] "+r"(height), [left] "=&r"(left), [diff] "=&r"(diff)
        : [la] "r"(leftAdder), [ra] "r"(rightAdder)
        : "cc", "memory");
#else
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
#endif
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

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
typedef char gr_check_sc_rect_[offsetof(Gerbera_s, renderScissor) == 104 ? 1 : -1];
typedef char gr_check_sc_x0_[offsetof(Gerbera_s, renderScissor.x0) == 104 ? 1 : -1];
typedef char gr_check_sc_x1_[offsetof(Gerbera_s, renderScissor.x1) == 108 ? 1 : -1];
typedef char gr_check_sc_y0_[offsetof(Gerbera_s, renderScissor.y0) == 112 ? 1 : -1];
typedef char gr_check_sc_y1_[offsetof(Gerbera_s, renderScissor.y1) == 116 ? 1 : -1];
typedef char gr_check_sc_rs_[offsetof(Gerbera_s, renderStack) == 5240 ? 1 : -1];
typedef char gr_check_sc_pp_[offsetof(Gerbera_s, pixelPipeline) == 5252 ? 1 : -1];
typedef char gr_check_sc_esc_[offsetof(Gerbera_s, edgeSlopeCache) == 8020 ? 1 : -1];
typedef char gr_check_sc_ns_[offsetof(_grPixelPipe, numScanlines) == 180 ? 1 : -1];
typedef char gr_check_sc_si_[offsetof(_grPixelPipe, scanlineInfo) == 184 ? 1 : -1];
typedef char gr_check_sc_ys_[offsetof(_grPixelPipe, yStart) == 188 ? 1 : -1];
typedef char gr_check_sc_ptx_[offsetof(_grRasterPoint, x) == 0 ? 1 : -1];
typedef char gr_check_sc_pty_[offsetof(_grRasterPoint, y) == 4 ? 1 : -1];

__attribute__((naked, noinline))
int grScanConvertTriangle(Gerbera_s *state, _grRasterPoint *p0,
                          _grRasterPoint *p1, _grRasterPoint *p2,
                          int emptyBits)
{
    (void)state;
    (void)p0;
    (void)p1;
    (void)p2;
    (void)emptyBits;
    __asm__ __volatile__(
        "push  {r4-r11, lr}\n\t"
        "ldr   r4, [r1, #4]\n\t"             /* p0y */
        "ldr   r6, [r3, #4]\n\t"             /* p2y */
        "add   r10, r0, #104\n\t"            /* &renderScissor */
        "ldmia r10, {r8, r9, r11, r12}\n\t"  /* r8=x0, r9=x1, r11=y0, r12=y1 */
        "asr   r7, r4, #12\n\t"              /* v0yT */
        "asr   r10, r6, #12\n\t"             /* v2yT */
        "cmp   r7, r10\n\t"
        "bge   .Lgrsc_ret0_nosub\n\t"
        "cmp   r10, r11\n\t"
        "cmpgt r12, r7\n\t"
        "ble   .Lgrsc_ret0_nosub\n\t"

        "sub   sp, sp, #28\n\t"
        "str   r0, [sp, #0]\n\t"             /* state */
        "ldr   r5, [r2, #4]\n\t"             /* p1y */
        "ldr   r0, [r1, #0]\n\t"             /* p0x */
        "ldr   r1, [r2, #0]\n\t"             /* p1x */
        "ldr   r2, [r3, #0]\n\t"             /* p2x */
        "asr   r3, r5, #12\n\t"              /* v1yT */

        "cmp   r7, r11\n\t"
        "movlt lr, r11\n\t"
        "movge lr, r7\n\t"
        "str   lr, [sp, #4]\n\t"             /* yStart = max(v0yT, y0) */
        "sub   lr, r3, r7\n\t"               /* lr = v1yT - v0yT */

        "cmp   r7, r12\n\t"
        "movge r7, r12\n\t"                  /* v0yTop */
        "cmp   r3, r12\n\t"
        "movge r3, r12\n\t"                  /* v1yTop */
        "cmp   r10, r12\n\t"
        "movge r10, r12\n\t"                 /* v2yTop */
        "sub   r12, r3, r7\n\t"              /* topHeight */
        "sub   r10, r10, r3\n\t"             /* btmHeight */
        "cmp   r7, r11\n\t"
        "movge r3, #0\n\t"
        "strge r3, [sp, #8]\n\t"             /* topAdvance = 0 */
        "strge r3, [sp, #12]\n\t"            /* btmAdvance = 0 */
        "blt   .Lgrsc_clip_y0\n\t"
    ".Lgrsc_after_clip_y0:\n\t"
        "orrs  r3, r12, r10\n\t"
        "beq   .Lgrsc_ret0_sp\n\t"
        "str   r12, [sp, #24]\n\t"           /* topHeight */
        "str   r10, [sp, #20]\n\t"           /* btmHeight */

        /* needsXScissoring */
        "rsb   r11, r8, r0, asr #12\n\t"
        "rsb   r12, r8, r1, asr #12\n\t"
        "orr   r11, r11, r12\n\t"
        "rsb   r12, r8, r2, asr #12\n\t"
        "orr   r11, r11, r12\n\t"
        "sub   r12, r9, r0, asr #12\n\t"
        "orr   r11, r11, r12\n\t"
        "sub   r12, r9, r1, asr #12\n\t"
        "orr   r11, r11, r12\n\t"
        "sub   r12, r9, r2, asr #12\n\t"
        "orr   r11, r11, r12\n\t"
        "str   r11, [sp, #16]\n\t"

        /* Edge deltas & fracV0y/fracV1y */
        "ldr   r7, [sp, #64]\n\t"            /* emptyBits */
        "mov   r8, lr\n\t"                   /* r8 = v1yT - v0yT */
        "sub   r10, r2, r0\n\t"
        "asr   r10, r10, r7\n\t"
        "lsl   r10, r10, #14\n\t"            /* r10 = num02 */
        "sub   r11, r6, r4\n\t"
        "asr   r11, r11, r7\n\t"             /* r11 = den02 */
        "sub   r2, r2, r1\n\t"
        "asr   r2, r2, r7\n\t"
        "lsl   r2, r2, #14\n\t"              /* r2 = num12 */
        "sub   r6, r6, r5\n\t"
        "asr   r6, r6, r7\n\t"               /* r6 = den12 */
        "sub   r3, r5, r4\n\t"
        "asr   r3, r3, r7\n\t"               /* r3 = den01 */
        "mvn   r7, #0\n\t"
        "lsr   r7, r7, #20\n\t"              /* 0xFFF */
        "bic   r4, r7, r4\n\t"               /* fracV0y */
        "bic   r5, r7, r5\n\t"               /* fracV1y */
        "orr   r4, r4, r5, lsl #16\n\t"      /* r4 = fracV0y | (fracV1y << 16) */
        "mov   r7, r3\n\t"                   /* r7 = den01 */
        "ldr   r9, [sp, #0]\n\t"
        "add   r9, r9, #7936\n\t"
        "add   r9, r9, #84\n\t"              /* r9 = cacheBase (+8020) */

        /* slope02 = DIVIDE_EDGE(r10=num02, r11=den02) -> r5 */
        "cmp   r11, #0\n\t"
        "ble   .Lgrsc_divsu_02\n\t"
        "cmp   r10, #0\n\t"
        "moveq r5, #0\n\t"
        "beq   .Lgrsc_after_slope02\n\t"
        "cmp   r11, #1\n\t"
        "moveq r5, r10\n\t"
        "beq   .Lgrsc_after_slope02\n\t"
        "eor   r12, r11, r11, lsr #8\n\t"
        "and   r12, r12, #255\n\t"
        "add   r12, r9, r12, lsl #3\n\t"
        "ldmia r12, {r3, lr}\n\t"
        "cmp   r11, r3\n\t"
        "blne  .Lgrsc_miss_den11\n\t"
        "cmp   r10, #0\n\t"
        "rsbmi r12, r10, #0\n\t"
        "movpl r12, r10\n\t"
        "umull r3, r5, lr, r12\n\t"
        "mul   r3, r5, r11\n\t"
        "sub   r12, r12, r3\n\t"
        "cmp   r11, r12\n\t"
        "addls r5, r5, #1\n\t"
        "cmp   r10, #0\n\t"
        "rsbmi r5, r5, #0\n\t"
    ".Lgrsc_after_slope02:\n\t"

        /* longAtV0 -> r10, longAtV1 -> r8, longOnRight -> bit 15 of r4 */
#if defined(__ARM_ARCH) && __ARM_ARCH >= 6
        "uxth  r10, r4\n\t"
#else
        "mov   r10, r4, lsl #16\n\t"
        "mov   r10, r10, lsr #16\n\t"
#endif
        "smull r11, r12, r10, r5\n\t"
        "lsr   r11, r11, #12\n\t"
        "orr   r11, r11, r12, lsl #20\n\t"
        "add   r10, r11, r0, lsl #2\n\t"     /* r10 = longAtV0 */
        "mla   r8, r5, r8, r10\n\t"          /* r8 = longAtV1 */
        "lsr   r11, r4, #16\n\t"             /* fracV1y */
        "smull r3, r12, r11, r5\n\t"
        "lsr   r3, r3, #12\n\t"
        "orr   r3, r3, r12, lsl #20\n\t"
        "sub   r11, r8, r3\n\t"              /* longMid */
        "rsbs  r11, r11, r1, lsl #2\n\t"     /* 4*p1x - longMid */
        "orrmi r4, r4, #0x8000\n\t"          /* longOnRight in r4 bit 15 */

        /* Top half */
        "ldr   r12, [sp, #24]\n\t"           /* topHeight */
        "cmp   r12, #0\n\t"
        "beq   .Lgrsc_skip_top\n\t"
        "ldr   r12, [sp, #64]\n\t"           /* emptyBits */
        "sub   r3, r1, r0\n\t"
        "asr   r3, r3, r12\n\t"
        "lsl   r3, r3, #14\n\t"              /* r3 = num01 */
        "cmp   r7, #0\n\t"
        "ble   .Lgrsc_divsu_01\n\t"
        "cmp   r3, #0\n\t"
        "beq   .Lgrsc_after_slope01\n\t"
        "cmp   r7, #1\n\t"
        "beq   .Lgrsc_after_slope01\n\t"
        "eor   r12, r7, r7, lsr #8\n\t"
        "and   r12, r12, #255\n\t"
        "add   r12, r9, r12, lsl #3\n\t"
        "ldmia r12, {r11, lr}\n\t"
        "cmp   r7, r11\n\t"
        "blne  .Lgrsc_miss_den7\n\t"
        "cmp   r3, #0\n\t"
        "orrlt r4, r4, #0x80000000\n\t"
        "rsblt r12, r3, #0\n\t"
        "movge r12, r3\n\t"
        "umull r11, r3, lr, r12\n\t"
        "mul   r11, r3, r7\n\t"
        "sub   r12, r12, r11\n\t"
        "cmp   r7, r12\n\t"
        "addls r3, r3, #1\n\t"
        "tst   r4, #0x80000000\n\t"
        "rsbne r3, r3, #0\n\t"
        "bicne r4, r4, #0x80000000\n\t"
    ".Lgrsc_after_slope01:\n\t"

        "lsl   r12, r4, #20\n\t"
        "lsr   r12, r12, #20\n\t"            /* fracV0y */
        "smull r11, r7, r12, r3\n\t"
        "lsr   r11, r11, #12\n\t"
        "orr   r11, r11, r7, lsl #20\n\t"
        "add   r0, r11, r0, lsl #2\n\t"      /* r0 = shortAtV0 */
        "mov   r7, r5\n\t"                   /* r7 = slope02 */
        "tst   r4, #0x8000\n\t"
        "moveq r11, r0\n\t"
        "moveq r0, r10\n\t"
        "moveq r10, r11\n\t"
        "moveq r11, r3\n\t"
        "moveq r3, r7\n\t"
        "moveq r7, r11\n\t"

        "ldr   r11, [sp, #8]\n\t"            /* topAdvance */
        "cmp   r11, #0\n\t"
        "mlane r0, r3, r11, r0\n\t"
        "mlane r10, r7, r11, r10\n\t"

        "ldr   r11, [sp, #0]\n\t"
        "add   r11, r11, #4096\n\t"
        "ldr   r11, [r11, #1144]\n\t"
        "add   r11, r11, #15616\n\t"
        "add   r11, r11, #52\n\t"
        "str   r11, [sp, #8]\n\t"            /* save scanStart */

        "ldr   r12, [sp, #16]\n\t"           /* needsXSciss */
        "cmp   r12, #0\n\t"
        "blt   .Lgrsc_top_scissor\n\t"
        "ldr   r12, [sp, #24]\n\t"           /* topHeight */
        "str   r9, [sp, #24]\n\t"            /* preserve cacheBase */
    ".Lgrsc_top_nosciss_loop:\n\t"
        "asr   r9, r0, #14\n\t"
        "rsbs  lr, r9, r10, asr #14\n\t"
        "addgt r9, r9, lr, lsl #16\n\t"
        "add   r0, r0, r3\n\t"
        "add   r10, r10, r7\n\t"
        "str   r9, [r11], #4\n\t"
        "subs  r12, r12, #1\n\t"
        "bne   .Lgrsc_top_nosciss_loop\n\t"
        "ldr   r9, [sp, #24]\n\t"
        "b     .Lgrsc_check_btm\n\t"

    ".Lgrsc_skip_top:\n\t"
        "ldr   r11, [sp, #0]\n\t"
        "add   r11, r11, #4096\n\t"
        "ldr   r11, [r11, #1144]\n\t"
        "add   r11, r11, #15616\n\t"
        "add   r11, r11, #52\n\t"
        "str   r11, [sp, #8]\n\t"

    ".Lgrsc_check_btm:\n\t"
        "ldr   r12, [sp, #20]\n\t"           /* btmHeight */
        "cmp   r12, #0\n\t"
        "beq   .Lgrsc_trim\n\t"

        /* slope12 = DIVIDE_EDGE(r2=num12, r6=den12) -> r3 */
        "cmp   r6, #0\n\t"
        "ble   .Lgrsc_divsu_12\n\t"
        "cmp   r2, #0\n\t"
        "moveq r3, #0\n\t"
        "beq   .Lgrsc_after_slope12\n\t"
        "cmp   r6, #1\n\t"
        "moveq r3, r2\n\t"
        "beq   .Lgrsc_after_slope12\n\t"
        "eor   r12, r6, r6, lsr #8\n\t"
        "and   r12, r12, #255\n\t"
        "add   r12, r9, r12, lsl #3\n\t"
        "ldmia r12, {r7, lr}\n\t"
        "cmp   r6, r7\n\t"
        "blne  .Lgrsc_miss_den6\n\t"
        "cmp   r2, #0\n\t"
        "rsbmi r12, r2, #0\n\t"
        "movpl r12, r2\n\t"
        "umull r7, r3, lr, r12\n\t"
        "mul   r7, r3, r6\n\t"
        "sub   r12, r12, r7\n\t"
        "cmp   r6, r12\n\t"
        "addls r3, r3, #1\n\t"
        "cmp   r2, #0\n\t"
        "rsbmi r3, r3, #0\n\t"
    ".Lgrsc_after_slope12:\n\t"

        "lsr   r12, r4, #16\n\t"             /* fracV1y */
        "smull r7, r10, r12, r3\n\t"
        "lsr   r7, r7, #12\n\t"
        "orr   r7, r7, r10, lsl #20\n\t"
        "add   r0, r7, r1, lsl #2\n\t"       /* r0 = shortAtV1 */
        "tst   r4, #0x8000\n\t"
        "moveq r10, r0\n\t"
        "moveq r0, r8\n\t"
        "moveq r8, r10\n\t"
        "moveq r10, r3\n\t"
        "moveq r3, r5\n\t"
        "moveq r5, r10\n\t"

        "ldr   r7, [sp, #12]\n\t"            /* btmAdvance */
        "cmp   r7, #0\n\t"
        "mlane r0, r3, r7, r0\n\t"
        "mlane r8, r5, r7, r8\n\t"

        "ldr   r12, [sp, #16]\n\t"           /* needsXSciss */
        "cmp   r12, #0\n\t"
        "ldr   r12, [sp, #20]\n\t"           /* btmHeight */
        "blt   .Lgrsc_btm_scissor\n\t"
    ".Lgrsc_btm_nosciss_loop:\n\t"
        "asr   r1, r0, #14\n\t"
        "rsbs  r2, r1, r8, asr #14\n\t"
        "addgt r1, r1, r2, lsl #16\n\t"
        "add   r0, r0, r3\n\t"
        "add   r8, r8, r5\n\t"
        "str   r1, [r11], #4\n\t"
        "subs  r12, r12, #1\n\t"
        "bne   .Lgrsc_btm_nosciss_loop\n\t"

    ".Lgrsc_trim:\n\t"
        "ldr   r0, [sp, #8]\n\t"             /* scanStart */
    ".Lgrsc_trim_tail:\n\t"
        "ldr   r1, [r11, #-4]!\n\t"
        "movs  r1, r1, lsr #16\n\t"
        "bne   .Lgrsc_tail_found\n\t"
        "cmp   r11, r0\n\t"
        "bne   .Lgrsc_trim_tail\n\t"
    ".Lgrsc_ret0_sp:\n\t"
        "add   sp, sp, #28\n\t"
    ".Lgrsc_ret0_nosub:\n\t"
        "mov   r0, #0\n\t"
        "pop   {r4-r11, pc}\n\t"

    ".Lgrsc_tail_found:\n\t"
        "mov   r2, r0\n\t"
    ".Lgrsc_trim_head:\n\t"
        "ldr   r1, [r0], #4\n\t"
        "movs  r1, r1, lsr #16\n\t"
        "beq   .Lgrsc_trim_head\n\t"
        "sub   r0, r0, #4\n\t"
        "sub   r3, r11, r0\n\t"
        "add   r3, r3, #4\n\t"
        "lsr   r3, r3, #2\n\t"
        "sub   r2, r0, r2\n\t"
        "ldr   r1, [sp, #4]\n\t"             /* yStart */
        "add   r1, r1, r2, asr #2\n\t"
        "ldr   r4, [sp, #0]\n\t"             /* state */
        "add   r4, r4, #4096\n\t"
        "ldr   r4, [r4, #1156]\n\t"          /* pixelPipe */
        "str   r3, [r4, #180]\n\t"
        "str   r0, [r4, #184]\n\t"
        "str   r1, [r4, #188]\n\t"
        "mov   r0, r3\n\t"
        "add   sp, sp, #28\n\t"
        "pop   {r4-r11, pc}\n\t"

        /* --- Cold out-of-line helpers --- */
    ".Lgrsc_clip_y0:\n\t"
        "sub   r7, r11, r7\n\t"              /* topAdvance = y0 - v0yTop */
        "str   r7, [sp, #8]\n\t"
        "subs  r12, r12, r7\n\t"
        "movlt r12, #0\n\t"
        "cmp   r3, r11\n\t"
        "sublt r3, r11, r3\n\t"
        "movge r3, #0\n\t"
        "str   r3, [sp, #12]\n\t"
        "subs  r10, r10, r3\n\t"
        "movlt r10, #0\n\t"
        "b     .Lgrsc_after_clip_y0\n\t"

    ".Lgrsc_top_scissor:\n\t"
        "push  {r1, r2, r5, r6}\n\t"
        "ldr   r5, [sp, #16]\n\t"            /* state */
        "ldr   r1, [r5, #104]\n\t"           /* x0 */
        "ldr   r2, [r5, #108]\n\t"           /* x1 */
        "ldr   r12, [sp, #40]\n\t"           /* topHeight */
    ".Lgrsc_top_sciss_loop:\n\t"
        "asr   r5, r0, #14\n\t"
        "cmp   r5, r1\n\t"
        "movlt r5, r1\n\t"
        "asr   r6, r10, #14\n\t"
        "cmp   r6, r2\n\t"
        "movgt r6, r2\n\t"
        "subs  r6, r6, r5\n\t"
        "addgt r5, r5, r6, lsl #16\n\t"
        "add   r0, r0, r3\n\t"
        "add   r10, r10, r7\n\t"
        "str   r5, [r11], #4\n\t"
        "subs  r12, r12, #1\n\t"
        "bne   .Lgrsc_top_sciss_loop\n\t"
        "pop   {r1, r2, r5, r6}\n\t"
        "b     .Lgrsc_check_btm\n\t"

    ".Lgrsc_btm_scissor:\n\t"
        "ldr   r9, [sp, #0]\n\t"             /* state (reloaded after? wait: dead after btm!) */
        "ldr   r1, [r9, #104]\n\t"           /* x0 */
        "ldr   r2, [r9, #108]\n\t"           /* x1 */
    ".Lgrsc_btm_sciss_loop:\n\t"
        "asr   r6, r0, #14\n\t"
        "cmp   r6, r1\n\t"
        "movlt r6, r1\n\t"
        "asr   r7, r8, #14\n\t"
        "cmp   r7, r2\n\t"
        "movgt r7, r2\n\t"
        "subs  r7, r7, r6\n\t"
        "addgt r6, r6, r7, lsl #16\n\t"
        "add   r0, r0, r3\n\t"
        "add   r8, r8, r5\n\t"
        "str   r6, [r11], #4\n\t"
        "subs  r12, r12, #1\n\t"
        "bne   .Lgrsc_btm_sciss_loop\n\t"
        "b     .Lgrsc_trim\n\t"

    ".Lgrsc_miss_den11:\n\t"
        "push  {r0, r1, r2, r3, r4, r5, r12, lr}\n\t"
        "mov   r4, r12\n\t"
        "mov   r5, r11\n\t"
        "b     .Lgrsc_miss_common\n\t"
    ".Lgrsc_miss_den7:\n\t"
        "push  {r0, r1, r2, r3, r4, r5, r12, lr}\n\t"
        "mov   r4, r12\n\t"
        "mov   r5, r7\n\t"
        "b     .Lgrsc_miss_common\n\t"
    ".Lgrsc_miss_den6:\n\t"
        "push  {r0, r1, r2, r3, r4, r5, r12, lr}\n\t"
        "mov   r4, r12\n\t"
        "mov   r5, r6\n\t"
    ".Lgrsc_miss_common:\n\t"
        "mov   r0, r5\n\t"
        "bl    grFastRecip32_asm\n\t"
        "str   r5, [r4, #0]\n\t"
        "str   r0, [r4, #4]\n\t"
        "mov   lr, r0\n\t"
        "pop   {r0, r1, r2, r3, r4, r5, r12}\n\t"
        "ldr   pc, [sp], #4\n\t"

    ".Lgrsc_divsu_02:\n\t"
        "push  {r0, r1, r2, r3, r12, lr}\n\t"
        "mov   r0, r10\n\t"
        "mov   r1, r11\n\t"
        "bl    grDivsu_arm\n\t"
        "mov   r5, r0\n\t"
        "pop   {r0, r1, r2, r3, r12, lr}\n\t"
        "b     .Lgrsc_after_slope02\n\t"
    ".Lgrsc_divsu_01:\n\t"
        "push  {r0, r1, r2, r3, r12, lr}\n\t"
        "mov   r0, r3\n\t"
        "mov   r1, r7\n\t"
        "bl    grDivsu_arm\n\t"
        "str   r0, [sp, #12]\n\t"
        "pop   {r0, r1, r2, r3, r12, lr}\n\t"
        "b     .Lgrsc_after_slope01\n\t"
    ".Lgrsc_divsu_12:\n\t"
        "push  {r0, r1, r2, r3, r12, lr}\n\t"
        "mov   r0, r2\n\t"
        "mov   r1, r6\n\t"
        "bl    grDivsu_arm\n\t"
        "str   r0, [sp, #12]\n\t"
        "pop   {r0, r1, r2, r3, r12, lr}\n\t"
        "b     .Lgrsc_after_slope12\n\t"
    );
}
#else
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
    int v0yBottom = hgMax32(v0yT, scissor->y0);
    int fracV0y = 4095 - (p0->y & 0xFFF);      /* distance to the next
                                                * scanline (0.12, -1 LSB) */
    int slope02 = GR_DIVIDE_EDGE(state, imLsl32OF(d20x, 14), d20y);  /* long-edge slope,
                                                * 2.14 per scanline */
    int longAtV0 = 4 * p0->x + (int)(((long long)fracV0y * slope02) >> 12);
    int longAtV1 = longAtV0 + slope02 * (v1yT - v0yT);  /* UNCLAMPED middle
                                                * delta (decompile p4.xAdder[1]
                                                * uses the raw truncs) */
    int fracV1y = 4095 - (p1->y & 0xFFF);
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
        int x0 = scissor->x0;
        int x1 = scissor->x1;
        int v0xT = grSubpixelTrunc(p0->x);
        int v1xT = grSubpixelTrunc(p1->x);
        int v2xT = grSubpixelTrunc(p2->x);
        int needsXScissoring = (((v0xT - x0) | (v1xT - x0) | (v2xT - x0) |
                                 (x1 - v0xT) | (x1 - v1xT) | (x1 - v2xT)) < 0);

        scanInfo = (unsigned int *)((unsigned char *)state->renderStack +
                                    GR_RENDER_STACK_SCANLINE_INFO);
        scanCursor = scanInfo;
#if defined(__GNUC__)
        if (__builtin_expect(needsXScissoring, 0))
#else
        if (needsXScissoring)
#endif
        {
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
#endif

/* --- grInterpolateVertexGradients (grRasterizer.inl 251; DLL 25530-25899) -----
 * Interpolates the per-vertex attributes of a clip-generated vertex from the
 * triangle's three source records. The barycentric weights ride in the new
 * vertex's normal[0..1] (the clipFactor alias); c = ONE-a-b is computed
 * locally. Attributes use (a*t0 + b*t1 + c*t2) >> 29 with IUNITSCALAR_ONE
 * weights. The trivial case (a==ONE, b==ONE, or a==b==0) copies attributes
 * from the corresponding source vertex.
 * Depth (mask bit 0) is NOT handled here - it is a per-scanline gradient. */

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void grInterp2Verts(_grVertex * restrict vertex,
                           const _grVertex * restrict u0,
                           const _grVertex * restrict u1,
                           int w0, unsigned int maskAndSide)
{
    int w1 = IUNITSCALAR_ONE - w0;
    unsigned int gradientMask = maskAndSide & 0x3FFu;
    int side = (int)(maskAndSide >> 31);
#define GR_INTERP2_TEXCOORD(u_)                                                 \
    do {                                                                        \
        int t0y = u0->texCoord[u_].y;                                           \
        int t1y = u1->texCoord[u_].y;                                           \
        int gy = (t0y == t1y) ? t0y :                                           \
            (int)imLsl64h_0_31OF(hgMadd64(hgMul64(w0, t0y), w1, t1y), 3);       \
        int t0x = u0->texCoord[u_].x;                                           \
        int t1x = u1->texCoord[u_].x;                                           \
        int gx = (t0x == t1x) ? t0x :                                           \
            (int)imLsl64h_0_31OF(hgMadd64(hgMul64(w0, t0x), w1, t1x), 3);       \
        if (hgInRange32(gy, hgMin32(t0y, t1y), hgMax32(t0y, t1y)) == 0 &&       \
            grAssert("hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1))",        \
                     "grRasterizer.inl", 332) != 0)                             \
            grDie(0);                                                           \
        if (hgInRange32(gx, hgMin32(t0x, t1x), hgMax32(t0x, t1x)) == 0 &&       \
            grAssert("hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1))",        \
                     "grRasterizer.inl", 332) != 0)                             \
            grDie(0);                                                           \
        vertex->texCoord[u_].y = gy;                                            \
        vertex->texCoord[u_].x = gx;                                            \
    } while (0)

    if ((gradientMask & 0xC0u) != 0)
        GR_INTERP2_TEXCOORD(0);
    if (__builtin_expect((gradientMask & 0x300u) != 0, 0))
        GR_INTERP2_TEXCOORD(1);
#undef GR_INTERP2_TEXCOORD
    if ((gradientMask & 2u) != 0)
    {
        int t0 = u0->color.final[1][3];
        int t1 = u1->color.final[1][3];
        int g = (t0 == t1) ? t0 :
            (int)imLsl64h_0_31OF(hgMadd64(hgMul64(w0, t0), w1, t1), 3);
        if (hgInRange32(g, 0, 0xFFFF) == 0 &&
            grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 341) != 0)
            grDie(0);
        if (hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1)) == 0 &&
            grAssert("hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1))",
                     "grRasterizer.inl", 343) != 0)
            grDie(0);
        vertex->color.final[1][3] = (unsigned short)g;
    }
    if ((gradientMask & 0x1Cu) != 0)
    {
        const unsigned short *c0 = u0->color.final[side];
        const unsigned short *c1 = u1->color.final[side];
        unsigned short *cd = vertex->color.final[side];
        if (__builtin_expect((gradientMask & 0x1Cu) == 0x1Cu, 1))
        {
            unsigned int rg0 = grLoadU32Aligned4(c0);
            unsigned int rg1 = grLoadU32Aligned4(c1);
            if (__builtin_expect(rg0 == rg1, 1))
            {
                if (side == 0 && (gradientMask & 0x20u) != 0)
                {
                    unsigned int ba0 = grLoadU32Aligned4(c0 + 2);
                    unsigned int ba1 = grLoadU32Aligned4(c1 + 2);
                    if (__builtin_expect(ba0 == ba1, 1))
                    {
                        grStoreU32Aligned4(cd, rg0);
                        grStoreU32Aligned4(cd + 2, ba0);
                        return;
                    }
                }
                else if (c0[2] == c1[2])
                {
                    grStoreU32Aligned4(cd, rg0);
                    cd[2] = c0[2];
                    goto after_rgb2;
                }
            }
        }
#define GR_INTERP2_COMP(comp_)                                                  \
        if ((gradientMask & (4u << (comp_))) != 0)                              \
        {                                                                       \
            int t0 = c0[comp_];                                                 \
            int t1 = c1[comp_];                                                 \
            int g = (t0 == t1) ? t0 :                                           \
                (int)imLsl64h_0_31OF(hgMadd64(hgMul64(w0, t0), w1, t1), 3);     \
            if (hgInRange32(g, 0, 0xFFFF) == 0 &&                               \
                grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 371) != 0) \
                grDie(0);                                                       \
            if (hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1)) == 0 &&        \
                grAssert("hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1))",    \
                         "grRasterizer.inl", 373) != 0)                         \
                grDie(0);                                                       \
            cd[comp_] = (unsigned short)g;                                      \
        }
        GR_INTERP2_COMP(2);
        GR_INTERP2_COMP(1);
        GR_INTERP2_COMP(0);
#undef GR_INTERP2_COMP
    }
after_rgb2:
    if ((gradientMask & 0x20u) != 0)
    {
        int t0 = u0->color.final[0][3];
        int t1 = u1->color.final[0][3];
        int g = (t0 == t1) ? t0 :
            (int)imLsl64h_0_31OF(hgMadd64(hgMul64(w0, t0), w1, t1), 3);
        if (hgInRange32(g, 0, 0xFFFF) == 0 &&
            grAssert("hgInRange32(g, 0, 65535)", "grRasterizer.inl", 384) != 0)
            grDie(0);
        if (hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1)) == 0 &&
            grAssert("hgInRange32(g, hgMin32(t0, t1), hgMax32(t0, t1))",
                     "grRasterizer.inl", 386) != 0)
            grDie(0);
        vertex->color.final[0][3] = (unsigned short)g;
    }
}

void grInterpolateVertexGradients(Gerbera_s *state, _grVertex *vertex,
                                  unsigned int origIndices, int side)
{
    _grVertex * restrict vb;
    unsigned int gradientMask;
    int a;
    int b;

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
    vb = GR_VERTEX_BUFFER(state);
    gradientMask = (unsigned int)state->activeGradientMask;
    a = vertex->normal[0];
    b = vertex->normal[1];
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
    if (__builtin_expect(((b | a) & 0x1FFFFFFF) != 0, 1))
    {
        int c = IUNITSCALAR_ONE - a - b;

        if ((a == IUNITSCALAR_ONE || b == IUNITSCALAR_ONE || (a == 0 && b == 0)) &&
            grAssert("a!=IUNITSCALAR_ONE && b!=IUNITSCALAR_ONE && (a!=0 || b!=0)",
                     "grRasterizer.inl", 303) != 0)
            grDie(0);
        if (c == 0)
        {
            _grVertex *u0 = vb + grUnpackIndex(origIndices, 0);
            _grVertex *u1 = vb + grUnpackIndex(origIndices, 1);
            grInterp2Verts(vertex, u0, u1, a, gradientMask | ((unsigned int)side << 31));
            return;
        }
        if (a == 0)
        {
            _grVertex *u0 = vb + grUnpackIndex(origIndices, 1);
            _grVertex *u1 = vb + grUnpackIndex(origIndices, 2);
            grInterp2Verts(vertex, u0, u1, b, gradientMask | ((unsigned int)side << 31));
            return;
        }
        if (b == 0)
        {
            _grVertex *u0 = vb + grUnpackIndex(origIndices, 0);
            _grVertex *u1 = vb + grUnpackIndex(origIndices, 2);
            grInterp2Verts(vertex, u0, u1, a, gradientMask | ((unsigned int)side << 31));
            return;
        }
        {
            _grVertex *v0 = vb + grUnpackIndex(origIndices, 0);
            _grVertex *v1 = vb + grUnpackIndex(origIndices, 1);
            _grVertex *v2 = vb + grUnpackIndex(origIndices, 2);
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
            if (__builtin_expect((gradientMask & 0x1Cu) == 0x1Cu, 1))
            {
                unsigned int rg0 = grLoadU32Aligned4(c0);
                unsigned int rg1 = grLoadU32Aligned4(c1);
                unsigned int rg2 = grLoadU32Aligned4(c2);
                if (__builtin_expect(rg0 == rg1 && rg0 == rg2, 1))
                {
                    if (side == 0 && (gradientMask & 0x20u) != 0)
                    {
                        unsigned int ba0 = grLoadU32Aligned4(c0 + 2);
                        unsigned int ba1 = grLoadU32Aligned4(c1 + 2);
                        unsigned int ba2 = grLoadU32Aligned4(c2 + 2);
                        if (__builtin_expect(ba0 == ba1 && ba0 == ba2, 1))
                        {
                            grStoreU32Aligned4(cd, rg0);
                            grStoreU32Aligned4(cd + 2, ba0);
                            return;
                        }
                    }
                    else if (c0[2] == c1[2] && c0[2] == c2[2])
                    {
                        grStoreU32Aligned4(cd, rg0);
                        cd[2] = c0[2];
                        goto after_rgb3;
                    }
                }
            }
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
after_rgb3:
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
    }
    else
    {
        _grVertex *rec;

        if (a == IUNITSCALAR_ONE)
            rec = vb + grUnpackIndex(origIndices, 0);
        else if (b == IUNITSCALAR_ONE)
            rec = vb + grUnpackIndex(origIndices, 1);
        else
            rec = vb + grUnpackIndex(origIndices, 2);
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

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
typedef char gr_check_tu_off_[offsetof(Gerbera_s, textureUnit) == 5108 ? 1 : -1];
typedef char gr_check_tu_sz_[sizeof(_grTextureUnitState) == 32 && offsetof(_grTextureUnitState, texture) == 0 ? 1 : -1];
typedef char gr_check_tu_pp_[offsetof(Gerbera_s, pixelPipeline) == 5252 ? 1 : -1];
typedef char gr_check_tu_agm_[offsetof(Gerbera_s, activeGradientMask) == 5260 ? 1 : -1];
typedef char gr_check_tu_ml_[sizeof(_grMipLevel) == 16 && offsetof(_grMipLevel, pixels) == 0 && offsetof(_grMipLevel, log2Dim) == 4 ? 1 : -1];
typedef char gr_check_tu_tex_[offsetof(_grTexture, mipmap) == 0 && offsetof(_grTexture, palette) == 176 &&
                              offsetof(_grTexture, numMipLevels) == 180 && offsetof(_grTexture, parmMask) == 183 &&
                              offsetof(_grTexture, wrapMode) == 204 && offsetof(_grTexture, filterMask) == 206 ? 1 : -1];
typedef char gr_check_tu_ppu_[offsetof(_grPixelPipe, textureUnit) == 0 && sizeof(_grPixelPipeTextureUnit) == 64 &&
                              offsetof(_grPixelPipeTextureUnit, pixels) == 4 && offsetof(_grPixelPipeTextureUnit, palette) == 8 &&
                              offsetof(_grPixelPipeTextureUnit, linear) == 12 && offsetof(_grPixelPipeTextureUnit, sMask) == 16 &&
                              offsetof(_grPixelPipeTextureUnit, tMask) == 20 && offsetof(_grPixelPipeTextureUnit, rShift) == 24 &&
                              offsetof(_grPixelPipeTextureUnit, log2DimS) == 32 ? 1 : -1];

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noclone, naked, used))
#else
__attribute__((noinline, naked, used))
#endif
static void grSetupTextureUnitCoord(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
                                    int v0sx, int v0sy)
{
    __asm__ volatile(
        "push    {r4-r11, lr}\n\t"
        "add     lr, r0, #4096\n\t"
        "ldr     r4, [lr, #1156]\n\t"     /* pipe */
        "ldr     r8, [lr, #1164]\n\t"     /* activeGradientMask */
        "add     r5, r0, r1, lsl #6\n\t"
        "add     r5, r5, #5056\n\t"
        "ldr     r5, [r5, #52]\n\t"       /* texture */
        "mov     r6, r2\n\t"              /* g0 */
        "mov     r7, r1\n\t"              /* texIndex */
        "mov     r9, r3\n\t"              /* v0sx */
        "ldrb    r3, [r5, #183]\n\t"      /* parmMask */
        "mov     r0, #0\n\t"              /* mipLevel = 0 */
        "mov     r1, #1\n\t"              /* isMag = 1 */
        "tst     r3, #32\n\t"
        "beq     10f\n\t"
        "ldrh    r2, [r5, #206]\n\t"      /* filterMask[0] | (filterMask[1]<<8) */
        "tst     r2, #2\n\t"
        "bne     0f\n\t"
        "eor     r3, r2, r2, lsr #8\n\t"
        "tst     r3, #255\n\t"
        "beq     10f\n\t"
    "0:\n\t"
        "mov     r10, #0\n\t"             /* maxRho2_lo */
        "mov     r11, #0\n\t"             /* maxRho2_hi */
        /* mip coord i = 1 */
        "lsl     r12, r7, #1\n\t"
        "add     lr, r12, #1\n\t"
        "mov     r0, #64\n\t"
        "tst     r8, r0, lsl lr\n\t"
        "beq     2f\n\t"
        "add     lr, r12, #7\n\t"
        "add     lr, r4, lr, lsl #2\n\t"
        "ldr     r0, [lr, #232]\n\t"
        "ldr     r1, [lr, #272]\n\t"
        "smull   r2, r3, r0, r0\n\t"
        "smlal   r2, r3, r1, r1\n\t"
        "ldrsh   r0, [r5, #6]\n\t"
        "ldr     r1, [r6, #12]\n\t"
        "add     r1, r1, #7\n\t"
        "sub     r0, r1, r0\n\t"
        "lsl     r0, r0, #1\n\t"
        "cmp     r0, #0\n\t"
        "movlt   r0, #0\n\t"
        "cmp     r0, #63\n\t"
        "movgt   r0, #63\n\t"
        "cmp     r0, #31\n\t"
        "bgt     1f\n\t"
        "rsb     r1, r0, #32\n\t"
        "lsr     r2, r2, r0\n\t"
        "orr     r2, r2, r3, lsl r1\n\t"
        "lsr     r3, r3, r0\n\t"
        "b       3f\n\t"
    "1:\n\t"
        "sub     r1, r0, #32\n\t"
        "lsr     r2, r3, r1\n\t"
        "mov     r3, #0\n\t"
    "3:\n\t"
        "cmp     r10, r2\n\t"
        "sbcs    r1, r11, r3\n\t"
        "movlo   r10, r2\n\t"
        "movlo   r11, r3\n\t"
    "2:\n\t"
        /* mip coord i = 0 */
        "lsl     r12, r7, #1\n\t"
        "mov     r0, #64\n\t"
        "tst     r8, r0, lsl r12\n\t"
        "beq     5f\n\t"
        "add     lr, r12, #6\n\t"
        "add     lr, r4, lr, lsl #2\n\t"
        "ldr     r0, [lr, #232]\n\t"
        "ldr     r1, [lr, #272]\n\t"
        "smull   r2, r3, r0, r0\n\t"
        "smlal   r2, r3, r1, r1\n\t"
        "ldrsh   r0, [r5, #4]\n\t"
        "ldr     r1, [r6, #4]\n\t"
        "add     r1, r1, #7\n\t"
        "sub     r0, r1, r0\n\t"
        "lsl     r0, r0, #1\n\t"
        "cmp     r0, #0\n\t"
        "movlt   r0, #0\n\t"
        "cmp     r0, #63\n\t"
        "movgt   r0, #63\n\t"
        "cmp     r0, #31\n\t"
        "bgt     4f\n\t"
        "rsb     r1, r0, #32\n\t"
        "lsr     r2, r2, r0\n\t"
        "orr     r2, r2, r3, lsl r1\n\t"
        "lsr     r3, r3, r0\n\t"
        "b       6f\n\t"
    "4:\n\t"
        "sub     r1, r0, #32\n\t"
        "lsr     r2, r3, r1\n\t"
        "mov     r3, #0\n\t"
    "6:\n\t"
        "cmp     r10, r2\n\t"
        "sbcs    r1, r11, r3\n\t"
        "movlo   r10, r2\n\t"
        "movlo   r11, r3\n\t"
    "5:\n\t"
        "cmp     r11, #0x8000\n\t"
        "bhs     7f\n\t"
        "lsl     r0, r11, #16\n\t"
        "orr     r0, r0, r10, lsr #16\n\t"
        "add     r0, r0, r0, lsr #3\n\t"
        "clz     r0, r0\n\t"
        "rsbs    r0, r0, #20\n\t"
        "movle   r0, #0\n\t"
        "asrgt   r0, r0, #1\n\t"
        "cmp     r0, #0\n\t"
        "moveq   r1, #1\n\t"
        "beq     8f\n\t"
        "mov     r1, #0\n\t"
        "ldrsh   r2, [r5, #180]\n\t"
        "sub     r2, r2, #1\n\t"
        "cmp     r0, r2\n\t"
        "movgt   r0, r2\n\t"
        "b       8f\n\t"
    "7:\n\t"
        "mov     r1, #0\n\t"
        "ldrsh   r0, [r5, #180]\n\t"
        "sub     r0, r0, #1\n\t"
    "8:\n\t"
        "ldrb    r2, [r5, #206]\n\t"
        "tst     r2, #2\n\t"
        "moveq   r0, #0\n\t"
    "10:\n\t"
        "add     r2, r5, #206\n\t"
        "ldrb    r1, [r2, r1]\n\t"        /* filterByte */
        "and     r1, r1, #1\n\t"          /* linear */
        "add     r0, r5, r0, lsl #4\n\t"  /* mipmap */
        "ldrsh   r2, [r0, #4]\n\t"        /* log2S */
        "ldrsh   r3, [r0, #6]\n\t"        /* log2T */
        "add     r11, r4, r7, lsl #6\n\t" /* tex */
        "str     r1, [r11, #12]\n\t"
        "str     r2, [r11, #32]\n\t"
        "ldr     r12, [r0]\n\t"
        "str     r12, [r11, #4]\n\t"
        "ldr     r12, [r5, #176]\n\t"
        "str     r12, [r11, #8]\n\t"
        "mov     r12, #1\n\t"
        "lsl     lr, r12, r2\n\t"
        "sub     lr, lr, #1\n\t"
        "str     lr, [r11, #16]\n\t"
        "lsl     lr, r12, r3\n\t"
        "sub     lr, lr, #1\n\t"
        "str     lr, [r11, #20]\n\t"
        "ldrh    r5, [r5, #204]\n\t"      /* wrapMode[0] | (wrapMode[1] << 8) */
        "orr     r5, r5, r1, lsl #16\n\t" /* | (linear << 16) */
        "lsl     r7, r7, #1\n\t"          /* 2 * texIndex */
        "orr     r7, r7, r2, lsl #8\n\t"
        "orr     r7, r7, r3, lsl #16\n\t"
        "ldr     r10, [sp, #36]\n\t"      /* v0sy */
        /* coord i = 1 */
        "and     r0, r7, #3\n\t"
        "add     lr, r0, #1\n\t"
        "mov     r1, #64\n\t"
        "tst     r8, r1, lsl lr\n\t"
        "beq     12f\n\t"
        "add     r0, r0, #7\n\t"
        "add     r0, r4, r0, lsl #2\n\t"
        "lsrs    r2, r7, #16\n\t"         /* l2 = log2T */
        "beq     11f\n\t"
        "ldr     r3, [r6, #12]\n\t"       /* g0sh = g0[1][1] */
        "mov     r1, #0\n\t"
        "tst     r5, #0x10000\n\t"
        "beq     13f\n\t"
        "subs    r1, r2, r3\n\t"
        "mov     r12, #0x100000\n\t"
        "lsrge   r1, r12, r1\n\t"
        "rsblt   r1, r1, #0\n\t"
        "lsllt   r1, r12, r1\n\t"
    "13:\n\t"
        "ldr     r12, [r6, #8]\n\t"       /* g0val = g0[1][0] */
        "sub     r1, r12, r1\n\t"
        "ldr     r12, [r0, #232]\n\t"     /* xAdd */
        "ldr     lr, [r0, #272]\n\t"      /* yAdd */
        "smull   r2, r3, r9, r12\n\t"
        "smlal   r2, r3, r10, lr\n\t"
        "lsr     r2, r2, #12\n\t"
        "orr     r2, r2, r3, lsl #20\n\t"
        "sub     r1, r1, r2\n\t"          /* ref */
        "ldr     r3, [r6, #12]\n\t"       /* g0sh */
        "lsr     r2, r5, #8\n\t"
        "and     r2, r2, #255\n\t"        /* wrapMode[1] */
        "cmp     r2, #1\n\t"
        "addeq   r2, r3, #21\n\t"         /* baseShift */
        "moveq   r3, #0\n\t"              /* lsh */
        "lsrne   r2, r2, #1\n\t"
        "addne   r3, r3, r2\n\t"
        "rsbne   r2, r2, #32\n\t"
        "rsbne   r3, r3, #11\n\t"
        "lsl     r1, r1, r3\n\t"
        "lsl     r12, r12, r3\n\t"
        "lsl     lr, lr, r3\n\t"
        "str     r1, [r0, #192]\n\t"
        "str     r12, [r0, #232]\n\t"
        "str     lr, [r0, #272]\n\t"
        "sub     r2, r2, r7, lsr #16\n\t"
        "str     r2, [r11, #28]\n\t"
        "b       12f\n\t"
    "11:\n\t"
        "str     r2, [r0, #232]\n\t"
        "str     r2, [r0, #272]\n\t"
        "str     r2, [r0, #192]\n\t"
        "str     r2, [r11, #28]\n\t"
    "12:\n\t"
        /* coord i = 0 */
        "and     r0, r7, #3\n\t"
        "mov     r1, #64\n\t"
        "tst     r8, r1, lsl r0\n\t"
        "popeq   {r4-r11, pc}\n\t"
        "add     r0, r0, #6\n\t"
        "add     r0, r4, r0, lsl #2\n\t"
        "lsr     r2, r7, #8\n\t"
        "ands    r2, r2, #255\n\t"        /* l2 = log2S */
        "beq     14f\n\t"
        "ldr     r3, [r6, #4]\n\t"        /* g0sh = g0[0][1] */
        "mov     r1, #0\n\t"
        "tst     r5, #0x10000\n\t"
        "beq     15f\n\t"
        "subs    r1, r2, r3\n\t"
        "mov     r12, #0x100000\n\t"
        "lsrge   r1, r12, r1\n\t"
        "rsblt   r1, r1, #0\n\t"
        "lsllt   r1, r12, r1\n\t"
    "15:\n\t"
        "ldr     r12, [r6]\n\t"           /* g0val = g0[0][0] */
        "sub     r1, r12, r1\n\t"
        "ldr     r12, [r0, #232]\n\t"
        "ldr     lr, [r0, #272]\n\t"
        "smull   r2, r3, r9, r12\n\t"
        "smlal   r2, r3, r10, lr\n\t"
        "lsr     r2, r2, #12\n\t"
        "orr     r2, r2, r3, lsl #20\n\t"
        "sub     r1, r1, r2\n\t"
        "ldr     r3, [r6, #4]\n\t"        /* g0sh */
        "and     r2, r5, #255\n\t"        /* wrapMode[0] */
        "cmp     r2, #1\n\t"
        "addeq   r2, r3, #21\n\t"
        "moveq   r3, #0\n\t"
        "lsrne   r2, r2, #1\n\t"
        "addne   r3, r3, r2\n\t"
        "rsbne   r2, r2, #32\n\t"
        "rsbne   r3, r3, #11\n\t"
        "lsl     r1, r1, r3\n\t"
        "lsl     r12, r12, r3\n\t"
        "lsl     lr, lr, r3\n\t"
        "str     r1, [r0, #192]\n\t"
        "str     r12, [r0, #232]\n\t"
        "str     lr, [r0, #272]\n\t"
        "lsr     r1, r7, #8\n\t"
        "and     r1, r1, #255\n\t"
        "sub     r2, r2, r1\n\t"
        "str     r2, [r11, #24]\n\t"
        "pop     {r4-r11, pc}\n\t"
    "14:\n\t"
        "str     r2, [r0, #232]\n\t"
        "str     r2, [r0, #272]\n\t"
        "str     r2, [r0, #192]\n\t"
        "str     r2, [r11, #24]\n\t"
        "pop     {r4-r11, pc}\n\t");
}

typedef char gr_check_tud_vtc_[offsetof(_grVertex, texCoord) == 56 ? 1 : -1];
typedef char gr_check_tud_ppref_[offsetof(_grPixelPipe, reference) == 192 ? 1 : -1];
typedef char gr_check_tud_ppxa_[offsetof(_grPixelPipe, xAdder) == 232 ? 1 : -1];
typedef char gr_check_tud_ppya_[offsetof(_grPixelPipe, yAdder) == 272 ? 1 : -1];
typedef char gr_check_tud_gr_[GR_GRADIENT_TEX0_S == 6 && GR_GRADIENT_TEX0_T == 7 ? 1 : -1];

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noclone, naked))
#else
__attribute__((noinline, naked))
#endif
static void grSetupTextureUnit0AndDeltas(Gerbera_s * restrict state, const _grGradientInfo * restrict info,
                                         const _grVertex * restrict v0, const _grVertex * restrict v1,
                                         const _grVertex * restrict v2)
{
    (void)state; (void)info; (void)v0; (void)v1; (void)v2;
    __asm__ volatile(
        "push    {r4-r11, lr}\n\t"
        "add     lr, r0, #4096\n\t"
        "ldr     r5, [lr, #1012]\n\t"      /* textureUnit[0].texture */
        "ldr     r4, [r1]\n\t"             /* pipe = info->pPP */
        "ldrb    r12, [r5, #183]\n\t"      /* parmMask */
        "tst     r12, #32\n\t"
        "beq     0f\n\t"
        "ldrh    r12, [r5, #206]\n\t"      /* filterMask[0..1] */
        "tst     r12, #2\n\t"
        "bne     20f\n\t"
        "eor     lr, r12, r12, lsr #8\n\t"
        "tst     lr, #255\n\t"
        "bne     20f\n\t"
    "0:\n\t"
        "ldr     r0, [sp, #36]\n\t"        /* v2 */
        "ldrb    r6, [r5, #207]\n\t"       /* filterMask[1] */
        "ldrsh   r7, [r5, #4]\n\t"         /* log2S */
        "ldrsh   r8, [r5, #6]\n\t"         /* log2T */
        "ldr     r9, [r5, #0]\n\t"         /* pixels */
        "ldr     r10, [r5, #176]\n\t"      /* palette */
        "and     r6, r6, #1\n\t"           /* linear */
        "mov     r12, #1\n\t"
        "lsl     r11, r12, r7\n\t"
        "sub     r11, r11, #1\n\t"         /* sMask */
        "lsl     r12, r12, r8\n\t"
        "sub     r12, r12, #1\n\t"         /* tMask */
        "str     r9, [r4, #4]\n\t"
        "str     r10, [r4, #8]\n\t"
        "str     r6, [r4, #12]\n\t"
        "str     r11, [r4, #16]\n\t"
        "str     r12, [r4, #20]\n\t"
        "str     r7, [r4, #32]\n\t"
        "ldrh    r12, [r5, #204]\n\t"      /* wrapMode[0] | (wrapMode[1] << 8) */
        "orr     r5, r12, r6, lsl #16\n\t" /* wrapS | (wrapT << 8) | (linear << 16) */
        "orr     r7, r7, r8, lsl #8\n\t"   /* log2S | (log2T << 8) */
        /* coord = 1 (T, +60, grad 7) */
        "cmp     r8, #0\n\t"
        "beq     10f\n\t"
        "ldr     r9, [r2, #60]\n\t"        /* d0 */
        "ldr     r10, [r3, #60]\n\t"       /* d1 */
        "ldr     r11, [r0, #60]\n\t"       /* d2 */
        "eor     r6, r9, r9, asr #31\n\t"
        "sub     r6, r6, r9, asr #31\n\t"
        "eor     r12, r10, r10, asr #31\n\t"
        "sub     r12, r12, r10, asr #31\n\t"
        "orr     r6, r6, r12\n\t"
        "eor     r12, r11, r11, asr #31\n\t"
        "sub     r12, r12, r11, asr #31\n\t"
        "orr     r6, r6, r12\n\t"
        "clz     r6, r6\n\t"
        "sub     r6, r6, #1\n\t"
        "asr     r12, r6, #31\n\t"
        "bic     r6, r6, r12\n\t"
        "cmp     r6, #11\n\t"
        "movgt   r6, #11\n\t"              /* clamped */
        "lsl     r9, r9, r6\n\t"
        "asr     r9, r9, #1\n\t"           /* d0 */
        "lsl     r10, r10, r6\n\t"
        "asr     r10, r10, #1\n\t"         /* d1 */
        "lsl     r11, r11, r6\n\t"
        "asr     r11, r11, #1\n\t"         /* d2 */
        "sub     r10, r10, r9\n\t"         /* g01 */
        "sub     r11, r11, r9\n\t"         /* g02 */
        "sub     r6, r6, #1\n\t"           /* g0sh */
        "tst     r5, #0x10000\n\t"
        "beq     1f\n\t"
        "subs    r12, r8, r6\n\t"
        "mov     lr, #0x100000\n\t"
        "lsrge   r12, lr, r12\n\t"
        "rsblt   r12, r12, #0\n\t"
        "lsllt   r12, lr, r12\n\t"
        "sub     r9, r9, r12\n\t"          /* d0 - bias */
    "1:\n\t"
        "orrs    r12, r10, r11\n\t"
        "moveq   r10, #0\n\t"
        "moveq   r11, #0\n\t"
        "beq     4f\n\t"
        "ldr     r0, [r1, #16]\n\t"        /* v01sy */
        "rsb     lr, r10, #0\n\t"          /* -g01 */
        "smull   r8, r12, r0, r11\n\t"
        "ldr     r0, [r1, #24]\n\t"        /* v02sy */
        "smlal   r8, r12, r0, lr\n\t"
        "ldr     r0, [r1, #28]\n\t"        /* shift */
        "cmp     r0, #18\n\t"
        "bge     2f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     lr, r0, #32\n\t"
        "orr     lr, r12, r8, lsr lr\n\t"
        "b       3f\n\t"
    "2:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     lr, r12, r0\n\t"
    "3:\n\t"
        "ldr     r0, [r1, #20]\n\t"        /* v02sx */
        "rsb     r11, r11, #0\n\t"         /* -g02 */
        "smull   r8, r12, r0, r10\n\t"
        "ldr     r0, [r1, #12]\n\t"        /* v01sx */
        "smlal   r8, r12, r0, r11\n\t"
        "ldr     r0, [r1, #28]\n\t"
        "cmp     r0, #18\n\t"
        "bge     5f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     r10, r0, #32\n\t"
        "orr     r11, r12, r8, lsr r10\n\t"
        "b       6f\n\t"
    "5:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     r11, r12, r0\n\t"
    "6:\n\t"
        "mov     r10, lr\n\t"              /* xAdd */
        "ldr     r0, [r1, #4]\n\t"         /* v0sx */
        "ldr     r8, [r1, #8]\n\t"         /* v0sy */
        "smull   r12, lr, r0, r10\n\t"
        "smlal   r12, lr, r8, r11\n\t"
        "lsr     r12, r12, #12\n\t"
        "orr     r12, r12, lr, lsl #20\n\t"
        "sub     r9, r9, r12\n\t"
    "4:\n\t"
        "lsr     r8, r5, #8\n\t"
        "and     r8, r8, #255\n\t"
        "cmp     r8, #1\n\t"
        "addeq   r8, r6, #21\n\t"
        "moveq   r6, #0\n\t"
        "lsrne   r8, r8, #1\n\t"
        "addne   r6, r6, r8\n\t"
        "rsbne   r8, r8, #32\n\t"
        "rsbne   r6, r6, #11\n\t"
        "lsl     r9, r9, r6\n\t"
        "lsl     r10, r10, r6\n\t"
        "lsl     r11, r11, r6\n\t"
        "str     r9, [r4, #220]\n\t"
        "str     r10, [r4, #260]\n\t"
        "str     r11, [r4, #300]\n\t"
        "sub     r8, r8, r7, lsr #8\n\t"
        "str     r8, [r4, #28]\n\t"
        "ldr     r0, [sp, #36]\n\t"        /* reload v2 */
        "b       11f\n\t"
    "10:\n\t"
        "mov     r8, #0\n\t"
        "str     r8, [r4, #220]\n\t"
        "str     r8, [r4, #260]\n\t"
        "str     r8, [r4, #300]\n\t"
        "str     r8, [r4, #28]\n\t"
    "11:\n\t"
        /* coord = 0 (S, +56, grad 6) */
        "ands    r8, r7, #255\n\t"         /* log2S */
        "beq     19f\n\t"
        "ldr     r9, [r2, #56]\n\t"        /* d0 */
        "ldr     r10, [r3, #56]\n\t"       /* d1 */
        "ldr     r11, [r0, #56]\n\t"       /* d2 */
        "eor     r6, r9, r9, asr #31\n\t"
        "sub     r6, r6, r9, asr #31\n\t"
        "eor     r12, r10, r10, asr #31\n\t"
        "sub     r12, r12, r10, asr #31\n\t"
        "orr     r6, r6, r12\n\t"
        "eor     r12, r11, r11, asr #31\n\t"
        "sub     r12, r12, r11, asr #31\n\t"
        "orr     r6, r6, r12\n\t"
        "clz     r6, r6\n\t"
        "sub     r6, r6, #1\n\t"
        "asr     r12, r6, #31\n\t"
        "bic     r6, r6, r12\n\t"
        "cmp     r6, #11\n\t"
        "movgt   r6, #11\n\t"
        "lsl     r9, r9, r6\n\t"
        "asr     r9, r9, #1\n\t"
        "lsl     r10, r10, r6\n\t"
        "asr     r10, r10, #1\n\t"
        "lsl     r11, r11, r6\n\t"
        "asr     r11, r11, #1\n\t"
        "sub     r10, r10, r9\n\t"
        "sub     r11, r11, r9\n\t"
        "sub     r6, r6, #1\n\t"
        "tst     r5, #0x10000\n\t"
        "beq     12f\n\t"
        "subs    r12, r8, r6\n\t"
        "mov     lr, #0x100000\n\t"
        "lsrge   r12, lr, r12\n\t"
        "rsblt   r12, r12, #0\n\t"
        "lsllt   r12, lr, r12\n\t"
        "sub     r9, r9, r12\n\t"
    "12:\n\t"
        "orrs    r12, r10, r11\n\t"
        "moveq   r10, #0\n\t"
        "moveq   r11, #0\n\t"
        "beq     17f\n\t"
        "ldr     r0, [r1, #16]\n\t"
        "rsb     lr, r10, #0\n\t"
        "smull   r8, r12, r0, r11\n\t"
        "ldr     r0, [r1, #24]\n\t"
        "smlal   r8, r12, r0, lr\n\t"
        "ldr     r0, [r1, #28]\n\t"
        "cmp     r0, #18\n\t"
        "bge     13f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     lr, r0, #32\n\t"
        "orr     lr, r12, r8, lsr lr\n\t"
        "b       14f\n\t"
    "13:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     lr, r12, r0\n\t"
    "14:\n\t"
        "ldr     r0, [r1, #20]\n\t"
        "rsb     r11, r11, #0\n\t"
        "smull   r8, r12, r0, r10\n\t"
        "ldr     r0, [r1, #12]\n\t"
        "smlal   r8, r12, r0, r11\n\t"
        "ldr     r0, [r1, #28]\n\t"
        "cmp     r0, #18\n\t"
        "bge     15f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     r10, r0, #32\n\t"
        "orr     r11, r12, r8, lsr r10\n\t"
        "b       16f\n\t"
    "15:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     r11, r12, r0\n\t"
    "16:\n\t"
        "mov     r10, lr\n\t"
        "ldr     r0, [r1, #4]\n\t"
        "ldr     r8, [r1, #8]\n\t"
        "smull   r12, lr, r0, r10\n\t"
        "smlal   r12, lr, r8, r11\n\t"
        "lsr     r12, r12, #12\n\t"
        "orr     r12, r12, lr, lsl #20\n\t"
        "sub     r9, r9, r12\n\t"
    "17:\n\t"
        "and     r8, r5, #255\n\t"
        "cmp     r8, #1\n\t"
        "addeq   r8, r6, #21\n\t"
        "moveq   r6, #0\n\t"
        "lsrne   r8, r8, #1\n\t"
        "addne   r6, r6, r8\n\t"
        "rsbne   r8, r8, #32\n\t"
        "rsbne   r6, r6, #11\n\t"
        "lsl     r9, r9, r6\n\t"
        "lsl     r10, r10, r6\n\t"
        "lsl     r11, r11, r6\n\t"
        "str     r9, [r4, #216]\n\t"
        "str     r10, [r4, #256]\n\t"
        "str     r11, [r4, #296]\n\t"
        "and     r7, r7, #255\n\t"
        "sub     r8, r8, r7\n\t"
        "str     r8, [r4, #24]\n\t"
        "pop     {r4-r11, pc}\n\t"
    "19:\n\t"
        "mov     r8, #0\n\t"
        "str     r8, [r4, #216]\n\t"
        "str     r8, [r4, #256]\n\t"
        "str     r8, [r4, #296]\n\t"
        "str     r8, [r4, #24]\n\t"
        "pop     {r4-r11, pc}\n\t"
    "20:\n\t"
        /* Mipmapped path: compute g0, xAdd/yAdd, maxRho2, mipLevel, then finish T & S */
        "sub     sp, sp, #32\n\t"
        "mov     r12, #0\n\t"
        "str     r5, [sp, #16]\n\t"        /* texture */
        "str     r12, [sp, #20]\n\t"       /* maxRho2_lo */
        "str     r12, [sp, #24]\n\t"       /* maxRho2_hi */
        "str     r1, [sp, #28]\n\t"        /* info */
        "mov     r5, r1\n\t"               /* r5 = info */
        "ldr     r6, [sp, #68]\n\t"        /* v2 */
        "mov     r7, #1\n\t"
    "21:\n\t"
        "cmp     r7, #0\n\t"
        "ldrne   r9, [r2, #60]\n\t"
        "ldrne   r10, [r3, #60]\n\t"
        "ldrne   r11, [r6, #60]\n\t"
        "ldreq   r9, [r2, #56]\n\t"
        "ldreq   r10, [r3, #56]\n\t"
        "ldreq   r11, [r6, #56]\n\t"
        "eor     r0, r9, r9, asr #31\n\t"
        "sub     r0, r0, r9, asr #31\n\t"
        "eor     r12, r10, r10, asr #31\n\t"
        "sub     r12, r12, r10, asr #31\n\t"
        "orr     r0, r0, r12\n\t"
        "eor     r12, r11, r11, asr #31\n\t"
        "sub     r12, r12, r11, asr #31\n\t"
        "orr     r0, r0, r12\n\t"
        "clz     r0, r0\n\t"
        "sub     r0, r0, #1\n\t"
        "asr     r12, r0, #31\n\t"
        "bic     r0, r0, r12\n\t"
        "cmp     r0, #11\n\t"
        "movgt   r0, #11\n\t"
        "lsl     r9, r9, r0\n\t"
        "asr     r9, r9, #1\n\t"
        "lsl     r10, r10, r0\n\t"
        "asr     r10, r10, #1\n\t"
        "lsl     r11, r11, r0\n\t"
        "asr     r11, r11, #1\n\t"
        "sub     r10, r10, r9\n\t"
        "sub     r11, r11, r9\n\t"
        "sub     r0, r0, #1\n\t"
        "add     r1, sp, r7, lsl #3\n\t"
        "str     r9, [r1]\n\t"             /* d0 */
        "str     r0, [r1, #4]\n\t"         /* g0sh */
        "mov     r9, r0\n\t"               /* keep g0sh in r9 */
        "orrs    r12, r10, r11\n\t"
        "moveq   r10, #0\n\t"
        "moveq   r11, #0\n\t"
        "beq     26f\n\t"
        "ldr     r0, [r5, #16]\n\t"
        "rsb     lr, r10, #0\n\t"
        "smull   r8, r12, r0, r11\n\t"
        "ldr     r0, [r5, #24]\n\t"
        "smlal   r8, r12, r0, lr\n\t"
        "ldr     r0, [r5, #28]\n\t"
        "cmp     r0, #18\n\t"
        "bge     22f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     lr, r0, #32\n\t"
        "orr     lr, r12, r8, lsr lr\n\t"
        "b       23f\n\t"
    "22:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     lr, r12, r0\n\t"
    "23:\n\t"
        "ldr     r0, [r5, #20]\n\t"
        "rsb     r11, r11, #0\n\t"
        "smull   r8, r12, r0, r10\n\t"
        "ldr     r0, [r5, #12]\n\t"
        "smlal   r8, r12, r0, r11\n\t"
        "ldr     r0, [r5, #28]\n\t"
        "cmp     r0, #18\n\t"
        "bge     24f\n\t"
        "rsb     r0, r0, #18\n\t"
        "lsl     r12, r12, r0\n\t"
        "rsb     r10, r0, #32\n\t"
        "orr     r11, r12, r8, lsr r10\n\t"
        "b       25f\n\t"
    "24:\n\t"
        "sub     r0, r0, #18\n\t"
        "asr     r11, r12, r0\n\t"
    "25:\n\t"
        "mov     r10, lr\n\t"
    "26:\n\t"
        "add     r0, r7, #6\n\t"
        "add     r0, r4, r0, lsl #2\n\t"
        "str     r10, [r0, #232]\n\t"
        "str     r11, [r0, #272]\n\t"
        "smull   r8, r12, r10, r10\n\t"
        "smlal   r8, r12, r11, r11\n\t"
        "ldr     lr, [sp, #16]\n\t"        /* texture */
        "add     lr, lr, r7, lsl #1\n\t"
        "ldrsh   r10, [lr, #4]\n\t"        /* mipmap[0].log2Dim[coord] */
        "rsb     r10, r10, #7\n\t"
        "add     r10, r10, r9\n\t"         /* 7 - shift + g0sh */
        "lsl     r10, r10, #1\n\t"
        "asr     r11, r10, #31\n\t"
        "bic     r10, r10, r11\n\t"
        "cmp     r10, #63\n\t"
        "movgt   r10, #63\n\t"
        "cmp     r10, #31\n\t"
        "bgt     27f\n\t"
        "rsb     r11, r10, #32\n\t"
        "lsr     r8, r8, r10\n\t"
        "orr     r8, r8, r12, lsl r11\n\t"
        "lsr     r12, r12, r10\n\t"
        "b       28f\n\t"
    "27:\n\t"
        "sub     r11, r10, #32\n\t"
        "lsr     r8, r12, r11\n\t"
        "mov     r12, #0\n\t"
    "28:\n\t"
        "ldr     r10, [sp, #20]\n\t"
        "ldr     r11, [sp, #24]\n\t"
        "cmp     r10, r8\n\t"
        "sbcs    r9, r11, r12\n\t"
        "strlo   r8, [sp, #20]\n\t"
        "strlo   r12, [sp, #24]\n\t"
        "subs    r7, r7, #1\n\t"
        "bge     21b\n\t"
        "ldr     r5, [sp, #16]\n\t"        /* texture */
        "ldr     r10, [sp, #20]\n\t"
        "ldr     r11, [sp, #24]\n\t"
        "cmp     r11, #0x8000\n\t"
        "bhs     29f\n\t"
        "lsl     r0, r11, #16\n\t"
        "orr     r0, r0, r10, lsr #16\n\t"
        "add     r0, r0, r0, lsr #3\n\t"
        "clz     r0, r0\n\t"
        "rsbs    r0, r0, #20\n\t"
        "movle   r0, #0\n\t"
        "asrgt   r0, r0, #1\n\t"
        "cmp     r0, #0\n\t"
        "moveq   r1, #1\n\t"
        "beq     30f\n\t"
        "mov     r1, #0\n\t"
        "ldrsh   r2, [r5, #180]\n\t"
        "sub     r2, r2, #1\n\t"
        "cmp     r0, r2\n\t"
        "movgt   r0, r2\n\t"
        "b       30f\n\t"
    "29:\n\t"
        "mov     r1, #0\n\t"
        "ldrsh   r0, [r5, #180]\n\t"
        "sub     r0, r0, #1\n\t"
    "30:\n\t"
        "ldrb    r2, [r5, #206]\n\t"
        "tst     r2, #2\n\t"
        "moveq   r0, #0\n\t"
        "add     r2, r5, #206\n\t"
        "ldrb    r6, [r2, r1]\n\t"
        "and     r6, r6, #1\n\t"           /* linear */
        "add     r0, r5, r0, lsl #4\n\t"   /* mipmap[mipLevel] */
        "ldrsh   r7, [r0, #4]\n\t"         /* log2S */
        "ldrsh   r8, [r0, #6]\n\t"         /* log2T */
        "ldr     r9, [r0, #0]\n\t"
        "ldr     r10, [r5, #176]\n\t"
        "mov     r12, #1\n\t"
        "lsl     r11, r12, r7\n\t"
        "sub     r11, r11, #1\n\t"
        "lsl     r12, r12, r8\n\t"
        "sub     r12, r12, #1\n\t"
        "str     r9, [r4, #4]\n\t"
        "str     r10, [r4, #8]\n\t"
        "str     r6, [r4, #12]\n\t"
        "str     r11, [r4, #16]\n\t"
        "str     r12, [r4, #20]\n\t"
        "str     r7, [r4, #32]\n\t"
        "ldrh    r12, [r5, #204]\n\t"
        "orr     r5, r12, r6, lsl #16\n\t"
        "orr     r7, r7, r8, lsl #8\n\t"
        "ldr     r1, [sp, #28]\n\t"        /* info */
        "ldr     r2, [r1, #4]\n\t"         /* v0sx */
        "ldr     r3, [r1, #8]\n\t"         /* v0sy */
        /* T finish */
        "cmp     r8, #0\n\t"
        "beq     32f\n\t"
        "ldr     r9, [sp, #8]\n\t"         /* d0_T */
        "ldr     r6, [sp, #12]\n\t"        /* g0sh_T */
        "ldr     r10, [r4, #260]\n\t"
        "ldr     r11, [r4, #300]\n\t"
        "tst     r5, #0x10000\n\t"
        "beq     31f\n\t"
        "subs    r12, r8, r6\n\t"
        "mov     lr, #0x100000\n\t"
        "lsrge   r12, lr, r12\n\t"
        "rsblt   r12, r12, #0\n\t"
        "lsllt   r12, lr, r12\n\t"
        "sub     r9, r9, r12\n\t"
    "31:\n\t"
        "smull   r12, lr, r2, r10\n\t"
        "smlal   r12, lr, r3, r11\n\t"
        "lsr     r12, r12, #12\n\t"
        "orr     r12, r12, lr, lsl #20\n\t"
        "sub     r9, r9, r12\n\t"
        "lsr     r8, r5, #8\n\t"
        "and     r8, r8, #255\n\t"
        "cmp     r8, #1\n\t"
        "addeq   r8, r6, #21\n\t"
        "moveq   r6, #0\n\t"
        "lsrne   r8, r8, #1\n\t"
        "addne   r6, r6, r8\n\t"
        "rsbne   r8, r8, #32\n\t"
        "rsbne   r6, r6, #11\n\t"
        "lsl     r9, r9, r6\n\t"
        "lsl     r10, r10, r6\n\t"
        "lsl     r11, r11, r6\n\t"
        "str     r9, [r4, #220]\n\t"
        "str     r10, [r4, #260]\n\t"
        "str     r11, [r4, #300]\n\t"
        "sub     r8, r8, r7, lsr #8\n\t"
        "str     r8, [r4, #28]\n\t"
        "b       33f\n\t"
    "32:\n\t"
        "mov     r8, #0\n\t"
        "str     r8, [r4, #220]\n\t"
        "str     r8, [r4, #260]\n\t"
        "str     r8, [r4, #300]\n\t"
        "str     r8, [r4, #28]\n\t"
    "33:\n\t"
        /* S finish */
        "ands    r8, r7, #255\n\t"
        "beq     35f\n\t"
        "ldr     r9, [sp, #0]\n\t"         /* d0_S */
        "ldr     r6, [sp, #4]\n\t"         /* g0sh_S */
        "ldr     r10, [r4, #256]\n\t"
        "ldr     r11, [r4, #296]\n\t"
        "tst     r5, #0x10000\n\t"
        "beq     34f\n\t"
        "subs    r12, r8, r6\n\t"
        "mov     lr, #0x100000\n\t"
        "lsrge   r12, lr, r12\n\t"
        "rsblt   r12, r12, #0\n\t"
        "lsllt   r12, lr, r12\n\t"
        "sub     r9, r9, r12\n\t"
    "34:\n\t"
        "smull   r12, lr, r2, r10\n\t"
        "smlal   r12, lr, r3, r11\n\t"
        "lsr     r12, r12, #12\n\t"
        "orr     r12, r12, lr, lsl #20\n\t"
        "sub     r9, r9, r12\n\t"
        "and     r8, r5, #255\n\t"
        "cmp     r8, #1\n\t"
        "addeq   r8, r6, #21\n\t"
        "moveq   r6, #0\n\t"
        "lsrne   r8, r8, #1\n\t"
        "addne   r6, r6, r8\n\t"
        "rsbne   r8, r8, #32\n\t"
        "rsbne   r6, r6, #11\n\t"
        "lsl     r9, r9, r6\n\t"
        "lsl     r10, r10, r6\n\t"
        "lsl     r11, r11, r6\n\t"
        "str     r9, [r4, #216]\n\t"
        "str     r10, [r4, #256]\n\t"
        "str     r11, [r4, #296]\n\t"
        "and     r7, r7, #255\n\t"
        "sub     r8, r8, r7\n\t"
        "str     r8, [r4, #24]\n\t"
        "add     sp, sp, #32\n\t"
        "pop     {r4-r11, pc}\n\t"
    "35:\n\t"
        "mov     r8, #0\n\t"
        "str     r8, [r4, #216]\n\t"
        "str     r8, [r4, #256]\n\t"
        "str     r8, [r4, #296]\n\t"
        "str     r8, [r4, #24]\n\t"
        "add     sp, sp, #32\n\t"
        "pop     {r4-r11, pc}\n\t");
}
#else
static inline int grComputeMipLevel(const _grTexture * restrict texture,
                                    const _grPixelPipe * restrict pipe,
                                    int texIndex, unsigned int activeGradMask,
                                    const int (* restrict g0)[2], int * restrict isMagOut)
{
    int mipLevel = 0;
    int isMag = 1;
    HGuint64 maxRho2 = hgToUnsigned64(hgZero64());
    int i;

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
    *isMagOut = isMag;
    return mipLevel;
}

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
static void grSetupTextureUnitCoord(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
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
        mipLevel = grComputeMipLevel(texture, pipe, texIndex, activeGradMask,
                                     (const int (*)[2])g0, &isMag);
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
                int g0sh = g0[i][1];
                int g0val = g0[i][0];
                int xAdd = pipe->xAdder[gradIdx];
                int yAdd = pipe->yAdder[gradIdx];
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
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
                    unsigned int lo;
                    int hi;
                    __asm__("smull %0, %1, %2, %3\n\t"
                            "smlal %0, %1, %4, %5"
                            : "=&r"(lo), "=&r"(hi)
                            : "r"(v0sx), "r"(xAdd), "r"(v0sy), "r"(yAdd));
                    ref = (int)((unsigned int)g0val -
                                (((unsigned int)hi << 20) | (lo >> 12)) -
                                (unsigned int)bias);
#else
                    HGint64 v5 = hgMadd64(hgMul64(v0sx, xAdd), v0sy, yAdd);
                    ref = (int)((unsigned int)g0val -
                                (unsigned int)hgLsl64h_0_31(v5, 20) -
                                (unsigned int)bias);
#endif
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
#endif

void grSetupTextureUnit(Gerbera_s * restrict state, int texIndex, int (* restrict g0)[2],
                        int v0sx, int v0sy)
{
    grSetupTextureUnitCoord(state, texIndex, g0, v0sx, v0sy);
}
