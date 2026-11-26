/* imApi.h - fixed point / bit-pattern scalar math library ("imath").
 *
 * Scalar representations:
 *   - IScalar     IEEE single-precision bits; valid exponents are 0..254
 *   - IUnitScalar signed fixed point with 29 fractional bits, in [-1, 1]
 *   - XScalar     signed fixed point with 21 fractional bits
 *   - XColor3     3 x XScalar (r, g, b)
 *
 * Vector types store signed mantissas scaled by 2^29:
 *   - _IVector3/_IVector4  { HGuint32 e; HGint32 m[3|4]; } share exponent e
 *   - _IFVector3/_IFVector4 have the same layout, with one exponent byte
 *       per component packed into e
 *   - _IMatrix4x4  { _IFVector4 v[4]; } - column major, v[c] = column c
 *   - _XNormalTransform { HGint32 m[3][3]; HGint32 scale; }
 *   - _IVertexTransform  pre-scaled transform for the vertex pipeline
 */
#ifndef IM_API_H
#define IM_API_H

#include "hg/hgDefs.h"
#include "hg/hgFloat.h"
#include "hg/hgInt32.h"

HG_EXTERN_C_BEGIN

/* Unit scalar scale: 1.0 = 2^29. */
#define IUNITSCALAR_ONE 0x20000000
#define IUNITSCALAR_TWO 0x40000000
#define IM_MANTISSA_MAX 0x3FFFFFFF
#define IM_EXPONENT_ZERO 0

typedef HGint32 IScalar;
typedef HGint32 IUnitScalar;
typedef HGint32 XScalar;

typedef struct _IVector3  { HGuint32 e; HGint32 m[3]; } _IVector3;
typedef struct _IVector4  { HGuint32 e; HGint32 m[4]; } _IVector4;
typedef struct _IFVector3 { HGuint32 e; HGint32 m[3]; } _IFVector3;
typedef struct _IFVector4 { HGuint32 e; HGint32 m[4]; } _IFVector4;
typedef struct _IMatrix4x4 { _IFVector4 v[4]; } _IMatrix4x4;
typedef struct _XNormalTransform { HGint32 m[3][3]; HGint32 scale; } _XNormalTransform;

/* Nokia CodeView TI=0x1648 and UIQ TI=0x166A both name v as _IFVector4[4]
 * and exp as signed int. Nokia also includes w and unnormalized. */
typedef struct _IVertexTransform
{
    _IFVector4 v[4];
    HGint64 w[4];
    HGint32 unnormalized[4][4];
    HGuint32 sh[4];
    HGint32 exp;
    HGuint32 cl;
} _IVertexTransform;

/* Dot-product alignment for one transform and one packed input exponent. */
typedef struct _IFVertexTransformPlan
{
    HGuint32 inputExponent;
    HGuint32 shift[4];
    HGint32 exponent[4];
} _IFVertexTransformPlan;

/* Shared matrix scale for bounded-error float position transforms. */
typedef struct _ICommonVertexTransform
{
    HGint32 m[4][4];
    HGint32 exponent;
    HGuint32 eligible;
} _ICommonVertexTransform;

enum
{
    IM_COMMON_TRANSFORM_SPREAD = 16,
    IM_COMMON_TRANSFORM_MIN_W = 1 << 17,
    IM_COMMON_TRANSFORM_CLIP_MARGIN = 32,
    IM_COMMON_TRANSFORM_ERROR = 8
};

void IVertexTransform_prepareCommon(_ICommonVertexTransform *plan, const _IVertexTransform *transform);
/* Input must come from IFVector4_setfv. Zero leaves out unchanged for the precise fallback. */
int IVertexTransform_mulfv4Common(_IVector4 *out, const _ICommonVertexTransform *plan, const _IFVector4 *in);

/* imath assert: same semantics as in the reference build - prints through
 * grAssert and dies.  Call sites keep the original file/line strings. */
void imAssert(const char *expr, const char *file, unsigned int line);
#if !HG_ASSERT_ENABLED
#define imAssert(expr, file, line) ((void)0)
#endif

/* --- construction / conversion (IScalar.c) -------------------------- */

