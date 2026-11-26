/* Core state, geometry, rasterization, texture and compiler tests. */
#include "gerbera/grDefs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Render-stack records contain both pointers and 64-bit cache masks. */
#define TEST_RENDER_STACK(name) \
    static union { HGint64 alignment; unsigned char bytes[GR_RENDER_STACK_SIZE]; } name##Storage; \
    unsigned char *name = name##Storage.bytes
#define TEST_PIXEL_PIPE_OFFSET (14644 + HC_extraStackSpace(grGetBackend()))

#include "test_support.h"

static void test_descriptors(void)
{
    _grSurfaceDescriptor a, b;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    grClearDescriptor(&a);
    CHECK(a.rgbaOffsets[0] == 0 && a.rgbaSizes[0] == 0);
    CHECK(a.colorMask == 0 && a.colorBytes == 0 && a.depthBytes == 0);
    CHECK(a.stencilBytes == 0 && a.pixelFormat == 0);
    grSetDescriptorSizes(&a, 5, 6, 5, 8);
    CHECK(grGetDescriptorSize(&a, 0) == 5);
    CHECK(grGetDescriptorSize(&a, 1) == 6);
    CHECK(grGetDescriptorSize(&a, 3) == 8);
    grSetDescriptorOffsets(&a, 0, 5, 11, 16);
    CHECK(grGetDescriptorOffset(&a, 0) == 0);
    CHECK(grGetDescriptorOffset(&a, 2) == 11);
    grClearDescriptor(&b);
    CHECK(grDescriptorsEqual(&a, &b) == 0);
    grSetDescriptorSizes(&b, 5, 6, 5, 8);
    grSetDescriptorOffsets(&b, 0, 5, 11, 16);
    CHECK(grDescriptorsEqual(&a, &b) == 1);
    b.colorMask = 0xF0;
    CHECK(grDescriptorsEqual(&a, &b) == 0);
    b.colorMask = 0;
    CHECK(grDescriptorsEqual(&a, &b) == 1);
    b.pixelFormat = 3;
    CHECK(grDescriptorsEqual(&a, &b) == 0);
}

static void test_rectangles(void)
{
    _grRect r;
    grSetRectangle(&r, 0, 100, 0, 200);
    CHECK(grIsRectangleEmpty(&r) == 0);
    grSetRectangle(&r, 10, 10, 0, 200);
    CHECK(grIsRectangleEmpty(&r) == 1);
    grSetRectangle(&r, 0, 100, 10, 10);
    CHECK(grIsRectangleEmpty(&r) == 1);
    CHECK(grIsAligned((void *)0x1000, 4) == 1);
    CHECK(grIsAligned((void *)0x1002, 4) == 0);
    CHECK(grIsAligned((void *)0x1003, 4) == 0);
    CHECK(grIsAligned((void *)0x1004, 4) == 1);
}

static void test_formats(void)
{
    /* data type size table verbatim */
    CHECK(grGetDataTypeSize(0) == 1);
    CHECK(grGetDataTypeSize(1) == 1);
    CHECK(grGetDataTypeSize(2) == 2);
    CHECK(grGetDataTypeSize(3) == 2);
    CHECK(grGetDataTypeSize(4) == 4);
    CHECK(grGetDataTypeSize(5) == 4);
    CHECK(grGetDataTypeSize(6) == 4);
    CHECK(grGetDataTypeSize(7) == 4);
    CHECK(grGetDataTypeSize(8) == 1);
    CHECK(GR_DATATYPE_MAX == 9);
    /* image format component counts: {1,3,4,1,2,3,4} */
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_ALPHA) == 1);
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_RGB) == 3);
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_RGBA) == 4);
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_LUMINANCE) == 1);
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_LUMINANCE_ALPHA) == 2);
    CHECK(grGetImageFormatNumComponents(GR_IMAGEFORMAT_BGR) == 3);
    CHECK(grGetImageFormatNumComponents(6) == 4);
}

static void test_stack(void)
{
    _grStackAllocator st;
    static char buf[256];
    char *p, *q;

    grStackInit(&st, buf, 256);
    CHECK(grIsStackValid(&st) == 1);
    CHECK(grStackGetFreeSpace(&st) == 256);
    p = (char *)grStackAllocate(&st, 17);
    CHECK(p == buf);
    CHECK(grIsAligned(p, 4) == 1);
    CHECK(grStackGetFreeSpace(&st) == 256 - 20);   /* 17 rounded up to 20 */
    q = (char *)grStackAllocate(&st, 40);
    CHECK(q == buf + 20);
    CHECK(grStackGetFreeSpace(&st) == 256 - 60);
    grStackFree(&st, q);
    CHECK(grStackGetFreeSpace(&st) == 256 - 20);
    grStackFree(&st, p);
    CHECK(grStackGetFreeSpace(&st) == 256);
    CHECK(grStackGetPtr(&st, 128) == buf + 128);
}

static void test_rgba(void);
static void test_pack_color(void);
static void test_texture(void);
static void test_util_helpers(void);
static void test_state_cull(void);
static void test_state_viewport(void);
static void test_state_pixel(void);
static void test_state_material(void);
static void test_state_lights(void);
static void test_transform(void);
static void test_update_lights(void);
static void test_itc_transform(void);
static void test_fetch_stages(void);
static void test_update_pipeline(void);
static void test_transform_vertex(void);
static void test_light_vertex(void);
static void test_setup_culling(void);
static void test_clear(void);
static void test_clipper(void);
static void test_vertextcache(void);
static void test_queueprimitive(void);
static void test_fetchprimitives(void);
static void test_scanconvert(void);
static void test_interpolate(void);
static void test_linepoint(void);
static void test_tessellate(void);
static void test_pipecache(void);
static void test_signature(void);
static void test_setuprendering(void);
static void test_texturerect(void);
static void test_renderrect(void);
static void test_hcopcode(void);
static void test_interpreter(void);
static void test_hcx86backend(void);
static void test_hcarmbackend(void);
static void test_hcdatastructs_and_emulator(void);
static void test_hccompiler(void);
static void test_gradients(void);
static void test_depth_span_rejection(void);
static void test_early_depth_gradients(void);
static void test_grr_texture_and_blit(void);
static void test_pixel_sampling(void);
static void test_gerbera_lifecycle_and_api_small(void);

int main(void)
{
    test_descriptors();
    test_rectangles();
    test_formats();
    test_stack();
    test_rgba();
    test_pack_color();
    test_texture();
    test_util_helpers();
    test_state_cull();
    test_state_viewport();
    test_state_pixel();
    test_state_material();
    test_state_lights();
    test_transform();
    test_update_lights();
    test_itc_transform();
    test_fetch_stages();
    test_update_pipeline();
    test_transform_vertex();
    test_light_vertex();
    test_setup_culling();
    test_clear();
    test_clipper();
    test_vertextcache();
    test_queueprimitive();
    test_fetchprimitives();
    test_gradients();
    test_depth_span_rejection();
    test_early_depth_gradients();
    test_scanconvert();
    test_interpolate();
    test_linepoint();
    test_tessellate();
    test_pipecache();
    test_signature();
    test_setuprendering();
    test_texturerect();
    test_renderrect();
    test_hcopcode();
    test_interpreter();
    test_hcx86backend();
    test_hcarmbackend();
    test_hcdatastructs_and_emulator();
    test_hccompiler();
    test_grr_texture_and_blit();
    test_pixel_sampling();
    test_gerbera_lifecycle_and_api_small();
    printf("core tests: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}

/* --- grRGBA.h + grUtilSmall.inl ------------------------------------------ */
#include "gerbera/grRGBA.h"

static void test_rgba(void)
{
    unsigned int c = grSetRGBA(0x12, 0x34, 0x56, 0x78);
    CHECK(c == 0x78563412u);
    CHECK(grUnpackRGBA(c, GR_COLOR_R) == 0x12);
    CHECK(grUnpackRGBA(c, GR_COLOR_G) == 0x34);
    CHECK(grUnpackRGBA(c, GR_COLOR_B) == 0x56);
    CHECK(grUnpackRGBA(c, GR_COLOR_A) == 0x78);
    CHECK(grPackRGBA(0xAB, GR_COLOR_B) == 0x00AB0000u);
    CHECK(grPackRGBA(0xFF, GR_COLOR_A) == 0xFF000000u);
}

static void test_pack_color(void)
{
    _grSurfaceDescriptor desc;
    /* RGBA8888: standard byte layout */
    memset(&desc, 0, sizeof(desc));
    grSetDescriptorSizes(&desc, 8, 8, 8, 8);
    grSetDescriptorOffsets(&desc, 0, 8, 16, 24);
    CHECK(grIsValidDescriptor(&desc) == 1);
    CHECK(grPackColor(grSetRGBA(0x11, 0x22, 0x33, 0x44), &desc) == 0x44332211u);
    CHECK(grUnpackColor(0x44332211u, &desc) == grSetRGBA(0x11, 0x22, 0x33, 0x44));

    /* RGB565: r 5 @11, g 6 @5, b 5 @0, alpha none */
    grSetDescriptorSizes(&desc, 5, 6, 5, 0);
    grSetDescriptorOffsets(&desc, 11, 5, 0, 0);
    {
        unsigned int packed = grPackColor(grSetRGBA(0xFF, 0xFF, 0xFF, 0xFF), &desc);
        CHECK(packed == 0xFFFFu);
        {
            unsigned int up = grUnpackColor(packed, &desc);
            CHECK(grUnpackRGBA(up, GR_COLOR_R) == 0xFF);
            CHECK(grUnpackRGBA(up, GR_COLOR_G) == 0xFF);
            CHECK(grUnpackRGBA(up, GR_COLOR_B) == 0xFF);
            CHECK(grUnpackRGBA(up, GR_COLOR_A) == 0xFF);   /* size 0 alpha -> 255 */
        }
    }
    /* small channel expansions */
    grSetDescriptorSizes(&desc, 1, 2, 3, 8);
    grSetDescriptorOffsets(&desc, 0, 1, 3, 11);
    {
        unsigned int up = grUnpackColor(0x1u, &desc);   /* r bit set */
        CHECK(grUnpackRGBA(up, GR_COLOR_R) == 0xFF);
    }
    {
        unsigned int up = grUnpackColor(0x2u, &desc);   /* g = 01 */
        CHECK(grUnpackRGBA(up, GR_COLOR_G) == 0x55);   /* 01 -> 01010101 */
    }
    {
        unsigned int up = grUnpackColor(0x8u, &desc);   /* b = 001 */
        CHECK(grUnpackRGBA(up, GR_COLOR_B) == 36);     /* (c>>1)|32c|4c, c=1 -> 36 */
    }

    /* color write mask: only R and B enabled */
    grSetDescriptorSizes(&desc, 8, 8, 8, 8);
    grSetDescriptorOffsets(&desc, 0, 8, 16, 24);
    CHECK(grGetPackedColorWriteMask(&desc, 0x5u) == 0x00FF00FFu);
    CHECK(grGetPackedColorWriteMask(&desc, 0x2u) == 0x0000FF00u);
    CHECK(grGetPackedColorWriteMask(&desc, 0x0u) == 0u);

    /* masked descriptor equality */
    {
        _grSurfaceDescriptor a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        grSetDescriptorSizes(&a, 5, 6, 5, 8);
        grSetDescriptorOffsets(&a, 11, 5, 0, 24);
        grSetDescriptorSizes(&b, 5, 6, 5, 8);
        grSetDescriptorOffsets(&b, 11, 5, 0, 24);
        CHECK(grMaskedDescriptorsEqual(0xFu, &a, &b) == 1);
        b.rgbaOffsets[1] = 6;
        CHECK(grMaskedDescriptorsEqual(0xFu, &a, &b) == 0);
        CHECK(grMaskedDescriptorsEqual(0xDu, &a, &b) == 1);  /* G bit not in mask */
        CHECK(grMaskedDescriptorsEqual(0x2u, &a, &b) == 0);
    }
}

/* --- grTexture.h ---------------------------------------------------------- */
#include "gerbera/grTexture.h"

static void test_texture(void)
{
    _grTexture tex;
    int dims[2];
    void *p;

    grInitTexture(&tex);
    CHECK(grIsValidTexture(&tex) == 1);
    CHECK(grGetTextureTarget(&tex) == GR_TEXTURETARGET_MAX);   /* unbound */
    CHECK(tex.filterMask[GR_FILTERTYPE_MIN] == 6);
    CHECK(tex.filterMask[GR_FILTERTYPE_MAG] == 1);
    CHECK(tex.numMipLevels == 1);
    CHECK(tex.mipmap[0].texFormat == GR_TEXTUREFORMAT_MAX);
    CHECK(tex.mipmap[10].log2Dim[0] == -1);
    CHECK(tex.genMipMaps == 0);
    CHECK(grIsPalettedTextureFormat(5) == 1);
    CHECK(grIsPalettedTextureFormat(14) == 1);
    CHECK(grIsPalettedTextureFormat(4) == 0);
    CHECK(grIsPalettedTextureFormat(15) == 0);
    CHECK(grGetTextureTargetDimensionality(GR_TEXTURETARGET_2D) == 2);

    /* bind (grUpdateTexture walks mips via dimensionality, which asserts 2D) */
    grSetTextureTarget(&tex, GR_TEXTURETARGET_2D);
    CHECK((tex.parmMask & 1u) != 0);
    /* set up a 4x4 level-0 image, no mipmaps */
    {
        static unsigned char pix[16 * 4];
        tex.mipmap[0].pixels = pix;
        tex.mipmap[0].log2Dim[0] = 2;
        tex.mipmap[0].log2Dim[1] = 2;
        tex.mipmap[0].texFormat = GR_TEXTUREFORMAT_RGBA;
        tex.mipmap[0].initialized = 1;
    }
    p = grGetTextureMipLevel(&tex, 0, dims);
    CHECK(p != NULL);
    CHECK(dims[0] == 2 && dims[1] == 2);
    CHECK(grGetTextureMipLevel(&tex, 1, dims) == NULL);
    CHECK(grGetTextureMipLevelFormat(&tex, 0) == GR_TEXTUREFORMAT_RGBA);
    CHECK(grTextureMipLevelInitialized(&tex, 0) == 1);

    /* update: MIN filter = 6 has USE_MIPMAPS; chain walk fails at level 1
     * (missing), so the final parmMask write is skipped and it remains 2. */
    grUpdateTexture(&tex);
    CHECK(tex.parmMask == 2u);

    /* enable mipmaps + full chain */
    {
        static unsigned char pix2[8 * 4], pix3[4 * 4];
        tex.filterMask[GR_FILTERTYPE_MIN] = 6;
        tex.mipmap[1].pixels = pix2; tex.mipmap[1].log2Dim[0] = 1; tex.mipmap[1].log2Dim[1] = 1;
        tex.mipmap[1].texFormat = GR_TEXTUREFORMAT_RGBA;
        tex.mipmap[2].pixels = pix3; tex.mipmap[2].log2Dim[0] = 0; tex.mipmap[2].log2Dim[1] = 0;
        tex.mipmap[2].texFormat = GR_TEXTUREFORMAT_RGBA;
        grUpdateTexture(&tex);
        /* The 4x4, 2x2, 1x1 chain ends at level 2. */
        CHECK(tex.numMipLevels == 3);
        CHECK((tex.parmMask & 8u) != 0);           /* mipmap chain complete */
        CHECK((tex.parmMask & 4u) != 0);
    }
    /* setters */
    grSetTextureWrapMode(&tex, GR_TEXCOORD_S, 1);
    CHECK(grGetTextureWrapMode(&tex, GR_TEXCOORD_S) == 1);
    grSetTextureGenMipmaps(&tex, 1);
    CHECK(grGetTextureGenMipmaps(&tex) == 1);
    grSetTextureFilter(&tex, GR_FILTERTYPE_MAG, 1);
    CHECK(grGetTextureFilter(&tex, GR_FILTERTYPE_MAG) == 1);
    {
        _grRect r, r2;
        grSetRectangle(&r, 1, 2, 3, 4);
        grSetTextureCropRect(&tex, &r);
        grGetTextureCropRect(&tex, &r2);
        CHECK(r2.x0 == 1 && r2.x1 == 2 && r2.y0 == 3 && r2.y1 == 4);
    }
    CHECK(grGetTextureTarget(&tex) == GR_TEXTURETARGET_2D);
    CHECK((tex.parmMask & 1u) != 0);
}

/* --- state-free grUtil.h helpers ------------------------------------------ */
static void test_util_helpers(void)
{
    _grVector3i v;
    v.x = 10; v.y = 20; v.z = 30;
    CHECK(grVector3iGet(&v, 0) == 10);
    CHECK(grVector3iGet(&v, 2) == 30);
    CHECK(grUnpackIndex(grSetIndices(0x11, 0x22, 0x33), 0) == 0x11);
    CHECK(grUnpackIndex(grSetIndices(0x11, 0x22, 0x33), 2) == 0x33);
    CHECK(grSetIndices(255, 0, 128) == 0x8000FFu);
    /* masked colors equal: ref channel = highest set mask bit */
    {
        unsigned int c = grSetRGBA(0x44, 0x44, 0x44, 0x44);
        /* ref channel = highest set mask bit; only masked channels compare */
        CHECK(grMaskedColorsEqual(c, 0x1u) == 1);
        CHECK(grMaskedColorsEqual(grSetRGBA(0x44, 0x45, 0x44, 0x44), 0x1u) == 1);  /* G not in mask */
        CHECK(grMaskedColorsEqual(grSetRGBA(0x44, 0x45, 0x44, 0x44), 0x2u) == 1);  /* ref IS G */
        CHECK(grMaskedColorsEqual(grSetRGBA(0x44, 0x45, 0x44, 0x44), 0x5u) == 1);  /* ref = B; G ignored */
        CHECK(grMaskedColorsEqual(grSetRGBA(0x44, 0x45, 0x44, 0x44), 0x3u) == 0);  /* R != ref G */
        CHECK(grMaskedColorsEqual(grSetRGBA(0x44, 0x45, 0x44, 0x45), 0xFu) == 0);
    }
}

/* --- grState.h / grApiSmall.c ---------------------------------------------- */
#include "gerbera/grState.h"
#include "gerbera/hc/hcOpcode.h"

extern int g_orderedDitherKernel[4];

static void test_state_cull(void)
{
    Gerbera_s st;
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;

    grSetCullMode(&st, GR_WINDING_CW, 1u);
    CHECK(st.frontFace == GR_WINDING_CW);
    CHECK(st.cullMask == 1u);
    CHECK(st.cullNegative == 1);            /* cullMask bit0 set, CW */
    CHECK(st.lightingSideMask == 1);
    grSetCullMode(&st, GR_WINDING_CCW, 1u);
    CHECK(st.cullNegative == 0);            /* CCW flips the negative test */
    grSetCullMode(&st, GR_WINDING_CW, 0u);
    CHECK(st.cullNegative == 0);
    CHECK(grTestCapability(&st, GR_CAPABILITY_MAX - 1) == 0);
    grEnableCapability(&st, 10);
    CHECK(grTestCapability(&st, 10) == 1);
    grDisableCapability(&st, 10);
    CHECK(grTestCapability(&st, 10) == 0);
    grSetCapability(&st, GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE, 1);
    CHECK(grGetCapability(&st, GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE) == 1);
    CHECK(st.lightingSideMask == 3);        /* updated by grSetCapability */
    grSetCapability(&st, GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE, 0);
    CHECK(st.lightingSideMask == 1);
    /* lights: caps 41..48 flip lightActiveMask bits */
    grSetCapability(&st, 44, 1);
    CHECK((st.lightActiveMask & (1 << 3)) != 0);
    CHECK((st.lightActiveDirtyMask & (1 << 3)) != 0);
    grSetCapability(&st, 44, 0);
    CHECK((st.lightActiveMask & (1 << 3)) == 0);
    CHECK(st.normalsToEyeDirty == 0);
    grSetCapability(&st, GR_CAPABILITY_RESCALE_NORMAL, 1);
    CHECK(st.normalsToEyeDirty == 1);
    CHECK(st.normalsToEyePalDirty[8] == 1 && st.normalsToEyePalDirty[0] == 1);
    /* color material marks all light colors dirty */
    grSetCapability(&st, GR_CAPABILITY_COLOR_MATERIAL, 1);
    CHECK(st.lightColorDirtyMask == 0xFFu);
}

static void test_state_viewport(void)
{
    Gerbera_s st;
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;

    st.drawSurface.width = 320;
    st.drawSurface.height = 240;
    grSetViewport(&st, 10, 310, 20, 220);
    CHECK(st.viewportRectangle.x0 == 10 && st.viewportRectangle.x1 == 310);
    CHECK(st.viewportOffset.normal[0] == 16 * 10 + 8 * 300 + 8);
    CHECK(st.viewportOffset.normal[1] == 16 * 20 + 8 * 200 + 8);
    CHECK(st.viewportScale.normal[0] == (300 << 6));
    CHECK(st.viewportScale.normal[1] == (int)((unsigned)(20 - 220) * 64u));
    /* surface scissor = whole surface; render scissor = intersection */
    CHECK(st.surfaceScissor.x0 == 0 && st.surfaceScissor.x1 == 320);
    CHECK(st.renderScissor.x0 == 10 && st.renderScissor.x1 == 310);
    CHECK(st.renderScissor.y0 == 20 && st.renderScissor.y1 == 220);
    CHECK(st.scissorDirty == 0);
    CHECK(st.rejectAllStateDirty == 1);
    /* viewport outside the surface -> empty render scissor */
    st.rejectAllStateDirty = 0;
    grSetViewport(&st, 400, 500, 0, 100);
    CHECK(st.renderScissor.x0 == 0 && st.renderScissor.x1 == 0);
    CHECK(st.renderScissor.y0 == 0 && st.renderScissor.y1 == 0);
    /* Scissor clipping only applies when SCISSOR_TEST is enabled. */
    grSetScissor(&st, 5, 200, 30, 300);
    CHECK(st.scissorRectangle.x1 == 200);
    CHECK(st.surfaceScissor.x1 == 320 && (HGuint32)st.surfaceScissor.y1 == 240u); /* ignored */
    grSetCapability(&st, GR_CAPABILITY_SCISSOR_TEST, 1);
    grSetScissor(&st, 5, 200, 30, 300);
    CHECK(st.surfaceScissor.x0 == 5 && st.surfaceScissor.x1 == 200);
    CHECK(st.surfaceScissor.y0 == 30 && st.surfaceScissor.y1 == 240);
    /* render scissor = surface scissor ∩ viewport (400..500 x 0..100 = empty) */
    CHECK(st.renderScissor.x0 == 0 && (HGuint32)st.renderScissor.x1 == 0u);
    grSetViewport(&st, 0, 320, 0, 240);
    grSetScissor(&st, 5, 200, 30, 300);
    CHECK(st.renderScissor.x0 == 5 && st.renderScissor.x1 == 200);
    CHECK(st.renderScissor.y0 == 30 && st.renderScissor.y1 == 240);
    (void)0;

    /* depth range 0.25..0.75 in 1.31 -> fixed(HO,30) */
    grSetDepthRange(&st, FIN(0.25f), FIN(0.75f));
    CHECK((HGuint32)st.zNear == (HGuint32)FIN(0.25f) && (HGuint32)st.zFar == (HGuint32)FIN(0.75f));
    {
        int zn = IScalar_toFixedHO(IScalar_clampUnit(FIN(0.25f)), 30);
        int zf = IScalar_toFixedHO(IScalar_clampUnit(FIN(0.75f)), 30);
        CHECK(st.viewportScale.normal[2] == ((zf - zn) >> 1));
        CHECK(st.viewportOffset.normal[2] == ((zn + zf + 1) >> 1));
    }
    CHECK(st.pixelPipelineDirty == 1);
}

static void test_state_pixel(void)
{
    Gerbera_s st;
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;

    grSetClearColor(&st, 0x8040C0FFu);
    CHECK(st.clearColor == 0x8040C0FFu);
    grSetClearDepth(&st, 0x40000000);       /* 0.5 in 1.31 */
    CHECK(st.clearDepth == 0x7FFFFFFF);     /* (2*d*(2^32-1)) >> 32 */
    grSetClearStencil(&st, 0xAB);
    CHECK(st.clearStencil == 0xAB);

    grSetAlphaFunc(&st, GR_FUNC_GEQUAL, FIN(0.5f));
    CHECK(st.alphaFunc == GR_FUNC_GEQUAL);
    CHECK(st.alphaReference == 128);        /* scalarToFixed(ref, 8) */
    CHECK(st.pixelPipelineDirty == 1 && st.rejectAllStateDirty == 1);
    grSetStencilFunc(&st, GR_FUNC_NOTEQUAL, 7, 0xFF);
    CHECK(st.stencilFunc == GR_FUNC_NOTEQUAL && st.stencilTestMask == 0xFF && st.stencilReference == 7);
    grSetStencilOp(&st, GR_STENCILOP_KEEP, GR_STENCILOP_INCR, GR_STENCILOP_REPLACE);
    CHECK(st.stencilFail == GR_STENCILOP_KEEP);
    CHECK(st.stencilZFail == GR_STENCILOP_INCR && st.stencilZPass == GR_STENCILOP_REPLACE);
    grSetDepthFunc(&st, GR_FUNC_LEQUAL);
    CHECK(st.depthFunc == GR_FUNC_LEQUAL);
    grSetLogicOp(&st, GR_LOGICOP_XOR);
    CHECK(st.logicOp == GR_LOGICOP_XOR);
    grSetBlendFunc(&st, 2, 3);
    CHECK(st.srcBlendFactor == 2 && st.dstBlendFactor == 3);
    grSetColorMask(&st, 0x5);
    CHECK(st.colorMask == 0x5);
    grSetDepthMask(&st, 0);
    CHECK(st.depthMask == 0);
    grSetStencilMask(&st, 0xF0);
    CHECK(st.stencilWriteMask == 0xF0);
    grSetDepthOffset(&st, -2, 4);
    CHECK(st.depthOffsetFactor == -2 && st.depthOffsetUnits == 4);
    CHECK(grIsDepthOffsetEnabled(&st, 0) == grTestCapability(&st, 11));

    /* fog */
    grSetFogMode(&st, GR_FOGMODE_LINEAR);
    CHECK(st.fogMode == GR_FOGMODE_LINEAR);
    grSetFogDensity(&st, FIN(1.5f));
    CHECK((HGuint32)st.fogDensity == (HGuint32)FIN(1.5f));
    grSetFogRange(&st, FIN(10.0f), FIN(20.0f));
    {
        int range = IScalar_sub(FIN(20.0f), FIN(10.0f));
        int oo = IScalar_rcp(range);
        int v = IScalar_scalarToFixed(IScalar_mul(FIN(20.0f), oo), 16);
        CHECK(st.fogOORange == oo);
        CHECK(st.fogAtZero == (HGuint32)~(unsigned int)(unsigned short)hgClamp32_0_x(v, 0xFFFF));
    }
    grSetFogRange(&st, FIN(5.0f), FIN(5.0f));   /* equal -> OOR zero, fog@0 = 0xFFFF */
    CHECK(st.fogOORange == IScalar_zero());
    CHECK(st.fogAtZero == 0xFFFF);
    grSetFogColor(&st, 0x00FFFFFFu);
    CHECK(st.fogColor == 0x00FFFFFFu);
    grSetRectGradients(&st, 1);
    CHECK(st.screenRectGradients == 1);

    /* global ambient */
    {
        _XColor3 amb;
        XColor3_setx(&amb, 100, 200, 300);
        grSetGlobalAmbient(&st, &amb);
        CHECK(st.ambientColor.r == amb.r && st.ambientColor.g == amb.g && st.ambientColor.b == amb.b);
        CHECK(st.ambientLightSumDirty == 1);
        st.ambientLightSumDirty = 0;
        grSetGlobalAmbient(&st, &amb);      /* same: no dirty */
        CHECK(st.ambientLightSumDirty == 0);
    }

    /* draw surface descriptor roundtrip */
    {
        _grSurfaceDescriptor desc;
        _grDrawSurface *ds = &st.drawSurface;
        ds->rgbaOffsets[0] = 0; ds->rgbaOffsets[1] = 8;
        ds->rgbaOffsets[2] = 16; ds->rgbaOffsets[3] = 24;
        ds->rgbaSizes[0] = 8; ds->rgbaSizes[1] = 8;
        ds->rgbaSizes[2] = 8; ds->rgbaSizes[3] = 8;
        ds->colorMask = 0xF;
        ds->pixelFormat = GR_IMAGEFORMAT_RGBA;
        grGetDrawBufferDescriptor(&st, &desc);
        CHECK(desc.rgbaOffsets[0] == 0 && desc.rgbaOffsets[3] == 24);
        CHECK(desc.rgbaSizes[0] == 8 && desc.rgbaSizes[3] == 8);
        CHECK(desc.colorMask == 0xF && desc.pixelFormat == GR_IMAGEFORMAT_RGBA);
        (void)ds;
    }

    /* dither decision: any channel masked in with size 1..7 */
    grSetCapability(&st, GR_CAPABILITY_DITHER, 1);
    st.drawSurface.colorMask = 0xF;
    st.drawSurface.rgbaSizes[0] = 8; st.drawSurface.rgbaSizes[1] = 8;
    st.drawSurface.rgbaSizes[2] = 8; st.drawSurface.rgbaSizes[3] = 8;
    grUpdateDither(&st);
    CHECK(st.useDithering == 0);            /* all channels 8-bit: no dither */
    st.drawSurface.rgbaSizes[1] = 5;
    grUpdateDither(&st);
    CHECK(st.useDithering == 1);            /* G 5-bit: dither */
    st.drawSurface.colorMask = 0xC;         /* bits 2,3: G (bit 1) not written */
    grUpdateDither(&st);
    CHECK(st.useDithering == 0);
}

static void test_state_material(void)
{
    Gerbera_s st;
    _XColor3 c1, c2;
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;

    XColor3_setx(&c1, 100, 200, 300);
    grSetMaterialColor(&st, 0, &c1);
    CHECK(st.material.colors[0].r == c1.r && st.material.colors[0].b == c1.b);
    CHECK(st.baseColorDirty == 1);          /* AMBIENT slot */
    st.baseColorDirty = 0;
    grSetMaterialColor(&st, 0, &c1);        /* same: no dirty */
    CHECK(st.baseColorDirty == 0);
    XColor3_setx(&c2, 9, 9, 9);
    grSetMaterialColor(&st, 3, &c2);        /* EMISSION slot */
    CHECK(st.baseColorDirty == 1);
    st.baseColorDirty = 0;
    grSetMaterialColor(&st, 1, &c2);
    CHECK(st.lightColorDirtyMask == 0xFFu && st.baseColorDirty == 0);

    grSetMaterialAlpha(&st, 0xBEEFu);
    CHECK(st.material.alpha == 0xBEEFu);
    grSetMaterialShininess(&st, FIN(0.25f));
    CHECK((HGuint32)grGetMaterialShininess(&st) == (HGuint32)FIN(0.25f));
}

/* --- transform stack + user clip planes + ITC transform -------------------- */

static void test_transform(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _IFVector4 eq;
    _ITCTransform itc;
    int ci;

    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IMatrix4x4_identity(&id);

    /* grSetMatrix: copy + dirty bit; MODELVIEW=0 / PROJECTION=1 (EPOC slots) */
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_MODELVIEW) == 1);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_PROJECTION) == 0);
    for (ci = 0; ci < 4; ++ci)
        CHECK(st.matrix[0].v[ci].e == id.v[ci].e &&
              st.matrix[0].v[ci].m[0] == id.v[ci].m[0] &&
              st.matrix[0].v[ci].m[3] == id.v[ci].m[3]);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_PROJECTION) == 1);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_TEXTURE0) == 0);

    /* grSetMatrixDirty: set/clear one bit at a time */
    grSetMatrixDirty(&st, GR_MATRIXMODE_TEXTURE1, 1);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_TEXTURE1) == 1);
    grSetMatrixDirty(&st, GR_MATRIXMODE_TEXTURE1, 0);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_TEXTURE1) == 0);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_MODELVIEW) == 1);  /* untouched */

    /* User clip planes: planes 10..15 map to slots 0..5. */
    IFVector4_set(&eq, FIN(0.0f), FIN(1.0f), FIN(0.0f), FIN(-0.5f));
    grSetUserClipPlane(&st, GR_CLIP_USER0 + 2, &eq, &id);
    CHECK(st.origUserClipPlanes[2].m[0] == eq.m[0] &&
          st.origUserClipPlanes[2].m[3] == eq.m[3]);
    CHECK(st.userClipMatrix[2].v[0].e == id.v[0].e);
    CHECK(grGetUserClipPlane(&st, GR_CLIP_USER0 + 2) == &st.origUserClipPlanes[2]);
    grSetUserClipPlane(&st, GR_CLIP_USER0 + 5, &eq, &id);
    CHECK(grGetUserClipPlane(&st, GR_CLIP_USER0 + 5) == &st.origUserClipPlanes[5]);

    /* ITCTransform_copy: identity -> lsh=8 (127-119), rsh=0, im row 3 =
     * 10.21 ones, classification 2-bits on the diagonal */
    ITCTransform_copy(&itc, &id);
    for (ci = 0; ci < 4; ++ci)
    {
        CHECK(itc.e[ci] == (0x7F << (8 * ci)));
        CHECK(itc.lsh[ci][ci] == 8 && itc.rsh[ci][ci] == 0);
        CHECK(itc.im[ci] == (ci == 3 ? 2097152 : 0));
    }
    CHECK(itc.cl == (int)0x80200802u);
}

/* --- grUpdateLight / grUpdateLights ----------------------------------------- */

