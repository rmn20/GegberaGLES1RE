/* grrBlit.c - pixel color conversion, span copies, box-filter mipmap
 * downsampling, and surface/texture blits (grrBlit.inl).
 *
 * Assert sites keep the original grrBlit.inl file/line strings.
 */
#include <stdlib.h>
#include <string.h>
#include "gerbera/grState.h"
#include "gerbera/grRGBA.h"
#include "hg/hgInt32.h"
#include "hg/hgMemory.h"
#include "imath/imApi.h"

unsigned int grReadDataTypeColor(void *src, int dataType, int imageFormat)
{
    int numComp = grGetImageFormatNumComponents(imageFormat);
    int comp[4] = { 0, 0, 0, 255 };
    const unsigned char *p;
    int i;
    int r, g, b, a;

    if (src == NULL && grAssert("src", "grrBlit.inl", 64) != 0)
        grDie(0);
    if (hgInRange32(dataType, 0, GR_DATATYPE_FLOAT) == 0 &&
        grAssert("hgInRange32(dataType, 0, GR_DATATYPE_FLOAT)", "grrBlit.inl", 65) != 0)
        grDie(0);
    if (hgInRange32(imageFormat, 0, GR_IMAGEFORMAT_MAX - 1) == 0 &&
        grAssert("hgInRange32(imageFormat, 0, GR_IMAGEFORMAT_MAX-1)", "grrBlit.inl", 66) != 0)
        grDie(0);

    p = (const unsigned char *)src;
    for (i = 0; i < numComp; ++i)
    {
        int val;
        switch (dataType)
        {
        case GR_DATATYPE_INT8:
            val = 2 * hgMaxZero32(*(const signed char *)p);
            p += 1;
            break;
        case GR_DATATYPE_UINT8:
            val = *p;
            p += 1;
            break;
        case GR_DATATYPE_INT16:
            val = hgMaxZero32(*(const short *)p) >> 7;
            p += 2;
            break;
        case GR_DATATYPE_UINT16:
            val = (int)(*(const unsigned short *)p) >> 8;
            p += 2;
            break;
        case GR_DATATYPE_INT32:
            val = hgMaxZero32(*(const int *)p) >> 23;
            p += 4;
            break;
        case GR_DATATYPE_UINT32:
            val = (int)((*(const unsigned int *)p) >> 24);
            p += 4;
            break;
        default:
            if (dataType != GR_DATATYPE_FLOAT &&
                grAssert("dataType == GR_DATATYPE_FLOAT", "grrBlit.inl", 81) != 0)
                grDie(0);
            val = hgMin32(hgMaxZero32(IScalar_toFixedHO(IScalar_iftos(*(const int *)p), 8)), 255);
            p += 4;
            break;
        }
        comp[i] = val;
    }

    r = comp[0];
    g = comp[1];
    b = comp[2];
    a = comp[3];
    switch (imageFormat)
    {
    case GR_IMAGEFORMAT_ALPHA:
        a = r;
        r = 0;
        break;
    case GR_IMAGEFORMAT_LUMINANCE:
        g = r;
        b = r;
        break;
    case GR_IMAGEFORMAT_LUMINANCE_ALPHA:
        a = g;
        g = r;
        b = r;
        break;
    case GR_IMAGEFORMAT_BGR:
    case GR_IMAGEFORMAT_BGRA:
    {
        int tmp = r;
        r = b;
        b = tmp;
        break;
    }
    default:
        if (!(imageFormat == GR_IMAGEFORMAT_RGB || imageFormat == GR_IMAGEFORMAT_RGBA) &&
            grAssert("imageFormat==GR_IMAGEFORMAT_RGB || imageFormat==GR_IMAGEFORMAT_RGBA",
                     "grrBlit.inl", 136) != 0)
            grDie(0);
        break;
    }
    return grSetRGBA(r, g, b, a);
}

