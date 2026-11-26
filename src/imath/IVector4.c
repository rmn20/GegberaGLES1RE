/* Four-component vectors with shared or independent exponents. */
#include "imath/imApi.h"
#include "hg/hgInt32.h"
#include "gerbera/grUtil.h"

/* --- basics (imInline.inl assert strings) ---------------------------- */

void IVector4_zero(_IVector4 *pD)
{
    if (pD == NULL)
        imAssert("pD", "imInline.inl", 0x26u);
    pD->e = 0;
    pD->m[0] = 0;
    pD->m[1] = 0;
    pD->m[2] = 0;
    pD->m[3] = 0;
}

void IVector4_set(_IVector4 *pD, HGuint32 e, int x, int y, int z, int w)
{
    if (pD == NULL)
        imAssert("pD", "imInline.inl", 0x69u);
    if (!((w | z | y | x) != 0 || e == IM_EXPONENT_ZERO))
        imAssert("(x|y|z|w) != 0 || e == IM_EXPONENT_ZERO", "imInline.inl", 0x6Au);
    pD->e = e;
    pD->m[0] = x;
    pD->m[1] = y;
    pD->m[2] = z;
    pD->m[3] = w;
    if (e > 0xFEu)
        IVector4_inf(pD);
    if (IVector4_isValid(pD) == 0)
        imAssert("IVector4_isValid(pD)", "imInline.inl", 0x77u);
}

void IVector4_set_noINF(_IVector4 *pD, HGuint32 e, int x, int y, int z, int w)
{
    if (pD == NULL)
        imAssert("pD", "imInline.inl", 0x7Du);
    if (!((w | z | y | x) != 0 || e == IM_EXPONENT_ZERO))
        imAssert("(x|y|z|w) != 0 || e == IM_EXPONENT_ZERO", "imInline.inl", 0x7Eu);
    pD->e = e;
    pD->m[0] = x;
    pD->m[1] = y;
    pD->m[2] = z;
    pD->m[3] = w;
    if (IVector4_isValid(pD) == 0)
        imAssert("IVector4_isValid(pD)", "imInline.inl", 0x85u);
}

void IVector4_inf(_IVector4 *pD)
{
    int i;
    pD->e = 254;
    for (i = 3; i >= 0; --i)
        pD->m[i] = (pD->m[i] >= 0) ? 0x3FFFFFFF : -1073741823;
}

void IVector4_copy(_IVector4 *pD, _IVector4 *pS)
{
    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "imApi.inl", 0x9CBu);
    if (IVector4_isValid(pS) == 0)
        imAssert("IVector4_isValid(pS)", "imApi.inl", 0x9CCu);
    *pD = *pS;
}

int IVector4_testeq(_IVector4 *v)
{
    return v->e == 0;
}

/* --- IFVector4 (independent per-component exponents) ------------------- */

void IFVector4_set(_IFVector4 *pD, int x, int y, int z, int w)
{
    if (IScalar_isValid(x) == 0)
        imAssert("IScalar_isValid(x)", "imApi.inl", 710);
    if (IScalar_isValid(y) == 0)
        imAssert("IScalar_isValid(y)", "imApi.inl", 711);
    if (IScalar_isValid(z) == 0)
        imAssert("IScalar_isValid(z)", "imApi.inl", 712);
    if (IScalar_isValid(w) == 0)
        imAssert("IScalar_isValid(w)", "imApi.inl", 713);
    pD->e = (IScalar_getExponent(w) << 24) | (IScalar_getExponent(z) << 16) |
            (IScalar_getExponent(y) << 8) | IScalar_getExponent(x);
    pD->m[0] = IScalar_getMantissa(x);
    pD->m[1] = IScalar_getMantissa(y);
    pD->m[2] = IScalar_getMantissa(z);
    pD->m[3] = IScalar_getMantissa(w);
}

