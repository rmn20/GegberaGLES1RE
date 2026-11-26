/* grTransform.c - texture-coordinate transform helpers (port of the
 * _helpers_core.c fragment ITCTransform_*; the original TU is unknown -
 * the functions are shared by grSetupCulling, grUpdateVertexPipeline and
 * grTransformTexCoord). The _ITCTransform record layout is documented in
 * grState.h; the fragment has no assert sites. */
#include "gerbera/grState.h"

/* The original multiplies high, shifts the 32-bit word left, then uses SAR. */
static int texCoordProduct(int a, int b, unsigned int lsh, unsigned int rsh)
{
    return (int)((unsigned int)hgMul64h(a, b) << lsh) >> rsh;
}

void ITCTransform_copy(_ITCTransform *pD, _IMatrix4x4 *pM)
{
    unsigned int cl = 0;
    int e;
    int ci;
    int ri;
    int ria;

    for (ci = 3; ci >= 0; --ci)
    {
        pD->e[ci] = pM->v[ci].e;
        for (ri = 3; ri >= 0; --ri)
        {
            /* per-row exponent byte minus 119 (the "row 3 at 16.16" bias) */
            e = (int)(unsigned char)(pM->v[ci].e >> (8 * ri)) - 119;
            pD->lsh[ci][ri] = (unsigned char)hgClamp32_0_x(e, 31);
            pD->rsh[ci][ri] = (unsigned char)hgClamp32_0_x(-e, 31);
            pD->m[ci][ri] = pM->v[ci].m[ri];
            if (pD->m[ci][ri] != 0)
            {
                if (pD->m[ci][ri] == 0x20000000 && e == 8)
                    cl |= 2u << (8 * ci + 2 * ri);
                else if (pD->m[ci][ri] == -536870912 && e == 8)
                    cl |= 3u << (8 * ci + 2 * ri);
                else
                    cl |= 1u << (8 * ci + 2 * ri);
            }
        }
    }
    for (ria = 3; ria >= 0; --ria)
        pD->im[ria] = imMulLsl64h(pD->m[3][ria], 0x10000, pD->lsh[3][ria]) >> pD->rsh[3][ria];
    pD->cl = cl;
}

/* --- grTexCoordDivByQ (decompile 54746-54783) -------------------------------
 * Divides (x,y) by the 2^21-scale q (coords[3]) without changing their scale.
 * q == 0x200000 (1.0 in 10.21) or 0 passes through. */

void grTexCoordDivByQ(int *output, int *coords)
{
    int q = coords[3];

    if (q == 0x200000 || q == 0)
    {
        *output = *coords;
        output[1] = coords[1];
    }
    else
    {
        unsigned int absq = (unsigned int)hgAbs32(q);
        HGint64 rr = imRcpu(absq);
        int rcp = (int)(rr >> 32);          /* high: reciprocal mantissa */
        int shift = (int)(rr & 0xFFFFFFFFu); /* low: shift */
        int sh = shift - 9;
        int r = rcp * hgSign32(q);

        if (shift - 9 <= 0)
        {
            *output = hgMul64h(*coords, r) >> (9 - shift);
            output[1] = hgMul64h(coords[1], r) >> (9 - shift);
        }
        else
        {
            *output = (int)hgLsl64h_0_31(hgMul64(*coords, r), sh);
            output[1] = (int)hgLsl64h_0_31(hgMul64(coords[1], r), sh);
        }
    }
}

/* --- ITCTransform_mulxv2 (decompile 54784-54847) ----------------------------
 * 16.16 (s,t) with implicit q=1 through the record. Cross-axis or projective
 * terms require the full 2x2+q product; otherwise each axis is transformed
 * independently. */

