/* IMatrix4.c - 4x4 shared-exponent matrices and transform helpers.
 *
 * Texture-coordinate transforms are implemented in grTransform.c.
 */
#include "imath/imApi.h"
#include "hg/hgInt32.h"
#include "gerbera/grUtil.h"

/* --- scalar access on _IFVector4 columns (IMatrix4.c statics) ---------- */

static int getScalar(_IFVector4 *pS, int ri)
{
    if (pS == NULL)
        imAssert("pS", "IMatrix4.c", 0x10Eu);
    return IScalar_set((pS->e >> (8 * ri)) & 0xFFu, pS->m[ri]);
}

static void setScalar(_IFVector4 *pD, int ri, int x)
{
    if (pD == NULL)
        imAssert("pD", "IMatrix4.c", 0x114u);
    if (IScalar_isValid(x) == 0)
        imAssert("IScalar_isValid(x)", "IMatrix4.c", 277);
    pD->e = (pD->e & ~(255u << (8 * ri))) | (IScalar_getExponent(x) << (8 * ri));
    pD->m[ri] = IScalar_getMantissa(x);
}

static void copy_column(_IFVector4 *pD, _IFVector4 *pA)
{
    *pD = *pA;
}

/* pD[c] = sum_i pA[i][c] * pB->m[i]  (matrix rows times vector) */
static void rows_by_column(_IFVector4 *pD, _IMatrix4x4 *pA, _IFVector4 *pB)
{
    _IFVector4 source = *pB;
    unsigned int out = 0;
    int c, i;

    /* Matrix multiplication permits the destination to alias its right operand. */
    pB = &source;
    for (c = 0; c < 4; ++c)
    {
        unsigned int e[4];
        unsigned int eMax;
        HGint64 dp = 0;
        int m, exp;

        e[0] = (pA->v[0].e >> (8 * c)) & 0xFFu;
        e[1] = (pA->v[1].e >> (8 * c)) & 0xFFu;
        e[2] = (pA->v[2].e >> (8 * c)) & 0xFFu;
        e[3] = (pA->v[3].e >> (8 * c)) & 0xFFu;
        for (i = 0; i < 4; ++i)
        {
            if (e[i] != 0 && ((pB->e >> (8 * i)) & 0xFFu) != 0)
                e[i] += (pB->e >> (8 * i)) & 0xFFu;
            else
                e[i] = 0;
        }
        eMax = e[0];
        if (eMax < e[1]) eMax = e[1];
        if (eMax < e[2]) eMax = e[2];
        if (eMax < e[3]) eMax = e[3];
        if (e[0] != 0)
            dp = imMulAsr64(pA->v[0].m[c], pB->m[0], imClampShift63(eMax - e[0]));
        if (e[1] != 0)
            dp = hgAdd64(dp, imMulAsr64(pA->v[1].m[c], pB->m[1], imClampShift63(eMax - e[1])));
        if (e[2] != 0)
            dp = hgAdd64(dp, imMulAsr64(pA->v[2].m[c], pB->m[2], imClampShift63(eMax - e[2])));
        if (e[3] != 0)
            dp = hgAdd64(dp, imMulAsr64(pA->v[3].m[c], pB->m[3], imClampShift63(eMax - e[3])));
        if (hgIsZero64(dp) != 0)
        {
            exp = 0;
            m = 0;
        }
        else
        {
            int sh = hgClz64(hgAbs64(dp)) - 2;
            exp = (int)eMax + 3 - (sh + 127);
            m = hgLsl64h(dp, sh);
            if (m == -1073741824)
            {
                m = (int)0xE0000000;
                ++exp;
            }
            if (exp <= 0)
            {
                exp = 0;
                m = 0;
            }
            if (exp > 0xFE)
            {
                exp = 254;
                m = ((m & 0x80000000) == 0) ? 0x3FFFFFFF : (int)0xC0000001;
            }
        }
        out |= (unsigned int)exp << (8 * c);
        pD->m[c] = m;
    }
    pD->e = out;
}

/* pD->m[ri] = pA->m[ri] * x with per-component exponent tracking */
static void rows_by_scalar(_IFVector4 *pD, _IFVector4 *pA, int x)
{
    unsigned int de = 0;
    int ri;

    for (ri = 0; ri < 4; ++ri)
    {
        HGint64 p = hgLsl64h_0_31(hgMul64(pA->m[ri], IScalar_getMantissa(x)), 3);
        unsigned int ea = (pA->e >> (8 * ri)) & 0xFFu;
        int e = IScalar_getExponent(x) - 127 + (int)ea;
        int m = (int)p;

        if (m == 0)
            e = 0;
        if (m >= 0x40000000 || m <= -0x40000000)
        {
            m >>= 1;
            ++e;
        }
        if (e <= 0)
        {
            e = 0;
            m = 0;
        }
        if (e > 0xFE)
        {
            e = 254;
            m = (m >= 0) ? 0x3FFFFFFF : -1073741823;
        }
        de |= (unsigned int)e << (8 * ri);
        pD->m[ri] = m;
    }
    pD->e = de;
}

/* rcp(l - r) with the reference's zero handling */
static int subrcp(int *l, int *r)
{
    int s = IScalar_sub(*l, *r);
    if (IScalar_testne(s) != 0)
        return IScalar_rcp(s);
    if (IScalar_testgt(s) != 0)
        return IScalar_iftos(2139095039);
    return IScalar_iftos(-8388609);
}

/* dot3 with per-operand exponent alignment: returns IScalar */
static int dp3h(int ax, int ay, int az, int bx, int by, int bz)
{
    int e[3], m, exp;

    e[0] = IScalar_getExponent(ax);
    e[1] = IScalar_getExponent(bx);
    e[0] = (e[0] != 0 && e[1] != 0) ? e[0] + e[1] : 0;
    e[1] = IScalar_getExponent(ay);
    e[2] = IScalar_getExponent(by);
    e[1] = (e[1] != 0 && e[2] != 0) ? e[1] + e[2] : 0;
    e[2] = IScalar_getExponent(az);
    {
        int ez = IScalar_getExponent(bz);
        e[2] = (e[2] != 0 && ez != 0) ? e[2] + ez : 0;
    }
    exp = e[0];
    if (exp < e[1]) exp = e[1];
    if (exp < e[2]) exp = e[2];
    if (e[0] != 0)
        m = hgMul64h(2 * IScalar_getMantissa(ax), 2 * IScalar_getMantissa(bx)) >>
            hgMinu32(exp + 1 - e[0], 0x1Fu);
    else
        m = 0;
    if (e[1] != 0)
        m += hgMul64h(2 * IScalar_getMantissa(ay), 2 * IScalar_getMantissa(by)) >>
             hgMinu32(exp + 1 - e[1], 0x1Fu);
    if (e[2] != 0)
        m += hgMul64h(2 * IScalar_getMantissa(az), 2 * IScalar_getMantissa(bz)) >>
             hgMinu32(exp + 1 - e[2], 0x1Fu);
    if (m != 0)
    {
        int clz = hgClz32(hgAbs32(m));
        m = (int)((unsigned int)m << (clz - 1));
        m = ((int)m) >> 1;
        exp = exp + 2 - (clz - 1 + 126);
        if (exp <= 0)
        {
            exp = 0;
            m = 0;
        }
        if (exp > 0xFE)
        {
            exp = 254;
            m = ((m & 0x80000000) == 0) ? 0x3FFFFFFF : (int)0xC0000001;
        }
    }
    else
    {
        exp = 0;
    }
    return IScalar_set(exp, m);
}

/* --- basics ------------------------------------------------------------ */

void IMatrix4x4_copy(_IMatrix4x4 *dst, _IMatrix4x4 *src)
{
    int i;
    if (dst == src)
        return;
    for (i = 3; i >= 0; --i)
        dst->v[i] = src->v[i];
}

void IMatrix4x4_transpose(_IMatrix4x4 *dst, _IMatrix4x4 *src)
{
    int i, j;
    if (!(src != NULL && dst != NULL))
        imAssert("src && dst", "IMatrix4.c", 0x50u);
    for (i = 0; i < 4; ++i)
    {
        for (j = i; j < 4; ++j)
        {
            unsigned int e0 = (src->v[i].e >> (8 * j)) & 0xFFu;
            int m0 = src->v[i].m[j];
            int m1 = src->v[j].m[i];
            dst->v[i].e = (dst->v[i].e & ~(255u << (8 * j))) |
                          (((src->v[j].e >> (8 * i)) & 0xFFu) << (8 * j));
            dst->v[j].e = (dst->v[j].e & ~(255u << (8 * i))) | (e0 << (8 * i));
            dst->v[i].m[j] = m1;
            dst->v[j].m[i] = m0;
        }
    }
}

