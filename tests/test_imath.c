/* Scalar, vector and matrix arithmetic, including exact encoded results. */
#include "imath/imApi.h"
#include "hg/hgInt32.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

#define CHECK_EQ CHECK_U32_EQ

#define FIN(x) hgFloatBitPattern(x)

static void test_construct(void)
{
    CHECK_EQ(IScalar_zero(), 0);
    CHECK_EQ(IScalar_one(), FIN(1.0f));
    CHECK_EQ(IScalar_intToScalar(5), FIN(5.0f));
    CHECK_EQ(IScalar_intToScalar(-5), FIN(-5.0f));
    CHECK_EQ(IScalar_intToScalar(0), 0);
    CHECK_EQ(IScalar_immediate(2.5f), FIN(2.5f));
    CHECK_EQ(IScalar_ftos(2.5f), FIN(2.5f));
    CHECK(IScalar_stof(FIN(3.25f)) == 3.25f);
    /* denormal flush */
    CHECK_EQ(IScalar_iftos(0x007FFFFF), 0);
    CHECK_EQ(IScalar_iftos(0x00000001), 0);
    /* NaN/Inf saturate to the largest finite value with the input sign. */
    CHECK_EQ(IScalar_iftos(0x7FC00000), (HGint32)0x7F7FFFFFu);
    CHECK_EQ(IScalar_iftos(0xFF800000), (HGint32)0xFF7FFFFFu);
    CHECK_EQ(IScalar_iftos(FIN(1.5f)), FIN(1.5f));

    CHECK_EQ(IScalar_set(129, 0x28000000), FIN(5.0f));
    CHECK_EQ(IScalar_set(127, 0x20000000), FIN(1.0f));
    CHECK_EQ(IScalar_set(126, 0x20000000), FIN(0.5f));
    CHECK_EQ(IScalar_set(127, -0x20000000), FIN(-1.0f));
    CHECK_EQ(IScalar_set(0, 0), 0);
}

static void test_accessors(void)
{
    CHECK_EQ(IScalar_getExponent(FIN(5.0f)), 129);
    CHECK_EQ(IScalar_getExponent(FIN(1.0f)), 127);
    CHECK_EQ(IScalar_getExponent(FIN(0.5f)), 126);
    /* Normalized 1.x mantissas lie in [2^29, 2^30). */
    CHECK_EQ(IScalar_getMantissa(FIN(1.0f)), 0x20000000);
    CHECK_EQ(IScalar_getMantissa(FIN(1.5f)), 0x30000000);
    CHECK_EQ(IScalar_getMantissa(FIN(-1.5f)), -0x30000000);
    CHECK_EQ(IScalar_getMantissa(FIN(0.5f)), 0x20000000);
    CHECK_EQ(IScalar_getUnsignedMantissa(FIN(1.5f)), 0x30000000u);
    CHECK_EQ(IScalar_getOESExponent(FIN(2.0f)), 128 - 140);
    CHECK_EQ(IScalar_getOESMantissa(FIN(2.0f)), 0x20000000);

    CHECK_EQ(IScalar_toUnitScalar(FIN(0.5f)), 0x10000000);
    CHECK_EQ(IScalar_toUnitScalar(FIN(-0.5f)), -0x10000000);
    CHECK_EQ(IScalar_toUnitScalar(FIN(1.5f)), 0x20000000);   /* clamp */
    CHECK_EQ(IScalar_toUnitScalar(FIN(-2.0f)), -0x20000000); /* clamp */
    CHECK_EQ(IScalar_toUnitScalar(FIN(0.25f)), 0x08000000);
}

static void test_fixed(void)
{
    /* The original SHL preserves signed word patterns before its overflow check. */
    CHECK_EQ(imLsl32OF(-1, 31), 0x80000000u);
    CHECK_EQ(imLsl32OF(-1, 0), 0xFFFFFFFFu);
    CHECK_EQ(imLsl32OF(-2, 29), 0xC0000000u);
    CHECK_EQ(imAdd32OF(-2147483647 - 1, 1), 0x80000001u);
    CHECK_EQ(imSub32OF(-2147483647, 1), 0x80000000u);
    /* fixedToScalar(m, n) = scalar(m * 2^-n) */
    CHECK_EQ(IScalar_fixedToScalar(60, 4), FIN(3.75f));
    CHECK_EQ(IScalar_fixedToScalar(-16, 4), FIN(-1.0f));
    CHECK_EQ(IScalar_fixedToScalar(-32, 4), FIN(-2.0f));
    CHECK_EQ(IScalar_fixedToScalar(0, 4), 0);
    CHECK_EQ(IScalar_fixedToScalar(1, 0), FIN(1.0f));
    CHECK_EQ(IScalar_fixedToScalar(0x80000000, 30), FIN(-2.0f)); /* INT_MIN path */

    /* Arithmetic right shifts round negative fixed-point results down. */
    CHECK_EQ(IScalar_scalarToFixed(FIN(1.0f), 0), 1);
    CHECK_EQ(IScalar_scalarToFixed(FIN(1.5f), 0), 1);
    CHECK_EQ(IScalar_scalarToFixed(FIN(3.75f), 4), 60);
    CHECK_EQ(IScalar_scalarToFixed(FIN(255.0f), 0), 255);
    CHECK_EQ(IScalar_scalarToFixed(FIN(0.5f), 1), 1);
    CHECK_EQ(IScalar_scalarToFixed(FIN(-1.5f), 0), -2);
    CHECK_EQ(IScalar_scalarToFixed(FIN(2.5f), 1), 5);

    /* byte conversion */
    CHECK_EQ(IScalar_byteToScalar(255), FIN(1.0f));
    CHECK_EQ(IScalar_byteToScalar(0), 0);
    CHECK_EQ(IScalar_byteToScalar(128), 0x3F008081u);   /* 128/255 (original carry) */
    {
        float v = IScalar_stof(IScalar_byteToScalar(200));
        CHECK(fabs(v - 200.0f / 255.0f) < 1e-6f);
    }
    CHECK_EQ(IScalar_toFixedHO(FIN(1.0f), 8), 255);
}

