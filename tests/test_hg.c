/* Integer, float-bit-pattern and memory operations in the hg support library. */
#include "hg/hgInt32.h"
#include "hg/hgFloat.h"
#include "hg/hgFill.h"
#include "hg/hgMemory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_support.h"

#define CHECK_EQ CHECK_I64_EQ

static void test_clz(void)
{
    CHECK_EQ(hgClz32(0u), 32);
    CHECK_EQ(hgClz32(1u), 31);
    CHECK_EQ(hgClz32(2u), 30);
    CHECK_EQ(hgClz32(3u), 30);
    CHECK_EQ(hgClz32(0x00F00000u), 8);
    CHECK_EQ(hgClz32(0x40000000u), 1);
    CHECK_EQ(hgClz32(0xFFFFFFFFu), 0);
    CHECK_EQ(hgClz64(0), 64);
    CHECK_EQ(hgClz64((HGint64)((HGuint64)1 << 63)), 0);
    CHECK_EQ(hgClz64(((HGint64)1) << 32), 31);
    CHECK_EQ(hgClz64(1), 63);
}

static void test_range_minmax(void)
{
    CHECK_EQ(hgInRange32(5, 1, 10), 1);
    CHECK_EQ(hgInRange32(1, 1, 10), 1);
    CHECK_EQ(hgInRange32(10, 1, 10), 1);
    CHECK_EQ(hgInRange32(0, 1, 10), 0);
    CHECK_EQ(hgInRange32(11, 1, 10), 0);
    CHECK_EQ(hgInRange32(-5, -10, -1), 1);
    CHECK_EQ(hgInRange32(-11, -10, -1), 0);
    CHECK_EQ(hgInRange32(-1, -10, -1), 1);
    CHECK_EQ(hgInRange32(0, -0x7FFFFFFF - 1, 0x7FFFFFFF), 1);
    CHECK_EQ(hgInRange32(0x7FFFFFFF, -0x7FFFFFFF - 1, -1), 0);

    CHECK_EQ(hgMin32(3, 7), 3);
    CHECK_EQ(hgMin32(-3, 7), -3);
    CHECK_EQ(hgMax32(3, 7), 7);
    CHECK_EQ(hgMinu32(3u, 7u), 3u);
    CHECK_EQ(hgMaxu32(3u, 7u), 7u);
    CHECK_EQ(hgAbs32(-9), 9);
    CHECK_EQ(hgAbs32(9), 9);
    CHECK_EQ(hgAbs32(-0x7FFFFFFF - 1), -0x7FFFFFFF - 1);
    CHECK_EQ(hgSign32(5), 1);
    CHECK_EQ(hgSign32(0), 0);
    CHECK_EQ(hgSign32(-5), -1);
    CHECK_EQ(hgSignMask32(-1), -1);
    CHECK_EQ(hgSignMask32(42), 0);
    CHECK_EQ(hgSignBit32(-1), 1);
    CHECK_EQ(hgSignBit32(42), 0);
    CHECK_EQ(hgMaxZero32(7), 7);
    CHECK_EQ(hgMaxZero32(-7), 0);
    CHECK_EQ(hgDiff32(10, 3), 7);
    CHECK_EQ(hgDiff32(3, 10), 7);
    CHECK_EQ(hgDiff32(0x7FFFFFFF, -0x7FFFFFFF - 1), 1);
}

static void test_clamp_misc(void)
{
    CHECK_EQ(hgClamp32(5, 0, 10), 5);
    CHECK_EQ(hgClamp32(-5, 0, 10), 0);
    CHECK_EQ(hgClamp32(15, 0, 10), 10);
    CHECK_EQ(hgClamp32_0_x(-5, 10), 0);
    CHECK_EQ(hgClamp32_0_x(5, 10), 5);
    CHECK_EQ(hgClamp32_0_x(50, 10), 10);

    CHECK_EQ(hgIsPowerOfTwo32(1u), 1);
    CHECK_EQ(hgIsPowerOfTwo32(2u), 1);
    CHECK_EQ(hgIsPowerOfTwo32(0x80000000u), 1);
    CHECK_EQ(hgIsPowerOfTwo32(3u), 0);
    CHECK_EQ(hgIsPowerOfTwo32(6u), 0);

    /* Rotating right by four is equivalent to rotating left by 28. */
    {
        HGuint32 v = 0xDEADBEEFu;
        HGuint32 r = (HGuint32)hgRor32((int)v, 4);
        CHECK_EQ(r, (v >> 4) | (v << 28));
    }
    CHECK_EQ(hgRor32(0x00000001, 1), (int)0x80000000u);
    CHECK_EQ(hgCopySign32(5, -1), -5);
    CHECK_EQ(hgCopySign32(5, 1), 5);
    CHECK_EQ(hgBitMask32(0x10, 4), -1);   /* Selected bit is set, so every output bit is set. */
    CHECK_EQ(hgBitMask32(0x10, 3), 0);

    CHECK_EQ(hgAdds32(2, 3), 5);
    CHECK_EQ(hgAdds32(0x7FFFFFFF, 1), 0x7FFFFFFF);   /* saturate high */
    CHECK_EQ(hgAdds32(-0x7FFFFFFF - 1, -1), -0x7FFFFFFF - 1); /* saturate low */
    CHECK_EQ(hgAdds32(-5, 5), 0);
}