void IMatrix4x4_setfv(_IMatrix4x4 *pD, int *pS)
{
    int ci, ri;
    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "IMatrix4.c", 0x77u);
    for (ci = 0; ci < 4; ++ci)
    {
        unsigned int ce = 0;
        for (ri = 0; ri < 4; ++ri)
        {
            int v = *pS++;
            unsigned int e = (HGuint8)(v >> 23);
            int m = ((v & 0x7FFFFF) << 6) | 0x20000000;
            if ((HGuint8)(v >> 23) == 0)
                m = 0;
            if (e == 255)
            {
                e = 254;
                m = 0x3FFFFFFF;
            }
            if (v < 0)
                m = -m;
            ce |= e << (8 * ri);
            pD->v[ci].m[ri] = m;
        }
        pD->v[ci].e = ce;
    }
}

void IMatrix4x4_getfv(int *pD, _IMatrix4x4 *pS)
{
    int ci, ri;
    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "IMatrix4.c", 0xA3u);
    for (ci = 0; ci < 4; ++ci)
    {
        for (ri = 0; ri < 4; ++ri)
        {
            unsigned int e = (pS->v[ci].e >> (8 * ri)) & 0xFFu;
            int m = pS->v[ci].m[ri];
            unsigned int s = (unsigned int)m & 0x80000000u;
            if (m < 0)
                m = -m;
            if ((pS->v[ci].e >> (8 * ri) & 0xFFu) == 0)
            {
                if ((unsigned int)m >> 6 != 0)
                    imAssert("m == 0", "IMatrix4.c", 0xB6u);
                e = 0;
            }
            if (e == 255)
                imAssert("e != 255", "IMatrix4.c", 186);
            *pD++ = (int)((((unsigned int)m >> 6) & 0xFF7FFFFFu) | s | (e << 23));
        }
    }
}

void IMatrix4x4_identity(_IMatrix4x4 *pD)
{
    unsigned int e = 2130706432;    /* 0x7F000000 */
    int ci;
    if (pD == NULL)
        imAssert("pD", "IMatrix4.c", 0xD0u);
    for (ci = 3; ci >= 0; --ci)
    {
        pD->v[ci].e = e;
        pD->v[ci].m[0] = 0;
        pD->v[ci].m[1] = 0;
        pD->v[ci].m[2] = 0;
        pD->v[ci].m[3] = 0;
        pD->v[ci].m[ci] = 0x20000000;
        e >>= 8;
    }
}

HGint32 I64_tof(HGint64 x)
{
    unsigned int e = 0;
    int m = 0;
    if (hgIsZero64(x) == 0)
    {
        int sh = hgClz64(hgAbs64(x)) - 2;
        e = 138 - sh;
        if (138 - sh > 0x7F)
            imAssert("e <= 127", "IMatrix4.c", 0xFAu);
        m = hgLsl64h(x, sh);
        if (m == -1073741824)
        {
            m = -536870912;
            ++e;
        }
    }
    return IScalar_set(e, m);
}

int IMatrix4x4_getScalar(_IMatrix4x4 *pS, int c, int r)
{
    if (pS == NULL)
        imAssert("pS", "imApi.inl", 0xA22u);
    if (!(c >= 0 && c <= 3))
        imAssert("c>=0 && c<=3", "imApi.inl", 0xA23u);
    if (!(r >= 0 && r <= 3))
        imAssert("r>=0 && r<=3", "imApi.inl", 0xA24u);
    return IFVector4_getf(&pS->v[c], r);
}

/* --- products ---------------------------------------------------------- */

void IMatrix4x4_mul(_IMatrix4x4 *pD, _IMatrix4x4 *pA, _IMatrix4x4 *pB)
{
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int i;

    if (!(pA != NULL && pB != NULL))
        imAssert("pA && pB", "IMatrix4.c", 0x201u);
    if (pD == pA)
        dst = &tmp;
    for (i = 3; i >= 0; --i)
        rows_by_column(&dst->v[i], pA, &pB->v[i]);
    IMatrix4x4_copy(pD, dst);
}

void IMatrix4x4_mulfv4(_IFVector4 *pD, _IMatrix4x4 *pM, _IFVector4 *pA)
{
    int x, y, z, w;
    if (!(pD != NULL && pM != NULL && pA != NULL))
        imAssert("pD && pM && pA", "IMatrix4.c", 0x822u);
    x = IFVector4_dp4l(&pM->v[0], pA);
    y = IFVector4_dp4l(&pM->v[1], pA);
    z = IFVector4_dp4l(&pM->v[2], pA);
    w = IFVector4_dp4l(&pM->v[3], pA);
    pD->e = IScalar_getExponent(x) | (IScalar_getExponent(y) << 8) |
            (IScalar_getExponent(z) << 16) | (IScalar_getExponent(w) << 24);
    pD->m[0] = IScalar_getMantissa(x);
    pD->m[1] = IScalar_getMantissa(y);
    pD->m[2] = IScalar_getMantissa(z);
    pD->m[3] = IScalar_getMantissa(w);
}

/* --- projections --------------------------------------------------------- */

void IMatrix4x4_frustum(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms)
{
    _IFVector4 last;
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int rcp[3];
    int twoFar, v;

    if (!(pD != NULL && pM != NULL && parms != NULL))
        imAssert("pD && pM && parms", "IMatrix4.c", 0x259u);
    {
        int i;
        for (i = 0; i < 6; ++i)
            if (IScalar_isValid(parms[i]) == 0)
                imAssert(i == 0 ? "IScalar_isValid(parms[0])" : "IScalar_isValid(parms[i])",
                         "IMatrix4.c", 0x25Au + i);
    }
    rcp[0] = subrcp(parms + 1, parms);              /* 1/(r-l)  */
    rcp[1] = subrcp(parms + 3, parms + 2);          /* 1/(t-b)  */
    rcp[2] = subrcp(parms + 4, parms + 5);          /* 1/(f-n)  */
    twoFar = IScalar_add(parms[4], parms[4]);
    v = IScalar_mul(parms[5], twoFar);
    {
        int one = IScalar_negate(IScalar_one());
        int e2 = IScalar_mul(IScalar_add(parms[5], parms[4]), rcp[2]);
        int e1 = IScalar_mul(IScalar_add(parms[3], parms[2]), rcp[1]);
        int e0 = IScalar_mul(IScalar_add(parms[1], parms[0]), rcp[0]);
        IFVector4_set(&last, e0, e1, e2, one);
    }
    if (pD == pM)
        dst = &tmp;
    rows_by_scalar(&dst->v[0], &pM->v[0], IScalar_mul(twoFar, rcp[0]));
    rows_by_scalar(&dst->v[1], &pM->v[1], IScalar_mul(twoFar, rcp[1]));
    rows_by_column(&dst->v[2], pM, &last);
    rows_by_scalar(&dst->v[3], &pM->v[2], IScalar_mul(v, rcp[2]));
    IMatrix4x4_copy(pD, dst);
}

void IMatrix4x4_ortho(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms)
{
    _IFVector4 last;
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int rcp[3];
    int i;

    if (!(pD != NULL && parms != NULL))
        imAssert("pD && parms", "IMatrix4.c", 0x842u);
    {
        int k;
        for (k = 0; k < 6; ++k)
            if (IScalar_isValid(parms[k]) == 0)
                imAssert(k == 0 ? "IScalar_isValid(parms[0])" : "IScalar_isValid(parms[k])",
                         "IMatrix4.c", 0x843u + k);
    }
    rcp[0] = subrcp(parms + 1, parms);
    rcp[1] = subrcp(parms + 3, parms + 2);
    rcp[2] = subrcp(parms + 4, parms + 5);
    {
        int e0 = IScalar_negate(IScalar_mul(IScalar_add(parms[1], parms[0]), rcp[0]));
        int e1 = IScalar_negate(IScalar_mul(IScalar_add(parms[3], parms[2]), rcp[1]));
        int e2 = IScalar_mul(IScalar_add(parms[5], parms[4]), rcp[2]);
        IFVector4_set(&last, e0, e1, e2, IScalar_one());
    }
    if (pD == pM)
        dst = &tmp;
    for (i = 2; i >= 0; --i)
        rows_by_scalar(&dst->v[i], &pM->v[i], IScalar_add(rcp[i], rcp[i]));
    rows_by_column(&dst->v[3], pM, &last);
    IMatrix4x4_copy(pD, dst);
}

void IMatrix4x4_scale(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms)
{
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int i;

    if (!(pD != NULL && parms != NULL))
        imAssert("pD && parms", "IMatrix4.c", 0x874u);
    {
        int k;
        for (k = 0; k < 3; ++k)
            if (IScalar_isValid(parms[k]) == 0)
                imAssert(k == 0 ? "IScalar_isValid(parms[0])" : "IScalar_isValid(parms[k])",
                         "IMatrix4.c", 0x875u + k);
    }
    if (pD == pM)
        dst = &tmp;
    for (i = 2; i >= 0; --i)
        rows_by_scalar(&dst->v[i], &pM->v[i], parms[i]);
    copy_column(&dst->v[3], &pM->v[3]);
    IMatrix4x4_copy(pD, dst);
}