void grWriteDataTypeColor(void *dst, int dataType, int imageFormat, unsigned int color)
{
    unsigned int r, g, b, a;
    unsigned int outComp[4];
    int numComp;
    unsigned char *p;
    int i;

    if (dst == NULL && grAssert("dst", "grrBlit.inl", 163) != 0)
        grDie(0);
    if (hgInRange32(dataType, 0, GR_DATATYPE_FLOAT) == 0 &&
        grAssert("hgInRange32(dataType, 0, GR_DATATYPE_FLOAT)", "grrBlit.inl", 164) != 0)
        grDie(0);
    if (hgInRange32(imageFormat, 0, GR_IMAGEFORMAT_MAX - 1) == 0 &&
        grAssert("hgInRange32(imageFormat, 0, GR_IMAGEFORMAT_MAX-1)", "grrBlit.inl", 165) != 0)
        grDie(0);

    r = grUnpackRGBA(color, GR_COLOR_R);
    g = grUnpackRGBA(color, GR_COLOR_G);
    b = grUnpackRGBA(color, GR_COLOR_B);
    a = grUnpackRGBA(color, GR_COLOR_A);
    numComp = 1;
    switch (imageFormat)
    {
    case GR_IMAGEFORMAT_ALPHA:
        outComp[0] = a;
        break;
    case GR_IMAGEFORMAT_RGB:
        outComp[0] = r;
        outComp[1] = g;
        outComp[2] = b;
        numComp = 3;
        break;
    case GR_IMAGEFORMAT_RGBA:
        outComp[0] = r;
        outComp[1] = g;
        outComp[2] = b;
        outComp[3] = a;
        numComp = 4;
        break;
    case GR_IMAGEFORMAT_LUMINANCE:
        outComp[0] = b + g + r;
        break;
    case GR_IMAGEFORMAT_LUMINANCE_ALPHA:
        outComp[0] = b + g + r;
        outComp[1] = a;
        numComp = 2;
        break;
    case GR_IMAGEFORMAT_BGR:
        outComp[0] = b;
        outComp[1] = g;
        outComp[2] = r;
        numComp = 3;
        break;
    default:
        if (imageFormat != GR_IMAGEFORMAT_BGRA &&
            grAssert("imageFormat == GR_IMAGEFORMAT_BGRA", "grrBlit.inl", 219) != 0)
            grDie(0);
        outComp[0] = b;
        outComp[1] = g;
        outComp[2] = r;
        outComp[3] = a;
        numComp = 4;
        break;
    }

    p = (unsigned char *)dst;
    for (i = 0; i < numComp; ++i)
    {
        unsigned int clamped = (unsigned int)hgMin32((int)outComp[i], 255);
        unsigned int rep = (clamped << 8) | clamped;
        rep |= rep << 16;
        switch (dataType)
        {
        case GR_DATATYPE_INT8:
            *(signed char *)p = (signed char)(rep >> 25);
            p += 1;
            break;
        case GR_DATATYPE_UINT8:
            *p = (unsigned char)(rep >> 24);
            p += 1;
            break;
        case GR_DATATYPE_INT16:
            *(short *)p = (short)(rep >> 9);
            p += 2;
            break;
        case GR_DATATYPE_UINT16:
            *(unsigned short *)p = (unsigned short)(rep >> 8);
            p += 2;
            break;
        case GR_DATATYPE_INT32:
            *(int *)p = (int)(rep >> 1);
            p += 4;
            break;
        case GR_DATATYPE_UINT32:
            *(unsigned int *)p = rep;
            p += 4;
            break;
        default:
            if (dataType != GR_DATATYPE_FLOAT &&
                grAssert("dataType == GR_DATATYPE_FLOAT", "grrBlit.inl", 244) != 0)
                grDie(0);
            *(float *)p = IScalar_stof(IScalar_byteToScalar((HGuint8)rep));
            p += 4;
            break;
        }
    }
}

void grrConvertBlitImage(Gerbera_s *state, grBlitImage_s *image)
{
    grMemoryImage_s *memImage;

    if (!(state != NULL && image != NULL) &&
        grAssert("state && image", "grrBlit.inl", 262) != 0)
        grDie(0);

    memImage = &image->memoryImage;
    switch (image->type)
    {
    case GR_BLITIMAGE_TEXTURE:
    {
        _grMipLevel *mipmap;

        if (image->texture.texture == NULL &&
            grAssert("image->texture.texture", "grrBlit.inl", 267) != 0)
            grDie(0);
        if (grIsValidTexture(image->texture.texture) == 0 &&
            grAssert("grIsValidTexture(image->texture.texture)", "grrBlit.inl", 268) != 0)
            grDie(0);
        if (hgInRange32(image->texture.mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS - 1) == 0 &&
            grAssert("hgInRange32(image->texture.mipLevel, 0, GR_MAX_TEXTURE_MIPMAPS-1)",
                     "grrBlit.inl", 269) != 0)
            grDie(0);
        mipmap = &image->texture.texture->mipmap[image->texture.mipLevel];
        memImage->data = mipmap->pixels;
        if (memImage->data != NULL)
        {
            memImage->imageFormat = GR_IMAGEFORMAT_MAX;
            grrGetTextureFormatDescriptor(&memImage->descriptor, mipmap->texFormat);
            memImage->width = 1 << mipmap->log2Dim[0];
            memImage->height = 1 << mipmap->log2Dim[1];
            memImage->useDescriptor = 1;
            memImage->bytesPerPixel = memImage->descriptor.colorBytes;
            memImage->stride = memImage->width * memImage->bytesPerPixel;
            if (memImage->width <= 0 &&
                grAssert("memImage->width > 0", "grrBlit.inl", 290) != 0)
                grDie(0);
            if (memImage->height <= 0 &&
                grAssert("memImage->height > 0", "grrBlit.inl", 291) != 0)
                grDie(0);
        }
        break;
    }
    case GR_BLITIMAGE_READ_SURFACE:
    {
        unsigned char *ptr = NULL;

        if (state->readSurface.buffers[GR_BUFFERTYPE_COLOR].data != NULL)
        {
            ptr = (unsigned char *)state->readSurface.buffers[GR_BUFFERTYPE_COLOR].data +
                  state->readSurface.buffers[GR_BUFFERTYPE_COLOR].stride *
                      (state->readSurface.height - 1);
        }
        memImage->stride = -state->readSurface.buffers[GR_BUFFERTYPE_COLOR].stride;
        memImage->width = state->readSurface.width;
        memImage->height = state->readSurface.height;
        memImage->data = ptr;
        hgMemCopy(&memImage->descriptor, &state->readSurface,
                  (unsigned int)sizeof(_grSurfaceDescriptor));
        memImage->useDescriptor = 1;
        memImage->bytesPerPixel = state->readSurface.colorBytes;
        break;
    }
    default:
        if (image->type != GR_BLITIMAGE_MEMORY_IMAGE &&
            grAssert("image->type == GR_BLITIMAGE_MEMORY_IMAGE", "grrBlit.inl", 318) != 0)
            grDie(0);
        break;
    }
    image->type = GR_BLITIMAGE_MEMORY_IMAGE;
}

