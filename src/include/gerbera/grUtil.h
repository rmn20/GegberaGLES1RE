/* grUtil.h - Gerbera core primitives: assert and die.
 *
 * The original assert machinery (recovered from the decompilation):
 *   grAssert(expr, file, line)  - prints "\nAssertion: %s (%s %d)\n",
 *                                 returns 1 (failure marker)
 *   grDie(v)                    - fatal handler; the reference build crashes
 *                                 by dereferencing null (Symbian exc panic)
 *
 * Call sites in the ported core follow the decompiled shape:
 *   ok = grAssert("expr", "file.inl", line) == 0;
 *   if (!ok) grDie(0);
 */
#ifndef GR_UTIL_H
#define GR_UTIL_H

#include "hg/hgDefs.h"

/* grDefs.h defines _grVector3i; pointer-only here to avoid an include cycle */
struct _grVector3i;

HG_EXTERN_C_BEGIN

void grPrintf(const char *format, ...);
int  grAssert(const char *expr, const char *file, int line);
void grDie(int v) ; /* marked noreturn in the port */

/* --- state-free grUtil.h helpers (need grDefs.h types + grRGBA.h) -------- */
int grVector3iGet(struct _grVector3i *v, int ndx);
int grUnpackIndex(unsigned int indices, int n);
unsigned int grSetIndices(int i0, int i1, int i2);
int grMaskedColorsEqual(unsigned int color, unsigned int mask);

/* combined check used by the ported .inl code */
#if HG_ASSERT_ENABLED
#define GR_CHECK(cond, file, line)                                          \
    do {                                                                    \
        HGbool gr_ok_ = HG_TRUE;                                            \
        if (!(cond))                                                        \
            gr_ok_ = grAssert(#cond, (file), (line)) == 0;                  \
        if (!gr_ok_)                                                        \
            grDie(0);                                                       \
    } while (0)
#else
#define GR_CHECK(cond, file, line) ((void)0)
#endif

HG_EXTERN_C_END

#if HG_ASSERT_ENABLED
#define GR_UTIL_CHECK(cond, file, line)          \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, (file), (line)) != 0) \
            grDie(0);                            \
    } while (0)
#else
#define grAssert(expr, file, line) (0)
#define GR_UTIL_CHECK(cond, file, line) ((void)0)
#endif

/* --- fills (grFill.inl, implemented in grFill.c) ---------------------------
 * grFillNMasked writes `value` through `mask` into N-bit elements;
 * grFill4xN writes the cyclic pattern a,b,c,d over `size` elements (the
 * dithered clear uses it with four per-row dithered colors). */
void grFill8Masked(unsigned char *dst, unsigned int value, unsigned int mask, int size);
void grFill16Masked(unsigned short *dst, unsigned int value, unsigned int mask, int size);
void grFill32Masked(unsigned int *dst, unsigned int value, unsigned int mask, int size);
void grFill4x8(unsigned char *dst, unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned int mask, int size);
void grFill4x16(unsigned short *dst, unsigned short a, unsigned short b, unsigned short c, unsigned short d, unsigned int mask, int size);
void grFill4x32(unsigned int *dst, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int mask, int size);

/* Bit-exact float texcoord -> 16.16 fixed-point conversion for unit matrix. */
static inline int grTexCoordUnitFloatFast(int value)
{
    unsigned int e = (unsigned int)(unsigned char)((unsigned int)value >> 23);
    int sign, mag, mantissa, rshift;

#if defined(__GNUC__)
    if (__builtin_expect((unsigned int)(e - 1u) >= 254u, 0))
#else
    if ((unsigned int)(e - 1u) >= 254u)
#endif
        return 0;
    sign = value >> 31;
    mag = (int)(((unsigned int)value & 0x7FFFFFu) | 0x800000u) << 3;
    mantissa = (int)(((unsigned int)mag ^ (unsigned int)sign) - (unsigned int)sign);
    rshift = 132 - (int)e;
    if (rshift >= 0)
    {
#if defined(__GNUC__)
        if (__builtin_expect(rshift > 31, 0))
#else
        if (rshift > 31)
#endif
            rshift = 31;
        return mantissa >> rshift;
    }
    {
        int lshift = -rshift;
#if defined(__GNUC__)
        if (__builtin_expect(lshift > 31, 0))
#else
        if (lshift > 31)
#endif
            lshift = 31;
        return (int)((unsigned int)mantissa << lshift);
    }
}

#endif /* GR_UTIL_H */
