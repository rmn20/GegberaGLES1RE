/* oglContext.c - OpenGL ES 1.1 context lifecycle, shared-list / shared-memory
 * linkage, matrix stack arena setup, surface descriptors, and render-texture
 * binding (oglContext.inl).
 *
 * Assert sites keep the original oglContext.inl file/line strings.
 */
#include "gerbera/api_opengl/oglContext.h"
#include "hg/hgInt32.h"
#include "hg/hgFloat.h"
#include "hg/hgFill.h"
#include "hg/hgMemory.h"
#include "imath/imApi.h"

oglContext_s *oglGetContextAndState(eglState_s **outState)
{
    eglState_s *state = egliGetState();
    if (outState != NULL)
        *outState = state;
    if (state != NULL)
        return state->currentESContext;
    eglSetError(0x3001); /* EGL_NOT_INITIALIZED */
    return NULL;
}

oglContext_s *oglGetContext(void)
{
    return oglGetContextAndState(NULL);
}

oglMatrixStack_s *oglInitMatrixStack(oglContext_s *context, int depth)
{
    oglMatrixStack_s *stack;

    OGL_ASSERT_AT("oglContext.inl", 46, "context", context != NULL);
    OGL_ASSERT_AT("oglContext.inl", 47, "hgInRange32(depth, 1, 255)",
                  hgInRange32(depth, 1, 255) != 0);
    stack = (oglMatrixStack_s *)grStackAllocate(&context->allocator, 80 * (depth - 1) + 84);
    stack->dirty = 1;
    stack->depth = (unsigned char)depth;
    stack->index = 0;
    IMatrix4x4_identity(&stack->elements[0]);
    return stack;
}

