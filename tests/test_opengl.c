/* test_opengl.c - unit tests for src/gerbera/api_opengl (OpenGL ES 1.1 CM). */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "GLES/egl.h"
#include "GLES/gl.h"
#include "gerbera/grRGBA.h"
#include "gerbera/api_opengl/oglContext.h"

#include "test_support.h"

#define NEAR_F(a, b, eps) (fabsf((float)(a) - (float)(b)) <= (eps))

static void init_dummy_matrix_stacks(oglContext_s *ctx, unsigned char *arena, int arenaSize)
{
    static const unsigned char depths[GR_MATRIXMODE_MAX] =
    {
        16, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    int i;

    grStackInit(&ctx->allocator, arena, arenaSize);
    for (i = 0; i < GR_MATRIXMODE_MAX; ++i)
    {
        int bytes = 4 + (int)depths[i] * (int)sizeof(_IMatrix4x4);
        oglMatrixStack_s *st = (oglMatrixStack_s *)grStackAllocate(&ctx->allocator, bytes);
        int j;
        st->index   = 0;
        st->depth   = depths[i];
        st->dirty   = 0;
        st->padding = 0;
        for (j = 0; j < (int)depths[i]; ++j)
            IMatrix4x4_identity(&st->elements[j]);
        ctx->matrixStack[i] = st;
    }
}

static void test_error_and_descriptor(void)
{
    oglContext_s ctx;
    _grSurfaceDescriptor desc;

    memset(&ctx, 0, sizeof(ctx));
    CHECK(ctx.error == OGL_ERROR_OK);

    /* Sticky first-error behavior per GLES 1.1 spec */
    oglSetError(&ctx, OGL_ERROR_INVALID_ENUM);
    CHECK(ctx.error == OGL_ERROR_INVALID_ENUM);
    oglSetError(&ctx, OGL_ERROR_OUT_OF_MEMORY);
    CHECK(ctx.error == OGL_ERROR_INVALID_ENUM);
    ctx.error = OGL_ERROR_OK;
    oglSetError(&ctx, OGL_ERROR_OUT_OF_MEMORY);
    CHECK(ctx.error == OGL_ERROR_OUT_OF_MEMORY);

    /* RGB565 */
    oglFillDescriptor(&desc, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
    CHECK(desc.colorBytes == 2);
    CHECK(desc.colorMask == 7);
    CHECK(desc.pixelFormat == 4);
    CHECK(desc.rgbaSizes[0] == 5 && desc.rgbaSizes[1] == 6 && desc.rgbaSizes[2] == 5 && desc.rgbaSizes[3] == 0);
    CHECK(desc.rgbaOffsets[0] == 11 && desc.rgbaOffsets[1] == 5 && desc.rgbaOffsets[2] == 0);

    /* RGBA4444 */
    oglFillDescriptor(&desc, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
    CHECK(desc.colorBytes == 2);
    CHECK(desc.colorMask == 15);
    CHECK(desc.pixelFormat == 5);
    CHECK(desc.rgbaSizes[0] == 4 && desc.rgbaSizes[1] == 4 && desc.rgbaSizes[2] == 4 && desc.rgbaSizes[3] == 4);
    CHECK(desc.rgbaOffsets[0] == 12 && desc.rgbaOffsets[1] == 8 && desc.rgbaOffsets[2] == 4 && desc.rgbaOffsets[3] == 0);

    /* RGBA5551 */
    oglFillDescriptor(&desc, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1);
    CHECK(desc.colorBytes == 2);
    CHECK(desc.colorMask == 15);
    CHECK(desc.pixelFormat == 5);
    CHECK(desc.rgbaSizes[0] == 5 && desc.rgbaSizes[1] == 5 && desc.rgbaSizes[2] == 5 && desc.rgbaSizes[3] == 1);
    CHECK(desc.rgbaOffsets[0] == 11 && desc.rgbaOffsets[1] == 6 && desc.rgbaOffsets[2] == 1 && desc.rgbaOffsets[3] == 0);
}

static void test_matrix_stack(void)
{
    static unsigned char arena[8192];
    oglContext_s ctx;
    oglMatrixStack_s *mvStack;
    _IMatrix4x4 *m;
    float outF[16];
    int outX[16];
    int fixedIn[16];
    float floatConverted[16];
    int i;

    memset(&ctx, 0, sizeof(ctx));
    init_dummy_matrix_stacks(&ctx, arena, (int)sizeof(arena));

    CHECK(oglIsMatrixStackValid(NULL) == 0);
    mvStack = oglGetMatrixStack(&ctx, GR_MATRIXMODE_MODELVIEW);
    CHECK(mvStack != NULL);
    CHECK(mvStack->depth == 16);
    CHECK(mvStack->dirty == 0);

    ctx.matrixMode = GR_MATRIXMODE_MODELVIEW;
    CHECK(oglGetActiveMatrixStack(&ctx) == mvStack);

    m = oglGetActiveMatrix(&ctx, 1);
    CHECK(m == &mvStack->elements[0]);
    CHECK(mvStack->dirty == 1);

    oglMatrixToOutput(outF, m, OGL_OUTPUTTYPE_FLOAT);
    oglMatrixToOutput(outX, m, OGL_OUTPUTTYPE_FIXED);
    for (i = 0; i < 16; ++i)
    {
        float want = (i % 5 == 0) ? 1.0f : 0.0f;
        CHECK(NEAR_F(outF[i], want, 1e-6f));
        CHECK(outX[i] == ((i % 5 == 0) ? 0x10000 : 0));
        fixedIn[i] = (i - 8) * 0x8000; /* -4.0 .. +3.5 in 16.16 */
    }

    oglMatrixFixedToFloat(floatConverted, fixedIn);
    CHECK(NEAR_F(floatConverted[0], -4.0f, 1e-5f));
    CHECK(NEAR_F(floatConverted[8], 0.0f, 1e-5f));
    CHECK(NEAR_F(floatConverted[9], 0.5f, 1e-5f));
    CHECK(NEAR_F(floatConverted[15], 3.5f, 1e-5f));
}

static void test_tex_manager(void)
{
    oglTextureManager *mgr;
    unsigned int names[24];
    int i;

    mgr = oglCreateTextureManager();
    CHECK(mgr != NULL);
    CHECK(oglIsValidTextureManager(mgr) == 1);
    /* Default texture 0 is allocated at creation */
    CHECK(mgr->numTextures == 1);
    CHECK(oglIsTexture(mgr, 0) == 1);
    CHECK(oglGetTexture(mgr, 0) != NULL);

    /* Generate & allocate 20 textures to trigger multiple power-of-2 hash growths */
    for (i = 0; i < 20; ++i)
    {
        names[i] = oglGenerateTextureName(mgr);
        CHECK(names[i] == (unsigned int)(i + 1));
        CHECK(oglAllocateTexture(mgr, names[i]) == 1);
        CHECK(oglIsTexture(mgr, names[i]) == 1);
    }
    CHECK(mgr->numTextures == 21);
    CHECK(mgr->capacity >= 32);

    /* Delete even-indexed textures, verify tombstones & remaining lookups */
    for (i = 0; i < 20; i += 2)
    {
        oglDeleteTexture(mgr, names[i]);
        CHECK(oglIsTexture(mgr, names[i]) == 0);
    }
    for (i = 1; i < 20; i += 2)
    {
        CHECK(oglIsTexture(mgr, names[i]) == 1);
    }
    CHECK(mgr->numTextures == 11);
    CHECK(mgr->numDeleted == 10);

    oglDestroyTextureManager(mgr);
}

static void test_bufobj_manager(void)
{
    oglContext_s ctx;
    oglBufObjManager_s *mgr;
    unsigned int b1, b2;
    oglBufObj_s *obj;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    oglSetDefaultGetterValues(&ctx.getValues);

    mgr = oglCreateBufObjManager();
    CHECK(mgr != NULL);
    ctx.bufObjManager = mgr;
    CHECK(mgr->numObjects == 1);
    CHECK(oglIsBufObj(mgr, 0) == 1);

    b1 = oglGenerateBufObjName(mgr);
    CHECK(b1 == 1u);
    CHECK(oglAllocateBufObj(mgr, b1) == 1);

    b2 = oglGenerateBufObjName(mgr);
    CHECK(b2 == 2u);
    CHECK(oglAllocateBufObj(mgr, b2) == 1);

    /* Grow hash table across 16 additional VBOs */
    for (i = 0; i < 16; ++i)
    {
        unsigned int bn = oglGenerateBufObjName(mgr);
        CHECK(oglAllocateBufObj(mgr, bn) == 1);
    }
    CHECK(mgr->capacity >= 32);

    oglBindBuffer(&ctx, OGL_BUFOBJTARGET_ARRAY, b1);
    CHECK(ctx.activeBufObjs[OGL_BUFOBJTARGET_ARRAY] == b1);
    CHECK(ctx.getValues.arrayBufferBinding == (int)b1);

    oglBindBuffer(&ctx, OGL_BUFOBJTARGET_ELEMENT_ARRAY, b2);
    CHECK(ctx.activeBufObjs[OGL_BUFOBJTARGET_ELEMENT_ARRAY] == b2);
    CHECK(ctx.getValues.elementArrayBufferBinding == (int)b2);

    obj = oglGetActiveBufObj(&ctx, OGL_BUFOBJTARGET_ARRAY);
    CHECK(obj != NULL);
    CHECK(oglSetupBufObj(obj, OGL_BUFOBJUSAGE_STATIC_DRAW, 128) == 1);
    CHECK(obj->size == 128 && obj->buffer != NULL);
    /* Reallocate buffer data */
    CHECK(oglSetupBufObj(obj, OGL_BUFOBJUSAGE_DYNAMIC_DRAW, 64) == 1);
    CHECK(obj->size == 64 && obj->usage == OGL_BUFOBJUSAGE_DYNAMIC_DRAW);
    /* Zero-size buffer data frees storage */
    CHECK(oglSetupBufObj(obj, OGL_BUFOBJUSAGE_STATIC_DRAW, 0) == 1);
    CHECK(obj->size == 0 && obj->buffer == NULL);

    oglDeleteBufObj(mgr, b1);
    CHECK(oglIsBufObj(mgr, b1) == 0);

    oglDestroyBufObjManager(mgr);
}

static void test_object_name_hash_wrap(void)
{
    static const unsigned int names[] = {
        0x10000000u, 0x10000008u, 0x30000000u,
        0x60000000u, 0x60000008u, 0xC0000000u, 0xFFFFFFFFu
    };
    oglTextureManager *textures = oglCreateTextureManager();
    oglBufObjManager_s *buffers = oglCreateBufObjManager();
    _grTexture *textureObjects[7];
    oglBufObj_s *bufferObjects[7];
    unsigned int i;

    /* The original hash probes wrap at 32 bits, including for arbitrary
     * application-supplied names whose probe sum exceeds signed int range. */
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        CHECK(oglAllocateTexture(textures, names[i]) == 1);
        CHECK(oglAllocateBufObj(buffers, names[i]) == 1);
        textureObjects[i] = oglGetTexture(textures, names[i]);
        bufferObjects[i] = oglGetBufObj(buffers, names[i]);
        CHECK(textureObjects[i] != NULL && bufferObjects[i] != NULL);
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        CHECK(oglGetTexture(textures, names[i]) == textureObjects[i]);
        CHECK(oglGetBufObj(buffers, names[i]) == bufferObjects[i]);
        oglDeleteTexture(textures, names[i]);
        oglDeleteBufObj(buffers, names[i]);
        CHECK(oglGetTexture(textures, names[i]) == NULL);
        CHECK(oglGetBufObj(buffers, names[i]) == NULL);
    }
    oglDestroyTextureManager(textures);
    oglDestroyBufObjManager(buffers);
}

static void test_getters(void)
{
    static unsigned char arena[8192];
    Gerbera_s gr;
    oglContext_s ctx;
    int outI[16];
    float outF[16];
    unsigned char outB[16];
    unsigned int uin[2];
    int svals[3];
    float unitIn[3];

    /* Pin exact 676-byte CodeView TI=0x149A layout */
    CHECK(sizeof(_oglGetterValues) == 0x2A4u);
    CHECK(offsetof(_oglGetterValues, colorWritemask) == 52u);
    CHECK(offsetof(_oglGetterValues, depthBits) == 56u);
    CHECK(offsetof(_oglGetterValues, stencilWritemask) == 336u);
    CHECK(offsetof(_oglGetterValues, pointSizeMin) == 344u);
    CHECK(offsetof(_oglGetterValues, textureCoordArraySize) == 396u);
    CHECK(offsetof(_oglGetterValues, aliasedPointSizeRange) == 444u);
    CHECK(offsetof(_oglGetterValues, viewport) == 508u);
    CHECK(offsetof(_oglGetterValues, currentColor) == 588u);
    CHECK(offsetof(_oglGetterValues, currentTextureCoords) == 604u);
    CHECK(offsetof(_oglGetterValues, compressedTextureFormats) == 636u);

    memset(&gr, 0, sizeof(gr));
    memset(&ctx, 0, sizeof(ctx));
    ctx.gerbera = &gr;
    init_dummy_matrix_stacks(&ctx, arena, (int)sizeof(arena));
    oglSetDefaultGetterValues(&ctx.getValues);

    /* Default GL state queries via oglGetInternal */
    oglGetInternal(&ctx, GL_MAX_LIGHTS, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 8);
    oglGetInternal(&ctx, GL_MAX_TEXTURE_SIZE, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 1024);
    oglGetInternal(&ctx, GL_DITHER, outB, OGL_OUTPUTTYPE_BOOL);
    CHECK(outB[0] == GL_TRUE);
    oglGetInternal(&ctx, GL_DEPTH_TEST, outB, OGL_OUTPUTTYPE_BOOL);
    CHECK(outB[0] == GL_FALSE);
    oglGetInternal(&ctx, GL_LIGHT_MODEL_AMBIENT, outF, OGL_OUTPUTTYPE_FLOAT);
    CHECK(NEAR_F(outF[0], 0.2f, 1e-6f) && NEAR_F(outF[3], 1.0f, 1e-6f));

    /* Unit-value converter: currentColor (1,1,1,1) -> INT 0x7FFFFFFF, FIXED 0x10000 */
    oglGetInternal(&ctx, GL_CURRENT_COLOR, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 0x7FFFFFFF && outI[3] == 0x7FFFFFFF);
    oglGetInternal(&ctx, GL_CURRENT_COLOR, outI, OGL_OUTPUTTYPE_FIXED);
    CHECK(outI[0] == 0x10000 && outI[3] == 0x10000);

    unitIn[0] = 0.0f;
    unitIn[1] = 0.5f;
    unitIn[2] = -1.0f;
    oglUnitValueToOutput(outI, unitIn, 3, OGL_OUTPUTTYPE_FIXED);
    CHECK(outI[0] == 0 && outI[1] == 0x8000 && outI[2] == -0x10000);
    oglUnitValueToOutput(outB, unitIn, 3, OGL_OUTPUTTYPE_BOOL);
    CHECK(outB[0] == 0 && outB[1] == 1 && outB[2] == 1);

    /* oglUIntToOutput handles unsigned values >= 0x80000000 */
    uin[0] = 42u;
    uin[1] = 0x80000000u;
    oglUIntToOutput(outF, uin, 2, OGL_OUTPUTTYPE_FLOAT);
    CHECK(NEAR_F(outF[0], 42.0f, 1e-3f));
    CHECK(NEAR_F(outF[1], 2147483648.0f, 1.0f));

    /* oglIScalarToOutput clamps overflow to INT32_MIN/MAX */
    svals[0] = IScalar_ftos(12.0f);
    svals[1] = IScalar_ftos(1.0e12f);
    svals[2] = IScalar_ftos(-1.0e12f);
    oglIScalarToOutput(outI, svals, 3, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 12);
    CHECK(outI[1] == 0x7FFFFFFF);
    CHECK(outI[2] == (int)0x80000000u);

    /* Per-activeTexture & per-clientActiveTexture indexed state */
    {
        int b0 = 7, b1 = 19;
        float tc1[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        int sz0 = 2, sz1 = 3;

        oglSetGetterValueInt(&ctx, 0, GL_TEXTURE_BINDING_2D, &b0);
        oglSetGetterValueInt(&ctx, 1, GL_TEXTURE_BINDING_2D, &b1);
        oglSetGetterValueFloat(&ctx, 1, GL_CURRENT_TEXTURE_COORDS, tc1);
        oglSetGetterValueInt(&ctx, 0, GL_TEXTURE_COORD_ARRAY_SIZE, &sz0);
        oglSetGetterValueInt(&ctx, 1, GL_TEXTURE_COORD_ARRAY_SIZE, &sz1);

        ctx.activeTexture = 0;
        oglGetInternal(&ctx, GL_TEXTURE_BINDING_2D, outI, OGL_OUTPUTTYPE_INT);
        CHECK(outI[0] == 7);
        ctx.activeTexture = 1;
        oglGetInternal(&ctx, GL_TEXTURE_BINDING_2D, outI, OGL_OUTPUTTYPE_INT);
        CHECK(outI[0] == 19);
        oglGetInternal(&ctx, GL_CURRENT_TEXTURE_COORDS, outF, OGL_OUTPUTTYPE_FLOAT);
        CHECK(NEAR_F(outF[0], 0.25f, 1e-6f) && NEAR_F(outF[2], 0.75f, 1e-6f));

        ctx.clientActiveTexture = 0;
        oglGetInternal(&ctx, GL_TEXTURE_COORD_ARRAY_SIZE, outI, OGL_OUTPUTTYPE_INT);
        CHECK(outI[0] == 2);
        ctx.clientActiveTexture = 1;
        oglGetInternal(&ctx, GL_TEXTURE_COORD_ARRAY_SIZE, outI, OGL_OUTPUTTYPE_INT);
        CHECK(outI[0] == 3);
    }

    /* Matrix & stack depth queries */
    ctx.matrixMode[&ctx.matrixStack[0]->index] = 0;
    ctx.matrixStack[GR_MATRIXMODE_MODELVIEW]->index = 3;
    oglGetInternal(&ctx, GL_MODELVIEW_STACK_DEPTH, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 4);
    oglGetInternal(&ctx, GL_MODELVIEW_MATRIX, outF, OGL_OUTPUTTYPE_FLOAT);
    CHECK(NEAR_F(outF[0], 1.0f, 1e-6f) && NEAR_F(outF[15], 1.0f, 1e-6f));

    /* Stencil ref / clear value masking against drawSurface.stencilBytes */
    gr.stencilReference = 0x1AB;
    gr.clearStencil     = 0x1CD;
    gr.drawSurface.stencilBytes = 0;
    oglGetInternal(&ctx, GL_STENCIL_REF, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 0);
    gr.drawSurface.stencilBytes = 1;
    oglGetInternal(&ctx, GL_STENCIL_REF, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 0xAB);
    oglGetInternal(&ctx, GL_STENCIL_CLEAR_VALUE, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 0xCD);

    /* Error recording on NULL params & invalid enum */
    ctx.error = OGL_ERROR_OK;
    oglGetInternal(&ctx, GL_MAX_LIGHTS, NULL, OGL_OUTPUTTYPE_INT);
    CHECK(ctx.error == OGL_ERROR_INVALID_VALUE);

    ctx.error = OGL_ERROR_OK;
    oglGetInternal(&ctx, 0xDEADu, outI, OGL_OUTPUTTYPE_INT);
    CHECK(ctx.error == OGL_ERROR_INVALID_ENUM);
}

static void test_ogl_util(void)
{
    grMemoryImage_s img;
    float fv[4];
    int   xv[4] = { 0x04000, 0x08000, 0x0C000, 0x10000 };
    float inF[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    _XColor3 xc;
    _IFVector4 v4;

    /* Run the original driver self-test verifying round-trip enum mappings */
    oglMappingTest();

    /* Fixed/float/color/input converters */
    CHECK(NEAR_F(oglFixedToFloat(0x10000), 1.0f, 1e-6f));
    CHECK(oglFloatToFixed(1.5f) == 0x18000);
    CHECK(NEAR_F(oglFloatClampUnit(-0.5f), 0.0f, 1e-6f));
    CHECK(NEAR_F(oglFloatClampUnit(2.5f), 1.0f, 1e-6f));
    CHECK(NEAR_F(oglFixedClampUnit(-100), 0.0f, 1e-6f));
    CHECK(NEAR_F(oglFixedClampUnit(0x20000), 1.0f, 1e-6f));
    CHECK(oglFloatsToRGBA(1.0f, 0.0f, 0.0f, 1.0f) == grSetRGBA(255, 0, 0, 255));
    CHECK(oglFixedToRGBA(0, 0x10000, 0, 0x10000) == grSetRGBA(0, 255, 0, 255));

    oglRGBAToFloatv(grSetRGBA(255, 0, 255, 0), fv);
    CHECK(NEAR_F(fv[0], 1.0f, 1e-5f) && NEAR_F(fv[1], 0.0f, 1e-5f) &&
          NEAR_F(fv[2], 1.0f, 1e-5f) && NEAR_F(fv[3], 0.0f, 1e-5f));

    oglInputToXColor3(&xc, inF, OGL_INPUTTYPE_FLOAT);
    CHECK(oglInputAlpha(inF, OGL_INPUTTYPE_FLOAT) == 0xFFFFu);
    CHECK(oglInputToRGBA(xv, OGL_INPUTTYPE_FIXED) == grSetRGBA(64, 128, 192, 255));
    CHECK(oglInputToIScalarDirect(&inF[1], OGL_INPUTTYPE_FLOAT) == IScalar_immediate(0.5f));
    CHECK(oglInputToIScalarDirect(&xv[1], OGL_INPUTTYPE_FIXED) == IScalar_immediate(0.5f));
    CHECK(oglTestInput(xv, 0, OGL_INPUTTYPE_FIXED) == 1);
    CHECK(NEAR_F(oglInputToFloatDirect(&xv[2], OGL_INPUTTYPE_FIXED), 0.75f, 1e-6f));
    oglInputToIFVector4Direct(&v4, inF, OGL_INPUTTYPE_FLOAT);
    CHECK((unsigned int)IFVector4_getf(&v4, 3) == hgFloatBitPattern(1.0f));
    oglInputToIFVector4W0(&v4, xv, OGL_INPUTTYPE_FIXED);
    CHECK(IFVector4_getf(&v4, 3) == 0);
    CHECK(oglInputToInteger(&xv[3], OGL_INPUTTYPE_FIXED) == 0x10000);
    CHECK(oglInputToInteger(&inF[3], OGL_INPUTTYPE_FLOAT) == 1);
    oglInputToFloatv(fv, xv, OGL_INPUTTYPE_FIXED, 4);
    CHECK(NEAR_F(fv[0], 0.25f, 1e-6f) && NEAR_F(fv[3], 1.0f, 1e-6f));

    /* Enum mappers */
    CHECK(oglMapDataType(GL_BYTE) == GR_DATATYPE_INT8);
    CHECK(oglMapDataType(GL_UNSIGNED_BYTE) == GR_DATATYPE_UINT8);
    CHECK(oglMapDataType(GL_SHORT) == GR_DATATYPE_INT16);
    CHECK(oglMapDataType(GL_FLOAT) == GR_DATATYPE_FLOAT);
    CHECK(oglMapDataType(GL_FIXED) == GR_DATATYPE_FIXED);
    CHECK(oglMapDataType(GL_UNSIGNED_SHORT_5_6_5) == GR_DATATYPE_MAX);

    CHECK(oglMapCapability(GL_DEPTH_TEST) == GR_CAPABILITY_DEPTH_TEST);
    CHECK(oglMapCapability(GL_BLEND) == GR_CAPABILITY_BLEND);
    CHECK(oglMapCapability(GL_LIGHT0 + 5) == GR_CAPABILITY_LIGHT0 + 5);
    CHECK(oglMapCapability(GL_CLIP_PLANE0 + 3) == GR_CAPABILITY_CLIP_PLANE0 + 3);
    CHECK(oglMapCapability(0xDEADu) == GR_CAPABILITY_MAX);

    CHECK(oglMapStencilOp(GL_ZERO) == GR_STENCILOP_ZERO);
    CHECK(oglMapStencilOp(GL_INCR) == GR_STENCILOP_INCR);
    CHECK(oglMapStencilOp(GL_INVERT) == GR_STENCILOP_INVERT);
    CHECK(oglMapBlendFactor(GL_SRC_ALPHA) == GR_BLENDFACTOR_SRC_ALPHA);
    CHECK(oglMapBlendFactor(GL_ONE_MINUS_SRC_ALPHA) == GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
    CHECK(oglMapMatrixMode(GL_MODELVIEW) == GR_MATRIXMODE_MODELVIEW);
    CHECK(oglMapMatrixMode(GL_PROJECTION) == GR_MATRIXMODE_PROJECTION);
    CHECK(oglMapMatrixMode(GL_TEXTURE) == GR_MATRIXMODE_TEXTURE0);
    CHECK(oglMapMatrixMode(GL_MATRIX_PALETTE_OES) == GR_MATRIXMODE_PALETTE0);

    CHECK(oglMapBufferMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) == 7u);
    CHECK(oglMapBufferMask(GL_DEPTH_BUFFER_BIT) == 2u);

    /* Texture format & memory image helpers */
    CHECK(oglMapInternalTextureFormatAndType(GL_RGB, GL_UNSIGNED_SHORT_5_6_5) == GR_TEXTUREFORMAT_RGB565);
    CHECK(oglMapInternalTextureFormatAndType(GL_RGBA, GL_UNSIGNED_BYTE) == GR_TEXTUREFORMAT_RGBA8);
    CHECK(oglIsCompatibleTextureType(GL_RGB, GL_UNSIGNED_SHORT_5_6_5) == 1);
    CHECK(oglIsCompatibleTextureType(GL_RGBA, GL_UNSIGNED_SHORT_5_6_5) == 0);
    CHECK(oglIsCompatibleInternalFormat(GL_RGB, GR_TEXTUREFORMAT_RGB565) == 1);
    CHECK(oglIsCompatibleInternalFormat(GL_RGB, GR_TEXTUREFORMAT_RGBA8) == 0);

    memset(&img, 0, sizeof(img));
    oglFillMemoryImageFormat(&img, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
    CHECK(img.useDescriptor == 1 && img.bytesPerPixel == 2);

    memset(&img, 0, sizeof(img));
    oglFillMemoryImageFormat(&img, GL_RGBA, GL_UNSIGNED_BYTE);
    CHECK(img.useDescriptor == 0 && img.bytesPerPixel == 4 &&
          img.dataType == GR_DATATYPE_UINT8 && img.imageFormat == GR_IMAGEFORMAT_RGBA);
}

static void test_ogl_context(void)
{
    oglContext_s *ctx1;
    oglContext_s *ctx2;
    oglSurfaceDescriptor surfDesc;
    static unsigned short colorBuf[32 * 32];
    static unsigned int   depthBuf[32 * 32];
    static unsigned char  stencilBuf[32 * 32];
    static unsigned int   surfTexPixels[8 * 8];
    int outI[4];
    float outF[4];
    float amb[4]  = { 0.1f, 0.2f, 0.3f, 0.8f };
    float lpos[4] = { 1.0f, 2.0f, 3.0f, 1.0f };

    /* Unbound current context returns NULL and records EGL_NOT_INITIALIZED (0x3001) */
    egliSetCurrentContext(NULL);
    CHECK(oglGetContext() == NULL);
    CHECK(eglGetError() == EGL_NOT_INITIALIZED);

    /* Standalone context creation & default GLES 1.1 state */
    ctx1 = oglCreateContext(NULL, NULL);
    CHECK(ctx1 != NULL);
    CHECK(oglIsValidContext(ctx1) == 1);
    oglCheckValidity(ctx1);

    egliSetCurrentContext(ctx1);
    CHECK(oglGetContext() == ctx1);

    /* Shared context: shares textureManager + bufObjManager + sharedWorkMem */
    ctx2 = oglCreateContext(ctx1, ctx1);
    CHECK(ctx2 != NULL);
    CHECK(ctx2->textureManager == ctx1->textureManager);
    CHECK(ctx2->bufObjManager == ctx1->bufObjManager);
    CHECK(ctx2->sharedWorkMem == ctx1->sharedWorkMem);
    CHECK(ctx1->shareListNext == ctx2 && ctx2->shareListPrev == ctx1);
    CHECK(ctx1->shareMemNext == ctx2 && ctx2->shareMemPrev == ctx1);

    /* Allocate a texture in ctx1 -> visible in ctx2 */
    CHECK(oglAllocateTexture(ctx1->textureManager, 42u) == 1);
    CHECK(oglIsTexture(ctx2->textureManager, 42u) == 1);

    /* Destroy ctx2 first -> ctx1 keeps shared managers & unlinks cleanly */
    oglDestroyContext(ctx2);
    CHECK(ctx1->shareListNext == NULL && ctx1->shareListPrev == NULL);
    CHECK(ctx1->shareMemNext == NULL && ctx1->shareMemPrev == NULL);
    CHECK(oglIsTexture(ctx1->textureManager, 42u) == 1);

    /* Bind RGB565 + D32 + S8 buffers via oglSetBuffers and query channel bits */
    memset(&surfDesc, 0, sizeof(surfDesc));
    surfDesc.width               = 32;
    surfDesc.height              = 32;
    surfDesc.colorBuffer         = colorBuf;
    surfDesc.depthBuffer         = depthBuf;
    surfDesc.stencilBuffer       = stencilBuf;
    surfDesc.colorBufferStride   = 32 * 2;
    surfDesc.depthBufferStride   = 32 * 4;
    surfDesc.stencilBufferStride = 32;
    surfDesc.colorBytes          = 2;
    surfDesc.depthBytes          = 4;
    surfDesc.stencilBytes        = 1;
    surfDesc.redBits             = 5;
    surfDesc.redOffset           = 11;
    surfDesc.greenBits           = 6;
    surfDesc.greenOffset         = 5;
    surfDesc.blueBits            = 5;
    surfDesc.blueOffset          = 0;
    surfDesc.alphaBits           = 0;
    surfDesc.alphaOffset         = 0;

    CHECK(oglIsValidSurfaceDescriptor(&surfDesc) == 1);
    oglSetBuffers(ctx1, &surfDesc, &surfDesc);
    oglGetInternal(ctx1, GL_RED_BITS, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 5);
    oglGetInternal(ctx1, GL_GREEN_BITS, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 6);
    oglGetInternal(ctx1, GL_DEPTH_BITS, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 32);
    oglGetInternal(ctx1, GL_STENCIL_BITS, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 8);
    CHECK(ctx1->drawSurfaceWidth == 32 && ctx1->drawSurfaceHeight == 32);

    /* Client state, material & light internal helpers */
    oglSetClientState(ctx1, GL_VERTEX_ARRAY, 1);
    oglGetInternal(ctx1, GL_VERTEX_ARRAY, outI, OGL_OUTPUTTYPE_INT);
    CHECK(outI[0] == 1);
    oglMaterialInternal(ctx1, GL_FRONT_AND_BACK, GL_AMBIENT, amb, OGL_INPUTTYPE_FLOAT);
    CHECK(NEAR_F(ctx1->materialColors[GR_MATERIALCOLOR_AMBIENT][0], 0.1f, 1e-5f) &&
          NEAR_F(ctx1->materialColors[GR_MATERIALCOLOR_AMBIENT][3], 0.8f, 1e-5f));
    oglLightInternal(ctx1, GL_LIGHT0, GL_POSITION, lpos, OGL_INPUTTYPE_FLOAT);
    CHECK(ctx1->gerbera->lights[0].positional == 1);
    (void)outF;

    /* Bind texture & EGL surface texture bind/release */
    oglBindTexture(ctx1, 0, GR_TEXTURETARGET_2D, 42u);
    ctx1->textureEnabled[0][GR_TEXTURETARGET_2D] = 1;
    CHECK(oglSetSurfaceTexture(ctx1, surfTexPixels, 8, 8, GL_RGBA, 0, 1) == 1);
    CHECK(oglIsSurfaceTextureReleased(ctx1, 42u) == 0);
    oglUpdateTextureUnit(ctx1, 0);
    CHECK(grTestCapability(ctx1->gerbera, GR_CAPABILITY_TEXTURE_0) == 1);
    oglReleaseSurfaceTexture(ctx1, 42u, 0);
    CHECK(oglIsSurfaceTextureReleased(ctx1, 42u) == 1);

    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx1);
}

typedef struct RGBA8SurfaceFixture {
    oglSurfaceDescriptor descriptor;
    unsigned int color[16 * 16], depth[16 * 16];
    unsigned char stencil[16 * 16];
} RGBA8SurfaceFixture;

/* R, G, B, A bytes, 32-bit depth and 8-bit stencil; at most 16x16 pixels. */
static void init_rgba8_surface(RGBA8SurfaceFixture *surface, int w, int h)
{
    oglSurfaceDescriptor *desc = &surface->descriptor;
    memset(desc, 0, sizeof(*desc));
    desc->width = w;
    desc->height = h;
    desc->colorBuffer = surface->color;
    desc->depthBuffer = surface->depth;
    desc->stencilBuffer = surface->stencil;
    desc->colorBufferStride = w * 4;
    desc->depthBufferStride = w * 4;
    desc->stencilBufferStride = w;
    desc->colorBytes = desc->depthBytes = 4;
    desc->stencilBytes = 1;
    desc->redBits = desc->greenBits = desc->blueBits = desc->alphaBits = 8;
    desc->redOffset = 0;
    desc->greenOffset = 8;
    desc->blueOffset = 16;
    desc->alphaOffset = 24;
}

static void bind_rgba8_surface(oglContext_s *context, RGBA8SurfaceFixture *surface,
                               int w, int h)
{
    init_rgba8_surface(surface, w, h);
    oglSetBuffers(context, &surface->descriptor, &surface->descriptor);
    egliSetCurrentContext(context);
}

#define CHECK_RGB_AT(x, y, red, green, blue) do { \
    unsigned char actual_[4]; \
    glReadPixels((x), (y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual_); \
    CHECK(actual_[0] == (red) && actual_[1] == (green) && actual_[2] == (blue)); \
} while (0)

static void test_gl_strings_errors_and_state(void)
{
    oglContext_s *ctx;
    RGBA8SurfaceFixture surf;
    GLboolean bval[4];
    GLint ival[4];
    GLfloat fval[4];
    GLfixed xval[4];

    /* Without current context, glGetError returns GL_INVALID_OPERATION and glGetString NULL */
    egliSetCurrentContext(NULL);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    CHECK(glGetString(GL_VENDOR) == NULL);

    ctx = oglCreateContext(NULL, NULL);
    CHECK(ctx != NULL);
    bind_rgba8_surface(ctx, &surf, 16, 16);

    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(strcmp((const char *)glGetString(GL_VENDOR), "Nokia") == 0);
    CHECK(strcmp((const char *)glGetString(GL_RENDERER), "1.1.0") == 0);
    CHECK(strcmp((const char *)glGetString(GL_VERSION), "OpenGL ES-CM 1.1") == 0);
    CHECK(strstr((const char *)glGetString(GL_EXTENSIONS), "GL_OES_draw_texture") != NULL);
    CHECK(strstr((const char *)glGetString(GL_EXTENSIONS), "GL_OES_matrix_palette") != NULL);
    CHECK(glGetString(0xDEADu) == NULL);
    CHECK(glGetError() == GL_INVALID_ENUM);
    CHECK(glGetError() == GL_NO_ERROR);

    /* Enable / Disable / IsEnabled */
    CHECK(glIsEnabled(GL_DITHER) == GL_TRUE);
    CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_FALSE);
    glEnable(0xDEADu);
    CHECK(glGetError() == GL_INVALID_ENUM);

    /* Hints, ShadeModel, CullFace, FrontFace, PolygonOffset, SampleCoverage */
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glGetIntegerv(GL_PERSPECTIVE_CORRECTION_HINT, ival);
    CHECK(ival[0] == GL_NICEST);
    /* Pin the original slot independently of the recovered enum. */
    CHECK(ctx->gerbera->capabilities[17] == 1 && ctx->gerbera->capabilities[8] == 0);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    CHECK(ctx->gerbera->capabilities[17] == 0);
    glHint(GL_FOG_HINT, 0xDEADu);
    CHECK(glGetError() == GL_INVALID_ENUM);

    glShadeModel(GL_FLAT);
    glGetIntegerv(GL_SHADE_MODEL, ival);
    CHECK(ival[0] == GL_FLAT);
    glShadeModel(GL_SMOOTH);

    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);
    glGetIntegerv(GL_FRONT_FACE, ival);
    CHECK(ival[0] == GL_CW);
    glGetIntegerv(GL_CULL_FACE_MODE, ival);
    CHECK(ival[0] == GL_FRONT);
    glFrontFace(GL_CW + 0x100);
#if GERBERA_ARM_REFERENCE
    CHECK(glGetError() == GL_NO_ERROR);
    glGetIntegerv(GL_FRONT_FACE, ival);
    CHECK(ival[0] == GL_CW + 0x100);
    CHECK(ctx->gerbera->frontFace == 0);
#else
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_FRONT_FACE, ival);
    CHECK(ival[0] == GL_CW);
#endif
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);

    glPolygonOffset(2.0f, 4.0f);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, fval);
    CHECK(NEAR_F(fval[0], 2.0f, 1e-5f));
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, fval);
    CHECK(NEAR_F(fval[0], 4.0f, 1e-5f));

    glSampleCoverage(0.5f, GL_TRUE);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, fval);
    CHECK(NEAR_F(fval[0], 0.5f, 1e-5f));
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, bval);
    CHECK(bval[0] == GL_TRUE);

    /* Original 0x446580 keeps eight core mask bits and the full GL query value. */
    glStencilMask(0x100u);
    glGetIntegerv(GL_STENCIL_WRITEMASK, ival);
    CHECK(ival[0] == 0x100 && ctx->gerbera->stencilWriteMask == 0);
    ctx->gerbera->pixelPipelineDirty = 0;
    glStencilMask(0x100u);
    CHECK(ctx->gerbera->pixelPipelineDirty == 1);
    ctx->gerbera->pixelPipelineDirty = 0;
    glStencilMask(0);
    CHECK(ctx->gerbera->pixelPipelineDirty == 0);
    glStencilMask(0xFFFFFFFFu);
    glGetIntegerv(GL_STENCIL_WRITEMASK, ival);
    CHECK(ival[0] == -1 && ctx->gerbera->stencilWriteMask == 0xFFu);

    /* ClipPlane round-trip (identity modelview -> eye plane == object plane) */
    {
        GLfloat eq[4] = { 1.0f, 0.0f, -0.5f, 2.0f };
        GLfloat outEq[4] = { 0 };
        GLfixed outEqX[4] = { 0 };
        glClipPlanef(GL_CLIP_PLANE0, eq);
        glGetClipPlanef(GL_CLIP_PLANE0, outEq);
        CHECK(NEAR_F(outEq[0], 1.0f, 1e-4f) && NEAR_F(outEq[2], -0.5f, 1e-4f) && NEAR_F(outEq[3], 2.0f, 1e-4f));
        glGetClipPlanex(GL_CLIP_PLANE0, outEqX);
        CHECK(outEqX[0] == 0x10000 && outEqX[3] == 0x20000);
    }

    /* Light, Material, PointParameter, Fog queries */
    {
        GLfloat diff[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        GLfloat outDiff[4] = { 0 };
        GLfloat attn[3] = { 1.0f, 0.5f, 0.25f };
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
        glGetLightfv(GL_LIGHT0, GL_DIFFUSE, outDiff);
        CHECK(NEAR_F(outDiff[0], 0.25f, 1e-4f) && NEAR_F(outDiff[2], 0.75f, 1e-4f));

        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, diff);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
        glGetMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, outDiff);
        CHECK(NEAR_F(outDiff[1], 0.5f, 1e-4f));
        glGetMaterialfv(GL_FRONT, GL_SHININESS, fval);
        CHECK(NEAR_F(fval[0], 32.0f, 1e-4f));
        glMaterialf(GL_FRONT, GL_SHININESS, 16.0f); /* only GL_FRONT_AND_BACK valid in ES 1.1 */
        CHECK(glGetError() == GL_INVALID_ENUM);

        glPointSize(4.0f);
        glPointParameterf(GL_POINT_SIZE_MIN, 2.0f);
        glPointParameterf(GL_POINT_SIZE_MAX, 16.0f);
        glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, attn);
        glGetFloatv(GL_POINT_SIZE, fval);
        CHECK(NEAR_F(fval[0], 4.0f, 1e-4f));
        glGetFixedv(GL_POINT_SIZE_MAX, xval);
        CHECK(xval[0] == (16 << 16));

        glFogf(GL_FOG_MODE, (GLfloat)GL_LINEAR);
        glFogf(GL_FOG_START, 1.0f);
        glFogf(GL_FOG_END, 10.0f);
        glGetIntegerv(GL_FOG_MODE, ival);
        CHECK(ival[0] == GL_LINEAR);
    }

    glFlush();
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);

    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_matrices_and_buffers(void)
{
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    GLfloat m[16];
    GLfixed mantissa[16];
    GLint exponent[16];
    GLuint bufs[2] = { 0, 0 };
    GLint bparam = 0;
    void *ptr = NULL;
    const float vboData[6] = { -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f };
    const float subData[2] = { 0.5f, 0.75f };
    int i;

    egliSetCurrentContext(ctx);

    /* Modelview stack push/pop, translate/scale/rotate, overflow & underflow */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(3.0f, -2.0f, 5.0f);
    glScalef(2.0f, 4.0f, 1.0f);
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    CHECK(NEAR_F(m[0], 2.0f, 1e-5f) && NEAR_F(m[5], 4.0f, 1e-5f));
    CHECK(NEAR_F(m[12], 3.0f, 1e-5f) && NEAR_F(m[13], -2.0f, 1e-5f) && NEAR_F(m[14], 5.0f, 1e-5f));

    CHECK(glQueryMatrixxOES(mantissa, exponent) == 0u);

    glPushMatrix();
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    glPopMatrix();
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    CHECK(NEAR_F(m[0], 2.0f, 1e-5f) && NEAR_F(m[12], 3.0f, 1e-5f));

    /* Pop at bottom -> GL_STACK_UNDERFLOW */
    glPopMatrix();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);

    /* Projection stack depth is 2 -> 2nd push overflows */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    glPushMatrix();
    CHECK(glGetError() == GL_NO_ERROR);
    glPushMatrix();
    CHECK(glGetError() == GL_STACK_OVERFLOW);
    glPopMatrix();
    glLoadIdentity();

    /* Frustum validation (zNear <= 0 -> GL_INVALID_VALUE) */
    glFrustumf(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glFrustumf(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 10.0f);
    CHECK(glGetError() == GL_NO_ERROR);

    /* Matrix palette mode & LoadPaletteFromModelViewMatrixOES */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(7.0f, 0.0f, 0.0f);
    glMatrixMode(GL_MATRIX_PALETTE_OES);
    glCurrentPaletteMatrixOES(2);
    glLoadPaletteFromModelViewMatrixOES();
    glPushMatrix(); /* palette matrix stack depth is 1 -> GL_STACK_OVERFLOW */
    CHECK(glGetError() == GL_STACK_OVERFLOW);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    (void)i;

    /* Original capability slots: matrix indices 27, weights 28. */
    glEnableClientState(GL_WEIGHT_ARRAY_OES);
    CHECK(ctx->gerbera->capabilities[28] == 1 && ctx->gerbera->capabilities[27] == 0);
    glEnableClientState(GL_MATRIX_INDEX_ARRAY_OES);
    glDisableClientState(GL_WEIGHT_ARRAY_OES);
    CHECK(ctx->gerbera->capabilities[27] == 1 && ctx->gerbera->capabilities[28] == 0);
    glDisableClientState(GL_MATRIX_INDEX_ARRAY_OES);

    glPointSizePointerOES(GL_FLOAT, 12, vboData);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_TYPE_OES, &bparam);
    CHECK(bparam == GL_FLOAT);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_STRIDE_OES, &bparam);
    CHECK(bparam == 12);
    glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_OES, &ptr);
    CHECK(ptr == vboData);
    CHECK(glGetError() == GL_NO_ERROR);

    /* VBO / EBO lifecycle, BufferData, BufferSubData, GetBufferParameteriv, GetPointerv */
    CHECK(glIsBuffer(9999u) == GL_FALSE);
    glGenBuffers(2, bufs);
    CHECK(bufs[0] != 0 && bufs[1] != 0 && bufs[0] != bufs[1]);
    CHECK(glIsBuffer(bufs[0]) == GL_TRUE);
    glBindBuffer(GL_ARRAY_BUFFER, bufs[0]);
    CHECK(glIsBuffer(bufs[0]) == GL_TRUE);

    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vboData), vboData, GL_STATIC_DRAW);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bparam);
    CHECK(bparam == (GLint)sizeof(vboData));
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &bparam);
    CHECK(bparam == GL_STATIC_DRAW);

    glBufferSubData(GL_ARRAY_BUFFER, 4 * (GLintptr)sizeof(float), (GLsizeiptr)sizeof(subData), subData);
    CHECK(glGetError() == GL_NO_ERROR);
    /* Out-of-bounds subdata -> GL_INVALID_VALUE */
    glBufferSubData(GL_ARRAY_BUFFER, 5 * (GLintptr)sizeof(float), (GLsizeiptr)sizeof(subData), subData);
    CHECK(glGetError() == GL_INVALID_VALUE);

    /* LP64-safe VBO byte offset in glVertexPointer + glGetPointerv */
    glVertexPointer(2, GL_FLOAT, 0, (const GLvoid *)(uintptr_t)8);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
    CHECK((uintptr_t)ptr == 8u);

    /* Deleting currently bound buffer unbinds it */
    glDeleteBuffers(2, bufs);
    CHECK(glIsBuffer(bufs[0]) == GL_FALSE);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bparam);
    CHECK(bparam == 0);
    CHECK(glGetError() == GL_NO_ERROR);

    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_clear_and_readpixels(void)
{
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    RGBA8SurfaceFixture rgbaSurf;
    unsigned short rgb565Buf[8 * 8];
    oglSurfaceDescriptor rgb565Surf;
    unsigned char px[8 * 8 * 4];
    GLint fmt = 0, type = 0;

    bind_rgba8_surface(ctx, &rgbaSurf, 8, 8);

    glViewport(0, 0, 8, 8);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0x5A);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    CHECK(glGetError() == GL_NO_ERROR);

    /* Scissored clear of top-right 4x4 to green */
    glEnable(GL_SCISSOR_TEST);
    glScissor(4, 4, 4, 4);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    memset(px, 0, sizeof(px));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR);
    /* (1, 1) is red */
    CHECK(px[(1 * 8 + 1) * 4 + 0] == 255 &&
          px[(1 * 8 + 1) * 4 + 1] == 0 &&
          px[(1 * 8 + 1) * 4 + 2] == 0 &&
          px[(1 * 8 + 1) * 4 + 3] == 255);
    /* (6, 6) is green */
    CHECK(px[(6 * 8 + 6) * 4 + 0] == 0 &&
          px[(6 * 8 + 6) * 4 + 1] == 255 &&
          px[(6 * 8 + 6) * 4 + 2] == 0 &&
          px[(6 * 8 + 6) * 4 + 3] == 255);
    CHECK(rgbaSurf.stencil[0] == 0x5Au);

    /* Switch to RGB565 surface and verify GL_IMPLEMENTATION_COLOR_READ_*_OES */
    memset(&rgb565Surf, 0, sizeof(rgb565Surf));
    rgb565Surf.width             = 8;
    rgb565Surf.height            = 8;
    rgb565Surf.colorBuffer       = rgb565Buf;
    rgb565Surf.colorBufferStride = 8 * 2;
    rgb565Surf.colorBytes        = 2;
    rgb565Surf.redBits           = 5;
    rgb565Surf.redOffset         = 11;
    rgb565Surf.greenBits         = 6;
    rgb565Surf.greenOffset       = 5;
    rgb565Surf.blueBits          = 5;
    rgb565Surf.blueOffset        = 0;
    oglSetBuffers(ctx, &rgb565Surf, &rgb565Surf);

    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, &fmt);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, &type);
    CHECK(fmt == GL_RGBA && type == GL_UNSIGNED_BYTE);

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    memset(px, 0, 16);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(rgb565Buf[0] == 0x001Fu);
    CHECK(px[0] == 0 && px[1] == 0 && px[2] == 255 && px[3] == 255);

    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_rendering_pipeline(void)
{
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    RGBA8SurfaceFixture surf;
    unsigned char px[4];

    static const GLfloat fullQuadZ0[12] = {
        -1.0f, -1.0f,  0.0f,
         1.0f, -1.0f,  0.0f,
        -1.0f,  1.0f,  0.0f,
         1.0f,  1.0f,  0.0f
    };
    static const GLfloat fullQuadZFar[12] = {
        -1.0f, -1.0f,  0.6f,
         1.0f, -1.0f,  0.6f,
        -1.0f,  1.0f,  0.6f,
         1.0f,  1.0f,  0.6f
    };
    static const GLfloat innerQuad[8] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
        -0.5f,  0.5f,
         0.5f,  0.5f
    };
    static const GLushort quadIdx[6] = { 0, 1, 2, 2, 1, 3 };

    bind_rgba8_surface(ctx, &surf, 16, 16);

    glViewport(0, 0, 16, 16);
    glDisable(GL_DITHER);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* 1. Basic flat color quad via glDrawArrays + glDrawElements */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, fullQuadZFar);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK(glGetError() == GL_NO_ERROR);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);

    /* 2. Depth test: clear depth=1.0, draw red at z=0.6 (NDC->winZ=0.8),
     *    draw blue at z=0.0 (winZ=0.5, nearer -> passes), then green at z=0.6 (fails GL_LESS). */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glVertexPointer(3, GL_FLOAT, 0, fullQuadZFar);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIdx);
    CHECK_RGB_AT(8, 8, 255, 0, 0);

    glVertexPointer(3, GL_FLOAT, 0, fullQuadZ0);
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIdx);
    CHECK_RGB_AT(8, 8, 0, 0, 255);

    glVertexPointer(3, GL_FLOAT, 0, fullQuadZFar);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIdx);
    CHECK_RGB_AT(8, 8, 0, 0, 255); /* nearer blue preserved */
    glDisable(GL_DEPTH_TEST);

    /* 3. Alpha blending: 50% green over solid red -> ~(127, 128, 0) */
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glVertexPointer(3, GL_FLOAT, 0, fullQuadZ0);
    glColor4ub(0, 255, 0, 128);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] >= 125 && px[0] <= 129 && px[1] >= 126 && px[1] <= 130 && px[2] == 0);

    /* 4. Alpha test: GL_GREATER 0.5 rejects alpha=64, accepts alpha=200 */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4ub(255, 255, 0, 64);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 0 && px[1] == 0);
    glColor4ub(255, 255, 0, 200);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK_RGB_AT(8, 8, 255, 255, 0);
    glDisable(GL_ALPHA_TEST);

    /* 5. Stencil mask: mark inner [-0.5..0.5]^2 (pixels 4..11) with stencil=1,
     *    then draw full-screen cyan where stencil==1 */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glVertexPointer(2, GL_FLOAT, 0, innerQuad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glVertexPointer(3, GL_FLOAT, 0, fullQuadZ0);
    glColor4ub(0, 255, 255, 255);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_STENCIL_TEST);

    CHECK_RGB_AT(8, 8, 0, 255, 255); /* inside innerQuad */
    CHECK_RGB_AT(1, 1, 0, 0, 0); /* outside innerQuad */

    /* 6. VBO + EBO rendering with non-zero byte offsets on 64-bit LP64 */
    {
        GLuint vbo = 0, ebo = 0;
        /* Pad first 16 bytes so vertex data starts at byte offset 16,
         * and first 4 bytes of index buffer so indices start at byte offset 4. */
        struct {
            float pad[4];
            float pos[8];
        } vboPayload = {
            { 0, 0, 0, 0 },
            { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f }
        };
        const GLushort eboPayload[8] = { 99, 99,  0, 1, 2, 2, 1, 3 };

        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vboPayload), &vboPayload, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(eboPayload), eboPayload, GL_STATIC_DRAW);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glVertexPointer(2, GL_FLOAT, 0, (const GLvoid *)(uintptr_t)16);
        glColor4ub(200, 100, 50, 255);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)4);
        CHECK(glGetError() == GL_NO_ERROR);
        CHECK_RGB_AT(8, 8, 200, 100, 50);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }

    /* 7. Directional lighting on a +Z normal quad */
    {
        const GLfloat ambZero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const GLfloat lightDir[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
        const GLfloat lightDiff[4] = { 0.0f, 0.8f, 0.0f, 1.0f };
        const GLfloat matDiff[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambZero);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambZero);
        glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiff);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambZero);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, matDiff);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertexPointer(3, GL_FLOAT, 0, fullQuadZ0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisable(GL_LIGHT0);
        glDisable(GL_LIGHTING);

        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 0 && px[1] >= 200 && px[1] <= 208 && px[2] == 0);
    }

    /* 8. Non-identity ortho & perspective transforms + linear fog with GL_FLOAT VBO/IBO */
    {
        typedef struct { GLfloat x, y, z; unsigned char r, g, b, a; } FloatColVtx;
        static const FloatColVtx orthoQuad[4] = {
            {  4.0f,  4.0f, 0.0f, 255, 0, 0, 255 },
            { 12.0f,  4.0f, 0.0f, 255, 0, 0, 255 },
            { 12.0f, 12.0f, 0.0f, 255, 0, 0, 255 },
            {  4.0f, 12.0f, 0.0f, 255, 0, 0, 255 }
        };
        static const FloatColVtx perspQuad[4] = {
            { -4.0f, -4.0f, 0.0f, 255, 0, 0, 255 },
            {  4.0f, -4.0f, 0.0f, 255, 0, 0, 255 },
            {  4.0f,  4.0f, 0.0f, 255, 0, 0, 255 },
            { -4.0f,  4.0f, 0.0f, 255, 0, 0, 255 }
        };
        static const GLushort idx6[6] = { 0, 1, 2, 2, 3, 0 };
        static const GLfloat fogBlue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        GLuint vbo = 0, ibo = 0;

        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(orthoQuad), orthoQuad, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(idx6), idx6, GL_STATIC_DRAW);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof(0.0f, 16.0f, 16.0f, 0.0f, -100.0f, 1000.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, (GLsizei)sizeof(FloatColVtx), (const GLvoid *)(uintptr_t)0);
        glColorPointer(4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(FloatColVtx), (const GLvoid *)(uintptr_t)12);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)0);
        CHECK(glGetError() == GL_NO_ERROR);

        CHECK_RGB_AT(8, 8, 255, 0, 0);
        CHECK_RGB_AT(1, 1, 0, 0, 0);

        /* Perspective + modelview z=-5 + 50% linear fog [start=2, end=8] -> purple ~(127, 0, 127) */
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(perspQuad), perspQuad, GL_DYNAMIC_DRAW);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 20.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -5.0f);
        glEnable(GL_FOG);
        glFogf(GL_FOG_MODE, (GLfloat)GL_LINEAR);
        glFogf(GL_FOG_START, 2.0f);
        glFogf(GL_FOG_END, 8.0f);
        glFogfv(GL_FOG_COLOR, fogBlue);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)0);
        CHECK(glGetError() == GL_NO_ERROR);
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] >= 115 && px[0] <= 140 && px[1] == 0 && px[2] >= 115 && px[2] <= 140);

        /* Near z=-2.5 in [2, 8] -> mostly un-fogged red (px[0] > 200, px[2] < 40),
         * plus GL_LEQUAL / GL_GREATER depthFunc enum order & back-facing near-clipped quad */
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -2.5f);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)0);
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] > 200 && px[1] == 0 && px[2] < 40);

        /* GL_GREATER against closer depth should reject */
        glDepthFunc(GL_GREATER);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -2.0f);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)0);
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] > 200 && px[2] < 40);

        /* Back-facing (CW) near-plane-clipped floor quad with perspective tessellation */
        {
            static const FloatColVtx clipBackQuad[4] = {
                { -6.0f, -1.0f,  1.0f, 0, 255, 0, 255 },
                { -6.0f, -1.0f, -8.0f, 0, 255, 0, 255 },
                {  6.0f, -1.0f, -8.0f, 0, 255, 0, 255 },
                {  6.0f, -1.0f,  1.0f, 0, 255, 0, 255 }
            };
            glDepthFunc(GL_LEQUAL);
            glDisable(GL_FOG);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glLoadIdentity();
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(clipBackQuad), clipBackQuad, GL_DYNAMIC_DRAW);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)(uintptr_t)0);
            CHECK(glGetError() == GL_NO_ERROR);
            CHECK_RGB_AT(8, 2, 0, 255, 0);
        }
        glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glDisable(GL_FOG);
        glDisableClientState(GL_COLOR_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ibo);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_textures_and_drawtex(void)
{
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    RGBA8SurfaceFixture surf;
    GLuint tex[3] = { 0, 0, 0 };
    unsigned char px[4];

    /* 2x2 RGBA8 texture:
     * (0,0)=Red, (1,0)=Green, (0,1)=Blue, (1,1)=Yellow */
    static const unsigned char rgba2x2[4 * 4] = {
        255,   0,   0, 255,     0, 255,   0, 255,
          0,   0, 255, 255,   255, 255,   0, 255
    };
    static const GLfloat quadPos[8] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    static const GLfloat quadUV[8] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f
    };

    bind_rgba8_surface(ctx, &surf, 16, 16);

    glViewport(0, 0, 16, 16);
    glDisable(GL_DITHER);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    /* 1. Standard 2D RGBA texture + auto-mipmap + GL_REPLACE / GL_MODULATE / GL_COMBINE */
    glGenTextures(3, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    CHECK(glIsTexture(tex[0]) == GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba2x2);
    CHECK(glGetError() == GL_NO_ERROR);

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, quadPos);
    glTexCoordPointer(2, GL_FLOAT, 0, quadUV);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK(glGetError() == GL_NO_ERROR);

    /* Quadrant checks: (2,2)->Red, (13,2)->Green, (2,13)->Blue, (13,13)->Yellow */
    CHECK_RGB_AT(2, 2, 255, 0, 0);
    CHECK_RGB_AT(13, 2, 0, 255, 0);
    CHECK_RGB_AT(2, 13, 0, 0, 255);
    CHECK_RGB_AT(13, 13, 255, 255, 0);

    /* GL_COMBINE doubles the primary red value of 100 to approximately 200. */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
    glColor4ub(100, 100, 100, 255);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] >= 198 && px[0] <= 202 && px[1] == 0 && px[2] == 0);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    /* 2. OES_compressed_paletted_texture: GL_PALETTE4_RGBA8_OES (16-entry palette + 2x2 nibbles) */
    {
        unsigned char palData[16 * 4 + 2];
        memset(palData, 0, sizeof(palData));
        /* Palette entry 1 = Magenta (255, 0, 255, 255), entry 2 = Cyan (0, 255, 255, 255) */
        palData[1 * 4 + 0] = 255; palData[1 * 4 + 2] = 255; palData[1 * 4 + 3] = 255;
        palData[2 * 4 + 1] = 255; palData[2 * 4 + 2] = 255; palData[2 * 4 + 3] = 255;
        /* Palette4 stores the first texel in the high nibble of each byte. */
        palData[64] = (unsigned char)((1 << 4) | 2);
        palData[65] = (unsigned char)((2 << 4) | 1);

        glBindTexture(GL_TEXTURE_2D, tex[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGBA8_OES, 2, 2, 0,
                               (GLsizei)sizeof(palData), palData);
        CHECK(glGetError() == GL_NO_ERROR);

        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECK_RGB_AT(2, 2, 255, 0, 255);
        CHECK_RGB_AT(13, 2, 0, 255, 255);
    }

    /* 3. glCopyTexImage2D + OES_draw_texture (glDrawTexiOES) */
    {
        GLint crop[4] = { 0, 0, 8, 8 };
        /* Clear framebuffer to solid orange, copy 8x8 into tex[2], clear to black,
         * then draw tex[2] into (4, 4, 8, 8) using glDrawTexiOES! */
        glDisable(GL_TEXTURE_2D);
        glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, tex[2]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        {
            static const int emptySizes[3][2] = { { 0, 8 }, { 8, 0 }, { 0, 0 } };
            int i;
            for (i = 0; i < 3; ++i)
            {
                _grTexture *texture = oglGetActiveTexture(ctx, GR_TEXTURETARGET_2D);
                glCopyTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 0, 0,
                                 emptySizes[i][0], emptySizes[i][1], 0);
                CHECK(glGetError() == GL_INVALID_VALUE);
                CHECK(texture->mipmap[1].initialized == 1);
                CHECK(texture->mipmap[1].pixels == NULL);
                CHECK(texture->mipmap[1].log2Dim[0] == (emptySizes[i][0] ? 3 : -1));
                CHECK(texture->mipmap[1].log2Dim[1] == (emptySizes[i][1] ? 3 : -1));
            }
        }
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
        CHECK(glGetError() == GL_NO_ERROR);

        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glDrawTexiOES(4, 4, 0, 8, 8);
        CHECK(glGetError() == GL_NO_ERROR);

        /* Inside (4..11, 4..11): orange; outside (1, 1): black */
        glReadPixels(6, 6, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 255 && px[1] >= 126 && px[1] <= 129 && px[2] == 0);
        CHECK_RGB_AT(1, 1, 0, 0, 0);

        /* Also verify 4-quadrant orientation of glDrawTexiOES on tex[0] (2x2 RGBA):
         * crop = {0, 0, 2, 2} -> bottom-left=Red, bottom-right=Green, top-left=Blue, top-right=Yellow */
        {
            GLint crop2[4] = { 0, 0, 2, 2 };
            glBindTexture(GL_TEXTURE_2D, tex[0]);
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop2);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawTexiOES(0, 0, 0, 16, 16);
            CHECK_RGB_AT(2, 2, 255, 0, 0);
            CHECK_RGB_AT(13, 2, 0, 255, 0);
            CHECK_RGB_AT(2, 13, 0, 0, 255);
            CHECK_RGB_AT(13, 13, 255, 255, 0);
        }
    }

    glDeleteTextures(3, tex);
    CHECK(glIsTexture(tex[0]) == GL_FALSE);
    CHECK(glGetError() == GL_NO_ERROR);

    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_buffer_storage_lifetime(void)
{
    typedef struct BufferVertex_s {
        GLfloat x, y;
        GLubyte color[4];
    } BufferVertex;
    struct {
        GLfloat padding[4];
        BufferVertex vertices[4];
    } payload = {
        { 0, 0, 0, 0 },
        {
            { -0.5f, -0.5f, { 255, 0, 0, 255 } },
            {  0.5f, -0.5f, { 255, 0, 0, 255 } },
            { -0.5f,  0.5f, { 255, 0, 0, 255 } },
            {  0.5f,  0.5f, { 255, 0, 0, 255 } }
        }
    };
    oglContext_s *contexts[2];
    RGBA8SurfaceFixture surf;
    unsigned char px[4];
    GLuint buffer;
    int i, pass;

    contexts[0] = oglCreateContext(NULL, NULL);
    contexts[1] = oglCreateContext(contexts[0], NULL);
    init_rgba8_surface(&surf, 8, 8);
    egliSetCurrentContext(contexts[0]);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(payload), &payload, GL_DYNAMIC_DRAW);

    for (i = 0; i < 2; ++i)
    {
        oglSetBuffers(contexts[i], &surf.descriptor, &surf.descriptor);
        egliSetCurrentContext(contexts[i]);
        glViewport(0, 0, 8, 8);
        glDisable(GL_DITHER);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glVertexPointer(2, GL_FLOAT, sizeof(BufferVertex), (const GLvoid *)(uintptr_t)16);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(BufferVertex), (const GLvoid *)(uintptr_t)24);
        glClientActiveTexture(GL_TEXTURE1);
        glTexCoordPointer(2, GL_FLOAT, sizeof(BufferVertex), (const GLvoid *)(uintptr_t)16);
        glClientActiveTexture(GL_TEXTURE0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    /* A buffer can supply vertex attributes even when replaced through the
     * element target or from another context in its sharing group. */
    for (pass = 0; pass < 2; ++pass)
    {
        GLenum target = pass == 0 ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        oglBufObj_s *object;
        egliSetCurrentContext(contexts[pass]);
        for (i = 0; i < 4; ++i)
        {
            payload.vertices[i].x = (i & 1) ? 1.0f : -1.0f;
            payload.vertices[i].y = (i & 2) ? 1.0f : -1.0f;
            payload.vertices[i].color[0] = 0;
            payload.vertices[i].color[1] = pass == 0 ? 255 : 0;
            payload.vertices[i].color[2] = pass == 0 ? 0 : 255;
        }
        glBindBuffer(target, buffer);
        glBufferData(target, sizeof(payload), &payload, GL_DYNAMIC_DRAW);
        CHECK(glGetError() == GL_NO_ERROR);
        object = oglGetBufObj(contexts[pass]->bufObjManager, buffer);

        for (i = 0; i < 2; ++i)
        {
            _grVertexArray *arrays = contexts[i]->gerbera->vertexArray;
            int addressesValid = arrays[GR_VERTEXARRAY_POSITION].data == (char *)object->buffer + 16 &&
                                 arrays[GR_VERTEXARRAY_COLOR].data == (char *)object->buffer + 24;
            CHECK(addressesValid);
            /* Avoid dereferencing freed storage when this regression fails. */
            if (!addressesValid)
                continue;
            egliSetCurrentContext(contexts[i]);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(px[0] == 0 && px[1] == (pass == 0 ? 255 : 0) &&
                  px[2] == (pass == 0 ? 0 : 255) && px[3] == 255);
            CHECK(glGetError() == GL_NO_ERROR);
        }
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
    for (i = 0; i < 2; ++i)
    {
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_POSITION].data == NULL);
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_COLOR].data == NULL);
    }

    /* Deletion detaches arrays in both contexts, including a client texture
     * unit that is not active. Reusing the name must not revive those arrays. */
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(payload), &payload, GL_DYNAMIC_DRAW);
    glDeleteBuffers(1, &buffer);
    for (i = 0; i < 2; ++i)
    {
        GLint binding = -1;
        /* The original retains saved pointer/binding queries after deletion. */
        GLvoid *pointer = (GLvoid *)(uintptr_t)1;
        egliSetCurrentContext(contexts[i]);
        glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &binding);
        CHECK(binding == (GLint)buffer);
        glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
        CHECK(pointer == (GLvoid *)(uintptr_t)16);
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_POSITION].data == NULL);
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_COLOR].data == NULL);
        glClientActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, &binding);
        CHECK(binding == (GLint)buffer);
        glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &pointer);
        CHECK(pointer == (GLvoid *)(uintptr_t)16);
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(payload), &payload, GL_DYNAMIC_DRAW);
    for (i = 0; i < 2; ++i)
    {
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_POSITION].data == NULL);
        CHECK(contexts[i]->gerbera->vertexArray[GR_VERTEXARRAY_TEXCOORD0 + 1].data == NULL);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    glDeleteBuffers(1, &buffer);
    egliSetCurrentContext(NULL);
    oglDestroyContext(contexts[1]);
    oglDestroyContext(contexts[0]);
}