static void test_arith(void)
{
    CHECK_EQ(IScalar_mul(FIN(2.0f), FIN(3.0f)), FIN(6.0f));
    CHECK_EQ(IScalar_mul(FIN(-2.5f), FIN(4.0f)), FIN(-10.0f));
    CHECK_EQ(IScalar_mul(FIN(0.0f), FIN(100.0f)), FIN(0.0f));
    CHECK_EQ(IScalar_add(FIN(1.5f), FIN(2.25f)), FIN(3.75f));
    CHECK_EQ(IScalar_sub(FIN(5.0f), FIN(3.0f)), FIN(2.0f));
    CHECK_EQ(IScalar_sqr(FIN(4.0f)), FIN(16.0f));
    CHECK_EQ(IScalar_rcp(FIN(4.0f)), FIN(0.25f));
    CHECK_EQ(IScalar_rcp(FIN(1.0f)), FIN(1.0f));
    {
        /* Reciprocal estimates for non-power-of-two inputs. */
        float r = IScalar_stof(IScalar_rcp(FIN(3.0f)));
        CHECK(fabs(r - 1.0f / 3.0f) < 1e-6f);
        r = IScalar_stof(IScalar_rcp(FIN(10.0f)));
        CHECK(fabs(r - 0.1f) < 1e-6f);
    }
    {
        /* Reciprocal-square-root estimates use a polynomial approximation. */
        float r = IScalar_stof(IScalar_rsq(FIN(4.0f)));
        CHECK(fabs(r - 0.5f) < 1e-6f);
        r = IScalar_stof(IScalar_rsq(FIN(2.0f)));
        CHECK(fabs(r - 1.0f / sqrtf(2.0f)) < 1e-6f);
        r = IScalar_stof(IScalar_rsq(FIN(1.0f)));
        CHECK(fabs(r - 1.0f) < 1e-6f);
    }
    CHECK_EQ(IScalar_negate(FIN(1.5f)), FIN(-1.5f));
    CHECK_EQ(IScalar_abs(FIN(-1.5f)), FIN(1.5f));
    CHECK_EQ(IScalar_max(FIN(1.5f), FIN(2.5f)), FIN(2.5f));
    CHECK_EQ(IScalar_max(FIN(-9.0f), FIN(-2.0f)), FIN(-2.0f));
    {
        float r = IScalar_stof(IScalar_degreesToRadians(FIN(180.0f)));
        CHECK(fabs(r - 3.14159265f) < 1e-6f);
        r = IScalar_stof(IScalar_degreesToRadians(FIN(90.0f)));
        CHECK(fabs(r - 1.5707963f) < 1e-6f);
    }
    CHECK_EQ(IScalar_cos(FIN(0.0f)), FIN(1.0f));
}

static void test_compares(void)
{
    CHECK_EQ(IScalar_cmpeq(FIN(1.0f), FIN(1.0f)), 1);
    CHECK_EQ(IScalar_cmpeq(FIN(1.0f), FIN(2.0f)), 0);
    CHECK_EQ(IScalar_cmpne(FIN(1.0f), FIN(2.0f)), 1);
    CHECK_EQ(IScalar_cmplt(FIN(1.0f), FIN(2.0f)), 1);
    CHECK_EQ(IScalar_cmplt(FIN(-2.0f), FIN(-1.0f)), 1);
    CHECK_EQ(IScalar_cmpgt(FIN(2.0f), FIN(1.0f)), 1);
    CHECK_EQ(IScalar_cmple(FIN(1.0f), FIN(1.0f)), 1);
    CHECK_EQ(IScalar_cmpge(FIN(1.0f), FIN(0.5f)), 1);

    CHECK_EQ(IScalar_testeq(0), 1);
    CHECK_EQ(IScalar_testeq(FIN(1.0f)), 0);
    CHECK_EQ(IScalar_testne(FIN(1.0f)), 1);
    CHECK_EQ(IScalar_testlt(FIN(-1.0f)), 1);
    CHECK_EQ(IScalar_testlt(FIN(1.0f)), 0);
    CHECK_EQ(IScalar_testgt(FIN(1.0f)), 1);
    CHECK_EQ(IScalar_testle(FIN(0.0f)), 1);
    CHECK_EQ(IScalar_testge(FIN(0.0f)), 1);
    CHECK_EQ(IScalar_testge(0x80000000u), 1);   /* -0.0 */
}

static void test_clamp_setnorm(void)
{
    CHECK_EQ(IScalar_clampUnit(FIN(2.0f)), FIN(1.0f));
    CHECK_EQ(IScalar_clampUnit(FIN(-2.0f)), FIN(0.0f));
    CHECK_EQ(IScalar_clampUnit(FIN(0.25f)), FIN(0.25f));

    CHECK_EQ(IScalar_setAndNormalize(128, 0x30000000), FIN(3.0f));
    CHECK_EQ(IScalar_setAndNormalize(127, 0x20000000), FIN(1.0f));
}

static void test_unit_scalar(void)
{
    CHECK_EQ(IUnitScalar_maxZero(0x20000000), 0x20000000);
    CHECK_EQ(IUnitScalar_maxZero(-1), 0);

    CHECK_EQ(IUnitScalar_lerp(0, 0x20000000, 0x10000000), 0x10000000);
    CHECK_EQ(IUnitScalar_lerp(0x20000000, 0x20000000, 0x10000000), 0x20000000);
    CHECK_EQ(IUnitScalar_lerp(0x20000000, 0, 0x20000000), 0);
    CHECK_EQ(IUnitScalar_lerp(0x10000000, 0x20000000, 0x10000000), 0x18000000);

    CHECK_EQ(IUnitScalar_tos(0x10000000), FIN(0.5f));
    CHECK_EQ(IUnitScalar_tos(0x20000000), FIN(1.0f));
    CHECK_EQ(IUnitScalar_tos(-0x20000000), FIN(-1.0f));
    CHECK_EQ(IUnitScalar_tos(0), 0);
    CHECK_EQ(IUnitScalar_tos(0x08000000), FIN(0.25f));

    CHECK_EQ(IUnitScalar_isValid(0x20000000), 1);
    CHECK_EQ(IScalar_isValid(FIN(1.5f)), 1);
    CHECK_EQ(IScalar_isValid(FIN(0.25f)), 1);
    CHECK_EQ(IScalar_isValid(0), 1);
    CHECK_EQ(IScalar_isValid(0x7F800000u), 0);  /* Inf: exponent 255 */
}

static void test_log2(void)
{
    CHECK_EQ(XScalar_log2(0), (HGint32)0x80000000u);
    CHECK_EQ((HGuint32)XScalar_log2(0x80000000), 0x80000000u);
    /* result = (log2(y) - 21) << 21 */
    CHECK_EQ(XScalar_log2(1 << 30), 9 << 21);
    CHECK_EQ(XScalar_log2(1 << 20), -(1 << 21));
    {
        HGint32 r = XScalar_log2(3 << 28);
        /* log2(3<<28) - 21 = 8.58496 */
        HGint32 want = (HGint32)(8.58496f * 2097152);
        CHECK(r > want - 8 && r < want + 8);
    }
}

