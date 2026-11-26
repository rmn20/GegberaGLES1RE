/* hgInt32.c - 32/64 bit integer helpers.
 *
 * Assert sites retain the original hgInt32.inl / hgInt64Native.inl file and
 * line strings for comparison with the reference binaries.
 */
#include "hg/hgInt32.h"
#include "hg/hgDefs.h"

/* ------------------------------------------------------------------ */
/* 32 bit helpers                                                     */
/* ------------------------------------------------------------------ */

int hgClz32(HGuint32 value)
{
#if defined(__arm__) && defined(__ARM_ARCH) && (__ARM_ARCH >= 5) && !defined(__thumb__)
    int res;
    __asm__("clz %0, %1" : "=r"(res) : "r"(value));
    return res;
#elif defined(__GNUC__) || defined(__clang__)
    return value != 0u ? __builtin_clz((unsigned int)value) : 32;
#else
    int c = 0;
    if (value >= 0x10000u) { value >>= 16; } else { c = 16; }
    if (value >= 0x100u)   { value >>= 8;  } else { c += 8; }
    if (value >= 0x10u)    { value >>= 4;  } else { c += 4; }
    if (value >= 4u)       { value >>= 2;  } else { c += 2; }
    /* value is 0, 1, 2 or 3 here: 2 >> value maps {0,1,2,3} -> {2,1,0,0} */
    return (int)(2u >> value) + c;
#endif
}

int hgInRange32(int a, int lo, int hi)
{
    HG_ASSERT_AT("hgInt32.inl", 294, lo <= hi);
    /* unsigned trick: 0 <= a-lo <= hi-lo, valid whenever lo <= hi */
    return (HGuint32)a - (HGuint32)lo <= (HGuint32)hi - (HGuint32)lo;
}

int hgMin32(int a, int b)
{
    return b < a ? b : a;
}

int hgMax32(int a, int b)
{
    return b > a ? b : a;
}

HGuint32 hgMinu32(HGuint32 a, HGuint32 b)
{
    return b < a ? b : a;
}

HGuint32 hgMaxu32(HGuint32 a, HGuint32 b)
{
    return b > a ? b : a;
}

int hgAbs32(int a)
{
    return a < 0 ? (int)(0u - (HGuint32)a) : a;
}

int hgSign32(int value)
{
    return (value > 0) | (value >> 31);
}

int hgSignMask32(int x)
{
    return x >> 31;
}

int hgSignBit32(int x)
{
    return (int)((HGuint32)x >> 31);
}

int hgMaxZero32(int a)
{
    return a & ~(a >> 31);
}

int hgClamp32(int v, int mn, int mx)
{
    HG_ASSERT_AT("hgInt32.inl", 527, mn <= mx);
    if (v < mn) v = mn;
    if (v > mx) return mx;
    return v;
}

int hgClamp32_0_x(int v, int mx)
{
    HG_ASSERT_AT("hgInt32.inl", 577, mx >= 0);
    if (v > mx) v = mx;
    return hgMaxZero32(v);
}

int hgIsPowerOfTwo32(HGuint32 x)
{
    return ((x - 1u) & x) == 0u;
}

int hgDiff32(int a, int b)
{
    return hgAbs32((int)((HGuint32)a - (HGuint32)b));
}

int hgRor32(int a, int sh)
{
    HGuint32 u = (HGuint32)a;
    HGuint32 s = (HGuint32)sh & 31u;
    HG_ASSERT_AT("hgInt32.inl", 503, sh >= 0 && sh <= 32);
    return (int)((u >> s) | (u << ((0u - s) & 31u)));
}

int hgCopySign32(int d, int s)
{
    HG_ASSERT_AT("hgInt32.inl", 611, d >= 0);
    return s < 0 ? -d : d;
}

int hgBitMask32(int x, int i)
{
    HG_ASSERT_AT("hgInt32.inl", 310, i >= 0 && i <= 31);
    return (int)((HGuint32)x << (31 - i)) >> 31;
}

int hgAdds32(int a, int b)
{
    /* saturating signed add: detect overflow by comparing the 32 bit sum
     * against the sign of the 64 bit sum */
    HGint64 sum64 = hgAdd64(hgSet64s(a), hgSet64s(b));
    int high = hgGet64h(sum64);
    int sum  = (int)((HGuint32)b + (HGuint32)a);
    if ((sum ^ high) < 0)
        return high ^ 0x7FFFFFFF;
    return sum;
}

HGuint32 hgFloatBitPattern(HGfloat f)
{
    union { HGfloat f; HGuint32 u; } conv;
    conv.f = f;
    return conv.u;
}