int IFVector4_getf(_IFVector4 *pS, int n)
{
    if (pS == NULL)
        imAssert("pS", "imApi.inl", 0x2BDu);
    if (!(n >= 0 && n < 4))
        imAssert("n >= 0 && n < 4", "imApi.inl", 0x2BEu);
    return IScalar_set((pS->e >> (8 * n)) & 0xFFu, pS->m[n]);
}

void IFVector4_copy(_IFVector4 *pD, _IFVector4 *pS)
{
    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "imApi.inl", 0x9BBu);
    *pD = *pS;
}

void IFVector4_negate(_IFVector4 *dst, _IFVector4 *src)
{
    dst->e = src->e;
    dst->m[0] = -src->m[0];
    dst->m[1] = -src->m[1];
    dst->m[2] = -src->m[2];
    dst->m[3] = -src->m[3];
}

void IFVector4_setxv(_IFVector4 *pD, int *pS, int n)
{
    unsigned int ve = 2130706432;           /* 0x7F000000: w defaults to 1.0 */
    int i, t;

    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "IVector4.c", 0x2F0u);
    if (n == 0)
        imAssert("n", "IVector4.c", 0x2F1u);
    pD->m[1] = 0;
    pD->m[2] = 0;
    pD->m[3] = 0x20000000;
    for (i = 0; i < n; ++i)
    {
        t = IScalar_fixedToScalar(pS[i], 16);
        ve = (ve & ~(255u << (8 * i))) | (IScalar_getExponent(t) << (8 * i));
        pD->m[i] = IScalar_getMantissa(t);
    }
    pD->e = ve;
}

void IFVector4_setfv(_IFVector4 *pD, int *pS, int n)
{
    unsigned int ve = 2130706432;
    int i, ix, m;
    unsigned int e;

    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "IVector4.c", 0x306u);
    if (n == 0)
        imAssert("n", "IVector4.c", 0x307u);
    pD->m[1] = 0;
    pD->m[2] = 0;
    pD->m[3] = 0x20000000;
    for (i = 0; i < n; ++i)
    {
        ix = pS[i];
        e = (HGuint8)(ix >> 23);
        if ((HGuint8)(ix >> 23) != 0)
        {
            if (e == 255)
            {
                e = 254;
                m = (ix >= 0) ? 0x3FFFFFFF : -1073741823;
            }
            else
            {
                m = (int)((unsigned int)(((ix >> 31) ^ ((ix & 0x7FFFFF) | 0x800000)) - (ix >> 31)) << 6);
            }
        }
        else
        {
            e = 0;
            m = 0;
        }
        ve = (ve & ~(255u << (8 * i))) | (e << (8 * i));
        pD->m[i] = m;
    }
    pD->e = ve;
}

/* --- arithmetic --------------------------------------------------------- */

int IVector4_getf(_IVector4 *pA, int i)
{
    int r;
    if (!(pA != NULL && i >= 0 && i < 4))
        imAssert("pA && i >= 0 && i < 4", "IVector4.c", 0x89u);
    r = IScalar_setAndNormalize(pA->e, pA->m[i]);
    if (IScalar_isValid(r) == 0)
        imAssert("IScalar_isValid(r)", "IVector4.c", 0x8Eu);
    return r;
}

void IVector4_smul(_IVector4 *pD, _IVector4 *pA, int b)
{
    HGint64 m[4];
    int e[4];
    unsigned int out = 0;
    int bump = 0, i;

    if (!(pD != NULL && pA != NULL))
        imAssert("pD && pA", "IVector4.c", 0x9Au);
    if (IVector4_isValid(pA) == 0)
        imAssert("IVector4_isValid(pA)", "IVector4.c", 0x9Bu);
    if (IScalar_isValid(b) == 0)
        imAssert("IScalar_isValid(b)", "IVector4.c", 0x9Cu);
    for (i = 0; i < 4; ++i)
    {
        m[i] = hgLsl64h_0_31(hgMul64(pA->m[i], IScalar_getMantissa(b)), 3);
        e[i] = m[i] + 0x40000000;
    }
    if (pA->e != 0 && IScalar_getExponent(b) != 0)
        out = pA->e + IScalar_getExponent(b) - 127;
    for (i = 0; i < 4; ++i)
        if (e[i] <= 0)
            bump = 1;
    for (i = 0; i < 4; ++i)
        m[i] >>= bump;
    out += bump;
    if ((int)out <= 0)
        IVector4_zero(pD);
    else
        IVector4_set(pD, out, (int)m[0], (int)m[1], (int)m[2], (int)m[3]);
}