static void test_xscalar(void);
static void test_ivector3(void);
static void test_ivector4(void);
static void test_ifvector4(void);
static void test_inormal3(void);
static void test_imatrix(void);
static void test_affine_unit_rows(void)
{
    static const float inputs[] = {0.f, -0.25000003f, 1.f, -1.f, 1e-20f, -1e20f};
    unsigned int i, n = sizeof(inputs) / sizeof(inputs[0]);
    int row, c, bits[16] = {0};
    _IMatrix4x4 mat;
    _IVertexTransform fast, scalar;

    for (row = 0; row < 3; ++row)
    {
        memset(bits, 0, sizeof(bits));
        for (c = 0; c < 4; ++c) bits[5 * c] = FIN(1.f);
        /* Keep the tested row a unit row without taking the identity path. */
        bits[12 + (row + 1) % 3] = FIN(0.125f);
        IMatrix4x4_setfv(&mat, bits);
        IVertexTransform_copy(&fast, &mat);
        scalar = fast;
        /* Keep the affine-W bit; disable the first three unit-row classes
         * to exercise the original dot path with the exact same operands. */
        scalar.cl = (scalar.cl & 0xff000000u) | 0x00010101u;
        for (i = 0; i < n; ++i)
        {
            _IFVector4 input;
            _IVector4 fused, normal;
            int xyzw[4];
            for (c = 0; c < 4; ++c) xyzw[c] = FIN(inputs[(i + c) % n]);
            IFVector4_setfv(&input, xyzw, 4);
            IVertexTransform_mulfv4(&fused, &fast, &input);
            IVertexTransform_mulfv4(&normal, &scalar, &input);
            CHECK(memcmp(&fused, &normal, sizeof(fused)) == 0);
        }
    }
}

static void test_float_transform_plans(void)
{
    static const unsigned int exponents[] = {0, 1, 31, 63, 95, 126, 127, 128, 160, 192, 223};
    unsigned int rng = 0x193ac827u;
    unsigned int sample, component, variant;
    for (sample = 0; sample < 256; ++sample)
    {
        _IMatrix4x4 matrix;
        _IVertexTransform transform;
        _IFVertexTransformPlan plan;
        _IFVector4 input;
        int bits[16];
        for (component = 0; component < 16; ++component)
        {
            rng = rng * 1664525u + 1013904223u;
            bits[component] = (int)((rng & 0x807fffffu) | ((112u + (rng >> 25)) << 23));
            if (sample % 4 == 0 && component % 5 != 0) bits[component] = 0;
            if (sample % 4 == 1 && component % 4 == 3)
                bits[component] = component == 15 ? FIN(1.0f) : 0;
        }
        if (sample == 0)
            IMatrix4x4_identity(&matrix);
        else
            IMatrix4x4_setfv(&matrix, bits);
        IVertexTransform_copy(&transform, &matrix);
        input.e = 0;
        for (component = 0; component < 4; ++component)
            input.e |= exponents[(sample + component * 3) % 11] << (component * 8);
        IVertexTransform_prepareFloat(&plan, &transform, input.e);
        for (variant = 0; variant < 8; ++variant)
        {
            _IVector4 expected, actual;
            _IFVector4 alias;
            for (component = 0; component < 4; ++component)
            {
                rng = rng * 1664525u + 1013904223u;
                input.m[component] = (int)(0x20000000u | (rng & 0x1fffffffu));
                if (rng & 0x80000000u) input.m[component] = -input.m[component];
                if (((input.e >> (component * 8)) & 255u) == 0) input.m[component] = 0;
            }
            IVertexTransform_mulfv4(&expected, &transform, &input);
            IVertexTransform_mulfv4Prepared(&actual, &transform, &input, &plan);
            CHECK(memcmp(&expected, &actual, sizeof(actual)) == 0);
            alias = input;
            IVertexTransform_mulfv4Prepared((_IVector4 *)&alias, &transform, &alias, &plan);
            CHECK(memcmp(&expected, &alias, sizeof(expected)) == 0);
        }
    }
    /* Cancellation must clear the row exponent even when the cached plan
     * was prepared from the largest finite input exponent. */
    for (sample = 0; sample < 2; ++sample)
    {
        _IMatrix4x4 matrix;
        _IVertexTransform transform;
        _IFVertexTransformPlan plan;
        _IFVector4 input;
        _IVector4 actual, expected;
        int bits[16];
        for (component = 0; component < 16; ++component)
            bits[component] = component < 8 ? FIN(1.0f) : FIN(-1.0f);
        IMatrix4x4_setfv(&matrix, bits);
        IVertexTransform_copy(&transform, &matrix);
        input.e = sample ? 0xfefefefeu : 0x01010101u;
        for (component = 0; component < 4; ++component)
            input.m[component] = 0x20000000;
        IVertexTransform_prepareFloat(&plan, &transform, input.e);
        IVertexTransform_mulfv4(&expected, &transform, &input);
        IVertexTransform_mulfv4Prepared(&actual, &transform, &input, &plan);
        CHECK(memcmp(&expected, &actual, sizeof(actual)) == 0);
        CHECK(actual.e == 0);
        CHECK((actual.m[0] | actual.m[1] | actual.m[2] | actual.m[3]) == 0);
    }
}

static int check_common_transform(_IVertexTransform *transform, _ICommonVertexTransform *plan,
                                  _IFVector4 *input)
{
    _IVector4 expected, actual, untouched;
    _IFVector4 alias;
    unsigned int inputExponent = 0;
    int component, accepted, scale;
    memset(&actual, 0xa5, sizeof(actual));
    untouched = actual;
    accepted = IVertexTransform_mulfv4Common(&actual, plan, input);
    if (!accepted)
    {
        CHECK(memcmp(&actual, &untouched, sizeof(actual)) == 0);
        return 0;
    }
    IVertexTransform_mulfv4(&expected, transform, input);
    CHECK_EQ(IVector4_clip(&actual, 2), IVector4_clip(&expected, 2));
    CHECK(IVector4_isValid(&actual));
    for (component = 0; component < 4; ++component)
    {
        unsigned int exponent = (input->e >> (8 * component)) & 255u;
        if (exponent > inputExponent) inputExponent = exponent;
    }
    scale = plan->exponent + (int)inputExponent - 124;
    for (component = 0; component < 4; ++component)
        CHECK(fabs(ldexp(expected.m[component], (int)expected.e - scale) -
                   ldexp(actual.m[component], (int)actual.e - scale)) < IM_COMMON_TRANSFORM_ERROR);
    for (component = 0; component < 2; ++component)
        CHECK(fabs((double)actual.m[component] / actual.m[3] -
                   (double)expected.m[component] / expected.m[3]) <
              40.0 / (IM_COMMON_TRANSFORM_MIN_W - IM_COMMON_TRANSFORM_ERROR));
    alias = *input;
    CHECK(IVertexTransform_mulfv4Common((_IVector4 *)&alias, plan, &alias));
    CHECK(memcmp(&alias, &actual, sizeof(actual)) == 0);
    return 1;
}

