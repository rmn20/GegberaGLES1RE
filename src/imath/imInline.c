/* imInline.c - XScalar fixed point domain + imath inline helpers.
 *
 * Ported from the Gerbera GLES1 driver imInline.inl / imApi.inl (the
 * functions the original kept as inlines; the build emitted them as
 * regular functions).
 *
 * XScalar is 10.21 fixed point: the output domain of XScalar_log2 and the
 * input domain of XScalar_exp2 (see IScalar.c for the pair).
 */
#include "imath/imApi.h"
#include "hg/hgInt32.h"
#include "gerbera/grUtil.h"

/* --- compares / add / sub ------------------------------------------- */

int XScalar_cmplt(int a, int b) { return a < b; }
int XScalar_cmple(int a, int b) { return a <= b; }
int XScalar_cmpne(int a, int b) { return a != b; }
int XScalar_zcmpge(int a) { return a >= 0; }
int XScalar_zcmpgt(int a) { return a > 0; }
int XScalar_zcmplt(int a) { return a < 0; }
int XScalar_zcmpeq(int a) { return a == 0; }
int XScalar_zcmpne(int a) { return a != 0; }
int XScalar_add(int a, int b) { return b + a; }
int XScalar_sub(int a, int b) { return a - b; }
int XScalar_min(int a, int b) { return hgMin32(a, b); }
int XScalar_signBit(int v) { return hgSignBit32(v); }
unsigned short XScalar_tous(int v) { return (unsigned short)hgClamp32_0_x(v >> 5, 0xFFFF); }
int XScalar_toIScalar(int v) { return IScalar_fixedToScalar(v, 21); }

int XScalar_abs(int v)
{
    int t = hgAbs32(v);
    if (t < 0)
        return ~t;
    return t;
}

int XScalar_mul(int a, int b)
{
    return hgLsl64h_0_31(hgMul64(a, b), 11);
}

/* --- saturating multiply (imApi.inl XScalar_mul_sat) ------------------ */

static int imSatMul(int a, int b, int rsh, int unsignedSat)
{
    HGint64 t = hgAsr64_0_31(hgMul64(a, b), rsh);
    int c = (int)hgGet64l(t);
    int h = (int)hgGet64h(t);
    int ok = 1;
    if (h != (c >> 31))
    {
        if (unsignedSat)
        {
            if (h < 0)
                imAssert("h >= 0", "imApi.inl", 1472);
            c = 0x7FFFFFFF;
        }
        else
        {
            c = (h >> 31) ^ 0x7FFFFFFF;
        }
    }
    if ((c >> 31) != ((b ^ a) >> 31))
    {
        if (!(a == 0 && c == 0))
            ok = 0;
    }
    if (ok == 0)
    {
        if (!(b == 0 && c == 0))
            imAssert("((a ^ b)>>31) == (c>>31) || (a == 0 && c == 0) || (b == 0 && c == 0)",
                     "imApi.inl", unsignedSat ? 0x5C5u : 0x63u);
    }
    return c;
}

int XScalar_mul_sat(int a, int b) { return imSatMul(a, b, 21, 0); }
int XScalar_mulu_sat(int a, int b) { return imSatMul(a, b, 21, 1); }

int XScalar_pow(int base, int power)
{
    if (!((base >= 0) || base == (int)0x80000000u))
        imAssert("base >= 0 || base == 0x80000000", "imApi.inl", 0x6Bu);
    return XScalar_exp2(XScalar_mul_sat(XScalar_log2(base), power));
}

/* --- shift helpers ---------------------------------------------------- */

int imLsl32s(int v, int sh)
{
    int shifted;
    if (hgInRange32(sh, 0, 31) == 0)
        imAssert("hgInRange32(sh, 0, 31)", "imApi.inl", 45);
    shifted = (int)((unsigned int)v << sh);
    if (v == (shifted >> sh))
        return shifted;
    return hgSignMask32(v) ^ 0x7FFFFFFF;
}