HGint32  IScalar_clampUnit(HGint32 a);          /* clamp float to [0, 1]   */
HGint32  IScalar_intToScalar(HGint32 a);        /* F_ITOF                  */
HGint32  IScalar_iftos(HGint32 a);              /* float bits -> IScalar   */
HGint32  IScalar_ftos(HGfloat a);
HGfloat  IScalar_stof(HGint32 a);
HGint32  IScalar_immediate(HGfloat a);
HGint32  IScalar_one(void);
HGint32  IScalar_zero(void);
HGint32  IScalar_set(HGuint32 e, HGint32 m);    /* biased exponent + signed mantissa scaled by 2^29 */
HGint32  IScalar_setAndNormalize(HGuint32 e, HGint32 m);
HGuint32 IScalar_getExponent(HGint32 x);
HGint32  IScalar_getMantissa(HGint32 x);        /* signed mantissa scaled by 2^29 */
HGint32  IScalar_getOESMantissa(HGint32 s);
HGint32  IScalar_getOESExponent(HGint32 s);
HGuint32 IScalar_getUnsignedMantissa(HGint32 x);
HGint32  IScalar_toUnitScalar(HGint32 x);
HGint32  IScalar_fixedToScalar(HGint32 m, HGint32 n);  /* n fractional bits */
HGint32  IScalar_scalarToFixed(HGint32 r, HGint32 n);
HGint32  IScalar_byteToScalar(HGuint8 b);       /* b / 255                 */
HGint32  IScalar_toFixedHO(HGint32 value, HGint32 bits);
HGint32  IScalar_toXScalar(HGint32 v);          /* 21 bit fixed            */

/* --- arithmetic (bit pattern ops) ----------------------------------- */

HGint32  IScalar_mul(HGint32 a, HGint32 b);
HGint32  IScalar_add(HGint32 a, HGint32 b);
HGint32  IScalar_sub(HGint32 a, HGint32 b);
HGint32  IScalar_sqr(HGint32 a);
HGint32  IScalar_rcp(HGint32 a);
HGint32  IScalar_rsq(HGint32 a);
HGint32  IScalar_negate(HGint32 x);
HGint32  IScalar_abs(HGint32 x);
HGint32  IScalar_max(HGint32 a, HGint32 b);
HGint32  IScalar_degreesToRadians(HGint32 x);

/* --- compares -------------------------------------------------------- */

HGint32  IScalar_cmpeq(HGint32 a, HGint32 b);
HGint32  IScalar_cmpne(HGint32 a, HGint32 b);
HGint32  IScalar_cmplt(HGint32 a, HGint32 b);
HGint32  IScalar_cmpgt(HGint32 a, HGint32 b);
HGint32  IScalar_cmple(HGint32 a, HGint32 b);
HGint32  IScalar_cmpge(HGint32 a, HGint32 b);
HGint32  IScalar_testeq(HGint32 a);             /* compare against zero    */
HGint32  IScalar_testne(HGint32 a);
HGint32  IScalar_testlt(HGint32 a);
HGint32  IScalar_testgt(HGint32 a);
HGint32  IScalar_testle(HGint32 a);
HGint32  IScalar_testge(HGint32 a);

/* --- misc ------------------------------------------------------------ */

HGuint32 IScalar_log2_iterate(HGuint32 y);      /* normalized input; 31 fractional output bits */
HGint32  XScalar_log2(HGint32 y);               /* 21 fractional bits, or INT_MIN for y <= 0 */
HGint32  IScalar_cos(HGint32 x);

/* --- unit scalar domain (IUnitScalar.c) ------------------------------ */

HGint32  IUnitScalar_maxZero(HGint32 x);
HGint32  IUnitScalar_lerp(HGint32 s0, HGint32 s1, HGint32 factor);
HGint32  IUnitScalar_tos(HGint32 x);            /* unit scalar -> IScalar  */
/* IUnitScalar_sin / IUnitScalar_cos live in hg/hgFloat.c (assert provenance) */

/* --- validity (IMath.c) ---------------------------------------------- */

HGint32  IScalar_isValid(HGint32 v);
HGint32  IUnitScalar_isValid(HGint32 v);
HGint32  IVector3_isValid(_IVector3 *v);        /* IMath.c                 */
HGint32  IVector4_isValid(_IVector4 *v);        /* IMath.c                 */