static void test_common_scale_transforms(void)
{
    _IMatrix4x4 matrix;
    _IVertexTransform transform;
    _ICommonVertexTransform plan;
    _IFVector4 input;
    unsigned int rng = 0x9ead0283u, sample, component;
    int accepted = 0, matrixBits[16], inputBits[4];
    for (sample = 0; sample < 4096; ++sample)
    {
        unsigned int base = sample < 1024 ? 127 : 17 + sample % 238;
        for (component = 0; component < 16; ++component)
        {
            rng = rng * 1664525u + 1013904223u;
            matrixBits[component] = (int)((rng & 0x807fffffu) | ((base - (rng >> 28)) << 23));
            if (sample % 3 == 0 && component % 5) matrixBits[component] = 0;
        }
        IMatrix4x4_setfv(&matrix, matrixBits);
        IVertexTransform_copy(&transform, &matrix);
        IVertexTransform_prepareCommon(&plan, &transform);
        for (component = 0; component < 4; ++component)
        {
            rng = rng * 1664525u + 1013904223u;
            inputBits[component] = (int)((rng & 0x807fffffu) | ((127 - (rng >> 28)) << 23));
        }
        IFVector4_setfv(&input, inputBits, 4);
        accepted += check_common_transform(&transform, &plan, &input);
    }
    CHECK(accepted > 200);

    /* Every float exponent, including subnormal and non-finite encodings. */
    for (sample = 0; sample < 256; ++sample)
    {
        for (component = 0; component < 16; ++component)
            matrixBits[component] = component % 5 == 0 ? FIN(0.75f) : FIN(0.0625f);
        IMatrix4x4_setfv(&matrix, matrixBits);
        IVertexTransform_copy(&transform, &matrix);
        IVertexTransform_prepareCommon(&plan, &transform);
        for (component = 0; component < 4; ++component)
            inputBits[component] = (int)((sample << 23) | (component * 0x190003u));
        IFVector4_setfv(&input, inputBits, 4);
        check_common_transform(&transform, &plan, &input);
        inputBits[0] ^= (int)0x80000000u;
        IFVector4_setfv(&input, inputBits, 4);
        check_common_transform(&transform, &plan, &input);
    }

    /* Clip boundaries, cancellation and ineligible matrices preserve the fallback. */
    memset(matrixBits, 0, sizeof(matrixBits));
    matrixBits[0] = matrixBits[5] = matrixBits[10] = FIN(0.5f);
    matrixBits[15] = FIN(1.0f);
    IMatrix4x4_setfv(&matrix, matrixBits);
    IVertexTransform_copy(&transform, &matrix);
    IVertexTransform_prepareCommon(&plan, &transform);
    inputBits[0] = FIN(0.5f); inputBits[1] = FIN(0.25f);
    inputBits[2] = FIN(0.75f); inputBits[3] = FIN(1.0f);
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(check_common_transform(&transform, &plan, &input));
    for (component = 0; component < 3; ++component)
    {
        int saved = inputBits[component];
        inputBits[component] = FIN(2.0f);
        IFVector4_setfv(&input, inputBits, 4);
        CHECK(!check_common_transform(&transform, &plan, &input));
        inputBits[component] = saved;
    }
    inputBits[0] = FIN(8.0f);
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(!check_common_transform(&transform, &plan, &input));
    inputBits[0] = FIN(0.5f); inputBits[3] = FIN(-1.0f);
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(!check_common_transform(&transform, &plan, &input));
    inputBits[3] = 0x7f800000;
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(!check_common_transform(&transform, &plan, &input));
    inputBits[3] = 0x7fc12345;
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(!check_common_transform(&transform, &plan, &input));
    inputBits[3] = FIN(1.0f);
    matrixBits[3] = FIN(-2.0f);
    IMatrix4x4_setfv(&matrix, matrixBits);
    IVertexTransform_copy(&transform, &matrix);
    IVertexTransform_prepareCommon(&plan, &transform);
    IFVector4_setfv(&input, inputBits, 4);
    CHECK(!check_common_transform(&transform, &plan, &input));
    matrixBits[3] = FIN(0x1p40f);
    IMatrix4x4_setfv(&matrix, matrixBits);
    IVertexTransform_copy(&transform, &matrix);
    IVertexTransform_prepareCommon(&plan, &transform);
    CHECK(!plan.eligible);
    CHECK(!check_common_transform(&transform, &plan, &input));
}

static void test_attenuate(void);