int imLsl32us(unsigned int v, int sh)
{
    unsigned int shifted = v << sh;
    if (v == (shifted >> sh) && (shifted & 0x80000000u) == 0)
        return (int)shifted;
    return 0x7FFFFFFF;
}

int imMulAsr32Sat(int a, int b, int rsh)
{
    HGint64 t;
    int c, h;
    int ok = 1;
    if (!(rsh >= 0 && rsh < 32))
        imAssert("rsh >= 0 && rsh < 32", "imApi.inl", 0x3Du);
    t = hgAsr64_0_31(hgMul64(a, b), rsh);
    c = (int)hgGet64l(t);
    h = (int)hgGet64h(t);
    if (h != (c >> 31))
        c = (h >> 31) ^ 0x7FFFFFFF;
    if ((c >> 31) != ((b ^ a) >> 31))
    {
        if (!(a == 0 && c == 0))
            ok = 0;
    }
    if (ok == 0)
    {
        if (!(b == 0 && c == 0))
            imAssert("((a ^ b)>>31) == (c>>31) || (a == 0 && c == 0) || (b == 0 && c == 0)",
                     "imApi.inl", 0x49u);
    }
    return c;
}

HGint64 imMulAsr64(int a, int b, unsigned int shift)
{
    if (shift > 0x3Fu)
        imAssert("shift <= 63", "imApi.inl", 0x194u);
    return hgAsr64(hgMul64(a, b), shift);
}

int imSat64(HGint64 a, int rsh)
{
    HGint64 t;
    int c, h;
    if (!(rsh >= 0 && rsh < 32))
        imAssert("rsh >= 0 && rsh < 32", "imApi.inl", 0x141u);
    t = hgAsr64_0_31(a, rsh);
    c = (int)hgGet64l(t);
    h = (int)hgGet64h(t);
    if (h != (c >> 31))
        c = (h >> 31) ^ 0x7FFFFFFF;
    if ((c >> 31) != (hgGet64h(a) >> 31))
        imAssert("(hgGet64h(a)>>31) == (c>>31)", "imApi.inl", 0x14Du);
    return c;
}

unsigned int imClampShift63(unsigned int x) { return hgMinu32((int)x, 0x3F); }

HGint64 imAsr64_1_31(HGint64 a, int sh)
{
    unsigned int l;
    int h;
    if ((HGint8)sh == 0)
        imAssert("(sh&0xff) > 0", "IMatrix4.c", 46);
    l = ((unsigned int)hgGet64l(a) >> sh) | ((unsigned int)hgGet64h(a) << (32 - sh));
    h = (int)hgGet64h(a) >> sh;
    return hgSet64(h, l);
}

HGint64 imDet2_64(int a, int b, int c, int d)
{
    return hgMadd64(hgMul64(a, d), b, -c);
}

int imMulLsl64h(int a, int b, unsigned int shift)
{
    if (shift > 0x3Fu)
        imAssert("shift <= 63", "IMatrix4.c", 0x2ABu);
    return hgLsl64h(hgMul64(a, b), shift);
}

/* --- overflow checking wrappers (imApi.inl) --------------------------- */

int imAdd32OF(int a, int b)
{
    int r = (int)((unsigned int)a + (unsigned int)b);
    HGint64 r64 = hgAdd64(hgSet64s(a), hgSet64s(b));
    if (hgEq64(r64, hgSet64s(r)) == 0)
        imAssert("hgEq64(r64, hgSet64s(r))", "imApi.inl", 0x525u);
    return r;
}

int imSub32OF(int a, int b)
{
    int r = (int)((unsigned int)a - (unsigned int)b);
    HGint64 r64 = hgSub64(hgSet64s(a), hgSet64s(b));
    if (hgEq64(r64, hgSet64s(r)) == 0)
        imAssert("hgEq64(r64, hgSet64s(r))", "imApi.inl", 0x519u);
    return r;
}