static void test_update_lights(void)
{
    Gerbera_s st;
    _IFVector4 pos;
    _XColor3 ac, lc, ma, me;
    int ret;

    /* grUpdateLight: directional -> mask 0x40 + headlight halfway =
     * normalize(nVP + (0,0,2^21)). */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IFVector4_set(&pos, FIN(2.0f), FIN(0.0f), FIN(0.0f), FIN(0.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    CHECK(grUpdateLight(&st, 0) == 0x40u);
    CHECK(st.lights[0].lightMask == 0x40);
    CHECK(st.lights[0].halfway[0] == 1482910);
    CHECK(st.lights[0].halfway[1] == 0);
    CHECK(st.lights[0].halfway[2] == 1482910);

    /* positional spotlight: bit 16 (cutoff != 180) + bit 32 (attn != 1,1,1),
     * halfway untouched */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IFVector4_set(&pos, FIN(1.0f), FIN(2.0f), FIN(3.0f), FIN(1.0f));
    grSetLightPosition(&st, 2, &pos);
    grSetLightSpotCutoff(&st, 2, FIN(60.0f));
    CHECK(grUpdateLight(&st, 2) == 0x30u);
    CHECK(st.lights[2].halfway[0] == 0 && st.lights[2].halfway[2] == 0);

    /* grUpdateLights: one directional + one positional, full refresh */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    XColor3_setx(&ac, 65536, 65536, 65536);       /* 2^21 = 1.0 (10.21) */
    XColor3_setx(&lc, 32768, 16384, 8192);
    XColor3_setx(&ma, 65536, 65536, 65536);
    XColor3_setx(&me, 16384, 16384, 16384);
    grSetGlobalAmbient(&st, &ac);
    IFVector4_set(&pos, FIN(2.0f), FIN(0.0f), FIN(0.0f), FIN(0.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    grSetLightColor(&st, 0, 0, &lc);
    IFVector4_set(&pos, FIN(1.0f), FIN(2.0f), FIN(3.0f), FIN(1.0f));
    grSetLightPosition(&st, 1, &pos);
    grSetLightSpotCutoff(&st, 1, FIN(180.0f));
    grSetMaterialColor(&st, 0, &ma);
    grSetMaterialColor(&st, 3, &me);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0, 1);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0 + 1, 1);
    ret = grUpdateLights(&st);
    CHECK(ret == 1);                                  /* positional light on */
    CHECK(st.lightDirectionalMask == 1);
    CHECK(st.lightActiveDirtyMask == 0);
    CHECK(st.lightTypeDirtyMask == 0);
    CHECK((st.lightColorDirtyMask & 3u) == 0);        /* cleared for enabled */
    CHECK(st.lights[0].lightMask == 0x40);            /* directional, no premul */
    CHECK(st.lights[1].lightMask == 0x20);            /* positional, zero colors */
    CHECK(st.l_directional[0] == &st.lights[0]);
    CHECK(st.l_directional[1] == NULL);
    CHECK(st.l_positional[0] == &st.lights[1]);
    CHECK(st.l_positional[1] == NULL);
    /* ambientLightSum = ambient + light0 ambient */
    CHECK(st.ambientLightSum.r == 3145728);
    CHECK(st.ambientLightSum.g == 2621440);
    CHECK(st.ambientLightSum.b == 2359296);
    /* baseColor = sum * material.ambient + material.emission */
    CHECK(st.baseColor.r == 3670016);
    CHECK(st.baseColor.g == 3145728);
    CHECK(st.baseColor.b == 2883584);
    CHECK(st.ambientLightSumMask == 1);
    CHECK(st.ambientLightSumDirty == 0);
    CHECK(st.baseColorDirty == 0);

    /* second run: idempotent (all asserts inside grUpdateLights hold) */
    ret = grUpdateLights(&st);
    CHECK(ret == 1);
    CHECK(st.ambientLightSum.r == 3145728);
    CHECK(st.baseColor.r == 3670016);

    /* disable the positional light -> partition drops it, return 0 */
    grSetCapability(&st, GR_CAPABILITY_LIGHT0 + 1, 0);
    ret = grUpdateLights(&st);
    CHECK(ret == 0);
    CHECK(st.l_positional[0] == NULL);
    CHECK(st.l_directional[0] == &st.lights[0]);
}

/* --- ITCTransform use (mul family + DivByQ) --------------------------------- */

static void test_itc_transform(void)
{
    _IMatrix4x4 id;
    _ITCTransform itc;
    int out[2];
    int st2[4];
    int fl[4];

    IMatrix4x4_identity(&id);
    ITCTransform_copy(&itc, &id);

    /* mulxv2: 16.16 in -> 10.21 out (identity) */
    st2[0] = 65536; st2[1] = 32768;
    ITCTransform_mulxv2(out, &itc, st2);
    CHECK(out[0] == 2097152 && out[1] == 1048576);
    /* mulxv4 */
    st2[0] = 65536; st2[1] = 65536; st2[2] = 0; st2[3] = 65536;
    ITCTransform_mulxv4(out, &itc, st2);
    CHECK(out[0] == 2097152 && out[1] == 2097152);
    /* mulxv n=3 pads q=1 (16.16) */
    st2[0] = 65536; st2[1] = 32768; st2[2] = 12345;
    ITCTransform_mulxv(out, &itc, st2, 3);
    CHECK(out[0] == 2097152 && out[1] == 1048576);
    /* mulfv2 with IScalar ones */
    st2[0] = IScalar_one(); st2[1] = IScalar_one();
    ITCTransform_mulfv2(out, &itc, st2);
    CHECK(out[0] == 2097152 && out[1] == 2097152);
    /* mulfv with float components {1, 0.5, 0, 1} */
    fl[0] = FIN(1.0f); fl[1] = FIN(0.5f); fl[2] = FIN(0.0f); fl[3] = FIN(1.0f);
    ITCTransform_mulfv(out, &itc, fl, 4);
    CHECK(out[0] == 2097152 && out[1] == 1048576);

    /* grTexCoordDivByQ: q = 1.0 (2^21) / 0 pass through */
    grTexCoordDivByQ(out, (int[]){100, 200, 0, 0x200000});
    CHECK(out[0] == 100 && out[1] == 200);
    grTexCoordDivByQ(out, (int[]){100, 200, 0, 0});
    CHECK(out[0] == 100 && out[1] == 200);
    /* q = 0.5: division doubles x, with one LSB lost to the reciprocal estimate. */
    grTexCoordDivByQ(out, (int[]){100, 200, 0, 1048576});
    CHECK(out[0] == 199 && out[1] == 399);
}

/* --- fetch / transform / fog stages (needs a fake render stack) -------------- */

static void test_fetch_stages(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    unsigned char *pool = (unsigned char *)malloc(1332 + 2048);
    unsigned char colors[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    unsigned char tc[4] = {10, 20, 30, 40};
    unsigned char list[3] = {0, 1, 0xFF};
    _grVertex *vb;

    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IMatrix4x4_identity(&id);
    st.renderStack = pool;
    vb = (_grVertex *)(pool + 1332);
    vb[0].index = 0;
    vb[1].index = 1;

    /* color fast path: u8 -> u16 rows (lit + secondary), alpha only row 0 */
    st.vertexArray[2].data = colors;
    st.vertexArray[2].stride = 4;
    st.vertexArray[2].type = GR_DATATYPE_UINT8;
    grFetchVertexColorFastUINT8(&st, list);
    CHECK(vb[0].color.final[0][0] == 0x1000);
    CHECK(vb[0].color.final[0][1] == 0x2000);
    CHECK(vb[0].color.final[0][2] == 0x3000);
    CHECK(vb[0].color.final[0][3] == 0x4000);
    CHECK(vb[0].color.final[1][0] == 0x1000);
    CHECK(vb[0].color.final[1][1] == 0x2000);
    CHECK(vb[0].color.final[1][2] == 0x3000);
    CHECK(vb[1].color.final[0][0] == 0x5000);

    /* color lighting path: XColor3 ints + doubled alpha */
    memset(pool + 1332, 0, 1024);
    vb[0].index = 0; vb[1].index = 1;
    grFetchVertexColorUINT8(&st, list);
    CHECK(vb[0].color.input.rgb[0].r == 131584);   /* (b<<13) | 32*b */
    CHECK(vb[0].color.input.rgb[0].g == 263168);
    CHECK(vb[0].color.input.rgb[0].b == 394752);
    CHECK(vb[0].color.final[1][2] == 0x4040);

    /* texcoords INT8x2 through the identity transform: 10.21 out */
    st.vertexArray[6].data = tc;
    st.vertexArray[6].stride = 2;
    st.vertexArray[6].type = GR_DATATYPE_INT8;
    st.vertexArray[6].size = 2;
    ITCTransform_copy(&st.texCoordTransform[0], &id);
    grTransformTexCoordINT8x2(&st, list, 0);
    CHECK(vb[0].texCoord[0].x == 10 * 2097152);
    CHECK(vb[0].texCoord[0].y == 20 * 2097152);
    CHECK(vb[1].texCoord[0].x == 30 * 2097152);
    CHECK(vb[1].texCoord[0].y == 40 * 2097152);

    /* point size FIXED: clamp [1.0, 64.0] in 16.16 */
    {
        unsigned int sizes[2] = {0x20000, 0x800000};
        st.vertexArray[5].data = sizes;
        st.vertexArray[5].stride = 4;
        grFetchPointSizeFIXED(&st, list);
        CHECK(vb[0].pointSize == 0x20000);
        CHECK(vb[1].pointSize == 0x400000);
    }

    /* fog FAST: full fog row */
    memset(pool + 1332, 0, 1024);
    vb[0].index = 0; vb[1].index = 1;
    grApplyFogFAST(&st, list);
    CHECK(vb[0].color.final[1][3] == 0xFFFF);
    CHECK(vb[1].color.final[1][3] == 0xFFFF);

    free(pool);
}

/* --- grUpdateVertexPipeline (stage selection) -------------------------------- */

static void test_update_pipeline(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;

    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IMatrix4x4_identity(&id);
    /* grInitState defaults: attenuation (1,0,0) */
    st.pointDistAttn.e = 127;
    st.pointDistAttn.m[0] = 0x20000000;

    /* lighting off: fast-default color, no normals */
    grUpdateVertexPipeline(&st);
    CHECK(st.fetchVertexColor == grFetchVertexColorFastDEFAULT);
    CHECK(st.fetchVertexNormal == NULL);
    CHECK(st.fetchPointSize == NULL);
    CHECK(st.applyFog == NULL);                    /* FOG cap off */
    CHECK(st.transformVE == 0);
    CHECK(st.vertexPipelineDirty == 0);
    /* The point-distance-attenuation cache is gated on a raw
     * primitiveType == 0 - a value grGetPrimitiveVertices (called further
     * down grUpdateVertexPipeline) rejects, so the branch is dead-end code
     * behind grSetupCulling's type validation: changing pointDistAttn does
     * not touch the pipeline for any reachable primitive type */
    st.pointDistAttn.m[0] = 0x10000000;
    grUpdateVertexPipeline(&st);
    CHECK(st.computePointDistAttn == 0);
    CHECK(st.transformVE == 0);
    CHECK(st.vertexPipelineDirty == 0);
    grUpdateVertexPipeline(&st);
    CHECK(st.vertexPipelineDirty == 0);

    /* fog on, LINEAR mode (0) + fogOORange != 0 -> LINEAR; == 0 -> FAST */
    grSetCapability(&st, GR_CAPABILITY_FOG, 1);
    st.colorMask = 0xF;
    st.fogOORange = 0x12345;
    grUpdateVertexPipeline(&st);
    CHECK(st.applyFog == grApplyFogLINEAR);
    st.fogOORange = 0;
    grUpdateVertexPipeline(&st);
    CHECK(st.applyFog == grApplyFogFAST);
    /* fog mode EXP */
    grSetFogMode(&st, GR_FOGMODE_EXP);
    grUpdateVertexPipeline(&st);
    CHECK(st.applyFog == grApplyFogEXP);
    /* fog mode EXP2 */
    grSetFogMode(&st, GR_FOGMODE_EXP2);
    grUpdateVertexPipeline(&st);
    CHECK(st.applyFog == grApplyFogEXP2);

    /* point size array: POINT primitive + cap + data -> FLOAT/FIXED */
    {
        unsigned int sizes[2] = {0x20000, 0x20000};
        st.primitiveType = GR_PRIMITIVETYPE_POINT;
        st.vertexArray[5].data = sizes;
        st.vertexArray[5].stride = 4;
        st.vertexArray[5].type = GR_DATATYPE_FLOAT;
        grSetCapability(&st, GR_CAPABILITY_POINT_SIZE_ARRAY, 1);
        grUpdateVertexPipeline(&st);
        CHECK(st.fetchPointSize == grFetchPointSizeFLOAT);
        st.vertexArray[5].type = GR_DATATYPE_FIXED;
        grUpdateVertexPipeline(&st);
        CHECK(st.fetchPointSize == grFetchPointSizeFIXED);
    }

    /* texture matrix dirty -> texCoordTransform refreshed; unit enabled with
     * no array -> default texcoords transformed (1,0) -> (2^21, 0) */
    grSetMatrix(&st, GR_MATRIXMODE_TEXTURE0, &id);
    st.textureUnitEnabled[0] = 1;
    st.defaultTexCoord.f[0][0] = 1.0f;
    st.defaultTexCoord.f[0][3] = 1.0f;
    grUpdateVertexPipeline(&st);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_TEXTURE0) == 0);
    CHECK(st.texCoordTransform[0].cl == (int)0x80200802u);
    CHECK(st.fetchTexCoord[0] == grTransformTexCoordDEFAULT);
    CHECK(st.defaultTexCoord.x[2][0] == 2097152);
    CHECK(st.defaultTexCoord.x[2][1] == 0);
    /* disabled unit -> NULL */
    CHECK(st.fetchTexCoord[1] == NULL);
}

/* --- grTransformVertex / grFetchWeightAndIndex / clip codes ------------------ */

static void test_transform_vertex(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _IFVector4 eq;
    unsigned char *pool = (unsigned char *)malloc(1332 + 2048);
    _grVertex *vb;
    float verts[8] = {1.0f, 2.0f, 3.0f, 1.0f, -1.0f, 0.5f, 0.25f, 1.0f};
    unsigned char k;

    memset(pool, 0, 1332 + 2048);
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IMatrix4x4_identity(&id);
    IVertexTransform_copy(&st.objectToClipTransform, &id);
    st.renderStack = pool;
    vb = (_grVertex *)(pool + 1332);
    st.vertexArray[0].data = verts;
    st.vertexArray[0].stride = 16;
    st.vertexArray[0].size = 4;
    st.vertexArray[0].type = GR_DATATYPE_FLOAT;

    /* float (1,2,3,1) through the identity: clip = same, e = max exp byte,
     * m shifted down per component; clip codes: z>w -> bit 1, y>w -> bit 9 */
    vb[0].index = 0;
    grTransformVertex(&st, &vb[0]);
    CHECK(grGetVertexPositionState(&vb[0]) == GR_POSITION_OBJECT_SPACE_FLOAT);
    CHECK((vb[0].stateMask & GR_VERTEXMASK_TRANSFORMED) != 0);
    CHECK(vb[0].clipPosition.e == 0x80);
    CHECK(vb[0].clipPosition.m[0] == 268435456);       /* 1.0 */
    CHECK(vb[0].clipPosition.m[1] == 536870912);       /* 2.0 */
    CHECK(vb[0].clipPosition.m[2] == 805306368);       /* 3.0 */
    CHECK(vb[0].clipPosition.m[3] == 268435456);       /* 1.0 */
    CHECK(vb[0].clipMask == 0x202u);
    /* (-1, 0.5, 0.25, 1): all inside */
    vb[1].index = 1;
    grTransformVertex(&st, &vb[1]);
    CHECK(vb[1].clipMask == 0);
    CHECK(vb[1].clipPosition.m[0] == -536870912);

    /* fixed 16.16 (1, -2, 0.5, 1): int view {x,y,z,w} through the identity */
    memset(pool + 1332, 0, 512);
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IVertexTransform_copy(&st.objectToClipTransform, &id);
    st.renderStack = pool;
    vb = (_grVertex *)(pool + 1332);
    {
        int fverts[4] = {65536, -131072, 32768, 65536};
        st.vertexArray[0].data = fverts;
        st.vertexArray[0].stride = 16;
        st.vertexArray[0].size = 4;
        st.vertexArray[0].type = GR_DATATYPE_FIXED;
        vb[0].index = 0;
        grTransformVertex(&st, &vb[0]);
        CHECK(grGetVertexPositionState(&vb[0]) == GR_POSITION_OBJECT_SPACE_FIXED);
        CHECK(vb[0].clipPosition.m[0] == 268435456);
        CHECK(vb[0].clipPosition.m[1] == -536870912);
        CHECK(vb[0].clipPosition.m[2] == 134217728);
        CHECK(vb[0].clipPosition.m[3] == 268435456);
    }

    /* user clip plane x <= 2 on slot 5, vertex (1,0,0,1): inside; plane
     * x <= 0.5: outside -> clip bit = digit+9 = 15 */
    memset(pool + 1332, 0, 512);
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IVertexTransform_copy(&st.objectToClipTransform, &id);
    st.renderStack = pool;
    vb = (_grVertex *)(pool + 1332);
    IFVector4_set(&eq, FIN(1.0f), FIN(0.0f), FIN(0.0f), FIN(-2.0f));
    grSetUserClipPlane(&st, GR_CLIP_USER0 + 5, &eq, &id);
    grSetCapability(&st, 30 + 5, 1);
    st.enabledUserClipPlaneIndices = 6;                /* plane 5 -> digit 6 */
    {
        float v2[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        st.vertexArray[0].data = v2;
        st.vertexArray[0].stride = 16;
        st.vertexArray[0].size = 4;
        st.vertexArray[0].type = GR_DATATYPE_FLOAT;
        vb[0].index = 0;
        /* the clip-plane transform is grSetupCulling's job; do it by hand:
         * identity modelview keeps the equation */
        st.userClipPlanes[5] = st.origUserClipPlanes[5];
        grTransformVertex(&st, &vb[0]);
        CHECK(st.userClipPlanes[5].m[0] == 536870912); /* 1.0 in the 2^29 mantissa scale. */
        CHECK(st.userClipPlanes[5].m[3] == -536870912);/* of -2.0 */
        CHECK(vb[0].clipMask == 0x8000u);              /* bit 6+9: 1-2 < 0 */
        /* grIsOutsideUserClipPlane directly */
        CHECK(grIsOutsideUserClipPlane(&st.userClipPlanes[5], &vb[0].clipPosition) == 1);
    }

    /* grFetchWeightAndIndex: FIXED weights, UINT8 indices (9 clamps to 8) */
    {
        int wfix[2] = {65536, 65536};
        unsigned char idx[2] = {3, 9};
        int weights[4];
        unsigned int packed;
        st.vertexArray[3].data = wfix;
        st.vertexArray[3].stride = 8;
        st.vertexArray[3].size = 2;
        st.vertexArray[3].type = GR_DATATYPE_FIXED;
        st.vertexArray[4].data = idx;
        st.vertexArray[4].stride = 2;
        st.vertexArray[4].size = 2;
        st.vertexArray[4].type = GR_DATATYPE_UINT8;
        packed = grFetchWeightAndIndex(&st, weights, 0);
        CHECK(weights[0] == 1065353216);               /* IScalar 1.0 */
        CHECK(weights[1] == 1065353216);
        CHECK(packed == 0x0803u);                      /* bytes: 3, 8 */
    }

    /* matrix palette path: identity matrices, weight (1,0), indices (0,1):
     * position (1,2,3,1) unchanged, state -> EYE_SPACE, clip via projection */
    memset(pool + 1332, 0, 512);
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IVertexTransform_copy(&st.objectToClipTransform, &id);
    IVertexTransform_copy(&st.projectionTransform, &id);
    for (k = 0; k < 9; ++k)
        IVertexTransform_copy(&st.matrixPalTransform[k], &id);
    st.renderStack = pool;
    vb = (_grVertex *)(pool + 1332);
    {
        float v3[4] = {1.0f, 2.0f, 3.0f, 1.0f};
        int wfix[2] = {65536, 0};
        unsigned char idx[2] = {0, 1};
        st.vertexArray[0].data = v3;
        st.vertexArray[0].stride = 16;
        st.vertexArray[0].size = 4;
        st.vertexArray[0].type = GR_DATATYPE_FLOAT;
        st.vertexArray[3].data = wfix;
        st.vertexArray[3].stride = 8;
        st.vertexArray[3].size = 2;
        st.vertexArray[3].type = GR_DATATYPE_FIXED;
        st.vertexArray[4].data = idx;
        st.vertexArray[4].stride = 2;
        st.vertexArray[4].size = 2;
        st.vertexArray[4].type = GR_DATATYPE_UINT8;
        grSetCapability(&st, GR_CAPABILITY_MATRIX_PALETTE, 1);
        grSetCapability(&st, GR_CAPABILITY_MATRIX_INDEX_ARRAY, 1);
        grSetCapability(&st, GR_CAPABILITY_WEIGHT_ARRAY, 1);
        vb[0].index = 0;
        grTransformVertex(&st, &vb[0]);
        CHECK(grGetVertexPositionState(&vb[0]) == GR_POSITION_EYE_SPACE);
        /* accfv output: per-component exponent bytes, m normalized */
        CHECK(vb[0].position.objectSpaceFloat[0].e == 0x7f80807fu);
        CHECK(vb[0].position.objectSpaceFloat[0].m[0] == 536870912);
        CHECK(vb[0].position.objectSpaceFloat[0].m[1] == 536870912);
        CHECK(vb[0].position.objectSpaceFloat[0].m[2] == 805306368);
        CHECK(vb[0].position.objectSpaceFloat[0].m[3] == 536870912);
        CHECK(vb[0].clipPosition.e == 0x80);
        CHECK(vb[0].clipPosition.m[0] == 268435456);
        CHECK(vb[0].clipPosition.m[2] == 805306368);
        CHECK(vb[0].clipMask == 0x202u);
    }

    /* vertex record size: the render stack addresses vertices with << 7 */
    CHECK(sizeof(_grVertex) == 128);
    free(pool);
}

static void test_state_lights(void)
{
    Gerbera_s st;
    _IFVector4 pos;
    _IFVector3 orgDir;
    _XColor3 c;
    int dir[3];
    int i;

    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IFVector3_setf(&orgDir, 0, FIN(0.0f));
    IFVector3_setf(&orgDir, 1, FIN(-1.0f));
    IFVector3_setf(&orgDir, 2, FIN(0.0f));

    /* positional light at (1,2,3,1) */
    IFVector4_set(&pos, FIN(1.0f), FIN(2.0f), FIN(3.0f), FIN(1.0f));
    grSetLightPosition(&st, 0, &pos);
    CHECK(st.lights[0].positional == 1);
    CHECK(fabs(IScalar_stof(IFVector4_getf(&st.lights[0].orgPosition, 1)) - 2.0f) < 1e-6f);
    CHECK((st.lightTypeDirtyMask & 1u) != 0);

    /* directional light at (2,0,0,0): w==0 -> positional==0, nVP normalized */
    IFVector4_set(&pos, FIN(2.0f), FIN(0.0f), FIN(0.0f), FIN(0.0f));
    grSetLightPosition(&st, 1, &pos);
    CHECK(st.lights[1].positional == 0);
    CHECK(abs(st.lights[1].nVP[0] - 2097152) <= 4);   /* (1,0,0) in 10.21 */
    CHECK(st.lights[1].nVP[1] == 0);

    /* spot direction: stored negated */
    dir[0] = 0; dir[1] = 2097152; dir[2] = 0;
    grSetLightSpotDirection(&st, 2, dir, &orgDir);
    CHECK(st.lights[2].spotDirection[1] == -2097152);
    CHECK(fabs(IScalar_stof(IFVector3_getf(&st.lights[2].orgSpotDirection, 1)) + 1.0f) < 1e-6f);

    /* colors: ambient(0)/diffuse(1)/specular(2).
     * NOTE setx scales x32; XScalar_mul_sat(a,b) = (a*b)>>21, so small
     * colors multiply to 0 - use large colors for the premul check. */
    XColor3_setx(&c, 10, 20, 30);
    for (i = 0; i < 3; ++i)
        grSetLightColor(&st, 0, i, &c);
    CHECK((st.lightColorDirtyMask & 1u) != 0);
    for (i = 0; i < 3; ++i)
    {
        _XColor3 out;
        grGetLightColor(&st, 0, i, &out);
        CHECK(out.r == c.r && out.g == c.g && out.b == c.b);
    }
    /* nonzero colors -> grTestLightColor; premul needs big colors */
    CHECK(grTestLightColor(&st.lights[0], 0) != 0);
    CHECK(grTestLightColor(&st.lights[3], 0) == 0);   /* untouched light: zero */
    {
        _XColor3 big;
        XColor3_setx(&big, 65536, 65536, 65536);      /* 2^21 = 1.0 (10.21) */
        grSetLightColor(&st, 0, 2, &big);
        CHECK(grPrecalcLightColor(&st.lights[0], 2, &big) != 0);
        CHECK(st.lights[0].premul_colors[2].r == 2097152);
        grSetLightColor(&st, 0, 2, &c);
        CHECK(grPrecalcLightColor(&st.lights[0], 2, &c) == 0);    /* c*c >> 21 = 0 */
    }

    /* attenuation (constant/linear/quadratic) */
    grSetLightAttenuation(&st, 0, 0, FIN(1.0f));
    grSetLightAttenuation(&st, 0, 1, FIN(0.5f));
    grSetLightAttenuation(&st, 0, 2, FIN(0.25f));
    CHECK(fabs(IScalar_stof(grGetLightAttenuation(&st, 0, 1)) - 0.5f) < 1e-6f);
    CHECK(fabs(IScalar_stof(grGetLightAttenuation(&st, 0, 2)) - 0.25f) < 1e-6f);

    /* exponent (XScalar roundtrip) */
    grSetLightExponent(&st, 0, FIN(2.0f));
    CHECK((HGuint32)grGetLightExponent(&st, 0) == (HGuint32)FIN(2.0f));

    /* spot cutoff: stored raw (IScalar degrees) + cosine clamped to [0,1]
     * as XScalar */
    grSetLightSpotCutoff(&st, 0, FIN(60.0f));
    CHECK((HGuint32)grGetLightSpotCutoff(&st, 0) == (HGuint32)FIN(60.0f));
    {
        int cos60 = IScalar_cos(IScalar_degreesToRadians(FIN(60.0f)));
        CHECK(st.lights[0].cosSpotCutoff == IScalar_toXScalar(IScalar_max(cos60, IScalar_zero())));
        /* cos(60) = 0.5: XScalar roundtrip */
        CHECK(fabs((float)st.lights[0].cosSpotCutoff / 2097152.0f - 0.5f) < 1e-3f);
    }
    /* cutoff 180 -> cos = -1 clamped to 0 */
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    CHECK(st.lights[0].cosSpotCutoff == 0);
}

/* --- light / grLightVertex / grApplyLighting --------------------------------- */
#include "gerbera/grState.h"

static _grVertex *init_lighting_vertex(Gerbera_s *state, unsigned char *pool,
                                       _IMatrix4x4 *identity,
                                       unsigned char colors[4])
{
    unsigned char list[] = {0, 0xFF};
    memset(pool, 0, 4096);
    memset(state, 0, sizeof(*state));
    state->primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    IVertexTransform_copy(&state->modelviewTransform, identity);
    state->renderStack = pool;
    state->vertexArray[2].data = colors;
    state->vertexArray[2].stride = 4;
    state->vertexArray[2].type = GR_DATATYPE_UINT8;
    grFetchVertexColorUINT8(state, list);
    return (_grVertex *)(pool + 1332);
}

static void test_light_vertex(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _IFVector4 pos;
    _XColor3 c, black, one;
    unsigned char *pool = malloc(4096);
    _grVertex *vb;
    unsigned char colors[4] = {255, 255, 255, 255};
    unsigned char list[2] = {0, 0xFF};
    int z;

    IMatrix4x4_identity(&id);
    XColor3_setx(&black, 0, 0, 0);
    XColor3_setx(&one, 65536, 65536, 65536);
    XColor3_setx(&c, 65536, 65536, 65536);

    /* directional light (0,0,1,0), white diffuse, vertex at the origin with
     * normal (0,0,1): fully lit, single-sided */
    vb = init_lighting_vertex(&st, pool, &id, colors);
    IFVector4_set(&pos, FIN(0.0f), FIN(0.0f), FIN(1.0f), FIN(0.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    grSetLightColor(&st, 0, 0, &black);
    grSetLightColor(&st, 0, 1, &c);
    grSetLightColor(&st, 0, 2, &black);
    grSetMaterialColor(&st, 0, &one);
    grSetMaterialColor(&st, 1, &one);
    grSetMaterialColor(&st, 3, &black);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0, 1);
    z = grUpdateLights(&st);
    CHECK(z == 0);                                  /* no positional light  */
    CHECK((st.lightDirectionalMask & 1u) != 0);
    CHECK(st.lights[0].lightMask == 0x44);          /* directional+diffuse  */
    CHECK(st.l_directional[0] == &st.lights[0]);
    CHECK(st.baseColor.r == 0);                     /* emission+global amb  */
    CHECK(st.lights[0].premul_colors[1].r == 2097152);
    XNormal3_setx(vb[0].normal, 0, 0, 65536);
    IMatrix4x4_inversetranspose(&st.normalsToEyeTransform, &id, 0);
    grApplyLighting(&st, list);
    CHECK(st.lights[0].ld == 2097152);
    CHECK(st.lights[0].ls == 0);
    CHECK(st.lights[0].la == 0);                /* directional: untouched */
    CHECK(vb[0].color.final[0][0] == 0xFF00);
    CHECK(vb[0].color.final[0][1] == 0xFF00);
    CHECK(vb[0].color.final[0][2] == 0xFF00);
    CHECK(vb[0].color.final[0][3] == 0x0000);       /* b=r quirk clears alpha */
    CHECK(vb[0].color.final[1][0] == 0xFF00);       /* b=r int-view packing  */
    CHECK(vb[0].color.final[1][1] == 0xFF00);
    CHECK(vb[0].color.final[1][2] == 0xFF00);

    /* same light from behind, two-sided: front unlit, back fully lit */
    vb = init_lighting_vertex(&st, pool, &id, colors);
    IFVector4_set(&pos, FIN(0.0f), FIN(0.0f), FIN(-1.0f), FIN(0.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    grSetLightColor(&st, 0, 0, &black);
    grSetLightColor(&st, 0, 1, &c);
    grSetLightColor(&st, 0, 2, &black);
    grSetMaterialColor(&st, 0, &one);
    grSetMaterialColor(&st, 1, &one);
    grSetMaterialColor(&st, 3, &black);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0, 1);
    grUpdateLights(&st);
    st.lightingSideMask = 2;
    XNormal3_setx(vb[0].normal, 0, 0, 65536);
    IMatrix4x4_inversetranspose(&st.normalsToEyeTransform, &id, 0);
    grApplyLighting(&st, list);
    CHECK(st.lights[0].ld == -2097152);
    CHECK(vb[0].color.final[0][0] == 0 && vb[0].color.final[0][1] == 0 &&
          vb[0].color.final[0][2] == 0);
    CHECK(vb[0].color.final[1][0] == 0xFF00);
    CHECK(vb[0].color.final[1][1] == 0xFF00);
    CHECK(vb[0].color.final[1][2] == 0xFF00);

    /* positional light at (0,0,1,1) + transformVE: the vertex is brought to
     * eye space (state 3) and lit; attenuation (1,0,0) keeps la at 1.0 */
    vb = init_lighting_vertex(&st, pool, &id, colors);
    {
        static float vpos[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        st.vertexArray[0].data = vpos;
        st.vertexArray[0].stride = 16;
        st.vertexArray[0].size = 4;
        st.vertexArray[0].type = GR_DATATYPE_FLOAT;
    }
    IFVector4_set(&pos, FIN(0.0f), FIN(0.0f), FIN(1.0f), FIN(1.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    grSetLightAttenuation(&st, 0, 0, IScalar_ftos(1.0f));
    grSetLightAttenuation(&st, 0, 1, IScalar_ftos(0.0f));
    grSetLightAttenuation(&st, 0, 2, IScalar_ftos(0.0f));
    grSetLightColor(&st, 0, 0, &black);
    grSetLightColor(&st, 0, 1, &c);
    grSetLightColor(&st, 0, 2, &black);
    grSetMaterialColor(&st, 0, &one);
    grSetMaterialColor(&st, 1, &one);
    grSetMaterialColor(&st, 3, &black);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0, 1);
    z = grUpdateLights(&st);
    CHECK(z == 1);                                  /* positional light on  */
    CHECK(st.l_positional[0] == &st.lights[0]);
    CHECK(st.l_directional[0] == NULL);
    CHECK((st.lights[0].lightMask & 0x40) == 0);
    st.transformVE = z;
    XNormal3_setx(vb[0].normal, 0, 0, 65536);
    IMatrix4x4_inversetranspose(&st.normalsToEyeTransform, &id, 0);
    grApplyLighting(&st, list);
    CHECK(grGetVertexPositionState(&vb[0]) == GR_POSITION_EYE_SPACE_CARTESIAN);
    CHECK(st.lights[0].ld == 2097152);
    CHECK(st.lights[0].la == 2097152);
    CHECK(vb[0].color.final[0][0] == 0xFF00);
    CHECK(vb[0].color.final[0][1] == 0xFF00);
    CHECK(vb[0].color.final[0][2] == 0xFF00);

    /* color material: diffuse scales with the per-vertex color */
    colors[0] = 128; colors[1] = 128; colors[2] = 128;
    vb = init_lighting_vertex(&st, pool, &id, colors);
    IFVector4_set(&pos, FIN(0.0f), FIN(0.0f), FIN(1.0f), FIN(0.0f));
    grSetLightPosition(&st, 0, &pos);
    grSetLightSpotCutoff(&st, 0, FIN(180.0f));
    grSetLightColor(&st, 0, 0, &black);
    grSetLightColor(&st, 0, 1, &c);
    grSetLightColor(&st, 0, 2, &black);
    grSetCapability(&st, GR_CAPABILITY_LIGHT0, 1);
    grSetCapability(&st, GR_CAPABILITY_COLOR_MATERIAL, 1);
    grUpdateLights(&st);
    XNormal3_setx(vb[0].normal, 0, 0, 65536);
    IMatrix4x4_inversetranspose(&st.normalsToEyeTransform, &id, 0);
    grApplyLighting(&st, list);
    CHECK(vb[0].color.final[0][0] == 0x8000);
    CHECK(vb[0].color.final[0][1] == 0x8000);
    CHECK(vb[0].color.final[0][2] == 0x8000);
    CHECK(vb[0].color.final[0][3] == 0xFF00);       /* alpha = fetched &FF00 */
    colors[0] = 255; colors[1] = 255; colors[2] = 255;
    free(pool);
}

/* --- grSetupCulling ---------------------------------------------------------- */

static void test_setup_culling(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _IFVector4 plane;
    unsigned char data[16] = {0};
    int r;

    IMatrix4x4_identity(&id);

    /* reject: vertex array enabled but no data */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    CHECK(grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_STOP);
    CHECK(st.primitiveType == GR_PRIMITIVETYPE_TRIANGLE);
    /* Texture rectangles bypass the vertex-data check but still reject an
     * empty scissor. Mark the cached rejection result dirty before switching. */
    st.rejectAllStateDirty = 1;
    CHECK(grSetupCulling(&st, GR_PRIMITIVETYPE_TEXTURE_RECT) == GR_COMMAND_STOP);
    CHECK(st.rejectAllState == 1);
    CHECK(st.rejectAllStateDirty == 0);

    /* alpha test with GL_NEVER-ish func 0 rejects everything */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    st.vertexArray[0].data = data;
    grSetCapability(&st, GR_CAPABILITY_ALPHA_TEST, 1);
    st.alphaFunc = 0;
    st.rejectAllStateDirty = 1;
    CHECK(grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_STOP);
    CHECK(st.rejectAllState == 1);
    /* cached verdict asserted consistent on the next non-dirty call */
    CHECK(grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_STOP);

    /* cull mask 3 on triangles rejects all faces */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    st.vertexArray[0].data = data;
    grSetRectangle(&st.renderScissor, 0, 100, 0, 100);
    grSetCapability(&st, GR_CAPABILITY_CULL_FACE, 1);
    st.cullMask = 3;
    CHECK(grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE) == GR_COMMAND_STOP);
    st.cullMask = 1;
    r = grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    CHECK(r == GR_COMMAND_RENDER);

    /* happy path with identity matrices: transforms refreshed, no planes */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    st.vertexArray[0].data = data;
    grSetRectangle(&st.renderScissor, 0, 100, 0, 100);
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    r = grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    CHECK(r == GR_COMMAND_RENDER);
    CHECK(st.enabledUserClipPlaneIndices == 0);
    CHECK(st.modelviewTransform.v[0].e == id.v[0].e);
    /* IVertexTransform_copy renormalizes the 3.29 identity mantissas to the
     * shared-exponent record (0x40000000 mantissa, exponent 0x8f). */
    CHECK(st.objectToClipTransform.v[0].m[0] == 0x40000000);
    CHECK(st.objectToClipTransform.v[3].m[3] == 0x40000000);
    CHECK(st.objectToClipTransform.exp == 0x8f);
    CHECK(st.projectionTransform.v[0].e == id.v[0].e);
    CHECK(st.matrixDirtyMask == 0);
    CHECK(st.rejectAllState == 0);

    /* clip planes: enabling planes 0 and 2 packs (plane+1) base-16 while
     * walking 5..0 -> plane2 lands first (3), plane0 in the low digit (0x31);
     * identity projection * userClipMatrix leaves the equations unchanged */
    grSetCapability(&st, GR_CAPABILITY_CLIP_PLANE0, 1);
    grSetCapability(&st, GR_CAPABILITY_CLIP_PLANE2, 1);
    IFVector4_set(&plane, FIN(0.0f), FIN(0.0f), FIN(1.0f), FIN(-1.0f));
    grSetUserClipPlane(&st, GR_CLIP_USER0 + 0, &plane, &id);
    grSetUserClipPlane(&st, GR_CLIP_USER0 + 2, &plane, &id);
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    r = grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    CHECK(r == GR_COMMAND_RENDER);
    CHECK(st.enabledUserClipPlaneIndices == 0x31);
    /* IMatrix4x4_mulfv4 emits the matrix's 2.29 mantissa domain: the
     * identity transform leaves (0, 0, 1, -1) as 0x20000000 / 0xE0000000 */
    CHECK(st.userClipPlanes[0].m[0] == 0);
    CHECK(st.userClipPlanes[0].m[2] == 0x20000000);
    CHECK(st.userClipPlanes[0].m[3] == (int)0xE0000000);
    CHECK(st.userClipPlanes[2].m[3] == (int)0xE0000000);

    /* palette matrices refresh from matrix[4..12] when dirty */
    grSetCapability(&st, GR_CAPABILITY_MATRIX_PALETTE, 1);
    grSetMatrix(&st, GR_MATRIXMODE_MATRIXPALETTE0 + 3, &id);
    r = grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    CHECK(r == GR_COMMAND_RENDER);
    CHECK(st.matrixPalTransform[3].v[0].e == id.v[0].e);
    CHECK(st.normalsToEyePalDirty[3] == 1);
    CHECK(grIsMatrixDirty(&st, GR_MATRIXMODE_MATRIXPALETTE0 + 3) == 0);
}

/* --- clear queue + grr clears ---------------------------------------------- */

static void test_clear(void)
{
    Gerbera_s st;
    static unsigned short color16[24];
    static unsigned short depth16[16];
    static unsigned char stencil8[16];
    unsigned char bytes8[8];
    unsigned short words16[4];
    unsigned int dwords32[4];
    int i;

    /* grClearBuffers queues a record (grApiSmall.inl 388) */
    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetRectangle(&st.surfaceScissor, 0, 100, 0, 100);
    grSetClearColor(&st, 0x12345678);
    grSetClearDepth(&st, 0x10000000);
    grSetClearStencil(&st, 0xAB);
    grClearBuffers(&st, 3);
    CHECK(st.clearInfoIndex == 1);
    CHECK(st.clearInfos[0].bufferMask == 3);
    CHECK(st.clearInfos[0].clearColor == 0x12345678);
    CHECK(st.clearInfos[0].clearDepth == 0x1FFFFFFF);
    CHECK(st.clearInfos[0].clearStencil == 0xAB);
    CHECK(st.clearInfos[0].scissor.x1 == 100);
    CHECK(st.clearInfos[0].scissor.y0 == 0);
    CHECK(st.clearInfos[0].colorMask == 0);
    CHECK(st.clearInfos[0].depthMask == 0);

    /* 4 slots; the 5th grClearBuffers flushes the queue first */
    grClearBuffers(&st, 3);
    grClearBuffers(&st, 3);
    grClearBuffers(&st, 3);
    CHECK(st.clearInfoIndex == 4);
    grClearBuffers(&st, 3);
    CHECK(st.clearInfoIndex == 1);
    grFlushBufferClears(&st);
    CHECK(st.clearInfoIndex == 0);

    /* real surface: 4x4 RGB565 + 16-bit depth + 8-bit stencil */
    memset(&st, 0, sizeof(st));
    memset(color16, 0x12, sizeof(color16));
    memset(depth16, 0, sizeof(depth16));
    memset(stencil8, 0x77, sizeof(stencil8));
    st.drawSurface.rgbaOffsets[0] = 11; st.drawSurface.rgbaSizes[0] = 5;
    st.drawSurface.rgbaOffsets[1] = 5;  st.drawSurface.rgbaSizes[1] = 6;
    st.drawSurface.rgbaOffsets[2] = 0;  st.drawSurface.rgbaSizes[2] = 5;
    st.drawSurface.rgbaOffsets[3] = 0;  st.drawSurface.rgbaSizes[3] = 0;
    st.drawSurface.colorMask = 0xF;
    st.drawSurface.colorBytes = 2;
    st.drawSurface.depthBytes = 2;
    st.drawSurface.stencilBytes = 1;
    st.drawSurface.width = 4;
    st.drawSurface.height = 4;
    st.drawSurface.buffers[0].data = color16;
    st.drawSurface.buffers[0].stride = 8;
    st.drawSurface.buffers[1].data = depth16;
    st.drawSurface.buffers[1].stride = 8;
    st.drawSurface.buffers[2].data = stencil8;
    st.drawSurface.buffers[2].stride = 4;
    grSetRectangle(&st.surfaceScissor, 0, 4, 0, 4);
    st.colorMask = 0xF;
    st.depthMask = 1;
    st.stencilWriteMask = 0xFF;
    grSetClearColor(&st, 0xFFFFFFFF);
    grSetClearDepth(&st, 0x10000000);
    grSetClearStencil(&st, 0xAB);

    grClearBuffers(&st, 7);
    grFlushBufferClears(&st);
    CHECK(st.clearInfoIndex == 0);
    for (i = 0; i < 16; ++i)
    {
        CHECK(color16[i] == 0xFFFF);
        CHECK(depth16[i] == 0x7FFF);      /* 0x1FFFFFFF >> (30 - 16) */
        CHECK(stencil8[i] == 0xAB);
    }

    /* masked clear: colorMask 0x3 keeps the blue channel bits (565 write
     * mask = R|G = 0xFFE0; original blue 0x14 survives) */
    for (i = 0; i < 16; ++i)
        color16[i] = 0x1234;
    st.colorMask = 0x3;
    grClearBuffers(&st, 1);
    grFlushBufferClears(&st);
    for (i = 0; i < 16; ++i)
        CHECK(color16[i] == 0xFFF4);

    /* scissor sub-rect: 6-wide surface, rect cols 2..4 / rows 1..2 */
    memset(&st.drawSurface.rgbaOffsets, 0, sizeof(st.drawSurface.rgbaOffsets));
    memset(&st.drawSurface.rgbaSizes, 0, sizeof(st.drawSurface.rgbaSizes));
    st.drawSurface.rgbaOffsets[0] = 11; st.drawSurface.rgbaSizes[0] = 5;
    st.drawSurface.rgbaOffsets[1] = 5;  st.drawSurface.rgbaSizes[1] = 6;
    st.drawSurface.rgbaOffsets[2] = 0;  st.drawSurface.rgbaSizes[2] = 5;
    st.drawSurface.width = 6;
    st.drawSurface.buffers[0].stride = 12;
    st.colorMask = 0xF;
    grSetClearColor(&st, 0xFFFFFFFF);
    for (i = 0; i < 24; ++i)
        color16[i] = 0x5555;
    grSetRectangle(&st.surfaceScissor, 2, 5, 1, 3);
    grClearBuffers(&st, 1);
    grFlushBufferClears(&st);
    for (i = 0; i < 24; ++i)
        CHECK(color16[i] == ((i >= 8 && i < 11) || (i >= 14 && i < 17) ? 0xFFFF : 0x5555));

    /* Dithered clears use grDitherColor; verify both rows of the RGB565 pattern. */
    st.drawSurface.buffers[0].stride = 12;
    st.useDithering = 1;
    grSetClearColor(&st, grSetRGBA(129, 129, 129, 255));
    for (i = 0; i < 24; ++i)
        color16[i] = 0x0000;
    grSetRectangle(&st.surfaceScissor, 0, 6, 0, 4);
    grClearBuffers(&st, 1);
    grFlushBufferClears(&st);
    /* kernel[0] = 0xFF88EE66: every byte leaves 129/129/129 at 0x8410 */
    CHECK(color16[0] == 0x8410);
    CHECK(color16[1] == 0x8410);
    CHECK(color16[2] == 0x8410);
    CHECK(color16[3] == 0x8410);
    CHECK(color16[4] == 0x8410);
    CHECK(color16[5] == 0x8410);
    /* kernel[1] = 0x440022AA: bytes 44 AA 22 00 -> 7c0f 8410 7bef 7bef */
    CHECK(color16[6] == 0x7C0F);
    CHECK(color16[7] == 0x8410);
    CHECK(color16[8] == 0x7BEF);
    CHECK(color16[9] == 0x7BEF);
    CHECK(color16[10] == 0x7C0F);
    CHECK(color16[11] == 0x8410);

    /* grFill4x8: cyclic pattern + masked keep */
    memset(bytes8, 0xAB, sizeof(bytes8));
    grFill4x8(bytes8, 0x10, 0x20, 0x30, 0x40, 0xFF, 8);
    CHECK(bytes8[0] == 0x10 && bytes8[1] == 0x20 && bytes8[2] == 0x30 && bytes8[3] == 0x40);
    CHECK(bytes8[4] == 0x10 && bytes8[5] == 0x20 && bytes8[6] == 0x30 && bytes8[7] == 0x40);
    memset(bytes8, 0xAB, sizeof(bytes8));
    grFill4x8(bytes8, 0x10, 0x20, 0x30, 0x40, 0xF0, 8);
    CHECK(bytes8[0] == 0x1B && bytes8[1] == 0x2B && bytes8[2] == 0x3B && bytes8[3] == 0x4B);
    CHECK(bytes8[4] == 0x1B && bytes8[5] == 0x2B && bytes8[6] == 0x3B && bytes8[7] == 0x4B);

    /* grFill16/32Masked: full mask routes to hgFill, partial keeps bits */
    memset(words16, 0x87, sizeof(words16));
    grFill16Masked(words16, 0xBEEF, 0xFFFF, 4);
    CHECK(words16[0] == 0xBEEF && words16[3] == 0xBEEF);
    memset(words16, 0x87, sizeof(words16));
    grFill16Masked(words16, 0x1234, 0xFFF0, 4);
    CHECK(words16[0] == (0x1230 | 0x0007) && words16[3] == (0x1230 | 0x0007));
    memset(dwords32, 0x87, sizeof(dwords32));
    grFill32Masked(dwords32, 0x12345678, 0xFFFFFFFF, 4);
    CHECK(dwords32[0] == 0x12345678 && dwords32[3] == 0x12345678);
    memset(dwords32, 0x87, sizeof(dwords32));
    grFill32Masked(dwords32, 0x12345678, 0xFFFFFF00, 4);
    CHECK(dwords32[0] == (0x12345600 | 0x87) && dwords32[3] == (0x12345600 | 0x87));
}

/* --- grClipper.inl ---------------------------------------------------------- */

static void test_clipper(void)
{
    Gerbera_s st;
    TEST_RENDER_STACK(stack);
    _grVertex *vb;
    unsigned char fan[16];
    unsigned int indices;
    int n, d;

    memset(&st, 0, sizeof(st));
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    memset(stack, 0, sizeof(stackStorage.bytes));
    st.renderStack = stack;

    /* grClipFactorInit: vertex ndx gets ONE in its slot */
    {
        _grClipVertex cv;

        grClipFactorInit(&cv, 0);
        CHECK(cv.clipFactor[0] == 0x20000000 && cv.clipFactor[1] == 0);
        grClipFactorInit(&cv, 1);
        CHECK(cv.clipFactor[0] == 0 && cv.clipFactor[1] == 0x20000000);
        grClipFactorInit(&cv, 2);
        CHECK(cv.clipFactor[0] == 0 && cv.clipFactor[1] == 0);
    }

    /* grClipInterpolate: symmetric distances cross at t = 1/2 (unit) */
    d = IScalar_set(3, 0x20000000);                 /* +0.5 */
    CHECK(grClipInterpolate(d, IScalar_negate(d)) == 0x10000000);
    CHECK(grClipInterpolate(IScalar_negate(d), d) == 0x10000000);

    /* grVertexCacheGetTemp: slots 92.. at renderStack+1332+128*idx */
    n = grVertexCacheGetTemp(&st, 0);
    CHECK(n == 92);
    vb = (_grVertex *)(stack + 1332);
    CHECK(vb[92].index == -1 && vb[92].stateMask == 0);
    CHECK(grVertexCacheGetTemp(&st, 11) == 103);

    /* grClipper: triangle straddling guardband plane 0 (z+w, shift 0):
     * v0/v1 inside (dist +), v2 outside (dist -) -> the outside vertex is
     * replaced by two intersection temps (fan 0,1,92,93); clip factors are
     * the barycentric unit pairs (1,0)/(0,1)/(0,0); temps carry mask 69. */
    vb[0].clipPosition.e = 127;
    vb[0].clipPosition.m[0] = 0;
    vb[0].clipPosition.m[1] = 0;
    vb[0].clipPosition.m[2] = 0x20000000;
    vb[0].clipPosition.m[3] = 0x20000000;
    vb[1] = vb[0];
    vb[2] = vb[0];
    vb[2].clipPosition.m[2] = (int)0xD0000000;      /* -0.75: z+w < 0 */
    vb[2].clipMask = 1;                              /* outside flag */
    indices = grSetIndices(0, 1, 2);
    n = grClipper(&st, fan, indices);
    CHECK(n == 4);
    CHECK(fan[0] == 0 && fan[1] == 1 && fan[2] == 92 && fan[3] == 93);
    CHECK(vb[92].stateMask == 69);
    CHECK(vb[92].index == -1 && vb[92].clipMask == 0);
    CHECK(vb[92].normal[0] == 0);
    CHECK(vb[92].normal[1] == 0x06666660);           /* lerp(ONE,0,0.8) */
    CHECK(vb[93].normal[0] == 0x06666660);
    CHECK(vb[93].normal[1] == 0);
    CHECK(vb[92].clipPosition.e == 127);
    CHECK(vb[92].clipPosition.m[3] == 0x20000000);   /* w = 1.0 */
    CHECK(vb[92].clipPosition.m[2] == (int)0xE0000000); /* z = -0.5 */
    CHECK(vb[0].normal[0] == 0x20000000);            /* clip factors stored */
    CHECK(vb[0].normal[1] == 0);
    CHECK(vb[1].normal[0] == 0);
    CHECK(vb[1].normal[1] == 0x20000000);

    /* fully outside plane 0 -> rejected (mask need only be nonzero) */
    vb[0].clipPosition.m[2] = (int)0xD0000000;
    vb[1].clipPosition.m[2] = (int)0xD0000000;
    n = grClipper(&st, fan, indices);
    CHECK(n == 0);

    /* a triangle with no clip bit in any vertex dies at the grClipper.inl
     * 396 assert (the queue stage only calls grClipper when the packed
     * vertex clipMask has a clipping bit) - dead-end, not exercised */
}

/* --- grApiFast.inl: the grFast vertex cache --------------------------------- */

static void test_vertextcache(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _grVertexCache *cache;
    _grVertex *vb;
    static float pos[66][3];
    TEST_RENDER_STACK(stack);
    int c, i;

    for (i = 0; i < 66; ++i)
    {
        pos[i][0] = (float)i;
        pos[i][1] = (float)(i + 1);
        pos[i][2] = (float)(i + 2);
    }
    IMatrix4x4_identity(&id);
    memset(&st, 0, sizeof(st));
    memset(stack, 0, sizeof(stackStorage.bytes));
    st.renderStack = stack;
    st.renderStackAllocated = 1;
    st.vertexCache = stack;                  /* grSetupRendering layout */
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    st.vertexArray[0].size = 3;
    st.vertexArray[0].type = GR_DATATYPE_FLOAT;
    st.vertexArray[0].data = pos;
    st.vertexArray[0].stride = 12;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    grSetRectangle(&st.renderScissor, 0, 100, 0, 100);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);

    grVertexCacheInvalidate(&st);
    cache = (_grVertexCache *)stack;
    vb = (_grVertex *)(stack + 1332);
    CHECK(cache->batchLockMask == 0);
    CHECK(cache->totalFreeSlots == 64 && cache->allocIndex == 64);
    CHECK(cache->prevVertexIndices[0] == -1 && cache->prevVertexIndices[1] == -1 &&
          cache->prevVertexIndices[2] == -1);
    CHECK(cache->prevCacheIndices[0] == 0 && cache->prevCacheIndices[2] == 0);
    CHECK(*(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX) == 0);
    CHECK(vb[0].index == -1);
    CHECK(cache->cacheIndices[0] == 0 && cache->cacheIndices[31] == 0);

    /* miss: slot = (v ^ v>>3 ^ v>>5) & 0x3F; full transform runs */
    c = grVertexCacheGet(&st, 5);
    CHECK(c == 5);
    CHECK(vb[5].index == 5);
    CHECK(vb[5].stateMask == 4);
    CHECK(vb[5].position.objectSpaceFloat[0].e == 0x7f818181u);
    CHECK(vb[5].position.objectSpaceFloat[0].m[0] == 0x28000000);
    CHECK(vb[5].position.objectSpaceFloat[0].m[1] == 0x30000000);
    CHECK(vb[5].position.objectSpaceFloat[0].m[2] == 0x38000000);
    CHECK(vb[5].clipPosition.e == 0x81);
    CHECK(vb[5].clipPosition.m[0] == 0x28000000);
    CHECK(vb[5].clipPosition.m[1] == 0x30000000);
    CHECK(vb[5].clipPosition.m[2] == 0x38000000);
    CHECK(vb[5].clipPosition.m[3] == 0x08000000);
    CHECK(cache->totalFreeSlots == 63);
    CHECK(cache->batchLockMask == 0x20);
    CHECK(cache->cacheIndices[5] == 5);

    /* hit: same index, no free-slot change */
    CHECK(grVertexCacheGet(&st, 5) == 5);
    CHECK(cache->totalFreeSlots == 63);

    /* second bucket entry */
    CHECK(grVertexCacheGet(&st, 0x0A) == 11);            /* 0x0A^1^0 = 0x0B */
    CHECK(cache->cacheIndices[11] == 0x0B);
    CHECK(cache->totalFreeSlots == 62);
    CHECK(grVertexCacheGet(&st, 0x2D) == 41);            /* 0x2D^5^1 = 0x29 */
    CHECK(cache->cacheIndices[9] == 0x29);
    CHECK(cache->totalFreeSlots == 61);

    /* byte-0 hit keeps the entry */
    CHECK(grVertexCacheGet(&st, 0x0A) == 11);
    CHECK(cache->cacheIndices[11] == 0x0B);
    CHECK(cache->totalFreeSlots == 61);

    /* eviction: 0x41 hashes to slot 11 (locked) -> highest unlocked = 63 */
    CHECK(grVertexCacheGet(&st, 0x41) == 63);
    CHECK(cache->cacheIndices[11] == 0x00000B3F);
    CHECK(cache->totalFreeSlots == 60);
    CHECK((HGuint64)cache->batchLockMask == (HGuint64)0x8000020000000820ull);
    CHECK(vb[63].index == 0x41);

    /* byte-1 hit rotates the packed entry */
    CHECK(grVertexCacheGet(&st, 0x0A) == 11);
    CHECK(cache->cacheIndices[11] == 0x3F00000B);
    CHECK(cache->totalFreeSlots == 60);

    /* sentinel overwrite: get(0) reuses the invalidated vb[0] slot */
    CHECK(grVertexCacheGet(&st, 0) == 0);
    CHECK(vb[0].index == 0);
    CHECK(cache->totalFreeSlots == 59);
}

/* --- grApiFast.inl: grQueuePrimitive + wrappers ----------------------------- */

static void test_queueprimitive(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _grVertexCache *cache;
    _grVertex *vb;
    unsigned int *q;
    static float pos[66][3];
    TEST_RENDER_STACK(stack);
    int i, qidx;

    for (i = 0; i < 66; ++i)
    {
        pos[i][0] = (float)(i % 10);
        pos[i][1] = (float)(i % 7);
        pos[i][2] = 0.5f * (float)(i % 3);
    }
    pos[5][0] = 0.5f;  pos[5][1] = 0.5f;  pos[5][2] = 0.5f;
    pos[7][0] = 0.25f; pos[7][1] = 0.0f;  pos[7][2] = 0.0f;
    pos[9][0] = 0.0f;  pos[9][1] = 0.25f; pos[9][2] = 0.0f;
    pos[20][0] = -5.0f; pos[20][1] = 0.0f; pos[20][2] = 0.0f;
    pos[21][0] = 1.0f;  pos[21][1] = 0.0f; pos[21][2] = 0.0f;
    pos[22][0] = 1.0f;  pos[22][1] = 1.0f; pos[22][2] = 0.0f;
    pos[23][0] = -5.0f; pos[23][1] = 0.0f; pos[23][2] = 0.0f;
    pos[24][0] = -5.0f; pos[24][1] = 0.0f; pos[24][2] = 0.0f;
    pos[30][0] = 0.0f;  pos[30][1] = 0.0f; pos[30][2] = 0.0f;
    pos[31][0] = 0.5f;  pos[31][1] = 0.0f; pos[31][2] = 0.0f;
    pos[32][0] = 0.0f;  pos[32][1] = 0.5f; pos[32][2] = 0.0f;
    pos[40][0] = 0.0f;  pos[40][1] = 0.0f; pos[40][2] = 0.0f;
    pos[41][0] = 0.5f;  pos[41][1] = 0.0f; pos[41][2] = 0.0f;
    pos[42][0] = 0.0f;  pos[42][1] = 0.5f; pos[42][2] = 0.0f;
    pos[43][0] = 0.0f;  pos[43][1] = 0.0f; pos[43][2] = 0.0f;
    pos[50][0] = 0.0f;  pos[50][1] = 0.0f; pos[50][2] = 0.0f;
    pos[51][0] = 0.25f; pos[51][1] = 0.0f; pos[51][2] = 0.0f;
    pos[52][0] = 0.0f;  pos[52][1] = 0.25f; pos[52][2] = 0.0f;
    pos[53][0] = 0.0f;  pos[53][1] = 0.0f; pos[53][2] = 0.0f;
    pos[54][0] = 0.5f;  pos[54][1] = 0.0f; pos[54][2] = 0.0f;
    pos[55][0] = 0.0f;  pos[55][1] = 0.5f; pos[55][2] = 0.0f;
    IMatrix4x4_identity(&id);
    memset(&st, 0, sizeof(st));
    memset(stack, 0, sizeof(stackStorage.bytes));
    st.renderStack = stack;
    st.renderStackAllocated = 1;
    st.vertexCache = stack;
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    st.vertexArray[0].size = 3;
    st.vertexArray[0].type = GR_DATATYPE_FLOAT;
    st.vertexArray[0].data = pos;
    st.vertexArray[0].stride = 12;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    grSetRectangle(&st.renderScissor, 0, 100, 0, 100);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grVertexCacheInvalidate(&st);
    st.viewportScale.normal[0] = 0x640;
    st.viewportScale.normal[1] = 0x640;
    cache = (_grVertexCache *)stack;
    vb = (_grVertex *)(stack + 1332);
    q = (unsigned int *)(stack + 168);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);

    /* point: single cache slot in all three packed bytes; viewport transform
     * ran (bit 8) and the screen position comes from the 0x01000000 scale */
    grQueuePrimitive1(&st, 5);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 2);
    CHECK(q[0] == 0x00050505 && q[1] == 0);
    CHECK(cache->prevVertexIndices[0] == 5 && cache->prevVertexIndices[1] == -1);
    CHECK(cache->prevCacheIndices[0] == 5 && cache->prevCacheIndices[1] == 5 &&
          cache->prevCacheIndices[2] == 5);
    CHECK(cache->totalFreeSlots == 63);
    CHECK(vb[5].stateMask == 0x000c);
    CHECK(vb[5].screenPosition[0] == 0x6300);
    CHECK(vb[5].screenPosition[1] == 0x6300);

    /* line: ci1 == ci2 in the packed command; fresh slot for vertex 7 */
    grQueuePrimitive2(&st, 5, 7);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 4);
    CHECK(q[2] == 0x00070705 && q[3] == 0);
    CHECK(cache->totalFreeSlots == 62);
    CHECK(cache->prevCacheIndices[1] == 7);

    /* line reuse: vertex 7 keeps its slot via the previous-primitive cache */
    grQueuePrimitive2(&st, 7, 9);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 6);
    CHECK(q[4] == 0x00080807 && q[5] == 0);
    CHECK(cache->totalFreeSlots == 61);

    /* degenerate primitives: wrappers reject without queueing */
    grQueuePrimitive2(&st, 5, 5);
    grQueuePrimitive3(&st, 5, 5, 6);
    grQueuePrimitive3(&st, 5, 7, 5);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 6);

    /* a point whose vertex carries a clip bit is AND-rejected (all three
     * packed bytes are the same record) */
    grQueuePrimitive1(&st, 20);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 6);

    /* triangle straddling the guardband plane: OR of clip masks nonzero ->
     * command {packed | 0x10000000, 0} (grClipper runs at flush time) */
    grQueuePrimitive3(&st, 20, 21, 22);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 8);
    CHECK(q[6] == 0x10141716 && q[7] == 0);

    /* all three vertices share plane bit 0x44 -> AND-reject (0xFFC3) */
    grQueuePrimitive3(&st, 20, 23, 24);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 8);

    /* plain triangle: {packed, rounded screen area}; no side bit (cull off) */
    grQueuePrimitive3(&st, 30, 31, 32);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 10);
    CHECK(q[8] == 0x00251c1d);
    CHECK(q[9] == 0x0000004d);

    /* scissor reject: whole primitive left of x0 / right of x1 */
    st.viewportScale.normal[0] = 0x640;
    st.viewportScale.normal[1] = 0x640;
    st.viewportOffset.normal[0] = -1600000;
    grQueuePrimitive3(&st, 50, 51, 52);
    st.viewportOffset.normal[0] = 1600000;
    grQueuePrimitive3(&st, 50, 51, 52);
    st.viewportOffset.normal[0] = 0;
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 10);

    /* face cull: y-flipped viewport -> screen winding sign 1; cullNegative 1
     * keeps the triangle (side bit 24 set, side = sign ^ frontFace) */
    st.viewportScale.normal[0] = 0x640;
    st.viewportScale.normal[1] = (int)0xFFFFF9C0;   /* -0x640: GL-style y flip */
    grSetCapability(&st, GR_CAPABILITY_CULL_FACE, 1);
    st.cullNegative = 1;
    grQueuePrimitive3(&st, 40, 41, 42);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 12);
    CHECK(q[10] == 0x012e2d2cu);
    CHECK(q[11] == 0xffffffb2u);   /* mul64h floors: screen y = -0x6400 */

    /* same shape with cullNegative 0: the clip-space stp sign matches ->
     * culled at grApiFast.inl 1046, nothing queued */
    st.cullNegative = 0;
    grQueuePrimitive3(&st, 43, 41, 42);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 12);

    /* flush trigger boundary: the queue holds 128 commands (256 dwords);
     * the 129th primitive would cross the 254-dword trigger and flush */
    grDisableCapability(&st, GR_CAPABILITY_CULL_FACE);
    st.viewportScale.normal[0] = 0x640;
    st.viewportScale.normal[1] = 0x640;
    while (*(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX) < 256)
        grQueuePrimitive3(&st, 53, 54, 55);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 256);
    CHECK(cache->totalFreeSlots == 64 - 21);
    i = 0;
    {
        HGuint64 m = (HGuint64)cache->batchLockMask;

        while (m != 0)
        {
            i += (int)(m & 1);
            m >>= 1;
        }
    }
    CHECK(i == 21);
}