int main(void)
{
    test_construct();
    test_accessors();
    test_fixed();
    test_arith();
    test_compares();
    test_clamp_setnorm();
    test_unit_scalar();
    test_log2();
    test_xscalar();
    test_ivector3();
    test_ivector4();
    test_ifvector4();
    test_inormal3();
    test_imatrix();
    test_attenuate();
    test_affine_unit_rows();
    test_float_transform_plans();
    test_common_scale_transforms();

    printf("imath tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

/* ================= vector / matrix layer ================= */

static void test_xscalar(void)
{
    /* log2/exp2 roundtrip: XScalar domain is 10.21 */
    {
        int i;
        for (i = 1; i <= 20; ++i)
        {
            HGint32 x = i * 2097152 / 4;        /* 0.25 .. 5.0 in 10.21 */
            HGint32 l = XScalar_log2(x);
            HGint32 r = XScalar_exp2(l);
            CHECK(abs(r - x) <= 128);
        }
    }
    /* exp2(0) = 1.0 (10.21) */
    CHECK_EQ(XScalar_exp2(0), 2097152);
    CHECK_EQ(XScalar_exp2(0x1FFFFFFF), 0x7FFFFFFF);
    CHECK_EQ(imLsl32s(-32769, 1), -65538);
    CHECK_EQ(imLsl32s(-32769, 16), 0x80000000u);
    CHECK_EQ(imLsl32s(32768, 16), 0x7FFFFFFF);
    CHECK_EQ(imLsl32s(-1, 31), 0x80000000u);
    CHECK_EQ(imLsl32us(0xFFFF7FFFu, 31), 0x7FFFFFFF);
    CHECK_EQ(imLsl32us(0x3FFFFFFFu, 1), 0x7FFFFFFE);
    CHECK_EQ(imLsl32us(0x40000000u, 1), 0x7FFFFFFF);
    CHECK_EQ(IScalar_toFixedHO(0, 31), 0);
    CHECK_EQ(IScalar_toFixedHO(FIN(1.0f), 31), 0x7FFFFF80);
    /* mul: (a*b)>>10 in 10.21 domain */
    CHECK_EQ(XScalar_mul(2 * 2097152, 3 * 2097152), 6 * 2097152);
    {
        HGint32 big = (HGint32)0x40000000;
        CHECK_EQ(XScalar_mul_sat(big, big), 0x7FFFFFFF);   /* saturates */
    }
    CHECK_EQ(XScalar_add(5, 7), 12);
    CHECK_EQ(XScalar_sub(5, 7), -2);
    CHECK_EQ(XScalar_abs(-5), 5);
    CHECK_EQ(XScalar_abs((HGint32)0x80000000u), 0x7FFFFFFF);
    CHECK_EQ(XScalar_min(3, 9), 3);
    CHECK_EQ(XScalar_cmplt(1, 2), 1);
    CHECK_EQ(XScalar_cmple(2, 2), 1);
    CHECK_EQ(XScalar_cmpne(1, 2), 1);
    CHECK_EQ(XScalar_zcmpge(0), 1);
    CHECK_EQ(XScalar_zcmpgt(0), 0);
    CHECK_EQ(XScalar_zcmplt(-1), 1);
    CHECK_EQ(XScalar_zcmpeq(0), 1);
    CHECK_EQ(XScalar_zcmpne(1), 1);
    CHECK_EQ(XScalar_signBit(-1), 1);
    CHECK_EQ(XScalar_signBit(1), 0);
    CHECK_EQ(XScalar_tous(32 * 65535), 65535);
    /* pow(2, 3) = 8 (10.21) */
    {
        HGint32 r = XScalar_pow(2 * 2097152, 3 * 2097152);
        CHECK(abs(r - 8 * 2097152) <= 16);
    }
    /* IScalar_expe: exp(0) = 1, exp(1) = e */
    CHECK_EQ(IScalar_expe(0), 2097152);
    {
        float v = (float)IScalar_expe(FIN(1.0f)) / 2097152.0f;
        CHECK(fabs(v - 2.7182818f) < 1e-3f);
    }
}

static void test_ivector3(void)
{
    _IVector3 v, a, b;
    int n[3];
    HGint32 len;

    IVector3_zero(&v);
    CHECK_EQ(v.e, 0);
    CHECK_EQ(IVector3_isValid(&v), 1);
    IVector3_init001p(&v);
    CHECK_EQ(v.e, 127);
    CHECK_EQ(v.m[2], IUNITSCALAR_ONE);

    /* length (1,0,0) = 1.0 */
    IVector3_set(&v, 127, IUNITSCALAR_ONE, 0, 0);
    CHECK_EQ(IVector3_length(&v), FIN(1.0f));
    /* length (0.5,0,0) = 0.5 */
    IVector3_set(&v, 126, IUNITSCALAR_ONE, 0, 0);
    CHECK_EQ(IVector3_length(&v), FIN(0.5f));
    /* length (1,1,1) = sqrt(3) (polynomial estimate) */
    IVector3_set(&v, 127, IUNITSCALAR_ONE, IUNITSCALAR_ONE, IUNITSCALAR_ONE);
    len = IVector3_length(&v);
    CHECK(fabs(IScalar_stof(len) - 1.7320508f) < 2e-5f);

    /* normsub: normalize((2,0,0) - (1,0,0)) = (1,0,0) in 10.21 */
    IVector3_set(&a, 128, IUNITSCALAR_ONE, 0, 0);  /* 2.0 */
    IVector3_set(&b, 127, IUNITSCALAR_ONE, 0, 0);  /* 1.0 */
    CHECK_EQ(IVector3_normsub(n, &a, &b), FIN(1.0f));
    CHECK(abs(n[0] - 2097152) <= 2);
    CHECK_EQ(n[1], 0);
    CHECK_EQ(n[2], 0);
    /* normsub zero diff -> zero vector, zero scalar */
    CHECK_EQ(IVector3_normsub(n, &b, &b), 0);
    CHECK_EQ(n[0], 0);
}

static void test_ivector4(void)
{
    _IVector4 a, b, d, c;
    /* (1, 2, 3, 4): shared exponent 129, mantissa = x * 2^27 */
    HGuint32 e = 129u;
    IVector4_set(&a, e, 0x08000000, 0x10000000, 0x18000000, 0x20000000);
    CHECK_EQ(IVector4_isValid(&a), 1);
    CHECK(fabs(IScalar_stof(IVector4_getf(&a, 0)) - 1.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IVector4_getf(&a, 2)) - 3.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IVector4_getf(&a, 3)) - 4.0f) < 1e-6f);

    /* (4, 2, 4, 2) + (1, 2, 3, 4) = (5, 4, 7, 6) */
    IVector4_set(&b, e, 0x20000000, 0x10000000, 0x20000000, 0x10000000);
    IVector4_add(&d, &a, &b);
    CHECK(fabs(IScalar_stof(IVector4_getf(&d, 0)) - 5.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IVector4_getf(&d, 1)) - 4.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IVector4_getf(&d, 3)) - 6.0f) < 1e-6f);

    IVector4_smul(&d, &a, FIN(2.0f));
    CHECK(fabs(IScalar_stof(IVector4_getf(&d, 0)) - 2.0f) < 1e-5f);
    CHECK(fabs(IScalar_stof(IVector4_getf(&d, 3)) - 8.0f) < 1e-5f);

    CHECK_EQ(IVector4_testeq(&a), 0);
    IVector4_zero(&d);
    CHECK_EQ(IVector4_testeq(&d), 1);

    /* set saturates an overflowing exponent to the largest finite value. */
    IVector4_set(&d, 255u, 0x20000000, 0, 0, 0);
    CHECK_EQ(d.e, 254u);
    CHECK_EQ(d.m[0], 0x3FFFFFFF);
    IVector4_set_noINF(&d, 127u, 0x20000000, 0, 0, 0);
    CHECK_EQ(d.e, 127u);
    CHECK_EQ(d.m[0], 0x20000000);

    /* clip: x > w sets bits 7 (main) + 3 (guard at guardBits=0).
     * NOTE: set() takes an UNPACKED single-byte exponent here; a packed
     * exponent word > 0xFE triggers finite saturation. */
    IVector4_set(&c, 128u, 0x20000000, 0x10000000, 0x10000000, 0x10000000);
    CHECK_EQ(IVector4_clip(&c, 0), 0x88u);      /* x=2 > w=1: main+guard */
    /* (1, w=4): x < w -> no bit */
    IVector4_set(&c, 129u, 0x08000000, 0, 0, 0x20000000);
    CHECK_EQ(IVector4_clip(&c, 0), 0u);         /* x=1 < w=4 */
    IVector4_set(&c, 128u, -0x30000000, 0x10000000, 0x10000000, 0x10000000);
    CHECK_EQ(IVector4_clip(&c, 0), 0x44u);      /* x=-3 < -w */

    /* viewportTransform with all-zero coords = offsets */
    {
        int out[3];
        int scale[3] = { 1 << 16, 1 << 16, 1 << 16 };
        int offset[3] = { 320 << 16, 240 << 16, 5 << 16 };
        IVector4_zero(&c);
        IVector4_viewportTransform(out, &c, scale, offset, 0);
        CHECK_EQ(out[0], 320 << 16);
        CHECK_EQ(out[1], 240 << 16);
        CHECK_EQ(out[2], 5 << 16);
    }
    /* Unit W must keep the biased reciprocal, including at pixel ties. */
    {
        int out[3], scale[3] = {81920, -61440, 0x1fffffff};
        int offset[3] = {81920, 61440, 0x1fffffff};
        HGint64 rr = imRcpu(0x20000004u);
        int recip = hgGet64h(rr), step = hgGet64l(rr) - 1;
        IVector4_set(&c, 127u, 0x10000000, -0x10000000, 0x0fffffff, IUNITSCALAR_ONE);
        IVector4_viewportTransform(out, &c, scale, offset, 8);
        for (int k = 0; k < 2; ++k)
        {
            int v = hgLsl64h_0_31(hgMul64(c.m[k], recip), step);
            CHECK_EQ(out[k], (offset[k] + hgMul64h(v, scale[k])) * 256);
        }
        int z = hgMul64h(c.m[2] * 2, recip);
        CHECK_EQ(out[2], offset[2] + hgLsl64h_0_31(hgMul64(z, scale[2]), 3));
    }
    /* fixClipSpacePosition: w <= 0 path */
    {
        _IVector4 s, o;
        IVector4_set(&s, 127u, 0x20000000, 0, 0, 0);
        IVector4_fixClipSpacePosition(&o, &s, 4);
        CHECK_EQ(o.e, 0x7Fu);
        CHECK_EQ(o.m[2], -536870912);
    }
}