void IMatrix4x4_translate(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms)
{
    _IFVector4 last;
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int i;

    if (!(pD != NULL && pM != NULL && parms != NULL))
        imAssert("pD && pM && parms", "IMatrix4.c", 0x893u);
    if (pD == pM)
        dst = &tmp;
    IFVector4_setfv(&last, parms, 3);
    for (i = 2; i >= 0; --i)
        copy_column(&dst->v[i], &pM->v[i]);
    rows_by_column(&dst->v[3], pM, &last);
    IMatrix4x4_copy(pD, dst);
}

void IMatrix4x4_rotate(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int angle, _IFVector3 *axisf)
{
    _IFVector4 cols[3];             /* rotation matrix columns 0..2 */
    _IVector3 axisv;
    int axis[3];
    int sinUS, cosUS, oneMinusCosUS;
    HGint64 cxs;
    HGint64 p, q;
    _IMatrix4x4 tmp;
    _IMatrix4x4 *dst = pD;
    int i, j, m, k;

    if (!(pD != NULL && axisf != NULL))
        imAssert("pD && axisf", "IMatrix4.c", 0x8B2u);
    if (IScalar_isValid(angle) == 0)
        imAssert("IScalar_isValid(angle)", "IMatrix4.c", 0x8B3u);
    if (IScalar_isValid(IFVector3_getf(axisf, 0)) == 0)
        imAssert("IScalar_isValid(IFVector3_getf(axisf,0))", "IMatrix4.c", 0x8B4u);
    if (IScalar_isValid(IFVector3_getf(axisf, 1)) == 0)
        imAssert("IScalar_isValid(IFVector3_getf(axisf,1))", "IMatrix4.c", 0x8B5u);
    if (IScalar_isValid(IFVector3_getf(axisf, 2)) == 0)
        imAssert("IScalar_isValid(IFVector3_getf(axisf,2))", "IMatrix4.c", 0x8B6u);

    sinUS = IScalar_toUnitScalar(IScalar_ftos(hgFsin(IScalar_stof(IScalar_degreesToRadians(angle)))));
    cosUS = IScalar_toUnitScalar(IScalar_ftos(hgFcos(IScalar_stof(IScalar_degreesToRadians(angle)))));
    oneMinusCosUS = 0x20000000 - cosUS;
    IFVector3_tov3(&axisv, axisf);
    XNormal3_normalize(axis, axisv.m);
    cxs = hgAsr64_0_31(hgSet64x(cosUS), 11);        /* 50 fractional bits */

    /* The homogeneous component stays zero; the other components follow. */
    for (i = 0; i < 3; ++i)
    {
        cols[i].e = 0;
        cols[i].m[3] = 0;
    }

    /* off-diagonal pairs: (k,j,m) = (1,2,0), (2,0,1), (0,1,2) */
    for (i = 0; i < 3; ++i)
    {
        static const int kk[3] = { 1, 2, 0 };
        static const int jj[3] = { 2, 0, 1 };
        HGint64 tmpk;

        k = kk[i];
        j = jj[i];
        m = i;
        tmpk = hgLsl64h_0_31(hgMul64(axis[k], oneMinusCosUS), 11);  /* 29 fractional bits */
        p = hgMul64((int)tmpk, axis[j]);                            /* 50 fractional bits */
        q = hgMul64(axis[m], sinUS);
        setScalar(&cols[k], j, I64_tof(hgAdd64(p, q)));
        setScalar(&cols[j], k, I64_tof(hgSub64(p, q)));
        /* diagonal uses a_m * t * a_m (decompile reads tmp[m+1], not tmp[k+1]) */
        tmpk = hgLsl64h_0_31(hgMul64(axis[m], oneMinusCosUS), 11);
        setScalar(&cols[m], m, I64_tof(hgAdd64(cxs, hgMul64((int)tmpk, axis[m]))));
    }

    if (pD == pM)
        dst = &tmp;
    for (i = 2; i >= 0; --i)
        rows_by_column(&dst->v[i], pM, &cols[i]);
    copy_column(&dst->v[3], &pM->v[3]);
    IMatrix4x4_copy(pD, dst);
}

/* --- determinants / inverse --------------------------------------------- */

HGint32 IMatrix4x4_det2h(_IMatrix4x4 *pA, int ca, int ra, int cb, int rb)
{
    unsigned int ea, eb, e2, eDiff;
    HGint64 p0, p1;
    int m, exp;

    if (pA == NULL)
        imAssert("pA", "IMatrix4.c", 0x91Eu);
    ea = (pA->v[ca].e >> (8 * ra)) & 0xFFu;
    eb = (pA->v[cb].e >> (8 * rb)) & 0xFFu;
    ea = (ea != 0 && eb != 0) ? ea + eb : 0;
    e2 = (pA->v[cb].e >> (8 * ra)) & 0xFFu;
    eb = (pA->v[ca].e >> (8 * rb)) & 0xFFu;
    e2 = (e2 != 0 && eb != 0) ? e2 + eb : 0;
    eDiff = hgMinu32(hgDiff32(ea, e2), 0x1Fu);
    p0 = hgLsl64h_0_31(hgMul64(pA->v[ca].m[ra], pA->v[cb].m[rb]), 2);
    p1 = hgLsl64h_0_31(hgMul64(pA->v[cb].m[ra], pA->v[ca].m[rb]), 2);
    if (ea >= e2)
    {
        exp = ea;
        p1 >>= eDiff;
    }
    else
    {
        exp = e2;
        p0 >>= eDiff;
    }
    m = (int)(p0 - p1);
    if (m != 0)
    {
        int clz = hgClz32(hgAbs32(m));
        exp = (int)exp + 1 - (clz - 1 + 126);   /* shared max exp drives it */
        m = (int)((unsigned int)m << (clz - 1)) >> 1;
        if (exp <= 0)
        {
            exp = 0;
            m = 0;
        }
        if (exp > 0xFE)
        {
            exp = 254;
            m = ((m & 0x80000000) == 0) ? 0x3FFFFFFF : (int)0xC0000001;
        }
    }
    else
    {
        exp = 0;
    }
    return IScalar_set(exp, m);
}