int imLsl32OF(int a, unsigned int sh)
{
    HGint64 r64;
    int r;
    if (sh > 0x1Fu)
        imAssert("sh <= 31", "imApi.inl", 0x9E2u);
    r64 = hgLsl64(hgSet64s(a), sh);
    r = (int)((unsigned int)a << sh);
    if (hgEq64(r64, hgSet64s(r)) == 0)
        imAssert("hgEq64(r64, hgSet64s(r))", "imApi.inl", 0x9E6u);
    return r;
}

int imLsl64h_0_31OF(HGint64 a, unsigned int sh)
{
    if (sh > 0x1Fu)
        imAssert("sh <= 31", "imApi.inl", 0x9F0u);
    if (hgEq64(hgAsr64(hgLsl64(a, sh), sh), a) == 0)
        imAssert("hgEq64(hgAsr64(hgLsl64(a, sh), sh), a)", "imApi.inl", 0x9F1u);
    return hgLsl64h_0_31(a, sh);
}

/* --- XScalar_exp2 ------------------------------------------------------ */
/* Shift-and-add exponential using the log2(1 + 2^-i) thresholds below. */

int XScalar_exp2(int x)
{
    unsigned int ty;
    int e = (int)((unsigned int)((HGint64)(HGint32)x >> 21));  /* integer part */
    unsigned int frac = (unsigned int)x << 11;

    ty = 0x80000000u;
    /* Thresholds approximate 2^32 * log2(1 + 2^-i); ty has 31 fractional bits. */
    if (frac >= 0x95C01A38u) { frac -= 2 * 1256197404u; ty += ty >> 1; }
    if (frac >= 0x5269E12Eu) { frac -= 2 * 691335319u;  ty += ty >> 2; }
    if (frac >= 0x2B803472u) { frac -= 2 * 364911161u;  ty += ty >> 3; }
    if (frac >= 0x1663F6FAu) { frac -= 2 * 187825021u;  ty += ty >> 4; }
    if (frac >= 0xB5D69BAu)  { frac -= 2 * 95335645u;   ty += ty >> 5; }
    if (frac >= 0x5B9E5A0u)  { frac -= 2 * 48034512u;   ty += ty >> 6; }
    if (frac >= 0x2DFCA16u)  { frac -= 2 * 24110347u;   ty += ty >> 7; }
    if (frac >= 0x1709C46u)  { frac -= 2 * 12078627u;   ty += ty >> 8; }
    if (frac >= 0xB87C1Eu)   { frac -= 2 * 6045199u;    ty += ty >> 9; }
    if (frac >= 0x5C4994u)   { frac -= 2 * 3024074u;    ty += ty >> 10; }
    if (frac >= 0x2E27ACu)   { frac -= 2 * 1512406u;    ty += ty >> 11; }
    if (frac >= 0x17148Eu)   { frac -= 2 * 756295u;     ty += ty >> 12; }
    if (frac >= 0xB8A74u)    { frac -= 2 * 378170u;     ty += ty >> 13; }
    if (frac >= 0x5C546u)    { frac -= 2 * 189091u;     ty += ty >> 14; }
    if (frac >= 0x2E2A6u)    { frac -= 2 * 94547u;      ty += ty >> 15; }
    if (frac >= 0x17152u)    { frac -= 2 * 47273u;      ty += (ty >> 16) & 0xFFFFu; /* HIWORD */ }
    if (frac >= 0xB8AAu)     { frac -= 2 * 23637u;      ty += ty >> 17; }

    e -= 10;
    if (ty < 0x80000000u)
        imAssert("ty >= 0x80000000u", "IScalar.c", 987);
    if ((e & 0x80000000u) != 0)
        return (int)(ty >> hgMin32(-e, 31));
    return imLsl32us(ty, hgMin32(e, 31));
}

/* --- imRcpu (imInline.inl) --------------------------------------------- */
/* reciprocal estimate: (x << sh) normalized, table seed + 2 NR steps.
 * result: high 32 bits = reciprocal mantissa, low 32 bits = shift. */