static void test_ifvector4(void)
{
    _IFVector4 f, g;
    int vals[1];

    IFVector4_set(&f, FIN(1.5f), FIN(-2.5f), FIN(0.25f), FIN(1.0f));
    CHECK_EQ(IScalar_cmpne(IFVector4_getf(&f, 0), FIN(1.5f)), 0);
    CHECK_EQ(IScalar_cmpne(IFVector4_getf(&f, 1), FIN(-2.5f)), 0);
    CHECK_EQ(IScalar_cmpne(IFVector4_getf(&f, 2), FIN(0.25f)), 0);
    CHECK_EQ(IScalar_cmpne(IFVector4_getf(&f, 3), FIN(1.0f)), 0);
    {
        _IVector3 divided;
        int i;
        IFVector4_set(&f, FIN(1.75f), FIN(1.75f), FIN(1.75f), FIN(0.75f));
        CHECK_EQ(IFVector4_tov3(&divided, &f), 1);
        CHECK_EQ(divided.e, 128);
        for (i = 0; i < 3; ++i)
            CHECK_EQ(divided.m[i], 0x25555530);
        IFVector4_set(&f, FIN(-1.75f), FIN(-1.75f), FIN(-1.75f), FIN(0.75f));
        CHECK_EQ(IFVector4_tov3(&divided, &f), 1);
        CHECK_EQ(divided.e, 128);
        for (i = 0; i < 3; ++i)
            CHECK_EQ(divided.m[i], -0x25555530);
    }

    /* setfv from float bits */
    {
        int src[4];
        src[0] = FIN(3.5f); src[1] = FIN(-1.0f); src[2] = 0; src[3] = FIN(1.0f);
        IFVector4_setfv(&f, src, 4);
        CHECK(fabs(IScalar_stof(IFVector4_getf(&f, 0)) - 3.5f) < 1e-6f);
        CHECK(fabs(IScalar_stof(IFVector4_getf(&f, 1)) + 1.0f) < 1e-6f);
        CHECK_EQ(IScalar_stof(IFVector4_getf(&f, 2)), 0);
        /* w default is 1.0 even when n < 4 */
        src[0] = FIN(2.0f);
        IFVector4_setfv(&f, src, 1);
        CHECK(fabs(IScalar_stof(IFVector4_getf(&f, 3)) - 1.0f) < 1e-6f);
    }
    /* setxv from 16.16 fixed */
    vals[0] = 2 << 16;
    IFVector4_setxv(&f, vals, 1);
    CHECK(fabs(IScalar_stof(IFVector4_getf(&f, 0)) - 2.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IFVector4_getf(&f, 3)) - 1.0f) < 1e-6f);

    IFVector4_negate(&g, &f);
    CHECK(fabs(IScalar_stof(IFVector4_getf(&g, 0)) + 2.0f) < 1e-6f);
    CHECK_EQ(IFVector4_getFirstNonZeroSign(&g), -1);

    /* IFVector3_setfv3 */
    {
        _IFVector3 v3;
        int src[3];
        src[0] = FIN(1.0f); src[1] = FIN(-0.5f); src[2] = FIN(0.25f);
        IFVector3_setfv3(&v3, src);
        CHECK(fabs(IScalar_stof(IFVector3_getf(&v3, 0)) - 1.0f) < 1e-6f);
        CHECK(fabs(IScalar_stof(IFVector3_getf(&v3, 1)) + 0.5f) < 1e-6f);
        CHECK(fabs(IScalar_stof(IFVector3_getf(&v3, 2)) - 0.25f) < 1e-6f);
    }
}

