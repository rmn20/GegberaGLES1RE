/* grClipper.c - primitive clipping (port of the decompiled grClipper.inl
 * 30654-31488, plus the grUtil.h clip-factor helpers and the guardband
 * distance helper from grRasterizer.inl).
 *
 * The clipper runs on the renderStack vertex buffer (records at
 * renderStack+1332, 128-byte _grVertex records, << 7 addressing) against
 * the 6 guardband planes (clip mask bits 0..5) and the 6 user clip planes
 * (bits 10..15 of the vertex clipMask, GR_CLIPMASK_CLIPPING_PLANES).
 * Polygon vertices carry barycentric clip factors (cv_anon_17F5: {a, b,
 * _grVertex *originalVertex, unsigned char edgeFlag}); the vertex records
 * store the two factors at +92/+96 - an intentional alias of normal[0..1]
 * (lighting has already consumed the normal when the clipper runs).
 *
 * grClipPrimitive (31235-31488) dispatches the clipped primitives through
 * grrDrawClippedTriangle/grrDrawClippedLine.
 *
 * Assert sites keep the original file/line strings.
 */
#include "gerbera/grState.h"
#include "gerbera/grDefs.h"
#include "imath/imApi.h"
#include "hg/hgInt32.h"

#define GR_CLIP_CHECK(cond, file, line)          \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, (file), (line)) != 0) \
            grDie(0);                            \
    } while (0)

/* the vertex buffer lives at renderStack+1332 (see grVertexPipeline.c) */
#define GR_CLIP_VERTEX_BUFFER(state) \
    ((_grVertex *)((unsigned char *)(state)->renderStack + 1332))

/* --- grIsInsideClipPlane / grIsOutsideClipPlane / grIsOnPlane (30671-30687) - */

int grIsInsideClipPlane(int distance)
{
    return IScalar_testge(distance);
}

int grIsOutsideClipPlane(int distance)
{
    return IScalar_testle(distance);
}

int grIsOnPlane(int distance)
{
    return IScalar_testeq(distance);
}

/* --- grSubpixelTrunc (grUtil.h; DLL 004214A0) ------------------------------ */

int grSubpixelTrunc(int i)
{
    return i >> 12;
}

/* --- grGetFrontFace (grUtil.h; DLL 00421690) ------------------------------- */

int grGetFrontFace(Gerbera_s *state)
{
    return state->frontFace;
}

/* --- grClipFactorSet / grClipFactorInit / grLerpClipFactor ------------------
 * (grUtil.h 298-332; DLL 00421BA0/00421CA0/00421C00) ------------------------- */

void grClipFactorSet(_grClipVertex *v, int ndx, int val)
{
    GR_CLIP_CHECK(hgInRange32(ndx, 0, 1), "grUtil.h", 318);
    GR_CLIP_CHECK(val >= 0 && val <= IUNITSCALAR_ONE, "grUtil.h", 319);
    v->clipFactor[ndx] = val;
}

void grClipFactorInit(_grClipVertex *v, int ndx)
{
    GR_CLIP_CHECK(hgInRange32(ndx, 0, 2), "grUtil.h", 332);
    v->clipFactor[0] = 0;
    v->clipFactor[1] = 0;
    if (ndx != 2)
        grClipFactorSet(v, ndx, IUNITSCALAR_ONE);
}

void grLerpClipFactor(_grClipVertex *dst, _grClipVertex *s0, _grClipVertex *s1, int factor)
{
    GR_CLIP_CHECK(hgInRange32(factor, 0, IUNITSCALAR_ONE), "grUtil.h", 298);
    GR_CLIP_CHECK(s0->clipFactor[1] + s0->clipFactor[0] <= IUNITSCALAR_ONE, "grUtil.h", 299);
    GR_CLIP_CHECK(s1->clipFactor[1] + s1->clipFactor[0] <= IUNITSCALAR_ONE, "grUtil.h", 300);
    dst->clipFactor[0] = IUnitScalar_lerp(s0->clipFactor[0], s1->clipFactor[0], factor);
    dst->clipFactor[1] = IUnitScalar_lerp(s0->clipFactor[1], s1->clipFactor[1], factor);
    GR_CLIP_CHECK(dst->clipFactor[1] + dst->clipFactor[0] <= IUNITSCALAR_ONE, "grUtil.h", 305);
}