void IMatrix4x4_inverse(_IMatrix4x4 *pD, _IMatrix4x4 *pS, int bScale)
{
    /* d[0..17]: det2h results, d[18..25]: source scalars a30,a20,a10,a01,
     * a31,a21,a11,a00 (decompile i[4]..i[104]) */
    int d[26];
    int w[4];
    unsigned int e;
    int det;
    int i;

    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "IMatrix4.c", 0x97Au);
    if (pD == pS)
        imAssert("pD != pS", "IMatrix4.c", 0x97Bu);
    d[25] = getScalar(&pS->v[0], 0);
    d[24] = getScalar(&pS->v[1], 0);
    d[23] = getScalar(&pS->v[2], 0);
    d[22] = getScalar(&pS->v[3], 0);
    d[21] = getScalar(&pS->v[0], 1);
    d[20] = getScalar(&pS->v[1], 1);
    d[19] = getScalar(&pS->v[2], 1);
    d[18] = getScalar(&pS->v[3], 1);
    d[17] = IMatrix4x4_det2h(pS, 0, 2, 1, 3);
    d[16] = IMatrix4x4_det2h(pS, 0, 2, 2, 3);
    d[15] = IMatrix4x4_det2h(pS, 0, 2, 3, 3);
    d[14] = IMatrix4x4_det2h(pS, 1, 2, 2, 3);
    d[13] = IMatrix4x4_det2h(pS, 1, 2, 3, 3);
    d[12] = IMatrix4x4_det2h(pS, 2, 2, 3, 3);
    d[11] = IMatrix4x4_det2h(pS, 0, 1, 3, 3);
    d[10] = IMatrix4x4_det2h(pS, 0, 1, 1, 3);
    d[9]  = IMatrix4x4_det2h(pS, 0, 1, 2, 3);
    d[8]  = IMatrix4x4_det2h(pS, 1, 1, 2, 3);
    d[7]  = IMatrix4x4_det2h(pS, 1, 1, 3, 3);
    d[6]  = IMatrix4x4_det2h(pS, 2, 1, 3, 3);
    d[5]  = IMatrix4x4_det2h(pS, 0, 1, 1, 2);
    d[4]  = IMatrix4x4_det2h(pS, 0, 1, 2, 2);
    d[3]  = IMatrix4x4_det2h(pS, 0, 1, 3, 2);
    d[2]  = IMatrix4x4_det2h(pS, 1, 1, 2, 2);
    d[1]  = IMatrix4x4_det2h(pS, 1, 1, 3, 2);
    d[0]  = IMatrix4x4_det2h(pS, 2, 1, 3, 2);

    setScalar(&pD->v[0], 0, dp3h(d[20], IScalar_negate(d[19]), d[18], d[12], d[13], d[14]));
    setScalar(&pD->v[0], 1, IScalar_negate(dp3h(d[21], IScalar_negate(d[19]), d[18], d[12], d[15], d[16])));
    setScalar(&pD->v[0], 2, dp3h(d[21], IScalar_negate(d[20]), d[18], d[13], d[15], d[17]));
    setScalar(&pD->v[0], 3, IScalar_negate(dp3h(d[21], IScalar_negate(d[20]), d[19], d[14], d[16], d[17])));
    setScalar(&pD->v[1], 0, IScalar_negate(dp3h(d[24], IScalar_negate(d[23]), d[22], d[12], d[13], d[14])));
    setScalar(&pD->v[1], 1, dp3h(d[25], IScalar_negate(d[23]), d[22], d[12], d[15], d[16]));
    setScalar(&pD->v[1], 2, IScalar_negate(dp3h(d[25], IScalar_negate(d[24]), d[22], d[13], d[15], d[17])));
    setScalar(&pD->v[1], 3, dp3h(d[25], IScalar_negate(d[24]), d[23], d[14], d[16], d[17]));
    setScalar(&pD->v[2], 0, dp3h(d[24], IScalar_negate(d[23]), d[22], d[6], d[7], d[8]));
    setScalar(&pD->v[2], 1, IScalar_negate(dp3h(d[25], IScalar_negate(d[23]), d[22], d[6], d[11], d[9])));
    setScalar(&pD->v[2], 2, dp3h(d[25], IScalar_negate(d[24]), d[22], d[7], d[11], d[10]));
    setScalar(&pD->v[2], 3, IScalar_negate(dp3h(d[25], IScalar_negate(d[24]), d[23], d[8], d[9], d[10])));
    setScalar(&pD->v[3], 0, IScalar_negate(dp3h(d[24], IScalar_negate(d[23]), d[22], d[0], d[1], d[2])));
    setScalar(&pD->v[3], 1, dp3h(d[25], IScalar_negate(d[23]), d[22], d[0], d[3], d[4]));
    setScalar(&pD->v[3], 2, IScalar_negate(dp3h(d[25], IScalar_negate(d[24]), d[22], d[1], d[3], d[5])));
    setScalar(&pD->v[3], 3, dp3h(d[25], IScalar_negate(d[24]), d[23], d[2], d[4], d[5]));

    /* first row becomes a vector for the determinant check */
    e = (pD->v[0].e & 0xFFu) | ((pD->v[1].e & 0xFFu) << 8) |
        ((pD->v[2].e & 0xFFu) << 16) | ((pD->v[3].e & 0xFFu) << 24);
    w[0] = pD->v[0].m[0];
    w[1] = pD->v[1].m[0];
    w[2] = pD->v[2].m[0];
    w[3] = pD->v[3].m[0];
    {
        _IFVector4 detv;
        detv.e = e;
        detv.m[0] = w[0];
        detv.m[1] = w[1];
        detv.m[2] = w[2];
        detv.m[3] = w[3];
        det = IFVector4_dp4l(&pS->v[0], &detv);
    }
    if (bScale != 0)
    {
        if (IScalar_testne(det) != 0)
            det = IScalar_rcp(det);
        for (i = 0; i < 4; ++i)
            rows_by_scalar(&pD->v[i], &pD->v[i], det);
    }
    else if (IScalar_testlt(det) != 0)
    {
        for (i = 0; i < 4; ++i)
        {
            pD->v[i].m[0] = -pD->v[i].m[0];
            pD->v[i].m[1] = -pD->v[i].m[1];
            pD->v[i].m[2] = -pD->v[i].m[2];
            pD->v[i].m[3] = -pD->v[i].m[3];
        }
    }
}

int IMatrix4x4_inversetranspose(_XNormalTransform *pD, _IMatrix4x4 *matrix, int bRescale)
{
    _IFVector4 cof[3];              /* cofactor columns t[2..4] */
    int det, maxExp = 0;
    int k, j, i, c, r;

    if (!(pD != NULL && matrix != NULL))
        imAssert("pD && matrix", "IMatrix4.c", 0x9EBu);
    k = 2;
    j = 0;
    i = 1;
    do
    {
        cof[k].e = 0;
        setScalar(&cof[k], 0, IMatrix4x4_det2h(matrix, j, 1, i, 2));
        setScalar(&cof[k], 1, IMatrix4x4_det2h(matrix, i, 0, j, 2));
        setScalar(&cof[k], 2, IMatrix4x4_det2h(matrix, j, 0, i, 1));
        cof[k].m[3] = 0;
        i = j;
        j = k--;
    } while (k >= 0);

    det = IFVector4_dp4l(&matrix->v[0], &cof[0]);
    if (IScalar_testeq(det) != 0)
        return 0;
    if (bRescale != 0)
    {
        int s = IFVector4_dp4l(&cof[2], &cof[2]);
        if (IScalar_testgt(s) != 0)
            s = IScalar_rsq(s);
        if (IScalar_testlt(det) != 0)
            s = IScalar_negate(s);
        det = s;
    }
    else
    {
        det = IScalar_rcp(det);
    }
    for (c = 0; c < 3; ++c)
    {
        for (r = 0; r < 3; ++r)
        {
            int v = IScalar_mul(IScalar_set((cof[c].e >> (8 * r)) & 0xFFu, cof[c].m[r]), det);
            unsigned int ex = IScalar_getExponent(v);
            cof[c].e = (cof[c].e & ~(255u << (8 * r))) | (ex << (8 * r));
            cof[c].m[r] = IScalar_getMantissa(v);
            if (maxExp < (int)ex)
                maxExp = IScalar_getExponent(v);
        }
    }
    for (c = 0; c < 3; ++c)
    {
        for (r = 0; r < 3; ++r)
        {
            int sh = hgMin32(maxExp - ((cof[c].e >> (8 * r)) & 0xFFu), 31);
            pD->m[c][r] = (cof[c].m[r] * 2) >> sh;
        }
    }
    pD->scale = hgMax32(hgMin32(maxExp - 125, 31), -31);
    return 1;
}

/* --- IFVector4 dot products --------------------------------------------- */

HGint64 IFVector4_dp4l_fast(_IFVector4 *pA, _IFVector4 *pB)
{
    int e[4], maxE, m = 0;

    if (!(pA != NULL && pB != NULL))
        imAssert("pA && pB", "IMatrix4.c", 0x50Du);
    e[0] = (pA->e & 0xFFu);
    e[1] = (pB->e & 0xFFu);
    e[0] = (e[0] != 0 && e[1] != 0) ? e[0] + e[1] : 0;
    e[1] = ((pA->e >> 8) & 0xFFu);
    {
        int eb = ((pB->e >> 8) & 0xFFu);
        e[1] = (e[1] != 0 && eb != 0) ? e[1] + eb : 0;
    }
    e[2] = ((pA->e >> 16) & 0xFFu);
    {
        int eb = ((pB->e >> 16) & 0xFFu);
        e[2] = (e[2] != 0 && eb != 0) ? e[2] + eb : 0;
    }
    e[3] = ((pA->e >> 24) & 0xFFu);
    {
        int eb = ((pB->e >> 24) & 0xFFu);
        e[3] = (e[3] != 0 && eb != 0) ? e[3] + eb : 0;
    }
    maxE = e[0];
    if (maxE < e[1]) maxE = e[1];
    if (maxE < e[2]) maxE = e[2];
    if (maxE < e[3]) maxE = e[3];
    m = hgMul64h(pA->m[0], pB->m[0]) >> hgMin32(maxE - e[0], 31);
    m += hgMul64h(pA->m[1], pB->m[1]) >> hgMin32(maxE - e[1], 31);
    m += hgMul64h(pA->m[2], pB->m[2]) >> hgMin32(maxE - e[2], 31);
    m += hgMul64h(pA->m[3], pB->m[3]) >> hgMin32(maxE - e[3], 31);
    if (m == 0)
        maxE = 0;
    return hgSet64(m, maxE);
}