void *grrComputeImageAddress(grMemoryImage_s *image, int x, int y)
{
    if (image == NULL && grAssert("image", "grrBlit.inl", 335) != 0)
        grDie(0);
    if (hgInRange32(x, 0, image->width) == 0 &&
        grAssert("hgInRange32(x, 0, image->width)", "grrBlit.inl", 336) != 0)
        grDie(0);
    if (hgInRange32(y, 0, image->height) == 0 &&
        grAssert("hgInRange32(y, 0, image->height)", "grrBlit.inl", 337) != 0)
        grDie(0);
    return (unsigned char *)image->data + image->stride * y + image->bytesPerPixel * x;
}

typedef struct grBlitByteLayout_s
{
    int r, g, b, a;
    int hasAlpha;
} grBlitByteLayout_s;

static int grrByteColorLayout(const grMemoryImage_s *image, grBlitByteLayout_s *layout)
{
    const unsigned short endian = 1;
    int littleEndian = *(const unsigned char *)&endian;
    if (image->useDescriptor == 1)
    {
        const _grSurfaceDescriptor *d = &image->descriptor;
        unsigned int used;
        if (image->bytesPerPixel != 4 || d->colorBytes != 4 ||
            d->rgbaSizes[0] != 8 || d->rgbaSizes[1] != 8 || d->rgbaSizes[2] != 8 ||
            (d->rgbaSizes[3] != 0 && d->rgbaSizes[3] != 8) ||
            ((d->rgbaOffsets[0] | d->rgbaOffsets[1] | d->rgbaOffsets[2]) & 7) ||
            d->rgbaOffsets[0] > 24 || d->rgbaOffsets[1] > 24 || d->rgbaOffsets[2] > 24)
            return 0;
        layout->r = littleEndian ? d->rgbaOffsets[0] / 8 : 3 - d->rgbaOffsets[0] / 8;
        layout->g = littleEndian ? d->rgbaOffsets[1] / 8 : 3 - d->rgbaOffsets[1] / 8;
        layout->b = littleEndian ? d->rgbaOffsets[2] / 8 : 3 - d->rgbaOffsets[2] / 8;
        used = (1u << layout->r) | (1u << layout->g) | (1u << layout->b);
        if (layout->r == layout->g || layout->r == layout->b || layout->g == layout->b)
            return 0;
        layout->hasAlpha = d->rgbaSizes[3] != 0;
        if (layout->hasAlpha)
        {
            if ((d->rgbaOffsets[3] & 7) || d->rgbaOffsets[3] > 24)
                return 0;
            layout->a = littleEndian ? d->rgbaOffsets[3] / 8 : 3 - d->rgbaOffsets[3] / 8;
            if (used & (1u << layout->a))
                return 0;
        }
        else
        {
            /* Packing RGB into a four-byte descriptor clears its unused byte. */
            layout->a = 6 - layout->r - layout->g - layout->b;
        }
        return 1;
    }
    if (image->useDescriptor != 0 || image->dataType != GR_DATATYPE_UINT8)
        return 0;
    switch (image->imageFormat)
    {
    case GR_IMAGEFORMAT_RGB:
    case GR_IMAGEFORMAT_RGBA:
        layout->r = 0; layout->g = 1; layout->b = 2;
        break;
    case GR_IMAGEFORMAT_BGR:
    case GR_IMAGEFORMAT_BGRA:
        layout->r = 2; layout->g = 1; layout->b = 0;
        break;
    default:
        return 0;
    }
    layout->hasAlpha = image->imageFormat == GR_IMAGEFORMAT_RGBA ||
                       image->imageFormat == GR_IMAGEFORMAT_BGRA;
    layout->a = layout->hasAlpha ? 3 : -1;
    return image->bytesPerPixel == 3 + layout->hasAlpha;
}