/* --- grApiFast.inl: grFetchPrimitives + grRenderElements/grRenderArrays ----- */

/* Dispatch fixtures mark fetched colors as computed and suppress pixel
 * writes so the tests can inspect primitive queues and vertex lists. */
static void noopPipeline(void *pipe)
{
    (void)pipe;
}

static void fetchVertexColorStage(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer = (_grVertex *)((unsigned char *)state->renderStack + 1332);
    unsigned char *lista = vertexList + 1;
    _grVertex *vertex = &vertexBuffer[vertexList[0]];

    do
    {
        vertex->stateMask |= GR_VERTEXMASK_COLOR_COMPUTED;
        vertex = &vertexBuffer[*lista];
    } while (*lista++ != 0xFF);
}

static int listlen(const unsigned char *p)
{
    int n = 0;

    while (p[n] != 0xFF)
        ++n;
    return n;
}

static float fpos[66][3];

static void test_fetchprimitives(void)
{
    Gerbera_s st;
    _IMatrix4x4 id;
    _grVertexCache *cache;
    _grVertex *vb;
    _grPixelPipe pp;
    TEST_RENDER_STACK(stack);
    static unsigned char seen[256];
    unsigned char idx8[16];
    unsigned short idx16[8];
    int i, qidx, pop;

    for (i = 0; i < 16; ++i)
    {
        idx8[i] = (unsigned char)i;
        fpos[i][0] = 0.06f * (float)i;
        fpos[i][1] = 0.07f * (float)((i * 7) % 13);
        fpos[i][2] = 0.5f;
        seen[i] = 0;
    }
    idx16[0] = 0; idx16[1] = 1; idx16[2] = 2; idx16[3] = 3;

    IMatrix4x4_identity(&id);
    memset(&st, 0, sizeof(st));
    memset(stack, 0, sizeof(stackStorage.bytes));
    memset(&pp, 0, sizeof(pp));
    st.renderStack = stack;
    st.renderStackAllocated = 1;
    st.vertexCache = stack;
    st.pixelPipeline = &pp;
    st.pixelPipelineCode = noopPipeline;
    st.fetchVertexColor = fetchVertexColorStage;
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    grSetMatrix(&st, GR_MATRIXMODE_MODELVIEW, &id);
    grSetMatrix(&st, GR_MATRIXMODE_PROJECTION, &id);
    st.vertexArray[0].size = 3;
    st.vertexArray[0].type = GR_DATATYPE_FLOAT;
    st.vertexArray[0].data = fpos;
    st.vertexArray[0].stride = 12;
    grSetCapability(&st, GR_CAPABILITY_VERTEX_ARRAY, 1);
    grSetRectangle(&st.renderScissor, 0, 100, 0, 100);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grVertexCacheInvalidate(&st);
    st.viewportScale.normal[0] = 0x640;
    st.viewportScale.normal[1] = 0x640;
    cache = (_grVertexCache *)stack;
    vb = (_grVertex *)(stack + 1332);

    /* POINTS via arrays: base 10, count 5 -> vertices 10..14; hash slots
     * 11/10/13/12/15; one cache allocation per point */
    grSetupCulling(&st, GR_PRIMITIVETYPE_POINT);
    grRenderArrays(&st, GR_PRIMITIVE_POINTS, 5, 10);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 63);
    CHECK(vb[11].index == 10);
    CHECK(vb[15].index == 14);
    CHECK(listlen(stack + 1264) == 5);
    CHECK(stack[1264] == 15 && stack[1265] == 12 && stack[1266] == 13 &&
          stack[1267] == 10 && stack[1268] == 11);
    CHECK(listlen(stack + 1196) == 5);
    CHECK(stack[1196] == 15 && stack[1197] == 12 && stack[1198] == 13 &&
          stack[1199] == 10 && stack[1200] == 11);

    /* TRIANGLES via u8 elements: rotated queue order (src[2], src[0], src[1]);
     * flush lists build in reverse command order */
    grVertexCacheInvalidate(&st);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grRenderElements(&st, GR_PRIMITIVE_TRIANGLES, 9, GR_DATATYPE_UINT8, idx8);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 61);
    pop = 0;
    {
        HGuint64 m = (HGuint64)cache->batchLockMask;

        while (m != 0) { pop += (int)(m & 1); m >>= 1; }
    }
    CHECK(pop == 3);
    for (i = 0; i <= 8; ++i)
        seen[i] = 0;
    for (i = 0; i < 64; ++i)
        if (vb[i].index >= 0 && vb[i].index <= 8)
            seen[vb[i].index] = 1;
    for (i = 0; i <= 8; ++i)
        CHECK(seen[i] == 1);
    CHECK(listlen(stack + 1264) == 3);
    CHECK(stack[1264] == 9 && stack[1265] == 5 && stack[1266] == 2);
    CHECK(listlen(stack + 1196) == 9);
    CHECK(stack[1196] == 9 && stack[1197] == 6 && stack[1198] == 7 &&
          stack[1199] == 5 && stack[1200] == 3 && stack[1201] == 4 &&
          stack[1202] == 2 && stack[1203] == 0 && stack[1204] == 1);

    /* LINELOOP via u8: 4 vertices -> 4 edges (3 strip + closing (0,3));
     * both lists end up {0,3,2,1} */
    grVertexCacheInvalidate(&st);
    idx8[0] = 0; idx8[1] = 1; idx8[2] = 2; idx8[3] = 3;
    grSetupCulling(&st, GR_PRIMITIVETYPE_LINE);
    grRenderElements(&st, GR_PRIMITIVE_LINE_LOOP, 4, GR_DATATYPE_UINT8, idx8);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 62);
    CHECK(listlen(stack + 1264) == 4);
    CHECK(stack[1264] == 0 && stack[1265] == 3 && stack[1266] == 2 && stack[1267] == 1);
    CHECK(listlen(stack + 1196) == 4);
    CHECK(stack[1196] == 0 && stack[1197] == 3 && stack[1198] == 2 && stack[1199] == 1);

    /* TRIFAN via u8: center = byte 0; tris (2,0,1),(3,0,2),(4,0,3) */
    grVertexCacheInvalidate(&st);
    idx8[0] = 0; idx8[1] = 1; idx8[2] = 2; idx8[3] = 3; idx8[4] = 4;
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grRenderElements(&st, GR_PRIMITIVE_TRIANGLE_FAN, 5, GR_DATATYPE_UINT8, idx8);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 61);
    CHECK(listlen(stack + 1264) == 3);
    CHECK(stack[1264] == 4 && stack[1265] == 3 && stack[1266] == 2);
    CHECK(listlen(stack + 1196) == 5);
    CHECK(stack[1196] == 4 && stack[1197] == 0 && stack[1198] == 3 &&
          stack[1199] == 2 && stack[1200] == 1);

    /* TRISTRIP via u16: winding swap per segment; tris (2,0,1),(3,1,0) */
    grVertexCacheInvalidate(&st);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grRenderElements(&st, GR_PRIMITIVE_TRIANGLE_STRIP, 4, GR_DATATYPE_UINT16, idx16);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 61);
    CHECK(listlen(stack + 1264) == 2);
    CHECK(stack[1264] == 3 && stack[1265] == 2);
    CHECK(listlen(stack + 1196) == 4);
    CHECK(stack[1196] == 3 && stack[1197] == 2 && stack[1198] == 1 &&
          stack[1199] == 0);   /* strip tris (2,0,1) then (3,2,1) */

    /* LINES via u16: (1,0),(3,2) */
    grVertexCacheInvalidate(&st);
    grSetupCulling(&st, GR_PRIMITIVETYPE_LINE);
    grRenderElements(&st, GR_PRIMITIVE_LINES, 4, GR_DATATYPE_UINT16, idx16);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 62);
    CHECK(listlen(stack + 1264) == 2);
    CHECK(stack[1264] == 3 && stack[1265] == 1);
    CHECK(listlen(stack + 1196) == 4);
    CHECK(stack[1196] == 3 && stack[1197] == 2 && stack[1198] == 1 && stack[1199] == 0);

    /* degenerate first triangle drops through the wrapper */
    grVertexCacheInvalidate(&st);
    idx8[0] = 0; idx8[1] = 0; idx8[2] = 1; idx8[3] = 0; idx8[4] = 1; idx8[5] = 2;
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    grRenderElements(&st, GR_PRIMITIVE_TRIANGLES, 6, GR_DATATYPE_UINT8, idx8);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(listlen(stack + 1264) == 1);
    CHECK(stack[1264] == 2);

    /* compilationFailed short-circuits both entry points */
    grVertexCacheInvalidate(&st);
    st.compilationFailed = 1;
    grRenderElements(&st, GR_PRIMITIVE_TRIANGLES, 9, GR_DATATYPE_UINT8, idx8);
    grRenderArrays(&st, GR_PRIMITIVE_POINTS, 5, 10);
    grSetupCulling(&st, GR_PRIMITIVETYPE_TRIANGLE);
    qidx = *(int *)(stack + GR_RENDER_STACK_COMMAND_QUEUE_INDEX);
    CHECK(qidx == 0);
    CHECK(cache->totalFreeSlots == 64);
    st.compilationFailed = 0;
}

static void test_depth_span_rejection(void)
{
    _grPixelPipe pipe = {0};
    unsigned short depth[4][16];
    unsigned int spans[4] = { 4u << 16, 4u << 16, 4u << 16, 4u << 16 };
    unsigned int random = 1136u;
    int i, sample, rejected = 0;

    pipe.depthRow = depth;
    pipe.checked.depthBufferStride = sizeof(depth[0]);
    pipe.scanlineInfo = spans;
    pipe.numScanlines = 1;
    pipe.reference[0] = 100 << 14;
    for (i = 0; i < 4; ++i) depth[0][i] = 99;
    CHECK(grDepthSpansOccluded(&pipe, 0));
    depth[0][3] = 100;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    CHECK(grDepthSpansOccluded(&pipe, 1));
    depth[0][3] = 101;
    CHECK(!grDepthSpansOccluded(&pipe, 1));
    pipe.reference[0] = 0;
    pipe.xAdder[0] = -1;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.reference[0] = 0x3FFFFFFF;
    pipe.xAdder[0] = 1;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.reference[0] = 0;
    pipe.xAdder[0] = INT32_MIN;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.depthRow = (unsigned char *)depth + 1;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.depthRow = depth;
    pipe.checked.depthBufferStride = 31;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.checked.depthBufferStride = -32;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.checked.depthBufferStride = 0;
    CHECK(!grDepthSpansOccluded(&pipe, 0));
    pipe.checked.depthBufferStride = sizeof(depth[0]);

    for (sample = 0; sample < 1024; ++sample)
    {
        unsigned int ref;
        int strict = sample & 1, allHidden = 1, y;
        random = random * 1664525u + 1013904223u;
        pipe.reference[0] = (int)(random & 0x3FFFFFFFu);
        random = random * 1664525u + 1013904223u;
        pipe.xAdder[0] = (int)(random & 65535u) - 32768;
        pipe.yAdder[0] = (int)(random >> 16) - 32768;
        pipe.numScanlines = 4;
        ref = (unsigned int)pipe.reference[0];
        for (y = 0; y < 4; ++y, ref += (unsigned int)pipe.yAdder[0])
        {
            unsigned int x = (random >> (2 * y)) & 3u, count = 1u + ((random >> (3 * y)) & 7u), j;
            spans[y] = x | (count << 16);
            for (j = 0; j < count; ++j)
            {
                unsigned int z = (ref + (unsigned int)pipe.xAdder[0] * (x + j)) >> 14;
                unsigned short old = sample % 3 == 0 ? 0 : sample % 3 == 1 ? UINT16_MAX : (unsigned short)z;
                depth[y][x + j] = old;
                if (strict ? z < old : z <= old) allHidden = 0;
            }
        }
        i = grDepthSpansOccluded(&pipe, strict);
        CHECK(!i || allHidden);
        rejected += i;
    }
    CHECK(rejected > 300);
}

static void test_early_depth_gradients(void)
{
    Gerbera_s state = {0};
    _grPixelPipe pipe = {0};
    _grVertex *vertices;
    _grClipTriangle triangle;
    TEST_RENDER_STACK(stack);
    unsigned short depth[4][16];
    unsigned int spans[4] = {3u | (4u << 16), 3u | (4u << 16),
                             3u | (4u << 16), 3u | (4u << 16)};
    int expected[3 * GR_GRADIENT_MAX];
    unsigned int random = 1136;
    int sample, i;

    memset(stack, 0, sizeof(stackStorage.bytes));
    state.renderStack = stack;
    state.pixelPipeline = &pipe;
    state.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    vertices = (_grVertex *)(stack + 1332);
    vertices[0].screenPosition[0] = vertices[0].screenPosition[1] = 2 << 12;
    vertices[1].screenPosition[0] = 14 << 12;
    vertices[1].screenPosition[1] = 2 << 12;
    vertices[2].screenPosition[0] = 2 << 12;
    vertices[2].screenPosition[1] = 14 << 12;
    triangle.indices = grSetIndices(0, 1, 2);
    triangle.area = grRoundTriangleArea(grComputeTriangleArea(vertices, triangle.indices));
    pipe.depthRow = depth;
    pipe.checked.depthBufferStride = sizeof(depth[0]);
    pipe.scanlineInfo = spans;
    pipe.numScanlines = 4;
    pipe.yStart = 3;

    for (sample = 0; sample < 256; ++sample)
    {
        triangle.side = sample & 1;
        state.depthFunc = sample & 2 ? GR_FUNC_LESS : GR_FUNC_LEQUAL;
        state.capabilities[GR_CAPABILITY_SMOOTH_SHADING] = (sample >> 2) & 1;
        for (i = 0; i < 3; ++i)
        {
            int side, component;
            random = random * 1664525u + 1013904223u;
            vertices[i].screenPosition[2] = (100 + (int)(random >> 24)) << 14;
            for (side = 0; side < 2; ++side)
                for (component = 0; component < 4; ++component)
                    vertices[i].color.final[side][component] = (unsigned short)(random >> (component * 3));
        }
        memset(pipe.reference, 0x5A, sizeof(expected));
        grSetupTriangleGradients(&state, &triangle, 1, 0x3Fu);
        memcpy(expected, pipe.reference, sizeof(expected));
        memset(depth, 0xFF, sizeof(depth));
        memset(pipe.reference, 0x5A, sizeof(expected));
        CHECK(grSetupTriangleGradientsVisible(&state, &triangle, 1, 0x3Fu, 1));
        CHECK(memcmp(expected, pipe.reference, sizeof(expected)) == 0);

        memset(depth, 0, sizeof(depth));
        memset(pipe.reference, 0x5A, sizeof(expected));
        CHECK(!grSetupTriangleGradientsVisible(&state, &triangle, 1, 0x3Fu, 1));
        CHECK(pipe.reference[GR_GRADIENT_DEPTH] == expected[GR_GRADIENT_DEPTH]);
        CHECK(pipe.xAdder[GR_GRADIENT_DEPTH] == expected[GR_GRADIENT_MAX]);
        CHECK(pipe.yAdder[GR_GRADIENT_DEPTH] == expected[2 * GR_GRADIENT_MAX]);
        for (i = GR_GRADIENT_FOG; i <= GR_GRADIENT_COLOR_A; ++i)
            CHECK(pipe.reference[i] == 0x5A5A5A5A && pipe.xAdder[i] == 0x5A5A5A5A &&
                  pipe.yAdder[i] == 0x5A5A5A5A);
    }
}