HGfloat hgBitPatternToFloat(HGuint32 x)
{
    union { HGuint32 u; HGfloat f; } conv;
    conv.u = x;
    return conv.f;
}

HGbool hgGetEndianess(void)
{
    /* probe byte order via a multi-byte constant; returns HG_ENDIAN_BIG (1)
     * when the first byte is 0x12, HG_ENDIAN_LITTLE (0) otherwise.
     * Call sites compare against HG_ENDIAN_LITTLE (see grFill.inl:160). */
    const HGuint32 probe = 0x12345678u;
    const HGuint8 *p = (const HGuint8 *)&probe;
    HG_ASSERT_AT("hgDefs.h", 573, *p == (HGuint8)0x12u || *p == (HGuint8)0x78u);
    return *p == 0x12 ? HG_TRUE : HG_FALSE;
}

/* ------------------------------------------------------------------ */
/* 64 bit helpers (native 64 bit path; original: hgInt64Native.inl)   */
/* ------------------------------------------------------------------ */

HGint64 hgToSigned64(HGuint64 a)      { return (HGint64)a; }
HGuint64 hgToUnsigned64(HGint64 a)    { return (HGuint64)a; }

HGint64 hgSet64s(int l)               { return (HGint64)l; }

HGuint64 hgSet64u(HGuint32 h, HGuint32 l)
{
    return ((HGuint64)h << 32) | (HGuint64)l;
}

HGint64 hgSet64(int h, int l)
{
    return (HGint64)(((HGuint64)(HGuint32)h << 32) | (HGuint64)(HGuint32)l);
}

HGint64 hgSet64x(int hi)
{
    return hgSet64(hi, 0);
}

HGint64 hgZero64(void)
{
    return 0;
}

int hgGet64h(HGint64 a)
{
    return (int)((HGuint64)a >> 32);
}

HGuint32 hgGet64uh(HGuint64 a)
{
    return (HGuint32)(a >> 32);
}

HGuint32 hgGet64ul(HGuint64 a)
{
    return (HGuint32)a;
}

int hgGet64l(HGint64 a)
{
    return (int)((HGuint64)a & 0xFFFFFFFFu);
}

int hgGetBit64(HGint64 x, int offset)
{
    HG_ASSERT_AT("hgInt64Native.inl", 283, offset >= 0 && offset <= 63);
    if (offset & 0x20)
        return (int)((HGuint32)((HGuint64)x >> 32) >> (offset & 31)) & 1;
    return (int)((HGuint64)x >> (offset & 31)) & 1;
}

HGint64 hgSetBit64(HGint64 x, int offset)
{
    HGuint64 mask;
    HG_ASSERT_AT("hgInt64Native.inl", 314, offset >= 0 && offset <= 63);
    if (offset & 0x20)
        mask = (HGuint64)1 << 32 << (offset & 31);
    else
        mask = (HGuint64)1 << (offset & 31);
    return x | (HGint64)mask;
}

HGint64 hgAdd64(HGint64 a, HGint64 b) { return (HGint64)((HGuint64)b + (HGuint64)a); }
HGint64 hgSub64(HGint64 a, HGint64 b) { return (HGint64)((HGuint64)a - (HGuint64)b); }
HGint64 hgOr64(HGint64 a, HGint64 b)  { return b | a; }

int hgLessThan64(HGint64 a, HGint64 b)
{
    return a < b;
}

int hgLessThan64u(HGuint64 a, HGuint64 b)
{
    return a < b;
}

int hgLessThan64_32u(HGuint64 a, HGuint32 b)
{
    int result = 0;
    if (hgGet64uh(a) == 0)
        result = hgGet64ul(a) < b;
    return result;
}

int hgIsZero64(HGint64 a)
{
    return a == 0;
}

int hgLessThanZero64(HGint64 a)
{
    return (int)((HGuint32)hgGet64h(a) >> 31);
}

int hgEq64(HGint64 a, HGint64 b)
{
    return a == b;
}

HGint64 hgLsl64(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1016, sh >= 0 && sh <= 63);
    return (HGint64)((HGuint64)a << (sh & 63));
}

int hgLsl64h(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1104, sh >= 0 && sh <= 63);
    return (int)(((HGuint64)a << (sh & 63)) >> 32);
}

int hgLsl64h_0_31(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1163, sh >= 0 && sh <= 31);
    return (int)(((HGuint64)a << sh) >> 32);
}

HGint64 hgLsr64(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1194, sh >= 0 && sh <= 63);
    return (HGint64)((HGuint64)a >> (sh & 63));
}