static void test_inormal3(void)
{
    int n[3], m[3];

    XNormal3_sets(n, -32768, -1, 32767);
    CHECK_EQ(n[0], -2097152);
    CHECK_EQ(n[1], -64);
    CHECK_EQ(n[2], 2097088);

    /* normalize (3,4,0) -> (0.6, 0.8, 0) in 10.21 */
    n[0] = 3 * 2097152; n[1] = 4 * 2097152; n[2] = 0;
    XNormal3_normalize(m, n);
    CHECK(abs(m[0] - (int)(0.6f * 2097152)) <= 8);
    CHECK(abs(m[1] - (int)(0.8f * 2097152)) <= 8);
    CHECK_EQ(m[2], 0);
    /* dot of unit vectors */
    CHECK(abs(XNormal3_dot(m, m) - 2097152) <= 32);
    /* setf: 10.21 outputs */
    XNormal3_setf(n, 0.0f, 0.0f, -0.5f);
    CHECK_EQ(n[2], -1048576);
    XNormal3_init001p(m);
    CHECK_EQ(m[2], 0x200000);
    XNormal3_init001n(m);
    CHECK_EQ(m[2], -0x200000);
    /* negate/copy */
    XNormal3_copy(m, n);
    CHECK_EQ(m[0], n[0]);
    XNormal3_negate(m, n);
    CHECK_EQ(m[0], -n[0]);
    /* accfv: sum of scaled normals */
    {
        int mat[2][3] = { { 2097152, 0, 0 }, { 0, 2097152, 0 } };
        int w[2];
        w[0] = FIN(2.0f);
        w[1] = FIN(3.0f);
        XNormal3_accfv(m, mat, w, 2);
        CHECK(m[0] != 0 || m[1] != 0);
    }
}