void IVector4_add(_IVector4 *pD, _IVector4 *pA, _IVector4 *pB)
{
    _IVector4 a = *pA, b = *pB;
    HGuint32 ea = a.e, eb = b.e, eMax = eb, diff;
    HGint32 m0, m1, m2, m3;
    unsigned int asum;
    int clz;

    if (!(pD != NULL && pA != NULL && pB != NULL))
        imAssert("pD && pA && pB", "IVector4.c", 0xD6u);
    if (IVector4_isValid(pA) == 0)
        imAssert("IVector4_isValid(pA)", "IVector4.c", 0xD7u);
    if (IVector4_isValid(pB) == 0)
        imAssert("IVector4_isValid(pB)", "IVector4.c", 0xD8u);
    diff = hgDiff32(ea, eb);
    if (ea < eb)
    {
        _IVector4 t = a;
        a = b;
        b = t;
        ea = a.e; eb = b.e;
        eMax = ea;
    }
    else
    {
        eMax = ea;
    }
    diff = hgMinu32(diff, 0x1Fu);
    m0 = a.m[0] + (b.m[0] >> diff);
    m1 = a.m[1] + (b.m[1] >> diff);
    m2 = a.m[2] + (b.m[2] >> diff);
    m3 = a.m[3] + (b.m[3] >> diff);
    asum = hgAbs32(m3) | hgAbs32(m2) | hgAbs32(m1) | hgAbs32(m0);
    if (asum != 0)
    {
        clz = hgClz32(asum);
        m0 = (int)((unsigned int)m0 << (clz - 1)) >> 1;
        m1 = (int)((unsigned int)m1 << (clz - 1)) >> 1;
        m2 = (int)((unsigned int)m2 << (clz - 1)) >> 1;
        m3 = (int)((unsigned int)m3 << (clz - 1)) >> 1;
        eMax += 1 - (clz - 1);
        if ((clz - 1) >= 0x3F)
            imAssert("sh < 63", "IVector4.c", 0x11Eu);
        if ((int)eMax > 0)
            IVector4_set(pD, eMax, m0, m1, m2, m3);
        else
            IVector4_zero(pD);
    }
    else
    {
        IVector4_zero(pD);
    }
}

/* --- clipping / viewport ------------------------------------------------ */

HGuint32 IVector4_clip(_IVector4 *clipPos, int guardBand)
{
    int x = clipPos->m[0];
    int y = clipPos->m[1];
    int z = clipPos->m[2];
    int w = clipPos->m[3];
    unsigned int clipMask;

    if ((w | z | y | x) != 0)
    {
        clipMask = (unsigned int)imAdd32OF(w, z) >> 31;
        clipMask |= (2 * ((unsigned int)imSub32OF(w, z) >> 31));
        clipMask |= (unsigned int)imAdd32OF(w, x) >> 31 << 6;
        clipMask |= (unsigned int)imSub32OF(w, x) >> 31 << 7;
        clipMask |= (unsigned int)imAdd32OF(w, y) >> 31 << 8;
        clipMask |= (unsigned int)imSub32OF(w, y) >> 31 << 9;
        if (clipMask != 0)
        {
            clipMask |= 4 * ((unsigned int)imAdd32OF(w, x >> guardBand) >> 31);
            clipMask |= 8 * ((unsigned int)imSub32OF(w, x >> guardBand) >> 31);
            clipMask |= 16 * ((unsigned int)imAdd32OF(w, y >> guardBand) >> 31);
            clipMask |= 32 * ((unsigned int)imSub32OF(w, y >> guardBand) >> 31);
            return clipMask;
        }
        return clipMask;
    }
    else
    {
        if (clipPos->e != 0)
            imAssert("clipPos->e == (0x00u)", "imApi.inl", 1351);
        return 15;
    }
}