/* --- grVertexCacheGetTemp (grClipper.inl 33; DLL 0042EE60) ------------------
 * Temp vertices live at vertexBuffer[92 + index] (renderStack[1638+16*i]
 * .whichPool); the caller fills them in from the clip factors. */

int grVertexCacheGetTemp(Gerbera_s *state, int index)
{
    _grVertex *vertex;

    GR_CLIP_CHECK(hgInRange32(index, 0, GR_MAX_CLIPPER_GENERATED_VERTICES - 1), "grClipper.inl", 40);
    vertex = &GR_CLIP_VERTEX_BUFFER(state)[GR_NUM_RASTERIZER_CACHE_VERTICES + GR_VERTEX_CACHE_SIZE + index];
    vertex->index = -1;
    vertex->stateMask = 0;
    return GR_VERTEX_CACHE_SIZE + GR_NUM_RASTERIZER_CACHE_VERTICES + index;
}

/* --- grClipInterpolate (grClipper.inl 114; DLL 0042EF00) ------------------- */

int grClipInterpolate(int d0, int d1)
{
    int t;

    GR_CLIP_CHECK((IScalar_testgt(d0) && IScalar_testlt(d1)) ||
                  (IScalar_testlt(d0) && IScalar_testgt(d1)), "grClipper.inl", 121);
    if (IScalar_testge(d0))
    {
        d0 = IScalar_negate(d0);
        d1 = IScalar_negate(d1);
    }
    t = IScalar_toUnitScalar(IScalar_mul(IScalar_rcp(IScalar_sub(d0, d1)), d0));
    GR_CLIP_CHECK(t >= 0 && t <= IUNITSCALAR_ONE, "grClipper.inl", 129);
    return t;
}

/* --- grComputeDistanceToGuardbandPlane (grRasterizer.inl; DLL 0041F5C0) ----
 * plane 0..5: axis pairs (z, x, y), even planes c+w / odd planes w-c;
 * shift 2 except the z planes. The e==254 clamp keeps the normalized
 * distance finite at the far plane. */

int grComputeDistanceToGuardbandPlane(int plane, _IVector4 *position, int shift)
{
    int axis, dist, re, rm, r;

    axis = (plane >> 1) + 2;
    if (axis >= 3)
        axis = (plane >> 1) - 1;
    if ((plane & 6) == 0)
        shift = 0;
    if ((plane & 1) != 0)
        dist = position->m[3] - (position->m[axis] >> shift);
    else
        dist = (position->m[axis] >> shift) + position->m[3];
    re = position->e;
    rm = dist;
    if (position->e == 254 && (hgAbs32(dist) & 0x40000000) != 0)
    {
        re = 254;
        rm = hgCopySign32(0x3FFFFFFF, rm);
    }
    r = IScalar_setAndNormalize(re, rm);
    if (IScalar_testeq(r) != 0 && dist != 0)
        return IScalar_set(1, hgCopySign32(0x20000000, dist));
    return r;
}

/* --- grComputeDistanceToUserClipPlane (grClipper.inl 157; DLL 0042EF50) ---- */

int grComputeDistanceToUserClipPlane(Gerbera_s *state, int plane, _IVector4 *position, int planeSign)
{
    _IFVector4 eq;
    int planea, d0, d1, d2, d3, e0, e1, e2, e3, dot;

    planea = plane - GR_CLIP_USER0;
    GR_CLIP_CHECK(hgInRange32(planea, 0, GR_MAX_USER_CLIP_PLANES - 1), "grClipper.inl", 164);
    if (planeSign >= 0)
        IFVector4_copy(&eq, &state->userClipPlanes[planea]);
    else
        IFVector4_negate(&eq, &state->userClipPlanes[planea]);
    GR_CLIP_CHECK(IFVector4_getFirstNonZeroSign(&eq) >= 0, "grClipper.inl", 183);
    d0 = IVector4_getf(position, 0);
    d1 = IVector4_getf(position, 1);
    d2 = IVector4_getf(position, 2);
    d3 = IVector4_getf(position, 3);
    e0 = IFVector4_getf(&eq, 0);
    e1 = IFVector4_getf(&eq, 1);
    e2 = IFVector4_getf(&eq, 2);
    e3 = IFVector4_getf(&eq, 3);
    dot = IScalar_add(IScalar_mul(e0, d0), IScalar_mul(e1, d1));
    dot = IScalar_add(dot, IScalar_mul(e2, d2));
    dot = IScalar_add(dot, IScalar_mul(e3, d3));
    if (planeSign < 0)
        dot = IScalar_negate(dot);
    return dot;
}