const HGint8 imIntrcpLUT[310] = {
    0xFF, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5,
    0xF4, 0xF3, 0xF2, 0xF1, 0xF0, 0xF0, 0xEF, 0xEE, 0xED, 0xEC, 0xEB, 0xEA,
    0xEA, 0xE9, 0xE8, 0xE7, 0xE6, 0xE5, 0xE5, 0xE4, 0xE3, 0xE2, 0xE1, 0xE1,
    0xE0, 0xDF, 0xDE, 0xDE, 0xDD, 0xDC, 0xDB, 0xDB, 0xDA, 0xD9, 0xD9, 0xD8,
    0xD7, 0xD6, 0xD6, 0xD5, 0xD4, 0xD4, 0xD3, 0xD2, 0xD2, 0xD1, 0xD0, 0xD0,
    0xCF, 0xCE, 0xCE, 0xCD, 0xCC, 0xCC, 0xCB, 0xCA, 0xCA, 0xC9, 0xC9, 0xC8,
    0xC7, 0xC7, 0xC6, 0xC5, 0xC5, 0xC4, 0xC4, 0xC3, 0xC3, 0xC2, 0xC1, 0xC1,
    0xC0, 0xC0, 0xBF, 0xBF, 0xBE, 0xBD, 0xBD, 0xBC, 0xBC, 0xBB, 0xBB, 0xBA,
    0xBA, 0xB9, 0xB9, 0xB8, 0xB8, 0xB7, 0xB7, 0xB6, 0xB6, 0xB5, 0xB5, 0xB4,
    0xB4, 0xB3, 0xB3, 0xB2, 0xB2, 0xB1, 0xB1, 0xB0, 0xB0, 0xAF, 0xAF, 0xAE,
    0xAE, 0xAD, 0xAD, 0xAC, 0xAC, 0xAC, 0xAB, 0xAB, 0xAA, 0xAA, 0xA9, 0xA9,
    0xA8, 0xA8, 0xA8, 0xA7, 0xA7, 0xA6, 0xA6, 0xA5, 0xA5, 0xA5, 0xA4, 0xA4,
    0xA3, 0xA3, 0xA3, 0xA2, 0xA2, 0xA1, 0xA1, 0xA1, 0xA0, 0xA0, 0x9F, 0x9F,
    0x9F, 0x9E, 0x9E, 0x9D, 0x9D, 0x9D, 0x9C, 0x9C, 0x9C, 0x9B, 0x9B, 0x9A,
    0x9A, 0x9A, 0x99, 0x99, 0x99, 0x98, 0x98, 0x98, 0x97, 0x97, 0x97, 0x96,
    0x96, 0x95, 0x95, 0x95, 0x94, 0x94, 0x94, 0x93, 0x93, 0x93, 0x92, 0x92,
    0x92, 0x91, 0x91, 0x91, 0x90, 0x90, 0x90, 0x90, 0x8F, 0x8F, 0x8F, 0x8E,
    0x8E, 0x8E, 0x8D, 0x8D, 0x8D, 0x8C, 0x8C, 0x8C, 0x8C, 0x8B, 0x8B, 0x8B,
    0x8A, 0x8A, 0x8A, 0x89, 0x89, 0x89, 0x89, 0x88, 0x88, 0x88, 0x87, 0x87,
    0x87, 0x87, 0x86, 0x86, 0x86, 0x86, 0x85, 0x85, 0x85, 0x84, 0x84, 0x84,
    0x84, 0x83, 0x83, 0x83, 0x83, 0x82, 0x82, 0x82, 0x82, 0x81, 0x81, 0x81,
    0x81, 0x80, 0x80, 0x80, 42, 112, 32, 61, 61, 32, 40, 72,
    71, 117, 105, 110, 116, 56, 41, 48, 120, 49, 50, 117,
    32, 124, 124, 32, 42, 112, 32, 61, 61, 32, 40, 72,
    71, 117, 105, 110, 116, 56, 41, 48, 120, 55, 56, 117,
    0, 104, 103, 68, 101, 102, 115, 46, 104, 0

};

HGint64 imRcpu(unsigned int x)
{
    return imRcpu_inline(x);
}

/* --- XColor3 ----------------------------------------------------------- */