static void test_64(void)
{
    CHECK_EQ(hgGet64h(hgSet64(1, 2)), 1);
    CHECK_EQ(hgGet64l(hgSet64(1, 2)), 2);
    CHECK_EQ(hgGet64h(hgSet64(-1, 2)), -1);
    CHECK_EQ(hgSet64u(1, 2), (HGuint64)0x100000002ull);
    CHECK_EQ(hgAdd64(hgSet64s(3), hgSet64s(4)), 7);
    CHECK_EQ(hgSub64(hgSet64s(3), hgSet64s(4)), -1);
    CHECK_EQ(hgMul64(3, 4), 12);
    CHECK_EQ(hgMul64(-3, 4), -12);
    CHECK_EQ(hgMul64h(0x7FFFFFFF, 4), 1);  /* 0x1FFFFFFFC >> 32 */
    /* Signed high products truncate downward; they must not round to nearest. */
    CHECK_EQ(hgMul64h(0x7FFFFFFF, 2), 0);
    CHECK_EQ(hgMul64h(-1, 1), -1);
    CHECK_EQ(hgMul64h(-1, -1), 0);
    CHECK_EQ(hgMul64h(-0x7FFFFFFF - 1, 1), -1);
    CHECK_EQ(hgMul64h(0x7FFFFFFF, 0x7FFFFFFF), 0x3FFFFFFF);
    CHECK_EQ(hgMul64h(-0x7FFFFFFF - 1, -0x7FFFFFFF - 1), 0x40000000);
    CHECK_EQ(hgMul64h(-0x7FFFFFFF - 1, 0x7FFFFFFF), -0x40000000);
    CHECK_EQ(hgMulu64h(0xFFFFFFFFu, 2u), 1u);
    CHECK_EQ(hgMulu64h(0x80000000u, 2u), 1u);
    CHECK_EQ(hgMadd64(10, 3, 4), 22);
    CHECK_EQ(hgMadd64h(0, 0x40000000, 4), 1);
    CHECK_EQ(hgMul64x32((HGint64)0x100000000ll, 3), (HGint64)0x300000000ll);
    CHECK_EQ(hgMul64x32(-2, 3), -6);
    CHECK_EQ(hgMul64x32(3, -2), -6);
    CHECK_EQ(hgMul64x32(-3, -2), 6);
    CHECK_EQ(hgMul64x32((HGint64)0xFFFFFFFFll, 0x7FFFFFFF),
             (HGint64)0x7FFFFFFE80000001ll);
    CHECK_EQ(hgMul64x32((HGint64)0x123456789ll, -5),
             (HGint64)0x123456789ll * -5);
    CHECK_EQ(hgMul64x32((HGint64)(-0x100000000ll), -1), (HGint64)0x100000000ll);
    CHECK_EQ(hgNeg64(hgSet64s(5)), -5);
    CHECK_EQ(hgNot64(0), -1);
    CHECK_EQ(hgAbs64(-5), 5);
    CHECK_EQ(hgNeg64((HGint64)0x8000000000000000ull), (HGint64)0x8000000000000000ull);
    CHECK_EQ(hgAbs64((HGint64)0x8000000000000000ull), (HGint64)0x8000000000000000ull);
    CHECK_EQ(hgAdd64(0x7FFFFFFFFFFFFFFFll, 1), (HGint64)0x8000000000000000ull);
    CHECK_EQ(hgSub64((HGint64)0x8000000000000000ull, 1), 0x7FFFFFFFFFFFFFFFll);
    CHECK_EQ(hgMadd64(0x7FFFFFFFFFFFFFFFll, 2, 2), (HGint64)0x8000000000000003ull);
    CHECK_EQ(hgMul64x32(0x7FFFFFFFFFFFFFFFll, 3), 0x7FFFFFFFFFFFFFFDll);
    CHECK_EQ(hgMul64x32((HGint64)0x80000000FFFFFFFFull, -2), (HGint64)0xFFFFFFFE00000002ull);
    CHECK_EQ(hgAbs64(hgSet64(-1, 5)), (HGint64)(0xFFFFFFFF00000005ull * -1ull));
    CHECK_EQ(hgEq64(hgSet64(1, 2), hgSet64(1, 2)), 1);
    CHECK_EQ(hgEq64(hgSet64(1, 2), hgSet64(1, 3)), 0);
    CHECK_EQ(hgLessThan64(-2, 1), 1);
    CHECK_EQ(hgLessThan64(1, -2), 0);
    CHECK_EQ(hgLessThan64u(1, 2), 1);
    CHECK_EQ(hgLessThan64u((HGuint64)-2, 1), 0);
    CHECK_EQ(hgLessThan64_32u(3, 5), 1);
    CHECK_EQ(hgLessThan64_32u((HGuint64)1 << 32, 5), 0);
    CHECK_EQ(hgIsZero64(0), 1);
    CHECK_EQ(hgIsZero64(1), 0);
    CHECK_EQ(hgLessThanZero64(-1), 1);
    CHECK_EQ(hgLessThanZero64(1), 0);
    CHECK_EQ(hgGetBit64(hgSet64(0, 8), 3), 1);
    CHECK_EQ(hgGetBit64(hgSet64(0, 8), 4), 0);
    CHECK_EQ(hgGetBit64(hgSet64x(1), 32), 1);
    CHECK_EQ(hgIsZero64(hgSetBit64(0, 32) ^ hgSet64x(1)), 1);

    CHECK_EQ(hgLsl64(1, 4), 16);
    CHECK_EQ(hgLsl64(-1, 4), -16);
    CHECK_EQ(hgLsl64(0x4000000000000000ll, 1), (HGint64)0x8000000000000000ull);
    CHECK_EQ(hgLsl64h(-1, 4), -1);
    CHECK_EQ(hgLsl64h_0_31(-1, 4), -1);
    CHECK_EQ(hgLsl64(1, 32), (HGint64)1 << 32);
    CHECK_EQ(hgLsl64h(((HGint64)1) << 40, 8), 65536);  /* (1<<48)>>32 */
    CHECK_EQ(hgLsl64h_0_31(((HGint64)1) << 40, 8), 65536);
    CHECK_EQ(hgLsr64(((HGint64)1) << 40, 40), 1);
    CHECK_EQ(hgLsr64(((HGint64)-16) , 2), (HGint64)(((HGuint64)-16) >> 2));
    CHECK_EQ(hgAsr64(-16, 2), -4);
    CHECK_EQ(hgAsr64((HGint64)(((HGuint64)-1) << 40), 40), -1);
    CHECK_EQ(hgAsr64_0_31(-16, 2), -4);
}