/* --- grClipConvexPolygon (grClipper.inl 231; DLL 0042F010) ------------------
 * Clips the polygon (inPoly, numVertices) against one plane given the
 * per-vertex distances; appends intersection vertices with lerped clip
 * factors, then removes reflex vertices (the barycentric a/b pairs of a
 * convex polygon wind consistently - imDet2_64 < 0 marks a reflex one). */

static void clipCopyVertex(_grClipVertex *dst, _grClipVertex *src)
{
    dst->clipFactor[0] = src->clipFactor[0];
    dst->clipFactor[1] = src->clipFactor[1];
    dst->originalVertex = src->originalVertex;
    dst->edgeFlag = src->edgeFlag;
}

int grClipConvexPolygon(_grClipVertex *clipped, int numVertices, _grClipVertex *inPoly,
                        int *distances, int swapDirection)
{
    int out, i, src0, src1, edgeFlag, t, j, k, removed, det;
    int dx0, dy0, dx1, dy1;
    _grClipVertex *cur, *nxt;

    out = 0;
    for (i = 0; i < numVertices; ++i)
    {
        src0 = i;
        src1 = i + 1 == numVertices ? 0 : i + 1;
        if ((grIsInsideClipPlane(distances[src0]) != 0 && grIsInsideClipPlane(distances[src1]) != 0) ||
            (grIsOnPlane(distances[src0]) != 0 && grIsOutsideClipPlane(distances[src1]) != 0))
        {
            clipCopyVertex(&clipped[out++], &inPoly[src0]);
        }
        else if (grIsInsideClipPlane(distances[src0]) != 0 || grIsOutsideClipPlane(distances[src1]) == 0)
        {
            edgeFlag = inPoly[src0].edgeFlag;
            GR_CLIP_CHECK((grIsInsideClipPlane(distances[src0]) != 0 && grIsOutsideClipPlane(distances[src1]) != 0) ||
                          (grIsOutsideClipPlane(distances[src0]) != 0 && grIsInsideClipPlane(distances[src1]) != 0),
                          "grClipper.inl", 259);
            if (grIsInsideClipPlane(distances[src0]) != 0)
            {
                clipCopyVertex(&clipped[out++], &inPoly[src0]);
                j = src0;
                src0 = src1;
                src1 = j;
                edgeFlag = 0;
            }
            GR_CLIP_CHECK(grIsOutsideClipPlane(distances[src0]) != 0 &&
                          grIsInsideClipPlane(distances[src1]) != 0, "grClipper.inl", 269);
            if (swapDirection != 0)
            {
                j = src0;
                src0 = src1;
                src1 = j;
            }
            cur = &clipped[out++];
            cur->clipFactor[0] = 0;
            cur->originalVertex = NULL;
            cur->edgeFlag = (unsigned char)edgeFlag;
            t = grClipInterpolate(distances[src0], distances[src1]);
            grLerpClipFactor(cur, &inPoly[src0], &inPoly[src1], t);
        }
    }
    do
    {
        cur = &clipped[out - 1];
        removed = 0;
        for (i = 0; i < out; ++i)
        {
            nxt = &clipped[i + 1 == out ? 0 : i + 1];
            dx0 = clipped[i].clipFactor[0] - cur->clipFactor[0];
            dy0 = clipped[i].clipFactor[1] - cur->clipFactor[1];
            dx1 = nxt->clipFactor[0] - clipped[i].clipFactor[0];
            dy1 = nxt->clipFactor[1] - clipped[i].clipFactor[1];
            det = (int)hgLessThanZero64(imDet2_64(dx0, dy0, dx1, dy1));
            if (det != 0)
            {
                for (k = i + 1; k < out; ++k)
                    clipCopyVertex(&clipped[k - 1], &clipped[k]);
                --out;
                removed = 1;
                break;
            }
            cur = &clipped[i];
        }
    } while (removed != 0);
    return out;
}