void XColor3_zero(_XColor3 *dst)
{
    dst->r = 0;
    dst->g = 0;
    dst->b = 0;
}

void XColor3_setx(_XColor3 *color, int r, int g, int b)
{
    if (color == NULL)
        imAssert("color", "imApi.inl", 1163);
    color->r = 32 * hgClamp32(r, -67108864, 0x3FFFFFF);
    color->g = 32 * hgClamp32(g, -67108864, 0x3FFFFFF);
    color->b = 32 * hgClamp32(b, -67108864, 0x3FFFFFF);
}

void XColor3_setub(_XColor3 *color, HGuint8 r, HGuint8 g, HGuint8 b)
{
    if (color == NULL)
        imAssert("color", "imApi.inl", 1171);
    color->r = (r << 13) | (32 * r);
    color->g = (g << 13) | (32 * g);
    color->b = (b << 13) | (32 * b);
}

void XColor3_setf(_XColor3 *color, int r, int g, int b)
{
    if (color == NULL)
        imAssert("color", "imApi.inl", 1179);
    color->r = IScalar_scalarToFixed(IScalar_iftos(r), 21);
    color->g = IScalar_scalarToFixed(IScalar_iftos(g), 21);
    color->b = IScalar_scalarToFixed(IScalar_iftos(b), 21);
}

void XColor3_mul(_XColor3 *dst, _XColor3 *src0, _XColor3 *src1)
{
    dst->r = XScalar_mul_sat(src0->r, src1->r);
    dst->g = XScalar_mul_sat(src0->g, src1->g);
    dst->b = XScalar_mul_sat(src0->b, src1->b);
}

void XColor3_add(_XColor3 *dst, _XColor3 *src0, _XColor3 *src1)
{
    dst->r = hgAdds32(src0->r, src1->r);
    dst->g = hgAdds32(src0->g, src1->g);
    dst->b = hgAdds32(src0->b, src1->b);
}

int XColor3_isZero(_XColor3 *src)
{
    if (src == NULL)
        imAssert("src", "imApi.inl", 0x610u);
    if (src->r == 0 && src->g == 0 && src->b == 0)
        return 1;
    return 0;
}

void XColor3_tousv(unsigned short *pD, _XColor3 *src)
{
    pD[0] = (unsigned short)hgClamp32_0_x(src->r >> 5, 0xFFFF);
    pD[1] = (unsigned short)hgClamp32_0_x(src->g >> 5, 0xFFFF);
    pD[2] = (unsigned short)hgClamp32_0_x(src->b >> 5, 0xFFFF);
}

/* --- XColor3Acc (64-bit accumulators) ----------------------------------- */

void XColor3Acc_set(HGint64 *d, _XColor3 *a)
{
    d[0] = hgAsr64_0_31(hgSet64x(a->r), 11);
    d[1] = hgAsr64_0_31(hgSet64x(a->g), 11);
    d[2] = hgAsr64_0_31(hgSet64x(a->b), 11);
}

void XColor3Acc_setZero(HGint64 *d)
{
    d[0] = hgZero64();
    d[1] = hgZero64();
    d[2] = hgZero64();
}

void XColor3Acc_muls(HGint64 *d, _XColor3 *a, int b)
{
    d[0] = hgMul64(a->r, b);
    d[1] = hgMul64(a->g, b);
    d[2] = hgMul64(a->b, b);
}

void XColor3Acc_scaleColor(HGint64 *d, HGint64 *a, _XColor3 *b)
{
    d[0] = hgMul64(imSat64(a[0], 21), b->r);
    d[1] = hgMul64(imSat64(a[1], 21), b->g);
    d[2] = hgMul64(imSat64(a[2], 21), b->b);
}

void XColor3Acc_addAcc(HGint64 *d, HGint64 *s0, HGint64 *s1)
{
    d[0] = hgAdd64(s0[0], s1[0]);
    d[1] = hgAdd64(s0[1], s1[1]);
    d[2] = hgAdd64(s0[2], s1[2]);
}