static void test_div(void)
{
    CHECK_EQ(hgDivu64_32(8, 2), 4);
    CHECK_EQ(hgDivu64_32(7, 2), 3);
    CHECK_EQ(hgDivu64_32(((HGuint64)1) << 32, 2), 0x80000000ull);
    CHECK_EQ(hgDivu64_32(0xFFFFFFFFFFFFFFFFull, 1), 0xFFFFFFFFFFFFFFFFull);
    CHECK_EQ(hgDivu64_32(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFull), 0x100000001ull); /* (2^32-1)*(2^32+1) = 2^64-1 */
    CHECK_EQ(hgDivu64_32(5, 8), 0);

    CHECK_EQ(hgDiv64_32(8, 2), 4);
    CHECK_EQ(hgDiv64_32(-8, 2), -4);
    CHECK_EQ(hgDiv64_32(7, -2), -3);
    CHECK_EQ(hgDiv64_32(-7, 2), -3);
    CHECK_EQ(hgDiv64_32(0, 5), 0);
    CHECK_EQ(hgDiv64_32(((HGint64)1) << 40, 0x100), ((HGint64)1) << 32);
}

static void test_float_bits(void)
{
    CHECK_EQ(F_ISVALID(FIN(1.0f)), 1);
    CHECK_EQ(F_ISVALID(FIN(0.0f)), 1);
    CHECK_EQ(F_ISVALID(0x7F800001u), 0);   /* NaN */
    CHECK_EQ(F_ISVALID(0x7F800000u), 1);   /* Inf */
    CHECK_EQ(F_FINITE(0x7F800000u), 0);
    CHECK_EQ(F_FINITE(FIN(1.0f)), 1);

    CHECK_EQ(F_ABS(0x80000000u), 0u);
    CHECK_EQ(F_ABS(0xBF800000u), 0x3F800000u);
    CHECK_EQ(F_NEG(0x3F800000u), 0xBF800000u);

    CHECK_EQ(F_CMPEQ(FIN(1.0f), FIN(1.0f)), 1);
    CHECK_EQ(F_CMPEQ(FIN(0.0f), 0x80000000u), 1);  /* +0 == -0 */
    CHECK_EQ(F_CMPEQ(FIN(1.0f), FIN(2.0f)), 0);
    CHECK_EQ(F_CMPNE(FIN(1.0f), FIN(2.0f)), 1);
    CHECK_EQ(F_CMPLT(FIN(-1.0f), FIN(1.0f)), 1);
    CHECK_EQ(F_CMPLT(FIN(1.0f), FIN(-1.0f)), 0);
    CHECK_EQ(F_CMPGT(FIN(2.0f), FIN(1.0f)), 1);
    CHECK_EQ(F_CMPLE(FIN(1.0f), FIN(1.0f)), 1);
    CHECK_EQ(F_CMPGE(FIN(1.0f), FIN(1.0f)), 1);
    /* The positive quiet NaN sorts above 1.0 in the comparable mapping. */
    CHECK_EQ(F_CMPLT(FIN(1.0f), 0x7FC00000u), 1);

    CHECK_EQ(F_ZCMPEQ(FIN(0.0f)), 1);
    CHECK_EQ(F_ZCMPEQ(0x80000000u), 1);
    CHECK_EQ(F_ZCMPEQ(FIN(1.0f)), 0);
    CHECK_EQ(F_ZCMPLT(0x80000001u), 1);
    CHECK_EQ(F_ZCMPLT(0x80000000u), 0);
    CHECK_EQ(F_ZCMPGT(FIN(1.0f)), 1);
    CHECK_EQ(F_ZCMPGT(FIN(0.0f)), 0);

    CHECK_EQ(F_MIN(FIN(1.0f), FIN(2.0f)), FIN(1.0f));
    CHECK_EQ(F_MAX(FIN(1.0f), FIN(2.0f)), FIN(2.0f));
    CHECK_EQ(F_MIN(0x80000000u, FIN(1.0f)), 0x80000000u);  /* -0 < 1 */
    CHECK_EQ(F_MAXZERO(0xC0000000u), 0u);
    CHECK_EQ(F_MAXZERO(FIN(1.0f)), FIN(1.0f));

    CHECK_EQ(F_ADD(FIN(1.0f), FIN(1.0f)), FIN(2.0f));
    CHECK_EQ(F_ADD(FIN(1.5f), FIN(2.25f)), FIN(3.75f));
    CHECK_EQ(F_ADD(FIN(1.0f), 0xBF800000u), FIN(0.0f));    /* 1 + -1 = 0 */
    CHECK_EQ(F_ADD(FIN(100.0f), FIN(0.0f)), FIN(100.0f));
    CHECK_EQ(F_SUB(FIN(5.0f), 0xC0400000u), FIN(8.0f));    /* 5 - (-3) = 8 */
    CHECK_EQ(F_SUB(FIN(5.0f), FIN(3.0f)), FIN(2.0f));
    /* sum with opposite signs round-trips through the subtract path */
    CHECK_EQ(F_ADD(FIN(3.75f), 0xC0200000u), FIN(1.25f));  /* 3.75 + (-2.5) */
}