void IVector4_wNorm(int *w, _IVector4 *v0, _IVector4 *v1, _IVector4 *v2)
{
    unsigned int e0 = v0->e, e1 = v1->e, e2 = v2->e;
    int maxe = hgMax32(hgMax32(v0->e, v1->e), v2->e) + 1;
    w[0] = v0->m[3] >> hgMinu32(maxe - e0, 0x1Fu);
    w[1] = v1->m[3] >> hgMinu32(maxe - e1, 0x1Fu);
    w[2] = v2->m[3] >> hgMinu32(maxe - e2, 0x1Fu);
}

void IVector4_fixClipSpacePosition(_IVector4 *pD, _IVector4 *src, int nGuardbandBits)
{
    int w = src->m[3];
    HGuint32 e = src->e;
    int x, y, z, gw;
    unsigned int amax;

    if (!(pD != NULL && src != NULL && IVector4_isValid(src) != 0))
        imAssert("pD && src && IVector4_isValid(src)", "IVector4.c", 0x49u);
    if (IVector4_isValid(src) == 0)
        imAssert("IVector4_isValid(src)", "IVector4.c", 0x4Du);
    if (w <= 0)
    {
        IVector4_set(pD, 0x7Fu, 0, 0, -536870912, 0x20000000);
        return;
    }
    if (w > (0x20000000 >> (nGuardbandBits - 1)))
    {
        x = src->m[0];
        y = src->m[1];
    }
    else
    {
        gw = imLsl32OF(w, nGuardbandBits);
        if (hgInRange32(gw, -1073741824, 0x40000000) == 0)
            imAssert("hgInRange32(gw, -IUNITSCALAR_TWO, IUNITSCALAR_TWO)", "IVector4.c", 0x58u);
        x = hgMin32(hgMax32(src->m[0], -gw), gw);
        y = hgMin32(hgMax32(src->m[1], -gw), gw);
    }
    z = hgMin32(hgMax32(src->m[2], -w), w);
    amax = hgAbs32(w) | hgAbs32(z) | hgAbs32(y) | hgAbs32(x);
    if ((amax & 0x20000000) != 0)
    {
        IVector4_set(pD, e, x, y, z, w);
    }
    else if (amax != 0)
    {
        int sh = hgClz32(amax) - 2;
        e -= sh;
        if ((int)e > 0)
            IVector4_set_noINF(pD, e, (int)((unsigned int)x << sh),
                              (int)((unsigned int)y << sh),
                              (int)((unsigned int)z << sh),
                              (int)((unsigned int)w << sh));
        else
        {
            IVector4_zero(pD);
            if (IVector4_isValid(pD) == 0)
                imAssert("IVector4_isValid(pD)", "IVector4.c", 0x79u);
        }
    }
    else
    {
        IVector4_zero(pD);
        if (IVector4_isValid(pD) == 0)
            imAssert("IVector4_isValid(pD)", "IVector4.c", 0x79u);
    }
}

int IVector4_stp(_IVector4 *pA, _IVector4 *pB, _IVector4 *pC)
{
    HGint64 r64;
    int r;

    if (!(pA != NULL && pB != NULL && pC != NULL))
        imAssert("pA && pB && pC", "IVector4.c", 0x40Bu);
    if (IVector4_isValid(pA) == 0)
        imAssert("IVector4_isValid(pA)", "IVector4.c", 0x40Cu);
    if (IVector4_isValid(pB) == 0)
        imAssert("IVector4_isValid(pB)", "IVector4.c", 0x40Du);
    if (IVector4_isValid(pC) == 0)
        imAssert("IVector4_isValid(pC)", "IVector4.c", 0x40Eu);
    r64 = hgMul64(pB->m[0], 2 * pC->m[1]);
    r64 = hgMadd64h(r64, -pC->m[0], 2 * pB->m[1]);
    r64 = hgMul64(r64, pA->m[3]);
    r64 = hgMadd64(r64, hgMadd64h(hgMul64(pC->m[0], 2 * pA->m[1]), -pA->m[0], 2 * pC->m[1]), pB->m[3]);
    r64 = hgMadd64(r64, hgMadd64h(hgMul64(pA->m[0], 2 * pB->m[1]), -pB->m[0], 2 * pA->m[1]), pC->m[3]);
    r = (int)hgGet64h(r64);
    if (hgInRange32(r, -94, 93) != 0)
        return 0;
    return r;
}