/* --- grClipper (grClipper.inl 341; DLL 0042F150) ----------------------------
 * Clips the triangle `indices` (grSetIndices packed bytes) against every
 * plane flagged in the vertices' clipMask; writes the output fan cache
 * indices into outputFan and returns the vertex count (0 = fully clipped). */

int grClipper(Gerbera_s *state, unsigned char *outputFan, unsigned int indices)
{
    _grVertex *vertexBuffer = GR_CLIP_VERTEX_BUFFER(state);
    _grClipVertex polys[2][GR_MAX_CLIPPER_VERTICES];
    _grClipVertex *inPoly = polys[0];
    _grClipVertex *clipped = polys[1];
    _grClipVertex *swap;
    unsigned short clipMask = 0;
    int numVertices = 3;
    int i, plane, sign, dist[3], tempCount, dot[GR_MAX_CLIPPER_VERTICES];
    int allInside, allOutside, foundZeros, numSignChanges, ok;

    for (i = 2; i >= 0; --i)
    {
        int idx = grUnpackIndex(indices, i);
        _grVertex *v = &vertexBuffer[idx];

        if (IVector4_testeq(&v->clipPosition) != 0)
            return 0;
        inPoly[i].originalVertex = v;
        inPoly[i].edgeFlag = (unsigned char)idx;
        grClipFactorInit(&inPoly[i], i);
        v->normal[0] = inPoly[i].clipFactor[0];   /* +92: clipFactor.a */
        v->normal[1] = inPoly[i].clipFactor[1];   /* +96: clipFactor.b */
        clipMask |= v->clipMask;
    }
    clipMask &= GR_CLIPMASK_CLIPPING_PLANES;
    GR_CLIP_CHECK((clipMask & GR_CLIPMASK_CLIPPING_PLANES) != 0, "grClipper.inl", 396);

    /* guardband planes 0..5 */
    for (plane = 0; plane < 6; ++plane)
    {
        allInside = 1;
        allOutside = 1;
        if (((1 << plane) & clipMask) != 0)
        {
            for (i = 2; i >= 0; --i)
                dist[i] = grComputeDistanceToGuardbandPlane(
                    plane,
                    &vertexBuffer[grUnpackIndex(indices, i)].clipPosition,
                    2);
            for (i = numVertices - 1; i >= 0; --i)
            {
                int w[3];

                w[0] = inPoly[i].clipFactor[0];
                w[1] = inPoly[i].clipFactor[1];
                w[2] = IUNITSCALAR_ONE - (inPoly[i].clipFactor[1] + inPoly[i].clipFactor[0]);
                dot[i] = IVector3_clipperDot(w, dist);
                allInside &= grIsInsideClipPlane(dot[i]);
                allOutside &= grIsOutsideClipPlane(dot[i]);
            }
            if (allOutside != 0)
                return 0;
            if (allInside == 0)
            {
                foundZeros = 0;
                numSignChanges = 0;
                for (i = 0; i < numVertices; ++i)
                {
                    int next = i + 1 == numVertices ? 0 : i + 1;
                    int in0 = grIsInsideClipPlane(dot[i]);
                    int in1 = grIsInsideClipPlane(dot[next]);
                    int zero = IScalar_testeq(dot[next]) |
                               IScalar_testeq(dot[i]);

                    foundZeros |= zero;
                    if (in0 != in1 && zero == 0)
                        ++numSignChanges;
                }
                ok = 1;
                if (numSignChanges != 2 &&
                    (foundZeros == 0 || numSignChanges >= 2) &&
                    grAssert("numSignChanges==2 || (foundZeros && numSignChanges < 2)",
                             "grClipper.inl", 489) != 0)
                    ok = 0;
                if (ok == 0)
                    grDie(0);
                numVertices = grClipConvexPolygon(clipped, numVertices, inPoly, dot, 0);
                swap = inPoly;
                inPoly = clipped;
                clipped = swap;
            }
        }
    }

    /* user clip planes 10..15 */
    for (plane = GR_CLIP_USER0; plane < GR_CLIP_USER0 + GR_MAX_USER_CLIP_PLANES; ++plane)
    {
        allInside = 1;
        allOutside = 1;
        sign = IFVector4_getFirstNonZeroSign(&state->userClipPlanes[plane - GR_CLIP_USER0]);
        if (((1 << plane) & clipMask) != 0)
        {
            for (i = 2; i >= 0; --i)
                dist[i] = grComputeDistanceToUserClipPlane(
                    state,
                    plane,
                    &vertexBuffer[grUnpackIndex(indices, i)].clipPosition,
                    sign);
            for (i = numVertices - 1; i >= 0; --i)
            {
                int w[3];

                w[0] = inPoly[i].clipFactor[0];
                w[1] = inPoly[i].clipFactor[1];
                w[2] = IUNITSCALAR_ONE - (inPoly[i].clipFactor[1] + inPoly[i].clipFactor[0]);
                dot[i] = IVector3_clipperDot(w, dist);
                allInside &= grIsInsideClipPlane(dot[i]);
                allOutside &= grIsOutsideClipPlane(dot[i]);
            }
            if (allOutside != 0)
                return 0;
            if (allInside == 0)
            {
                foundZeros = 0;
                numSignChanges = 0;
                for (i = 0; i < numVertices; ++i)
                {
                    int next = (i + 1) % numVertices;
                    int in0 = grIsInsideClipPlane(dot[i]);
                    int in1 = grIsInsideClipPlane(dot[next]);
                    int zero = IScalar_testeq(dot[next]) |
                               IScalar_testeq(dot[i]);

                    foundZeros |= zero;
                    if (in0 != in1 && zero == 0)
                        ++numSignChanges;
                }
                ok = 1;
                if (numSignChanges != 2 &&
                    (foundZeros == 0 || numSignChanges >= 2) &&
                    grAssert("numSignChanges==2 || (foundZeros && numSignChanges < 2)",
                             "grClipper.inl", 584) != 0)
                    ok = 0;
                if (ok == 0)
                    grDie(0);
                numVertices = grClipConvexPolygon(clipped, numVertices, inPoly, dot, sign >= 0);
                swap = inPoly;
                inPoly = clipped;
                clipped = swap;
            }
        }
    }

    /* emit the fan: originals keep their cache index, intersections get a
     * temp vertex with the clip-space position rebuilt from the factors */
    tempCount = 0;
    for (i = 0; i < numVertices; ++i)
    {
        if (inPoly[i].originalVertex != NULL)
        {
            outputFan[i] = inPoly[i].edgeFlag;
            GR_CLIP_CHECK((inPoly[i].originalVertex->clipMask & GR_CLIPMASK_CLIPPING_PLANES) == 0,
                          "grClipper.inl", 645);
        }
        else
        {
            int temp = grVertexCacheGetTemp(state, tempCount++);
            _grVertex *tv = &vertexBuffer[temp];
            _IVector4 sum, term;
            int w0, w1, w2;

            outputFan[i] = (unsigned char)temp;
            tv->stateMask = GR_VERTEXMASK_SURVIVED_2D_REJECTION |
                            GR_VERTEXMASK_TRANSFORMED |
                            GR_VERTEXMASK_INTERPOLATE_GRADIENTS;   /* 69 */
            tv->index = -1;
            tv->clipMask = 0;
            tv->normal[0] = inPoly[i].clipFactor[0];   /* +92 */
            tv->normal[1] = inPoly[i].clipFactor[1];   /* +96 */
            w0 = IUnitScalar_tos(inPoly[i].clipFactor[0] & 0xFFFFFFC0);
            w1 = IUnitScalar_tos(inPoly[i].clipFactor[1] & 0xFFFFFFC0);
            w2 = IUnitScalar_tos(IUNITSCALAR_ONE -
                                 (inPoly[i].clipFactor[0] & 0xFFFFFFC0) -
                                 (inPoly[i].clipFactor[1] & 0xFFFFFFC0));
            IVector4_smul(&sum, &vertexBuffer[grUnpackIndex(indices, 0)].clipPosition, w0);
            IVector4_smul(&term, &vertexBuffer[grUnpackIndex(indices, 1)].clipPosition, w1);
            IVector4_add(&sum, &sum, &term);
            IVector4_smul(&term, &vertexBuffer[grUnpackIndex(indices, 2)].clipPosition, w2);
            IVector4_add(&sum, &sum, &term);
            IVector4_fixClipSpacePosition(&tv->clipPosition, &sum, 2);
        }
    }
    return numVertices;
}