HGint32 IFVector4_dp4l(_IFVector4 *pA, _IFVector4 *pB)
{
    int e[4], maxE, m = 0, exp;
    int i;

    if (!(pA != NULL && pB != NULL))
        imAssert("pA && pB", "IMatrix4.c", 0x764u);
    for (i = 0; i < 4; ++i)
    {
        int ea = (pA->e >> (8 * i)) & 0xFFu;
        int eb = (pB->e >> (8 * i)) & 0xFFu;
        e[i] = (ea != 0 && eb != 0) ? ea + eb : 0;
    }
    maxE = e[0];
    if (maxE < e[1]) maxE = e[1];
    if (maxE < e[2]) maxE = e[2];
    if (maxE < e[3]) maxE = e[3];
    ++maxE;
    if (e[0] != 0)
        m = hgMul64h(2 * pA->m[0], 2 * pB->m[0]) >> hgMinu32(maxE - e[0], 0x1Fu);
    if (e[1] != 0)
        m += hgMul64h(2 * pA->m[1], 2 * pB->m[1]) >> hgMinu32(maxE - e[1], 0x1Fu);
    if (e[2] != 0)
        m += hgMul64h(2 * pA->m[2], 2 * pB->m[2]) >> hgMinu32(maxE - e[2], 0x1Fu);
    if (e[3] != 0)
        m += hgMul64h(2 * pA->m[3], 2 * pB->m[3]) >> hgMinu32(maxE - e[3], 0x1Fu);
    if (m != 0)
    {
        int clz = hgClz32(hgAbs32(m));
        m = (int)((unsigned int)m << (clz - 1));
        m = ((int)m) >> 1;
        exp = maxE + 1 - (clz - 1 + 126);
        if (exp <= 0)
        {
            exp = 0;
            m = 0;
        }
        if (exp > 0xFE)
        {
            exp = 254;
            m = ((m & 0x80000000) == 0) ? 0x3FFFFFFF : (int)0xC0000001;
        }
    }
    else
    {
        exp = 0;
    }
    return IScalar_set(exp, m);
}

/* --- conversions ---------------------------------------------------------- */

int IFVector4_tov3(_IVector3 *pD, _IFVector4 *pA)
{
    int hasQ = 0, q;
    int e[3], m[3], eMax, i;
    HGint64 p;

    if (!(pD != NULL && pA != NULL))
        imAssert("pD && pA", "IMatrix4.c", 0xC29u);
    for (i = 0; i < 4; ++i)
        if (IScalar_isValid(IFVector4_getf(pA, i)) == 0)
            imAssert(i == 0 ? "IScalar_isValid(IFVector4_getf(pA,0))"
                            : "IScalar_isValid(IFVector4_getf(pA,i))",
                     "IMatrix4.c", 0xC2Au + i);
    q = IFVector4_getf(pA, 3);
    if (IScalar_testne(q) != 0)
    {
        if (IScalar_cmpne(q, IScalar_one()) != 0)
            q = IScalar_rcp(q);
        hasQ = 1;
    }
    else
    {
        q = IScalar_one();
    }
    if (IScalar_getExponent(q) == 0)
    {
        IVector3_zero(pD);
        return hasQ;
    }
    e[0] = (HGuint8)pA->e;
    e[1] = (HGuint8)(pA->e >> 8);
    e[2] = (HGuint8)(pA->e >> 16);
    p = hgLsl64h_0_31(hgMul64(pA->m[0], IScalar_getMantissa(q)), 3);
    m[0] = (int)p;
    p = hgLsl64h_0_31(hgMul64(pA->m[1], IScalar_getMantissa(q)), 3);
    m[1] = (int)p;
    p = hgLsl64h_0_31(hgMul64(pA->m[2], IScalar_getMantissa(q)), 3);
    m[2] = (int)p;
    if ((int)((unsigned int)m[0] + 0x40000000u) <= 0) { m[0] >>= 1; ++e[0]; }
    if ((int)((unsigned int)m[1] + 0x40000000u) <= 0) { m[1] >>= 1; ++e[1]; }
    if ((int)((unsigned int)m[2] + 0x40000000u) <= 0) { m[2] >>= 1; ++e[2]; }
    eMax = e[0];
    if (eMax < e[1]) eMax = e[1];
    if (eMax < e[2]) eMax = e[2];
    if (eMax == 0)
    {
        IVector3_zero(pD);
        return hasQ;
    }
    m[0] >>= hgMinu32(eMax - e[0], 0x1Fu);
    m[1] >>= hgMinu32(eMax - e[1], 0x1Fu);
    m[2] >>= hgMinu32(eMax - e[2], 0x1Fu);
    eMax += IScalar_getExponent(q) - 127;
    if (eMax > 0)
        IVector3_set(pD, eMax, m[0], m[1], m[2]);
    else
        IVector3_zero(pD);
    return hasQ;
}

void IFVector3_tov3(_IVector3 *pD, _IFVector3 *pS)
{
    unsigned int xe = (HGuint8)pS->e;
    unsigned int ye = (HGuint8)(pS->e >> 8);
    unsigned int ze = (HGuint8)(pS->e >> 16);
    unsigned int e = xe;
    if (pD == NULL)
        imAssert("pD", "IMatrix4.c", 0xCC4u);
    if (e < ye)
        e = ye;
    if (e < ze)
        e = ze;
    IVector3_set(pD, e,
                 pS->m[0] >> hgMinu32(e - xe, 0x1Fu),
                 pS->m[1] >> hgMinu32(e - ye, 0x1Fu),
                 pS->m[2] >> hgMinu32(e - ze, 0x1Fu));
}

void IFVector4_accfv(_IFVector4 *pD, _IFVector4 *pS, int *pScale, int n)
{
    int maxE[4] = { 0, 0, 0, 0 };
    int acc[4] = { 0, 0, 0, 0 };
    int out[4], ex[4];
    int i, c;

    if (!(n >= 1 && n <= 4))
        imAssert("n >= 1 && n <= 4", "IMatrix4.c", 2909);
    for (i = 0; i < n; ++i)
    {
        int se = IScalar_getExponent(pScale[i]);
        if (se != 0)
        {
            for (c = 0; c < 4; ++c)
            {
                int e = (pS[i].e >> (8 * c)) & 0xFFu;
                if (e != 0)
                    e += se;
                if (maxE[c] < e)
                    maxE[c] = e;
            }
        }
    }
    for (i = 0; i < n; ++i)
    {
        int se = IScalar_getExponent(pScale[i]);
        if (se != 0)
        {
            int sm = 2 * IScalar_getMantissa(pScale[i]);
            for (c = 0; c < 4; ++c)
            {
                int e = (pS[i].e >> (8 * c)) & 0xFFu;
                if (e != 0)
                {
                    int sh = hgMinu32(maxE[c] - (e + se), 0x1Fu);
                    acc[c] += hgMul64h(pS[i].m[c], sm) >> sh;
                }
            }
        }
    }
    for (c = 0; c < 4; ++c)
    {
        if (acc[c] != 0)
        {
            int clz = hgClz32(hgAbs32(acc[c]));
            int e = maxE[c] - (clz - 1) - 124;
            out[c] = ((int)(acc[c] << (clz - 1))) >> 1;
            if (e > 0)
            {
                if (e > 0xFE)
                {
                    e = 254;
                    out[c] = ((out[c] >> 31) ^ 0x3FFFFFFF) | 1;
                }
            }
            else
            {
                e = 0;
                out[c] = 0;
            }
            ex[c] = e;
        }
        else
        {
            out[c] = 0;
            ex[c] = 0;
        }
    }
    pD->e = ((unsigned int)ex[3] << 24) | ((unsigned int)ex[2] << 16) |
            ((unsigned int)ex[1] << 8) | (unsigned int)ex[0];
    pD->m[0] = out[0];
    pD->m[1] = out[1];
    pD->m[2] = out[2];
    pD->m[3] = out[3];
    for (c = 0; c < 4; ++c)
        if (IScalar_isValid(IFVector4_getf(pD, c)) == 0)
            imAssert(c == 0 ? "IScalar_isValid(IFVector4_getf(pD,0))"
                            : "IScalar_isValid(IFVector4_getf(pD,i))",
                     "IMatrix4.c", 3072 + c);
}

/* --- _IVertexTransform (pre-scaled transform for the vertex pipeline) --- */

void IVertexTransform_copy(_IVertexTransform *pD, _IMatrix4x4 *pM)
{
    unsigned int cl = 0, maxExp = 0;
    int r, c;

    if (!(pD != NULL && pM != NULL))
        imAssert("pD && pM", "IMatrix4.c", 0x46Du);
    for (r = 0; r < 4; ++r)
    {
        unsigned int rowMax = 0, rowE = 0;
        for (c = 0; c < 4; ++c)
        {
            unsigned int e = (pM->v[c].e >> (8 * r)) & 0xFFu;
            int m = pM->v[c].m[r];
            if (rowMax < e)
                rowMax = e;
            rowE |= e << (8 * c);
            pD->v[r].m[c] = 2 * m;
            if (m != 0)
            {
                if (m == 0x20000000 && e == 127)
                    cl |= 2u << (8 * r + 2 * c);
                else if (m == -536870912 && e == 127)
                    cl |= 3u << (8 * r + 2 * c);
                else
                    cl |= 1u << (8 * r + 2 * c);
            }
        }
        pD->v[r].e = rowE;
        if (maxExp < rowMax)
            maxExp = rowMax;
    }
    pD->exp = maxExp + 16;
    pD->cl = cl;
    for (r = 0; r < 4; ++r)
    {
        unsigned int shRow = 0;
        int v7 = hgMin32((int)maxExp - (int)((pD->v[r].e >> 24) & 0xFFu) + 1, 63);
        pD->w[r] = hgAsr64(hgLsl64(hgSet64s(pD->v[r].m[3]), 16), v7);
        for (c = 0; c < 4; ++c)
        {
            int sh = (int)maxExp - (int)((pD->v[r].e >> (8 * c)) & 0xFFu) + 1;
            int m = pD->v[r].m[c];
            if (m == 0)
            {
                sh = 1;
            }
            else if (sh > 0x1F)
            {
                m >>= hgClamp32_0_x(sh - 31, 31);
                sh = 31;
            }
            pD->unnormalized[r][c] = m;
            shRow |= (unsigned int)sh << (8 * c);
        }
        pD->sh[r] = shRow;
    }
}