static int grrIsRGB565Image(const grMemoryImage_s *image)
{
    const _grSurfaceDescriptor *d = &image->descriptor;
    return image->useDescriptor == 1 && image->bytesPerPixel == 2 && d->colorBytes == 2 &&
           d->rgbaSizes[0] == 5 && d->rgbaSizes[1] == 6 && d->rgbaSizes[2] == 5 &&
           d->rgbaSizes[3] == 0 && d->rgbaOffsets[0] == 11 &&
           d->rgbaOffsets[1] == 5 && d->rgbaOffsets[2] == 0;
}

static int grrCopyByteColorSpan(grMemoryImage_s *dstImage, grMemoryImage_s *srcImage,
                               unsigned char *dst, const unsigned char *src, int length)
{
    grBlitByteLayout_s s, d;
    int srcBytes = grrByteColorLayout(srcImage, &s);
    int dstBytes = grrByteColorLayout(dstImage, &d);
    const unsigned short endian = 1;
    int low = *(const unsigned char *)&endian ? 0 : 1;
    int high = 1 - low;
    int srcStride = srcImage->bytesPerPixel;
    int dstStride = dstImage->bytesPerPixel;

    if (srcBytes && dstBytes)
    {
        do
        {
            unsigned int r = src[s.r], g = src[s.g], b = src[s.b];
            unsigned int a = s.hasAlpha ? src[s.a] : 255;
            dst[d.r] = (unsigned char)r;
            dst[d.g] = (unsigned char)g;
            dst[d.b] = (unsigned char)b;
            if (d.a >= 0)
                dst[d.a] = d.hasAlpha ? (unsigned char)a : 0;
            src += srcStride;
            dst += dstStride;
        } while (--length);
        return 1;
    }
    if (dstBytes && grrIsRGB565Image(srcImage))
    {
        do
        {
            unsigned int packed = src[low] | ((unsigned int)src[high] << 8);
            unsigned int r = packed >> 11, g = (packed >> 5) & 63, b = packed & 31;
            dst[d.r] = (unsigned char)((r << 3) | (r >> 2));
            dst[d.g] = (unsigned char)((g << 2) | (g >> 4));
            dst[d.b] = (unsigned char)((b << 3) | (b >> 2));
            if (d.a >= 0)
                dst[d.a] = d.hasAlpha ? 255 : 0;
            src += 2;
            dst += dstStride;
        } while (--length);
        return 1;
    }
    if (srcBytes && grrIsRGB565Image(dstImage))
    {
        do
        {
            unsigned int packed = ((unsigned int)(src[s.r] >> 3) << 11) |
                                  ((unsigned int)(src[s.g] >> 2) << 5) | (src[s.b] >> 3);
            dst[low] = (unsigned char)packed;
            dst[high] = (unsigned char)(packed >> 8);
            src += srcStride;
            dst += 2;
        } while (--length);
        return 1;
    }
    return 0;
}