int oglInitContext(oglContext_s *context, unsigned char *sharedWorkMem, int sharedWorkMemSize)
{
    int i, j;
    int alignRem;
    int freeSpace;
    unsigned char *gerberaMem;
    Gerbera_s *gerbera;
    _XColor3 colZero, colOne, col02, col08;
    unsigned int zeroBits, oneBits, p2Bits, p8Bits;

    context->matrixStack[GR_MATRIXMODE_MODELVIEW] = oglInitMatrixStack(context, 16);
    context->matrixStack[GR_MATRIXMODE_PROJECTION] = oglInitMatrixStack(context, 2);
    for (i = 0; i < 2; ++i)
        context->matrixStack[GR_MATRIXMODE_TEXTURE0 + i] = oglInitMatrixStack(context, 2);
    for (i = 0; i < 9; ++i)
        context->matrixStack[GR_MATRIXMODE_PALETTE0 + i] = oglInitMatrixStack(context, 1);

    /* Ensure pointer-sized alignment before carving Gerbera_s out of the stack */
    alignRem = context->allocator.allocated & ((int)sizeof(void *) - 1);
    if (alignRem != 0)
        grStackAllocate(&context->allocator, (int)sizeof(void *) - alignRem);

    freeSpace = grStackGetFreeSpace(&context->allocator);
    gerberaMem = (unsigned char *)grStackAllocate(&context->allocator, freeSpace);
    OGL_ASSERT_AT("oglContext.inl", 94, "freeSpace >= grGetMinWorkMemorySize()",
                  freeSpace >= grGetMinWorkMemorySize());

    gerbera = grCreateGerbera(freeSpace, gerberaMem, sharedWorkMem, sharedWorkMemSize);
    context->gerbera = gerbera;
    if (gerbera == NULL)
        return 0;

    for (i = 0; i < GR_TEXTURETARGET_MAX; ++i)
    {
        for (j = 0; j < GR_MAX_TEXTURE_UNITS; ++j)
            oglBindTexture(context, j, i, 0);
    }

    context->pixelStorePackAlignment = 4;
    context->pixelStoreUnpackAlignment = 4;

    zeroBits = hgFloatBitPattern(0.0f);
    oneBits  = hgFloatBitPattern(1.0f);
    p2Bits   = hgFloatBitPattern(0.2f);
    p8Bits   = hgFloatBitPattern(0.8f);
    XColor3_setf(&colZero, zeroBits, zeroBits, zeroBits);
    XColor3_setf(&colOne,  oneBits,  oneBits,  oneBits);
    XColor3_setf(&col02,   p2Bits,   p2Bits,   p2Bits);
    XColor3_setf(&col08,   p8Bits,   p8Bits,   p8Bits);

    grSetGlobalAmbient(gerbera, &col02);
    grSetMaterialColor(gerbera, GR_MATERIALCOLOR_AMBIENT,  &col02);
    grSetMaterialColor(gerbera, GR_MATERIALCOLOR_DIFFUSE,  &col08);
    grSetMaterialColor(gerbera, GR_MATERIALCOLOR_SPECULAR, &colZero);
    grSetMaterialColor(gerbera, GR_MATERIALCOLOR_EMISSION, &colZero);
    grSetMaterialShininess(gerbera, IScalar_zero());
    grSetMaterialAlpha(gerbera, 0xFFFFu);

    context->materialColors[GR_MATERIALCOLOR_AMBIENT][0]  = 0.2f;
    context->materialColors[GR_MATERIALCOLOR_AMBIENT][1]  = 0.2f;
    context->materialColors[GR_MATERIALCOLOR_AMBIENT][2]  = 0.2f;
    context->materialColors[GR_MATERIALCOLOR_AMBIENT][3]  = 1.0f;
    context->materialColors[GR_MATERIALCOLOR_DIFFUSE][0]  = 0.8f;
    context->materialColors[GR_MATERIALCOLOR_DIFFUSE][1]  = 0.8f;
    context->materialColors[GR_MATERIALCOLOR_DIFFUSE][2]  = 0.8f;
    context->materialColors[GR_MATERIALCOLOR_DIFFUSE][3]  = 1.0f;
    context->materialColors[GR_MATERIALCOLOR_SPECULAR][0] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_SPECULAR][1] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_SPECULAR][2] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_SPECULAR][3] = 1.0f;
    context->materialColors[GR_MATERIALCOLOR_EMISSION][0] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_EMISSION][1] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_EMISSION][2] = 0.0f;
    context->materialColors[GR_MATERIALCOLOR_EMISSION][3] = 1.0f;

    for (i = 0; i < GR_MAX_LIGHTS; ++i)
    {
        int posRaw[4];
        _IFVector4 pos4;
        int dirRaw[3];
        _IFVector3 dir3;
        int norm3[3];

        for (j = 0; j <= GR_MATERIALCOLOR_SPECULAR; ++j)
        {
            grSetLightColor(gerbera, i, j, &colZero);
            context->lightAlphas[i][0] = 1.0f;
            context->lightAlphas[i][1] = 0.0f;
            context->lightAlphas[i][2] = 0.0f;
        }

        posRaw[0] = 0;
        posRaw[1] = 0;
        posRaw[2] = (int)hgFloatBitPattern(1.0f);
        posRaw[3] = 0;
        IFVector4_setfv(&pos4, posRaw, 4);
        grSetLightPosition(gerbera, i, &pos4);

        dirRaw[0] = IScalar_zero();
        dirRaw[1] = IScalar_zero();
        dirRaw[2] = IScalar_negate(IScalar_one());
        IFVector3_setfv3(&dir3, dirRaw);
        XNormal3_init001n(norm3);
        grSetLightSpotDirection(gerbera, i, norm3, &dir3);
        grSetLightSpotCutoff(gerbera, i, IScalar_immediate(180.0f));
        grSetLightAttenuation(gerbera, i, 0, IScalar_one());
        grSetLightAttenuation(gerbera, i, 1, IScalar_zero());
        grSetLightAttenuation(gerbera, i, 2, IScalar_zero());
    }

    grSetLightColor(gerbera, 0, GR_MATERIALCOLOR_DIFFUSE,  &colOne);
    grSetLightColor(gerbera, 0, GR_MATERIALCOLOR_SPECULAR, &colOne);
    context->lightAlphas[0][1] = 1.0f;
    context->lightAlphas[0][2] = 1.0f;
    context->pointSizeMin = 0x10000;
    context->pointSizeMax = 0x400000;
    oglSetDefaultGetterValues(&context->getValues);
    return 1;
}