static void test_gradients(void)
{
    Gerbera_s st;
    _grClipTriangle tri;
    _grVertex *vb;
    _grPixelPipe *pp;
    _grGradientInfo info;
    TEST_RENDER_STACK(stack);
    int i;
    HGint64 area;
    HGint64 rec;

    memset(&st, 0, sizeof(st));
    memset(stack, 0, sizeof(stackStorage.bytes));
    st.renderStack = stack;
    st.renderStackAllocated = 1;
    st.vertexCache = stack;
    st.pixelPipeline = stack + TEST_PIXEL_PIPE_OFFSET; /* grExecutePixelPipeline's record */
    st.primitiveType = GR_PRIMITIVETYPE_TRIANGLE;
    pp = (_grPixelPipe *)(stack + TEST_PIXEL_PIPE_OFFSET);
    vb = (_grVertex *)(stack + 1332);

    /* three well-separated screen positions in the real viewport-transform
     * format (x/y pixel << 8 per IVector4_viewportTransform's <<8 pad, z in
     * the depth-gradient units) with distinct depth/fog/front+back colors */
    vb[0].screenPosition[0] = 100 << 8;
    vb[0].screenPosition[1] = 80 << 8;
    vb[0].screenPosition[2] = 1600;
    vb[1].screenPosition[0] = 300 << 8;
    vb[1].screenPosition[1] = 40 << 8;
    vb[1].screenPosition[2] = 3200;
    vb[2].screenPosition[0] = 60 << 8;
    vb[2].screenPosition[1] = 400 << 8;
    vb[2].screenPosition[2] = 6400;
    vb[0].color.final[0][0] = 0x1000;
    vb[0].color.final[0][1] = 0x2000;
    vb[0].color.final[0][2] = 0x3000;
    vb[0].color.final[0][3] = 0x8000;
    vb[1].color.final[0][0] = 0xC000;
    vb[1].color.final[0][1] = 0x4000;
    vb[1].color.final[0][2] = 0x2000;
    vb[1].color.final[0][3] = 0x4000;
    vb[2].color.final[0][0] = 0x3000;
    vb[2].color.final[0][1] = 0xE000;
    vb[2].color.final[0][2] = 0x7000;
    vb[2].color.final[0][3] = 0xC000;
    vb[0].color.final[1][0] = 0x0500;
    vb[0].color.final[1][1] = 0x0600;
    vb[0].color.final[1][2] = 0x0700;
    vb[0].color.final[1][3] = 0x0800;
    vb[1].color.final[1][0] = 0x1500;
    vb[1].color.final[1][1] = 0x1600;
    vb[1].color.final[1][2] = 0x1700;
    vb[1].color.final[1][3] = 0x1800;
    vb[2].color.final[1][0] = 0x2500;
    vb[2].color.final[1][1] = 0x2600;
    vb[2].color.final[1][2] = 0x2700;
    vb[2].color.final[1][3] = 0x2800;
    tri.indices = grSetIndices(0, 1, 2);
    tri.side = 0;
    area = grRoundTriangleArea(grComputeTriangleArea(vb, tri.indices));
    tri.area = (int)area;
    CHECK(tri.area != 0);

#define GRAD_VALUE(pp_, grad, sx, sy)                                      \
    ((pp_)->reference[grad] +                                              \
     ((((HGint64)(pp_)->xAdder[grad] * ((sx)-4096)) +                      \
       ((HGint64)(pp_)->yAdder[grad] * ((sy)-4096))) >>                    \
      12))

#define GRAD_POISON()                                                      \
    do                                                                     \
    {                                                                      \
        for (i = 0; i < GR_GRADIENT_MAX; ++i)                              \
        {                                                                  \
            pp->reference[i] = 0x5A5A5A5A;                                 \
            pp->xAdder[i] = 0x5A5A5A5A;                                    \
            pp->yAdder[i] = 0x5A5A5A5A;                                    \
        }                                                                  \
    } while (0)

    /* mask 0 leaves the arrays untouched */
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 0);
    CHECK(pp->reference[0] == 0x5A5A5A5A);
    CHECK(pp->xAdder[5] == 0x5A5A5A5A);
    CHECK(pp->yAdder[9] == 0x5A5A5A5A);

    /* depth: exact reconstruction at v0 (reference subtracts the v0 term
     * with the same floor), plane fit at v1/v2 */
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    CHECK(pp->xAdder[0] != 0);
    rec = GRAD_VALUE(pp, GR_GRADIENT_DEPTH, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == 1600);
    rec = GRAD_VALUE(pp, GR_GRADIENT_DEPTH, vb[1].screenPosition[0],
                     vb[1].screenPosition[1]);
    CHECK(rec > 3200 - 64 && rec < 3200 + 64);
    rec = GRAD_VALUE(pp, GR_GRADIENT_DEPTH, vb[2].screenPosition[0],
                     vb[2].screenPosition[1]);
    CHECK(rec > 6400 - 64 && rec < 6400 + 64);

    /* fog rides the back-alpha slot; reference base = (fog0 << 8) + 128 */
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 2);
    CHECK(pp->reference[0] == 0x5A5A5A5A); /* depth untouched */
    rec = GRAD_VALUE(pp, GR_GRADIENT_FOG, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x0800 << 8) + 128);
    rec = GRAD_VALUE(pp, GR_GRADIENT_FOG, vb[1].screenPosition[0],
                     vb[1].screenPosition[1]);
    /* imRcpu's reciprocal carries ~2.6e-4 relative error (8-bit LUT seed +
     * 2 NR steps, inherited from the DLL), so vertex fits are approximate */
    CHECK(rec > (0x1800 << 8) - 2048 && rec < (0x1800 << 8) + 2048);

    /* smooth colors: R/G/B/A plane fits, exact at v0, alpha from the FRONT
     * row even on the back side */
    grSetCapability(&st, GR_CAPABILITY_SMOOTH_SHADING, 1);
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 0x3C);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_R, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x1000 << 8) + 0x8000);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_G, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x2000 << 8) + 0x8000);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_B, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x3000 << 8) + 0x8000);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_A, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x8000 << 8) + 0x8000);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_R, vb[1].screenPosition[0],
                     vb[1].screenPosition[1]);
    CHECK(rec > (0xC000 << 8) - 0x20000 && rec < (0xC000 << 8) + 0x20000);
    tri.side = 1;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 0x3C);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_R, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x0500 << 8) + 0x8000);
    rec = GRAD_VALUE(pp, GR_GRADIENT_COLOR_A, vb[0].screenPosition[0],
                     vb[0].screenPosition[1]);
    CHECK(rec == (0x8000 << 8) + 0x8000);
    tri.side = 0;

    /* flat shading: constants from the flat-shade vertex, zero adders */
    grSetCapability(&st, GR_CAPABILITY_SMOOTH_SHADING, 0);
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 0x3C);
    CHECK(pp->reference[GR_GRADIENT_COLOR_R] == (0xC000 << 8) + 0x8000);
    CHECK(pp->reference[GR_GRADIENT_COLOR_G] == (0x4000 << 8) + 0x8000);
    CHECK(pp->reference[GR_GRADIENT_COLOR_B] == (0x2000 << 8) + 0x8000);
    CHECK(pp->reference[GR_GRADIENT_COLOR_A] == (0x4000 << 8) + 0x8000);
    CHECK(pp->xAdder[GR_GRADIENT_COLOR_R] == 0 &&
          pp->xAdder[GR_GRADIENT_COLOR_G] == 0 &&
          pp->xAdder[GR_GRADIENT_COLOR_B] == 0 &&
          pp->xAdder[GR_GRADIENT_COLOR_A] == 0);
    CHECK(pp->yAdder[GR_GRADIENT_COLOR_R] == 0 &&
          pp->yAdder[GR_GRADIENT_COLOR_G] == 0 &&
          pp->yAdder[GR_GRADIENT_COLOR_B] == 0 &&
          pp->yAdder[GR_GRADIENT_COLOR_A] == 0);
    /* back side takes the back-color row; alpha stays the front row */
    tri.side = 1;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 0x3C);
    CHECK(pp->reference[GR_GRADIENT_COLOR_R] == (0x1500 << 8) + 0x8000);
    CHECK(pp->reference[GR_GRADIENT_COLOR_A] == (0x4000 << 8) + 0x8000);
    tri.side = 0;

    /* depth offset: grSetupGradient rebuilds the reference fresh each call,
     * then grComputeDepthOffset adds units once plus a format-scaled copy
     * (drawSurface byte +10 = depthBytes: 1 -> <<8, 2 -> as-is, 4 -> >>16)
     * and factor*steeperAdder >> 16 */
    CHECK(grIsDepthOffsetEnabled(&st, GR_PRIMITIVETYPE_TRIANGLE) == 0);
    grSetCapability(&st, GR_CAPABILITY_POLYGON_OFFSET_FILL, 1);
    CHECK(grIsDepthOffsetEnabled(&st, GR_PRIMITIVETYPE_TRIANGLE) == 1);
    st.depthOffsetUnits = 0;
    st.depthOffsetFactor = 0;
    st.drawSurface.depthBytes = 2;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    rec = pp->reference[0];   /* base gradient reference, no offset */
    st.depthOffsetUnits = 5;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    CHECK(pp->reference[0] == rec + 10);   /* 5 + 5 (depthBytes 2) */
    st.drawSurface.depthBytes = 4;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    CHECK(pp->reference[0] == rec + 5 + (5 >> 16));
    st.drawSurface.depthBytes = 1;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    CHECK(pp->reference[0] == rec + 5 + (5 << 8));
    st.drawSurface.depthBytes = 2;
    st.depthOffsetFactor = 3;
    GRAD_POISON();
    grSetupTriangleGradients(&st, &tri, 1, 1);
    {
        int steep = pp->xAdder[0] > pp->yAdder[0] ? pp->xAdder[0] : pp->yAdder[0];

        if (steep < 0)
            steep = -steep;
        CHECK(pp->reference[0] == rec + 10 + ((HGint64)3 * steep >> 16));
    }
    grSetCapability(&st, GR_CAPABILITY_POLYGON_OFFSET_FILL, 0);
    st.depthOffsetFactor = 0;
    st.depthOffsetUnits = 0;
    st.drawSurface.depthBytes = 0;

    /* Offset units and the reference use wrapping ARM ADD/LSL operations. */
    {
        static const int cases[][7] = {
            { -1, 0, 0, 0, 0, 1, -257 },
            { 0x7FFFFFFF, 0, 1, 0, 0, 1, 0x7FFFFF00 },
            { (int)0x80000000u, 0, (int)0x80000000u, 0, 0, 2, (int)0x80000000u },
            { -65536, 0, 0, 0, 0, 4, -65537 },
            { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 65536, 0, 2, -4 },
            { 0, 0x7FFFFFFF, 1, (int)0x80000000u, 65536, 1, (int)0x80000000u }
        };
        int index;
        for (index = 0; index < 6; ++index)
        {
            st.depthOffsetUnits = cases[index][0];
            st.depthOffsetFactor = cases[index][1];
            pp->reference[0] = cases[index][2];
            pp->xAdder[0] = cases[index][3];
            pp->yAdder[0] = cases[index][4];
            st.drawSurface.depthBytes = (unsigned char)cases[index][5];
            grComputeDepthOffset(&st);
            CHECK(pp->reference[0] == cases[index][6]);
        }
        st.depthOffsetUnits = st.depthOffsetFactor = 0;
        st.drawSurface.depthBytes = 0;
    }

#undef GRAD_POISON

    /* grSetupGradient vs grComputeDeltas produce identical adders (the
     * >=32/<32 shift split is value-equivalent), and the reference identity
     * ref = g01 - ((xAdd*v0sx + yAdd*v0sy) >> 12) holds */
    info.pPP = pp;
    info.shift = 0;
    info.v0sx = 333;
    info.v0sy = 444;
    info.v01sxPerArea = 12345;
    info.v01syPerArea = -23456;
    info.v02sxPerArea = 34567;
    info.v02syPerArea = -45678;
    grSetupGradient(&info, GR_GRADIENT_TEX1_T, 777000, -888000, 999000);
    grComputeDeltas(&info, GR_GRADIENT_TEX1_S, 777000, -888000);
    CHECK(pp->xAdder[GR_GRADIENT_TEX1_T] == pp->xAdder[GR_GRADIENT_TEX1_S]);
    CHECK(pp->yAdder[GR_GRADIENT_TEX1_T] == pp->yAdder[GR_GRADIENT_TEX1_S]);
    CHECK(pp->reference[GR_GRADIENT_TEX1_T] ==
          999000 - (((HGint64)pp->xAdder[GR_GRADIENT_TEX1_T] * 333 +
                     (HGint64)pp->yAdder[GR_GRADIENT_TEX1_T] * 444) >> 12));
    /* the >= 32 branch: shift + 14 >= 32 -> shift >= 18 */
    info.shift = 20;
    grSetupGradient(&info, GR_GRADIENT_TEX1_T, 777000, -888000, 999000);
    grComputeDeltas(&info, GR_GRADIENT_TEX1_S, 777000, -888000);
    CHECK(pp->xAdder[GR_GRADIENT_TEX1_T] == pp->xAdder[GR_GRADIENT_TEX1_S]);
    CHECK(pp->yAdder[GR_GRADIENT_TEX1_T] == pp->yAdder[GR_GRADIENT_TEX1_S]);

    memset(&info, 0, sizeof(info));
    info.pPP = pp;
    info.v0sx = 4096;
    info.v01syPerArea = 16384;
    grSetupGradient(&info, GR_GRADIENT_TEX1_S, 0, 1, (int)0x80000000u);
    CHECK(pp->xAdder[GR_GRADIENT_TEX1_S] == 1);
    CHECK(pp->reference[GR_GRADIENT_TEX1_S] == 0x7FFFFFFF);
    CHECK(grComputeGradientReference(4096, 0, (int)0x80000000u, 0) == (int)0x80000000u);
    CHECK(grComputeGradientReference(0, 4096, 0, (int)0x80000000u) == (int)0x80000000u);

    /* ARM 0x15D4C/0x15E8C negate in 32 bits before multiplying. A minimum
     * signed derivative therefore stays negative, including across the
     * high-word shift branch used for very small gradients. */
    {
        static const int shifts[] = { 0, 18, 31 };
        static const int expected[] = { (int)0x80000000u, -8192, -1 };
        int index, axis;
        memset(&info, 0, sizeof(info));
        info.pPP = pp;
        info.v01sxPerArea = info.v02syPerArea = 16384;
        for (index = 0; index < 3; ++index)
        {
            info.shift = shifts[index];
            for (axis = 0; axis < 2; ++axis)
            {
                int g01 = axis == 0 ? (int)0x80000000u : 1;
                int g02 = axis == 1 ? (int)0x80000000u : 1;
                grSetupGradient(&info, GR_GRADIENT_TEX1_T, g01, g02, 123);
                grComputeDeltas(&info, GR_GRADIENT_TEX1_S, g01, g02);
                CHECK(pp->reference[GR_GRADIENT_TEX1_T] == 123);
                CHECK(pp->xAdder[GR_GRADIENT_TEX1_T] == (axis == 0 ? expected[index] : -1));
                CHECK(pp->yAdder[GR_GRADIENT_TEX1_T] == (axis == 1 ? expected[index] : -1));
                CHECK(pp->xAdder[GR_GRADIENT_TEX1_S] == pp->xAdder[GR_GRADIENT_TEX1_T]);
                CHECK(pp->yAdder[GR_GRADIENT_TEX1_S] == pp->yAdder[GR_GRADIENT_TEX1_T]);
            }
        }
    }

#undef GRAD_VALUE
}

/* --- test_scanconvert: grDivsu + scan conversion + the real triangle draw ----
 * Geometry (pixels, 20.12 screen coords): v0=(10,12), v1=(30,20), v2=(18,28),
 * scissor {0,64,0,64}. Hand-derived scanline words (left | numPixels<<16). */
static void test_scanconvert(void)
{
    TEST_RENDER_STACK(stack);
    static unsigned char colorBuf[64 * 64 * 2];
    static unsigned char depthBuf[64 * 64 * 4];
    static unsigned char stencilBuf[64 * 64];
    Gerbera_s stObj;
    Gerbera_s *st = &stObj;
    _grPixelPipe *pp;
    _grVertex *vb;
    _grClipTriangle tri;
    _grEdgePair pairs[2];
    unsigned int *words;
    int i;

    memset(st, 0, sizeof(*st));
    CHECK(grDivsu(100, 7) == 14);
    CHECK(grDivsu(0, 5) == 0);
    {
        static const int numerators[] = {
            0, 1, -1, 16384, -16384, 0x7FFFFFFF, (int)0x80000000u
        };
        unsigned int random = 1136;
        int denominator, n;
        for (denominator = 1; denominator <= 8192; ++denominator)
        {
            for (n = 0; n < (int)(sizeof(numerators) / sizeof(numerators[0])); ++n)
                CHECK(grDivideEdgeSlope(st, numerators[n], denominator) ==
                      numerators[n] / denominator);
            CHECK(grDivideEdgeSlope(st, denominator - 1, denominator) == 0);
            CHECK(grDivideEdgeSlope(st, denominator, denominator) == 1);
            CHECK(grDivideEdgeSlope(st, denominator + 1, denominator) ==
                  (denominator + 1) / denominator);
        }
        for (n = 0; n < 16384; ++n)
        {
            int numerator, pass;
            random = random * 1664525u + 1013904223u;
            numerator = (int)random;
            random = random * 1664525u + 1013904223u;
            denominator = (int)(random & 0x7FFFFFFFu) | 1;
            for (pass = 0; pass < 2; ++pass)
                CHECK(grDivideEdgeSlope(st, numerator, denominator) == numerator / denominator);
        }
    }

    /* direct scanline emission without X scissoring */
    memset(pairs, 0, sizeof(pairs));
    pairs[0].xStart[0] = 100 << 14;
    pairs[0].xStart[1] = 200 << 14;
    pairs[1].xStart[0] = 50 << 14;
    pairs[1].xStart[1] = 250 << 14;
    memset(stack, 0, sizeof(stackStorage.bytes));
    grScanConvertWithoutXScissoring(pairs, (unsigned int *)stack, 3, 2);
    words = (unsigned int *)stack;
    CHECK(words[0] == (100u | (100u << 16)));
    CHECK(words[2] == (100u | (100u << 16)));
    CHECK(words[3] == (50u | (200u << 16)));
    CHECK(words[4] == (50u | (200u << 16)));

    /* ... and with X scissoring clamping to {150..180} */
    grSetRectangle(&st->renderScissor, 150, 180, 0, 64);
    grScanConvertWithXScissoring(pairs, (unsigned int *)stack, &st->renderScissor, 3, 2);
    CHECK(words[0] == (150u | (30u << 16)));
    CHECK(words[3] == (150u | (30u << 16)));
    grSetRectangle(&st->renderScissor, 0, 64, 0, 64);

    /* full grScanConvertTriangle: the hand-worked triangle. The vertex
     * records are shuffled so the y-sort inside grDrawClippedTriangle is
     * exercised in the end-to-end case below. */
    st->renderStack = stack;
    st->renderStackAllocated = 1;
    st->pixelPipeline = (_grPixelPipe *)(stack + TEST_PIXEL_PIPE_OFFSET);
    pp = st->pixelPipeline;
    vb = (_grVertex *)(stack + 1332);
    memset(stack, 0xCC, sizeof(stackStorage.bytes));
    memset(&vb[0], 0, sizeof(vb[0]));
    memset(&vb[1], 0, sizeof(vb[1]));
    memset(&vb[2], 0, sizeof(vb[2]));
    vb[0].screenPosition[0] = 30 << 12;
    vb[0].screenPosition[1] = 20 << 12;
    vb[1].screenPosition[0] = 18 << 12;
    vb[1].screenPosition[1] = 28 << 12;
    vb[2].screenPosition[0] = 10 << 12;
    vb[2].screenPosition[1] = 12 << 12;

    /* inside the scissor on X: the plain path (16 rows, none trimmed:
     * the last row still covers one pixel) */
    CHECK(grScanConvertTriangle(st,
             (_grRasterPoint *)&vb[2].screenPosition[0],
             (_grRasterPoint *)&vb[0].screenPosition[0],
             (_grRasterPoint *)&vb[1].screenPosition[0], 8) == 16);
    CHECK(pp->yStart == 12);
    CHECK(pp->numScanlines == 16);
    CHECK(pp->scanlineInfo == (unsigned int *)(stack + 15668));
    words = pp->scanlineInfo;
    CHECK(words[0] == (10u | (2u << 16)));
    CHECK(words[1] == (10u | (4u << 16)));
    CHECK(words[7] == (13u | (16u << 16)));
    CHECK(words[8] == (14u | (14u << 16)));
    CHECK(words[14] == (17u | (2u << 16)));
    CHECK(words[15] == (17u | (1u << 16)));

    /* X scissoring to {13..17}: front row (13|0) and the two zero-pixel
     * bottom rows trim away -> 13 rows starting at y=13 */
    grSetRectangle(&st->renderScissor, 13, 17, 0, 64);
    CHECK(grScanConvertTriangle(st,
             (_grRasterPoint *)&vb[2].screenPosition[0],
             (_grRasterPoint *)&vb[0].screenPosition[0],
             (_grRasterPoint *)&vb[1].screenPosition[0], 8) == 13);
    CHECK(pp->yStart == 13);
    words = pp->scanlineInfo;
    CHECK(words[0] == (13u | (1u << 16)));
    CHECK(words[1] == (13u | (4u << 16)));
    CHECK(words[12] == (16u | (1u << 16)));

    /* Y clip: scissor y0=16 cuts 4 rows off the top half (the long and
     * short edges advance, the bottom half keeps its own rows); the last
     * zero-pixel row trims -> 11 rows from y=16 */
    grSetRectangle(&st->renderScissor, 0, 64, 16, 64);
    CHECK(grScanConvertTriangle(st,
             (_grRasterPoint *)&vb[2].screenPosition[0],
             (_grRasterPoint *)&vb[0].screenPosition[0],
             (_grRasterPoint *)&vb[1].screenPosition[0], 8) == 12);
    CHECK(pp->yStart == 16);
    words = pp->scanlineInfo;
    CHECK(words[0] == (12u | (10u << 16)));
    CHECK(words[3] == (13u | (16u << 16)));
    CHECK(words[10] == (17u | (2u << 16)));
    CHECK(words[11] == (17u | (1u << 16)));
    grSetRectangle(&st->renderScissor, 0, 64, 0, 64);

    /* A flat top above the scissor has no short top edge; clipping must
     * skip that unused half and still advance the bottom edges. */
    {
        _grRasterPoint flat[3] = {
            { 0, -2 * 4096 }, { 8 * 4096, -2 * 4096 }, { 4 * 4096, 2 * 4096 }
        };
        CHECK(grScanConvertTriangle(st, &flat[0], &flat[1], &flat[2], 8) == 2);
        CHECK(pp->yStart == 0);
        CHECK(pp->scanlineInfo[0] == (2u | (3u << 16)));
        CHECK(pp->scanlineInfo[1] == (3u | (1u << 16)));
    }

    {
        _grRasterPoint empty[][3] = {
            { { 0, 256 }, { 8 * 4096, 512 }, { 4 * 4096, 768 } },
            { { 0, -3 * 4096 }, { 8 * 4096, -4096 }, { 4 * 4096, 0 } },
            { { 0, 64 * 4096 }, { 8 * 4096, 65 * 4096 }, { 4 * 4096, 66 * 4096 } }
        };
        unsigned char saved[sizeof(*pp)];
        memcpy(saved, pp, sizeof(saved));
        for (i = 0; i < (int)(sizeof(empty) / sizeof(empty[0])); ++i)
        {
            CHECK(grScanConvertTriangle(st, &empty[i][0], &empty[i][1],
                                         &empty[i][2], 8) == 0);
            CHECK(memcmp(pp, saved, sizeof(saved)) == 0);
        }
    }

    /* end-to-end grDrawClippedTriangle through the y-sort: rows, row
     * pointers, gradient pre-advance and the mutable-parameter poison */
    st->pixelPipelineCode = noopPipeline;
    st->numActiveGradients = 0;
    st->activeGradientMask = 0;
    st->drawSurface.buffers[0].data = colorBuf;
    st->drawSurface.buffers[0].stride = 64 * 2;
    st->drawSurface.buffers[1].data = depthBuf;
    st->drawSurface.buffers[1].stride = 64 * 4;
    st->drawSurface.buffers[2].data = stencilBuf;
    st->drawSurface.buffers[2].stride = 64;
    for (i = 0; i < 3; ++i)
        vb[i].stateMask = 0x2C;   /* TRANSFORMED | VIEWPORT | COLOR_COMPUTED */
    tri.indices = grSetIndices(0, 1, 2);
    tri.area = 512;
    tri.side = 0;
    pp->mutableParams[0] = 123;
    pp->mutableParams[1] = 123;
    pp->mutableParams[32] = 123;
    pp->mutableParams[40] = 123;
    pp->mutableParams[42] = 123;
    grDrawClippedTriangle(st, &tri, grSetIndices(0, 1, 2), 8);
    CHECK(pp->yStart == 12);
    CHECK(pp->numScanlines == 16);
    CHECK(pp->mutableParams[1] == (HG_ASSERT_ENABLED ? -623195394 : 123));
    CHECK(pp->mutableParams[0] == 123);             /* mask bit 0 clear */
    CHECK(pp->mutableParams[32] == 123);            /* mask byte 4 == 0 */
    CHECK(pp->mutableParams[40] == 123);            /* byte 5 bit 0 clear */
    CHECK(pp->mutableParams[42] == (HG_ASSERT_ENABLED ? -623195394 : 123));
    CHECK(pp->colorRow == colorBuf + 128 * 12);
    CHECK(pp->depthRow == depthBuf + 256 * 12);
    CHECK(pp->stencilRow == stencilBuf + 64 * 12);

    /* dispatch: PERSPECTIVE_CORRECTION selects the real tessellator (the
     * zero-w test triangle subdivides down to the affine floor and draws
     * its pieces - the scan state ends on the LAST drawn piece), otherwise
     * the affine path runs */
    pp->numScanlines = -1;
    grSetCapability(st, GR_CAPABILITY_PERSPECTIVE_CORRECTION, 1);
    grrDrawClippedTriangle(st, &tri, grSetIndices(0, 1, 2));
    CHECK(pp->numScanlines > 0);
    grSetCapability(st, GR_CAPABILITY_PERSPECTIVE_CORRECTION, 0);
    grrDrawClippedTriangle(st, &tri, grSetIndices(0, 1, 2));
    CHECK(pp->numScanlines == 16);
    grSetCapability(st, GR_CAPABILITY_UNUSED, 1);
    CHECK(grTestCapability(st, GR_CAPABILITY_UNUSED) == 1);
    grSetCapability(st, GR_CAPABILITY_UNUSED, 0);

    st->renderStackAllocated = 0;
    st->pixelPipeline = NULL;
    st->pixelPipelineCode = NULL;
}

/* --- test_interpolate: grInterpolateVertexGradients ---------------------------
 * Weights a=0.5, b=0.25 (c=0.25) against three records with known colors,
 * fog, texcoords; then the trivial-copy cases and mask gating. */
static void test_interpolate(void)
{
    TEST_RENDER_STACK(stack);
    Gerbera_s stObj;
    Gerbera_s *st = &stObj;
    _grVertex *vb;
    _grVertex *target;
    int i;

    memset(st, 0, sizeof(*st));
    st->renderStack = stack;
    st->renderStackAllocated = 1;
    memset(stack, 0, sizeof(stackStorage.bytes));
    vb = (_grVertex *)(stack + 1332);
    memset(&vb[0], 0, 3 * sizeof(vb[0]));
    for (i = 0; i < 3; ++i)
    {
        vb[i].color.final[0][0] = (unsigned short)(100 + 300 * i);
        vb[i].color.final[0][1] = (unsigned short)(200 + 300 * i);
        vb[i].color.final[0][2] = (unsigned short)(300 + 300 * i);
        vb[i].color.final[1][0] = (unsigned short)(10 + 30 * i);
        vb[i].color.final[1][1] = (unsigned short)(20 + 30 * i);
        vb[i].color.final[1][2] = (unsigned short)(30 + 30 * i);
        vb[i].color.final[0][3] = (unsigned short)(111 + 111 * i);
        vb[i].color.final[1][3] = (unsigned short)(1000 + 1000 * i);
        vb[i].texCoord[0].x = 10 + 20 * i;
        vb[i].texCoord[0].y = 20 + 20 * i;
        vb[i].texCoord[1].x = 100 + 200 * i;
        vb[i].texCoord[1].y = 200 + 200 * i;
    }
    target = &vb[64];
    memset(target, 0, sizeof(*target));

    /* barycentric: g = (a*t0 + b*t1 + c*t2) >> 29, a=0.5 b=0.25 c=0.25 */
    target->normal[0] = 0x10000000;
    target->normal[1] = 0x08000000;
    target->stateMask = 0x4F;             /* INTERPOLATE|TRANSFORMED|VIEWPORT|
                                           * SURVIVED_2D|SURVIVED_3D */
    target->texCoord[0].x = -7;
    target->texCoord[0].y = -7;
    st->activeGradientMask = 0x3FF;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 0);
    CHECK(target->stateMask == 0x2F);     /* INTERPOLATE -> COLOR_COMPUTED */
    CHECK(target->color.final[0][0] == 325);
    CHECK(target->color.final[0][1] == 425);
    CHECK(target->color.final[0][2] == 525);
    CHECK(target->color.final[1][3] == 1750);          /* fog */
    CHECK(target->color.final[0][3] == 194);           /* alpha (floored) */
    CHECK(target->texCoord[0].x == 25);
    CHECK(target->texCoord[0].y == 35);
    CHECK(target->texCoord[1].x == 250);
    CHECK(target->texCoord[1].y == 350);

    /* side=1 reads the secondary color row; alpha stays on final[0][3] */
    memset(target, 0, sizeof(*target));
    target->normal[0] = 0x10000000;
    target->normal[1] = 0x08000000;
    target->stateMask = 0x4F;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 1);
    CHECK(target->color.final[1][0] == 32);
    CHECK(target->color.final[1][1] == 42);
    CHECK(target->color.final[1][2] == 52);
    CHECK(target->color.final[0][3] == 194);

    /* mask gating: with no gradient bits set nothing is touched, but the
     * stateMask flip still happens */
    memset(target, 0, sizeof(*target));
    target->normal[0] = 0x10000000;
    target->normal[1] = 0x08000000;
    target->stateMask = 0x4F;
    target->color.final[0][0] = 0x5A5A;
    target->texCoord[1].y = -3;
    st->activeGradientMask = 0;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 0);
    CHECK(target->stateMask == 0x2F);
    CHECK(target->color.final[0][0] == 0x5A5A);
    CHECK(target->texCoord[1].y == -3);

    /* trivial weights copy the winning record wholesale (b=1 -> record 1) */
    st->activeGradientMask = 0x3FF;
    memset(target, 0, sizeof(*target));
    target->normal[0] = 0;
    target->normal[1] = 0x20000000;
    target->stateMask = 0x4F;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 0);
    CHECK(target->color.final[0][0] == 400);
    CHECK(target->color.final[1][3] == 2000);
    CHECK(target->texCoord[1].y == 400);

    /* a==b==0 -> record 2 */
    memset(target, 0, sizeof(*target));
    target->normal[0] = 0;
    target->normal[1] = 0;
    target->stateMask = 0x4F;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 0);
    CHECK(target->color.final[0][0] == 700);
    CHECK(target->texCoord[0].x == 50);

    /* a==1 -> record 0 */
    memset(target, 0, sizeof(*target));
    target->normal[0] = 0x20000000;
    target->normal[1] = 0;
    target->stateMask = 0x4F;
    grInterpolateVertexGradients(st, target, grSetIndices(0, 1, 2), 0);
    CHECK(target->color.final[0][0] == 100);
    CHECK(target->texCoord[1].x == 100);

    st->renderStackAllocated = 0;
}

/* Count rasterized fragments to catch cracks, overlaps, and skewed line ends. */
static void lineCoveragePipeline(void *pipe)
{
    _grPixelPipe *pp = (_grPixelPipe *)pipe;
    unsigned short *row = (unsigned short *)pp->colorRow;
    int y;
    for (y = 0; y < pp->numScanlines; ++y, row += 64)
    {
        unsigned int span = pp->scanlineInfo[y];
        unsigned int x = span & 0xFFFFu;
        unsigned int end = x + (span >> 16);
        for (; x < end; ++x)
            ++row[x];
    }
}

/* --- test_linepoint: grDrawClippedLine + grDrawClippedPoint -------------------
 * Original line quads use width only on the minor axis, and a half-pixel
 * offset in the direction of the major axis. Point:
 * (20,20) px at size 3px -> odd snap, corners on the [18,21] box. */
static void test_linepoint(void)
{
    TEST_RENDER_STACK(stack);
    static unsigned short colorBuf[64 * 64];
    static unsigned char depthBuf[64 * 64 * 4];
    static unsigned char stencilBuf[64 * 64];
    Gerbera_s stObj;
    Gerbera_s *st = &stObj;
    _grPixelPipe *pp;
    _grVertex *vb;
    _grClipTriangle tri;
    unsigned int *words;

    memset(st, 0, sizeof(*st));
    st->renderStack = stack;
    st->renderStackAllocated = 1;
    st->pixelPipeline = (_grPixelPipe *)(stack + TEST_PIXEL_PIPE_OFFSET);
    st->pixelPipelineCode = noopPipeline;
    pp = st->pixelPipeline;
    vb = (_grVertex *)(stack + 1332);
    memset(stack, 0xCC, sizeof(stackStorage.bytes));
    memset(&vb[0], 0, 3 * sizeof(vb[0]));
    vb[0].color.final[0][0] = 11;
    vb[1].color.final[0][0] = 22;
    vb[2].color.final[0][0] = 33;
    vb[2].texCoord[0].x = 77;
    st->drawSurface.buffers[0].data = colorBuf;
    st->drawSurface.buffers[0].stride = 64 * 2;
    st->drawSurface.buffers[1].data = depthBuf;
    st->drawSurface.buffers[1].stride = 64 * 4;
    st->drawSurface.buffers[2].data = stencilBuf;
    st->drawSurface.buffers[2].stride = 64;
    st->lineWidth = 0x20000;              /* 2.0 px -> hw = 1px */
    st->pointSize = 0x30000;              /* 3.0 px */
    st->pointSizeMin = 0x10000;
    st->pointSizeMax = 0x400000;
    st->numActiveGradients = 0;
    st->activeGradientMask = 0;
    grSetRectangle(&st->renderScissor, 0, 64, 0, 64);

    /* horizontal line: corners are 256-aligned 20.12 positions */
    vb[0].screenPosition[0] = 10 << 12;
    vb[0].screenPosition[1] = 15 << 12;
    vb[1].screenPosition[0] = 30 << 12;
    vb[1].screenPosition[1] = 15 << 12;
    tri.indices = grSetIndices(0, 1, 1);
    tri.area = 0;
    tri.side = 0;
    grDrawClippedLine(st, &tri, grSetIndices(0, 1, 1));
    CHECK(vb[64].screenPosition[0] == 10 * 4096 + 2048);
    CHECK(vb[64].screenPosition[1] == 14 << 12);
    CHECK(vb[65].screenPosition[0] == 10 * 4096 + 2048);  /* 10.5 px */
    CHECK(vb[65].screenPosition[1] == 16 << 12);
    CHECK(vb[66].screenPosition[0] == 30 * 4096 + 2048);  /* 30.5 px */
    CHECK(vb[66].screenPosition[1] == 16 << 12);
    CHECK(vb[67].screenPosition[0] == 30 * 4096 + 2048);
    CHECK(vb[67].screenPosition[1] == 14 << 12);
    CHECK(vb[64].stateMask == 0x2F);   /* 79 with INTERPOLATE cleared */
    CHECK(vb[64].color.final[0][0] == 11);   /* weights (1,0) -> orig rec 0 */
    CHECK(vb[66].color.final[0][0] == 22);   /* weights (0,1) -> orig rec 1 */
    CHECK(pp->yStart == 14);                 /* second triangle's scan */
    CHECK(pp->numScanlines == 1);
    words = pp->scanlineInfo;
    CHECK(words[0] == (20u | (10u << 16)));

    /* vertical line: steep path, coverage [19,21] x [10.5,30.5] */
    vb[0].screenPosition[0] = 20 << 12;
    vb[0].screenPosition[1] = 10 << 12;
    vb[1].screenPosition[0] = 20 << 12;
    vb[1].screenPosition[1] = 30 << 12;
    grDrawClippedLine(st, &tri, grSetIndices(0, 1, 1));
    CHECK(vb[64].screenPosition[0] == 21 << 12);
    CHECK(vb[64].screenPosition[1] == 10 * 4096 + 2048);
    CHECK(vb[65].screenPosition[0] == 19 << 12);
    CHECK(vb[65].screenPosition[1] == 10 * 4096 + 2048);
    CHECK(vb[66].screenPosition[0] == 19 << 12);
    CHECK(vb[66].screenPosition[1] == 30 * 4096 + 2048);
    CHECK(vb[67].screenPosition[0] == 21 << 12);
    CHECK(vb[67].screenPosition[1] == 30 * 4096 + 2048);
    /* The second triangle starts in the right column, then spans both. */
    CHECK(pp->yStart == 10);
    CHECK(pp->numScanlines == 20);
    words = pp->scanlineInfo;
    CHECK(words[0] == (20u | (1u << 16)));
    CHECK(words[9] == (20u | (1u << 16)));
    CHECK(words[10] == (19u | (2u << 16)));
    CHECK(words[19] == (19u | (2u << 16)));

    /* Check both triangles together, including the half-open endpoint rule
     * in each direction. Every covered pixel must be emitted exactly once. */
    {
        int vertical, reverse, x, y;
        st->pixelPipelineCode = lineCoveragePipeline;
        for (vertical = 0; vertical < 2; ++vertical)
        {
            for (reverse = 0; reverse < 2; ++reverse)
            {
                int start = reverse ? 30 : 10;
                int end = reverse ? 10 : 30;
                int lo = reverse ? 9 : 10;
                memset(colorBuf, 0, sizeof(colorBuf));
                vb[0].screenPosition[0] = (vertical ? 20 : start) * 4096;
                vb[0].screenPosition[1] = (vertical ? start : 15) * 4096;
                vb[1].screenPosition[0] = (vertical ? 20 : end) * 4096;
                vb[1].screenPosition[1] = (vertical ? end : 15) * 4096;
                grDrawClippedLine(st, &tri, tri.indices);
                for (y = 0; y < 64; ++y)
                    for (x = 0; x < 64; ++x)
                    {
                        int covered = vertical
                            ? (x >= 19 && x < 21 && y >= lo && y < lo + 20)
                            : (x >= lo && x < lo + 20 && y >= 14 && y < 16);
                        CHECK(colorBuf[y * 64 + x] == covered);
                    }
            }
        }
        /* Sloped quads in all eight octants retain the two-pixel width
         * without cracks or overlap along their shared triangle edge. */
        for (vertical = 0; vertical < 2; ++vertical)
        {
            int sx, sy;
            for (sx = -1; sx <= 1; sx += 2)
                for (sy = -1; sy <= 1; sy += 2)
                {
                    unsigned int covered = 0;
                    memset(colorBuf, 0, sizeof(colorBuf));
                    vb[0].screenPosition[0] = 24 * 4096;
                    vb[0].screenPosition[1] = 24 * 4096;
                    vb[1].screenPosition[0] = (24 + sx * (vertical ? 10 : 20)) * 4096;
                    vb[1].screenPosition[1] = (24 + sy * (vertical ? 20 : 10)) * 4096;
                    grDrawClippedLine(st, &tri, tri.indices);
                    for (y = 0; y < 64; ++y)
                        for (x = 0; x < 64; ++x)
                        {
                            covered += colorBuf[y * 64 + x];
                            CHECK(colorBuf[y * 64 + x] <= 1);
                        }
                    CHECK(covered == 40);
                    CHECK(vb[64].screenPosition[vertical] == vb[65].screenPosition[vertical]);
                    CHECK(vb[66].screenPosition[vertical] == vb[67].screenPosition[vertical]);
                }
        }
        st->pixelPipelineCode = noopPipeline;
    }

    /* zero-length line draws nothing */
    vb[0].screenPosition[0] = 12 << 12;
    vb[0].screenPosition[1] = 12 << 12;
    vb[1].screenPosition[0] = 12 << 12;
    vb[1].screenPosition[1] = 12 << 12;
    pp->numScanlines = -7;
    grDrawClippedLine(st, &tri, grSetIndices(0, 1, 1));
    CHECK(pp->numScanlines == -7);

    /* point at (20,20) px, size 3px: odd size -> half-pixel snap keeps the
     * center at 19.5; corners on the [18,21] box */
    vb[2].screenPosition[0] = 20 << 12;
    vb[2].screenPosition[1] = 20 << 12;
    vb[2].screenPosition[2] = 5 << 12;
    tri.indices = grSetIndices(2, 2, 2);
    grDrawClippedPoint(st, &tri, grSetIndices(2, 2, 2));
    CHECK(vb[64].screenPosition[0] == 18 << 12);
    CHECK(vb[64].screenPosition[1] == 18 << 12);
    CHECK(vb[64].screenPosition[2] == 5 << 12);
    CHECK(vb[65].screenPosition[0] == 18 << 12);
    CHECK(vb[65].screenPosition[1] == 21 << 12);
    CHECK(vb[66].screenPosition[0] == 21 << 12);
    CHECK(vb[66].screenPosition[1] == 21 << 12);
    CHECK(vb[67].screenPosition[0] == 21 << 12);
    CHECK(vb[67].screenPosition[1] == 18 << 12);
    CHECK(vb[64].color.final[0][0] == 33);   /* weights (0,0) -> rec 2 */
    CHECK(vb[64].texCoord[0].x == 77);
    CHECK(pp->yStart == 18);
    CHECK(pp->numScanlines > 0);

    /* even size 2px: no snap shift, [19,21] box */
    st->pointSize = 0x20000;
    grDrawClippedPoint(st, &tri, grSetIndices(2, 2, 2));
    CHECK(vb[64].screenPosition[0] == 19 << 12);
    CHECK(vb[64].screenPosition[1] == 19 << 12);
    CHECK(vb[66].screenPosition[1] == 21 << 12);

    /* point sprite texcoords replace the interpolated ones per unit */
    grSetCapability(st, GR_CAPABILITY_POINT_SPRITE, 1);
    st->textureUnitEnabled[0] = 1;
    st->textureUnitEnabled[1] = 0;
    grDrawClippedPoint(st, &tri, grSetIndices(2, 2, 2));
    CHECK(vb[64].texCoord[0].x == 0);
    CHECK(vb[64].texCoord[0].y == 0);
    CHECK(vb[65].texCoord[0].x == 0);
    CHECK(vb[65].texCoord[0].y == 2097151);
    CHECK(vb[66].texCoord[0].x == 2097151);
    CHECK(vb[66].texCoord[0].y == 2097151);
    CHECK(vb[67].texCoord[0].x == 2097151);
    CHECK(vb[67].texCoord[0].y == 0);
    grSetCapability(st, GR_CAPABILITY_POINT_SPRITE, 0);
    st->textureUnitEnabled[0] = 0;

    st->renderStackAllocated = 0;
    st->pixelPipeline = NULL;
    st->pixelPipelineCode = NULL;
}