/* Inlining exposes the implicit W=1 in short-position transforms. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && __ARM_ARCH >= 6
extern inline __attribute__((always_inline))
#endif
void IVertexTransform_mulxv4(_IVector4 *pD, _IVertexTransform *pT, int *pB)
{
    HGint64 acc[4];
    int x = pB[0], y = pB[1], z = pB[2], w = pB[3];

    if (!(pD != NULL && pT != NULL && pB != NULL))
        imAssert("pD && pT && pB", "IMatrix4.c", 0x568u);
    if (pT->cl != 0x80200802u)
    {
        HGuint64 amax;
        int e, sh;
        if ((unsigned int)w == 0x10000u)
        {
            acc[0] = hgAdd64(pT->w[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][0], x), (HGuint8)pT->sh[0]));
            acc[0] = hgAdd64(acc[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][1], y), (HGuint8)(pT->sh[0] >> 8)));
            acc[0] = hgAdd64(acc[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][2], z), (HGuint8)(pT->sh[0] >> 16)));
            acc[1] = hgAdd64(pT->w[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][0], x), (HGuint8)pT->sh[1]));
            acc[1] = hgAdd64(acc[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][1], y), (HGuint8)(pT->sh[1] >> 8)));
            acc[1] = hgAdd64(acc[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][2], z), (HGuint8)(pT->sh[1] >> 16)));
            acc[2] = hgAdd64(pT->w[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][0], x), (HGuint8)pT->sh[2]));
            acc[2] = hgAdd64(acc[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][1], y), (HGuint8)(pT->sh[2] >> 8)));
            acc[2] = hgAdd64(acc[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][2], z), (HGuint8)(pT->sh[2] >> 16)));
            acc[3] = pT->w[3];
            if ((pT->cl & 0x3F000000u) != 0)
                goto add_w_cols;
        }
        else
        {
            acc[0] = imAsr64_1_31(hgMul64(pT->unnormalized[0][0], x), (HGuint8)pT->sh[0]);
            acc[0] = hgAdd64(acc[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][1], y), (HGuint8)(pT->sh[0] >> 8)));
            acc[0] = hgAdd64(acc[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][2], z), (HGuint8)(pT->sh[0] >> 16)));
            acc[0] = hgAdd64(acc[0], imAsr64_1_31(hgMul64(pT->unnormalized[0][3], w), (HGuint8)(pT->sh[0] >> 24)));
            acc[1] = imAsr64_1_31(hgMul64(pT->unnormalized[1][0], x), (HGuint8)pT->sh[1]);
            acc[1] = hgAdd64(acc[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][1], y), (HGuint8)(pT->sh[1] >> 8)));
            acc[1] = hgAdd64(acc[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][2], z), (HGuint8)(pT->sh[1] >> 16)));
            acc[1] = hgAdd64(acc[1], imAsr64_1_31(hgMul64(pT->unnormalized[1][3], w), (HGuint8)(pT->sh[1] >> 24)));
            acc[2] = imAsr64_1_31(hgMul64(pT->unnormalized[2][0], x), (HGuint8)pT->sh[2]);
            acc[2] = hgAdd64(acc[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][1], y), (HGuint8)(pT->sh[2] >> 8)));
            acc[2] = hgAdd64(acc[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][2], z), (HGuint8)(pT->sh[2] >> 16)));
            acc[2] = hgAdd64(acc[2], imAsr64_1_31(hgMul64(pT->unnormalized[2][3], w), (HGuint8)(pT->sh[2] >> 24)));
            acc[3] = imAsr64_1_31(hgMul64(pT->unnormalized[3][3], w), (HGuint8)(pT->sh[3] >> 24));
            if ((pT->cl & 0x3F000000u) != 0)
            {
add_w_cols:
                acc[3] = hgAdd64(acc[3], imAsr64_1_31(hgMul64(pT->unnormalized[3][0], x), (HGuint8)pT->sh[3]));
                acc[3] = hgAdd64(acc[3], imAsr64_1_31(hgMul64(pT->unnormalized[3][1], y), (HGuint8)(pT->sh[3] >> 8)));
                acc[3] = hgAdd64(acc[3], imAsr64_1_31(hgMul64(pT->unnormalized[3][2], z), (HGuint8)(pT->sh[3] >> 16)));
            }
        }
        amax = (HGuint64)hgAbs64(acc[0]) | (HGuint64)hgAbs64(acc[1]) |
               (HGuint64)hgAbs64(acc[2]) | (HGuint64)hgAbs64(acc[3]);
        if (hgIsZero64((HGint64)amax) == 0)
        {
            sh = hgClz64((HGint64)amax) - 1;
            e = (int)pT->exp - sh + 1;
            if (sh >= 32)
            {
                acc[0] = hgLsl64h(acc[0], sh);
                acc[1] = hgLsl64h(acc[1], sh);
                acc[2] = hgLsl64h(acc[2], sh);
                acc[3] = hgLsl64h(acc[3], sh);
            }
            else
            {
                acc[0] = hgLsl64h_0_31(acc[0], sh);
                acc[1] = hgLsl64h_0_31(acc[1], sh);
                acc[2] = hgLsl64h_0_31(acc[2], sh);
                acc[3] = hgLsl64h_0_31(acc[3], sh);
            }
            {
                int m0 = (int)acc[0] >> 1, m1 = (int)acc[1] >> 1;
                int m2 = (int)acc[2] >> 1, m3 = (int)acc[3] >> 1;
                if (m0 == -1073741824 || m1 == -1073741824 ||
                    m2 == -1073741824 || m3 == -1073741824)
                {
                    m0 >>= 1; m1 >>= 1; m2 >>= 1; m3 >>= 1;
                    ++e;
                }
                if (e > 0)
                {
                    IVector4_set(pD, e, m0, m1, m2, m3);
                    return;
                }
            }
        }
        IVector4_zero(pD);
        return;
    }
    else
    {
        /* cl == magic: pass-through transform (mantissas used directly) */
        unsigned int amax = hgAbs32(x) | hgAbs32(y) | hgAbs32(z) | hgAbs32(w);
        if (amax == 0)
        {
            IVector4_zero(pD);
            return;
        }
        {
            int sh = hgClz32(amax) - 2;
            int e = 140 - sh;
            if (sh >= 0)
            {
                x = (int)((unsigned int)x << sh);
                y = (int)((unsigned int)y << sh);
                z = (int)((unsigned int)z << sh);
                w = (int)((unsigned int)w << sh);
            }
            else
            {
                sh = -sh;
                x >>= sh; y >>= sh; z >>= sh; w >>= sh;
            }
            if (x == -1073741824 || y == -1073741824 ||
                z == -1073741824 || w == -1073741824)
            {
                x >>= 1; y >>= 1; z >>= 1; w >>= 1;
                ++e;
            }
            IVector4_set_noINF(pD, e, x, y, z, w);
        }
    }
}

void IVertexTransform_mulxv4s(_IFVector4 *pD, _IVertexTransform *pT, int *pB)
{
    int x = pB[0], y = pB[1], z = pB[2], w = pB[3];
    unsigned int eAcc;
    int r, last;

    if (!(pD != NULL && pT != NULL && pB != NULL))
        imAssert("pD && pT && pB", "IMatrix4.c", 0xAE9u);
    last = (((pT->cl & 0x3F000000u) == 0) && (unsigned int)w == 0x10000u) ? 2 : 3;
    eAcc = (pT->v[3].e >> 24) & 0xFFu;
    pD->m[3] = pT->v[3].m[3] >> 1;
    for (r = last; r >= 0; --r)
    {
        HGuint32 sh = pT->sh[r];
        HGint64 acc;
        int m = 0, e = 0;

        if ((unsigned int)w == 0x10000u)
        {
            acc = pT->w[r];
        }
        else
        {
            acc = imAsr64_1_31(hgMul64(pT->unnormalized[r][3], w), (HGuint8)(sh >> 24));
        }
        acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[r][0], x), (HGuint8)sh));
        acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[r][1], y), (HGuint8)(sh >> 8)));
        acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[r][2], z), (HGuint8)(sh >> 16)));
        if (hgIsZero64(acc) == 0)
        {
            int clz = hgClz64(hgAbs64(acc)) - 1;
            e = (int)pT->exp - clz + 1;
            m = (int)hgLsl64h(acc, clz) >> 1;
            if (m + 0x40000000 <= 0)
            {
                m >>= 1;
                ++e;
            }
            if (e <= 0)
            {
                e = 0;
                m = 0;
            }
            if (e > 0xFE)
            {
                e = 254;
                m = (m >= 0) ? 0x3FFFFFFF : -1073741823;
            }
        }
        eAcc = ((unsigned int)e) | (eAcc << 8);
        pD->m[r] = m;
    }
    pD->e = eAcc;
    for (r = 0; r < 4; ++r)
        if (IScalar_isValid(IFVector4_getf(pD, r)) == 0)
            imAssert(r == 0 ? "IScalar_isValid(IFVector4_getf(pD,0))"
                            : "IScalar_isValid(IFVector4_getf(pD,i))",
                     "IMatrix4.c", 2878 + r);
}