/* --- XScalar domain (imInline.inl) ----------------------------------- */

int           XScalar_cmplt(XScalar a, XScalar b);
int           XScalar_cmple(XScalar a, XScalar b);
int           XScalar_cmpne(XScalar a, XScalar b);
int           XScalar_zcmpge(XScalar a);
int           XScalar_zcmpgt(XScalar a);
int           XScalar_zcmplt(XScalar a);
int           XScalar_zcmpeq(XScalar a);
int           XScalar_zcmpne(XScalar a);
XScalar       XScalar_add(XScalar a, XScalar b);
XScalar       XScalar_sub(XScalar a, XScalar b);
XScalar       XScalar_abs(XScalar v);
XScalar       XScalar_mul(XScalar a, XScalar b);          /* low 32 bits of (a*b)>>21 */
XScalar       XScalar_min(XScalar a, XScalar b);
XScalar       XScalar_mul_sat(XScalar a, XScalar b);      /* saturating, rsh21 */
XScalar       XScalar_mulu_sat(XScalar a, XScalar b);     /* unsigned variant */
XScalar       XScalar_exp2(XScalar x);                    /* inverse of log2  */
XScalar       XScalar_pow(XScalar base, XScalar power);
XScalar       XScalar_toIScalar(XScalar v);
unsigned short XScalar_tous(XScalar v);
int           XScalar_signBit(XScalar v);

/* --- imath fixed point helpers (imApi.inl / imInline.inl) ------------ */

int      imLsl32s(int v, int sh);               /* saturating left shift   */
int      imLsl32us(unsigned int v, int sh);     /* unsigned saturating     */
int      imMulAsr32Sat(int a, int b, int rsh);
HGint64  imMulAsr64(int a, int b, unsigned int shift);
int      imSat64(HGint64 a, int rsh);
unsigned int imClampShift63(unsigned int x);
HGint64  imAsr64_1_31(HGint64 a, int sh);       /* shift 1..31 only        */
HGint64  imDet2_64(int a, int b, int c, int d);
int      imMulLsl64h(int a, int b, unsigned int shift);
int      imAdd32OF(int a, int b);               /* overflow-checking adds  */
int      imSub32OF(int a, int b);
int      imLsl32OF(int a, unsigned int sh);
int      imLsl64h_0_31OF(HGint64 a, unsigned int sh);

/* reciprocal estimate: returns 64-bit packed (high = reciprocal mantissa,
 * low = normalization shift) */
extern const HGint8 imIntrcpLUT[310];
HGint64  imRcpu(unsigned int x);

static inline HGint64 imRcpu_inline(unsigned int x)
{
    unsigned int sh = hgClz32(x);
    unsigned int xn = x << sh;
    unsigned int a0 = ((unsigned int)(HGuint8)imIntrcpLUT[(HGuint8)(xn >> 23)]) << 24;
    unsigned int v1 = hgMulu64h(a0, xn);
    unsigned int a0a = 2 * hgMulu64h(a0, ~v1);
    unsigned int v2 = hgMulu64h(a0a, xn);
    return (HGint64)(((HGint64)(hgMulu64h(a0a, ~v2)) << 32) | sh);
}

/* --- XColor3 (imInline.inl) ------------------------------------------ */

typedef struct _XColor3 { XScalar r, g, b; } _XColor3;

void XColor3_zero(_XColor3 *dst);
void XColor3_setx(_XColor3 *color, int r, int g, int b);
void XColor3_setub(_XColor3 *color, HGuint8 r, HGuint8 g, HGuint8 b);
void XColor3_setf(_XColor3 *color, int r, int g, int b);
void XColor3_mul(_XColor3 *dst, _XColor3 *src0, _XColor3 *src1);
void XColor3_add(_XColor3 *dst, _XColor3 *src0, _XColor3 *src1);
int  XColor3_isZero(_XColor3 *src);
void XColor3_tousv(unsigned short *pD, _XColor3 *src);