/* --- grFracDiv/grLerp/grClassifyTriangle/grSplitEdge and tessellation ------
 * grFracDiv(1, 2) is one unit below ONE/2 in the fixed-point domain. */
static void test_tessellate(void)
{
    TEST_RENDER_STACK(stack);
    Gerbera_s stObj;
    Gerbera_s *st = &stObj;
    _grPixelPipe *pp;
    _grVertex *vb;
    _grClipTriangle tri;
    unsigned int *words;
    int i, splitVerts;

#define ONE 0x20000000
    memset(st, 0, sizeof(*st));
    st->renderStack = stack;
    st->renderStackAllocated = 1;
    st->pixelPipeline = (_grPixelPipe *)(stack + TEST_PIXEL_PIPE_OFFSET);
    st->pixelPipelineCode = noopPipeline;
    pp = st->pixelPipeline;
    vb = (_grVertex *)(stack + 1332);
    grSetRectangle(&st->renderScissor, 0, 64, 0, 64);
    st->activeGradientMask = 0;
    st->numActiveGradients = 0;

    CHECK(grFracDiv(1, 2) == 268435455);        /* ONE/2 - 1 (floor) */
    CHECK(grFracDiv(1, 4) == 134217727);        /* ONE/4 - 1 */
    CHECK(grFracDiv(0, 5) == 0);
    CHECK(grLerp(100, 200, ONE / 2) == 150);
    CHECK(grLerp(10, 20, 0) == 10);
    CHECK(grLerp(0, ONE, ONE) == ONE);

    /* classification */
    memset(stack, 0, sizeof(stackStorage.bytes));
    vb[0].screenPosition[0] = 10 << 12; vb[0].screenPosition[1] = 10 << 12;
    vb[1].screenPosition[0] = 12 << 12; vb[1].screenPosition[1] = 11 << 12;
    vb[2].screenPosition[0] = 11 << 12; vb[2].screenPosition[1] = 13 << 12;
    CHECK(grClassifyTriangle(st, vb, grSetIndices(0, 1, 2)) ==
          GR_TRIANGLESTATUS_AFFINE);
    vb[0].screenPosition[0] = -10 * 4096; vb[0].screenPosition[1] = -10 * 4096;
    vb[1].screenPosition[0] = -30 * 4096; vb[1].screenPosition[1] = -20 * 4096;
    vb[2].screenPosition[0] = -20 * 4096; vb[2].screenPosition[1] = -30 * 4096;
    CHECK(grClassifyTriangle(st, vb, grSetIndices(0, 1, 2)) ==
          GR_TRIANGLESTATUS_DISCARD);
    vb[0].screenPosition[0] = 10 << 12; vb[0].screenPosition[1] = 10 << 12;
    vb[1].screenPosition[0] = 42 << 12; vb[1].screenPosition[1] = 10 << 12;
    vb[2].screenPosition[0] = 10 << 12; vb[2].screenPosition[1] = 42 << 12;
    vb[0].normal[2] = ONE; vb[1].normal[2] = ONE; vb[2].normal[2] = ONE;
    CHECK(grClassifyTriangle(st, vb, grSetIndices(0, 1, 2)) ==
          GR_TRIANGLESTATUS_AFFINE);
    vb[0].normal[2] = ONE; vb[1].normal[2] = 2 * ONE; vb[2].normal[2] = 2 * ONE;
    CHECK(grClassifyTriangle(st, vb, grSetIndices(0, 1, 2)) ==
          GR_TRIANGLESTATUS_NONAFFINE);

    /* grSplitEdge: w-variance edge splits with the harmonic w and the
     * perspective-biased clip factors; flat and tiny edges do not */
    memset(&vb[3], 0, sizeof(vb[3]));
    memset(&vb[4], 0, sizeof(vb[4]));
    vb[3].screenPosition[0] = 10 << 12;
    vb[3].screenPosition[1] = 10 << 12;
    vb[4].screenPosition[0] = 42 << 12;
    vb[4].screenPosition[1] = 10 << 12;
    vb[3].normal[0] = ONE; vb[3].normal[1] = 0; vb[3].normal[2] = ONE;
    vb[4].normal[0] = 0; vb[4].normal[1] = ONE; vb[4].normal[2] = 2 * ONE;
    CHECK(grSplitEdge(&vb[68], &vb[3], &vb[4], 0) == 1);
    CHECK(vb[68].screenPosition[0] == 106496);   /* 26 px, 256-aligned */
    CHECK(vb[68].screenPosition[1] == 40960);    /* 10 px */
    CHECK(vb[68].normal[2] == 715827883);        /* ~ 2/3 of 2*ONE */
    CHECK(vb[68].normal[0] == 357913941);        /* 2/3 */
    CHECK(vb[68].normal[1] == 178956971);        /* 1/3 */
    vb[4].normal[2] = ONE;
    CHECK(grSplitEdge(&vb[69], &vb[3], &vb[4], 0) == 0);
    CHECK(grSplitEdge(&vb[70], &vb[3], &vb[3], 0) == 0);
    CHECK(grSplitEdge(&vb[70], &vb[3], &vb[3], 1) == 0);  /* forced, unjustified */

    /* end-to-end: perspective triangle (w variance 1:2) subdivides; the
     * pieces' union scans; 6 split records created */
    memset(stack, 0xCC, sizeof(stackStorage.bytes));
    memset(&vb[0], 0, sizeof(vb[0]));
    memset(&vb[1], 0, sizeof(vb[1]));
    memset(&vb[2], 0, sizeof(vb[2]));
    vb[0].screenPosition[0] = 10 << 12; vb[0].screenPosition[1] = 10 << 12;
    vb[1].screenPosition[0] = 42 << 12; vb[1].screenPosition[1] = 10 << 12;
    vb[2].screenPosition[0] = 10 << 12; vb[2].screenPosition[1] = 42 << 12;
    vb[0].clipPosition.m[3] = ONE; vb[0].clipPosition.e = 0;
    vb[1].clipPosition.m[3] = 2 * ONE; vb[1].clipPosition.e = 0;
    vb[2].clipPosition.m[3] = 2 * ONE; vb[2].clipPosition.e = 0;
    vb[0].color.final[0][0] = 50; vb[1].color.final[0][0] = 100;
    vb[2].color.final[0][0] = 150;
    vb[0].stateMask = 0x2C; vb[1].stateMask = 0x2C; vb[2].stateMask = 0x2C;
    tri.indices = grSetIndices(0, 1, 2);
    tri.area = 0;
    tri.side = 0;
    grTriangleTessellator(st, &tri, grSetIndices(0, 1, 2));
    CHECK(vb[0].normal[2] == 268435456);         /* wNorm: ONE >> 1 */
    CHECK(vb[1].normal[2] == ONE);
    CHECK(vb[2].normal[2] == ONE);
    splitVerts = 0;
    for (i = 68; i < 92; ++i)
    {
        if (vb[i].index == -1)
        {
            ++splitVerts;
            CHECK((vb[i].screenPosition[0] & 0xFF) == 0);
            CHECK((vb[i].screenPosition[1] & 0xFF) == 0);
            CHECK(vb[i].stateMask == 0x2F);      /* 79 post-interpolate */
        }
    }
    CHECK(splitVerts == 6);
    CHECK(pp->yStart == 10);
    CHECK(pp->numScanlines == 7);                /* last drawn piece */
    words = pp->scanlineInfo;
    CHECK(words[0] == (26u | (7u << 16)));
    CHECK(words[1] == (26u | (6u << 16)));
    CHECK(words[4] == (26u | (3u << 16)));

    /* equal-w big triangle: affine at level 0, single draw, no split records */
    vb[1].clipPosition.m[3] = ONE;
    vb[2].clipPosition.m[3] = ONE;
    splitVerts = 0;
    for (i = 68; i < 92; ++i)
        vb[i].index = 0;
    grTriangleTessellator(st, &tri, grSetIndices(0, 1, 2));
    splitVerts = 0;
    for (i = 68; i < 92; ++i)
    {
        if (vb[i].index == -1)
            ++splitVerts;
    }
    CHECK(splitVerts == 0);
    CHECK(pp->yStart == 10);
    CHECK(pp->numScanlines == 31);

#undef ONE
    st->renderStackAllocated = 0;
    st->pixelPipeline = NULL;
    st->pixelPipelineCode = NULL;
}

/* --- test_pipecache: the compiled-pipeline program cache ----------------------
 * 4 entries, 256-byte arena. Exercises find-miss, insert (tail allocation
 * of the free block), find-hit (timeStamp bump), eviction (LRU), free-block
 * merging and defragmentation. */
static void test_pipecache(void)
{
    static unsigned char arena[256];
    static _grCacheEntry entries[4];
    _grPipelineCache cache;
    _grCacheSignature sigA, sigB;
    unsigned int mask;
    unsigned char *code;
    unsigned char *code2;

    memset(&sigA, 0, sizeof(sigA));
    memset(&sigB, 0, sizeof(sigB));
    sigA.bytes[0] = 0xAA;
    sigB.bytes[0] = 0xBB;

    grCacheInit(&cache, arena, entries, 256, 4);
    CHECK(cache.firstFree == entries);
    CHECK(cache.firstFree->dataSize == 256);
    CHECK(cache.firstUnused == &entries[1]);
    CHECK(entries[1].next == &entries[2]);
    CHECK(entries[2].next == &entries[3]);
    CHECK(entries[3].next == NULL);
    CHECK(grCacheGetFreeMemory(&cache) == 256);

    /* miss on the empty cache */
    CHECK(grCacheFind(&cache, &sigA, &mask) == NULL);

    /* insert A (64 bytes): allocated at the TAIL of the free block */
    code = grCacheInsert(&cache, &sigA, 0x35, arena, 64);
    CHECK(code == arena + 192);
    CHECK(cache.firstUsed == &entries[1]);
    CHECK(entries[1].dataOffset == 192);
    CHECK(entries[1].dataSize == 64);
    CHECK(entries[1].gradientMask == 0x35);
    CHECK(memcmp(entries[1].signature.bytes, sigA.bytes, 40) == 0);
    CHECK(cache.firstFree == &entries[0]);
    CHECK(entries[0].dataSize == 192);
    CHECK(grCacheGetFreeMemory(&cache) == 192);
    CHECK(memcmp(arena + 192, arena, 64) == 0);   /* data copied */

    /* hit refreshes the timestamp and returns the code */
    code = grCacheFind(&cache, &sigA, &mask);
    CHECK(code == arena + 192);
    CHECK(mask == 0x35);
    CHECK(entries[1].timeStamp == 2);

    /* insert B (64): second used entry, free block shrinks again */
    code2 = grCacheInsert(&cache, &sigB, 0x5A, arena, 64);
    CHECK(code2 == arena + 128);
    CHECK(cache.firstUsed == &entries[2]);
    CHECK(entries[2].dataOffset == 128);
    CHECK(grCacheGetFreeMemory(&cache) == 128);

    /* fill the arena: insert C (128 bytes) fits exactly */
    code2 = grCacheInsert(&cache, &sigB, 0x11, arena, 128);
    CHECK(code2 == arena);
    CHECK(grCacheGetFreeMemory(&cache) == 0);
    CHECK(cache.firstFree == NULL);

    /* Inserting into the full arena evicts the least-recently-used entry (A, stamp 2);
     * its freed block (at the arena tail) is reused as-is: tail allocation
     * of the exact-fit free block returns the same address */
    sigA.bytes[1] = 1;
    code = grCacheInsert(&cache, &sigA, 0x77, arena, 64);
    CHECK(code == arena + 192);
    CHECK(grCacheFind(&cache, &sigA, &mask) == arena + 192);
    CHECK(mask == 0x77);
    CHECK(grCacheFind(&cache, &sigB, &mask) != NULL);   /* B still resident */

    /* remove both B entries: the freed blocks merge with neighbours */
    grCacheRemove(&cache, &entries[2]);
    CHECK(grCacheGetFreeMemory(&cache) == 64);
    grCacheRemove(&cache, &entries[3]);
    CHECK(grCacheGetFreeMemory(&cache) == 192);
    CHECK(cache.firstFree != NULL);
    CHECK(cache.firstFree->dataSize == 192);
    CHECK(cache.firstFree->next == NULL);        /* fully merged */

    /* defragment: single free block -> no-op loop */
    grCacheDefragment(&cache);
    CHECK(grCacheGetFreeMemory(&cache) == 192);

    /* oversized insert fails cleanly */
    CHECK(grCacheInsert(&cache, &sigA, 1, arena, 512) == NULL);
    grCacheInit(&cache, NULL, entries, 0, 0);
    CHECK(grCacheInsert(&cache, &sigA, 1, arena, 4) == NULL);

    /* Both binaries use unsigned age ordering, even after full wrap. */
    {
        static const unsigned int stamps[] = {0x7FFFFFFCu, 0xFFFFFFFCu};
        _grCacheSignature signatures[3];
        unsigned char replacement[128];
        int boundary, i;
        memset(signatures, 0, sizeof(signatures));
        for (i = 0; i < 3; ++i)
            signatures[i].bytes[0] = (unsigned char)i;
        for (boundary = 0; boundary < 2; ++boundary)
        {
            grCacheInit(&cache, arena, entries, sizeof(arena), 4);
            cache.timeStamp = stamps[boundary];
            for (i = 0; i < 3; ++i)
                CHECK(grCacheInsert(&cache, &signatures[i], i, arena, 32) != NULL);
            CHECK(grCacheFind(&cache, &signatures[0], &mask) == arena + 224);
            CHECK(cache.timeStamp == stamps[boundary] + 4u);
            CHECK(grCacheGetOldestUsed(&cache) == &entries[boundary ? 1 : 2]);
            grCacheSetArena(&cache, replacement, sizeof(replacement));
            CHECK(cache.arena == replacement && cache.entries == entries);
            CHECK(cache.entryCount == 4 && cache.timeStamp == 0);
            CHECK(grCacheGetFreeMemory(&cache) == sizeof(replacement));
            CHECK(grCacheFind(&cache, &signatures[0], &mask) == NULL);
        }
    }
}

/* --- test_signature: signature machinery (grEvaluatePixelPipeline, ------------
 * grCompressSignature, grIsGradientUsed, grEnsureValidPixelPipeline,
 * grCheckPixelPipeConsistency). Expected packed values follow the original
 * 32-bit DLL signature layout and bit-count tables. */
static void test_signature(void);

static void test_signature(void)
{
    /* grIsGradientUsed: bit (48+g) of the HC parameter mask */
    {
        unsigned char pm[16];

        memset(pm, 0, sizeof(pm));
        pm[6] = 0x01;                       /* bit 48 -> gradient 0 */
        pm[7] = 0x10;                       /* bit 60 -> gradient 12 */
        CHECK(grIsGradientUsed(pm, 0) == 1);
        CHECK(grIsGradientUsed(pm, 1) == 0);
        CHECK(grIsGradientUsed(pm, 12) == 1);
        CHECK(grIsGradientUsed(pm, 11) == 0);
    }

    /* grCompressSignature: bytes built from the DLL's bit-count tables
     * pack MSB-first into exactly 10 words (314 significant bits) */
    {
        _grSignature sig;
        _grCacheSignature cs;
        unsigned char *p = (unsigned char *)&sig;
        static const unsigned char mainCounts[56] =
        {
            5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 0, 0, 0,
            1, 4, 1, 1, 3, 1, 3, 1, 1, 1, 4, 1, 4, 4, 1, 1,
            1, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 0, 0, 0, 0, 0, 0, 0
        };
        static const unsigned char texCounts[44] =
        {
            5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 0, 0,
            1, 1, 3, 1, 1, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 3, 3, 2, 2, 1, 1, 0, 0
        };
        static const unsigned int want[10] =
        {
            0x5C2BAF49u, 0xE315785Cu, 0x569F2AAFu, 0x2AA9F913u,
            0x582D7C39u, 0x91458D8Du, 0x8CC34307u, 0x74E83D62u,
            0xAE551636u, 0x006C6F1Au
        };
        int i;

        memset(&sig, 0, sizeof(sig));
        for (i = 0; i < 56; ++i)
            p[i] = (unsigned char)((i * 37 + 11) & ((1u << mainCounts[i]) - 1));
        p += 56;
        for (i = 0; i < 44; ++i)
            p[i] = (unsigned char)((i * 53 + 7) & ((1u << texCounts[i]) - 1));
        p += 44;
        for (i = 0; i < 44; ++i)
            p[i] = (unsigned char)((i * 29 + 3) & ((1u << texCounts[i]) - 1));
        memset(&cs, 0xAB, sizeof(cs));
        grCompressSignature(&cs, &sig);
        for (i = 0; i < 10; ++i)
        {
            unsigned int w;
            memcpy(&w, cs.bytes + 4 * i, 4);
            CHECK(w == want[i]);
        }
    }

    /* grEvaluatePixelPipeline: RGBA8888 surface, blend + depth + stencil +
     * alpha test + fog + depth offset, two bound texture units (unit 0:
     * RGBA/modulate-style env 1, unit 1: RGB/env 5 combine) */
    {
        Gerbera_s st;
        _grTexture tex[2];
        _grSignature sig;
        _grCacheSignature cs;
        int i;

        memset(&st, 0, sizeof(st));
        st.drawSurface.rgbaOffsets[0] = 16;
        st.drawSurface.rgbaOffsets[1] = 8;
        st.drawSurface.rgbaOffsets[2] = 0;
        st.drawSurface.rgbaOffsets[3] = 24;
        st.drawSurface.rgbaSizes[0] = 8;
        st.drawSurface.rgbaSizes[1] = 8;
        st.drawSurface.rgbaSizes[2] = 8;
        st.drawSurface.rgbaSizes[3] = 8;
        st.drawSurface.colorMask = 0xF;
        st.drawSurface.colorBytes = 4;
        st.drawSurface.pixelFormat = 4;
        st.drawSurface.buffers[0].data = (void *)0x1000;
        st.drawSurface.buffers[1].data = (void *)0x2000;
        st.drawSurface.buffers[2].data = (void *)0x3000;
        st.colorMask = 0xF;
        st.useDithering = 1;
        st.depthFunc = 5;
        st.depthMask = 1;
        st.viewportScale.normal[2] = 1;
        st.srcBlendFactor = 2;
        st.dstBlendFactor = 3;
        st.fogColor = 0x8040C0FFu;
        st.alphaFunc = 4;
        st.stencilFunc = 5;
        st.stencilFail = 1;
        st.stencilZFail = 2;
        st.stencilZPass = 3;
        st.stencilTestMask = 0xFF;
        st.stencilWriteMask = 0xFF;
        st.screenRectGradients = 1;
        grEnableCapability(&st, GR_CAPABILITY_BLEND);
        grEnableCapability(&st, GR_CAPABILITY_DEPTH_TEST);
        grEnableCapability(&st, GR_CAPABILITY_STENCIL_TEST);
        grEnableCapability(&st, GR_CAPABILITY_ALPHA_TEST);
        grEnableCapability(&st, GR_CAPABILITY_FOG);
        grEnableCapability(&st, GR_CAPABILITY_DEPTH_OFFSET_LINE);
        grEnableCapability(&st, GR_CAPABILITY_SMOOTH_SHADING);
        for (i = 0; i < 2; ++i)
        {
            grInitTexture(&tex[i]);
            tex[i].mipmap[0].texFormat = (unsigned short)(i == 0 ? 2 : 1);
            tex[i].wrapMode[0] = 0;
            tex[i].wrapMode[1] = 1;
            tex[i].parmMask = 0x10;
            tex[i].filterMask[GR_FILTERTYPE_MIN] = 1;
            tex[i].filterMask[GR_FILTERTYPE_MAG] = 0;
            st.textureUnit[2 * i].texture = &tex[i];
            st.useTexUnit[i] = 1;
        }
        st.textureUnit[0].texEnvMode = 1;
        st.textureUnit[2].texEnvMode = 5;
        st.textureUnit[2].texEnvRGBCombFunc = 1;
        st.textureUnit[2].texEnvAlphaCombFunc = 2;
        st.textureUnit[2].texEnvRGBCombSrc[0] = 0;
        st.textureUnit[2].texEnvRGBCombSrc[1] = 1;
        st.textureUnit[2].texEnvRGBCombSrc[2] = 2;
        st.textureUnit[2].texEnvRGBCombOperand[0] = 0;
        st.textureUnit[2].texEnvRGBCombOperand[1] = 1;
        st.textureUnit[2].texEnvRGBCombOperand[2] = 2;
        st.textureUnit[2].texEnvAlphaCombSrc[0] = 1;
        st.textureUnit[2].texEnvAlphaCombSrc[1] = 2;
        st.textureUnit[2].texEnvAlphaCombSrc[2] = 0;
        st.textureUnit[2].texEnvAlphaCombOperand[0] = 2;
        st.textureUnit[2].texEnvAlphaCombOperand[1] = 1;
        st.textureUnit[2].texEnvAlphaCombOperand[2] = 0;
        st.textureUnit[2].texEnvRGBCombScale = 8;
        st.textureUnit[2].texEnvAlphaCombScale = 9;

        grEvaluatePixelPipeline(&st, &sig);
        CHECK(sig.isValid == 1);
        CHECK(sig.dither == 1);
        CHECK(sig.logicOp == 0);
        CHECK(sig.logicOpEnabled == 0);
        CHECK(sig.blendEnabled == 1);
        CHECK(sig.alphaFunc == 4);
        CHECK(sig.alphaTest == 1);
        CHECK(sig.depthFunc == 5);
        CHECK(sig.depthTest == 1);
        CHECK(sig.depthMask == 1);
        CHECK(sig.constantDepth == 0);
        CHECK(sig.colorMask == 0xF);
        CHECK(sig.writeColor == 1);
        CHECK(sig.srcFactor == 2);
        CHECK(sig.dstFactor == 3);
        CHECK(sig.fogEnabled == 1);
        CHECK(sig.monochromeFog == 0);
        CHECK(sig.depthClampMin == 0);
        CHECK(sig.depthClampMax == 0);
        CHECK(sig.stencilFunc == 5);
        CHECK(sig.stencilFail == 1);
        CHECK(sig.stencilZFail == 2);
        CHECK(sig.stencilZPass == 3);
        CHECK(sig.stencilTest == 1);
        CHECK(sig.stencilTestMaskFull == 1);
        CHECK(sig.stencilWriteMaskFull == 1);
        CHECK(sig.stencilAllOpsEqual == 0);
        CHECK(sig.stencilZOpsEqual == 0);
        CHECK(sig.flatShading == 0);
        CHECK(sig.constantWriteColor == 0);
        CHECK(sig.perPixelPack == 1);
        CHECK(sig.applyColorWriteMask == 0);
        CHECK(sig.screenRectGradients == 1);
        CHECK(st.packedFogColor == 0x8040C0FFu);
        /* the surface descriptor is copied verbatim (4 ints) */
        CHECK(memcmp(sig.descriptor.rgbaOffsets, st.drawSurface.rgbaOffsets,
                     4) == 0);
        CHECK(memcmp(sig.descriptor.rgbaSizes, st.drawSurface.rgbaSizes, 4) == 0);
        CHECK(sig.descriptor.colorMask == 0xF);
        CHECK(sig.descriptor.colorBytes == 4);
        CHECK(sig.descriptor.pixelFormat == 4);
        /* unit 0 record: RGBA descriptor + env-1 decode */
        CHECK(sig.texture[0][12] == 5);       /* pixelFormat RGBA */
        CHECK(sig.texture[0][16] == 1);       /* enabled */
        CHECK(sig.texture[0][17] == 1);       /* flags183 & 0x10 */
        CHECK(sig.texture[0][19] == 0);       /* flags207 & 1 */
        CHECK(sig.texture[0][20] == 1);       /* flags206 & 1 */
        CHECK(sig.texture[0][21] == 0);       /* coord format s */
        CHECK(sig.texture[0][22] == 1);       /* coord format t */
        CHECK(sig.texture[0][24] == 0);
        CHECK(sig.texture[0][25] == 3);
        CHECK(sig.texture[0][27] == 2);
        CHECK(sig.texture[0][28] == 3);
        CHECK(sig.texture[0][34] == 2);
        CHECK(sig.texture[0][36] == 4);
        CHECK(sig.texture[0][37] == 1);
        CHECK(sig.texture[0][40] == 1);
        CHECK(sig.texture[0][41] == 1);
        /* unit 1 record: RGB descriptor + env-5 combine decode */
        CHECK(sig.texture[1][8] == 7);        /* colorMask (RGB) */
        CHECK(sig.texture[1][12] == 4);       /* pixelFormat RGB */
        CHECK(sig.texture[1][16] == 1);
        CHECK(sig.texture[1][24] == 0);       /* rgbSource0 */
        CHECK(sig.texture[1][25] == 1);       /* alphaSource0 */
        CHECK(sig.texture[1][27] == 2);       /* alphaOp0 */
        CHECK(sig.texture[1][29] == 2);       /* alphaSource1 */
        CHECK(sig.texture[1][31] == 1);       /* alphaOp1 */
        CHECK(sig.texture[1][35] == 2);       /* alphaOp2 default */
        CHECK(sig.texture[1][36] == 1);       /* rgbFunction */
        CHECK(sig.texture[1][37] == 2);       /* alphaFunction */
        CHECK(sig.texture[1][38] == 3);       /* rgbScale shift */
        CHECK(sig.texture[1][39] == 3);       /* alphaScale shift */
        CHECK(sig.texture[1][40] == 1);
        CHECK(sig.texture[1][41] == 1);

        /* Expected 10-word compressed key for this TI=0x18AA signature. */
        grCompressSignature(&cs, &sig);
        {
            static const unsigned int want[10] =
            {
                0x82018888u, 0x8F804FF3u, 0x4048DE94u, 0xCA5C100Cu,
                0x44447C02u, 0x98446584u, 0x15087040u, 0x011100F0u,
                0x08611091u, 0x001022BFu
            };
            int k;

            for (k = 0; k < 10; ++k)
            {
                unsigned int w;
                memcpy(&w, cs.bytes + 4 * k, 4);
                CHECK(w == want[k]);
            }
        }

        /* grEnsureValidPixelPipeline: miss compiles the program via HC_compile
         * (unit 1 REPLACE(TEXTURE) DCE-eliminates primary RGB and unit 0 fetch,
         * leaving depth + fog + primaryA + s1 + t1 = 0x323, 5 gradients), and
         * at max scratch size (204800) with exhausted arena sets compilationFailed = 1 */
        {
            _grPipelineCache emptyCache;
            _grCacheEntry emptyEntries[8];
            unsigned char emptyArena[4096];
            static unsigned char compileScratch[204800];

            /* Restore legal GLES surface/texenv invariants after the raw bitpack check */
            st.drawSurface.depthBytes   = 4;
            st.drawSurface.stencilBytes = 1;
            st.textureUnit[2].texEnvAlphaCombOperand[1] = 3;
            st.textureUnit[2].texEnvAlphaCombOperand[2] = 2;
            st.setjmpConfigStorage.setjmpWrap = eglHCWrapSetjmp;
            st.setjmpConfigStorage.longjmp    = eglHCWrapLongjmp;
            st.setjmpConfigStorage.jmpbufSize = (unsigned int)eglGetJmpbufSize();
            st.setjmpConfig  = &st.setjmpConfigStorage;
            st.scratchMemory = compileScratch;
            st.scratchMemorySize = (int)sizeof(compileScratch);
            grStackInit(&st.scratchAllocator, compileScratch, (int)sizeof(compileScratch));

            grCacheInit(&emptyCache, emptyArena, emptyEntries, 4096, 8);
            st.pipelineCache = &emptyCache;
            grEnsureValidPixelPipeline(&st);
            CHECK(st.compilationFailed == 0);
            CHECK(st.pixelPipelineCode != NULL);
            CHECK(st.activeGradientMask == 0x323);
            CHECK(st.numActiveGradients == 5);

            /* Exhausted scratch at scratchSizes[6] (204800) -> compilationFailed = 1 */
            grCacheInit(&emptyCache, emptyArena, emptyEntries, 4096, 8);
            st.setjmpConfigStorage.jmpbufSize = 300000u;
            grEnsureValidPixelPipeline(&st);
            CHECK(st.compilationFailed == 1);
            st.setjmpConfigStorage.jmpbufSize = (unsigned int)eglGetJmpbufSize();
            st.drawSurface.depthBytes   = 0;
            st.drawSurface.stencilBytes = 0;
            st.textureUnit[2].texEnvAlphaCombOperand[1] = 1;
            st.textureUnit[2].texEnvAlphaCombOperand[2] = 0;
        }

        /* hit path: the pre-inserted program is published */
        {
            _grPipelineCache cache;
            _grCacheEntry entries[8];
            static unsigned char arena[16384];
            static unsigned char prog[16384];
            void *ins;

            memset(&st.pixelPipelineCode, 0, sizeof(st.pixelPipelineCode));
            st.compilationFailed = 0;
            grCacheInit(&cache, arena, entries, (int)sizeof(arena), 8);
            memset(prog, 0x77, sizeof(prog));
            ins = grCacheInsert(&cache, &cs, 0x1234u, prog,
                                (int)sizeof(_grPixelPipe));
            CHECK(ins != NULL);
            st.pipelineCache = &cache;
            grEnsureValidPixelPipeline(&st);
            CHECK(st.compilationFailed == 0);
            CHECK((void *)st.pixelPipelineCode == ins);
            CHECK(st.activeGradientMask == 0x1234);
            /* the index walk covers gradients 0..9 only (bit 12 ignored) */
            CHECK(st.numActiveGradients == 4);
            CHECK(st.activeGradientIndex[0] == 2);
            CHECK(st.activeGradientIndex[1] == 4);
            CHECK(st.activeGradientIndex[2] == 5);
            CHECK(st.activeGradientIndex[3] == 9);
            /* consistency: the cache still resolves to the same program */
            st.pixelPipelineDirty = 0;
            grCheckPixelPipeConsistency(&st);
            CHECK(st.compilationFailed == 0);
        }
    }
}

/* --- test_setuprendering: grSetupRendering / grCleanupRendering ------------ */
static void test_setuprendering(void)
{
    Gerbera_s st;
    _grPipelineCache cache;
    _grCacheEntry entries[8];
    static unsigned char arena[16384];
    static unsigned char prog[16384];
    static unsigned char scratch[65536];
    _grSignature sig;
    _grCacheSignature cs;
    _grPixelPipe *pipe;
    void *ins;

    memset(&st, 0, sizeof(st));

    /* draw surface: RGBA8888 with valid buffers */
    st.drawSurface.rgbaOffsets[0] = 0;
    st.drawSurface.rgbaOffsets[1] = 1;
    st.drawSurface.rgbaOffsets[2] = 2;
    st.drawSurface.rgbaOffsets[3] = 3;
    st.drawSurface.rgbaSizes[0] = 8;
    st.drawSurface.rgbaSizes[1] = 8;
    st.drawSurface.rgbaSizes[2] = 8;
    st.drawSurface.rgbaSizes[3] = 8;
    st.drawSurface.colorMask = 0x0F;
    st.drawSurface.colorBytes = 4;
    st.drawSurface.depthBytes = 2;
    st.drawSurface.stencilBytes = 1;
    st.drawSurface.pixelFormat = 5;  /* RGBA */
    st.drawSurface.width = 64;
    st.drawSurface.height = 64;
    st.drawSurface.buffers[0].data = (void *)0x1000;
    st.drawSurface.buffers[0].stride = 256;
    st.drawSurface.buffers[1].data = (void *)0x2000;
    st.drawSurface.buffers[1].stride = 128;
    st.drawSurface.buffers[2].data = (void *)0x3000;
    st.drawSurface.buffers[2].stride = 64;

    /* scissors: surface covers full draw surface, render inside it */
    st.surfaceScissor.x0 = 0;
    st.surfaceScissor.y0 = 0;
    st.surfaceScissor.x1 = 64;
    st.surfaceScissor.y1 = 64;
    st.renderScissor.x0 = 0;
    st.renderScissor.y0 = 0;
    st.renderScissor.x1 = 64;
    st.renderScissor.y1 = 64;

    /* rasterizer state */
    st.packedFogColor = 0x11223344;
    st.alphaReference = 0x55;
    st.stencilReference = 0xAA;
    st.stencilTestMask = 0xFF;
    st.stencilWriteMask = 0xBB;

    /* Scratch storage must hold the render stack and compiler allocations. */
    st.scratchAllocator.data = scratch;
    st.scratchAllocator.size = (int)sizeof(scratch);
    st.scratchAllocator.allocated = 0;
    st.scratchMemory = scratch;
    st.scratchMemorySize = (int)sizeof(scratch);

    /* evaluate + compress + insert so the cache hit path works */
    grEvaluatePixelPipeline(&st, &sig);
    grCompressSignature(&cs, &sig);
    grCacheInit(&cache, arena, entries, (int)sizeof(arena), 8);
    memset(prog, 0x77, sizeof(prog));
    ins = grCacheInsert(&cache, &cs, 0, prog, (int)sizeof(_grPixelPipe));
    CHECK(ins != NULL);
    st.pipelineCache = &cache;
    st.pixelPipelineCode = (grPixelPipelineCode_fn)ins;
    st.pixelPipelineDirty = 0;
    st.scissorDirty = 0;
    st.compilationFailed = 0;

    /* grSetupRendering: allocates the render stack and publishes the head */
    grSetupRendering(&st);
    CHECK(st.renderStackAllocated == 1);
    CHECK(st.renderStack != NULL);
    CHECK(st.vertexCache == st.renderStack);
    CHECK(st.pixelPipeline != NULL);

    /* pixel pipe head block: dither kernel + strides + refs */
    pipe = (_grPixelPipe *)st.pixelPipeline;
    CHECK(pipe->ditherKernel == (void *)g_orderedDitherKernel);
    CHECK(pipe->checked.colorBufferStride == 256);
    CHECK(pipe->checked.depthBufferStride == 128);
    CHECK(pipe->checked.stencilBufferStride == 64);
    CHECK(pipe->checked.fogColor == 0x11223344u);
    CHECK(pipe->checked.alphaReference == 0x55);
    CHECK(pipe->checked.stencilReference == 0xAAu);
    CHECK(pipe->checked.stencilReferenceMasked == (unsigned char)(0xAA & 0xFF));
    CHECK(pipe->checked.stencilTestMask == 0xFFu);
    CHECK(pipe->checked.stencilWriteMask == 0xBBu);

    /* grCleanupRendering: frees the render stack */
    grCleanupRendering(&st);
    CHECK(st.renderStackAllocated == 0);
    CHECK(st.renderStack == (void *)0xBAADF00D);
    CHECK(st.pixelPipeline == (void *)0xBAADF00D);
}