/* --- IVertexTransform z-only multiply (decompile 55696-55791; fragment
 * _helpers_core) - returns only the Z component of the transform ---------- */

int IVertexTransform_zmulfv4(_IVertexTransform *pT, _IFVector4 *pS)
{
    int z;
    HGint64 t;

    if ((pT->cl & 0xFF0000) == 0x200000)
    {
        z = IFVector4_getf(pS, 2);
    }
    else
    {
        t = IFVector4_dp4l_fast(&pT->v[2], pS);
        z = IScalar_setAndNormalize(hgGet64l(t) - 125, hgGet64h(t));
    }
    if (IScalar_isValid(z) == 0)
        imAssert("IScalar_isValid(z)", "IMatrix4.c", 1789);
    return z;
}

int IVertexTransform_zmulxv4(_IVertexTransform *pT, int *pS)
{
    HGint64 acc, a;
    int sh, e, m;

    if ((pT->cl & 0xFF0000) == 0x200000)
        return IScalar_fixedToScalar(pS[2], 16);
    sh = pT->sh[2];
    acc = imAsr64_1_31(hgMul64(pT->unnormalized[2][0], pS[0]), (HGuint8)sh);
    acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[2][1], pS[1]), (HGuint8)(sh >> 8)));
    acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[2][2], pS[2]), (HGuint8)(sh >> 16)));
    if (pS[3] == 0x10000)
        acc = hgAdd64(acc, pT->w[2]);
    else
        acc = hgAdd64(acc, imAsr64_1_31(hgMul64(pT->unnormalized[2][3], pS[3]), (HGuint8)(sh >> 24)));
    a = hgAbs64(acc);
    if (hgIsZero64(a) != 0)
        return IScalar_zero();
    {
        int clz = hgClz64(a) - 1;
        e = pT->exp - clz + 1;
        m = hgLsl64h(acc, clz) >> 1;
        if (m == -1073741824)
        {
            m = -536870912;
            ++e;
        }
        if (e <= 0)
            return IScalar_zero();
        if (e > 0xFE)
        {
            e = 254;
            m = (m >= 0) ? 0x3FFFFFFF : -1073741823;
        }
        return IScalar_set(e, m);
    }
}

/* --- IVertexTransform_mulfv4s (decompile 56718-56881) -----------------------
 * _IFVector4-out variant used by the matrix-palette vertex path: computes
 * up to 4 rows into a float-vector with per-component exponent bytes
 * packed in pD->e (w passthrough when the w row classifies as identity). */

void IVertexTransform_mulfv4s(_IFVector4 *pD, _IVertexTransform *pT, _IFVector4 *pB)
{
    unsigned int eAcc;
    unsigned int pBe;
    int bm[4];
    int eb[4];
    int r;
    int last;

    if (!(pD != NULL && pT != NULL && pB != NULL))
        imAssert("pD && pT && pB", "IMatrix4.c", 2636);
    if (IScalar_isValid(IFVector4_getf(pB, 0)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pB,0))", "IMatrix4.c", 2646);
    if (IScalar_isValid(IFVector4_getf(pB, 1)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pB,1))", "IMatrix4.c", 2647);
    if (IScalar_isValid(IFVector4_getf(pB, 2)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pB,2))", "IMatrix4.c", 2648);
    if (IScalar_isValid(IFVector4_getf(pB, 3)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pB,3))", "IMatrix4.c", 2649);
    pBe = pB->e;
    bm[0] = pB->m[0];
    bm[1] = pB->m[1];
    bm[2] = pB->m[2];
    bm[3] = pB->m[3];
    last = ((pT->cl & 0xFF000000u) == 0x80000000u) ? 2 : 3;
    eAcc = (pBe >> 24) & 0xFFu;          /* seed byte 3 with pB's w exponent */
    pD->m[3] = bm[3];
    for (r = last; r >= 0; --r)
    {
        unsigned int rowE = pT->v[r].e;
        int maxExp;
        int acc = 0;
        int c;

        for (c = 0; c < 4; ++c)
        {
            int e = (int)((rowE >> (8 * c)) & 0xFFu);

            if (e != 0 && (int)((pBe >> (8 * c)) & 0xFFu) != 0)
                e += (int)((pBe >> (8 * c)) & 0xFFu);
            else
                e = 0;
            eb[c] = e;
        }
        maxExp = eb[0];
        for (c = 1; c < 4; ++c)
            if (maxExp < eb[c])
                maxExp = eb[c];
        ++maxExp;
        for (c = 0; c < 4; ++c)
        {
            if (eb[c] != 0)
                acc += hgMul64h(pT->v[r].m[c], bm[c]) >> hgMinu32(maxExp - eb[c], 31);
        }
        if (acc != 0)
        {
            int clz = hgClz32((unsigned int)hgAbs32(acc));

            maxExp = maxExp + 3 - (clz - 1 + 127);
            acc = (int)((unsigned int)acc << (clz - 1)) >> 1;
            if (maxExp <= 0)
            {
                maxExp = 0;
                acc = 0;
            }
            if (maxExp > 0xFE)
            {
                maxExp = 254;
                acc = (acc >= 0) ? 0x3FFFFFFF : -1073741823;
            }
        }
        else
        {
            maxExp = 0;
            acc = 0;
        }
        pD->m[r] = acc;
        eAcc = (unsigned int)maxExp | (eAcc << 8);
    }
    pD->e = eAcc;
    if (IScalar_isValid(IFVector4_getf(pD, 0)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pD,0))", "IMatrix4.c", 2754);
    if (IScalar_isValid(IFVector4_getf(pD, 1)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pD,1))", "IMatrix4.c", 2755);
    if (IScalar_isValid(IFVector4_getf(pD, 2)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pD,2))", "IMatrix4.c", 2756);
    if (IScalar_isValid(IFVector4_getf(pD, 3)) == 0)
        imAssert("IScalar_isValid(IFVector4_getf(pD,3))", "IMatrix4.c", 2757);
}

/* The transform class marks a row containing only +1 on its diagonal.
 * Check the stored row as well: even a zero product with a nonzero row
 * exponent can change the reference dot product's maxE and rounding. */
static inline HGint64 vertexTransformDotRow(_IVertexTransform *t, _IFVector4 *s, int row)
{
    unsigned int shift = 8u * (unsigned int)row;
    _IFVector4 *a = &t->v[row];
    if (((t->cl >> shift) & 0xFFu) == (2u << (2 * row)) && a->e == (127u << shift) &&
        a->m[row] == 0x40000000 &&
        (a->m[(row + 1) & 3] | a->m[(row + 2) & 3] | a->m[(row + 3) & 3]) == 0)
    {
        int m = s->m[row] >> 2; /* high half of 0x40000000 * s->m[row] */
        int e = (int)((s->e >> shift) & 0xFFu);
        return hgSet64(m, (m == 0 || e == 0) ? 0 : e + 127);
    }
    return IFVector4_dp4l_fast(a, s);
}

void IVertexTransform_prepareFloat(_IFVertexTransformPlan *plan, _IVertexTransform *t, unsigned int exponent)
{
    int b0 = exponent & 255u, b1 = (exponent >> 8) & 255u;
    int b2 = (exponent >> 16) & 255u, b3 = exponent >> 24;
    int row;
    plan->inputExponent = exponent;
    for (row = 0; row < 4; ++row)
    {
        unsigned int a = t->v[row].e;
        int e0 = a & 255u, e1 = (a >> 8) & 255u;
        int e2 = (a >> 16) & 255u, e3 = a >> 24;
        int maximum;
        e0 = e0 && b0 ? e0 + b0 : 0;
        e1 = e1 && b1 ? e1 + b1 : 0;
        e2 = e2 && b2 ? e2 + b2 : 0;
        e3 = e3 && b3 ? e3 + b3 : 0;
        maximum = hgMax32(hgMax32(e0, e1), hgMax32(e2, e3));
        plan->shift[row] = (unsigned int)hgMin32(maximum - e0, 31) |
                          ((unsigned int)hgMin32(maximum - e1, 31) << 8) |
                          ((unsigned int)hgMin32(maximum - e2, 31) << 16) |
                          ((unsigned int)hgMin32(maximum - e3, 31) << 24);
        plan->exponent[row] = maximum;
    }
}

