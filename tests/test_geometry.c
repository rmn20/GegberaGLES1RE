#include "gerbera/grState.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

static void setup_directional_light(Gerbera_s *state, int index)
{
    _IFVector4 position;
    _XColor3 diffuse = {0x200000, 0x200000, 0x200000};

    IFVector4_set(&position, 0, 0, IScalar_one(), 0);
    grSetLightPosition(state, index, &position);
    grSetLightSpotCutoff(state, index, IScalar_immediate(180.0f));
    grSetLightColor(state, index, 1, &diffuse);
    grSetCapability(state, GR_CAPABILITY_LIGHT0 + index, 1);
}

static void test_partial_light_update(void)
{
    Gerbera_s state;
    _IFVector4 position;

    memset(&state, 0, sizeof(state));
    setup_directional_light(&state, 0);
    setup_directional_light(&state, 1);
    CHECK(grUpdateLights(&state) == 0);
    CHECK(state.lightDirectionalMask == 3);

    /* Moving one light must preserve the type of the other active light. */
    IFVector4_set(&position, IScalar_one(), 0, 0, 0);
    grSetLightPosition(&state, 1, &position);
    CHECK(grUpdateLights(&state) == 0);
    CHECK(state.lightDirectionalMask == 3);
    CHECK(state.l_directional[0] == &state.lights[0]);
    CHECK(state.l_directional[1] == &state.lights[1]);
    CHECK(state.ambientLightSumMask == 3);

    /* Changing that light to positional only clears its own bit. */
    IFVector4_set(&position, 0, 0, IScalar_one(), IScalar_one());
    grSetLightPosition(&state, 1, &position);
    IFVector3_init100(&state.lights[1].attenuation);
    CHECK(grUpdateLights(&state) == 1);
    CHECK(state.lightDirectionalMask == 1);
    CHECK(state.l_directional[0] == &state.lights[0]);
    CHECK(state.l_directional[1] == NULL);
    CHECK(state.l_positional[0] == &state.lights[1]);
    CHECK(state.ambientLightSumMask == 1);
}

static void test_normalize_lighting(void)
{
    Gerbera_s state;
    _grVertex vertex;
    _IMatrix4x4 identity;
    _XColor3 white = {0x200000, 0x200000, 0x200000};
    unsigned short unnormalized, normalized, rescaled;

    memset(&state, 0, sizeof(state));
    memset(&vertex, 0, sizeof(vertex));
    IMatrix4x4_identity(&identity);
    setup_directional_light(&state, 0);
    grSetMaterialColor(&state, 1, &white);
    grUpdateLights(&state);
    IMatrix4x4_inversetranspose(&state.normalsToEyeTransform, &identity, 0);
    XNormal3_setf(vertex.normal, 0.0f, 0.0f, 0.5f);

    grLightVertex(&state, &vertex);
    unnormalized = vertex.color.final[0][0];
    CHECK(unnormalized >= 0x7E00 && unnormalized <= 0x8000);

    grSetCapability(&state, GR_CAPABILITY_NORMALIZE, 1);
    grLightVertex(&state, &vertex);
    normalized = vertex.color.final[0][0];
    CHECK(normalized >= 0xFE00);
    CHECK(normalized > unnormalized + 0x7000);

    /* Rescaling the identity normal matrix preserves the supplied length. */
    grSetCapability(&state, GR_CAPABILITY_NORMALIZE, 0);
    grSetCapability(&state, GR_CAPABILITY_RESCALE_NORMAL, 1);
    IMatrix4x4_inversetranspose(&state.normalsToEyeTransform, &identity, 1);
    grLightVertex(&state, &vertex);
    rescaled = vertex.color.final[0][0];
    CHECK(abs((int)rescaled - (int)unnormalized) <= 0x100);
}