void grrCopyColorSpan(grMemoryImage_s *dstImage, grMemoryImage_s *srcImage,
                      int xDst, int yDst, int xSrc, int ySrc, int length, void *workMemory)
{
    unsigned char *dst;
    unsigned char *src;
    int srcPixelStride;
    int dstPixelStride;

    (void)workMemory;

    if (!(dstImage != NULL && srcImage != NULL) &&
        grAssert("dstImage && srcImage", "grrBlit.inl", 366) != 0)
        grDie(0);
    if (hgInRange32(xSrc, 0, srcImage->width - length) == 0 &&
        grAssert("hgInRange32(xSrc, 0, srcImage->width - length)", "grrBlit.inl", 367) != 0)
        grDie(0);
    if (hgInRange32(ySrc, 0, srcImage->height) == 0 &&
        grAssert("hgInRange32(ySrc, 0, srcImage->height)", "grrBlit.inl", 368) != 0)
        grDie(0);
    if (hgInRange32(xDst, 0, dstImage->width - length) == 0 &&
        grAssert("hgInRange32(xDst, 0, dstImage->width - length)", "grrBlit.inl", 369) != 0)
        grDie(0);
    if (hgInRange32(yDst, 0, dstImage->height) == 0 &&
        grAssert("hgInRange32(yDst, 0, dstImage->height)", "grrBlit.inl", 370) != 0)
        grDie(0);
    if (length <= 0 && grAssert("length > 0", "grrBlit.inl", 371) != 0)
        grDie(0);

    dst = (unsigned char *)grrComputeImageAddress(dstImage, xDst, yDst);
    src = (unsigned char *)grrComputeImageAddress(srcImage, xSrc, ySrc);
    srcPixelStride = srcImage->bytesPerPixel;
    dstPixelStride = dstImage->bytesPerPixel;

    if (srcPixelStride == dstPixelStride &&
        srcImage->useDescriptor != 0 &&
        dstImage->useDescriptor != 0 &&
        dstImage->descriptor.colorMask == GR_COLORMASK_FULL &&
        dstImage->descriptor.colorBytes == srcImage->descriptor.colorBytes &&
        hgMemCompare(srcImage->descriptor.rgbaOffsets, dstImage->descriptor.rgbaOffsets, 4u) == 0 &&
        hgMemCompare(srcImage->descriptor.rgbaSizes, dstImage->descriptor.rgbaSizes, 4u) == 0)
    {
        hgMemCopy(dst, src, (unsigned int)(srcPixelStride * length));
        return;
    }

    if (grrCopyByteColorSpan(dstImage, srcImage, dst, src, length))
        return;

    do
    {
        unsigned int color;

        if (grIsValidBool(srcImage->useDescriptor) == 0 &&
            grAssert("grIsValidBool(srcImage->useDescriptor)", "grrBlit.inl", 406) != 0)
            grDie(0);
        if (srcImage->useDescriptor != 0)
        {
            if (srcImage->bytesPerPixel == 1)
                color = grUnpackColor(*src, &srcImage->descriptor);
            else if (srcImage->bytesPerPixel == 2)
            {
                unsigned short srcPacked;
                memcpy(&srcPacked, src, sizeof(srcPacked));
                color = grUnpackColor(srcPacked, &srcImage->descriptor);
            }
            else
            {
                unsigned int srcPacked;
                memcpy(&srcPacked, src, sizeof(srcPacked));
                if (srcImage->bytesPerPixel != 4 &&
                    grAssert("srcImage->bytesPerPixel == 4", "grrBlit.inl", 415) != 0)
                    grDie(0);
                color = grUnpackColor(srcPacked, &srcImage->descriptor);
            }
        }
        else
        {
            color = grReadDataTypeColor(src, srcImage->dataType, srcImage->imageFormat);
        }
        src += srcPixelStride;

        if (grIsValidBool(dstImage->useDescriptor) == 0 &&
            grAssert("grIsValidBool(dstImage->useDescriptor)", "grrBlit.inl", 431) != 0)
            grDie(0);
        if (dstImage->useDescriptor != 0)
        {
            unsigned int dstPacked = grPackColor(color, &dstImage->descriptor);
            if (!(hgInRange32(dstImage->bytesPerPixel, 1, 4) != 0 &&
                  hgIsPowerOfTwo32(dstImage->bytesPerPixel) != 0) &&
                grAssert("hgInRange32(dstImage->bytesPerPixel, 1, 4) && hgIsPowerOfTwo32(dstImage->bytesPerPixel)",
                         "grrBlit.inl", 436) != 0)
                grDie(0);
            if (dstImage->bytesPerPixel == 1)
                *dst = (unsigned char)dstPacked;
            else if (dstImage->bytesPerPixel == 2)
            {
                unsigned short value = (unsigned short)dstPacked;
                memcpy(dst, &value, sizeof(value));
            }
            else
            {
                memcpy(dst, &dstPacked, sizeof(dstPacked));
                if (dstImage->bytesPerPixel != 4 &&
                    grAssert("dstImage->bytesPerPixel == 4", "grrBlit.inl", 442) != 0)
                    grDie(0);
            }
        }
        else
        {
            grWriteDataTypeColor(dst, dstImage->dataType, dstImage->imageFormat, color);
        }
        dst += dstPixelStride;
        --length;
    } while (length != 0);
}