static void vertexTransformNormalize(_IVector4 *pD, int *e, int *m)
{
    unsigned int maxE, probe = 0;
    int c;
    maxE = (unsigned int)e[0];
    for (c = 1; c < 4; ++c)
        if (maxE < (unsigned int)e[c])
            maxE = (unsigned int)e[c];
    for (c = 0; c < 4; ++c)
        probe |= (unsigned int)hgAbs32(m[c]) >> hgMin32((int)(maxE - (unsigned int)e[c]), 31);
    if (probe != 0)
    {
        int clz = hgClz32(probe) - 2;
        int newExp = (int)maxE + 2 - (clz + 127);

        if (newExp > 0)
        {
            int sh, mm;

            for (c = 0; c < 4; ++c)
            {
                sh = clz - ((int)maxE - e[c]);
                if (sh >= 0)
                    mm = (int)((unsigned int)m[c] << sh);
                else
                    mm = m[c];
                mm >>= hgClamp32_0_x(-sh, 31);
                m[c] = mm;
            }
            IVector4_set(pD, (unsigned int)newExp, m[0], m[1], m[2], m[3]);
            return;
        }
    }
    IVector4_zero(pD);
}

/* Combine independently aligned dot products into a shared-exponent vector. */
void IVertexTransform_mulfv4(_IVector4 *pD, _IVertexTransform *pT, _IFVector4 *pS)
{
    if (pT->cl == 0x80200802u)
    {
        unsigned int pe = pS->e;
        int eb[4];
        unsigned int maxE;
        int c;

        for (c = 0; c < 4; ++c)
            eb[c] = (int)((pe >> (8 * c)) & 0xFFu);
        maxE = (unsigned int)eb[0];
        for (c = 1; c < 4; ++c)
            if (maxE < (unsigned int)eb[c])
                maxE = (unsigned int)eb[c];
        pD->e = maxE;
        for (c = 0; c < 4; ++c)
            pD->m[c] = pS->m[c] >> hgMin32(maxE - (unsigned int)eb[c], 31);
    }
    else
    {
        HGint64 t;
        int e[4], m[4];

        if ((pT->cl & 0xFF000000u) == 0x80000000u)
        {
            int w;
            t = vertexTransformDotRow(pT, pS, 0);
            e[0] = hgGet64l(t);
            m[0] = hgGet64h(t);
            t = vertexTransformDotRow(pT, pS, 1);
            e[1] = hgGet64l(t);
            m[1] = hgGet64h(t);
            t = vertexTransformDotRow(pT, pS, 2);
            e[2] = hgGet64l(t);
            m[2] = hgGet64h(t);
            w = IFVector4_getf(pS, 3);
            e[3] = IScalar_getExponent(w) + 125;
            m[3] = IScalar_getMantissa(w);
        }
        else
        {
            t = IFVector4_dp4l_fast(&pT->v[0], pS);
            e[0] = hgGet64l(t);
            m[0] = hgGet64h(t);
            t = IFVector4_dp4l_fast(&pT->v[1], pS);
            e[1] = hgGet64l(t);
            m[1] = hgGet64h(t);
            t = IFVector4_dp4l_fast(&pT->v[2], pS);
            e[2] = hgGet64l(t);
            m[2] = hgGet64h(t);
            t = IFVector4_dp4l_fast(&pT->v[3], pS);
            e[3] = hgGet64l(t);
            m[3] = hgGet64h(t);
        }
        vertexTransformNormalize(pD, e, m);
    }
}

void IVertexTransform_mulfv4Prepared(_IVector4 *pD, _IVertexTransform *pT, _IFVector4 *pS,
                                    const _IFVertexTransformPlan *plan)
{
    int e[4], m[4];
    int c;
    int rows = (pT->cl & 0xFF000000u) == 0x80000000u ? 3 : 4;

    if (pT->cl == 0x80200802u)
    {
        IVertexTransform_mulfv4(pD, pT, pS);
        return;
    }
    for (c = 0; c < rows; ++c)
    {
        unsigned int shifts = plan->shift[c];
        _IFVector4 *a = &pT->v[c];
        int v = hgMul64h(a->m[0], pS->m[0]) >> (shifts & 255u);
        v += hgMul64h(a->m[1], pS->m[1]) >> ((shifts >> 8) & 255u);
        v += hgMul64h(a->m[2], pS->m[2]) >> ((shifts >> 16) & 255u);
        v += hgMul64h(a->m[3], pS->m[3]) >> (shifts >> 24);
        m[c] = v;
        e[c] = v ? plan->exponent[c] : 0;
    }
    if (rows == 3)
    {
        int w = IFVector4_getf(pS, 3);
        e[3] = IScalar_getExponent(w) + 125;
        m[3] = IScalar_getMantissa(w);
    }
    vertexTransformNormalize(pD, e, m);
}
void IVertexTransform_prepareCommon(_ICommonVertexTransform *plan, const _IVertexTransform *t)
{
    int row, col, maximum = t->exp - 16;
    plan->exponent = maximum;
    plan->eligible = t->cl != 0x80200802u;
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 4; ++col)
        {
            int e = (t->v[row].e >> (col * 8)) & 255;
            int shift = maximum - e + 1;
            if (t->v[row].m[col] && shift > IM_COMMON_TRANSFORM_SPREAD + 1)
                plan->eligible = 0;
            plan->m[row][col] = t->v[row].m[col] >> hgMin32(shift, 31);
        }
}

/* For positions decoded by IFVector4_setfv, each accumulated unit represents
 * 2^(matrixExponent + inputExponent - 280).
 * Aligning operands and truncating the sum differs from the precise transform
 * by less than eight units per component. With |x/y| <= 4W and W >= 2^17,
 * projected XY error is bounded by 40/(W-8), below 0.049 pixels at width 320.
 * The 32-unit margins preserve canonical and fourfold-guardband clip signs. */
int IVertexTransform_mulfv4Common(_IVector4 *out, const _ICommonVertexTransform *plan, const _IFVector4 *in)
{
    int e0 = in->e & 255, e1 = (in->e >> 8) & 255;
    int e2 = (in->e >> 16) & 255, e3 = in->e >> 24;
    int maximum = hgMax32(hgMax32(e0, e1), hgMax32(e2, e3));
    int x, y, z, w, m[4], row, shift, exponent;
    unsigned int probe;
    if (!plan->eligible ||
        (in->m[0] && maximum - e0 > IM_COMMON_TRANSFORM_SPREAD) ||
        (in->m[1] && maximum - e1 > IM_COMMON_TRANSFORM_SPREAD) ||
        (in->m[2] && maximum - e2 > IM_COMMON_TRANSFORM_SPREAD) ||
        (in->m[3] && maximum - e3 > IM_COMMON_TRANSFORM_SPREAD))
        return 0;
    x = in->m[0] >> hgMin32(maximum - e0, 31);
    y = in->m[1] >> hgMin32(maximum - e1, 31);
    z = in->m[2] >> hgMin32(maximum - e2, 31);
    w = in->m[3] >> hgMin32(maximum - e3, 31);
    for (row = 0; row < 4; ++row)
    {
        const int *q = plan->m[row];
        /* Four products of magnitudes below 2^30 fit in signed 64 bits. */
        HGint64 sum = (HGint64)q[0] * x + (HGint64)q[1] * y +
                      (HGint64)q[2] * z + (HGint64)q[3] * w;
        m[row] = (int)(sum >> 32);
    }
    probe = (unsigned int)(hgAbs32(m[0]) | hgAbs32(m[1]) | hgAbs32(m[2]) | hgAbs32(m[3]));
    if (m[3] < IM_COMMON_TRANSFORM_MIN_W ||
        (hgAbs32(m[0]) >> 2) >= m[3] - IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (hgAbs32(m[1]) >> 2) >= m[3] - IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (unsigned int)(hgAbs32(m[0]) - m[3] + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (unsigned int)(hgAbs32(m[1]) - m[3] + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (unsigned int)(hgAbs32(m[2]) - m[3] + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN)
        return 0;
    shift = hgClz32(probe) - 2;
    exponent = plan->exponent + maximum - 124 - shift;
    if ((unsigned int)(exponent - 2) >= 252)
        return 0;
    out->e = (unsigned int)exponent;
    for (row = 0; row < 4; ++row)
        out->m[row] = (int)((unsigned int)m[row] << shift);
    return 1;
}