HGint64 hgAsr64(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1393, sh >= 0 && sh <= 63);
    return a >> (sh & 63);
}

HGint64 hgAsr64_0_31(HGint64 a, int sh)
{
    HG_ASSERT_AT("hgInt64Native.inl", 1434, sh >= 0 && sh <= 31);
    return a >> sh;
}

HGint64 hgNeg64(HGint64 a)
{
    return (HGint64)(0ull - (HGuint64)a);
}

HGint64 hgNot64(HGint64 a)
{
    return ~a;
}

HGint64 hgAbs64(HGint64 a)
{
    return a >= 0 ? a : hgNeg64(a);
}

HGint64 hgMul64(int a, int b)
{
    return (HGint64)a * (HGint64)b;
}

HGuint64 hgMulu64(HGuint32 a, HGuint32 b)
{
    return (HGuint64)a * (HGuint64)b;
}

int hgMul64h(int a, int b)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && \
    defined(__ARM_ARCH) && __ARM_ARCH >= 6 && !defined(__thumb__)
    int result;
    __asm__("smmul %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
#else
    return (int)(((HGint64)a * (HGint64)b) >> 32);
#endif
}

HGuint32 hgMulu64h(HGuint32 a, HGuint32 b)
{
    return (HGuint32)(((HGuint64)a * (HGuint64)b) >> 32);
}

HGint64 hgMul64x32(HGint64 a, int b)
{
    return (HGint64)((HGuint64)a * (HGuint64)(HGint64)b);
}

HGint64 hgMadd64(HGint64 a, int b, int c)
{
    return hgAdd64(a, hgMul64(b, c));
}

int hgMadd64h(HGint64 a, int b, int c)
{
    return hgGet64h(hgAdd64(a, hgMul64(b, c)));
}

int hgClz64(HGint64 a)
{
    int bits = 0;
    int x = hgGet64h(a);
    if (x == 0)
    {
        x = hgGet64l(a);
        bits = 32;
    }
    return bits + hgClz32((HGuint32)x);
}

HGuint32 hgDiv32Internal(HGuint32 *cio, HGuint64 *aio, HGuint32 b, int n)
{
    /* restoring division of a 64 bit value by a 32 bit divisor, producing
     * n+1 quotient bits per call; original: hgInt64Native.inl:2129 */
    HGuint32 ah = hgGet64uh(*aio);
    HGuint32 al = hgGet64ul(*aio);
    HGuint32 carry = *cio;
    HGuint32 q = 0;
    HG_ASSERT_AT("hgInt64Native.inl", 2129, n >= 0 && n <= 31);
    HG_ASSERT_AT("hgInt64Native.inl", 2130, n == 31 || *cio == 0);
    do
    {
        if (carry == 0)
            carry = ah >= b;
        if (carry != 0)
            ah -= b;
        q = carry + 2 * q;
        carry = ah >= 0x80000000u;
        {
            HGuint64 doubled = hgSet64u(ah, al) + hgSet64u(ah, al);
            al = hgGet64ul(doubled);
            ah = hgGet64uh(doubled);
        }
    } while (n-- != 0);
    *aio = hgSet64u(ah, al);
    *cio = carry;
    return q;
}

HGuint64 hgDivu64_32(HGuint64 a, HGuint32 b)
{
    int shb, sha;
    HGuint32 ql, qh, carry;
    HG_ASSERT_AT("hgInt64Native.inl", 2264, b != 0);
    if (hgLessThan64_32u(a, b) != 0)
        return hgSet64u(0, 0);
    shb = hgClz32(b);
    b <<= shb;
    sha = hgClz64((HGint64)a);
    a = (HGuint64)hgLsl64((HGint64)a, sha);
    carry = 0;
    qh = 0;
    if (shb - sha >= 0)
        qh = hgDiv32Internal(&carry, &a, b, shb - sha);
    ql = hgDiv32Internal(&carry, &a, b, hgMin32(shb + 32 - sha, 31));
    return hgSet64u(qh, ql);
}

HGint64 hgDiv64_32(HGint64 a, int b)
{
    HGuint32 sign = ((HGuint32)b ^ (HGuint32)hgGet64h(a)) >> 31;
    HGint64 res;
    HG_ASSERT_AT("hgInt64Native.inl", 2331, b != 0);
    res = (HGint64)hgDivu64_32((HGuint64)hgAbs64(a), (HGuint32)hgAbs32(b));
    if (sign != 0)
        return hgNeg64(res);
    return res;
}