void grrBlit(Gerbera_s *state, grBlitInfo_s *blitInfo)
{
    grMemoryImage_s *srcMem;
    grMemoryImage_s *dstMem;
    int usesReadSurface;

    if (state == NULL && grAssert("state", "grrBlit.inl", 476) != 0)
        grDie(0);
    if (blitInfo == NULL && grAssert("blitInfo", "grrBlit.inl", 477) != 0)
        grDie(0);
    if (!(blitInfo->srcImage != NULL && blitInfo->dstImage != NULL) &&
        grAssert("blitInfo->srcImage && blitInfo->dstImage", "grrBlit.inl", 478) != 0)
        grDie(0);
    if ((blitInfo->dstRect.x1 - blitInfo->dstRect.x0 != blitInfo->srcRect.x1 - blitInfo->srcRect.x0) &&
        grAssert("blitInfo->dstRect.x1 - blitInfo->dstRect.x0 == blitInfo->srcRect.x1 - blitInfo->srcRect.x0",
                 "grrBlit.inl", 479) != 0)
        grDie(0);
    if ((blitInfo->dstRect.y1 - blitInfo->dstRect.y0 != blitInfo->srcRect.y1 - blitInfo->srcRect.y0) &&
        grAssert("blitInfo->dstRect.y1 - blitInfo->dstRect.y0 == blitInfo->srcRect.y1 - blitInfo->srcRect.y0",
                 "grrBlit.inl", 480) != 0)
        grDie(0);

    srcMem = &blitInfo->srcImage->memoryImage;
    dstMem = &blitInfo->dstImage->memoryImage;
    usesReadSurface = (blitInfo->srcImage->type == GR_BLITIMAGE_READ_SURFACE ||
                       blitInfo->dstImage->type == GR_BLITIMAGE_READ_SURFACE);
    if (usesReadSurface != 0)
    {
        grFlushBufferClears(state);
        grLockSurface(GR_SURFACE_READ);
    }

    grrConvertBlitImage(state, blitInfo->srcImage);
    grrConvertBlitImage(state, blitInfo->dstImage);

    if (blitInfo->dstImage->type != GR_BLITIMAGE_MEMORY_IMAGE &&
        grAssert("blitInfo->dstImage->type == GR_BLITIMAGE_MEMORY_IMAGE", "grrBlit.inl", 492) != 0)
        grDie(0);
    if (blitInfo->srcImage->type != GR_BLITIMAGE_MEMORY_IMAGE &&
        grAssert("blitInfo->srcImage->type == GR_BLITIMAGE_MEMORY_IMAGE", "grrBlit.inl", 493) != 0)
        grDie(0);

    if (dstMem->data != NULL && srcMem->data != NULL)
    {
        int xClip0 = hgMaxZero32(-hgMin32(blitInfo->srcRect.x0, blitInfo->dstRect.x0));
        int xClip1 = hgMaxZero32(hgMax32(blitInfo->srcRect.x1 - srcMem->width,
                                         blitInfo->dstRect.x1 - dstMem->width));
        int yClip0, yClip1;

        blitInfo->dstRect.x0 += xClip0;
        blitInfo->srcRect.x0 += xClip0;
        blitInfo->dstRect.x1 -= xClip1;
        blitInfo->srcRect.x1 -= xClip1;

        yClip0 = hgMaxZero32(-hgMin32(blitInfo->srcRect.y0, blitInfo->dstRect.y0));
        yClip1 = hgMaxZero32(hgMax32(blitInfo->srcRect.y1 - srcMem->height,
                                     blitInfo->dstRect.y1 - dstMem->height));
        blitInfo->dstRect.y0 += yClip0;
        blitInfo->srcRect.y0 += yClip0;
        blitInfo->dstRect.y1 -= yClip1;
        blitInfo->srcRect.y1 -= yClip1;

        if (blitInfo->dstRect.x1 > blitInfo->dstRect.x0 &&
            blitInfo->dstRect.y1 > blitInfo->dstRect.y0 &&
            blitInfo->srcRect.x1 > blitInfo->srcRect.x0 &&
            blitInfo->srcRect.y1 > blitInfo->srcRect.y0)
        {
            int width = blitInfo->dstRect.x1 - blitInfo->dstRect.x0;
            int height = blitInfo->dstRect.y1 - blitInfo->dstRect.y0;
            _grStackAllocator *allocator = &state->scratchAllocator;
            void *workMem;
            int y;

            if (state->scratchMemory == NULL)
            {
                if (state->scratchMemorySize != 0 &&
                    grAssert("!state->scratchMemorySize", "grrBlit.inl", 540) != 0)
                    grDie(0);
                grScratchGrowMemory(state);
            }
            if (grStackGetFreeSpace(allocator) < GR_MAX_DISPLAY_DIMENSION * 4 &&
                grAssert("grStackGetFreeSpace(allocator) >= GR_MAX_DISPLAY_DIMENSION*4",
                         "grrBlit.inl", 546) != 0)
                grDie(0);
            workMem = grStackAllocate(allocator, GR_MAX_DISPLAY_DIMENSION * 4);
            if (width > GR_MAX_DISPLAY_DIMENSION &&
                grAssert("width <= GR_MAX_DISPLAY_DIMENSION", "grrBlit.inl", 549) != 0)
                grDie(0);
            if (blitInfo->srcRect.x1 - blitInfo->srcRect.x0 != width &&
                grAssert("blitInfo->srcRect.x1 - blitInfo->srcRect.x0 == width", "grrBlit.inl", 550) != 0)
                grDie(0);
            if (blitInfo->srcRect.y1 - blitInfo->srcRect.y0 != height &&
                grAssert("blitInfo->srcRect.y1 - blitInfo->srcRect.y0 == height", "grrBlit.inl", 551) != 0)
                grDie(0);

            for (y = 0; y < height; ++y)
            {
                grrCopyColorSpan(dstMem, srcMem,
                                 blitInfo->dstRect.x0, y + blitInfo->dstRect.y0,
                                 blitInfo->srcRect.x0, y + blitInfo->srcRect.y0,
                                 width, workMem);
            }
            grStackFree(allocator, workMem);
        }
    }

    if (usesReadSurface != 0)
        grReleaseSurface(GR_SURFACE_READ);
}

