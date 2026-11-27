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

/* DLL data tables (data.fragments.c 68-78). In the binary the three tables
 * are three overlapping views of one triple array: splitIndexTable@S,
 * byte_4751D0@S+1, byte_4751D1@S+2, each read at stride 3, so entry i of
 * the {A, B, C} triple below is exactly (splitIndexTable[3i],
 * byte_4751D0[3i], byte_4751D1[3i]). Byte bits: 0..1 = vertex slot,
 * bit 2 (4) = split vertex relative to the level base (68 + 3*level),
 * otherwise an original triangle vertex slot. */
static const unsigned char g_tessSplitTriples[13] =
{
    0x00, 0x04, 0x06,   /* child 0: (v0, m01, m20) */
    0x04, 0x05, 0x06,   /* child 1: (m01, m12, m20) */
    0x04, 0x01, 0x05,   /* child 2: (m01, v1, m12) */
    0x05, 0x02, 0x06,   /* child 3: (m12, v2, m20) */
    0x00
};
static const signed char g_tessNextLut[3] = { 1, 2, 0 };

/* --- grFracDiv (grTessellator.inl 49; DLL 26703-26725) -----------------------
 * a/b as an IUNIT fraction ([0, IUNITSCALAR_ONE]; a <= b asserted). */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
int grFracDiv(int a, int b)
{
    HGint64 reciprocal;
    HGuint64 u;
    HGint64 s;

    if (b <= 0 && grAssert("b > 0", "grTessellator.inl", 49) != 0)
        grDie(0);
    if (a > b && grAssert("a <= b", "grTessellator.inl", 50) != 0)
        grDie(0);
    reciprocal = imRcpu_inline(b);
    u = hgMulu64((unsigned int)a, (unsigned int)(reciprocal >> 32));
    s = hgToSigned64(u);
    return hgLsl64h(s, (int)reciprocal) / 2;
}

/* --- grLerp (grTessellator.inl 70; DLL 26728-26742) --------------------------
 * ((IUNITSCALAR_ONE - factor) * a + factor * b) >> 29. */

int grLerp(int a, int b, int factor)
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