/* --- test_texturerect: grSetupTextureRectGradients ------------------------- */
static void test_texturerect(void)
{
    Gerbera_s st;
    _grTexture tex[2];
    static unsigned int dummyTexPixels[16 * 16];
    _grTRGradSetupInfo info;
    int txMin[2] = { 0, 100 };
    int tyMin[2] = { 0, 200 };
    int txMax[2] = { 1000, 1100 };
    int tyMax[2] = { 2000, 2200 };
    _grPixelPipe pipe;
    int u;

    memset(&st, 0, sizeof(st));
    memset(&pipe, 0, sizeof(pipe));
    st.pixelPipeline = &pipe;
    st.activeGradientMask = 0x3FF;  /* all gradients: depth, fog, colors, tex */
    st.fogMode = 0;
    st.fogAtZero = 0x1234;
    st.defaultColorRGBA[0] = 0x11;
    st.defaultColorRGBA[1] = 0x22;
    st.defaultColorRGBA[2] = 0x33;
    st.defaultColorRGBA[3] = 0x44;
    st.zNear = 0;
    st.zFar = 0x3F800000;  /* 1.0f in IScalar (float-as-bits) */
    st.primitiveType = 4;  /* TEXTURE_RECT */
    st.capabilities[GR_CAPABILITY_FOG] = 1;
    for (u = 0; u < 2; ++u)
    {
        grInitTexture(&tex[u]);
        grSetTextureTarget(&tex[u], GR_TEXTURETARGET_2D);
        grrReplaceTextureLevel(&tex[u], 0, dummyTexPixels, 16, 16, GR_TEXTUREFORMAT_RGBA8);
        grUpdateTexture(&tex[u]);
        st.textureUnit[2 * u].texture = &tex[u];
    }

    info.z = 0x3F000000;   /* 0.5f in IScalar */
    info.fx0 = 0;
    info.fy0 = 0;
    info.fx1 = 100;
    info.fy1 = 200;
    info.txMin = txMin;
    info.tyMin = tyMin;
    info.txMax = txMax;
    info.tyMax = tyMax;

    grSetupTextureRectGradients(&st, &info);

    /* texture gradients: unit 0 at indices 6/7, unit 1 at 8/9
     * pre-setup xAdder = 40960, shifted left by lsh = 11 - 0 = 11 for REPEAT */
    CHECK(pipe.xAdder[6] == (40960 << 11));
    CHECK(pipe.yAdder[6] == 0);
    CHECK(pipe.xAdder[7] == 0);
    CHECK(pipe.yAdder[7] == (40960 << 11));
    CHECK(pipe.xAdder[8] == (40960 << 11));
    CHECK(pipe.yAdder[9] == (40960 << 11));
    CHECK(pipe.textureUnit[0].pixels == dummyTexPixels);
    CHECK(pipe.textureUnit[0].sMask == 15);
    CHECK(pipe.textureUnit[0].tMask == 15);
    CHECK(pipe.textureUnit[0].rShift[0] == 28);
    CHECK(pipe.textureUnit[0].rShift[1] == 28);

    /* fog gradient: reference[1] = (fogAtZero & 0xFFFF) << 8 */
    CHECK(pipe.reference[1] == (0x1234 << 8));
    CHECK(pipe.xAdder[1] == 0);
    CHECK(pipe.yAdder[1] == 0);

    /* color gradients: reference[i+2] = defaultColorRGBA[i] << 8 */
    CHECK(pipe.reference[2] == (0x11 << 8));
    CHECK(pipe.reference[3] == (0x22 << 8));
    CHECK(pipe.reference[4] == (0x33 << 8));
    CHECK(pipe.reference[5] == (0x44 << 8));
    CHECK(pipe.xAdder[2] == 0);
    CHECK(pipe.yAdder[5] == 0);

    /* depth gradient: z = lerp(0.0f, 1.0f, 0.5f) = 0.5f;
     * IScalar_toFixedHO(0.5f, 30) = 0.5 * (2^30-1) = 0x1FFFFFE0 (float rounding) */
    CHECK(pipe.reference[0] == 0x1FFFFFE0);
    CHECK(pipe.xAdder[0] == 0);
    CHECK(pipe.yAdder[0] == 0);
}

/* --- test_renderrect: grRenderTextureRect ---------------------------------- */
static void test_renderrect(void)
{
    Gerbera_s state;
    _grPixelPipe pipe, before;
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0, sizeof(pipe));
    state.pixelPipeline = &pipe;
    state.compilationFailed = 1;
    memcpy(&before, &pipe, sizeof(pipe));

    /* Compilation failure skips rectangle setup and pixel dispatch. */
    grRenderTextureRect(&state, IScalar_immediate(10.0f), IScalar_immediate(10.0f),
                         IScalar_immediate(20.0f), IScalar_immediate(20.0f), 0);
    CHECK(pipe.numScanlines == 0);
    CHECK(memcmp(&pipe, &before, sizeof(pipe)) == 0);
}

/* --- test_hcopcode: HC opcode encoding ------------------------------------ */
static void test_hcopcode(void)
{
    HCInstr_s instr;

    /* genOp3: op=3 (ASR), dst=5, src0=7, src1=9 */
    instr = HC_genOp3(3, 5, 7, 9);
    CHECK(HC_getOpcodeOper(&instr) == 3);
    CHECK(HC_getOpcodeDst(&instr) == 5);
    CHECK(HC_getOpcodeSrc0(&instr) == 7);
    CHECK(HC_getOpcodeSrc1(&instr) == 9);
    CHECK(HC_opcodeHasImm(&instr) == 0);
    CHECK(HC_getOpcodeArgCount(&instr) == 3);

    /* genOp3Imm: op=3, dst=5, src0=7, imm=0x1234 */
    instr = HC_genOp3Imm(3, 5, 7, 0x1234);
    CHECK(HC_getOpcodeOper(&instr) == 3);
    CHECK(HC_getOpcodeDst(&instr) == 5);
    CHECK(HC_getOpcodeSrc0(&instr) == 7);
    CHECK(HC_getOpcodeImm(&instr) == 0x1234);
    CHECK(HC_opcodeHasImm(&instr) != 0);

    /* genOp2: op=14 (2-arg op), dst=3, src0=4 */
    instr = HC_genOp2(14, 3, 4);
    CHECK(HC_getOpcodeOper(&instr) == 14);
    CHECK(HC_getOpcodeDst(&instr) == 3);
    CHECK(HC_getOpcodeSrc0(&instr) == 4);
    CHECK(HC_getOpcodeArgCount(&instr) == 2);

    /* genOp0: op=16 (NOP, argc=0) */
    instr = HC_genOp0(16);
    CHECK(HC_getOpcodeOper(&instr) == 16);
    CHECK(HC_getOpcodeArgCount(&instr) == 0);

    /* opcode types: 0-22 ALUOP, 23-38 MEMOP, 39-54 BRANCH */
    CHECK(HC_getOpcodeTypeOp(3) == 0);   /* ASR */
    CHECK(HC_getOpcodeTypeOp(28) == 1);  /* LW2U */
    CHECK(HC_getOpcodeTypeOp(40) == 2);  /* BRA */

    /* store ops: 31-38 */
    CHECK(HC_isStoreOp(31) != 0);
    CHECK(HC_isStoreOp(38) != 0);
    CHECK(HC_isStoreOp(30) == 0);

    /* load ops: 23-30 */
    CHECK(HC_isLoadOp(23) != 0);
    CHECK(HC_isLoadOp(30) != 0);
    CHECK(HC_isLoadOp(31) == 0);

    /* branch ops: 40-52 */
    CHECK(HC_isBranchOp(40) != 0);
    CHECK(HC_isBranchOp(52) != 0);
    CHECK(HC_isBranchOp(39) == 0);

    /* setOpcodeDst */
    instr = HC_genOp3(3, 5, 7, 9);
    HC_setOpcodeDst(&instr, 10);
    CHECK(HC_getOpcodeDst(&instr) == 10);
    CHECK(HC_getOpcodeSrc0(&instr) == 7);
    CHECK(HC_getOpcodeSrc1(&instr) == 9);
}

/* --- test_interpreter: grExecuteProgram ------------------------------------ */
static void test_interpreter(void)
{
    _grSignature sig;
    HCInstr_s prog[4096];
    int count;
    int i;

    memset(&sig, 0, sizeof(sig));
    /* minimal signature: no texture, no blend, no depth/stencil */
    sig.isValid = 1;
    sig.descriptor.colorMask = 0x0F;
    sig.descriptor.colorBytes = 4;
    sig.descriptor.pixelFormat = 5;  /* RGBA */
    sig.writeColor = 1;
    sig.perPixelPack = 1;

    count = grExecuteProgram(prog, &sig, 4096);
    CHECK(count > 0);
    CHECK(count < 4096);
    /* Expected program size for this TI=0x18AA signature. */
    CHECK(count == 173);
    /* last instruction must be EOP */
    CHECK(HC_getOpcodeOper(&prog[count - 1]) == HCOP_EOP);
    /* first instruction should not be EOP */
    CHECK(HC_getOpcodeOper(&prog[0]) != HCOP_EOP);
    /* all instructions have valid opcodes */
    for (i = 0; i < count; ++i)
        CHECK(HC_getOpcodeOper(&prog[i]) < HCOP_MAX);
}

/* --- test_hcx86backend: hcMemory + hcBackendInfo + hcX86Backend ----------- */
#include "gerbera/hc/hcBackend.h"
#include "gerbera/hc/hcBackendInfo.h"
#include "gerbera/hc/hcMemory.h"

static void test_hcx86backend(void)
{
    static unsigned char arenaBuf[65536];
    HCArena_s arena;
    HCMemory_s lowMem, hiMem;
    HCBackend_s *be;
    unsigned char *remap;
    HCInstr_s prog[32];
    HCInstr_s *fixed = NULL;
    unsigned int freeMasks[32];
    unsigned short duZero[32];
    UDChainStub_s *udZero[32];
    HCBackendInfo_s beInfo;
    void *codePtr = NULL;
    unsigned char *code;
    int codeLen;
    int mark;
    int uses[4];
    int i;

    /* CodeView HCOP_e values and opcode helpers. */
    CHECK(HCOP_ADD == 0);
    CHECK(HCOP_MOV == 14);
    CHECK(HCOP_NOP == 16);
    CHECK(HCOP_EOP == 53);
    CHECK(HCOP_LABEL == 54);
    CHECK(HC_opcodeDecls[HCOP_EOP] == 0xD0);
    CHECK(HC_opcodeDecls[HCOP_LABEL] == 0xD1);
    CHECK(HC_getMemOpIndexSizeOp(HCOP_LW2S) == 2);
    CHECK(HC_getMemOpIndexSizeOp(HCOP_LD4) == 4);
    CHECK(HC_getMemOpIndexSizeOp(HCOP_SD4) == 4);
    CHECK(HC_getMemOpIndexSizeOp(HCOP_LD) == 1);
    CHECK(HC_bsf(1u) == 0);
    CHECK(HC_bsf(0x10u) == 4);
    CHECK(HC_bsf(0x80000000u) == 31);
    {
        HCInstr_s st = HC_genOp3(HCOP_SD, 5, 2, 7);
        CHECK(HC_getDef(&st) == -1);
        CHECK(HC_getUses(&st, uses) == 3);
        CHECK(uses[0] == 5 && uses[1] == 2 && uses[2] == 7);
        HC_setUse(&st, 0, 6);
        HC_setUse(&st, 2, 9);
        CHECK(HC_getOpcodeDst(&st) == 6);
        CHECK(HC_getOpcodeSrc1(&st) == 9);
    }

    /* 2. HCArena / HCMemory checks */
    HCArena_init(&arena, arenaBuf, (int)sizeof(arenaBuf));
    CHECK(arena.bottom == 0);
    CHECK(arena.top == (int)sizeof(arenaBuf));
    lowMem = HCArena_bottomInterface(&arena);
    hiMem = HCArena_topInterface(&arena);
    mark = HCMemory_mark(&lowMem);
    CHECK(mark == 0);
    {
        void *p1 = HCMemory_alloc(&lowMem, 5);
        void *p2 = HCMemory_alloc(&hiMem, 7);
        CHECK(p1 == arenaBuf);
        CHECK(arena.bottom == 8);
        CHECK(arena.top == (int)sizeof(arenaBuf) - 8);
        CHECK(HCArena_inLowMem(&arena, p1) != 0);
        CHECK(HCArena_inLowMem(&arena, p2) == 0);
    }
    HCMemory_release(&lowMem, mark);
    CHECK(arena.bottom == 0);
#if UINTPTR_MAX > 0xFFFFFFFFu
    {
        void **lowPtr;
        void **highPtr;
        unsigned int *packed;
        int packedStart;
        HCArena_init(&arena, arenaBuf, (int)sizeof(arenaBuf));
        HCArena_allocBottomAlign2(&arena, 2);
        lowPtr = (void **)HCArena_allocBottom(&arena, sizeof(void *));
        HCArena_allocTopAlign2(&arena, 2);
        highPtr = (void **)HCArena_allocTop(&arena, sizeof(void *));
        CHECK(((uintptr_t)lowPtr & (sizeof(void *) - 1u)) == 0);
        CHECK(((uintptr_t)highPtr & (sizeof(void *) - 1u)) == 0);
        *lowPtr = highPtr;
        *highPtr = lowPtr;
        CHECK(*lowPtr == highPtr && *highPtr == lowPtr);

        /* HC appends packed four-byte fields within an aligned block. */
        HCArena_allocBottomAlign2(&arena, 2);
        packed = (unsigned int *)HCArena_current(&arena);
        packedStart = arena.bottom;
        CHECK(((uintptr_t)packed & (sizeof(void *) - 1u)) == 0);
        HCArena_growCurrent(&arena, sizeof(unsigned int));
        HCArena_growCurrent(&arena, sizeof(unsigned int));
        CHECK(arena.bottom == packedStart + 2 * (int)sizeof(unsigned int));
        packed[0] = 0x12345678u;
        packed[1] = 0x9ABCDEF0u;
        CHECK(packed[0] == 0x12345678u && packed[1] == 0x9ABCDEF0u);
    }
#endif
    HCArena_init(&arena, arenaBuf, (int)sizeof(arenaBuf));

    /* 3. HCX86Backend vtable verification */
    be = HCX86Backend_create();
    CHECK(be != NULL);
    CHECK(be->canCreateImmediate != NULL && be->canCreateImmediate(NULL) == 1);
    CHECK(be->internalStackSpace != NULL && be->internalStackSpace() == 40);
    CHECK(be->internalStackSpaceAfterInvars == NULL);
    CHECK(be->targetRegisterMapping != NULL);
    remap = be->targetRegisterMapping();
    CHECK(remap[0] == 4 && remap[1] == 0 && remap[2] == 3 && remap[3] == 1);
    CHECK(remap[4] == 2 && remap[5] == 6 && remap[6] == 7 && remap[7] == 5);
    CHECK(be->numHardRegs != NULL && be->numHardRegs() == 8);
    CHECK(be->preRegallocFixer != NULL);
    CHECK(be->codegen != NULL);
    CHECK(be->postRegallocFixer != NULL);
    CHECK(be->emulate != NULL);
    CHECK(be->disasm == NULL);
    CHECK(be->isNative != NULL && be->isNative() == 1);
    CHECK(be->optimizationMask == NULL);

    /* 4. preRegallocFixer: var-shift != r3, var-shift == r3, BIC imm & reg */
    prog[0] = HC_genOp3(HCOP_LSL, 1, 2, 5);        /* expands to 4 instrs   */
    prog[1] = HC_genOp3(HCOP_LSR, 1, 2, 3);        /* src1==3: 1 instr      */
    prog[2] = HC_genOp3Imm(HCOP_BIC, 4, 2, 0x0F);  /* -> AND 4, 2, ~0x0F    */
    prog[3] = HC_genOp3(HCOP_BIC, 4, 2, 5);        /* -> MOVN tmp,5; AND    */
    prog[4] = HC_genOp0(HCOP_EOP);
    mark = be->preRegallocFixer(&arena, &fixed, prog, NULL);
    CHECK(fixed != NULL);
    /* maxReg of prog is max(1,2,5,3,4)+1 = 6 */
    CHECK(HC_getOpcodeOper(&fixed[0]) == HCOP_MOV && HC_getOpcodeDst(&fixed[0]) == 6 && HC_getOpcodeSrc0(&fixed[0]) == 3);
    CHECK(HC_getOpcodeOper(&fixed[1]) == HCOP_MOV && HC_getOpcodeDst(&fixed[1]) == 3 && HC_getOpcodeSrc0(&fixed[1]) == 5);
    CHECK(HC_getOpcodeOper(&fixed[2]) == HCOP_LSL && HC_getOpcodeSrc1(&fixed[2]) == 3);
    CHECK(HC_getOpcodeOper(&fixed[3]) == HCOP_MOV && HC_getOpcodeDst(&fixed[3]) == 3 && HC_getOpcodeSrc0(&fixed[3]) == 6);
    CHECK(HC_getOpcodeOper(&fixed[4]) == HCOP_LSR && HC_getOpcodeSrc1(&fixed[4]) == 3);
    CHECK(HC_getOpcodeOper(&fixed[5]) == HCOP_AND && HC_opcodeHasImm(&fixed[5]) != 0 && HC_getOpcodeImm(&fixed[5]) == ~0x0F);
    CHECK(HC_getOpcodeOper(&fixed[6]) == HCOP_MOVN && HC_getOpcodeDst(&fixed[6]) == 6 && HC_getOpcodeSrc0(&fixed[6]) == 5);
    CHECK(HC_getOpcodeOper(&fixed[7]) == HCOP_AND && HC_getOpcodeSrc1(&fixed[7]) == 6);
    CHECK(HC_getOpcodeOper(&fixed[8]) == HCOP_EOP);
    HCArena_releaseBottom(&arena, mark);

    /* 5. postRegallocFixer: SUB imm->ADD -imm, LSL 2 kept, non-comm dst==s1
     *    with & without free reg, SB from HC reg 5 (ESI) with spill */
    memset(duZero, 0, sizeof(duZero));
    memset(udZero, 0, sizeof(udZero));
    for (i = 0; i < 32; ++i)
        freeMasks[i] = 0xFF;
    freeMasks[2] = 0x00; /* force spill on prog[2] (non-comm SUB dst==s1)   */
    freeMasks[3] = 0x00; /* force spill on prog[3] (SB from non-byte reg 5) */
    prog[0] = HC_genOp3Imm(HCOP_SUB, 1, 2, 10);
    prog[1] = HC_genOp3Imm(HCOP_LSL, 1, 2, 2);
    prog[2] = HC_genOp3(HCOP_SUB, 1, 2, 1);        /* dst==s1, non-comm     */
    prog[3] = HC_genOp3Imm(HCOP_SB, 5, 2, 4);      /* reg 5 not in 0x1E     */
    prog[4] = HC_genOp0(HCOP_EOP);
    beInfo.instructions = prog;
    beInfo.freeRegMasks = freeMasks;
    beInfo.liveRegs = NULL;
    beInfo.chains.duChains = duZero;
    beInfo.chains.udChains = udZero;
    mark = be->postRegallocFixer(&arena, &fixed, prog, &beInfo);
    /* [0]: ADD 1, 2, -10 */
    CHECK(HC_getOpcodeOper(&fixed[0]) == HCOP_ADD && HC_getOpcodeImm(&fixed[0]) == -10);
    /* [1]: LSL 1, 2, 2 (kept 3-operand) */
    CHECK(HC_getOpcodeOper(&fixed[1]) == HCOP_LSL && HC_getOpcodeDst(&fixed[1]) == 1 && HC_getOpcodeSrc0(&fixed[1]) == 2);
    /* [2..6]: SD 3,0,8; MOV 3,2; SUB 3,3,1; MOV 1,3; LD 3,0,8 */
    CHECK(HC_getOpcodeOper(&fixed[2]) == HCOP_SD && HC_getOpcodeDst(&fixed[2]) == 3 && HC_getOpcodeImm(&fixed[2]) == 8);
    CHECK(HC_getOpcodeOper(&fixed[3]) == HCOP_MOV && HC_getOpcodeDst(&fixed[3]) == 3 && HC_getOpcodeSrc0(&fixed[3]) == 2);
    CHECK(HC_getOpcodeOper(&fixed[4]) == HCOP_SUB && HC_getOpcodeDst(&fixed[4]) == 3 && HC_getOpcodeSrc1(&fixed[4]) == 1);
    CHECK(HC_getOpcodeOper(&fixed[5]) == HCOP_MOV && HC_getOpcodeDst(&fixed[5]) == 1 && HC_getOpcodeSrc0(&fixed[5]) == 3);
    CHECK(HC_getOpcodeOper(&fixed[6]) == HCOP_LD && HC_getOpcodeDst(&fixed[6]) == 3 && HC_getOpcodeImm(&fixed[6]) == 8);
    /* [7..10]: SD 1,0,8; MOV 1,5; SB 1,2,4; LD 1,0,8 */
    CHECK(HC_getOpcodeOper(&fixed[7]) == HCOP_SD && HC_getOpcodeDst(&fixed[7]) == 1);
    CHECK(HC_getOpcodeOper(&fixed[8]) == HCOP_MOV && HC_getOpcodeDst(&fixed[8]) == 1 && HC_getOpcodeSrc0(&fixed[8]) == 5);
    CHECK(HC_getOpcodeOper(&fixed[9]) == HCOP_SB && HC_getOpcodeDst(&fixed[9]) == 1);
    CHECK(HC_getOpcodeOper(&fixed[10]) == HCOP_LD && HC_getOpcodeDst(&fixed[10]) == 1);
    CHECK(HC_getOpcodeOper(&fixed[11]) == HCOP_EOP);
    HCArena_releaseBottom(&arena, mark);

    /* 6. codegen: empty EOP -> exact 33-byte prologue + 29-byte epilogue */
    prog[0] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 40);
    code = (unsigned char *)codePtr;
    CHECK(codeLen == 62);
    /* mov eax, [esp+4]: 8B 44 24 04; mov [eax+4], esp: 89 60 04 */
    CHECK(code[0] == 0x8B && code[1] == 0x44 && code[2] == 0x24 && code[3] == 0x04);
    CHECK(code[4] == 0x89 && code[5] == 0x60 && code[6] == 0x04);
    /* mov esp, eax at [31..32]: 89 C4 */
    CHECK(code[31] == 0x89 && code[32] == 0xC4);
    /* epilogue starts at [33]: mov esp, [esp+4] = 8B 64 24 04; ends with C3 */
    CHECK(code[33] == 0x8B && code[34] == 0x64 && code[35] == 0x24 && code[36] == 0x04);
    CHECK(code[61] == 0xC3);
    HCArena_releaseBottom(&arena, mark);

    /* 7. codegen: ALU/LEA/MUL/ANSA/MAXS(Imm32Set) byte-exact sequence */
    prog[0] = HC_genOp2Imm(HCOP_MOV, 0, 0);            /* 31 C0 (2B)             */
    prog[1] = HC_genOp2Imm(HCOP_MOV, 1, 0x12345678);   /* C7 C1 78 56 34 12 (6B) */
    prog[2] = HC_genOp3(HCOP_ADD, 2, 1, 3);            /* 8D 54 0B 00 (4B)       */
    prog[3] = HC_genOp3Imm(HCOP_ADD, 0, 0, 0x1000);    /* 05 00 10 00 00 (5B)    */
    prog[4] = HC_genOp3(HCOP_SUB, 1, 1, 2);            /* 2B CA (2B)             */
    prog[5] = HC_genOp3Imm(HCOP_LSL, 3, 1, 2);         /* 8D 1C 8D 00..00 (7B)   */
    prog[6] = HC_genOp3(HCOP_MULS, 1, 1, 2);           /* 0F AF CA (3B)          */
    prog[7] = HC_genOp0(HCOP_ANSA);                    /* CD 03 (2B)             */
    prog[8] = HC_genOp3Imm(HCOP_MAXS, 1, 1, 42);       /* +11B pool + 12B body   */
    prog[9] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 40);
    code = (unsigned char *)codePtr;
    /* 33B prologue + 11B Imm32Set init + 43B body + 29B epilogue = 116B */
    CHECK(codeLen == 116);
    /* Imm32Set slot [esp+12] = 42 at [33..43]: C7 84 24 0C 00 00 00 2A 00 00 00 */
    CHECK(code[33] == 0xC7 && code[34] == 0x84 && code[35] == 0x24 && code[36] == 0x0C);
    CHECK(code[40] == 0x2A && code[41] == 0x00 && code[42] == 0x00 && code[43] == 0x00);
    {
        unsigned char *b = code + 44;
        CHECK(b[0] == 0x31 && b[1] == 0xC0);
        CHECK(b[2] == 0xC7 && b[3] == 0xC1 && b[4] == 0x78 && b[7] == 0x12);
        CHECK(b[8] == 0x8D && b[9] == 0x54 && b[10] == 0x0B && b[11] == 0x00);
        CHECK(b[12] == 0x05 && b[13] == 0x00 && b[14] == 0x10 && b[16] == 0x00);
        CHECK(b[17] == 0x2B && b[18] == 0xCA);
        CHECK(b[19] == 0x8D && b[20] == 0x1C && b[21] == 0x8D);
        CHECK(b[26] == 0x0F && b[27] == 0xAF && b[28] == 0xCA);
        CHECK(b[29] == 0xCD && b[30] == 0x03);
        /* MAXS: 3B 4C 24 0C | 7D 06 | C7 C1 2A 00 00 00 */
        CHECK(b[31] == 0x3B && b[32] == 0x4C && b[33] == 0x24 && b[34] == 0x0C);
        CHECK(b[35] == 0x7D && b[36] == 0x06);
        CHECK(b[37] == 0xC7 && b[38] == 0xC1 && b[39] == 0x2A);
    }
    HCArena_releaseBottom(&arena, mark);

    /* 8. codegen: EFLAGS CMP-elision + forward rel32 + backward rel8 branch */
    prog[0] = HC_genOp2Imm(HCOP_MOV, 0, 0);            /* 31 C0 (sets ZF/SF EAX) */
    prog[1] = HC_genOp3ImmRaw(HCOP_CBEQ, 0, 0, 0);     /* elides CMP! 0F 84 +2   */
    prog[2] = HC_genOp2(HCOP_MOV, 1, 2);               /* 89 D1 (2B)             */
    prog[3] = HC_genOp1Imm(HCOP_LABEL, 0);             /* body+10                */
    prog[4] = HC_genOp1Imm(HCOP_LABEL, 1);             /* body+10                */
    prog[5] = HC_genOp3Imm(HCOP_ADD, 1, 1, -1);        /* 83 C1 FF (sets ECX)    */
    prog[6] = HC_genOp3ImmRaw(HCOP_CBNEQ, 1, 1, 0);    /* elides CMP! 75 FB      */
    prog[7] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 40);
    code = (unsigned char *)codePtr;
    /* 33B prologue + 15B body + 29B epilogue = 77B */
    CHECK(codeLen == 77);
    {
        unsigned char *b = code + 33;
        CHECK(b[0] == 0x31 && b[1] == 0xC0);
        CHECK(b[2] == 0x0F && b[3] == 0x84 && b[4] == 0x02 && b[5] == 0x00 && b[6] == 0x00 && b[7] == 0x00);
        CHECK(b[8] == 0x89 && b[9] == 0xD1);
        CHECK(b[10] == 0x83 && b[11] == 0xC1 && b[12] == 0xFF);
        CHECK(b[13] == 0x75 && b[14] == 0xFB);
    }
    HCArena_releaseBottom(&arena, mark);

    /* 9. codegen: stack-load fusion (checkLoad) via synthetic DU/UD chains */
    {
        unsigned short duBuf[5] = { 1, 1, 2, 0, 0 }; /* chain at [0..1]: len=1, val=1 */
        unsigned short udLink[2] = { 1, 0 };         /* chain: len=1, defSite=0       */
        DUChain_s *perUse1[2];
        UDChainStub_s *udArr[3];
        HCBackendInfo_s fuseInfo;

        prog[0] = HC_genOp3Imm(HCOP_LD, 2, 4, 16);   /* LD EDX, [ESP+16] -> fused!    */
        prog[1] = HC_genOp3(HCOP_ADD, 1, 1, 2);      /* ADD ECX, [ESP+16]: 03 4C 24 10 */
        prog[2] = HC_genOp0(HCOP_EOP);

        perUse1[0] = NULL;
        perUse1[1] = (DUChain_s *)udLink;
        udArr[0] = NULL;
        udArr[1] = (UDChainStub_s *)perUse1;
        udArr[2] = NULL;

        fuseInfo.instructions = prog;
        fuseInfo.freeRegMasks = freeMasks;
        fuseInfo.liveRegs = NULL;
        fuseInfo.chains.duChains = &duBuf[2];
        fuseInfo.chains.udChains = udArr;

        mark = HCArena_markBottom(&arena);
        codeLen = be->codegen(&arena, &codePtr, prog, &fuseInfo, 40);
        code = (unsigned char *)codePtr;
        /* 33B prologue + 4B fused addMem + 29B epilogue = 66B */
        CHECK(codeLen == 66);
        CHECK(code[33] == 0x03 && code[34] == 0x4C && code[35] == 0x24 && code[36] == 0x10);
        HCArena_releaseBottom(&arena, mark);
    }
}

/* --- test_hcarmbackend: 32-bit ARM backend (hcARMBackend.c) --------------- */

static void armEmulateProbeCallback(void *stackPtr)
{
    *((int *)stackPtr) = 0x5A5AA5A5;
}

