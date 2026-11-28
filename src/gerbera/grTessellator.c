/* grTessellator.c - perspective-correct triangle subdivision (port of
 * grTessellator.inl).
 *
 * Big triangles whose clip-w varies too much across their area are split
 * recursively (up to 8 levels) into affine-safe pieces; the per-vertex w
 * rides in the vertex records' normal[2] (the third clip-factor slot) and
 * the split points' barycentric weights in normal[0..1], so every piece
 * flows through the ordinary affine draw path (grDrawClippedTriangle).
 * Split vertices live in the vertex cache's temp region (slots 68..91,
 * disjoint from the clipper's 92+).
 */
#include "gerbera/grState.h"
#include "gerbera/grDefs.h"
#include "imath/imApi.h"
#include "hg/hgInt32.h"
#include <string.h>

int grSubpixelTrunc(int i);   /* grClipper.c (grUtil.h; DLL 004214A0) */

#define GR_VERTEX_BUFFER(state) \
    ((_grVertex *)((unsigned char *)(state)->renderStack + 1332))

#if defined(__GNUC__) || defined(__clang__)
#define GR_TESS_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define GR_TESS_ALWAYS_INLINE inline
#endif

/* --- grFracDiv (grTessellator.inl 49; DLL 26703-26725) -----------------------
 * a/b as an IUNIT fraction ([0, IUNITSCALAR_ONE]; a <= b asserted). */

static inline int grFracDivFromRecip(int a, HGint64 reciprocal)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5)
    unsigned int lo;
    int hi;
    unsigned int recipHi = (unsigned int)((HGuint64)reciprocal >> 32);
    unsigned int sh = (unsigned int)reciprocal & 31u;
    unsigned int rsh = 32u - sh;

    __asm__(
        "umull %0, %1, %2, %3\n\t"
        "lsl %1, %1, %4\n\t"
        "orr %1, %1, %0, lsr %5"
        : "=&r"(lo), "=&r"(hi)
        : "r"((unsigned int)a), "r"(recipHi), "r"(sh), "r"(rsh));
    return hi / 2;
#else
    HGuint64 u = hgMulu64((unsigned int)a, (unsigned int)(reciprocal >> 32));
    HGint64 s = hgToSigned64(u);
    return hgLsl64h(s, (int)reciprocal) / 2;
#endif
}