static void test_gl_viewport_resize(void)
{
    const EGLint attributes[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
                                 EGL_NONE };
    const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    eglNativeWindow_s window = { 4, 4, 0, EGL_SYMBIAN_ECOLOR64K, NULL, 0 };
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig config;
    EGLint count;
    EGLSurface surface;
    EGLContext handle;
    oglContext_s *context;
    GLint viewport[4];

    CHECK(eglInitialize(display, NULL, NULL) == EGL_TRUE);
    CHECK(eglChooseConfig(display, attributes, &config, 1, &count) == EGL_TRUE && count == 1);
    surface = eglCreateWindowSurface(display, config, &window, NULL);
    handle = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    CHECK(surface != EGL_NO_SURFACE && handle != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(display, surface, surface, handle) == EGL_TRUE);
    context = egliGetState()->currentESContext;

    /* The first viewport after a native resize must use the refreshed height. */
    window.width = window.height = 8;
    glViewport(0, 0, 8, 8);
    CHECK(context->drawSurfaceHeight == 8);
    CHECK(context->gerbera->viewportRectangle.y0 == 0 &&
          context->gerbera->viewportRectangle.y1 == 8);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, quad);
    glColor4ub(255, 0, 0, 255);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK_RGB_AT(3, 0, 255, 0, 0);
    CHECK_RGB_AT(3, 7, 255, 0, 0);

    glViewport(5, -7, 4096, 8192);
    glGetIntegerv(GL_VIEWPORT, viewport);
    CHECK(viewport[0] == 5 && viewport[1] == -7 && viewport[2] == 2048 && viewport[3] == 2048);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(eglTerminate(display) == EGL_TRUE);
}