int oglIsValidContext(oglContext_s *context)
{
    if (context != NULL)
    {
        oglTextureManager *texManager = context->textureManager;
        oglContext_s *shareList = context;

        while (shareList->shareListPrev != NULL)
            shareList = shareList->shareListPrev;

        while (shareList->shareListNext != NULL)
        {
            OGL_ASSERT_AT("oglContext.inl", 287,
                          "shareList->textureManager == texManager",
                          shareList->textureManager == texManager);
            if (shareList->shareListPrev != NULL)
            {
                OGL_ASSERT_AT("oglContext.inl", 290,
                              "shareList->shareListPrev->shareListNext == shareList",
                              shareList->shareListPrev->shareListNext == shareList);
            }
            if (shareList->shareListNext != NULL)
            {
                OGL_ASSERT_AT("oglContext.inl", 292,
                              "shareList->shareListNext->shareListPrev == shareList",
                              shareList->shareListNext->shareListPrev == shareList);
            }
            shareList = shareList->shareListNext;
        }
    }
    return 1;
}

oglContext_s *oglCreateContext(oglContext_s *shareList, oglContext_s *shareMemContext)
{
    oglTextureManager *texManager;
    oglBufObjManager_s *bufObjManager;
    unsigned int sharedWorkMemSize;
    unsigned char *sharedWorkMem;
    int alignedCtxSize;
    int stacksPlusPad;
    int workMemorySize;
    unsigned char *workMemory;
    oglContext_s *context;

    if (shareList != NULL)
    {
        texManager = shareList->textureManager;
        bufObjManager = shareList->bufObjManager;
    }
    else
    {
        texManager = oglCreateTextureManager();
        if (texManager == NULL)
            return NULL;
        bufObjManager = oglCreateBufObjManager();
        if (bufObjManager == NULL)
        {
            oglDestroyTextureManager(texManager);
            return NULL;
        }
    }

    sharedWorkMemSize = (unsigned int)grGetMinSharedMemorySize();
    if (shareMemContext != NULL)
    {
        sharedWorkMem = shareMemContext->sharedWorkMem;
    }
    else
    {
        sharedWorkMem = (unsigned char *)eglAlloc(sharedWorkMemSize);
        if (sharedWorkMem == NULL)
        {
            if (shareList == NULL)
            {
                oglDestroyTextureManager(texManager);
                oglDestroyBufObjManager(bufObjManager);
            }
            return NULL;
        }
    }

    oglMappingTest();

    alignedCtxSize = ((int)sizeof(oglContext_s) + (int)sizeof(void *) - 1) &
                     ~((int)sizeof(void *) - 1);
    stacksPlusPad = (2532 + (int)sizeof(void *) - 1) & ~((int)sizeof(void *) - 1);
    workMemorySize = grGetMinWorkMemorySize() + alignedCtxSize + stacksPlusPad;
    workMemory = (unsigned char *)eglAlloc((unsigned int)workMemorySize);
    if (workMemory == NULL)
    {
        if (shareList == NULL)
        {
            oglDestroyTextureManager(texManager);
            oglDestroyBufObjManager(bufObjManager);
        }
        if (shareMemContext == NULL)
            eglFree(sharedWorkMem);
        return NULL;
    }

    context = (oglContext_s *)workMemory;
    hgFill8((HGuint8 *)context, 0, alignedCtxSize);
    context->workMemory = workMemory;
    context->textureManager = texManager;
    context->bufObjManager = bufObjManager;
    context->sharedWorkMem = sharedWorkMem;
    grStackInit(&context->allocator, workMemory, workMemorySize);
    grStackAllocate(&context->allocator, alignedCtxSize);

    if (oglInitContext(context, sharedWorkMem, (int)sharedWorkMemSize) == 0)
    {
        if (shareList == NULL)
        {
            oglDestroyTextureManager(texManager);
            oglDestroyBufObjManager(bufObjManager);
        }
        if (shareMemContext == NULL)
            eglFree(sharedWorkMem);
        eglFree(workMemory);
        return NULL;
    }

    if (!(context->shareListPrev == NULL && context->shareListNext == NULL))
        imAssert("!context->shareListPrev && !context->shareListNext", "oglContext.inl", 448);

    if (shareList != NULL)
    {
        while (shareList->shareListNext != NULL)
            shareList = shareList->shareListNext;
        shareList->shareListNext = context;
        context->shareListPrev = shareList;
    }
    if (shareMemContext != NULL)
    {
        while (shareMemContext->shareMemNext != NULL)
            shareMemContext = shareMemContext->shareMemNext;
        shareMemContext->shareMemNext = context;
        context->shareMemPrev = shareMemContext;
    }

    OGL_ASSERT_AT("oglContext.inl", 477, "oglIsValidContext(context)",
                  oglIsValidContext(context) != 0);
    return context;
}