static void test_float_multiply(void)
{
    static const HGuint32 subnormalCases[][3] = {
        {0x7939E89Fu, 0x004C8976u, 0x3A148931u},
        {0xEC3A5FAEu, 0x007DE735u, 0xAD38D8E1u},
        {0x621C2D9Eu, 0x800F7B92u, 0xA2AF11BCu},
        {0x40000000u, 0x00000001u, 0x00800001u},
        {0x40000000u, 0x80000001u, 0x80800001u},
        {0x40000000u, 0x007FFFFFu, 0x00FFFFFFu},
        {0x40000000u, 0x807FFFFFu, 0x80FFFFFFu},
    };
    unsigned int i;
    /* The original treats subnormals differently in the two operands. */
    for (i = 0; i < sizeof(subnormalCases) / sizeof(subnormalCases[0]); ++i)
    {
        const HGuint32 *c = subnormalCases[i];
        CHECK_EQ(F_MUL(c[0], c[1]), c[2]);
        CHECK_EQ(F_MUL(c[1], c[0]), 0u);
    }
    CHECK_EQ(F_MUL(0x40000000u, 0u), 0u);
    CHECK_EQ(F_MUL(0x40000000u, 0x80000000u), 0u);
    CHECK_EQ(F_MUL(0x3F800000u, 0x007FFFFFu), 0u);
    CHECK_EQ(F_MUL(0x40000000u, 0x00800000u), 0x01000000u);
    CHECK_EQ(F_MUL(0x00800000u, 0x40000000u), 0x01000000u);
    CHECK_EQ(F_SQR(FIN(1.0f)), FIN(1.0f));
    CHECK_EQ(F_SQR(FIN(1.5f)), FIN(2.25f));
    CHECK_EQ(F_SQR(FIN(-1.5f)), FIN(2.25f));
    CHECK_EQ(F_SQR(FIN(0.75f)), FIN(0.5625f));
}

