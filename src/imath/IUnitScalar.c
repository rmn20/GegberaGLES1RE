/* IUnitScalar.c - unit scalar operations (29 fractional bits, [-1, 1]).
 *
 * IUnitScalar_sin/cos are defined in hg/hgFloat.c - the reference binary
 * asserts into hgFloat.c from those functions, pinning them there.
 */
#include "imath/imApi.h"
#include "hg/hgInt32.h"

HGint32 IUnitScalar_maxZero(HGint32 x)
{
    return hgMaxZero32(x);
}

HGint32 IUnitScalar_lerp(HGint32 s0, HGint32 s1, HGint32 factor)
{
    HGint64 v;
    HGint32 result;
    if (hgInRange32(factor, 0, IUNITSCALAR_ONE) == 0)
        imAssert("hgInRange32(factor, 0, IUNITSCALAR_ONE)", "IUnitScalar.c", 36);
    if (hgInRange32(s0, 0, IUNITSCALAR_ONE) == 0)
        imAssert("hgInRange32(s0, 0, IUNITSCALAR_ONE)", "IUnitScalar.c", 37);
    if (hgInRange32(s1, 0, IUNITSCALAR_ONE) == 0)
        imAssert("hgInRange32(s1, 0, IUNITSCALAR_ONE)", "IUnitScalar.c", 38);
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    (void)v;
    {
        unsigned int lo;
        int hi;
        int oneMinus = IUNITSCALAR_ONE - factor;
        __asm__("smull %0, %1, %2, %3\n\t"
                "smlal %0, %1, %4, %5"
                : "=&r"(lo), "=&r"(hi)
                : "r"(oneMinus), "r"(s0), "r"(factor), "r"(s1));
        result = (int)(((unsigned int)hi << 3) | (lo >> 29));
    }
#else
    v = hgMul64(IUNITSCALAR_ONE - factor, s0);
    v = hgMadd64(v, factor, s1);
    result = hgLsl64h_0_31(v, 3);
#endif
    if (hgInRange32(result, 0, IUNITSCALAR_ONE) == 0)
        imAssert("hgInRange32(result, 0, IUNITSCALAR_ONE)", "IUnitScalar.c", 42);
    return result;
}

HGint32 IUnitScalar_tos(HGint32 x)
{
    /* Normalize the fixed-point value into an IScalar mantissa/exponent. */
    HGint32 a = hgAbs32(x);
    HGint32 sh = hgClz32((HGuint32)a) - 2;
    HGuint32 e;
    if (IUnitScalar_isValid(x) == 0)
        imAssert("IUnitScalar_isValid(x)", "IUnitScalar.c", 62);
    if (x != 0)
        e = (HGuint32)(127 - sh);
    else
        e = 0;
    return IScalar_set(e, (HGint32)((HGuint32)x << sh));
}