void oglDestroyContext(oglContext_s *context)
{
    if (context != NULL)
    {
        OGL_ASSERT_AT("oglContext.inl", 500, "oglIsValidContext(context)",
                      oglIsValidContext(context) != 0);
        if (context->shareListPrev != NULL)
            context->shareListPrev->shareListNext = context->shareListNext;
        if (context->shareListNext != NULL)
            context->shareListNext->shareListPrev = context->shareListPrev;

        OGL_ASSERT_AT("oglContext.inl", 508, "oglIsValidContext(context->shareListPrev)",
                      oglIsValidContext(context->shareListPrev) != 0);
        OGL_ASSERT_AT("oglContext.inl", 509, "oglIsValidContext(context->shareListNext)",
                      oglIsValidContext(context->shareListNext) != 0);

        if (context->shareListPrev == NULL && context->shareListNext == NULL)
        {
            oglDestroyTextureManager(context->textureManager);
            oglDestroyBufObjManager(context->bufObjManager);
        }

        if (context->shareMemPrev != NULL)
            context->shareMemPrev->shareMemNext = context->shareMemNext;
        if (context->shareMemNext != NULL)
            context->shareMemNext->shareMemPrev = context->shareMemPrev;

        OGL_ASSERT_AT("oglContext.inl", 530, "oglIsValidContext(context->shareListPrev)",
                      oglIsValidContext(context->shareListPrev) != 0);
        OGL_ASSERT_AT("oglContext.inl", 531, "oglIsValidContext(context->shareListNext)",
                      oglIsValidContext(context->shareListNext) != 0);

        if (context->shareMemPrev == NULL && context->shareMemNext == NULL)
            eglFree(context->sharedWorkMem);

        grDestroyGerbera(context->gerbera);
        eglFree(context->workMemory);
    }
}

int oglIsValidSurfaceDescriptor(const oglSurfaceDescriptor *desc)
{
    OGL_ASSERT_AT("oglContext.inl", 554, "desc", desc != NULL);
    OGL_ASSERT_AT("oglContext.inl", 557,
                  "hgInRange32(desc->colorBytes, 0, 4) && hgIsPowerOfTwo32(desc->colorBytes)",
                  hgInRange32(desc->colorBytes, 0, 4) != 0 &&
                  hgIsPowerOfTwo32((unsigned int)desc->colorBytes) != 0);
    OGL_ASSERT_AT("oglContext.inl", 558,
                  "hgInRange32(desc->depthBytes, 0, 4) && hgIsPowerOfTwo32(desc->depthBytes)",
                  hgInRange32(desc->depthBytes, 0, 4) != 0 &&
                  hgIsPowerOfTwo32((unsigned int)desc->depthBytes) != 0);
    OGL_ASSERT_AT("oglContext.inl", 559,
                  "hgInRange32(desc->stencilBytes, 0, 1) && hgIsPowerOfTwo32(desc->stencilBytes)",
                  hgInRange32(desc->stencilBytes, 0, 1) != 0 &&
                  hgIsPowerOfTwo32((unsigned int)desc->stencilBytes) != 0);
    if (desc->width != 0 && desc->height != 0)
    {
        OGL_ASSERT_AT("oglContext.inl", 562,
                      "desc->depthBuffer ? desc->depthBytes : !desc->depthBytes",
                      desc->depthBuffer != NULL ? desc->depthBytes != 0 : desc->depthBytes == 0);
        OGL_ASSERT_AT("oglContext.inl", 563,
                      "desc->stencilBuffer ? desc->stencilBytes : !desc->stencilBytes",
                      desc->stencilBuffer != NULL ? desc->stencilBytes != 0 : desc->stencilBytes == 0);
    }
    return 1;
}