static void test_trig(void)
{
    CHECK_EQ(FIN(hgFsin(0.0f)), FIN(0.0f));
    /* The scaled pi constant rounds one integer unit below 0x20000000. */
    CHECK_EQ(radiansToUnitScalar(FIN(3.14159265f)), 0x1FFFFFFF);
    CHECK_EQ(radiansToUnitScalar(FIN(3.14159265f / 2.0f)), 0x0FFFFFFF);
    /* sin near zero: bit pattern passthrough */
    CHECK_EQ(F_SIN(FIN(0.0001f)), FIN(0.0001f));  /* below the ~2^-11 passthrough threshold */
    CHECK(fabs(hgFsin(0.1f) - sinf(0.1f)) < 5e-4f);
    CHECK(fabs(hgFcos(0.1f) - cosf(0.1f)) < 5e-4f);
    CHECK(fabs(hgFsin(1.0f) - sinf(1.0f)) < 5e-4f);
    CHECK(fabs(hgFcos(1.0f) - cosf(1.0f)) < 5e-4f);
    CHECK(fabs(hgFsin(3.0f) - sinf(3.0f)) < 5e-4f);
    CHECK(fabs(hgFcos(3.0f) - cosf(3.0f)) < 5e-4f);
}

static void test_fill(void)
{
    static HGuint8 buf8[64 + 8];
    static HGuint16 buf16[32 + 4];
    static HGuint32 buf32[16 + 4];

    memset(buf8, 0xAA, sizeof(buf8));
    hgFill8(buf8 + 1, 0x5A, 64);
    CHECK(buf8[0] == 0xAA && buf8[65] == 0xAA);
    for (int i = 1; i <= 64; ++i)
        CHECK_EQ(buf8[i], 0x5A);

    memset(buf16, 0xAA, sizeof(buf16));
    hgFill16(buf16, 0x1234, 32);
    for (int i = 0; i < 32; ++i)
        CHECK_EQ(buf16[i], 0x1234);

    memset(buf32, 0xAA, sizeof(buf32));
    hgFill32(buf32, 0xDEADBEEFu, 16);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(buf32[i], 0xDEADBEEFu);

    /* odd lengths */
    hgFill8(buf8, 0, 3);
    CHECK_EQ(buf8[0], 0); CHECK_EQ(buf8[2], 0);
    hgFill16(buf16, 0xBEEF, 7);
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(buf16[i], 0xBEEF);
    hgFill32(buf32, 1, 5);
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(buf32[i], 1);
}

static void test_endian(void)
{
    CHECK_EQ(hgGetEndianess(), HG_FALSE);   /* Supported host and target layouts are little endian. */
}

int main(void)
{
    test_clz();
    test_range_minmax();
    test_clamp_misc();
    test_64();
    test_div();
    test_float_bits();
    test_float_multiply();
    test_trig();
    test_fill();
    test_endian();

    printf("hg tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