void ITCTransform_mulxv2(int *pO, _ITCTransform *pT, int *st)
{
    int m[9];

    if ((pT->cl & 0xC3CC) != 0)
    {
        m[0] = pT->im[0];
        m[0] += texCoordProduct(pT->m[0][0], st[0], pT->lsh[0][0], pT->rsh[0][0]);
        m[0] += texCoordProduct(pT->m[1][0], st[1], pT->lsh[1][0], pT->rsh[1][0]);
        m[5] = m[0];
        m[0] = pT->im[1];
        m[0] += texCoordProduct(pT->m[0][1], st[0], pT->lsh[0][1], pT->rsh[0][1]);
        m[0] += texCoordProduct(pT->m[1][1], st[1], pT->lsh[1][1], pT->rsh[1][1]);
        m[6] = m[0];
        m[0] = pT->im[3];
        m[0] += texCoordProduct(pT->m[0][3], st[0], pT->lsh[0][3], pT->rsh[0][3]);
        m[0] += texCoordProduct(pT->m[1][3], st[1], pT->lsh[1][3], pT->rsh[1][3]);
        m[8] = m[0];
    }
    else
    {
        m[4] = st[0];
        m[2] = pT->im[0];
        if ((pT->cl & 3) == 2)
            m[4] = (int)((unsigned int)m[2] + 32u * (unsigned int)m[4]);
        else
            m[4] = m[2] + texCoordProduct(pT->m[0][0], m[4], pT->lsh[0][0], pT->rsh[0][0]);
        m[5] = m[4];
        m[3] = st[1];
        m[1] = pT->im[1];
        if ((pT->cl & 0xC00) == 0x800)
            m[3] = (int)((unsigned int)m[1] + 32u * (unsigned int)m[3]);
        else
            m[3] = m[1] + texCoordProduct(pT->m[1][1], m[3], pT->lsh[1][1], pT->rsh[1][1]);
        m[6] = m[3];
        m[8] = pT->im[3];
    }
    grTexCoordDivByQ(pO, &m[5]);
}

/* --- ITCTransform_mulxv4 (decompile 54850-54901) ----------------------------
 * 16.16 (s,t,r,q) through the record. */

void ITCTransform_mulxv4(int *pO, _ITCTransform *pT, int *strq)
{
    int m[10];

    if (!(pT != NULL && strq != NULL))
        imAssert("pD && pT && strq", "IMatrix4.c", 0x351u);
    if (strq == pO)
        imAssert("strq != pD", "IMatrix4.c", 0x352u);
    if ((pT->cl & 0xCFCFC3CCu) == 0x80000000u)
    {
        m[4] = strq[0];
        if ((pT->cl & 3) == 2)
            m[4] = (int)((unsigned int)m[4] * 32u);
        else
            m[4] = texCoordProduct(pT->m[0][0], m[4], pT->lsh[0][0], pT->rsh[0][0]);
        m[5] = m[4];
        m[3] = strq[1];
        if ((pT->cl & 0xC00) == 0x800)
            m[3] = (int)((unsigned int)m[3] * 32u);
        else
            m[3] = texCoordProduct(pT->m[1][1], m[3], pT->lsh[1][1], pT->rsh[1][1]);
        m[6] = m[3];
        m[8] = (int)(32u * (unsigned int)strq[3]);
    }
    else
    {
        int j, ci;

        for (j = 3; j >= 0; --j)
        {
            int acc = 0;

            if (j != 2)
            {
                for (ci = 0; ci < 4; ++ci)
                    acc += texCoordProduct(pT->m[ci][j], strq[ci], pT->lsh[ci][j], pT->rsh[ci][j]);
                m[j + 5] = acc;
            }
        }
    }
    grTexCoordDivByQ(pO, &m[5]);
}

/* --- ITCTransform_mulxv (decompile 54903-54938; asserts IMatrix4.c 916..919) */

void ITCTransform_mulxv(int *pD, _ITCTransform *pT, int *strq, int n)
{
    int str1[4];

    if (pD == NULL)
        imAssert("pD", "IMatrix4.c", 916);
    if (pT == NULL)
        imAssert("pT", "IMatrix4.c", 917);
    if (strq == NULL)
        imAssert("strq", "IMatrix4.c", 918);
    if (!(n >= 2 && n <= 4))
        imAssert("n >= 2 && n <= 4", "IMatrix4.c", 919);
    if (n == 2)
    {
        ITCTransform_mulxv2(pD, pT, strq);
    }
    else
    {
        int *ptc = strq;

        if (n == 3)
        {
            str1[0] = strq[0];
            str1[1] = strq[1];
            str1[2] = strq[2];
            str1[3] = 0x10000;
            ptc = str1;
        }
        ITCTransform_mulxv4(pD, pT, ptc);
    }
}

/* --- ITCTransform_mulfv2 (decompile 54942-55000) ----------------------------
 * IScalar (s,t) with implicit q=1; out components in 2^21 scale (10.21). */