void oglConvertSurfaceDescriptor(_grDrawSurface *dst, const oglSurfaceDescriptor *src)
{
    int i;
    unsigned int colorMask = 0;

    OGL_ASSERT_AT("oglContext.inl", 578, "dst && src", dst != NULL && src != NULL);
    hgMemSet(dst, 0, (unsigned int)sizeof(_grDrawSurface));
    dst->width = src->width;
    dst->height = src->height;
    dst->buffers[GR_BUFFERTYPE_COLOR].data     = src->colorBuffer;
    dst->buffers[GR_BUFFERTYPE_COLOR].stride   = src->colorBufferStride;
    dst->buffers[GR_BUFFERTYPE_DEPTH].data     = src->depthBuffer;
    dst->buffers[GR_BUFFERTYPE_DEPTH].stride   = src->depthBufferStride;
    dst->buffers[GR_BUFFERTYPE_STENCIL].data   = src->stencilBuffer;
    dst->buffers[GR_BUFFERTYPE_STENCIL].stride = src->stencilBufferStride;

    grClearDescriptor((_grSurfaceDescriptor *)dst);
    grSetDescriptorSizes((_grSurfaceDescriptor *)dst,
                         src->redBits, src->greenBits, src->blueBits, src->alphaBits);
    grSetDescriptorOffsets((_grSurfaceDescriptor *)dst,
                           src->redOffset, src->greenOffset, src->blueOffset, src->alphaOffset);
    dst->colorBytes   = (unsigned char)src->colorBytes;
    dst->depthBytes   = (unsigned char)src->depthBytes;
    dst->stencilBytes = (unsigned char)src->stencilBytes;
    for (i = 0; i < 4; ++i)
    {
        if (dst->rgbaSizes[i] != 0)
            colorMask |= (1u << i);
    }
    dst->colorMask = (unsigned char)colorMask;
}

void oglSetBuffers(oglContext_s *context, const oglSurfaceDescriptor *drawDesc,
                   const oglSurfaceDescriptor *readDesc)
{
    _grDrawSurface drawSurf;
    _grDrawSurface readSurf;
    int redBits, greenBits, blueBits, alphaBits, depthBits, stencilBits;

    OGL_ASSERT_AT("oglContext.inl", 644, "context", context != NULL);
    OGL_ASSERT_AT("oglContext.inl", 645, "oglIsValidSurfaceDescriptor(drawDesc)",
                  oglIsValidSurfaceDescriptor(drawDesc) != 0);
    OGL_ASSERT_AT("oglContext.inl", 646, "oglIsValidSurfaceDescriptor(readDesc)",
                  oglIsValidSurfaceDescriptor(readDesc) != 0);
    OGL_ASSERT_AT("oglContext.inl", 647, "oglIsValidContext(context)",
                  oglIsValidContext(context) != 0);

    context->drawSurfaceWidth = drawDesc->width;
    context->drawSurfaceHeight = drawDesc->height;
    oglConvertSurfaceDescriptor(&drawSurf, drawDesc);
    oglConvertSurfaceDescriptor(&readSurf, readDesc);
    grSetSurfaces(context->gerbera, &drawSurf, &readSurf);

    redBits     = drawSurf.rgbaSizes[0];
    greenBits   = drawSurf.rgbaSizes[1];
    blueBits    = drawSurf.rgbaSizes[2];
    alphaBits   = drawSurf.rgbaSizes[3];
    depthBits   = 8 * drawSurf.depthBytes;
    stencilBits = 8 * drawSurf.stencilBytes;
    oglSetGetterValueInt(context, 0, GL_RED_BITS,     &redBits);
    oglSetGetterValueInt(context, 0, GL_GREEN_BITS,   &greenBits);
    oglSetGetterValueInt(context, 0, GL_BLUE_BITS,    &blueBits);
    oglSetGetterValueInt(context, 0, GL_ALPHA_BITS,   &alphaBits);
    oglSetGetterValueInt(context, 0, GL_DEPTH_BITS,   &depthBits);
    oglSetGetterValueInt(context, 0, GL_STENCIL_BITS, &stencilBits);
}