/* XColor3Acc: 64-bit accumulators for color blending */
void XColor3Acc_set(HGint64 *d, _XColor3 *a);
void XColor3Acc_setZero(HGint64 *d);
void XColor3Acc_muls(HGint64 *d, _XColor3 *a, int b);
void XColor3Acc_scaleColor(HGint64 *d, HGint64 *a, _XColor3 *b);
void XColor3Acc_addAcc(HGint64 *d, HGint64 *s0, HGint64 *s1);
void XColor3Acc_addColor(HGint64 *d, HGint64 *s0, _XColor3 *s1);
void XColor3Acc_madd(HGint64 *d, HGint64 *s0, _XColor3 *s1, int s2);
void XColor3Acc_toRGB(unsigned short *pD, HGint64 *rgb);

/* --- XNormal3 (INormal3.c / imInline.inl) ---------------------------- */
/* normals are 3 x int in 10.21 fixed point (an int[3], no struct wrapper) */

void XNormal3_init001n(int *pD);                /* (0, 0, -1)              */
void XNormal3_init001p(int *pD);                /* (0, 0, 1)               */
void XNormal3_setf(int *normal, HGfloat x, HGfloat y, HGfloat z);
void XNormal3_setb(int *pD, HGint8 vx, HGint8 vy, HGint8 vz);
void XNormal3_sets(int *pD, HGint16 vx, HGint16 vy, HGint16 vz);
void XNormal3_setx(int *pD, int vx, int vy, int vz);
void XNormal3_copy(int *pD, int *pA);
void XNormal3_negate(int *pD, int *pA);
int  XNormal3_dot(int *pA, int *pB);
void XNormal3_normalize(int *pD, int *pA);
void XNormal3_accfv(int *pD, int (*pA)[3], int *pS, int n);

/* --- IFVector3 / IFVector4 ------------------------------------------- */

void IFVector3_init100(_IFVector3 *pD);         /* (1, 0, 0)               */
void IFVector3_setf(_IFVector3 *pD, int i, int x);
void IFVector3_setfv3(_IFVector3 *pD, int *pS);   /* float-bits triple */
int  IFVector3_getf(_IFVector3 *pS, int n);
void IFVector3_copy(_IFVector3 *pD, _IFVector3 *pS);
int  IFVector3_is100(_IFVector3 *pS);
void IFVector4_set(_IFVector4 *pD, int x, int y, int z, int w);
void IFVector4_setxv(_IFVector4 *pD, int *pS, int n);   /* 16.16 fixed in  */
void IFVector4_setfv(_IFVector4 *pD, int *pS, int n);   /* float bits in   */
int  IFVector4_getf(_IFVector4 *pS, int n);
void IFVector4_copy(_IFVector4 *pD, _IFVector4 *pS);
void IFVector4_negate(_IFVector4 *dst, _IFVector4 *src);
int  IFVector4_getFirstNonZeroSign(_IFVector4 *v);
HGint32 IFVector4_dp4l(_IFVector4 *pA, _IFVector4 *pB); /* returns IScalar */
HGint64 IFVector4_dp4l_fast(_IFVector4 *pA, _IFVector4 *pB); /* 64-bit raw */
int  IFVector4_tov3(_IVector3 *pD, _IFVector4 *pA);      /* perspective div */
void IFVector3_tov3(_IVector3 *pD, _IFVector3 *pS);
void IFVector4_accfv(_IFVector4 *pD, _IFVector4 *pS, int *pScale, int n);

/* --- IVector3 ---------------------------------------------------------- */

void IVector3_init001p(_IVector3 *pD);          /* (0, 0, 1)               */
void IVector3_zero(_IVector3 *pD);
void IVector3_set(_IVector3 *pD, HGuint32 e, int x, int y, int z);
int  IVector3_normsub(int *pD, _IVector3 *pA, _IVector3 *pB);
int  IVector3_length(_IVector3 *pA);
int  IVector3_clipperDot(int *weights, int *distances);

/* --- IVector4 ---------------------------------------------------------- */