int grSplitEdge(_grVertex *dst, _grVertex *v0, _grVertex *v1, int forceSplit)
{
    int willSplit = 1;
    int extent = hgMax32(hgDiff32(v1->screenPosition[0], v0->screenPosition[0]),
                         hgDiff32(v1->screenPosition[1], v0->screenPosition[1]));
    int w0, w1, t17, fdw2, factor1, t;
    _grVertex *swap;
    HGint64 sq, r;

    if (extent < 0x4000)
    {
        if (forceSplit == 0)
            return 0;
        willSplit = 0;
    }
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
        w0 = hgMax32(w0, w1 + (w1 >> 4));
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
    t17 = 0;
    if (w0 + w1 != 0)
    {
        int fdw = grFracDiv(w0 - w1, w0 + w1);
        int dist;

        r = hgMul64(fdw, extent);
        dist = (int)hgLsl64h(r, 3);
        dist = hgMin32(dist, 0x2000);
        t17 = hgGet64l(hgMul64(dist, 0x10000));
    }
    {
        int w0Orig = v0->normal[2];
        fdw2 = (w1 + w0Orig != 0) ? grFracDiv(w0Orig, w1 + w0Orig) : 0;
        factor1 = hgMax32(2 * t17 - 0x20000000, 0);
        t = (factor1 != 0)
          ? (0x10000000 + hgLsl64h_0_31(hgMul64(factor1, fdw2 - 0x10000000), 3))
          : 0x10000000;
        dst->normal[2] = grLerp(w0Orig, w1, fdw2);
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

/* --- grTessellateTriangle (grTessellator.inl 283; DLL 26947-27246) ------------
 * One recursion step: classify, split the edges that need it (recording
 * which edges split in splitBits), then either recurse into the children
 * picked by the split-count tables or draw the (now affine) piece. */

static void grTessellateTriangle(Gerbera_s *state, _grClipTriangle *tri,
                                 unsigned int origIndices, int level);

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void grTessellateTriangleSubdivide(Gerbera_s *state, _grClipTriangle *tri,
                                          unsigned int origIndices, int level)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    unsigned int splitMask = 0;
    int numSplits = 0;
    int forceSplit = 0;
    int j;

    for (j = 2; j >= 0; --j)
    {
        int dstIndex = j + 3 * level + 68;
        _grVertex *dst = vertexBuffer + dstIndex;
        _grVertex *va = vertexBuffer + grUnpackIndex(tri->indices, j);
        _grVertex *vb = vertexBuffer +
            grUnpackIndex(tri->indices, g_tessNextLut[j]);
        int split = grSplitEdge(dst, va, vb, forceSplit);
        int bit = forceSplit | split;

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
            grInterpolateVertexGradients(state, dst, origIndices,
                                         tri->side);
        }
    }
    forceSplit = 1;
    if (hgInRange32(numSplits, 0, 3) == 0 &&
        grAssert("hgInRange32(numSplits, 0, 3)", "grTessellator.inl", 350) != 0)
        grDie(0);
    if (numSplits != 0 && level != 7)
    {
        int base = 3 * level + 68;

        switch (numSplits)
        {
            case 3:
            {
                int i;

                for (i = 3; i >= 0; --i)
                {
                    unsigned char aByte = g_tessSplitTriples[3 * i];
                    unsigned char bByte = g_tessSplitTriples[3 * i + 1];
                    unsigned char cByte = g_tessSplitTriples[3 * i + 2];
                    int iA = base + (aByte & 3);
                    int iB = base + (bByte & 3);
                    int iC = base + (cByte & 3);
                    _grClipTriangle child;

                    if ((aByte & 4) == 0)
                        iA = grUnpackIndex(tri->indices, aByte);
                    if ((bByte & 4) == 0)
                        iB = grUnpackIndex(tri->indices, bByte);
                    if ((cByte & 4) == 0)
                        iC = grUnpackIndex(tri->indices, cByte);
                    child.side = tri->side;
                    child.indices = grSetIndices(iA, iB, iC);
                    grTessellateTriangle(state, &child, origIndices, level + 1);
                }
                break;
            }
            case 2:
            {
                int rel = ((splitMask & 1u) != 0)
                        ? (((splitMask & 2u) != 0) ? 2 : 1)
                        : 0;
                int next = (rel + 1 <= 2) ? (rel + 1) : 0;
                int prev = (rel - 1 >= 0) ? (rel - 1) : 2;
                _grClipTriangle child;

                child.side = tri->side;
                child.indices = grSetIndices(grUnpackIndex(tri->indices, rel),
                                             grUnpackIndex(tri->indices, next),
                                             base + next);
                grTessellateTriangle(state, &child, origIndices, level + 1);
                child.indices = grSetIndices(base + next,
                                             grUnpackIndex(tri->indices, prev),
                                             base + prev);
                grTessellateTriangle(state, &child, origIndices, level + 1);
                child.indices = grSetIndices(grUnpackIndex(tri->indices, rel),
                                             base + prev,
                                             base + next);
                grTessellateTriangle(state, &child, origIndices, level + 1);
                break;
            }
            case 1:
            {
                int rel = ((splitMask & 1u) != 0) ? 0
                        : (((splitMask & 2u) != 0) ? 1 : 2);
                int prev = (rel - 1 >= 0) ? (rel - 1) : 2;
                int next = (rel + 1 <= 2) ? (rel + 1) : 0;
                _grClipTriangle child;

                child.side = tri->side;
                child.indices = grSetIndices(grUnpackIndex(tri->indices, rel),
                                             base + rel,
                                             grUnpackIndex(tri->indices, prev));
                grTessellateTriangle(state, &child, origIndices, level + 1);
                child.indices = grSetIndices(base + rel,
                                             grUnpackIndex(tri->indices, next),
                                             grUnpackIndex(tri->indices, prev));
                grTessellateTriangle(state, &child, origIndices, level + 1);
                break;
            }
            default:
                break;
        }
    }
    else
    {
        if (level != 0)
        {
            HGint64 area64 = grComputeTriangleArea(vertexBuffer, tri->indices);

            if (hgIsZero64(area64) != 0)
                return;
            tri->area = grRoundTriangleArea(area64);
        }
        grDrawClippedTriangle(state, tri, origIndices, 8);
    }
}

static void grTessellateTriangle(Gerbera_s *state, _grClipTriangle *tri,
                                 unsigned int origIndices, int level)
{
    _grVertex *vertexBuffer = GR_VERTEX_BUFFER(state);
    int classify = grClassifyTriangle(state, vertexBuffer, tri->indices);

    if (classify == GR_TRIANGLESTATUS_DISCARD)
        return;
    if (classify == GR_TRIANGLESTATUS_AFFINE || level == 7)
    {
        if (level != 0)
        {
            HGint64 area64 = grComputeTriangleArea(vertexBuffer, tri->indices);

            if (hgIsZero64(area64) != 0)
                return;
            tri->area = grRoundTriangleArea(area64);
        }
        grDrawClippedTriangle(state, tri, origIndices, 8);
        return;
    }
    grTessellateTriangleSubdivide(state, tri, origIndices, level);
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
    int w[3];
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
    IVector4_wNorm(w, &v0->clipPosition, &v1->clipPosition, &v2->clipPosition);
    wMin = hgMin32(hgMin32(w[0], w[1]), w[2]);
    wMax = hgMax32(hgMax32(w[0], w[1]), w[2]);
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
        int wMaxBoosted = hgMax32(wMax, wMin + (wMin >> 4));
        HGint64 smallSq = hgLsl64h(hgMul64(4096 - extent, 4096 - extent), 10);
        HGint64 bigSq = hgLsl64h(hgMul64(extent + 4096, extent + 4096), 10);

        if (hgLessThan64(hgMul64(wMaxBoosted, smallSq), hgMul64(wMin, bigSq)) != 0)
        {
            grDrawClippedTriangle(state, tri, origIndices, 8);
            return;
        }
    }
    v0->normal[2] = w[0];
    v1->normal[2] = w[1];
    v2->normal[2] = w[2];
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
    grTessellateTriangleSubdivide(state, tri, origIndices, 0);
}