int oglSetSurfaceTexture(oglContext_s *context, void *pixels, int width, int height,
                         unsigned int format, int mipLevel, int createMipmaps)
{
    int internalFormat;
    _grTexture *texture;

    OGL_ASSERT_AT("oglContext.inl", 688, "context", context != NULL);
    OGL_ASSERT_AT("oglContext.inl", 689, "pixels", pixels != NULL);
    OGL_ASSERT_AT("oglContext.inl", 690,
                  "hgInRange32(width, 0, GR_MAX_TEXTURE_DIMENSION) && hgIsPowerOfTwo32(width)",
                  hgInRange32(width, 0, GR_MAX_TEXTURE_DIMENSION) != 0 &&
                  hgIsPowerOfTwo32((unsigned int)width) != 0);
    OGL_ASSERT_AT("oglContext.inl", 691,
                  "hgInRange32(height, 0, GR_MAX_TEXTURE_DIMENSION) && hgIsPowerOfTwo32(height)",
                  hgInRange32(height, 0, GR_MAX_TEXTURE_DIMENSION) != 0 &&
                  hgIsPowerOfTwo32((unsigned int)height) != 0);
    OGL_ASSERT_AT("oglContext.inl", 692,
                  "hgInRange32(mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS - 1)",
                  hgInRange32(mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS - 1) != 0);

    internalFormat = oglMapInternalTextureFormat(format);
    texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
    OGL_ASSERT_AT("oglContext.inl", 697, "texture", texture != NULL);
    OGL_ASSERT_AT("oglContext.inl", 699,
                  "internalFormat == GR_TEXTUREFORMAT_RGB8 || internalFormat == GR_TEXTUREFORMAT_RGBA8",
                  internalFormat == GR_TEXTUREFORMAT_RGB8 ||
                  internalFormat == GR_TEXTUREFORMAT_RGBA8);

    grrFreeTextureData(texture);
    grrReplaceTextureLevel(texture, mipLevel, pixels, width, height, internalFormat);
    texture->mipmap[mipLevel].pbuffer = 1;
    if (createMipmaps != 0 && texture->genMipMaps != 0 && mipLevel == 0)
        grrCalcTextureMipmaps(context->gerbera, texture);
    return 1;
}

void oglReleaseSurfaceTexture(oglContext_s *context, unsigned int textureName, int mipLevel)
{
    OGL_ASSERT_AT("oglContext.inl", 723, "context", context != NULL);
    if (oglIsTexture(context->textureManager, textureName) != 0)
    {
        _grTexture *texture = oglGetTexture(context->textureManager, textureName);
        OGL_ASSERT_AT("oglContext.inl", 728, "texture", texture != NULL);
        OGL_ASSERT_AT("oglContext.inl", 729,
                      "hgInRange32(mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS-1)",
                      hgInRange32(mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS - 1) != 0);
        OGL_ASSERT_AT("oglContext.inl", 730,
                      "texture->mipmap[mipLevel].pbuffer",
                      texture->mipmap[mipLevel].pbuffer != 0);
        grrFreeTextureData(texture);
    }
}

int oglIsSurfaceTextureReleased(oglContext_s *context, unsigned int textureName)
{
    _grTexture *texture;
    int bound = 0;
    int i;

    OGL_ASSERT_AT("oglContext.inl", 745, "context", context != NULL);
    texture = oglGetTexture(context->textureManager, textureName);
    if (texture == NULL)
        return 1;
    for (i = 0; i < GR_MAX_TEXTURE_MIPMAPS; ++i)
        bound |= texture->mipmap[i].pbuffer;
    return bound == 0;
}

void oglCheckValidity(oglContext_s *context)
{
    OGL_ASSERT_AT("oglContext.inl", 806, "oglIsValidContext(context)",
                  oglIsValidContext(context) != 0);
}