static inline int grFracDivInline(int a, int b)
{
    if (b <= 0 && grAssert("b > 0", "grTessellator.inl", 49) != 0)
        grDie(0);
    if (a > b && grAssert("a <= b", "grTessellator.inl", 50) != 0)
        grDie(0);
    return grFracDivFromRecip(a, imRcpu_inline(b));
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
int grFracDiv(int a, int b)
{
    return grFracDivInline(a, b);
}

/* --- grLerp (grTessellator.inl 70; DLL 26728-26742) --------------------------
 * ((IUNITSCALAR_ONE - factor) * a + factor * b) >> 29. */

static inline int grLerpInline(int a, int b, int factor)
{
    HGint64 v;

    if (hgInRange32(factor, 0, IUNITSCALAR_ONE) == 0 &&
        grAssert("hgInRange32(factor, 0, IUNITSCALAR_ONE)", "grTessellator.inl", 70) != 0)
        grDie(0);
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    (void)v;
    {
        unsigned int lo;
        int hi;
        int oneMinus = IUNITSCALAR_ONE - factor;
        __asm__("smull %0, %1, %2, %3\n\t"
                "smlal %0, %1, %4, %5"
                : "=&r"(lo), "=&r"(hi)
                : "r"(oneMinus), "r"(a), "r"(factor), "r"(b));
        return (int)(((unsigned int)hi << 3) | (lo >> 29));
    }
#else
    v = hgMul64(IUNITSCALAR_ONE - factor, a);
    v = hgMadd64(v, factor, b);
    return hgLsl64h_0_31(v, 3);
#endif
}

int grLerp(int a, int b, int factor)
{
    return grLerpInline(a, b, factor);
}

/* --- grClassifyTriangle (grTessellator.inl 92; DLL 26745-26841) --------------
 * AFFINE when the bbox extent is under 4px; DISCARD when the pixel-rounded
 * bbox misses the scissor; otherwise the clip-w variation test: subdivide
 * (PERSPECTIVE) when maxW' * (4096-extent)^2 >= minW * (4096+extent)^2
 * (maxW' = max(maxW, minW + minW>>4) absorbs a 1/16 w tolerance). */

int grClassifyTriangle(Gerbera_s *state, _grVertex *vertexCache,
                       unsigned int indices)
{
    _grRect *scissor = &state->renderScissor;
    int i0 = grUnpackIndex(indices, 0);
    int i1 = grUnpackIndex(indices, 1);
    int i2 = grUnpackIndex(indices, 2);
    int x0 = vertexCache[i0].screenPosition[0];
    int x1 = vertexCache[i1].screenPosition[0];
    int x2 = vertexCache[i2].screenPosition[0];
    int y0 = vertexCache[i0].screenPosition[1];
    int y1 = vertexCache[i1].screenPosition[1];
    int y2 = vertexCache[i2].screenPosition[1];
    int xMin = hgMin32(hgMin32(x0, x1), x2);
    int xMax = hgMax32(hgMax32(x0, x1), x2);
    int yMin = hgMin32(hgMin32(y0, y1), y2);
    int yMax = hgMax32(hgMax32(y0, y1), y2);
    int extent = hgMax32(xMax - xMin, yMax - yMin);
    int w0 = vertexCache[i0].normal[2];
    int w1 = vertexCache[i1].normal[2];
    int w2 = vertexCache[i2].normal[2];
    int wMin = hgMin32(hgMin32(w0, w1), w2);
    int wMax = hgMax32(hgMax32(w0, w1), w2);
    int wMaxBoosted;
    HGint64 smallSq, bigSq;

    if ((state == NULL || vertexCache == NULL) &&
        grAssert("state && vertexCache", "grTessellator.inl", 92) != 0)
        grDie(0);
    if (extent < 0x4000)
        return GR_TRIANGLESTATUS_AFFINE;
    if (grSubpixelTrunc(xMax) <= scissor->x0)
        return GR_TRIANGLESTATUS_DISCARD;
    if (grSubpixelTrunc(xMin) >= scissor->x1)
        return GR_TRIANGLESTATUS_DISCARD;
    if (grSubpixelTrunc(yMax) <= scissor->y0)
        return GR_TRIANGLESTATUS_DISCARD;
    if (grSubpixelTrunc(yMin) >= scissor->y1)
        return GR_TRIANGLESTATUS_DISCARD;
    if (wMin == wMax)
        return (wMin <= 0) ? GR_TRIANGLESTATUS_NONAFFINE : GR_TRIANGLESTATUS_AFFINE;
    wMaxBoosted = hgMax32(wMax, wMin + (wMin >> 4));
    smallSq = hgLsl64h(hgMul64(4096 - extent, 4096 - extent), 10);
    bigSq = hgLsl64h(hgMul64(extent + 4096, extent + 4096), 10);
    return hgLessThan64(hgMul64(wMaxBoosted, smallSq),
                        hgMul64(wMin, bigSq)) == 0;
}

/* --- grSplitEdge (grTessellator.inl 195; DLL 26844-26944) --------------------
 * Tries to split edge v0-v1 into the temp record dst. Returns 0 when the
 * edge is too short or its w variance is affine-safe and forceSplit == 0;
 * otherwise computes the perspective-biased split position, lerps the clip
 * factors and the w, and snaps the screen position to the midpoint. */

static GR_TESS_ALWAYS_INLINE int grSplitEdgeTail(_grVertex *dst, _grVertex *v0, _grVertex *v1,
                                                 int extent, int forceSplit, int willSplit)
{
    int w0, w1, t17, fdw2, factor1, t;
    _grVertex *swap;
    HGint64 sq, r;

    w0 = v0->normal[2];
    w1 = v1->normal[2];
    if (w0 == w1)
    {
        if (w1 > 0 && forceSplit == 0)
            return 0;
    }
    else if (w0 < w1)
    {
        int wt = w0;

        w0 = w1;
        w1 = wt;
        swap = v0;
        v0 = v1;
        v1 = swap;
    }
    if (w0 < w1 && grAssert("w0 >= w1", "grTessellator.inl", 195) != 0)
        grDie(0);
    if (w0 != w1)
    {
        int wBoost = w1 + (w1 >> 4);
        if (w0 <= wBoost)
        {
            if (forceSplit == 0 && extent < 0x40000)
                return 0;
            w0 = wBoost;
        }
    }
    if (w0 < w1 && grAssert("w0 >= w1", "grTessellator.inl", 198) != 0)
        grDie(0);
    sq = hgLsl64h(hgMul64(4096 - extent, 4096 - extent), 10);
    r = hgLsl64h(hgMul64(extent + 4096, extent + 4096), 10);
    if (hgLessThan64(hgMul64(w0, sq), hgMul64(w1, r)) != 0)
    {
        if (forceSplit == 0)
            return 0;
        willSplit = 0;
    }
    {
        int denom = w0 + w1;
        int w0Orig = v0->normal[2];
        HGint64 recipSum = 0;

        t17 = 0;
        if (denom != 0)
        {
            int a = w0 - w1;
            int fdw;
            int dist;

            if (denom <= 0 && grAssert("b > 0", "grTessellator.inl", 49) != 0)
                grDie(0);
            if (a > denom && grAssert("a <= b", "grTessellator.inl", 50) != 0)
                grDie(0);
            recipSum = imRcpu_inline(denom);
            fdw = grFracDivFromRecip(a, recipSum);
            r = hgMul64(fdw, extent);
            dist = (int)hgLsl64h(r, 3);
            dist = hgMin32(dist, 0x2000);
            t17 = hgGet64l(hgMul64(dist, 0x10000));
        }
        if (w0Orig == w0)
        {
            if (denom != 0)
            {
                if (w0Orig > denom && grAssert("a <= b", "grTessellator.inl", 50) != 0)
                    grDie(0);
                fdw2 = grFracDivFromRecip(w0Orig, recipSum);
            }
            else
                fdw2 = 0;
        }
        else
            fdw2 = (w1 + w0Orig != 0) ? grFracDiv(w0Orig, w1 + w0Orig) : 0;
        factor1 = hgMax32(2 * t17 - 0x20000000, 0);
        t = (factor1 != 0)
          ? (0x10000000 + hgLsl64h_0_31(hgMul64(factor1, fdw2 - 0x10000000), 3))
          : 0x10000000;
        dst->normal[2] = grLerpInline(w0Orig, w1, fdw2);
    }
    grLerpClipFactor((_grClipVertex *)&dst->normal[0],
                     (_grClipVertex *)&v0->normal[0],
                     (_grClipVertex *)&v1->normal[0], t);
    dst->screenPosition[0] = ((v1->screenPosition[0] + v0->screenPosition[0]) >> 1) &
                             (int)0xFFFFFF00u;
    dst->screenPosition[1] = ((v1->screenPosition[1] + v0->screenPosition[1]) >> 1) &
                             (int)0xFFFFFF00u;
    dst->screenPosition[2] = (v1->screenPosition[2] + v0->screenPosition[2]) >> 1;
    return willSplit;
}

static GR_TESS_ALWAYS_INLINE int grSplitEdgeImpl(_grVertex *dst, _grVertex *v0, _grVertex *v1, int forceSplit)
{
    int willSplit = 1;
    int extent = hgMax32(hgDiff32(v1->screenPosition[0], v0->screenPosition[0]),
                         hgDiff32(v1->screenPosition[1], v0->screenPosition[1]));

    if (extent < 0x4000)
    {
        if (forceSplit == 0)
            return 0;
        willSplit = 0;
    }
    return grSplitEdgeTail(dst, v0, v1, extent, forceSplit, willSplit);
}

int grSplitEdge(_grVertex *dst, _grVertex *v0, _grVertex *v1, int forceSplit)
{
    return grSplitEdgeImpl(dst, v0, v1, forceSplit);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int grSplitEdgeFast(_grVertex *dst, _grVertex *v0, _grVertex *v1)
{
    return grSplitEdgeImpl(dst, v0, v1, 0);
}

/* --- grTessellateTriangle (grTessellator.inl 283; DLL 26947-27246) ------------
 * One recursion step: classify, split the edges that need it (recording
 * which edges split in splitBits), then either recurse into the children
 * picked by the split-count tables or draw the (now affine) piece. */

static void grTessellateTriangle(Gerbera_s *state, unsigned int indices,
                                 unsigned int origIndices, unsigned int sideLevel);

static GR_TESS_ALWAYS_INLINE HGint64 grComputeTriangleAreaXY(
    int x0, int y0, int x1, int y1, int x2, int y2)
{
    return hgMadd64(hgMul64(x1 - x0, y2 - y0), y1 - y0, x0 - x2);
}

static GR_TESS_ALWAYS_INLINE void grTessellateTriangleDrawLeafXY(
    Gerbera_s *state, unsigned int indices, unsigned int origIndices, int side,
    int x0, int y0, int x1, int y1, int x2, int y2)
{
    HGint64 area64 = grComputeTriangleAreaXY(x0, y0, x1, y1, x2, y2);

    if (hgIsZero64(area64) != 0)
        return;
    {
        _grClipTriangle leaf;

        leaf.indices = indices;
        leaf.area = grRoundTriangleArea(area64);
        leaf.side = side;
        grDrawClippedTriangle(state, &leaf, origIndices, 8);
    }
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void grTessellateTriangleSubdivide(Gerbera_s *state, unsigned int indices,
                                          unsigned int origIndices, unsigned int sideLevel)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    int level = (int)(sideLevel >> 8);
    int side = (int)(signed char)(sideLevel & 0xFFu);
    int base = 3 * level + 68;
    int i0 = grUnpackIndex(indices, 0);
    int i1 = grUnpackIndex(indices, 1);
    int i2 = grUnpackIndex(indices, 2);
    _grVertex *v0 = vertexBuffer + i0;
    _grVertex *v1 = vertexBuffer + i1;
    _grVertex *v2 = vertexBuffer + i2;
    unsigned int splitMask = 0;
    int numSplits = 0;
    int j;

    for (j = 2; j >= 0; --j)
    {
        _grVertex *dst = vertexBuffer + base + j;
        _grVertex *va = (j == 2) ? v2 : ((j == 1) ? v1 : v0);
        _grVertex *vb = (j == 2) ? v0 : ((j == 1) ? v2 : v1);
        int bit = grSplitEdgeFast(dst, va, vb);

        splitMask |= (unsigned int)bit << j;
        numSplits += bit;
        if (bit != 0)
        {
            int emptyBits = 7 - level;
            int x = dst->screenPosition[0];
            int y = dst->screenPosition[1];

            if (((1 << emptyBits) - 1) & x &&
                grAssert("!(x & ((1<<emptyBits)-1))",
                         "grTessellator.inl", 336) != 0)
                grDie(0);
            if (((1 << emptyBits) - 1) & y &&
                grAssert("!(y & ((1<<emptyBits)-1))",
                         "grTessellator.inl", 337) != 0)
                grDie(0);
            dst->index = -1;
            dst->stateMask = 79;
            grInterpolateVertexGradients(state, dst, origIndices, side);
        }
    }
    if (hgInRange32(numSplits, 0, 3) == 0 &&
        grAssert("hgInRange32(numSplits, 0, 3)", "grTessellator.inl", 350) != 0)
        grDie(0);
    if (numSplits != 0 && level != 7)
    {
        unsigned int childSideLevel = sideLevel + 0x100u;

        switch (numSplits)
        {
            case 3:
            {
                int b0 = base;
                int b1 = base + 1;
                int b2 = base + 2;

                grTessellateTriangle(state, grSetIndices(b1, i2, b2),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(b0, i1, b1),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(b0, b1, b2),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(i0, b0, b2),
                                     origIndices, childSideLevel);
                break;
            }
            case 2:
            {
                int p0, p1, p2, mNext, mPrev;

                if ((splitMask & 1u) == 0)
                {
                    p0 = i0; p1 = i1; p2 = i2;
                    mNext = base + 1; mPrev = base + 2;
                }
                else if ((splitMask & 2u) == 0)
                {
                    p0 = i1; p1 = i2; p2 = i0;
                    mNext = base + 2; mPrev = base;
                }
                else
                {
                    p0 = i2; p1 = i0; p2 = i1;
                    mNext = base; mPrev = base + 1;
                }
                grTessellateTriangle(state, grSetIndices(p0, p1, mNext),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(mNext, p2, mPrev),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(p0, mPrev, mNext),
                                     origIndices, childSideLevel);
                break;
            }
            case 1:
            {
                int p0, p1, p2, mp;

                if ((splitMask & 1u) != 0)
                {
                    p0 = i0; p1 = i1; p2 = i2; mp = base;
                }
                else if ((splitMask & 2u) != 0)
                {
                    p0 = i1; p1 = i2; p2 = i0; mp = base + 1;
                }
                else
                {
                    p0 = i2; p1 = i0; p2 = i1; mp = base + 2;
                }
                grTessellateTriangle(state, grSetIndices(p0, mp, p2),
                                     origIndices, childSideLevel);
                grTessellateTriangle(state, grSetIndices(mp, p1, p2),
                                     origIndices, childSideLevel);
                break;
            }
            default:
                break;
        }
    }
    else
    {
        grTessellateTriangleDrawLeafXY(state, indices, origIndices, side,
                                       v0->screenPosition[0], v0->screenPosition[1],
                                       v1->screenPosition[0], v1->screenPosition[1],
                                       v2->screenPosition[0], v2->screenPosition[1]);
    }
}

static void grTessellateTriangle(Gerbera_s *state, unsigned int indices,
                                 unsigned int origIndices, unsigned int sideLevel)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    _grRect *scissor = &state->renderScissor;
    int i0 = grUnpackIndex(indices, 0);
    int i1 = grUnpackIndex(indices, 1);
    int i2 = grUnpackIndex(indices, 2);
    _grVertex *v0 = vertexBuffer + i0;
    _grVertex *v1 = vertexBuffer + i1;
    _grVertex *v2 = vertexBuffer + i2;
    int x0 = v0->screenPosition[0];
    int x1 = v1->screenPosition[0];
    int x2 = v2->screenPosition[0];
    int y0 = v0->screenPosition[1];
    int y1 = v1->screenPosition[1];
    int y2 = v2->screenPosition[1];
    int xMin = hgMin32(hgMin32(x0, x1), x2);
    int xMax = hgMax32(hgMax32(x0, x1), x2);
    int yMin = hgMin32(hgMin32(y0, y1), y2);
    int yMax = hgMax32(hgMax32(y0, y1), y2);
    int extent = hgMax32(xMax - xMin, yMax - yMin);
    int level = (int)(sideLevel >> 8);
    int w0, w1, w2, wMin, wMax;

    if (__builtin_expect(extent < 0x4000, 0))
        goto draw_leaf;
    if (grSubpixelTrunc(xMax) <= scissor->x0 ||
        grSubpixelTrunc(xMin) >= scissor->x1 ||
        grSubpixelTrunc(yMax) <= scissor->y0 ||
        grSubpixelTrunc(yMin) >= scissor->y1)
        return;
    if (__builtin_expect(level == 7, 0))
        goto draw_leaf;
    w0 = v0->normal[2];
    w1 = v1->normal[2];
    w2 = v2->normal[2];
    wMin = hgMin32(hgMin32(w0, w1), w2);
    wMax = hgMax32(hgMax32(w0, w1), w2);
    if (wMin == wMax)
    {
        if (wMin > 0)
            goto draw_leaf;
    }
    else
    {
        int wBoost = wMin + (wMin >> 4);

        if (wMax <= wBoost)
        {
            if (extent < 0x40000)
                goto draw_leaf;
            wMax = wBoost;
        }
        {
            HGint64 smallSq = hgLsl64h(hgMul64(4096 - extent, 4096 - extent), 10);
            HGint64 bigSq = hgLsl64h(hgMul64(extent + 4096, extent + 4096), 10);

            if (hgLessThan64(hgMul64(wMax, smallSq), hgMul64(wMin, bigSq)) != 0)
                goto draw_leaf;
        }
    }
    grTessellateTriangleSubdivide(state, indices, origIndices, sideLevel);
    return;

draw_leaf:
    grTessellateTriangleDrawLeafXY(state, indices, origIndices,
                                   (int)(signed char)(sideLevel & 0xFFu),
                                   x0, y0, x1, y1, x2, y2);
}

/* --- grTriangleTessellator (grTessellator.inl 512; DLL 27249-27359) ----------
 * Seeds the per-vertex w (normalized by IVector4_wNorm so all three share an
 * exponent), checks/seeds the barycentric weights of the triangle's records,
 * and starts the recursion. */

void grTriangleTessellator(Gerbera_s *state, _grClipTriangle *tri,
                           unsigned int origIndices)
{
    _grVertex *vertexBuffer;
    _grVertex *v0;
    _grVertex *v1;
    _grVertex *v2;
    _grRect *scissor;
    int x0, x1, x2, y0, y1, y2;
    int xMin, xMax, yMin, yMax, extent;
    int w0, w1, w2;
    int wMin, wMax;

    if ((state == NULL || tri == NULL) &&
        grAssert("state && tri", "grTessellator.inl", 512) != 0)
        grDie(0);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    v0 = vertexBuffer + grUnpackIndex(tri->indices, 0);
    v1 = vertexBuffer + grUnpackIndex(tri->indices, 1);
    v2 = vertexBuffer + grUnpackIndex(tri->indices, 2);
    x0 = v0->screenPosition[0];
    x1 = v1->screenPosition[0];
    x2 = v2->screenPosition[0];
    y0 = v0->screenPosition[1];
    y1 = v1->screenPosition[1];
    y2 = v2->screenPosition[1];
    xMin = hgMin32(hgMin32(x0, x1), x2);
    xMax = hgMax32(hgMax32(x0, x1), x2);
    yMin = hgMin32(hgMin32(y0, y1), y2);
    yMax = hgMax32(hgMax32(y0, y1), y2);
    extent = hgMax32(xMax - xMin, yMax - yMin);
    if (extent < 0x4000)
    {
        grDrawClippedTriangle(state, tri, origIndices, 8);
        return;
    }
    scissor = &state->renderScissor;
    if (grSubpixelTrunc(xMax) <= scissor->x0 ||
        grSubpixelTrunc(xMin) >= scissor->x1 ||
        grSubpixelTrunc(yMax) <= scissor->y0 ||
        grSubpixelTrunc(yMin) >= scissor->y1)
        return;
    {
        unsigned int e0 = (unsigned int)v0->clipPosition.e;
        unsigned int e1 = (unsigned int)v1->clipPosition.e;
        unsigned int e2 = (unsigned int)v2->clipPosition.e;

        if (__builtin_expect(((e0 ^ e1) | (e1 ^ e2)) == 0, 1))
        {
            w0 = v0->clipPosition.m[3] >> 1;
            w1 = v1->clipPosition.m[3] >> 1;
            w2 = v2->clipPosition.m[3] >> 1;
        }
        else
        {
            int maxe = hgMax32(hgMax32((int)e0, (int)e1), (int)e2) + 1;

            w0 = v0->clipPosition.m[3] >> hgMinu32((unsigned int)(maxe - (int)e0), 0x1Fu);
            w1 = v1->clipPosition.m[3] >> hgMinu32((unsigned int)(maxe - (int)e1), 0x1Fu);
            w2 = v2->clipPosition.m[3] >> hgMinu32((unsigned int)(maxe - (int)e2), 0x1Fu);
        }
    }
    wMin = hgMin32(hgMin32(w0, w1), w2);
    wMax = hgMax32(hgMax32(w0, w1), w2);
    if (wMin == wMax)
    {
        if (wMin > 0)
        {
            grDrawClippedTriangle(state, tri, origIndices, 8);
            return;
        }
    }
    else
    {
        int wBoost = wMin + (wMin >> 4);

        if (wMax <= wBoost)
        {
            if (extent < 0x40000)
            {
                grDrawClippedTriangle(state, tri, origIndices, 8);
                return;
            }
            wMax = wBoost;
        }
        {
            HGint64 smallSq = hgLsl64h(hgMul64(4096 - extent, 4096 - extent), 10);
            HGint64 bigSq = hgLsl64h(hgMul64(extent + 4096, extent + 4096), 10);

            if (hgLessThan64(hgMul64(wMax, smallSq), hgMul64(wMin, bigSq)) != 0)
            {
                grDrawClippedTriangle(state, tri, origIndices, 8);
                return;
            }
        }
    }
    v0->normal[2] = w0;
    v1->normal[2] = w1;
    v2->normal[2] = w2;
    if ((unsigned int)origIndices == tri->indices)
    {
        v0->normal[0] = IUNITSCALAR_ONE;
        v0->normal[1] = 0;
        v1->normal[0] = 0;
        v1->normal[1] = IUNITSCALAR_ONE;
        v2->normal[0] = 0;
        v2->normal[1] = 0;
    }
    else
    {
        int i0 = grUnpackIndex(origIndices, 0);
        int i1 = grUnpackIndex(origIndices, 1);
        int i2 = grUnpackIndex(origIndices, 2);

        if (grUnpackIndex(origIndices, 0) == grUnpackIndex(origIndices, 1) &&
            grAssert("grUnpackIndex(origIndices, 0) != grUnpackIndex(origIndices, 1)",
                     "grTessellator.inl", 547) != 0)
            grDie(0);
        if (vertexBuffer[i0].normal[0] != IUNITSCALAR_ONE &&
            grAssert("vertexCache[grUnpackIndex(origIndices, 0)].nc.clip.factor.a == IUNITSCALAR_ONE",
                     "grTessellator.inl", 549) != 0)
            grDie(0);
        if (vertexBuffer[i0].normal[1] != 0 &&
            grAssert("vertexCache[grUnpackIndex(origIndices, 0)].nc.clip.factor.b == 0",
                     "grTessellator.inl", 550) != 0)
            grDie(0);
        if (vertexBuffer[i1].normal[0] != 0 &&
            grAssert("vertexCache[grUnpackIndex(origIndices, 1)].nc.clip.factor.a == 0",
                     "grTessellator.inl", 551) != 0)
            grDie(0);
        if (vertexBuffer[i1].normal[1] != IUNITSCALAR_ONE &&
            grAssert("vertexCache[grUnpackIndex(origIndices, 1)].nc.clip.factor.b == IUNITSCALAR_ONE",
                     "grTessellator.inl", 552) != 0)
            grDie(0);
        if (grUnpackIndex(origIndices, 1) != grUnpackIndex(origIndices, 2))
        {
            if (vertexBuffer[i2].normal[0] != 0 &&
                grAssert("vertexCache[grUnpackIndex(origIndices, 2)].nc.clip.factor.a == 0",
                         "grTessellator.inl", 555) != 0)
                grDie(0);
            if (vertexBuffer[i2].normal[1] != 0 &&
                grAssert("vertexCache[grUnpackIndex(origIndices, 2)].nc.clip.factor.b == 0",
                         "grTessellator.inl", 556) != 0)
                grDie(0);
        }
    }
    grTessellateTriangleSubdivide(state, tri->indices, origIndices,
                                  (unsigned int)tri->side & 0xFFu);
}