static void test_gl_white_modulate_and_skinning(void)
{
    oglContext_s *ctx;
    RGBA8SurfaceFixture surf;
    unsigned char px[4];
    GLuint tex[2];
    GLint envMode = 0;
    const GLfloat quadVerts[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    const GLfloat quadUV[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    const GLubyte nonWhiteColors[] = {
        128, 255, 255, 255,  128, 255, 255, 255,
        128, 255, 255, 255,  128, 255, 255, 255
    };
    const unsigned char rgbaTexel[4] = { 200, 100, 50, 128 };
    const unsigned char lumTexel[1] = { 180 };
    void *replaceCode, *whiteModCode, *tintedModCode;
    unsigned int replaceGrads, whiteModGrads;

    ctx = oglCreateContext(NULL, NULL);
    init_rgba8_surface(&surf, 8, 8);
    oglSetBuffers(ctx, &surf.descriptor, &surf.descriptor);
    egliSetCurrentContext(ctx);
    glViewport(0, 0, 8, 8);
    glDisable(GL_DITHER);

    glGenTextures(2, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaTexel);

    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, quadVerts);
    glTexCoordPointer(2, GL_FLOAT, 0, quadUV);

    /* Explicit GL_REPLACE baseline. */
    glColor4ub(255, 255, 255, 255);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    replaceCode = (void *)ctx->gerbera->pixelPipelineCode;
    replaceGrads = ctx->gerbera->activeGradientMask;
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 200 && px[1] == 100 && px[2] == 50 && px[3] == 128);

    /* Constant white GL_MODULATE reuses the exact GL_REPLACE pipeline
     *    while keeping GL_TEXTURE_ENV_MODE observable as GL_MODULATE. */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &envMode);
    CHECK(envMode == GL_MODULATE);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    whiteModCode = (void *)ctx->gerbera->pixelPipelineCode;
    whiteModGrads = ctx->gerbera->activeGradientMask;
    CHECK(ctx->gerbera->constWhiteModulate == 1);
    CHECK(whiteModCode == replaceCode && whiteModGrads == replaceGrads);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 200 && px[1] == 100 && px[2] == 50 && px[3] == 128);

    /* Non-white constant color uses MODULATE. */
    glColor4ub(128, 255, 255, 64);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    tintedModCode = (void *)ctx->gerbera->pixelPipelineCode;
    CHECK(ctx->gerbera->constWhiteModulate == 0);
    CHECK(tintedModCode != replaceCode);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] >= 99 && px[0] <= 102 && px[1] == 100 && px[2] == 50 && px[3] >= 31 && px[3] <= 33);

    /* White modulation also supports GL_LUMINANCE
     *    (where Cf = Lt, Af = Ap = 1.0 matches GL_REPLACE) and glDrawTexiOES. */
    glColor4ub(255, 255, 255, 255);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lumTexel);
    {
        GLint crop[4] = { 0, 0, 1, 1 };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawTexiOES(0, 0, 0, 8, 8);
        CHECK(ctx->gerbera->constWhiteModulate == 1);
        glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 180 && px[1] == 180 && px[2] == 180 && px[3] == 255);
    }

    /* Enabling GL_COLOR_ARRAY disables constWhiteModulate. */
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, nonWhiteColors);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK(ctx->gerbera->constWhiteModulate == 0);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] >= 99 && px[0] <= 102 && px[1] == 100);
    glDisableClientState(GL_COLOR_ARRAY);

    /* Indexed GL_MATRIX_PALETTE_OES skinning exercises grTransformVertexPreloaded
     *    across both cache misses and shared-vertex cache hits. */
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    {
        const GLfloat skinVerts[] = { -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
        const GLfloat weights[] = { 0.5f, 0.5f,  0.5f, 0.5f,  0.5f, 0.5f,  0.5f, 0.5f };
        const GLubyte indices[] = { 0, 1,  0, 1,  0, 1,  0, 1 };
        const GLubyte elemIdx[] = { 0, 1, 2, 2, 1, 3 };
        glEnable(GL_MATRIX_PALETTE_OES);
        glMatrixMode(GL_MATRIX_PALETTE_OES);
        glCurrentPaletteMatrixOES(0);
        glLoadIdentity();
        glTranslatef(-0.5f, 0.0f, 0.0f);
        glCurrentPaletteMatrixOES(1);
        glLoadIdentity();
        glTranslatef(0.5f, 0.0f, 0.0f);
        glMatrixMode(GL_MODELVIEW);
        glEnableClientState(GL_WEIGHT_ARRAY_OES);
        glEnableClientState(GL_MATRIX_INDEX_ARRAY_OES);
        glVertexPointer(2, GL_FLOAT, 0, skinVerts);
        glWeightPointerOES(2, GL_FLOAT, 0, weights);
        glMatrixIndexPointerOES(2, GL_UNSIGNED_BYTE, 0, indices);
        glColor4ub(0, 220, 110, 255);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, elemIdx);
        CHECK_RGB_AT(4, 4, 0, 220, 110);
        CHECK_RGB_AT(0, 0, 0, 0, 0);
        glDisableClientState(GL_WEIGHT_ARRAY_OES);
        glDisableClientState(GL_MATRIX_INDEX_ARRAY_OES);
        glDisable(GL_MATRIX_PALETTE_OES);
    }

    glDeleteTextures(2, tex);
    CHECK(glGetError() == GL_NO_ERROR);
    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