static void test_transformed_user_clip_plane(void)
{
    Gerbera_s state;
    _IMatrix4x4 identity, projection;
    _IFVector4 plane;
    _grVertex vertex;
    int matrix[16] = {0};
    int positions[2][3] = {{0x2000, 0, 0}, {-0x2000, 0, 0}};
    int i;

    memset(&state, 0, sizeof(state));
    IMatrix4x4_identity(&identity);
    /* x' = x + 3y, y' = 2x + 7y. The object plane x >= 0
     * therefore becomes 7x' - 3y' >= 0 in clip space. */
    matrix[0] = IScalar_one();
    matrix[1] = IScalar_immediate(2.0f);
    matrix[4] = IScalar_immediate(3.0f);
    matrix[5] = IScalar_immediate(7.0f);
    matrix[10] = IScalar_one();
    matrix[15] = IScalar_one();
    IMatrix4x4_setfv(&projection, matrix);
    grSetMatrix(&state, GR_MATRIXMODE_MODELVIEW, &identity);
    grSetMatrix(&state, GR_MATRIXMODE_PROJECTION, &projection);
    grSetRectangle(&state.renderScissor, 0, 64, 0, 64);
    grSetVertexArray(&state, GR_VERTEXARRAY_POSITION, GR_DATATYPE_FIXED, 3,
                     sizeof(positions[0]), positions, 0, positions);
    grSetCapability(&state, GR_CAPABILITY_VERTEX_ARRAY, 1);
    grSetCapability(&state, GR_CAPABILITY_CLIP_PLANE0, 1);
    IFVector4_set(&plane, IScalar_one(), 0, 0, 0);
    grSetUserClipPlane(&state, GR_CLIP_USER0, &plane, &identity);
    CHECK(grSetupCulling(&state, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_RENDER);
    CHECK(IFVector4_getf(&state.userClipPlanes[0], 0) == IScalar_immediate(7.0f));
    CHECK(IFVector4_getf(&state.userClipPlanes[0], 1) == IScalar_immediate(-3.0f));
    for (i = 0; i < 2; ++i)
    {
        memset(&vertex, 0, sizeof(vertex));
        vertex.index = i;
        grTransformVertex(&state, &vertex);
        CHECK((vertex.clipMask & (1u << GR_CLIP_USER0)) == (i ? 1u << GR_CLIP_USER0 : 0));
    }
}

static void test_identity_float_texcoords(void)
{
    static const unsigned int fractions[] = {0, 1, 0x3FFFFF, 0x7FFFFE, 0x7FFFFF};
    _IMatrix4x4 matrix;
    _ITCTransform transform;
    unsigned int sign, exponent, fraction;

    IMatrix4x4_identity(&matrix);
    ITCTransform_copy(&transform, &matrix);
    for (sign = 0; sign < 2; ++sign)
        for (exponent = 0; exponent < 256; ++exponent)
            for (fraction = 0; fraction < sizeof(fractions) / sizeof(fractions[0]); ++fraction)
            {
                int input[3], scalar[2], expected[2], actual[2];
                input[0] = (int)((sign << 31) | (exponent << 23) | fractions[fraction]);
                input[1] = (int)((unsigned int)input[0] ^ 0x80000000u);
                scalar[0] = IScalar_iftos(input[0]);
                scalar[1] = IScalar_iftos(input[1]);
                ITCTransform_mulfv2(expected, &transform, scalar);
                ITCTransform_mulfv(actual, &transform, input, 2);
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
                /* Output may overlap the second input coordinate. */
                ITCTransform_mulfv(input + 1, &transform, input, 2);
                CHECK(memcmp(input + 1, expected, sizeof(expected)) == 0);
            }
}

static void test_texture_transforms(void)
{
    _IMatrix4x4 matrix;
    _ITCTransform transform;
    int elements[16];
    int fixed[4] = {0x8000, -0x4000, 0, 0x10000};
    int scalar[4] = {IScalar_immediate(0.5f), IScalar_immediate(-0.25f),
                     0, IScalar_one()};
    int output[2];
    int general, kind, component;

    for (general = 0; general < 2; ++general)
    {
        int expected[2] = {-0x200000, -0x180000};
        memset(elements, 0, sizeof(elements));
        elements[0] = IScalar_immediate(-2.0f);
        elements[5] = IScalar_immediate(3.0f);
        elements[10] = elements[15] = IScalar_one();
        if (general)
        {
            elements[0] = IScalar_immediate(2.0f);
            elements[1] = IScalar_one();
            elements[4] = IScalar_immediate(-1.0f);
            elements[12] = IScalar_immediate(0.25f);
            elements[13] = IScalar_immediate(-0.5f);
            expected[0] = 0x300000;
        }
        IMatrix4x4_setfv(&matrix, elements);
        ITCTransform_copy(&transform, &matrix);
        for (kind = 0; kind < 4; ++kind)
        {
            if (kind == 0) ITCTransform_mulxv2(output, &transform, fixed);
            if (kind == 1) ITCTransform_mulxv4(output, &transform, fixed);
            if (kind == 2) ITCTransform_mulfv2(output, &transform, scalar);
            if (kind == 3) ITCTransform_mulfv4(output, &transform, scalar);
            for (component = 0; component < 2; ++component)
                CHECK(output[component] == expected[component]);
        }
    }
}

static void test_bound_normal_array(void)
{
    Gerbera_s state;
    _IMatrix4x4 identity;
    _XColor3 white = {0x200000, 0x200000, 0x200000};
    unsigned char storage[1332 + sizeof(_grVertex)];
    unsigned char vertices[] = {0, 0xFF};
    float normals[] = {0.0f, 0.0f, -1.0f};
    _grVertex *vertex = (_grVertex *)(storage + 1332);

    memset(&state, 0, sizeof(state));
    memset(storage, 0, sizeof(storage));
    state.renderStack = storage;
    state.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IMatrix4x4_identity(&identity);
    IMatrix4x4_inversetranspose(&state.normalsToEyeTransform, &identity, 0);
    setup_directional_light(&state, 0);
    grSetMaterialColor(&state, 1, &white);
    grSetCapability(&state, GR_CAPABILITY_LIGHTING, 1);
    grSetCapability(&state, GR_CAPABILITY_NORMALIZE, 1);
    XNormal3_init001p(state.defaultNormal);
    grSetVertexArray(&state, GR_VERTEXARRAY_NORMAL, GR_DATATYPE_FLOAT, 3,
                     sizeof(normals), normals, 0, normals);
    grSetCapability(&state, GR_CAPABILITY_NORMAL_ARRAY, 1);
    grUpdateVertexPipeline(&state);
    state.fetchVertexNormal(&state, vertices);
    grLightVertex(&state, vertex);
    CHECK(vertex->color.final[0][0] == 0);

    /* Both original binaries select bound normal data without consulting
     * the client enable flag. Retain that behavior when the flag changes. */
    grSetCapability(&state, GR_CAPABILITY_NORMAL_ARRAY, 0);
    grUpdateVertexPipeline(&state);
    CHECK(state.vertexArray[GR_VERTEXARRAY_NORMAL].data == normals);
    CHECK(state.fetchVertexNormal == grFetchVertexNormalFLOAT);
    state.fetchVertexNormal(&state, vertices);
    CHECK(vertex->normal[2] == -0x200000);
    grLightVertex(&state, vertex);
    CHECK(vertex->color.final[0][0] == 0);

    grSetCapability(&state, GR_CAPABILITY_NORMAL_ARRAY, 1);
    grUpdateVertexPipeline(&state);
    state.fetchVertexNormal(&state, vertices);
    grLightVertex(&state, vertex);
    CHECK(vertex->color.final[0][0] == 0);
}

static void test_signed_vertex_inputs(void)
{
    Gerbera_s state;
    _IMatrix4x4 identity;
    _grVertex vertex;
    _grVertex *buffer;
    signed char bytes[4] = {-128, -1, 127, 1};
    short shorts[4] = {-32768, -1, 32767, 1};
    signed char tex_bytes[4] = {-2, -1, 1, 1};
    short tex_shorts[4] = {-2, -1, 1, 1};
    unsigned char indices[] = {0, 255};
    int i;

    memset(&state, 0, sizeof(state));
    IMatrix4x4_identity(&identity);
    IVertexTransform_copy(&state.objectToClipTransform, &identity);
    state.vertexArray[0].size = 4;
    for (i = 0; i < 2; ++i)
    {
        memset(&vertex, 0, sizeof(vertex));
        state.vertexArray[0].type = i ? GR_DATATYPE_INT16 : GR_DATATYPE_INT8;
        state.vertexArray[0].data = i ? (void *)shorts : (void *)bytes;
        grTransformVertex(&state, &vertex);
        CHECK((int)vertex.position.objectSpaceFloat[0].e == (i ? -2147483647 - 1 : -8388608));
        CHECK(vertex.position.objectSpaceFloat[0].m[0] == -65536);
        CHECK(vertex.position.objectSpaceFloat[0].m[1] == (i ? 2147418112 : 8323072));
        CHECK(vertex.position.objectSpaceFloat[0].m[2] == 65536);
    }

    state.renderStack = calloc(1, 1332 + 256 * sizeof(_grVertex));
    CHECK(state.renderStack != NULL);
    if (state.renderStack == NULL)
        return;
    buffer = (_grVertex *)((unsigned char *)state.renderStack + 1332);
    ITCTransform_copy(&state.texCoordTransform[0], &identity);
    state.vertexArray[6].size = 4;
    state.vertexArray[6].data = tex_bytes;
    grTransformTexCoordINT8x2(&state, indices, 0);
    CHECK(buffer[0].texCoord[0].x == -0x400000);
    CHECK(buffer[0].texCoord[0].y == -0x200000);
    grTransformTexCoordINT8x4(&state, indices, 0);
    CHECK(buffer[0].texCoord[0].x == -0x400000);
    CHECK(buffer[0].texCoord[0].y == -0x200000);
    state.vertexArray[6].data = tex_shorts;
    grTransformTexCoordINT16x2(&state, indices, 0);
    CHECK(buffer[0].texCoord[0].x == -0x400000);
    CHECK(buffer[0].texCoord[0].y == -0x200000);
    grTransformTexCoordINT16x4(&state, indices, 0);
    CHECK(buffer[0].texCoord[0].x == -0x400000);
    CHECK(buffer[0].texCoord[0].y == -0x200000);
    free(state.renderStack);
}

static void test_short3_transforms(void)
{
    static const float matrices[][16] = {
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {-2, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1.5f, 0, .5f, -.25f, 2, 1},
        {1, .25f, .125f, .125f, -.5f, 1, -.25f, -.125f,
         .25f, -.125f, 1, .25f, .5f, -.25f, .125f, 1},
        {1.25f, 0, 0, 0, 0, 1.5f, 0, 0, 0, 0, -1.25f, -1, 0, 0, -2.25f, 0}
    };
    /* Two-byte alignment and an interleaved stride are legal for GL_SHORT. */
    short positions[][4] = {{7, -32768, -1, 32767}, {7, 0, 0, 0},
                            {7, -2, 3, -4}, {7, 1, -1, 1}, {7, 32767, -32768, -32768}};
    int fixed[5][3];
    Gerbera_s state;
    unsigned int i, j, k;

    memset(&state, 0, sizeof(state));
    state.vertexArray[0].size = 3;
    for (i = 0; i < 5; ++i)
        for (j = 0; j < 3; ++j)
            fixed[i][j] = (int)positions[i][j + 1] * 65536;
    for (k = 0; k < sizeof(matrices) / sizeof(matrices[0]); ++k)
    {
        _IMatrix4x4 matrix;
        int bits[16];
        memcpy(bits, matrices[k], sizeof(bits));
        IMatrix4x4_setfv(&matrix, bits);
        IVertexTransform_copy(&state.objectToClipTransform, &matrix);
        for (i = 0; i < 5; ++i)
        {
            _grVertex shortVertex, fixedVertex;
            memset(&shortVertex, 0xA5, sizeof(shortVertex));
            shortVertex.index = (int)i;
            shortVertex.stateMask = 0;
            fixedVertex = shortVertex;
            state.vertexArray[0].type = GR_DATATYPE_INT16;
            state.vertexArray[0].stride = sizeof(positions[0]);
            state.vertexArray[0].data = &positions[0][1];
            grTransformVertex(&state, &shortVertex);
            state.vertexArray[0].type = GR_DATATYPE_FIXED;
            state.vertexArray[0].stride = sizeof(fixed[0]);
            state.vertexArray[0].data = fixed;
            grTransformVertex(&state, &fixedVertex);
            CHECK(memcmp(&shortVertex, &fixedVertex, sizeof(shortVertex)) == 0);
        }
    }
}

static void test_float_transform_cache(void)
{
    Gerbera_s state;
    _IMatrix4x4 identity, matrix;
    int position[4] = {0, 0, 0, 0};
    unsigned int draw, index;

    memset(&state, 0, sizeof(state));
    IMatrix4x4_identity(&identity);
    grSetMatrix(&state, GR_MATRIXMODE_PROJECTION, &identity);
    grSetRectangle(&state.renderScissor, 0, 64, 0, 64);
    grSetVertexArray(&state, GR_VERTEXARRAY_POSITION, GR_DATATYPE_FLOAT, 4,
                     sizeof(position), position, 0, position);
    grSetCapability(&state, GR_CAPABILITY_VERTEX_ARRAY, 1);
    for (draw = 0; draw < 4; ++draw)
    {
        int bits[16];
        for (index = 0; index < 16; ++index)
            bits[index] = IScalar_immediate((float)((int)(index + draw) % 7 - 3) * .25f);
        IMatrix4x4_setfv(&matrix, bits);
        grSetMatrix(&state, GR_MATRIXMODE_MODELVIEW, &matrix);
        CHECK(grSetupCulling(&state, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_RENDER);
        {
            _ICommonVertexTransform common;
            IVertexTransform_prepareCommon(&common, &state.objectToClipTransform);
            CHECK(memcmp(&common, &state.commonTransform, sizeof(common)) == 0);
        }
        state.commonTransform.eligible = 0;
        CHECK(state.floatTransformMisses == 0);
        CHECK(state.floatTransformPlan[0].inputExponent == ~0u);
        CHECK(state.floatTransformPlan[1].inputExponent == ~0u);
        for (index = 0; index < 32; ++index)
        {
            _grVertex cached, direct;
            unsigned int exponent = draw & 1 ? 110 + index : 126 + (index & 1);
            position[0] = (int)((exponent << 23) | (index * 7919));
            position[1] = position[0] ^ (int)0x80000000u;
            position[2] = index & 3 ? IScalar_one() : 0;
            position[3] = draw & 2 ? IScalar_immediate(2.0f) : IScalar_one();
            memset(&cached, 0, sizeof(cached));
            direct = cached;
            state.renderStackAllocated = 1;
            grTransformVertex(&state, &cached);
            state.renderStackAllocated = 0;
            grTransformVertex(&state, &direct);
            CHECK(memcmp(&cached, &direct, sizeof(cached)) == 0);
        }
        CHECK(state.floatTransformMisses == (draw & 1 ? GR_FLOAT_TRANSFORM_MISS_LIMIT : 0u));
    }
}

static void test_precise_clipper_restores_cache(void)
{
    Gerbera_s state;
    _IMatrix4x4 matrix;
    _grVertex *buffer, saved[3];
    float positions[3][3] = {{0.125f, 0.25f, 0}, {-0.5f, 0.125f, 0}, {0.25f, -0.5f, 0}};
    int bits[16] = {0};
    int i;
    memset(&state, 0, sizeof(state));
    state.renderStack = calloc(1, 1332 + 256 * sizeof(_grVertex));
    CHECK(state.renderStack != NULL);
    if (!state.renderStack) return;
    state.renderStackAllocated = 1;
    buffer = (_grVertex *)((unsigned char *)state.renderStack + 1332);
    bits[0] = bits[5] = bits[10] = bits[15] = IScalar_one();
    bits[14] = IScalar_immediate(2.0f);
    IMatrix4x4_setfv(&matrix, bits);
    IVertexTransform_copy(&state.objectToClipTransform, &matrix);
    IVertexTransform_prepareCommon(&state.commonTransform, &state.objectToClipTransform);
    grSetVertexArray(&state, GR_VERTEXARRAY_POSITION, GR_DATATYPE_FLOAT, 3,
                     sizeof(positions[0]), positions, 0, positions);
    for (i = 0; i < 3; ++i)
    {
        buffer[i].index = i;
        grTransformVertex(&state, &buffer[i]);
        CHECK(buffer[i].stateMask & GR_VERTEXMASK_APPROXIMATE_POSITION);
    }
    memcpy(saved, buffer, sizeof(saved));
    grClipPrimitive(&state, grSetIndices(0, 1, 2), 3);
    CHECK(memcmp(saved, buffer, sizeof(saved)) == 0);
    grClipPrimitive(&state, grSetIndices(0, 1, 1), 2);
    CHECK(memcmp(saved, buffer, sizeof(saved)) == 0);
    grClipPrimitive(&state, grSetIndices(0, 0, 0), 1);
    CHECK(memcmp(saved, buffer, sizeof(saved)) == 0);
    free(state.renderStack);
}

int main(void)
{
    test_partial_light_update();
    test_normalize_lighting();
    test_transformed_user_clip_plane();
    test_texture_transforms();
    test_identity_float_texcoords();
    test_bound_normal_array();
    test_signed_vertex_inputs();
    test_short3_transforms();
    test_float_transform_cache();
    test_precise_clipper_restores_cache();
    printf("geometry: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