static inline int iv4_mulLsl32h(int a, int b, unsigned int sh)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    unsigned int lo;
    int hi;
    unsigned int rsh = 31u - sh;
    __asm__("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(a), "r"(b));
    return (int)(((unsigned int)hi << sh) | ((lo >> 1) >> rsh));
#else
    return hgLsl64h_0_31(hgMul64(a, b), (int)sh);
#endif
}

void IVector4_viewportTransform(int *pD, _IVector4 *pS, int *pScale, int *pOffset, int xyShift)
{
    if (!(pD != NULL && pS != NULL && pScale != NULL && pOffset != NULL))
        imAssert("pD && pS && pScale && pOffset", "imApi.inl", 0x56Au);
    if (IVector4_isValid(pS) == 0)
        imAssert("IVector4_isValid(pS)", "imApi.inl", 0x56Bu);
    if (!(pS->m[3] == 0 || pS->m[3] >= IUNITSCALAR_ONE / 8))
        imAssert("pS->m[3] == 0 || pS->m[3] >= IUNITSCALAR_ONE/8", "imApi.inl", 0x56Cu);
    if (hgInRange32(xyShift, 0, 31) == 0)
        imAssert("hgInRange32(xyShift, 0, 31)", "imApi.inl", 0x56Du);
    if (pS->m[3] != 0)
    {
        unsigned int w = (unsigned int)pS->m[3];
        /* Preserve the reciprocal estimator's bias and truncation for the
         * common normalized affine W. This is not exact division by W. */
        HGint64 rr = w == IUNITSCALAR_ONE
            ? (HGint64)0x7fffffef00000002ULL : imRcpu(w + (w >> 27));
        unsigned int recip = (unsigned int)(rr >> 32);   /* high: mantissa */
        unsigned int rsh = (unsigned int)rr;             /* low: shift     */
        int sx, sy, sz;

        if (hgInRange32((int)rsh, 1, 5) == 0)
            imAssert("hgInRange32(rsh, 1, 5)", "imApi.inl", 0x576u);
        sx = iv4_mulLsl32h(pS->m[0], (int)recip, rsh - 1u);
        sy = iv4_mulLsl32h(pS->m[1], (int)recip, rsh - 1u);
        sz = hgMul64h((int)((unsigned int)pS->m[2] << ((int)rsh - 1)), (int)recip);
        if (hgSignBit32(sx) != hgSignBit32(pS->m[0]))
            imAssert("hgSignBit32(sx) == hgSignBit32(pS->m[0])", "imApi.inl", 0x580u);
        if (hgSignBit32(sy) != hgSignBit32(pS->m[1]))
            imAssert("hgSignBit32(sy) == hgSignBit32(pS->m[1])", "imApi.inl", 0x581u);
        if (hgInRange32(sz, -536870912, 0x20000000) == 0)
            imAssert("hgInRange32(sz, -IUNITSCALAR_ONE, IUNITSCALAR_ONE)", "imApi.inl", 0x582u);
        pD[0] = (int)((unsigned int)(*pOffset + hgMul64h(sx, *pScale)) << xyShift);
        pD[1] = (int)((unsigned int)(pOffset[1] + hgMul64h(sy, pScale[1])) << xyShift);
        pD[2] = pOffset[2] + iv4_mulLsl32h(sz, pScale[2], 3u);
    }
    else
    {
        pD[0] = (int)((unsigned int)*pOffset << xyShift);
        pD[1] = (int)((unsigned int)pOffset[1] << xyShift);
        pD[2] = pOffset[2];
    }
}