static void test_gl_texture_layout_specialization(void)
{
#if GERBERA_ARMV6_JIT
    static const struct { int w, h, s, t, filter; } cases[] = {
        {1, 1, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {2, 2, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {1, 4, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {4, 1, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {1, 4, GR_WRAPMODE_MIRRORED_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {1, 4, GR_WRAPMODE_REPEAT, GR_WRAPMODE_MIRRORED_REPEAT, GL_NEAREST},
        {1, 4, GR_WRAPMODE_CLAMP_TO_EDGE, GR_WRAPMODE_REPEAT, GL_NEAREST},
        {1, 1, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST_MIPMAP_NEAREST},
        {4, 4, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_LINEAR},
        {1, 1, GR_WRAPMODE_REPEAT, GR_WRAPMODE_REPEAT, GL_NEAREST}
    };
    const GLfloat vertices[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    const GLfloat coords[] = {-2, -2, 3, -2, -2, 3, 3, 3};
    unsigned char texels[4 * 4 * 4], seen[128] = {0};
    RGBA8SurfaceFixture surface;
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    Gerbera_s *state;
    _grTexture *texture;
    void *firstCode = NULL, *secondCode = NULL, *previousCode = NULL;
    unsigned int previousLayout = 0;
    GLuint names[2];
    unsigned int i;
    int w, h;

    CHECK(ctx != NULL);
    if (ctx == NULL) return;
    state = ctx->gerbera;
    bind_rgba8_surface(ctx, &surface, 8, 8);
    glViewport(0, 0, 8, 8);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, coords);
    glGenTextures(2, names);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (i = 0; i < sizeof(texels); i += 4)
    {
        texels[i] = 32; texels[i + 1] = 96; texels[i + 2] = 160; texels[i + 3] = 255;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        unsigned int layout;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, cases[i].filter);
        grSetTextureWrapMode(state->textureUnit[0].texture, GR_TEXCOORD_S, cases[i].s);
        grSetTextureWrapMode(state->textureUnit[0].texture, GR_TEXCOORD_T, cases[i].t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cases[i].w, cases[i].h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texels);
        texture = state->textureUnit[0].texture;
        if (i != 0 && grGetPixelTextureLayout(state) != state->pixelTextureLayout)
        {
            /* A shared context may consume the texture's dirty bit. */
            grUpdateTexture(texture);
            state->pixelPipelineDirty = 0;
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        layout = grGetPixelTextureLayout(state);
        CHECK(state->pixelTextureLayout == layout);
        CHECK((layout != 0) == (cases[i].filter == GL_NEAREST && cases[i].s != GR_WRAPMODE_CLAMP_TO_EDGE));
        if (i != 0 && layout != previousLayout)
            CHECK((void *)state->pixelPipelineCode != previousCode);
        if (i == 0) firstCode = (void *)state->pixelPipelineCode;
        if (i == 1) secondCode = (void *)state->pixelPipelineCode;
        previousCode = (void *)state->pixelPipelineCode;
        previousLayout = layout;
        CHECK_RGB_AT(3, 3, 32, 96, 160);
        CHECK(glGetError() == GL_NO_ERROR);
    }
    CHECK((void *)state->pixelPipelineCode == firstCode);
    texture = state->textureUnit[0].texture;
    for (w = 0; w <= 10; ++w)
        for (h = 0; h <= 10; ++h)
        {
            unsigned int key;
            texture->mipmap[0].log2Dim[0] = w;
            texture->mipmap[0].log2Dim[1] = h;
            key = grGetPixelTextureLayout(state);
            CHECK(key > 0 && key < 128 && !seen[key]);
            seen[key] = 1;
        }
    texture->mipmap[0].log2Dim[0] = 11;
    CHECK(grGetPixelTextureLayout(state) == 0);
    texture->mipmap[0].log2Dim[0] = texture->mipmap[0].log2Dim[1] = 0;
    glBindTexture(GL_TEXTURE_2D, names[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK((void *)state->pixelPipelineCode == secondCode);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK((void *)state->pixelPipelineCode == firstCode);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, names[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glEnable(GL_TEXTURE_2D);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK(state->pixelTextureLayout == 0);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK((void *)state->pixelPipelineCode == firstCode);
    glDeleteTextures(2, names);
    CHECK(glGetError() == GL_NO_ERROR);
    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
#endif
}

static void test_gl_common_transform_clipping(void)
{
    oglContext_s *ctx = oglCreateContext(NULL, NULL);
    RGBA8SurfaceFixture surface;
    unsigned int expected[16 * 16];
    static const unsigned short triangles[] = {0, 1, 2, 2, 1, 3, 0, 1, 2};
    static const unsigned short lines[] = {0, 1, 2, 1, 1, 3};
    static const unsigned char colors[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255},
                                              {0, 0, 255, 255}, {255, 255, 255, 255}};
    float vertices[4][4] = {{-.9f, -.8f, 0, 1}, {.9f, -.8f, 0, 1},
                            {-.9f, .8f, 0, 1}, {.9f, .8f, 0, 1}};
    float matrix[16] = {.713213f, 0, 0, 0, 0, .612197f, 0, 0,
                        0, 0, 256.12345f, 0, 0, 0, 0, 1};
    int plane, mode, i;
    CHECK(ctx != NULL);
    if (!ctx) return;
    bind_rgba8_surface(ctx, &surface, 16, 16);
    glViewport(0, 0, 16, 16);
    glDisable(GL_DITHER);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(colors[0]), colors);
    glVertexPointer(4, GL_FLOAT, sizeof(vertices[0]), vertices);
    glColor4f(.7f, .3f, .9f, 1);
    glClearColor(0, 0, 0, 0);
    for (plane = 0; plane < 2; ++plane)
        for (mode = 0; mode < 2; ++mode)
        {
            GLenum primitive = mode ? GL_LINES : GL_TRIANGLES;
            const unsigned short *indices = mode ? lines : triangles;
            int count = mode ? 6 : 9, fast = 0, colored = 0;
            matrix[14] = (plane ? 1.000000119f : -1.000000119f) - matrix[10];
            for (i = 0; i < 4; ++i)
                vertices[i][2] = 1.0f + (i == 1 ? 0.0000013f : -0.0000017f) * (plane ? 1 : -1);
            glLoadMatrixf(matrix);
            glDrawElements(primitive, count, GL_UNSIGNED_SHORT, indices);
            CHECK(ctx->gerbera->commonTransform.eligible);
            for (i = 0; i < 4; ++i)
            {
                _IFVector4 input;
                _IVector4 output;
                int bits[4];
                memcpy(bits, vertices[i], sizeof(bits));
                IFVector4_setfv(&input, bits, 4);
                fast += IVertexTransform_mulfv4Common(&output, &ctx->gerbera->commonTransform, &input);
            }
            CHECK(fast > 0);
            ctx->gerbera->commonTransform.eligible = 0;
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(primitive, count, GL_UNSIGNED_SHORT, indices);
            memcpy(expected, surface.color, sizeof(expected));
            for (i = 0; i < 16 * 16; ++i) colored += expected[i] != 0;
            CHECK(colored > 0);
            ctx->gerbera->commonTransform.eligible = 1;
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(primitive, count, GL_UNSIGNED_SHORT, indices);
            CHECK(memcmp(expected, surface.color, sizeof(expected)) == 0);
            CHECK(glGetError() == GL_NO_ERROR);
        }
    egliSetCurrentContext(NULL);
    oglDestroyContext(ctx);
}

int main(void)
{
    test_gl_common_transform_clipping();
    test_gl_texture_layout_specialization();
    test_gl_white_modulate_and_skinning();
    test_error_and_descriptor();
    test_matrix_stack();
    test_tex_manager();
    test_bufobj_manager();
    test_object_name_hash_wrap();
    test_getters();
    test_ogl_util();
    test_ogl_context();
    test_gl_strings_errors_and_state();
    test_gl_matrices_and_buffers();
    test_gl_clear_and_readpixels();
    test_gl_rendering_pipeline();
    test_gl_textures_and_drawtex();
    test_gl_buffer_storage_lifetime();
    test_gl_viewport_resize();

    printf("opengl tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