void XColor3Acc_addColor(HGint64 *d, HGint64 *s0, _XColor3 *s1)
{
    d[0] = hgAdd64(s0[0], hgAsr64_0_31(hgSet64x(s1->r), 11));
    d[1] = hgAdd64(s0[1], hgAsr64_0_31(hgSet64x(s1->g), 11));
    d[2] = hgAdd64(s0[2], hgAsr64_0_31(hgSet64x(s1->b), 11));
}

void XColor3Acc_madd(HGint64 *d, HGint64 *s0, _XColor3 *s1, int s2)
{
    d[0] = hgMadd64(s0[0], s1->r, s2);
    d[1] = hgMadd64(s0[1], s1->g, s2);
    d[2] = hgMadd64(s0[2], s1->b, s2);
}

void XColor3Acc_toRGB(unsigned short *pD, HGint64 *rgb)
{
    pD[0] = (unsigned short)hgClamp32_0_x((int)hgGet64h(rgb[0]) >> 2, 255) << 8;
    pD[1] = (unsigned short)hgClamp32_0_x((int)hgGet64h(rgb[1]) >> 2, 255) << 8;
    pD[2] = (unsigned short)hgClamp32_0_x((int)hgGet64h(rgb[2]) >> 2, 255) << 8;
}

/* --- IFVector3 ---------------------------------------------------------- */

void IFVector3_init100(_IFVector3 *pD)
{
    pD->e = 127;
    pD->m[0] = 0x20000000;
    pD->m[1] = 0;
    pD->m[2] = 0;
}

void IFVector3_setf(_IFVector3 *pD, int i, int x)
{
    if (!(pD != NULL && IScalar_isValid(x) != 0 && i >= 0 && i <= 2))
        imAssert("pD && IScalar_isValid(x) && i >= 0 && i <= 2", "imApi.inl", 300);
    pD->e = (pD->e & ~(255u << (8 * i))) | (IScalar_getExponent(x) << (8 * i));
    pD->m[i] = IScalar_getMantissa(x);
}

int IFVector3_getf(_IFVector3 *pS, int n)
{
    if (pS == NULL)
        imAssert("pS", "imApi.inl", 0x2B5u);
    if (!(n >= 0 && n < 3))
        imAssert("n >= 0 && n < 3", "imApi.inl", 0x2B6u);
    return IScalar_set((pS->e >> (8 * n)) & 0xFFu, pS->m[n]);
}

void IFVector3_copy(_IFVector3 *pD, _IFVector3 *pS)
{
    if (!(pD != NULL && pS != NULL))
        imAssert("pD && pS", "imApi.inl", 0x9B0u);
    *pD = *pS;
}

int IFVector3_is100(_IFVector3 *pS)
{
    return pS->e == 127 && pS->m[0] == 0x20000000 && pS->m[1] == 0 && pS->m[2] == 0;
}

int IFVector4_getFirstNonZeroSign(_IFVector4 *v)
{
    int result = hgSign32(v->m[0]);
    if (result == 0)
    {
        result = hgSign32(v->m[1]);
        if (result == 0)
        {
            result = hgSign32(v->m[2]);
            if (result == 0)
            {
                result = hgSign32(v->m[3]);
                if (result == 0)
                    return 0;
            }
        }
    }
    return result;
}

void IFVector3_setfv3(_IFVector3 *pD, int *pS)
{
    int i;
    if (pD == NULL)
        imAssert("pD", "imApi.inl", 2464);
    if (pS == NULL)
        imAssert("pS", "imApi.inl", 2465);
    if (IScalar_isValid(pS[0]) == 0)
        imAssert("IScalar_isValid(pS[0])", "imApi.inl", 2466);
    if (IScalar_isValid(pS[1]) == 0)
        imAssert("IScalar_isValid(pS[1])", "imApi.inl", 2467);
    if (IScalar_isValid(pS[2]) == 0)
        imAssert("IScalar_isValid(pS[2])", "imApi.inl", 2468);
    pD->e = 0;
    for (i = 0; i < 3; ++i)
        IFVector3_setf(pD, i, pS[i]);
}