void grrBoxFilterLoop32(unsigned int *src, unsigned int *dst, int srcWidth, int srcHeight,
                        int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                        unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep)
{
    int y;
    (void)srcHeight;

    for (y = 0; y < dstHeight; ++y)
    {
        int xLeft = dstWidth;
        if (!(xStep == 0 || xStep == 1) &&
            grAssert("xStep == 0 || xStep == 1", "grrBlit.inl", 609) != 0)
            grDie(0);
        if (!((int)yStep == srcWidth || yStep == 0) &&
            grAssert("(int)yStep == srcWidth || yStep == 0", "grrBlit.inl", 610) != 0)
            grDie(0);
        do
        {
            unsigned int s0 = src[0];
            unsigned int s1 = src[xStep];
            unsigned int s2 = src[yStep];
            unsigned int s3 = src[xStep + yStep];
            unsigned int acc = 0;
            int c;
            for (c = 3; c >= 0; --c)
            {
                unsigned int m = mask[c];
                unsigned int ps = preShift[c];
                acc |= m & ((rounding[c] +
                             ((m & s3) >> ps) +
                             ((m & s2) >> ps) +
                             ((m & s0) >> ps) +
                             ((m & s1) >> ps)) >> postShift[c]);
            }
            *dst++ = acc;
            src += xStep + 1;
            --xLeft;
        } while (xLeft != 0);
        src += srcWidth;
    }
}

void grrBoxFilterLoop16(unsigned short *src, unsigned short *dst, int srcWidth, int srcHeight,
                        int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                        unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep)
{
    int y;
    (void)srcHeight;
    (void)preShift;

    for (y = 0; y < dstHeight; ++y)
    {
        int xLeft = dstWidth;
        if (!(xStep == 0 || xStep == 1) &&
            grAssert("xStep == 0 || xStep == 1", "grrBlit.inl", 697) != 0)
            grDie(0);
        if (!((int)yStep == srcWidth || yStep == 0) &&
            grAssert("(int)yStep == srcWidth || yStep == 0", "grrBlit.inl", 698) != 0)
            grDie(0);
        do
        {
            unsigned int s0 = src[0];
            unsigned int s1 = src[xStep];
            unsigned int s2 = src[yStep];
            unsigned int s3 = src[xStep + yStep];
            unsigned int acc = 0;
            int c;
            for (c = 3; c >= 0; --c)
            {
                unsigned int m = mask[c];
                acc |= m & ((rounding[c] +
                             (m & s3) + (m & s2) + (m & s0) + (m & s1)) >> postShift[c]);
            }
            *dst++ = (unsigned short)acc;
            src += xStep + 1;
            --xLeft;
        } while (xLeft != 0);
        src += srcWidth;
    }
}

void grrBoxFilterLoop8(unsigned char *src, unsigned char *dst, int srcWidth, int srcHeight,
                       int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                       unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep)
{
    int y;
    (void)srcHeight;
    (void)preShift;

    for (y = 0; y < dstHeight; ++y)
    {
        int xLeft = dstWidth;
        if (!(xStep == 0 || xStep == 1) &&
            grAssert("xStep == 0 || xStep == 1", "grrBlit.inl", 787) != 0)
            grDie(0);
        if (!((int)yStep == srcWidth || yStep == 0) &&
            grAssert("(int)yStep == srcWidth || yStep == 0", "grrBlit.inl", 788) != 0)
            grDie(0);
        do
        {
            unsigned int s0 = src[0];
            unsigned int s1 = src[xStep];
            unsigned int s2 = src[yStep];
            unsigned int s3 = src[yStep + xStep];
            unsigned int acc = 0;
            int c;
            for (c = 3; c >= 0; --c)
            {
                unsigned int m = mask[c];
                acc |= m & ((rounding[c] +
                             (m & s3) + (m & s2) + (m & s0) + (m & s1)) >> postShift[c]);
            }
            *dst++ = (unsigned char)acc;
            src += xStep + 1;
            --xLeft;
        } while (xLeft != 0);
        src += srcWidth;
    }
}