/* --- grClipPrimitive (grClipper.inl 559; DLL 31235-31488) -------------------
 * Draws one queued primitive after clipping: runs grClipper on the packed
 * cache indices, rechecks the scissor over the produced fan, then hands the
 * fan triangles (or the degenerate line record) to the rasterizer. */

static void clipPrimitive(Gerbera_s *state, unsigned int origIndices, int numVertices)
{
    unsigned char fan[GR_MAX_CLIPPER_VERTICES];
    _grVertex *vertexBuffer = GR_CLIP_VERTEX_BUFFER(state);
    _grRect *scissor = &state->renderScissor;
    _grClipTriangle tri;
    int n, i, j, k, combinedMask, triSide, numTriangles;

    n = grClipper(state, fan, origIndices);
    if (n == 0)
        return;
    if (numVertices == 2)
        n = 2;
    combinedMask = (int)GR_SCISSORMASK_FULL;
    for (i = 0; i < n; ++i)
    {
        _grVertex *v = &vertexBuffer[fan[i]];
        int sx, sy, mask;

        if ((v->stateMask & GR_VERTEXMASK_VIEWPORT_TRANSFORMED) == 0)
            grViewportTransformVertex(state, v);
        sx = grSubpixelTrunc(v->screenPosition[0]);
        sy = grSubpixelTrunc(v->screenPosition[1]);
        mask = hgSignBit32(sx - scissor->x0) |
               2 * hgSignBit32(scissor->x1 - sx) |
               4 * hgSignBit32(sy - scissor->y0) |
               8 * hgSignBit32(scissor->y1 - sy);
        combinedMask &= mask;
        if ((mask & ~GR_SCISSORMASK_FULL) != 0 &&
            grAssert("!(vertexMask & ~GR_SCISSORMASK_FULL)", "grClipper.inl", 730) != 0)
            grDie(0);
        if (sx >= scissor->x0)
        {
            if ((mask & GR_SCISSORMASK_LEFT) != 0 &&
                grAssert("!(vertexMask & GR_SCISSORMASK_LEFT)", "grClipper.inl", 732) != 0)
                grDie(0);
        }
        else
        {
            if ((mask & GR_SCISSORMASK_LEFT) == 0 &&
                grAssert("vertexMask & GR_SCISSORMASK_LEFT", "grClipper.inl", 731) != 0)
                grDie(0);
        }
        if (sx <= scissor->x1)
        {
            if ((mask & GR_SCISSORMASK_RIGHT) != 0 &&
                grAssert("!(vertexMask & GR_SCISSORMASK_RIGHT)", "grClipper.inl", 734) != 0)
                grDie(0);
        }
        else
        {
            if ((mask & GR_SCISSORMASK_RIGHT) == 0 &&
                grAssert("vertexMask & GR_SCISSORMASK_RIGHT", "grClipper.inl", 733) != 0)
                grDie(0);
        }
        if (sy >= scissor->y0)
        {
            if ((mask & GR_SCISSORMASK_TOP) != 0 &&
                grAssert("!(vertexMask & GR_SCISSORMASK_TOP)", "grClipper.inl", 736) != 0)
                grDie(0);
        }
        else
        {
            if ((mask & GR_SCISSORMASK_TOP) == 0 &&
                grAssert("vertexMask & GR_SCISSORMASK_TOP", "grClipper.inl", 735) != 0)
                grDie(0);
        }
        if (sy <= scissor->y1)
        {
            if ((mask & GR_SCISSORMASK_BOTTOM) != 0 &&
                grAssert("!(vertexMask & GR_SCISSORMASK_BOTTOM)", "grClipper.inl", 738) != 0)
                grDie(0);
        }
        else
        {
            if ((mask & GR_SCISSORMASK_BOTTOM) == 0 &&
                grAssert("vertexMask & GR_SCISSORMASK_BOTTOM", "grClipper.inl", 737) != 0)
                grDie(0);
        }
    }
    if (combinedMask != 0)
        return;
    numTriangles = n - 2;
    if (numVertices == 3)
    {
        HGint64 area64 = hgZero64();
        HGint64 area;

        k = 0;
        for (j = 0; j < numTriangles; ++j)
            area64 = hgAdd64(area64,
                             grComputeTriangleArea(vertexBuffer,
                                                   grSetIndices(fan[0], fan[j + 1], fan[j + 2])));
        triSide = grGetFrontFace(state) ^ ((unsigned int)hgGet64h(area64) >> 31);
        for (j = 0; j < numTriangles; ++j)
        {
            unsigned int indices = grSetIndices(fan[0], fan[j + 1], fan[j + 2]);
            int sign, ok;

            area = grComputeTriangleArea(vertexBuffer, indices);
            sign = (((unsigned int)hgGet64h(area) >> 31) ^ state->cullNegative) &
                   grTestCapability(state, GR_CAPABILITY_CULL_FACE);
            if (sign != 1 && hgIsZero64(area) == 0)
            {
                for (; k < j + 3; ++k)
                {
                    _grVertex *v = &vertexBuffer[fan[k]];

                    if ((v->stateMask & GR_VERTEXMASK_INTERPOLATE_GRADIENTS) != 0)
                        grInterpolateVertexGradients(state, v, origIndices, triSide);
                }
                ok = 1;
                if (triSide != GR_SIDE_FRONT && triSide != GR_SIDE_BACK &&
                    grAssert("triSide==GR_SIDE_FRONT || triSide==GR_SIDE_BACK",
                             "grClipper.inl", 813) != 0)
                    ok = 0;
                if (ok == 0)
                    grDie(0);
                tri.indices = indices;
                tri.area = grRoundTriangleArea(area);
                tri.side = triSide;
                grrDrawClippedTriangle(state, &tri, origIndices);
            }
        }
    }
    else
    {
        tri.indices = grSetIndices(fan[0], fan[1], fan[1]);
        tri.area = 0;
        tri.side = 0;
        grrDrawClippedLine(state, &tri, origIndices);
    }
}