void IVector4_zero(_IVector4 *pD);
void IVector4_set(_IVector4 *pD, HGuint32 e, int x, int y, int z, int w);
void IVector4_set_noINF(_IVector4 *pD, HGuint32 e, int x, int y, int z, int w);
void IVector4_inf(_IVector4 *pD);
void IVector4_copy(_IVector4 *pD, _IVector4 *pS);
int  IVector4_testeq(_IVector4 *v);
int  IVector4_getf(_IVector4 *pA, int i);
void IVector4_smul(_IVector4 *pD, _IVector4 *pA, int b);
void IVector4_add(_IVector4 *pD, _IVector4 *pA, _IVector4 *pB);
HGuint32 IVector4_clip(_IVector4 *clipPos, int guardBand);
void IVector4_wNorm(int *w, _IVector4 *v0, _IVector4 *v1, _IVector4 *v2);
void IVector4_fixClipSpacePosition(_IVector4 *pD, _IVector4 *src, int nGuardbandBits);
int  IVector4_stp(_IVector4 *pA, _IVector4 *pB, _IVector4 *pC);
void IVector4_viewportTransform(int *pD, _IVector4 *pS, int *pScale,
                                int *pOffset, int xyShift);

/* --- IMatrix4x4 -------------------------------------------------------- */

void IMatrix4x4_copy(_IMatrix4x4 *dst, _IMatrix4x4 *src);
void IMatrix4x4_transpose(_IMatrix4x4 *dst, _IMatrix4x4 *src);
void IMatrix4x4_setfv(_IMatrix4x4 *pD, int *pS);        /* float bits in   */
void IMatrix4x4_getfv(int *pD, _IMatrix4x4 *pS);        /* float bits out  */
void IMatrix4x4_identity(_IMatrix4x4 *pD);
void IMatrix4x4_mul(_IMatrix4x4 *pD, _IMatrix4x4 *pA, _IMatrix4x4 *pB);
void IMatrix4x4_mulfv4(_IFVector4 *pD, _IMatrix4x4 *pM, _IFVector4 *pA);
void IMatrix4x4_frustum(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms);
void IMatrix4x4_ortho(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms);
void IMatrix4x4_scale(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms);
void IMatrix4x4_translate(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int *parms);
void IMatrix4x4_rotate(_IMatrix4x4 *pD, _IMatrix4x4 *pM, int angle, _IFVector3 *axisf);
HGint32 IMatrix4x4_det2h(_IMatrix4x4 *pA, int ca, int ra, int cb, int rb);
void IMatrix4x4_inverse(_IMatrix4x4 *pD, _IMatrix4x4 *pS, int bScale);
int  IMatrix4x4_inversetranspose(_XNormalTransform *pD, _IMatrix4x4 *matrix, int bRescale);

/* --- _IVertexTransform use (IMatrix4.c) --------------------------------- */
void IVertexTransform_copy(_IVertexTransform *pD, _IMatrix4x4 *pM);
void IVertexTransform_mulxv4(_IVector4 *pD, _IVertexTransform *pT, int *pB);
void IVertexTransform_mulfv4(_IVector4 *pD, _IVertexTransform *pT, _IFVector4 *pS);
void IVertexTransform_prepareFloat(_IFVertexTransformPlan *plan, _IVertexTransform *t, unsigned int exponent);
void IVertexTransform_mulfv4Prepared(_IVector4 *d, _IVertexTransform *t, _IFVector4 *s,
                                    const _IFVertexTransformPlan *plan);
void IVertexTransform_mulxv4s(_IFVector4 *pD, _IVertexTransform *pT, int *pB);
void IVertexTransform_mulfv4s(_IFVector4 *pD, _IVertexTransform *pT, _IFVector4 *pB);
int  IVertexTransform_zmulfv4(_IVertexTransform *pT, _IFVector4 *pS);
int  IVertexTransform_zmulxv4(_IVertexTransform *pT, int *pS);
int  IMatrix4x4_getScalar(_IMatrix4x4 *pS, int c, int r);
HGint32 I64_tof(HGint64 x);                     /* 64-bit fixed -> IScalar */

/* --- XNormalTransform (IMatrix3.c) ------------------------------------ */

void XNormalTransform_mulNormal(int *pD, _XNormalTransform *pM, int *pV, int renormalize);

/* --- light attenuation (imApi.inl; needs _IFVector3) ------------------- */

HGint32 IScalar_attenuateRcp(HGint32 d, _IFVector3 *ka);
HGint32 IScalar_attenuateRsq(HGint32 d, _IFVector3 *ka);
HGint32 IScalar_expe(HGint32 x);

HG_EXTERN_C_END

#endif /* IM_API_H */