static void test_hcarmbackend(void)
{
    static unsigned char arenaBuf[65536];
    HCArena_s arena;
    HCBackend_s *be;
    unsigned char *remap;
    HCInstr_s prog[32];
    HCInstr_s *fixed = NULL;
    unsigned int freeMasks[32];
    unsigned short duZero[32];
    UDChainStub_s *udZero[32];
    HCBackendInfo_s beInfo;
    void *codePtr = NULL;
    unsigned int *words;
    int codeLen;
    int mark;
    int probeSlot = 0;

    HCArena_init(&arena, arenaBuf, (int)sizeof(arenaBuf));

    /* 1. vtable & singleton properties */
    be = HCARMBackend_create();
    CHECK(be != NULL);
    CHECK(be == HCARMBackend_create());
    CHECK(be->internalStackSpace() == 60);
    CHECK(be->numHardRegs() == 14);
    CHECK(be->isNative() == 1);
    CHECK(be->internalStackSpaceAfterInvars == NULL);
    CHECK(be->postRegallocFixer == NULL);
    CHECK(be->optimizationMask == NULL);
    CHECK(be->disasm != NULL);
    be->disasm(NULL, 0, NULL);
    be->emulate(&armEmulateProbeCallback, &probeSlot);
    CHECK(probeSlot == 0x5A5AA5A5);

    remap = be->targetRegisterMapping();
    CHECK(remap != NULL);
    CHECK(remap[0] == 0 && remap[1] == 1 && remap[12] == 12 && remap[13] == 14);

    /* 2. canCreateImmediate across ARM rotated-imm8 & load/store classes */
    {
        static const struct {
            int opcode, source, immediate, accepted;
        } cases[] = {
            /* MOV accepts a rotated byte in the value or its complement. */
            {HCOP_MOV, 0, 0, 1},
            {HCOP_MOV, 0, 0x00FF0000, 1},
            {HCOP_MOV, 0, (int)0xFF00FFFFu, 1},
            {HCOP_MOV, 0, 0x12345678, 0},
            /* ALU operations require a rotated byte in the value itself. */
            {HCOP_ADD, 2, 0x00FF0000, 1},
            {HCOP_ADD, 2, (int)0xFF00FFFFu, 0},
            {HCOP_AND, 2, 0x0101, 0},
            {HCOP_LSL, 2, 19, 1},
            /* Halfword/signed-byte offsets are strictly within +/-256. */
            {HCOP_LWS, 0, 255, 1}, {HCOP_LWS, 0, -255, 1},
            {HCOP_LWS, 0, 256, 0}, {HCOP_SW, 0, -256, 0},
            /* Word/unsigned-byte offsets are strictly within +/-4096. */
            {HCOP_LD, 0, 4095, 1}, {HCOP_SD, 0, -4095, 1},
            {HCOP_LD, 0, 4096, 0}, {HCOP_SD, 0, -4096, 0},
            {HCOP_MULS, 2, 3, 0}, {HCOP_LD4, 0, 4, 0}
        };
        unsigned int i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            HCInstr_s ins = cases[i].opcode == HCOP_MOV
                ? HC_genOp2Imm(cases[i].opcode, 1, cases[i].immediate)
                : HC_genOp3Imm(cases[i].opcode, 1, cases[i].source, cases[i].immediate);
            CHECK(be->canCreateImmediate(&ins) == cases[i].accepted);
        }
    }

    /* 3. preRegallocFixer: scaled memops, ROR #0, ROL lowering, 0xFFFF mask */
    prog[0] = HC_genOp3Imm(HCOP_LD4, 1, 0, 3);       /* -> LD r1, r0, #12   */
    prog[1] = HC_genOp3Imm(HCOP_LW2U, 2, 0, 5);      /* -> LWU r2, r0, #10  */
    prog[2] = HC_genOp3(HCOP_LW2S, 3, 0, 4);         /* -> LSL t,r4,#1; LWS */
    prog[3] = HC_genOp3(HCOP_LD4, 1, 0, 2);          /* kept as LD4 reg     */
    prog[4] = HC_genOp3Imm(HCOP_ROR, 1, 2, 0);       /* -> MOV r1, r2       */
    prog[5] = HC_genOp3Imm(HCOP_ROL, 1, 2, 5);       /* -> LSR + LSL + OR   */
    prog[6] = HC_genOp3(HCOP_ROL, 1, 2, 3);          /* -> 5-op reg ROL     */
    prog[7] = HC_genOp3Imm(HCOP_AND, 1, 2, 0xFFFF);  /* -> MOVN+LSR+AND     */
    prog[8] = HC_genOp0(HCOP_EOP);

    mark = be->preRegallocFixer(&arena, &fixed, prog, NULL);
    CHECK(fixed != NULL);
    /* [0]: LD r1, r0, #12 */
    CHECK(HC_getOpcodeOper(&fixed[0]) == HCOP_LD && fixed[0].instr1 == 12u);
    /* [1]: LWU r2, r0, #10 */
    CHECK(HC_getOpcodeOper(&fixed[1]) == HCOP_LWU && fixed[1].instr1 == 10u);
    /* [2..3]: LSL r5, r4, #1; LWS r3, r0, r5 */
    CHECK(HC_getOpcodeOper(&fixed[2]) == HCOP_LSL && fixed[2].instr1 == 1u);
    CHECK(HC_getOpcodeOper(&fixed[3]) == HCOP_LWS && (fixed[3].instr1 & 0x7FFu) == 5u);
    /* [4]: LD4 r1, r0, r2 preserved for native [Rn, Rm, LSL #2] */
    CHECK(HC_getOpcodeOper(&fixed[4]) == HCOP_LD4);
    /* [5]: ROR #0 -> MOV r1, r2 */
    CHECK(HC_getOpcodeOper(&fixed[5]) == HCOP_MOV && (fixed[5].instr0 & 0x100u) == 0);
    /* [6..8]: ROL #5 -> LSR #27 + LSL #5 + OR */
    CHECK(HC_getOpcodeOper(&fixed[6]) == HCOP_LSR && fixed[6].instr1 == 27u);
    CHECK(HC_getOpcodeOper(&fixed[7]) == HCOP_LSL && fixed[7].instr1 == 5u);
    CHECK(HC_getOpcodeOper(&fixed[8]) == HCOP_OR);
    /* [9..13]: ROL reg -> SUBR #32, AND #31, LSR, LSL, OR */
    CHECK(HC_getOpcodeOper(&fixed[9]) == HCOP_SUBR && fixed[9].instr1 == 32u);
    CHECK(HC_getOpcodeOper(&fixed[10]) == HCOP_AND && fixed[10].instr1 == 31u);
    CHECK(HC_getOpcodeOper(&fixed[11]) == HCOP_LSR);
    CHECK(HC_getOpcodeOper(&fixed[12]) == HCOP_LSL);
    CHECK(HC_getOpcodeOper(&fixed[13]) == HCOP_OR);
    /* [14..16]: AND #0xFFFF -> MOVN #0, LSR #16, AND */
    CHECK(HC_getOpcodeOper(&fixed[14]) == HCOP_MOVN && fixed[14].instr1 == 0u);
    CHECK(HC_getOpcodeOper(&fixed[15]) == HCOP_LSR && fixed[15].instr1 == 16u);
    CHECK(HC_getOpcodeOper(&fixed[16]) == HCOP_AND);
    CHECK(HC_getOpcodeOper(&fixed[17]) == HCOP_EOP);
    HCArena_releaseBottom(&arena, mark);

    /* Common backend info setup */
    memset(freeMasks, 0, sizeof(freeMasks));
    memset(duZero, 0, sizeof(duZero));
    memset(udZero, 0, sizeof(udZero));
    beInfo.instructions = prog;
    beInfo.freeRegMasks = freeMasks;
    beInfo.liveRegs = NULL;
    beInfo.chains.duChains = duZero;
    beInfo.chains.udChains = udZero;

    /* 4. codegen: minimal EOP -> STMFD sp!,{r0-r12,lr}; LDMFD sp!,{r0-r12,pc} */
    prog[0] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 0);
    words = (unsigned int *)codePtr;
    CHECK(codeLen == 8);
    CHECK(words[0] == 0xE92D5FFFu);
    CHECK(words[1] == 0xE8BD9FFFu);
    HCArena_releaseBottom(&arena, mark);

    /* 5. codegen: multi-chunk MOV/ORR & inverted MVN immediate synthesis */
    prog[0] = HC_genOp2Imm(HCOP_MOV, 1, 0x13355779);        /* 4 chunks MOV+3*ORR */
    prog[1] = HC_genOp2Imm(HCOP_MOV, 2, 0x78);              /* 0x1E ROR 30        */
    prog[2] = HC_genOp2Imm(HCOP_MOV, 3, (int)0xFFFF00FFu);  /* MVN r3, #0xFF00    */
    prog[3] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 0);
    words = (unsigned int *)codePtr;
    CHECK(codeLen == 32); /* (1 + 4 + 1 + 1 + 1) * 4 */
    CHECK(words[1] == 0xE3A01079u);
    CHECK(words[2] == 0xE3811C57u);
    CHECK(words[3] == 0xE3811835u);
    CHECK(words[4] == 0xE3811413u);
    CHECK(words[5] == 0xE3A02F1Eu);
    CHECK(words[6] == 0xE3E03CFFu);
    HCArena_releaseBottom(&arena, mark);

    /* 6. codegen: loads/stores (imm, negative imm, scaled reg LD4, signed half) */
    prog[0] = HC_genOp3Imm(HCOP_LD, 1, 0, 16);   /* LDR r1, [r0, #16]        */
    prog[1] = HC_genOp3Imm(HCOP_SD, 1, 0, -8);   /* STR r1, [r0, #-8]        */
    prog[2] = HC_genOp3(HCOP_LD4, 1, 0, 2);      /* LDR r1, [r0, r2, LSL #2] */
    prog[3] = HC_genOp3Imm(HCOP_LWS, 1, 0, 6);   /* LDRSH r1, [r0, #6]       */
    prog[4] = HC_genOp0(HCOP_EOP);
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 0);
    words = (unsigned int *)codePtr;
    CHECK(codeLen == 24);
    CHECK(words[1] == 0xE5901010u);
    CHECK(words[2] == 0xE5001008u);
    CHECK(words[3] == 0xE7901102u);
    CHECK(words[4] == 0xE1D010F6u);
    HCArena_releaseBottom(&arena, mark);

    /* 7. codegen: MUL Rd!=Rm swap, free-reg scratch, and spill-to-[r0,#60] */
    prog[0] = HC_genOp3(HCOP_MULS, 1, 2, 3);     /* MUL r1, r2, r3           */
    prog[1] = HC_genOp3(HCOP_MULS, 1, 1, 3);     /* swapped: MUL r1, r3, r1  */
    prog[2] = HC_genOp3(HCOP_MULS, 1, 1, 1);     /* free r2: MOV r2,r1 + MUL */
    prog[3] = HC_genOp3(HCOP_MULS, 1, 1, 1);     /* spill r2 to [r0,#60]     */
    prog[4] = HC_genOp3Imm(HCOP_MAXS, 1, 2, 0);  /* BIC r1, r2, r2, ASR #31  */
    prog[5] = HC_genOp0(HCOP_EOP);
    freeMasks[2] = (1 << 2);
    freeMasks[3] = 0;
    mark = HCArena_markBottom(&arena);
    codeLen = be->codegen(&arena, &codePtr, prog, &beInfo, 0);
    words = (unsigned int *)codePtr;
    CHECK(codeLen == 44); /* 1 + (1 + 1 + 2 + 4 + 1) + 1 = 11 words */
    CHECK(words[1] == 0xE0010392u);
    CHECK(words[2] == 0xE0010193u);
    CHECK(words[3] == 0xE1A02001u);
    CHECK(words[4] == 0xE0010192u);
    CHECK(words[5] == 0xE580203Cu); /* STR r2, [r0, #60] */
    CHECK(words[6] == 0xE1A02001u); /* MOV r2, r1        */
    CHECK(words[7] == 0xE0010192u); /* MUL r1, r2, r1    */
    CHECK(words[8] == 0xE590203Cu); /* LDR r2, [r0, #60] */
    CHECK(words[9] == 0xE1C21FC2u); /* BIC r1, r2, r2, ASR #31 */
    HCArena_releaseBottom(&arena, mark);

    /* 8. codegen: barrel-shifter consumer fusion + CPSR S-bit branch fusion */
    {
        unsigned short duBuf[8] = { 1, 1, 2, 0, 0, 0, 0, 0 }; /* pc=0 -> use at pc=1 */
        unsigned short udShift[2] = { 1, 0 };                 /* def at pc=0         */
        unsigned short udSub[2] = { 1, 3 };                   /* def at pc=3         */
        DUChain_s *usesAt1[2];
        DUChain_s *usesAt4[1];
        UDChainStub_s *udArr[6];
        HCBackendInfo_s fuseInfo;

        prog[0] = HC_genOp3Imm(HCOP_LSL, 3, 2, 2);      /* fused into [1]!            */
        prog[1] = HC_genOp3(HCOP_SUB, 1, 3, 4);         /* RSB r1, r4, r2, LSL #2     */
        prog[2] = HC_genOp1Imm(HCOP_LABEL, 0);          /* loop target at word[2]     */
        prog[3] = HC_genOp3Imm(HCOP_SUB, 1, 1, 1);      /* SUBS r1, r1, #1 (S-bit!)   */
        prog[4] = HC_genOp3ImmRaw(HCOP_CBNEQ, 0, 1, 0); /* BNE word[2] (elides CMP!)  */
        prog[5] = HC_genOp0(HCOP_EOP);

        /* At pc=1 (SUB r1, r3, r4), HC_getUses returns uses={3, 4}: index 0 is r3 */
        usesAt1[0] = (DUChain_s *)udShift;
        usesAt1[1] = NULL;
        /* At pc=4 (CBNEQ label0, r1, #0), HC_getUses returns uses={1}: index 0 is r1 */
        usesAt4[0] = (DUChain_s *)udSub;

        memset(udArr, 0, sizeof(udArr));
        udArr[1] = (UDChainStub_s *)usesAt1;
        udArr[4] = (UDChainStub_s *)usesAt4;

        fuseInfo.instructions = prog;
        fuseInfo.freeRegMasks = freeMasks;
        fuseInfo.liveRegs = NULL;
        fuseInfo.chains.duChains = &duBuf[2];
        fuseInfo.chains.udChains = udArr;

        mark = HCArena_markBottom(&arena);
        codeLen = be->codegen(&arena, &codePtr, prog, &fuseInfo, 0);
        words = (unsigned int *)codePtr;
        /* prologue + RSB + SUBS + BNE + epilogue = 5 words = 20 bytes */
        CHECK(codeLen == 20);
        CHECK(words[0] == 0xE92D5FFFu);
        CHECK(words[1] == 0xE0641102u); /* RSB r1, r4, r2, LSL #2 */
        CHECK(words[2] == 0xE2511001u); /* SUBS r1, r1, #1        */
        CHECK(words[3] == 0x1AFFFFFDu); /* BNE -12 -> words[2]    */
        CHECK(words[4] == 0xE8BD9FFFu);
        HCArena_releaseBottom(&arena, mark);
    }
}

/* --- test_hcdatastructs_and_emulator -------------------------------------- */
#include "gerbera/hc/hcAdjList.h"
#include "gerbera/hc/hcAdjMtx.h"
#include "gerbera/hc/hcBasicBlock.h"
#include "gerbera/hc/hcBitSet.h"
#include "gerbera/hc/hcBucketDefs.h"
#include "gerbera/hc/hcDUChain.h"
#include "gerbera/hc/hcEmulator.h"
#include "gerbera/hc/hcQueue.h"
#include "gerbera/hc/hcStack.h"

static void test_hcdatastructs_and_emulator(void)
{
    static unsigned char arenaBuf[65536];
    HCArena_s arena;
    HCMemory_s mem;
    int mark;

    HCArena_init(&arena, arenaBuf, (int)sizeof(arenaBuf));
    mem = HCArena_bottomInterface(&arena);

    /* 1. HCBitSet: multi-word operations (length = 70 -> 3 words) */
    mark = HCMemory_mark(&mem);
    {
        HCBitSet_s *a = HCBitSet_create(&mem, 70);
        HCBitSet_s *b = HCBitSet_create(&mem, 70);

        CHECK(HCBitSet_numElements(a) == 0);
        CHECK(HCBitSet_getFirst(a) == -1);
        CHECK(HCBitSet_getFirstUnset(a) == 0);

        HCBitSet_or(a, 0);
        HCBitSet_or(a, 31);
        HCBitSet_or(a, 32);
        HCBitSet_or(a, 69);
        CHECK(HCBitSet_contains(a, 0) != 0);
        CHECK(HCBitSet_contains(a, 31) != 0);
        CHECK(HCBitSet_contains(a, 32) != 0);
        CHECK(HCBitSet_contains(a, 69) != 0);
        CHECK(HCBitSet_contains(a, 33) == 0);
        CHECK(HCBitSet_numElements(a) == 4);
        CHECK(HCBitSet_getFirst(a) == 0);
        CHECK(HCBitSet_getNext(a, 0) == 31);
        CHECK(HCBitSet_getNext(a, 31) == 32);
        CHECK(HCBitSet_getNext(a, 32) == 69);
        CHECK(HCBitSet_getNext(a, 69) == -1);

        HCBitSet_sub(a, 31);
        CHECK(HCBitSet_contains(a, 31) == 0);
        CHECK(HCBitSet_numElements(a) == 3);

        HCBitSet_copy(b, a);
        CHECK(HCBitSet_equal(a, b) == 1);
        HCBitSet_or(b, 15);
        CHECK(HCBitSet_equal(a, b) == 0);

        HCBitSet_union(a, b);
        CHECK(HCBitSet_equal(a, b) == 1);
        HCBitSet_sub(b, 0);
        HCBitSet_intersect(a, b);
        CHECK(HCBitSet_contains(a, 0) == 0 && HCBitSet_contains(a, 15) != 0);
        HCBitSet_difference(a, b);
        CHECK(HCBitSet_numElements(a) == 0);

        /* HCBitSet_set masks tail word to 70 bits (69 max, 70 out-of-range) */
        HCBitSet_set(b);
        CHECK(HCBitSet_numElements(b) == 70);
        CHECK(HCBitSet_getFirstUnset(b) == -1);
        HCBitSet_sub(b, 35);
        HCBitSet_sub(b, 42);
        CHECK(HCBitSet_getFirstUnset(b) == 35);
        CHECK(HCBitSet_getNextUnset(b, 35) == 42);
        HCBitSet_clear(b);
        CHECK(HCBitSet_numElements(b) == 0);

        /* Single-word nHardRegs=14 bitset (as used by minColor) */
        {
            HCBitSet_s *c = HCBitSet_create(&mem, 14);
            HCBitSet_set(c);
            HCBitSet_sub(c, 3);
            HCBitSet_sub(c, 11);
            CHECK(HCBitSet_getFirstUnset(c) == 3);
            CHECK(HCBitSet_getNextUnset(c, 3) == 11);
            CHECK(HCBitSet_getNextUnset(c, 11) == -1);
        }
    }
    HCMemory_release(&mem, mark);

    /* 2. HCAdjMtx + HCAdjList: 8-node compressed interference graph */
    mark = HCMemory_mark(&mem);
    {
        static const int pairs[][2] = {
            {1, 0}, {2, 0}, {2, 1}, {4, 0}, {4, 1}, {4, 3},
            {5, 0}, {5, 1}, {5, 4}, {6, 2}, {6, 5}, {7, 0}, {7, 3}, {7, 6}
        };
        int adjRef[8][8];
        HCAdjMtx_s *mtx = HCAdjMtx_create(&mem, 8);
        HCAdjList_s *list;
        unsigned short edges[16];
        int k, u, v;

        memset(adjRef, 0, sizeof(adjRef));
        for (k = 0; k < (int)(sizeof(pairs) / sizeof(pairs[0])); ++k)
        {
            u = pairs[k][0];
            v = pairs[k][1];
            HCAdjMtx_connect(mtx, u, v);
            adjRef[u][v] = 1;
            adjRef[v][u] = 1;
        }
        CHECK(HCAdjMtx_hasEdge(mtx, 4, 1) != 0);
        CHECK(HCAdjMtx_hasEdge(mtx, 4, 2) == 0);

        list = HCAdjList_create(&mem, mtx);
        CHECK(list != NULL && list->numNodes == 8);
        for (u = 0; u < 8; ++u)
        {
            int got = HCAdjList_getEdges(list, u, edges);
            int expected = 0;
            int idx = 0;
            for (v = 0; v < 8; ++v)
                expected += adjRef[u][v];
            CHECK(got == expected);
            for (v = 0; v < 8; ++v)
            {
                if (adjRef[u][v])
                    CHECK(idx < got && edges[idx++] == (unsigned short)v);
            }
        }
    }
    HCMemory_release(&mem, mark);

    /* 3. HCBucketDefs (cached nBuckets<32 and uncached nBuckets>=32) + DUChain + BasicBlock */
    mark = HCMemory_mark(&mem);
    {
        HCInstr_s ir[6];
        HCBucketDefs_s *bdSmall;
        HCBucketDefs_s *bdLarge;
        HCBitSet_s *reach;
        DUChain_s *du;
        UDChainStub_s *uds;
        BasicBlock_s bb;

        ir[0] = HC_genOp2Imm(HCOP_MOV, 2, 10);
        ir[1] = HC_genOp2Imm(HCOP_MOV, 5, 20);
        ir[2] = HC_genOp3(HCOP_ADD, 2, 2, 5);
        ir[3] = HC_genOp3Imm(HCOP_SD, 2, 0, 4); /* store: no def */
        ir[4] = HC_genOp2Imm(HCOP_MOV, 2, 99);
        ir[5] = HC_genOp0(HCOP_EOP);

        bdSmall = HCBucketDefs_create(&mem, ir, 8, 6);
        CHECK(HCBucketDefs_numValues(bdSmall, 2) == 3);
        CHECK(HCBucketDefs_getValue(bdSmall, 2, 0) == 0);
        CHECK(HCBucketDefs_getValue(bdSmall, 2, 1) == 2);
        CHECK(HCBucketDefs_getValue(bdSmall, 2, 2) == 4);
        CHECK(HCBucketDefs_numValues(bdSmall, 5) == 1);
        CHECK(HCBucketDefs_getValue(bdSmall, 5, 0) == 1);
        CHECK(HCBucketDefs_numValues(bdSmall, 3) == 0);
        CHECK(HCBucketDefs_getCachedSet(bdSmall, 2) != NULL);

        reach = HCBitSet_create(&mem, 6);
        HCBitSet_set(reach);
        HCBucketDefs_sub(bdSmall, reach, 2);
        CHECK(HCBitSet_contains(reach, 0) == 0);
        CHECK(HCBitSet_contains(reach, 2) == 0);
        CHECK(HCBitSet_contains(reach, 4) == 0);
        CHECK(HCBitSet_contains(reach, 1) != 0);

        bdLarge = HCBucketDefs_create(&mem, ir, 40, 6);
        CHECK(HCBucketDefs_getCachedSet(bdLarge, 2) == NULL);
        HCBitSet_set(reach);
        HCBucketDefs_sub(bdLarge, reach, 2);
        CHECK(HCBitSet_numElements(reach) == 3);

        du = DUChain_create(&mem, 4);
        CHECK(du != NULL && du->length == 0);
        uds = UDChainStub_create(&mem, 3);
        CHECK(uds != NULL && uds->chains[0] == NULL && uds->chains[2] == NULL);

        BasicBlock_init(&bb);
        BasicBlock_setStart(&bb, 3);
        BasicBlock_setEnd(&bb, 19);
        BasicBlock_setDominators(&bb, reach);
        CHECK(BasicBlock_getStart(&bb) == 3);
        CHECK(BasicBlock_getEnd(&bb) == 19);
        CHECK(bb.dominators == reach);
    }
    HCMemory_release(&mem, mark);

    /* 4. HCQueue (wrap-around + grow) & HCStack (grow past 2) */
    mark = HCArena_markBottom(&arena);
    {
        HCQueue_s *q = HCQueue_create(&arena);
        int i;

        CHECK(HCQueue_isEmpty(q) == 1);
        for (i = 0; i < 8; ++i)
            HCQueue_enqueue(&arena, q, 100 + i);
        for (i = 0; i < 5; ++i)
            CHECK(HCQueue_dequeue(q) == 100 + i);
        /* Three elements wrap around the buffer end; twelve more force growth. */
        for (i = 8; i < 20; ++i)
            HCQueue_enqueue(&arena, q, 100 + i);
        for (i = 5; i < 20; ++i)
            CHECK(HCQueue_dequeue(q) == 100 + i);
        CHECK(HCQueue_isEmpty(q) == 1);
    }
    HCArena_releaseBottom(&arena, mark);

    mark = HCArena_markBottom(&arena);
    {
        HCStack_s *st = HCStack_create(&arena);
        int i;

        CHECK(HCStack_isEmpty(st) == 1);
        for (i = 0; i < 10; ++i)
            HCStack_push(&arena, st, 500 + i);
        CHECK(HCStack_isEmpty(st) == 0);
        for (i = 9; i >= 0; --i)
            CHECK(HCStack_pop(st) == 500 + i);
        CHECK(HCStack_isEmpty(st) == 1);
    }
    HCArena_releaseBottom(&arena, mark);

    /* 5. hcEmulator: conditionals, ALU ops, and instruction substitution */
    CHECK(emulator_evalConditional(HCOP_CBA, 0x0Au, 0x02u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBNA, 0x0Au, 0x04u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBEQ, 42u, 42u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBNEQ, 42u, 43u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBLTS, 0x80000000u, 1u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBLTU, 0x80000000u, 1u) == 0);
    CHECK(emulator_evalConditional(HCOP_CBGTS, 1u, 0xFFFFFFFFu) == 1);
    CHECK(emulator_evalConditional(HCOP_CBGTU, 0xFFFFFFFFu, 1u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBGES, 0xFFFFFFFFu, 0xFFFFFFFFu) == 1);
    CHECK(emulator_evalConditional(HCOP_CBGEU, 5u, 5u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBLES, 0xFFFFFFFFu, 0u) == 1);
    CHECK(emulator_evalConditional(HCOP_CBLEU, 0u, 0xFFFFFFFFu) == 1);

    CHECK(emulator_evalALUOp(HCOP_ADD, 10u, 25u) == 35);
    CHECK(emulator_evalALUOp(HCOP_SUB, 10u, 25u) == -15);
    CHECK(emulator_evalALUOp(HCOP_SUBR, 10u, 25u) == 15);
    CHECK(emulator_evalALUOp(HCOP_BIC, 0xFFu, 0x0Fu) == 0xF0);
    CHECK(emulator_evalALUOp(HCOP_ASR, 0xFFFFFFF0u, 2u) == -4);
    CHECK(emulator_evalALUOp(HCOP_LSR, 0xFFFFFFF0u, 4u) == 0x0FFFFFFF);
    CHECK(emulator_evalALUOp(HCOP_ASD, 0xFFFFFFF0u, (unsigned int)-2) == -4);
    CHECK(emulator_evalALUOp(HCOP_LSD, 0xFFFFFFF0u, (unsigned int)-4) == 0x0FFFFFFF);
    {
        static const int shifts[] = { 32, -32, 256, -256, (int)0x80000000u };
#if GERBERA_ARM_REFERENCE
        static const unsigned int asd[] = { 0, 0xFFFFFFFFu, 0x87654321u, 0x87654321u, 0x87654321u };
        static const unsigned int lsd[] = { 0, 0, 0x87654321u, 0x87654321u, 0x87654321u };
#else
        static const unsigned int asd[] = { 0x87654321u, 0x87654321u, 0x87654321u, 0x87654321u, 0x87654321u };
        static const unsigned int lsd[] = { 0x87654321u, 0x87654321u, 0x87654321u, 0x87654321u, 0x87654321u };
#endif
        int index;
        for (index = 0; index < 5; ++index)
        {
            CHECK((unsigned int)emulator_evalALUOp(HCOP_ASD, 0x87654321u, (unsigned int)shifts[index]) == asd[index]);
            CHECK((unsigned int)emulator_evalALUOp(HCOP_LSD, 0x87654321u, (unsigned int)shifts[index]) == lsd[index]);
        }
    }
    CHECK((unsigned int)emulator_evalALUOp(HCOP_ROL, 0x80000001u, 1u) == 3u);
    CHECK((unsigned int)emulator_evalALUOp(HCOP_ROR, 3u, 1u) == 0x80000001u);
    CHECK(emulator_evalALUOp(HCOP_MAXS, (unsigned int)-5, 3u) == 3);
    CHECK((unsigned int)emulator_evalALUOp(HCOP_MAXU, (unsigned int)-5, 3u) == (unsigned int)-5);
    CHECK(emulator_evalALUOp(HCOP_MINS, (unsigned int)-5, 3u) == -5);
    CHECK(emulator_evalALUOp(HCOP_MINU, (unsigned int)-5, 3u) == 3);
    CHECK(emulator_evalALUOp(HCOP_MOVN, 0u, 0u) == -1);

    {
        HCInstr_s in, out;

        /* ADD r1, r3, #7 with r3=10 -> MOV r1, #17 */
        in = HC_genOp3Imm(HCOP_ADD, 1, 3, 7);
        out = emulator_substitute(&in, 3, 10);
        CHECK(HC_getOpcodeOper(&out) == HCOP_MOV && HC_getOpcodeImm(&out) == 17);

        /* Original substitution also matches an immediate's low 11 bits. */
        in = HC_genOp3Imm(HCOP_ADD, 1, 2, 0x803);
        out = emulator_substitute(&in, 3, 11);
        CHECK(HC_getOpcodeOper(&out) == HCOP_ADD
              && HC_getOpcodeSrc0(&out) == 2
              && HC_getOpcodeImm(&out) == 11);

        /* SUB r1, r3, r4 with r3=50 -> SUBR r1, r4, #50 */
        in = HC_genOp3(HCOP_SUB, 1, 3, 4);
        out = emulator_substitute(&in, 3, 50);
        CHECK(HC_getOpcodeOper(&out) == HCOP_SUBR
              && HC_getOpcodeSrc0(&out) == 4
              && HC_getOpcodeImm(&out) == 50);

        /* LD4 r1, r0, r3 with r3=5 -> LD r1, r0, #20 */
        in = HC_genOp3(HCOP_LD4, 1, 0, 3);
        out = emulator_substitute(&in, 3, 5);
        CHECK(HC_getOpcodeOper(&out) == HCOP_LD && HC_getOpcodeImm(&out) == 20);

        out = emulator_substitute(&in, 3, 0x7FFFFFFF);
        CHECK(HC_getOpcodeOper(&out) == HCOP_LD && HC_getOpcodeImm(&out) == -4);

        /* LD4 r1, r3, r2 (base is r3): NOT folded (scaled index is r2) */
        in = HC_genOp3(HCOP_LD4, 1, 3, 2);
        out = emulator_substitute(&in, 3, 5);
        CHECK(HC_getOpcodeOper(&out) == HCOP_LD4 && !HC_opcodeHasImm(&out));

        /* CBEQ label9, r3, #42: r3=42 -> BRA 9; r3=41 -> NOP */
        in = HC_genOp3ImmRaw(HCOP_CBEQ, 9, 3, 42);
        out = emulator_substitute(&in, 3, 42);
        CHECK(HC_getOpcodeOper(&out) == HCOP_BRA && HC_getOpcodeDst(&out) == 9);
        out = emulator_substitute(&in, 3, 41);
        CHECK(HC_getOpcodeOper(&out) == HCOP_NOP);
    }
}

/* --- HC_compile / HC_compilePriv / pipeline cache ------------------------- */
#include "gerbera/hc/hcCompiler.h"
#include <setjmp.h>

static int hc_test_setjmpWrap(void *jmpbuf, int (*func)(void *), void *args)
{
    jmp_buf *jb = (jmp_buf *)jmpbuf;
    if (setjmp(*jb) != 0)
        return -1;
    return func(args);
}

static void hc_test_longjmp(void *jmpbuf, int value)
{
    jmp_buf *jb = (jmp_buf *)jmpbuf;
    longjmp(*jb, value);
}

/* Portable HC IR interpreter for verifying optimizer + regalloc equivalence
 * on 64-bit hosts without executing 32-bit IA-32/ARM32 machine code.
 * Register 0 holds offset 0 into `mem` (`memSize` bytes). */
static void eval_hc_ir(HCInstr_s *lst, unsigned char *mem, unsigned int memSize)
{
    unsigned int regs[512];
    int pc = 0;
    int guard = 0;

    memset(regs, 0, sizeof(regs));
    while (guard++ < 100000)
    {
        HCInstr_s *in = &lst[pc];
        HCOP_e op = HC_getOpcodeOper(in);

        if (op == HCOP_EOP)
            return;
        if (op == HCOP_LABEL || op == HCOP_NOP)
        {
            ++pc;
            continue;
        }
        if (HC_getOpcodeType(in) == HCOP_TYPE_ALUOP)
        {
            int dst = HC_getOpcodeDst(in);
            int argc = HC_getOpcodeArgCountOp(op);
            unsigned int s0 = 0;
            unsigned int s1 = 0xFFFFFFFFu;

            if (argc == 2)
            {
                s0 = HC_opcodeHasImm(in)
                    ? (unsigned int)HC_getOpcodeImm(in)
                    : regs[HC_getOpcodeSrc0(in)];
            }
            else if (argc == 3)
            {
                s0 = regs[HC_getOpcodeSrc0(in)];
                s1 = HC_opcodeHasImm(in)
                    ? (unsigned int)HC_getOpcodeImm(in)
                    : regs[HC_getOpcodeSrc1(in)];
            }
            regs[dst] = (unsigned int)emulator_evalALUOp(op, s0, s1);
            regs[0] = 0;
            ++pc;
            continue;
        }
        if (HC_isLoadOp(op) || HC_isStoreOp(op))
        {
            int scale = HC_getMemOpIndexSize(in);
            unsigned int base = regs[HC_getOpcodeSrc0(in)];
            int off = HC_opcodeHasImm(in)
                ? HC_getOpcodeImm(in)
                : scale * (int)regs[HC_getOpcodeSrc1(in)];
            unsigned int addr = (unsigned int)((int)base + off);
            int dst = HC_getOpcodeDst(in);

            CHECK(addr + 4u <= memSize);
            if (HC_isLoadOp(op))
            {
                unsigned int v = 0;
                if (op == HCOP_LBU)
                    v = mem[addr];
                else if (op == HCOP_LBS)
                    v = (unsigned int)(int)(signed char)mem[addr];
                else if (op == HCOP_LWU || op == HCOP_LW2U)
                {
                    unsigned short w;
                    memcpy(&w, &mem[addr], 2);
                    v = w;
                }
                else if (op == HCOP_LWS || op == HCOP_LW2S)
                {
                    short w;
                    memcpy(&w, &mem[addr], 2);
                    v = (unsigned int)(int)w;
                }
                else
                {
                    memcpy(&v, &mem[addr], 4);
                }
                regs[dst] = v;
                regs[0] = 0;
            }
            else
            {
                unsigned int v = regs[dst];
                if (op == HCOP_SB)
                    mem[addr] = (unsigned char)v;
                else if (op == HCOP_SW || op == HCOP_SW2)
                {
                    unsigned short w = (unsigned short)v;
                    memcpy(&mem[addr], &w, 2);
                }
                else
                {
                    memcpy(&mem[addr], &v, 4);
                }
            }
            ++pc;
            continue;
        }
        if (op == HCOP_BRA)
        {
            int target = HC_getOpcodeDst(in);
            int k = 0;
            while (HC_getOpcodeOper(&lst[k]) != HCOP_EOP)
            {
                if (HC_getOpcodeOper(&lst[k]) == HCOP_LABEL
                    && HC_getOpcodeLabel(&lst[k]) == target)
                {
                    pc = k;
                    break;
                }
                ++k;
            }
            continue;
        }
        if (HC_isCondBranchOp(op))
        {
            unsigned int s0 = regs[HC_getOpcodeSrc0(in)];
            unsigned int s1 = HC_opcodeHasImm(in)
                ? (unsigned int)HC_getOpcodeImm(in)
                : regs[HC_getOpcodeSrc1(in)];
            if (emulator_evalConditional(op, s0, s1) != 0)
            {
                int target = HC_getOpcodeDst(in);
                int k = 0;
                while (HC_getOpcodeOper(&lst[k]) != HCOP_EOP)
                {
                    if (HC_getOpcodeOper(&lst[k]) == HCOP_LABEL
                        && HC_getOpcodeLabel(&lst[k]) == target)
                    {
                        pc = k;
                        break;
                    }
                    ++k;
                }
            }
            else
            {
                ++pc;
            }
            continue;
        }
        CHECK(0 && "unhandled opcode in eval_hc_ir");
        return;
    }
    CHECK(0 && "eval_hc_ir step limit exceeded");
}

static unsigned int rd_u32(const unsigned char *mem, unsigned int off)
{
    unsigned int v;
    memcpy(&v, &mem[off], 4);
    return v;
}

static void wr_u32(unsigned char *mem, unsigned int off, unsigned int v)
{
    memcpy(&mem[off], &v, 4);
}