void grrDownsample(_grSurfaceDescriptor *desc, int srcWidth, int srcHeight,
                   int dstWidth, int dstHeight, void *src, void *dst)
{
    unsigned int mask[4];
    unsigned int rounding[4];
    unsigned int preShift[4];
    unsigned int postShift[4];
    unsigned int xStep = 1;
    unsigned int yStep = (unsigned int)srcWidth;
    int c;

    if (desc == NULL && grAssert("desc", "grrBlit.inl", 863) != 0)
        grDie(0);
    if (!(src != NULL && dst != NULL) && grAssert("src && dst", "grrBlit.inl", 864) != 0)
        grDie(0);
    if (desc->colorBytes > 4u && grAssert("desc->colorBytes <= 4", "grrBlit.inl", 865) != 0)
        grDie(0);

    mask[0] = grPackColor(grSetRGBA(255, 0, 0, 0), desc);
    mask[1] = grPackColor(grSetRGBA(0, 255, 0, 0), desc);
    mask[2] = grPackColor(grSetRGBA(0, 0, 255, 0), desc);
    mask[3] = grPackColor(grSetRGBA(0, 0, 0, 255), desc);

    for (c = 3; c >= 0; --c)
    {
        unsigned int off = desc->rgbaOffsets[c];
        unsigned int sz = desc->rgbaSizes[c];
        if (sz + off >= 30u)
        {
            rounding[c] = 0;
            preShift[c] = 2;
            postShift[c] = 0;
        }
        else
        {
            rounding[c] = 2u << off;
            preShift[c] = 0;
            postShift[c] = 2;
        }
    }

    if (!(srcWidth != 1 || srcHeight != 1) &&
        grAssert("(srcWidth != 1) || (srcHeight != 1)", "grrBlit.inl", 923) != 0)
        grDie(0);
    if (srcWidth < 2)
    {
        if (srcWidth != 1 && grAssert("srcWidth == 1", "grrBlit.inl", 927) != 0)
            grDie(0);
        xStep = 0;
    }
    if (srcHeight < 2)
    {
        if (srcHeight != 1 && grAssert("srcHeight == 1", "grrBlit.inl", 933) != 0)
            grDie(0);
        yStep = 0;
    }

    switch (desc->colorBytes)
    {
    case 2u:
        grrBoxFilterLoop16((unsigned short *)src, (unsigned short *)dst,
                           srcWidth, srcHeight, dstWidth, dstHeight,
                           mask, rounding, preShift, postShift, xStep, yStep);
        break;
    case 4u:
        grrBoxFilterLoop32((unsigned int *)src, (unsigned int *)dst,
                           srcWidth, srcHeight, dstWidth, dstHeight,
                           mask, rounding, preShift, postShift, xStep, yStep);
        break;
    default:
        if (desc->colorBytes != 1u &&
            grAssert("desc->colorBytes == 1", "grrBlit.inl", 954) != 0)
            grDie(0);
        grrBoxFilterLoop8((unsigned char *)src, (unsigned char *)dst,
                          srcWidth, srcHeight, dstWidth, dstHeight,
                          mask, rounding, preShift, postShift, xStep, yStep);
        break;
    }
}

void grrBlitMipmap(Gerbera_s *state, grBlitInfo_s *blitInfo)
{
    grBlitImage_s *srcImage;
    grBlitImage_s *dstImage;

    if (state == NULL && grAssert("state", "grrBlit.inl", 987) != 0)
        grDie(0);
    if (blitInfo == NULL && grAssert("blitInfo", "grrBlit.inl", 988) != 0)
        grDie(0);
    srcImage = blitInfo->srcImage;
    dstImage = blitInfo->dstImage;
    if (!(srcImage != NULL && dstImage != NULL) &&
        grAssert("srcImage && dstImage", "grrBlit.inl", 989) != 0)
        grDie(0);
    if (dstImage->type != GR_BLITIMAGE_TEXTURE &&
        grAssert("dstImage->type == GR_BLITIMAGE_TEXTURE", "grrBlit.inl", 990) != 0)
        grDie(0);
    if (srcImage->type != GR_BLITIMAGE_TEXTURE &&
        grAssert("srcImage->type == GR_BLITIMAGE_TEXTURE", "grrBlit.inl", 991) != 0)
        grDie(0);
    if (dstImage->texture.mipLevel != srcImage->texture.mipLevel + 1 &&
        grAssert("dstImage->texture.mipLevel == (srcImage->texture.mipLevel + 1)",
                 "grrBlit.inl", 992) != 0)
        grDie(0);
    if (dstImage->texture.texture->mipmap[dstImage->texture.mipLevel].texFormat !=
            srcImage->texture.texture->mipmap[srcImage->texture.mipLevel].texFormat &&
        grAssert("dstImage->texture.texture->mipmap[dstImage->texture.mipLevel].texFormat == srcImage->texture.texture->mipmap[srcImage->texture.mipLevel].texFormat",
                 "grrBlit.inl", 994) != 0)
        grDie(0);

    grrConvertBlitImage(state, srcImage);
    grrConvertBlitImage(state, dstImage);

    if (!(srcImage->memoryImage.width == 2 * dstImage->memoryImage.width ||
          srcImage->memoryImage.height == 2 * dstImage->memoryImage.height) &&
        grAssert("(srcImage->memoryImage.width == (2 * dstImage->memoryImage.width)) || (srcImage->memoryImage.height == (2 * dstImage->memoryImage.height))",
                 "grrBlit.inl", 1004) != 0)
        grDie(0);
    if (grDescriptorsEqual(&srcImage->memoryImage.descriptor, &dstImage->memoryImage.descriptor) == 0 &&
        grAssert("grDescriptorsEqual(&srcImage->memoryImage.descriptor, &dstImage->memoryImage.descriptor)",
                 "grrBlit.inl", 1007) != 0)
        grDie(0);

    grrDownsample(&srcImage->memoryImage.descriptor,
                  srcImage->memoryImage.width, srcImage->memoryImage.height,
                  dstImage->memoryImage.width, dstImage->memoryImage.height,
                  srcImage->memoryImage.data, dstImage->memoryImage.data);
}