void ITCTransform_mulfv2(int *pO, _ITCTransform *pT, int *st)
{
    int e[15];

    if ((pT->cl & 0xC3CC) != 0)
    {
        int j;

        for (j = 3; j >= 0; --j)
        {
            int acc = pT->im[j];

            if (j != 2)
            {
                int ci;

                for (ci = 1; ci >= 0; --ci)
                {
                    int eb = (unsigned char)(pT->e[ci] >> (8 * j));
                    int ex = eb + IScalar_getExponent(st[ci]) - 259;
                    int lsh = hgClamp32_0_x(ex, 31);
                    int rsh = hgClamp32_0_x(-ex, 31);

                    acc += texCoordProduct(pT->m[ci][j], IScalar_getMantissa(st[ci]), lsh, rsh);
                }
                e[j + 11] = acc;
            }
        }
    }
    else
    {
        int j;

        for (j = 1; j >= 0; --j)
        {
            int mant = IScalar_getMantissa(st[j]);
            int eb = (unsigned char)(pT->e[j] >> (8 * j));
            int ex = eb + IScalar_getExponent(st[j]) - 259;
            int lsh = hgClamp32_0_x(ex, 31);
            int rsh = hgClamp32_0_x(-ex, 31);

            e[j + 11] = pT->im[j] + texCoordProduct(pT->m[j][j], mant, lsh, rsh);
        }
        e[14] = pT->im[3];
    }
    grTexCoordDivByQ(pO, &e[11]);
}

/* --- ITCTransform_mulfv4 (decompile 55003-55062) ----------------------------
 * IScalar (s,t,r,q) through the record; z output is never produced. */

void ITCTransform_mulfv4(int *pO, _ITCTransform *pT, int *strq)
{
    int e[15];

    if ((pT->cl & 0xFCFC3CCu) != 0)
    {
        int j;

        for (j = 3; j >= 0; --j)
        {
            int acc;

            if (j == 2)
                continue;
            acc = 0;
            {
                int ci;

                for (ci = 3; ci >= 0; --ci)
                {
                    int eb = (unsigned char)(pT->e[ci] >> (8 * j));
                    int ex = eb + IScalar_getExponent(strq[ci]) - 259;
                    int lsh = hgClamp32_0_x(ex, 31);
                    int rsh = hgClamp32_0_x(-ex, 31);

                    acc += texCoordProduct(pT->m[ci][j], IScalar_getMantissa(strq[ci]), lsh, rsh);
                }
            }
            e[j + 11] = acc;
        }
    }
    else
    {
        int j;

        for (j = 3; j >= 0; --j)
        {
            if (j == 2)
                continue;
            {
                int mant = IScalar_getMantissa(strq[j]);
                int eb = (unsigned char)(pT->e[j] >> (8 * j));
                int lsh;
                int rsh;

                if (j != 2)
                {
                    int ex = eb + IScalar_getExponent(strq[j]) - 259;

                    lsh = hgClamp32_0_x(ex, 31);
                    rsh = hgClamp32_0_x(-ex, 31);
                    e[j + 11] = texCoordProduct(pT->m[j][j], mant, lsh, rsh);
                }
            }
        }
    }
    grTexCoordDivByQ(pO, &e[11]);
}

/* --- ITCTransform_mulfv (decompile 55065-55097; asserts IMatrix4.c
 * 1101..1105) - converts float components to IScalars first ------------- */

/* The unit coefficient's high product is mantissa >> 3. Keep the
 * clamped shifts so large coordinates retain the transform's wrapping. */
static int texCoordUnitFloat(int value)
{
    return grTexCoordUnitFloatFast(value);
}

void ITCTransform_mulfv(int *pO, _ITCTransform *pT, int *strq, int n)
{
    int str1[4];

    if (pO == NULL)
        imAssert("pO", "IMatrix4.c", 1102);
    if (pT == NULL)
        imAssert("pT", "IMatrix4.c", 1103);
    if (strq == NULL)
        imAssert("strq", "IMatrix4.c", 1104);
    if (!(n >= 2 && n <= 4))
        imAssert("n >= 2 && n <= 4", "IMatrix4.c", 1105);
    if (n == 2 && (unsigned int)pT->cl == 0x80200802u)
    {
        int s = texCoordUnitFloat(strq[0]);
        int t = texCoordUnitFloat(strq[1]);
        pO[0] = s;
        pO[1] = t;
        return;
    }
    str1[0] = IScalar_iftos(strq[0]);
    str1[1] = IScalar_iftos(strq[1]);
    if (n == 2)
    {
        ITCTransform_mulfv2(pO, pT, str1);
    }
    else
    {
        str1[2] = IScalar_iftos(strq[2]);
        if (n == 3)
            str1[3] = IScalar_one();
        else
            str1[3] = IScalar_iftos(strq[3]);
        ITCTransform_mulfv4(pO, pT, str1);
    }
}