static void test_imatrix(void)
{
    _IMatrix4x4 m, a, b;
    float fv[16];
    int i;

    /* identity */
    IMatrix4x4_identity(&m);
    for (i = 0; i < 4; ++i)
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&m, i, i)) - 1.0f) < 1e-6f);
    CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&m, 0, 1))) < 1e-6f);

    /* setfv/getfv roundtrip */
    {
        int bits[16], k;
        for (k = 0; k < 16; ++k)
            bits[k] = FIN((float)(k + 1) * 0.25f);
        IMatrix4x4_setfv(&m, bits);
        IMatrix4x4_getfv((int *)fv, &m);
        for (k = 0; k < 16; ++k)
            CHECK(fabs(fv[k] - (float)(k + 1) * 0.25f) < 1e-6f);
    }
    /* transpose twice = original */
    IMatrix4x4_transpose(&a, &m);
    IMatrix4x4_transpose(&b, &a);
    IMatrix4x4_getfv((int *)fv, &b);
    for (i = 0; i < 16; ++i)
        CHECK(fabs(fv[i] - (float)(i + 1) * 0.25f) < 1e-6f);

    /* mul with identity = same */
    IMatrix4x4_identity(&b);
    IMatrix4x4_mul(&a, &m, &b);
    IMatrix4x4_getfv((int *)fv, &a);
    for (i = 0; i < 16; ++i)
        CHECK(fabs(fv[i] - (float)(i + 1) * 0.25f) < 1e-5f);

    /* translate: identity * T(10,20,30) */
    {
        int parms[3];
        parms[0] = FIN(10.0f); parms[1] = FIN(20.0f); parms[2] = FIN(30.0f);
        IMatrix4x4_identity(&a);
        IMatrix4x4_translate(&b, &a, parms);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 0)) - 10.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 1)) - 20.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 2)) - 30.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 3)) - 1.0f) < 1e-5f);
    }
    /* scale: identity * S(2,3,4) */
    {
        int parms[3];
        parms[0] = FIN(2.0f); parms[1] = FIN(3.0f); parms[2] = FIN(4.0f);
        IMatrix4x4_identity(&a);
        IMatrix4x4_scale(&b, &a, parms);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 0)) - 2.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 1, 1)) - 3.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) - 4.0f) < 1e-5f);
    }
    /* ortho(0,2,0,2,0,2): x/y map to x/y - 1; z maps to -z - 1. */
    {
        int parms[6];
        parms[0] = FIN(0.0f); parms[1] = FIN(2.0f);
        parms[2] = FIN(0.0f); parms[3] = FIN(2.0f);
        parms[4] = FIN(0.0f); parms[5] = FIN(2.0f);
        IMatrix4x4_identity(&a);
        IMatrix4x4_ortho(&b, &a, parms);
        /* getScalar(c, r) = element (row r, col c); column-major storage */
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 0)) - 1.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) + 1.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 0)) + 1.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 2)) + 1.0f) < 1e-5f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 3)) - 1.0f) < 1e-5f);
    }
    /* Scaling across a mantissa boundary must renormalize both signs. */
    {
        int factors[3] = {FIN(1.5f), FIN(-1.5f), FIN(1.5f)};
        IMatrix4x4_identity(&a);
        IMatrix4x4_scale(&a, &a, factors);
        factors[1] = FIN(1.5f);
        IMatrix4x4_scale(&b, &a, factors);
        for (i = 0; i < 3; ++i)
            CHECK(fabsf(IScalar_stof(IMatrix4x4_getScalar(&b, i, i)) -
                        (i == 1 ? -2.25f : 2.25f)) < 1e-6f);
    }
    /* rotate 0 deg = identity; 180 about z = diag(-1,-1,1);
     * 90 about z: R[1][0]=1, R[0][1]=-1 (right-handed) */
    {
        _IFVector3 axis;
        IFVector3_setf(&axis, 0, FIN(0.0f));
        IFVector3_setf(&axis, 1, FIN(0.0f));
        IFVector3_setf(&axis, 2, FIN(1.0f));
        IMatrix4x4_identity(&a);
        IMatrix4x4_rotate(&b, &a, FIN(0.0f), &axis);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 0)) - 1.0f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) - 1.0f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 1, 0))) < 1e-4f);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(IMatrix4x4_getScalar(&b, i, 3), i == 3 ? FIN(1.0f) : 0);
        IMatrix4x4_rotate(&b, &a, FIN(180.0f), &axis);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 0)) + 1.0f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 1, 1)) + 1.0f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) - 1.0f) < 1e-4f);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(IMatrix4x4_getScalar(&b, i, 3), i == 3 ? FIN(1.0f) : 0);
        IMatrix4x4_rotate(&b, &a, FIN(90.0f), &axis);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 1)) - 1.0f) < 1e-4f);  /* R[1][0] */
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 1, 0)) + 1.0f) < 1e-4f);  /* R[0][1] */
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) - 1.0f) < 1e-4f);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(IMatrix4x4_getScalar(&b, i, 3), i == 3 ? FIN(1.0f) : 0);
    }
    /* inverse of diag(2,2,2,1) with rescale = diag(0.5,0.5,0.5,1) */
    {
        int bits[16];
        memset(bits, 0, sizeof(bits));
        bits[0] = FIN(2.0f); bits[5] = FIN(2.0f);
        bits[10] = FIN(2.0f); bits[15] = FIN(1.0f);
        IMatrix4x4_setfv(&a, bits);
        IMatrix4x4_inverse(&b, &a, 1);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 0, 0)) - 0.5f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 2, 2)) - 0.5f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 3, 3)) - 1.0f) < 1e-4f);
        CHECK(fabs(IScalar_stof(IMatrix4x4_getScalar(&b, 1, 0))) < 1e-4f);
    }
    /* A shear has nonzero cofactors with alternating signs. Verify the
     * complete inverse and the unscaled adjugate (the determinant is one). */
    {
        const float shear[16] = {
            1, 2, 0, 0, 3, 7, 0, 0, 0, 0, 1, 0, 5, 11, 0, 1
        };
        const float inverse[16] = {
            7, -2, 0, 0, -3, 1, 0, 0, 0, 0, 1, 0, -2, -1, 0, 1
        };
        int bits[16], scale;
        for (i = 0; i < 16; ++i)
            bits[i] = FIN(shear[i]);
        IMatrix4x4_setfv(&a, bits);
        for (scale = 0; scale <= 1; ++scale)
        {
            IMatrix4x4_inverse(&b, &a, scale);
            IMatrix4x4_getfv((int *)fv, &b);
            for (i = 0; i < 16; ++i)
                CHECK(fabsf(fv[i] - inverse[i]) < 1e-5f);
        }
    }
    /* Equal nonzero determinant terms cancel to scalar zero. */
    {
        int bits[16] = {0};
        bits[0] = FIN(2.0f); bits[1] = FIN(3.0f);
        bits[4] = FIN(4.0f); bits[5] = FIN(6.0f);
        bits[10] = FIN(1.0f); bits[15] = FIN(1.0f);
        IMatrix4x4_setfv(&a, bits);
        CHECK_EQ(IMatrix4x4_det2h(&a, 0, 0, 1, 1), 0);
    }
    /* Destination may alias the right operand of a matrix product. Each
     * output row must still use the original complete input column. */
    {
        const float left[16] = {
            1, 2, 0, 0, 3, 4, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
        };
        const float right[16] = {
            5, 6, 0, 0, 7, 8, 0, 0, 0, 0, 1, 0, 9, 10, 0, 1
        };
        const float product[16] = {
            23, 34, 0, 0, 31, 46, 0, 0, 0, 0, 1, 0, 39, 58, 0, 1
        };
        int bits[16];
        for (i = 0; i < 16; ++i)
            bits[i] = FIN(left[i]);
        IMatrix4x4_setfv(&a, bits);
        for (i = 0; i < 16; ++i)
            bits[i] = FIN(right[i]);
        IMatrix4x4_setfv(&b, bits);
        IMatrix4x4_mul(&b, &a, &b);
        IMatrix4x4_getfv((int *)fv, &b);
        for (i = 0; i < 16; ++i)
            CHECK(fabsf(fv[i] - product[i]) < 1e-5f);
    }
    /* A rotation preserves its inverse transpose; inspect the encoded columns. */
    {
        _IFVector3 axis;
        _XNormalTransform nt;
        IFVector3_setf(&axis, 0, FIN(0.0f));
        IFVector3_setf(&axis, 1, FIN(0.0f));
        IFVector3_setf(&axis, 2, FIN(1.0f));
        IMatrix4x4_identity(&a);
        IMatrix4x4_rotate(&a, &a, FIN(90.0f), &axis);
        CHECK_EQ(IMatrix4x4_inversetranspose(&nt, &a, 1), 1);
        /* Normal-transform entries represent m * 2^(scale - 32). */
        CHECK(abs(nt.m[0][1] - 0x7FFFFF80) <= 0x20000);
        CHECK(abs(nt.m[1][0] + 0x7FFFFF80) <= 0x20000);
        CHECK(abs(nt.m[2][2] - 0x7FFFFF00) <= 0x20000);
        CHECK(nt.m[0][0] < 0x10000);
    }
    /* The fixed z-only path feeds fog. Exercise distinct packed shift bytes,
     * non-unit homogeneous coordinates, and both signs of the accumulated Z. */
    {
        _IVertexTransform transform;
        int bits[16] = {0};
        int positions[4][4] = {
            {0x8000, -0x4000, 0x20000, 0x10000},
            {0x8000, -0x4000, 0x20000, 0x20000},
            {-0x8000, 0x4000, 0x20000, -0x10000},
            {0x40000, 0, 0, -0x10000}
        };
        const float expected[4] = {7.0f, 15.0f, -9.0f, 0.0f};
        bits[0] = bits[5] = bits[15] = FIN(1.0f);
        bits[2] = FIN(2.0f);
        bits[6] = FIN(4.0f);
        bits[10] = FIN(-0.5f);
        bits[14] = FIN(8.0f);
        IMatrix4x4_setfv(&a, bits);
        IVertexTransform_copy(&transform, &a);
        for (i = 0; i < 4; ++i)
            CHECK_EQ(IVertexTransform_zmulxv4(&transform, positions[i]), FIN(expected[i]));
    }
    /* I64_tof: input scale is 2^50; inputs >= 2^51 hit the driver's own
     * "e <= 127" boundary assert, so stay below it */
    CHECK_EQ(I64_tof(0), 0);
    CHECK_EQ(I64_tof((HGint64)1 << 50), FIN(1.0f));
    CHECK_EQ(I64_tof((HGint64)3 << 49), FIN(1.5f));
    CHECK_EQ(I64_tof(-((HGint64)1 << 50)), FIN(-1.0f));
}

static void test_attenuate(void)
{
    _IFVector3 ka;
    HGint32 r;

    static const struct {
        int coefficient;
        float distance;
        int expected, tolerance;
    } cases[] = {
        {0, 4.0f, 2097152, 8},
        {0, 1.0f, 2097152, 8},
        {1, 4.0f, 2097152 / 4, 64},
        {2, 2.0f, 2097152 / 4, 64},
        /* Tiny attenuation overflows the reciprocal and saturates. */
        {1, 1e-9f, 0x7FFFFFFF, 0},
        {0, 1e30f, 2097152, 8}
    };
    unsigned int i;
    int component;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        for (component = 0; component < 3; ++component)
            IFVector3_setf(&ka, component,
                          component == cases[i].coefficient ? FIN(1.0f) : 0);
        r = IScalar_attenuateRcp(FIN(cases[i].distance), &ka);
        CHECK(llabs((long long)r - cases[i].expected) <= cases[i].tolerance);
    }
    /* attenuateRsq with ka=(1,0,0): 1/sqrt(1) = 1.0 */
    r = IScalar_attenuateRsq(FIN(4.0f), &ka);
    CHECK(fabs(IScalar_stof(r) - 1.0f) < 1e-4f);
}