static void test_hccompiler(void)
{
    static unsigned char scratch[131072];
    HCSetjmpConfig_s sjCfg;
    HCBackend_s *x86Be = HCX86Backend_create();
    HCBackend_s *armBe = HCARMBackend_create();

    sjCfg.setjmpWrap = hc_test_setjmpWrap;
    sjCfg.longjmp    = hc_test_longjmp;
    sjCfg.jmpbufSize = (unsigned int)((sizeof(jmp_buf) + 7u) & ~7u);

    /* 1. HC_extraStackSpace + grGetBackend wiring */
    CHECK(HC_extraStackSpace(NULL) == 0);
    CHECK(HC_extraStackSpace(x86Be) == 44); /* 40 + 4 */
    CHECK(HC_extraStackSpace(armBe) == 64); /* 60 + 4 */
    CHECK(grGetBackend() == (void *)x86Be);
    CHECK(HC_containsInt((const int[]){ 3, 7, 11 }, 3, 7) == 1);
    CHECK(HC_containsInt((const int[]){ 3, 7, 11 }, 3, 9) == 0);

    /* 2. Out-of-scratch-memory escape via setjmpWrap / longjmp -> -1 */
    {
        HCInstr_s tinyProg[4];
        void *outPtr = NULL;

        tinyProg[0] = HC_genOp3Imm(HCOP_LD, 1, 0, 4);
        tinyProg[1] = HC_genOp3Imm(HCOP_ADD, 1, 1, 1);
        tinyProg[2] = HC_genOp3Imm(HCOP_SD, 1, 0, 8);
        tinyProg[3] = HC_genOp0(HCOP_EOP);

        /* Arena smaller than jmpbufSize immediately returns -1 */
        CHECK(HC_compile(scratch, (int)sjCfg.jmpbufSize, &sjCfg, &outPtr,
                         tinyProg, x86Be, HC_OPT_ALL, NULL, NULL, 4) == -1);
        /* Arena slightly above jmpbufSize exhausts HCArena and longjmps -1 */
        CHECK(HC_compile(scratch, (int)sjCfg.jmpbufSize + 64, &sjCfg, &outPtr,
                         tinyProg, x86Be, HC_OPT_ALL, NULL, NULL, 4) == -1);
    }

    /* 3. Optimizer pass suite + IR-mode (backend == NULL) + inputParamsUsed */
    {
        HCInstr_s prog[64];
        unsigned char memRef[1024];
        unsigned char memOpt[1024];
        unsigned char paramUsed[4];
        void *optCode = NULL;
        int n = 0;
        int optBytes;
        int optInstrs;

        /* Stack params: slot 2 (off 8) = 0x12345678, slot 5 (off 20) = 7,
         * slot 6 (off 24, dead load) = 999, output at off 128..160. */
        prog[n++] = HC_genOp3Imm(HCOP_LD,   1, 0, 8);     /* param 2 (used)   */
        prog[n++] = HC_genOp3Imm(HCOP_LD,   2, 0, 20);    /* param 5 (used)   */
        prog[n++] = HC_genOp3Imm(HCOP_LD,   3, 0, 24);    /* param 6 (dead!)  */
        /* Constant propagation + algebraic peepholes */
        prog[n++] = HC_genOp2Imm(HCOP_MOV,  4, 10);
        prog[n++] = HC_genOp3Imm(HCOP_ADD,  5, 4, 5);     /* -> MOV 15        */
        prog[n++] = HC_genOp2(HCOP_MOV,     6, 5);        /* copy prop        */
        prog[n++] = HC_genOp3Imm(HCOP_ADD,  7, 1, 0);     /* peephole -> MOV  */
        prog[n++] = HC_genOp3Imm(HCOP_AND,  7, 7, -1);    /* peephole -> MOV  */
        prog[n++] = HC_genOp3Imm(HCOP_MULU, 8, 2, 8);     /* peephole -> LSL3 */
        prog[n++] = HC_genOp3(HCOP_ADD,     9, 7, 8);
        prog[n++] = HC_genOp3(HCOP_ADD,     9, 9, 6);
        /* Shift-chain & shift-logical-and combine */
        prog[n++] = HC_genOp3Imm(HCOP_LSR,  10, 1, 16);
        prog[n++] = HC_genOp3Imm(HCOP_LSR,  11, 10, 8);   /* -> LSR r1, 24    */
        prog[n++] = HC_genOp3Imm(HCOP_AND,  12, 11, 0xFF);/* -> redundant AND */
        /* Branch inversion (CBEQ L1; BRA L2; LABEL L1) + unreachable code */
        prog[n++] = HC_genOp3ImmRaw(HCOP_CBEQ, 1, 2, 7);
        prog[n++] = HC_genOp1(HCOP_BRA, 2);
        prog[n++] = HC_genOp2Imm(HCOP_MOV,  9, 0xDEAD);   /* unreachable!     */
        prog[n++] = HC_genOp1Imm(HCOP_LABEL, 1);
        prog[n++] = HC_genOp3Imm(HCOP_ADD,  9, 9, 100);
        prog[n++] = HC_genOp1Imm(HCOP_LABEL, 2);
        prog[n++] = HC_genOp2Imm(HCOP_MOV,  13, 424242);  /* dead code!       */
        prog[n++] = HC_genOp3Imm(HCOP_SD,   9,  0, 128);
        prog[n++] = HC_genOp3Imm(HCOP_SD,   12, 0, 132);
        prog[n++] = HC_genOp0(HCOP_EOP);

        memset(memRef, 0, sizeof(memRef));
        memset(memOpt, 0, sizeof(memOpt));
        wr_u32(memRef, 8,  0x12345678u);
        wr_u32(memRef, 20, 7u);
        wr_u32(memRef, 24, 999u);
        memcpy(memOpt, memRef, sizeof(memRef));

        eval_hc_ir(prog, memRef, sizeof(memRef));
        CHECK(rd_u32(memRef, 128) == 0x12345678u + 56u + 15u + 100u);
        CHECK(rd_u32(memRef, 132) == 0x12u);

        memset(paramUsed, 0xFF, sizeof(paramUsed));
        optBytes = HC_compile(scratch, (int)sizeof(scratch), &sjCfg, &optCode,
                              prog, NULL, HC_OPT_ALL, paramUsed, NULL, 16);
        CHECK(optBytes > 0);
        CHECK(optCode != NULL);
        optInstrs = optBytes / (int)sizeof(HCInstr_s);
        CHECK(optInstrs < n);
        /* Only params 2 and 5 survive DCE (param 6 at offset 24 was eliminated) */
        CHECK(paramUsed[0] == ((1u << 2) | (1u << 5)));
        CHECK(paramUsed[1] == 0u);

        eval_hc_ir((HCInstr_s *)optCode, memOpt, sizeof(memOpt));
        CHECK(rd_u32(memOpt, 128) == rd_u32(memRef, 128));
        CHECK(rd_u32(memOpt, 132) == rd_u32(memRef, 132));
    }

    /* 4. peepholeFills + inlineFills: loop fill recognition & 8x unroll */
    {
        HCInstr_s fillProg[16];
        unsigned char memRef[512];
        unsigned char memOpt[512];
        void *optCode = NULL;
        int n = 0;
        int optBytes;
        int k;

        /* Fill 11 dwords starting at byte offset 128:
         * dst = 128 + 11*4 = 172, cnt = -11; loop stores dst[cnt] and ++cnt */
        fillProg[n++] = HC_genOp2Imm(HCOP_MOV,   1, 0xCAFEBABEu);
        fillProg[n++] = HC_genOp2Imm(HCOP_MOV,   2, 128 + 11 * 4);
        fillProg[n++] = HC_genOp2Imm(HCOP_MOV,   3, -11);
        fillProg[n++] = HC_genOp1Imm(HCOP_LABEL, 5);
        fillProg[n++] = HC_genOp3(HCOP_SD4,      1, 2, 3);
        fillProg[n++] = HC_genOp3Imm(HCOP_ADD,   3, 3, 1);
        fillProg[n++] = HC_genOp3ImmRaw(HCOP_CBNEQ, 5, 3, 0);
        fillProg[n++] = HC_genOp0(HCOP_EOP);

        memset(memRef, 0x11, sizeof(memRef));
        memset(memOpt, 0x11, sizeof(memOpt));
        eval_hc_ir(fillProg, memRef, sizeof(memRef));

        optBytes = HC_compile(scratch, (int)sizeof(scratch), &sjCfg, &optCode,
                              fillProg, NULL, HC_OPT_INLINE_FILLS | HC_OPT_PEEPHOLE,
                              NULL, NULL, 4);
        CHECK(optBytes > (int)(n * sizeof(HCInstr_s))); /* expanded 8x unroll */
        eval_hc_ir((HCInstr_s *)optCode, memOpt, sizeof(memOpt));
        for (k = 0; k < 11; ++k)
        {
            CHECK(rd_u32(memRef, 128u + 4u * (unsigned int)k) == 0xCAFEBABEu);
            CHECK(rd_u32(memOpt, 128u + 4u * (unsigned int)k) == 0xCAFEBABEu);
        }
        CHECK(rd_u32(memOpt, 124u) == 0x11111111u);
        CHECK(rd_u32(memOpt, 128u + 44u) == 0x11111111u);
    }

    /* 5. High-pressure register allocation (spilling!) + panic regalloc on
     *    BOTH x86 (7 hard regs) and ARM (13 hard regs), verifying semantic
     *    equivalence via HC_compilePriv's emuDst! */
    {
        HCInstr_s pressureProg[128];
        static HCInstr_s emuIr[512];
        unsigned char memRef[2048];
        unsigned char memRun[2048];
        unsigned char mutableMask[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        int n = 0;
        int r;
        int bIdx;
        HCBackend_s *backends[2];

        backends[0] = x86Be;
        backends[1] = armBe;

        /* 16 simultaneously live values across a loop (> 7 x86 & > 13 ARM
         * hard regs), forcing pruneGraph + genSpillCode + multi-round coloring */
        for (r = 1; r <= 16; ++r)
            pressureProg[n++] = HC_genOp3Imm(HCOP_LD, r, 0, 4 * r);
        pressureProg[n++] = HC_genOp2Imm(HCOP_MOV, 17, 3); /* 3 loop iters */
        pressureProg[n++] = HC_genOp1Imm(HCOP_LABEL, 1);
        for (r = 1; r <= 16; ++r)
            pressureProg[n++] = HC_genOp3Imm(HCOP_ADD, r, r, r);
        pressureProg[n++] = HC_genOp3Imm(HCOP_SUB, 17, 17, 1);
        pressureProg[n++] = HC_genOp3ImmRaw(HCOP_CBNEQ, 1, 17, 0);
        pressureProg[n++] = HC_genOp2Imm(HCOP_MOV, 18, 0);
        for (r = 1; r <= 16; ++r)
            pressureProg[n++] = HC_genOp3(HCOP_ADD, 18, 18, r);
        pressureProg[n++] = HC_genOp3Imm(HCOP_SD, 18, 0, 4 * 17);
        pressureProg[n++] = HC_genOp0(HCOP_EOP);

        memset(memRef, 0, sizeof(memRef));
        for (r = 1; r <= 16; ++r)
            wr_u32(memRef, 4u * (unsigned int)r, (unsigned int)(10 * r));
        eval_hc_ir(pressureProg, memRef, sizeof(memRef));
        /* Sum_{r=1..16} (10*r + 3*r) = 13 * 136 = 1768 */
        CHECK(rd_u32(memRef, 4 * 17) == 1768u);

        for (bIdx = 0; bIdx < 2; ++bIdx)
        {
            HCBackend_s *be = backends[bIdx];
            int extra = HC_extraStackSpace(be);
            unsigned int masks[2] = { HC_OPT_ALL, HC_OPT_ALL & ~HC_OPT_REGALLOC };
            int mIdx;

            for (mIdx = 0; mIdx < 2; ++mIdx)
            {
                void *jitCode = NULL;
                int codeBytes;

                memset(emuIr, 0, sizeof(emuIr));
                codeBytes = HC_compilePriv(
                    scratch, (int)sizeof(scratch), &sjCfg,
                    &jitCode, emuIr, pressureProg, be,
                    masks[mIdx], NULL, mutableMask, 20);
                CHECK(codeBytes > 0);
                CHECK(jitCode != NULL);

                if (be == armBe)
                {
                    const unsigned int *w = (const unsigned int *)jitCode;
                    CHECK((codeBytes & 3) == 0);
                    /* ARM prologue: STMFD sp!, {r0..r12, lr} = 0xE92D5FFF */
                    CHECK(w[0] == 0xE92D5FFFu);
                    /* ARM epilogue ends with LDMFD sp!, {r0..r12, pc} = 0xE8BD9FFF */
                    CHECK(w[(codeBytes / 4) - 1] == 0xE8BD9FFFu);
                }
                else
                {
                    const unsigned char *b = (const unsigned char *)jitCode;
                    /* IA-32 epilogue ends with RET (0xC3) */
                    CHECK(b[codeBytes - 1] == 0xC3u);
                }

                /* Execute post-regalloc emuDst with stack shifted by extraStack! */
                memset(memRun, 0, sizeof(memRun));
                for (r = 1; r <= 16; ++r)
                    wr_u32(memRun, (unsigned int)extra + 4u * (unsigned int)r,
                           (unsigned int)(10 * r));
                eval_hc_ir(emuIr, memRun, sizeof(memRun));
                CHECK(rd_u32(memRun, (unsigned int)extra + 4u * 17u) == 1768u);
            }
        }
    }

    /* 6. End-to-end Gerbera pixel pipeline compilation + caching via
     *    grEnsureValidPixelPipeline (cache miss -> grExecuteProgram ->
     *    HC_compile -> grIsGradientUsed -> grCacheInsert -> cache hit)! */
    {
        static Gerbera_s st;
        static unsigned char cacheArena[32768];
        static unsigned char pipeScratch[131072];
        static unsigned short dummyColor[64 * 64];
        static unsigned int dummyDepth[64 * 64];
        _grPipelineCache cache;
        _grCacheEntry entries[8];
        void *firstCode;

        memset(&st, 0, sizeof(st));
        /* Configure a valid RGB565 Gouraud + depth-test pixel pipeline state */
        st.drawSurface.colorMask       = 0x07;
        st.drawSurface.colorBytes      = 2;
        st.drawSurface.depthBytes      = 4;
        st.drawSurface.pixelFormat     = 4; /* RGB565 -> GR_PIXELFORMAT_RGB */
        st.drawSurface.rgbaSizes[0]    = 5;
        st.drawSurface.rgbaSizes[1]    = 6;
        st.drawSurface.rgbaSizes[2]    = 5;
        st.drawSurface.rgbaOffsets[0]  = 11;
        st.drawSurface.rgbaOffsets[1]  = 5;
        st.drawSurface.rgbaOffsets[2]  = 0;
        st.drawSurface.width           = 64;
        st.drawSurface.height          = 64;
        st.drawSurface.buffers[0].data = dummyColor;
        st.drawSurface.buffers[1].data = dummyDepth;
        st.colorMask                 = 0x0F;
        st.depthMask                 = 1;
        st.depthFunc                 = GR_FUNC_LESS;
        st.viewportScale.normal[2]   = 0x10000000;
        grSetCapability(&st, GR_CAPABILITY_DEPTH_TEST, 1);
        grSetCapability(&st, GR_CAPABILITY_SMOOTH_SHADING, 1);

        grCacheInit(&cache, cacheArena, entries, (int)sizeof(cacheArena), 8);
        st.pipelineCache     = &cache;
        st.scratchMemory     = pipeScratch;
        st.scratchMemorySize = (int)sizeof(pipeScratch);
        st.setjmpConfig      = &sjCfg;

        /* Cache miss #1 (RGB565 + depth, no fog): activates depth (0) + R,G,B (2,3,4) */
        grEnsureValidPixelPipeline(&st);
        CHECK(st.compilationFailed == 0);
        CHECK(st.pixelPipelineCode != NULL);
        CHECK(st.activeGradientMask == 0x1D);
        CHECK(st.numActiveGradients == 4);
        CHECK(st.activeGradientIndex[0] == 0);
        CHECK(st.activeGradientIndex[1] == 2);
        CHECK(st.activeGradientIndex[2] == 3);
        CHECK(st.activeGradientIndex[3] == 4);

        firstCode = (void *)st.pixelPipelineCode;
        st.pixelPipelineDirty = 0;
        grCheckPixelPipeConsistency(&st);

        /* Second call hits grCacheFind and returns the exact cached entry */
        st.pixelPipelineCode = NULL;
        grEnsureValidPixelPipeline(&st);
        CHECK(st.compilationFailed == 0);
        CHECK((void *)st.pixelPipelineCode == firstCode);

        /* Cache miss #2: enable GR_CAPABILITY_FOG -> compiles a new shader
         * and activates fog gradient (bit 1) alongside depth + R,G,B (0x1F)! */
        st.fogColor = 0x11223344u;
        grSetCapability(&st, GR_CAPABILITY_FOG, 1);
        grEnsureValidPixelPipeline(&st);
        CHECK(st.compilationFailed == 0);
        CHECK(st.pixelPipelineCode != NULL);
        CHECK((void *)st.pixelPipelineCode != firstCode);
        CHECK(st.activeGradientMask == 0x1F);
        CHECK(st.numActiveGradients == 5);
    }
}

/* --- grrTexture + grrBlit + grSetupTextureUnit ----------------------------- */

static void test_grr_texture_and_blit(void)
{
    int minWork = grGetMinWorkMemorySize();
    unsigned char *workMem = (unsigned char *)malloc((size_t)minWork + 64u);
    Gerbera_s *st;
    _grTexture tex;
    _grSurfaceDescriptor desc;

    grrSelfTest();
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_ALPHA8) == 8);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8) == 16);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_RGB8) == 32);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_RGBA8) == 32);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_RGB565) == 16);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_PALETTE4_RGB8) == 4);
    CHECK(grrGetBitsPerPixel(GR_TEXTUREFORMAT_PALETTE8_RGBA8) == 8);

    st = grCreateGerbera(minWork, workMem, NULL, 0);
    CHECK(st != NULL);

    /* Allocate 4x4 RGBA8 level 0, populate 2x2 constant quadrants, generate mipmaps */
    grInitTexture(&tex);
    grSetTextureTarget(&tex, GR_TEXTURETARGET_2D);
    CHECK(grrSetupTextureLevel(&tex, 0, 4, 4, GR_TEXTUREFORMAT_RGBA8) == 1);
    {
        int dim[2];
        unsigned int *p0 = (unsigned int *)grGetTextureMipLevel(&tex, 0, dim);
        unsigned int *p1;
        unsigned int *p2;
        int x, y;

        CHECK(p0 != NULL && dim[0] == 2 && dim[1] == 2);
        for (y = 0; y < 4; ++y)
        {
            for (x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2)
                    p0[y * 4 + x] = grSetRGBA(200, 0, 0, 255);
                else if (x >= 2 && y < 2)
                    p0[y * 4 + x] = grSetRGBA(0, 200, 0, 255);
                else if (x < 2 && y >= 2)
                    p0[y * 4 + x] = grSetRGBA(0, 0, 200, 255);
                else
                    p0[y * 4 + x] = grSetRGBA(200, 200, 200, 255);
            }
        }

        CHECK(grrCalcTextureMipmaps(st, &tex) == 1);
        grUpdateTexture(&tex);
        CHECK(tex.numMipLevels == 3);

        p1 = (unsigned int *)grGetTextureMipLevel(&tex, 1, dim);
        CHECK(p1 != NULL && dim[0] == 1 && dim[1] == 1);
        CHECK(p1[0] == grSetRGBA(200, 0, 0, 255));
        CHECK(p1[1] == grSetRGBA(0, 200, 0, 255));
        CHECK(p1[2] == grSetRGBA(0, 0, 200, 255));
        CHECK(p1[3] == grSetRGBA(200, 200, 200, 255));

        p2 = (unsigned int *)grGetTextureMipLevel(&tex, 2, dim);
        CHECK(p2 != NULL && dim[0] == 0 && dim[1] == 0);
        CHECK(p2[0] == grSetRGBA(100, 100, 100, 255));
    }

    /* grSetupTextureUnit: verify LOD selection (level 0 vs level 2) & wrap mask */
    {
        TEST_RENDER_STACK(stack);
        _grPixelPipe *pp = (_grPixelPipe *)(stack + TEST_PIXEL_PIPE_OFFSET);
        int pairs[2][2] = { { 0x80000, 0 }, { 0x80000, 0 } };

        memset(stack, 0, sizeof(stackStorage.bytes));
        st->renderStack = stack;
        st->renderStackAllocated = 1;
        st->pixelPipeline = pp;
        st->activeGradientMask = 0xC0;
        grSetActiveTexture(st, 0, &tex);
        grSetTextureFilter(&tex, GR_FILTERTYPE_MIN,
                           GR_FILTERMASK_FILTER | GR_FILTERMASK_USE_MIPMAPS);
        grSetTextureWrapMode(&tex, 0, GR_WRAPMODE_REPEAT);
        grSetTextureWrapMode(&tex, 1, GR_WRAPMODE_CLAMP_TO_EDGE);
        grUpdateTexture(&tex);

        /* Minification with huge s/t step selects smallest 1x1 mip level (dim=0) */
        pp->xAdder[6] = 0x400000;
        pp->yAdder[7] = 0x400000;
        grSetupTextureUnit(st, 0, pairs, 0, 0);
        CHECK(pp->textureUnit[0].log2DimS == 0);
        CHECK(pp->textureUnit[0].sMask == 0);
        CHECK(pp->textureUnit[0].tMask == 0);

        /* Magnification (tiny s/t step) selects 4x4 base mip level (dim=2) */
        pp->xAdder[6] = 0x100;
        pp->yAdder[7] = 0x100;
        grSetupTextureUnit(st, 0, pairs, 0, 0);
        CHECK(pp->textureUnit[0].log2DimS == 2);
        CHECK(pp->textureUnit[0].sMask == 3);
        CHECK(pp->textureUnit[0].tMask == 3);
        CHECK(pp->textureUnit[0].rShift[0] == 30); /* REPEAT: 32 - 2 */
        CHECK(pp->textureUnit[0].rShift[1] == 19); /* CLAMP_TO_EDGE: 21 - 2 */
        CHECK(pp->textureUnit[0].linear == 1);     /* mag filter init = 1 (FILTER) */

        st->activeGradientMask = 0;
        st->renderStack = NULL;
        st->renderStackAllocated = 0;
        st->pixelPipeline = NULL;
    }

    /* grrBlit: memory RGBA8 -> RGB565 memory image & read-surface Y-flip */
    {
        unsigned int srcPx[4] =
        {
            grSetRGBA(255, 0, 0, 255),     grSetRGBA(0, 255, 0, 255),
            grSetRGBA(0, 0, 255, 255),     grSetRGBA(255, 255, 255, 255)
        };
        unsigned short dstPx[4] = { 0, 0, 0, 0 };
        grBlitImage_s srcImg, dstImg;
        grBlitInfo_s info;

        memset(&srcImg, 0, sizeof(srcImg));
        memset(&dstImg, 0, sizeof(dstImg));
        memset(&info, 0, sizeof(info));

        srcImg.type = GR_BLITIMAGE_MEMORY_IMAGE;
        srcImg.memoryImage.width = 2;
        srcImg.memoryImage.height = 2;
        srcImg.memoryImage.stride = 8;
        srcImg.memoryImage.bytesPerPixel = 4;
        srcImg.memoryImage.data = srcPx;
        srcImg.memoryImage.useDescriptor = 0;
        srcImg.memoryImage.dataType = GR_DATATYPE_UINT8;
        srcImg.memoryImage.imageFormat = GR_IMAGEFORMAT_RGBA;

        grrGetTextureFormatDescriptor(&desc, GR_TEXTUREFORMAT_RGB565);
        dstImg.type = GR_BLITIMAGE_MEMORY_IMAGE;
        dstImg.memoryImage.width = 2;
        dstImg.memoryImage.height = 2;
        dstImg.memoryImage.stride = 4;
        dstImg.memoryImage.bytesPerPixel = 2;
        dstImg.memoryImage.data = dstPx;
        dstImg.memoryImage.useDescriptor = 1;
        dstImg.memoryImage.descriptor = desc;

        grSetRectangle(&info.srcRect, 0, 2, 0, 2);
        grSetRectangle(&info.dstRect, 0, 2, 0, 2);
        info.srcImage = &srcImg;
        info.dstImage = &dstImg;
        grrBlit(st, &info);

        CHECK(dstPx[0] == 0xF800u);
        CHECK(dstPx[1] == 0x07E0u);
        CHECK(dstPx[2] == 0x001Fu);
        CHECK(dstPx[3] == 0xFFFFu);
    }

    grrFreeTextureData(&tex);

    /* grrSetPalettedTexture: 4-bit 16-entry RGB8 (3-byte packed) palette + 2x2 indices */
    {
        unsigned char palBlob[16 * 3 + 2];
        int e;

        memset(palBlob, 0, sizeof(palBlob));
        for (e = 0; e < 16; ++e)
        {
            palBlob[3 * e + 0] = (unsigned char)(e * 17);
            palBlob[3 * e + 1] = (unsigned char)(255 - e * 17);
            palBlob[3 * e + 2] = 0x40u;
        }
        palBlob[48] = 0x12u;
        palBlob[49] = 0xF0u;

        grInitTexture(&tex);
        grSetTextureTarget(&tex, GR_TEXTURETARGET_2D);
        grrGetTextureFormatDescriptor(&desc, GR_TEXTUREFORMAT_PALETTE4_RGB8);
        CHECK(grrSetPalettedTexture(&tex, &desc, GR_TEXTUREFORMAT_PALETTE4_RGB8,
                                    2, 2, 1, palBlob) == 1);
        CHECK(tex.palette != NULL);
        CHECK(((unsigned int *)tex.palette)[0] == grSetRGBA(0, 255, 0x40, 0));
        CHECK(((unsigned int *)tex.palette)[15] == grSetRGBA(255, 0, 0x40, 0));
        grrFreeTextureData(&tex);
        CHECK(tex.palette == NULL);
    }

    grDestroyGerbera(st);
    CHECK(workMem[0] == 0xBEu);
    free(workMem);
}

/* Exercise the host pixel pipeline with texture coordinates prepared by the
 * recovered setup routine and code cached by the normal compiler path. */
#if !defined(__SYMBIAN32__) && !(defined(__arm__) && UINTPTR_MAX == 0xFFFFFFFFu)
static unsigned int sample_pixel(Gerbera_s *st, _grTexture *tex, int s, int t,
                                 int linear, int wrap)
{
    _grPixelPipe pp;
    unsigned int span = 1u << 16;
    unsigned int pixel = 0;
    int coords[2][2] = { { s, 0 }, { t, 0 } };

    grSetTextureFilter(tex, GR_FILTERTYPE_MIN, linear);
    grSetTextureFilter(tex, GR_FILTERTYPE_MAG, linear);
    grSetTextureWrapMode(tex, 0, wrap);
    grSetTextureWrapMode(tex, 1, wrap);
    grUpdateTexture(tex);
    grSetActiveTexture(st, 0, tex);
    st->useTexUnit[0] = 1;
    st->textureUnitEnabled[0] = 1;
    grEnsureValidPixelPipeline(st);
    CHECK(st->compilationFailed == 0 && st->pixelPipelineCode != NULL);

    memset(&pp, 0, sizeof(pp));
    st->pixelPipeline = &pp;
    st->activeGradientMask = 0xC0;
    grSetupTextureUnit(st, 0, coords, 4096, 4096);
    CHECK(pp.textureUnit[0].linear == linear);
    pp.colorRow = &pixel;
    pp.numScanlines = 1;
    pp.scanlineInfo = &span;
    grExecutePixelPipeline(st);
    st->pixelPipeline = NULL;
    return pixel;
}

static void test_pixel_sampling(void)
{
    int workSize = grGetMinWorkMemorySize();
    unsigned char *work = (unsigned char *)malloc((size_t)workSize);
    Gerbera_s *st = grCreateGerbera(workSize, work, NULL, 0);
    _grTexture tex;
    _grSurfaceDescriptor desc;
    int c;

    CHECK(st != NULL);
    if (st == NULL)
    {
        free(work);
        return;
    }
    grrGetTextureFormatDescriptor((_grSurfaceDescriptor *)&st->drawSurface,
                                  GR_TEXTUREFORMAT_RGBA8);
    st->drawSurface.width = st->drawSurface.height = 1;
    st->drawSurface.buffers[0].stride = 4;
    for (c = 0; c < 4; ++c)
        st->defaultColorRGBA[c] = 0xFF00;
    grDisableCapability(st, GR_CAPABILITY_DITHER);
    grSetTexEnvMode(st, 0, GR_TEXENVMODE_MODULATE);
    grInitTexture(&tex);
    grSetTextureTarget(&tex, GR_TEXTURETARGET_2D);
    CHECK(grrSetupTextureLevel(&tex, 0, 2, 2, GR_TEXTUREFORMAT_RGBA8));
    {
        unsigned int *p = (unsigned int *)tex.mipmap[0].pixels;
        p[0] = 0xFFFF0000u;
        p[1] = 0xFF00FF00u;
        p[2] = 0xFF0000FFu;
        p[3] = 0xFFFFFFFFu;
    }
    /* RGBA8 MODULATE enters the optimized path when filtering is nearest. */
    CHECK(sample_pixel(st, &tex, 0x100000, 0x100000, 0, GR_WRAPMODE_REPEAT) == 0xFFFFFFFFu);
    CHECK(sample_pixel(st, &tex, 0x100000, 0x100000, 1, GR_WRAPMODE_REPEAT) == 0xFF7F7F7Fu);
    CHECK(sample_pixel(st, &tex, 0x80000, 0x80000, 1, GR_WRAPMODE_REPEAT) == 0xFFFF0000u);
    CHECK(sample_pixel(st, &tex, 0xC0000, 0x80000, 1, GR_WRAPMODE_REPEAT) == 0xFFBF3F00u);
    CHECK(sample_pixel(st, &tex, 0, 0, 1, GR_WRAPMODE_REPEAT) == 0xFF7F7F7Fu);
    CHECK(sample_pixel(st, &tex, 0, 0, 1, GR_WRAPMODE_CLAMP_TO_EDGE) == 0xFFFF0000u);
    CHECK(sample_pixel(st, &tex, 0x200000, 0x200000, 1, GR_WRAPMODE_CLAMP_TO_EDGE) == 0xFFFFFFFFu);
    CHECK(sample_pixel(st, &tex, 0x200000, 0x200000, 1, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFFFFFFFFu);
    CHECK(sample_pixel(st, &tex, 0x300000, 0x300000, 1, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFF7F7F7Fu);
    CHECK(sample_pixel(st, &tex, 0x280000, 0x80000, 0, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFF00FF00u);
    CHECK(sample_pixel(st, &tex, 0x480000, 0x80000, 0, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFFFF0000u);
    CHECK(sample_pixel(st, &tex, -0x80000, 0x80000, 0, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFFFF0000u);
    CHECK(sample_pixel(st, &tex, -0x180000, 0x80000, 0, GR_WRAPMODE_MIRRORED_REPEAT) == 0xFF00FF00u);
    {
        _grPixelPipe pp;
        unsigned int pixels[10];
        unsigned int spans[2] = { (2u << 16) | 3u, 5u << 16 };
        int coords[2][2] = { { 0, 0 }, { 0, 0 } };
        int env;

        /* Repeated coordinates cross the signed boundary in both axes.
         * MODULATE exercises the fast path, REPLACE the general path. */
        grSetTextureWrapMode(&tex, 0, GR_WRAPMODE_REPEAT);
        grSetTextureWrapMode(&tex, 1, GR_WRAPMODE_REPEAT);
        grUpdateTexture(&tex);
        st->drawSurface.width = 5;
        st->drawSurface.height = 2;
        st->drawSurface.buffers[0].stride = 5 * sizeof(unsigned int);
        for (env = 0; env < 2; ++env)
        {
            grSetTexEnvMode(st, 0, env ? GR_TEXENVMODE_REPLACE : GR_TEXENVMODE_MODULATE);
            grEnsureValidPixelPipeline(st);
            CHECK(st->compilationFailed == 0 && st->pixelPipelineCode != NULL);
            memset(&pp, 0, sizeof(pp));
            memset(pp.reference, 0xAB, sizeof(pp.reference));
            memset(pp.xAdder, 0xAB, sizeof(pp.xAdder));
            memset(pp.yAdder, 0xAB, sizeof(pp.yAdder));
            memset(pixels, 0, sizeof(pixels));
            st->pixelPipeline = &pp;
            st->activeGradientMask = 0xC0;
            pp.xAdder[6] = pp.xAdder[7] = 0;
            pp.yAdder[6] = pp.yAdder[7] = 0;
            grSetupTextureUnit(st, 0, coords, 4096, 4096);
            pp.reference[6] = pp.reference[7] = 0x40000000;
            pp.xAdder[6] = (int)0x80000000u;
            pp.yAdder[6] = pp.yAdder[7] = (int)0x80000000u;
            pp.colorRow = pixels;
            pp.numScanlines = 2;
            pp.scanlineInfo = spans;
            grExecutePixelPipeline(st);
            CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
            CHECK(pixels[3] == 0xFF00FF00u && pixels[4] == 0xFFFF0000u);
            CHECK(pixels[5] == 0xFFFFFFFFu && pixels[6] == 0xFF0000FFu);
            CHECK(pixels[7] == 0xFFFFFFFFu && pixels[8] == 0xFF0000FFu);
            CHECK(pixels[9] == 0xFFFFFFFFu);
        }
        st->pixelPipeline = NULL;
        st->drawSurface.width = st->drawSurface.height = 1;
        st->drawSurface.buffers[0].stride = sizeof(unsigned int);
        grSetTexEnvMode(st, 0, GR_TEXENVMODE_MODULATE);
    }
    {
        TEST_RENDER_STACK(renderStack);
        _grPixelPipe pp;
        unsigned int pixels[8] = { 0 };
        unsigned short depth[8] = { 0 };
        unsigned char stencil[8] = { 0 };

        /* Negative crop origins repeat the same texels. Starting at row two
         * also exercises the wrapped texture-gradient yStart multiply. */
        tex.cropRect.x0 = tex.cropRect.y0 = -2;
        tex.cropRect.x1 = tex.cropRect.y1 = 0;
        st->drawSurface.width = 2;
        st->drawSurface.height = 4;
        st->drawSurface.buffers[0].data = pixels;
        st->drawSurface.buffers[0].stride = 2 * sizeof(unsigned int);
        st->drawSurface.buffers[1].data = depth;
        st->drawSurface.buffers[1].stride = 2 * sizeof(unsigned short);
        st->drawSurface.buffers[2].data = stencil;
        st->drawSurface.buffers[2].stride = 2;
        grSetRectangle(&st->surfaceScissor, 0, 2, 0, 4);
        grEnsureValidPixelPipeline(st);
        memset(&pp, 0, sizeof(pp));
        st->pixelPipeline = &pp;
        st->renderStack = renderStack;
        st->activeGradientMask = 0xC0;
        st->numActiveGradients = 2;
        st->activeGradientIndex[0] = 6;
        st->activeGradientIndex[1] = 7;
        grRenderTextureRect(st, IScalar_zero(), IScalar_zero(),
                            IScalar_intToScalar(2), IScalar_intToScalar(2), IScalar_zero());
        CHECK(pp.yStart == 2 && pp.numScanlines == 2);
        CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 0);
        CHECK(pixels[4] == 0xFF0000FFu && pixels[5] == 0xFFFFFFFFu);
        CHECK(pixels[6] == 0xFFFF0000u && pixels[7] == 0xFF00FF00u);
        st->pixelPipeline = NULL;
        st->renderStack = NULL;
        st->drawSurface.width = st->drawSurface.height = 1;
        st->drawSurface.buffers[0].stride = sizeof(unsigned int);
        st->drawSurface.buffers[0].data = NULL;
        st->drawSurface.buffers[1].data = NULL;
        st->drawSurface.buffers[2].data = NULL;
    }
    grrFreeTextureData(&tex);

    /* One-texel axes have no coordinate shift or fractional bits. */
    CHECK(grrSetupTextureLevel(&tex, 0, 2, 1, GR_TEXTUREFORMAT_RGB565));
    ((unsigned short *)tex.mipmap[0].pixels)[0] = 0xF800;
    ((unsigned short *)tex.mipmap[0].pixels)[1] = 0x001F;
    CHECK(sample_pixel(st, &tex, 0x100000, -0x200000, 1, GR_WRAPMODE_REPEAT) == 0xFF7F007Fu);
    grrFreeTextureData(&tex);
    CHECK(grrSetupTextureLevel(&tex, 0, 1, 2, GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8));
    ((unsigned short *)tex.mipmap[0].pixels)[0] = 0xFF00;
    ((unsigned short *)tex.mipmap[0].pixels)[1] = 0x00FF;
    CHECK(sample_pixel(st, &tex, -0x200000, 0x100000, 1, GR_WRAPMODE_REPEAT) == 0x7F7F7F7Fu);
    grrFreeTextureData(&tex);
    CHECK(grrSetupTextureLevel(&tex, 0, 1, 1, GR_TEXTUREFORMAT_ALPHA8));
    *(unsigned char *)tex.mipmap[0].pixels = 73;
    CHECK(sample_pixel(st, &tex, -0x200000, 0x600000, 1, GR_WRAPMODE_REPEAT) == 0x49FFFFFFu);
    grrFreeTextureData(&tex);

    /* The original palette fetch shifts the even texel by four bits.
     * Distinct indices catch pair reversal; filtering uses palette colors. */
    {
        unsigned char blob[16 * 4 + 1];
        memset(blob, 0, sizeof(blob));
        blob[4] = 255;
        blob[7] = 255;
        blob[10] = 255;
        blob[11] = 255;
        blob[64] = 0x12;
        grrGetTextureFormatDescriptor(&desc, GR_TEXTUREFORMAT_PALETTE4_RGBA8);
        CHECK(grrSetPalettedTexture(&tex, &desc, GR_TEXTUREFORMAT_PALETTE4_RGBA8,
                                    2, 1, 1, blob));
        CHECK(sample_pixel(st, &tex, 0x80000, 0, 0, GR_WRAPMODE_REPEAT) == 0xFFFF0000u);
        CHECK(sample_pixel(st, &tex, 0x180000, 0, 0, GR_WRAPMODE_REPEAT) == 0xFF0000FFu);
        CHECK(sample_pixel(st, &tex, 0x100000, 0, 1, GR_WRAPMODE_REPEAT) == 0xFF7F007Fu);
    }
    grrFreeTextureData(&tex);
    /* Original ARM pixel programs truncate source and destination blend
     * contributions separately. Exercise the optimized and general paths. */
    {
        _grPixelPipe pipe;
        unsigned int span = 1u << 16;
        unsigned int pixel;
        unsigned char stencil = 0;
        int general;
        st->useTexUnit[0] = st->useTexUnit[1] = 0;
        st->textureUnitEnabled[0] = st->textureUnitEnabled[1] = 0;
        st->textureUnit[0].texture = st->textureUnit[2].texture = NULL;
        st->activeGradientMask = 0x3C;
        st->colorMask = 15;
        st->drawSurface.stencilBytes = 1;
        st->stencilFunc = GR_FUNC_ALWAYS;
        st->stencilFail = st->stencilZFail = st->stencilZPass = GR_STENCILOP_KEEP;
        grEnableCapability(st, GR_CAPABILITY_BLEND);
        st->srcBlendFactor = GR_BLENDFACTOR_SRC_ALPHA;
        st->dstBlendFactor = GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        for (general = 0; general < 2; ++general)
        {
            memset(&pipe, 0, sizeof(pipe));
            pixel = 0x8F3A716Cu;
            pipe.colorRow = &pixel;
            pipe.stencilRow = &stencil;
            pipe.numScanlines = 1;
            pipe.scanlineInfo = &span;
            pipe.reference[2] = 80 << 16;
            pipe.reference[3] = 40 << 16;
            pipe.reference[4] = 180 << 16;
            pipe.reference[5] = 64 << 16;
            st->pixelPipeline = &pipe;
            grSetCapability(st, GR_CAPABILITY_STENCIL_TEST, general);
            grExecutePixelPipeline(st);
            CHECK(pixel == 0x7B3F5E7Eu);
        }
        pixel = 0x8F3A7172u;
        pipe.reference[2] = 98 << 16;
        pipe.reference[3] = 61 << 16;
        pipe.reference[4] = 160 << 16;
        pipe.reference[5] = 131 << 16;
        st->srcBlendFactor = GR_BLENDFACTOR_SRC_ALPHA_SATURATE;
        st->dstBlendFactor = GR_BLENDFACTOR_ONE;
        grExecutePixelPipeline(st);
        CHECK(pixel == 0xFF658CB8u);
        st->pixelPipeline = NULL;
    }
    grDestroyGerbera(st);
    free(work);
}
#else
static void test_pixel_sampling(void) { }
#endif

static void test_gerbera_lifecycle_and_api_small(void)
{
    int minWork = grGetMinWorkMemorySize();
    int minShared = grGetMinSharedMemorySize();
    unsigned char *workMem = (unsigned char *)malloc((size_t)minWork + 64u);
    Gerbera_s *st;
    _grDrawSurface surf;
    unsigned short colorBuf[16 * 16];
    int attn[3] = { IScalar_immediate(1.0f), IScalar_immediate(0.5f), IScalar_immediate(0.25f) };
    int norm[3] = { 0, 0, 0x8000 };
    int tc[4]   = { IScalar_immediate(0.5f), IScalar_immediate(0.75f), IScalar_zero(), IScalar_one() };
    _XColor3 vcol;

    CHECK(minWork > (int)sizeof(Gerbera_s));
    CHECK(minShared > 0);

    st = grCreateGerbera(minWork, workMem, NULL, 0);
    CHECK(st != NULL);
    CHECK(st->pipelineCache->entryCount == GR_PIPELINE_CACHE_ENTRIES);
    CHECK(st->pipelineCache->arenaSize == GR_PIPELINE_CACHE_BYTES);
    CHECK(st->scratchMemory != NULL && st->scratchMemorySize == 30720);
    CHECK(grTestCapability(st, GR_CAPABILITY_SMOOTH_SHADING) == 1);

    /* Surface binding */
    memset(&surf, 0, sizeof(surf));
    grrGetTextureFormatDescriptor((_grSurfaceDescriptor *)&surf, GR_TEXTUREFORMAT_RGB565);
    surf.width = 16;
    surf.height = 16;
    surf.buffers[GR_BUFFERTYPE_COLOR].data = colorBuf;
    surf.buffers[GR_BUFFERTYPE_COLOR].stride = 32;
    CHECK(grIsValidSurfaceInfo(&surf) == 1);
    grSetSurfaces(st, &surf, &surf);
    CHECK(st->drawSurface.width == 16 && st->drawSurface.height == 16);

    /* TexEnv getters/setters */
    grSetTexEnvColor(st, 0, 0x80402010u);
    CHECK(grGetTexEnvColor(st, 0) == 0x80402010u);
    grSetTexEnvMode(st, 0, GR_TEXENVMODE_COMBINE);
    CHECK(grGetTexEnvMode(st, 0) == GR_TEXENVMODE_COMBINE);
    grSetTexEnvRGBCombFunc(st, 0, GR_TEXENVCOMBFUNC_INTERPOLATE);
    CHECK(grGetTexEnvRGBCombFunc(st, 0) == GR_TEXENVCOMBFUNC_INTERPOLATE);
    grSetTexEnvAlphaCombFunc(st, 0, GR_TEXENVCOMBFUNC_ADD_SIGNED);
    CHECK(grGetTexEnvAlphaCombFunc(st, 0) == GR_TEXENVCOMBFUNC_ADD_SIGNED);
    grSetTexEnvRGBCombSrc(st, 0, 2, GR_TEXENVCOMBSRC_CONSTANT);
    CHECK(grGetTexEnvRGBCombSrc(st, 0, 2) == GR_TEXENVCOMBSRC_CONSTANT);
    grSetTexEnvAlphaCombSrc(st, 0, 1, GR_TEXENVCOMBSRC_PRIMARY_COLOR);
    CHECK(grGetTexEnvAlphaCombSrc(st, 0, 1) == GR_TEXENVCOMBSRC_PRIMARY_COLOR);
    grSetTexEnvRGBCombOperand(st, 0, 0, GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_COLOR);
    CHECK(grGetTexEnvRGBCombOperand(st, 0, 0) == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_COLOR);
    grSetTexEnvAlphaCombOperand(st, 0, 1, GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA);
    CHECK(grGetTexEnvAlphaCombOperand(st, 0, 1) == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA);
    grSetTexEnvRGBCombScale(st, 0, 2);
    CHECK(grGetTexEnvRGBCombScale(st, 0) == 2);
    grSetTexEnvAlphaCombScale(st, 0, 4);
    CHECK(grGetTexEnvAlphaCombScale(st, 0) == 4);
    grSetTexCoordReplace(st, 1, 1);
    CHECK(grGetTexCoordReplace(st, 1) == 1);

    /* Vertex array, point/line parameters, and immediate vertex attributes */
    grSetVertexArray(st, GR_VERTEXARRAY_POSITION, GR_DATATYPE_FLOAT, 3, 12,
                     colorBuf, 7u, (void *)0x100u);
    CHECK(st->vertexArray[GR_VERTEXARRAY_POSITION].size == 3);
    CHECK(st->vertexArray[GR_VERTEXARRAY_POSITION].stride == 12);
    CHECK(st->vertexArray[GR_VERTEXARRAY_POSITION].binding == 7u);

    grSetPointSize(st, 0x40000);
    grSetPointSizeMin(st, IScalar_immediate(1.0f));
    grSetPointSizeMax(st, IScalar_immediate(32.0f));
    grSetPointFadeThreshold(st, IScalar_immediate(1.5f));
    grSetPointDistAttn(st, attn);
    grSetLineWidth(st, 0x20000);
    CHECK(st->pointSize == 0x40000);
    CHECK(st->pointFadeThreshold == IScalar_immediate(1.5f));
    CHECK(st->lineWidth == 0x20000);

    XColor3_setf(&vcol, hgFloatBitPattern(0.25f), hgFloatBitPattern(0.5f),
                hgFloatBitPattern(0.75f));
    grSetVertexColor(st, &vcol, 0x8000u);
    grSetVertexNormal(st, norm);
    grSetVertexTexCoord(st, 0, tc);
    CHECK(st->defaultTexCoord.x[0][0] == IScalar_immediate(0.5f));
    CHECK(st->defaultTexCoord.x[0][1] == IScalar_immediate(0.75f));

    grDestroyGerbera(st);
    free(workMem);
}