void grClipPrimitive(Gerbera_s *state, unsigned int origIndices, int numVertices)
{
    _grVertex *buffer = GR_CLIP_VERTEX_BUFFER(state);
    _grVertex *vertices[3];
    _grVertex saved[3];
    unsigned int approximate = 0;
    int count = 0, i, j;

    for (i = 0; i < numVertices; ++i)
    {
        _grVertex *vertex = &buffer[grUnpackIndex(origIndices, i)];
        approximate |= vertex->stateMask & GR_VERTEXMASK_APPROXIMATE_POSITION;
        for (j = 0; j < count && vertices[j] != vertex; ++j) {}
        if (j == count) vertices[count++] = vertex;
    }
    if (!approximate)
    {
        clipPrimitive(state, origIndices, numVertices);
        return;
    }

    /* Near-parallel plane intersections can amplify endpoint errors. Restore
     * the cache after the precise draw: other queued areas use its positions. */
    for (i = 0; i < count; ++i)
    {
        _grVertex *vertex = vertices[i];
        saved[i] = *vertex;
        if (vertex->stateMask & GR_VERTEXMASK_APPROXIMATE_POSITION)
        {
            const _grVertexArray *array = &state->vertexArray[0];
            int *source = (int *)((unsigned char *)array->data + array->stride * vertex->index);
            _IFVector4 position;
            IFVector4_setfv(&position, source, array->size);
            IVertexTransform_mulfv4(&vertex->clipPosition, &state->objectToClipTransform, &position);
            vertex->stateMask &= ~(GR_VERTEXMASK_APPROXIMATE_POSITION | GR_VERTEXMASK_VIEWPORT_TRANSFORMED);
            grComputeVertexClipCodes(state, vertex);
        }
    }
    clipPrimitive(state, origIndices, numVertices);
    for (i = 0; i < count; ++i)
        *vertices[i] = saved[i];
}
