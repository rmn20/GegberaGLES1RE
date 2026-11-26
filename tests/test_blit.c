#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gerbera/grState.h"
#include "gerbera/grRGBA.h"
#include "test_support.h"

static grMemoryImage_s image_format(int format)
{
    grMemoryImage_s image;
    static const int byteFormats[] = { GR_IMAGEFORMAT_RGB, GR_IMAGEFORMAT_RGBA,
                                      GR_IMAGEFORMAT_BGR, GR_IMAGEFORMAT_BGRA };
    memset(&image, 0, sizeof(image));
    if (format < 4)
    {
        image.dataType = GR_DATATYPE_UINT8;
        image.imageFormat = byteFormats[format];
        image.bytesPerPixel = 3 + (format & 1);
    }
    else
    {
        int textureFormat = format == 8 ? GR_TEXTUREFORMAT_RGB565 :
                            format == 9 ? GR_TEXTUREFORMAT_RGBA4444 :
                            (format & 1) ? GR_TEXTUREFORMAT_RGBA8 : GR_TEXTUREFORMAT_RGB8;
        image.useDescriptor = 1;
        grrGetTextureFormatDescriptor(&image.descriptor, textureFormat);
        image.bytesPerPixel = image.descriptor.colorBytes;
        if (format == 6 || format == 7)
            grSetDescriptorOffsets(&image.descriptor, 0, 8, 16, 24);
    }
    return image;
}

static void reference_span(grMemoryImage_s *dst, grMemoryImage_s *src, int length)
{
    const unsigned char *s = (const unsigned char *)src->data + src->bytesPerPixel;
    unsigned char *d = (unsigned char *)dst->data + 2 * dst->bytesPerPixel;
    int i;
    for (i = 0; i < length; ++i, s += src->bytesPerPixel, d += dst->bytesPerPixel)
    {
        unsigned int color;
        if (src->useDescriptor)
        {
            unsigned int packed;
            if (src->bytesPerPixel == 2)
            {
                unsigned short word;
                memcpy(&word, s, sizeof(word));
                packed = word;
            }
            else
                memcpy(&packed, s, sizeof(packed));
            color = grUnpackColor(packed, &src->descriptor);
        }
        else
            color = grReadDataTypeColor((void *)s, src->dataType, src->imageFormat);
        if (dst->useDescriptor)
        {
            unsigned int packed = grPackColor(color, &dst->descriptor);
            if (dst->bytesPerPixel == 2)
            {
                unsigned short word = (unsigned short)packed;
                memcpy(d, &word, sizeof(word));
            }
            else
                memcpy(d, &packed, sizeof(packed));
        }
        else
            grWriteDataTypeColor(d, dst->dataType, dst->imageFormat, color);
    }
}

static void test_layouts(void)
{
    enum { WIDTH = 263, ROWS = 3, CAPACITY = (WIDTH * 4 + 16) * ROWS + 8 };
    unsigned char source[CAPACITY], actual[CAPACITY], expected[CAPACITY];
    static const int lengths[] = { 1, 7, 257 };
    uint32_t random = 0x1136;
    int s, d, alignment, lengthIndex, y, i;
    for (i = 0; i < CAPACITY; ++i)
    {
        random = random * 1664525u + 1013904223u;
        source[i] = (unsigned char)(random >> 24);
    }
    for (s = 0; s < 10; ++s)
    for (d = 0; d < 10; ++d)
    for (alignment = 0; alignment < 4; ++alignment)
    for (lengthIndex = 0; lengthIndex < 3; ++lengthIndex)
    {
        grMemoryImage_s src = image_format(s), dst = image_format(d), reference;
        int length = lengths[lengthIndex];
        src.width = dst.width = WIDTH;
        src.height = dst.height = ROWS;
        src.stride = WIDTH * src.bytesPerPixel + 16;
        dst.stride = WIDTH * dst.bytesPerPixel + 16;
        src.data = source + alignment;
        dst.data = actual + 3 - alignment;
        memset(actual, 0xA5, sizeof(actual));
        memcpy(expected, actual, sizeof(actual));
        reference = dst;
        reference.data = expected + 3 - alignment;
        if (alignment & 1)
        {
            src.data = (unsigned char *)src.data + (ROWS - 1) * src.stride;
            src.stride = -src.stride;
            dst.data = (unsigned char *)dst.data + (ROWS - 1) * dst.stride;
            reference.data = (unsigned char *)reference.data + (ROWS - 1) * dst.stride;
            dst.stride = reference.stride = -dst.stride;
        }
        for (y = 0; y < ROWS; ++y)
        {
            grMemoryImage_s rowSrc = src, rowDst = reference;
            rowSrc.data = (unsigned char *)src.data + y * src.stride;
            rowDst.data = (unsigned char *)reference.data + y * reference.stride;
            reference_span(&rowDst, &rowSrc, length);
            grrCopyColorSpan(&dst, &src, 2, y, 1, y, length, NULL);
        }
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    }
}

static void test_rgb565(void)
{
    unsigned short *source = malloc(65536 * sizeof(*source));
    unsigned char *result = malloc(65536 * 4);
    grMemoryImage_s src = image_format(8), dst = image_format(1);
    unsigned int i;
    CHECK(source != NULL && result != NULL);
    if (!source || !result)
        exit(1);
    for (i = 0; i < 65536; ++i)
        source[i] = (unsigned short)i;
    src.width = dst.width = 65536;
    src.height = dst.height = 1;
    src.data = source;
    dst.data = result;
    grrCopyColorSpan(&dst, &src, 0, 0, 0, 0, 65536, NULL);
    for (i = 0; i < 65536; ++i)
    {
        unsigned int r = i >> 11, g = (i >> 5) & 63, b = i & 31;
        CHECK(result[4 * i] == ((r << 3) | (r >> 2)) &&
              result[4 * i + 1] == ((g << 2) | (g >> 4)) &&
              result[4 * i + 2] == ((b << 3) | (b >> 2)) && result[4 * i + 3] == 255);
    }
    memset(source, 0, 65536 * sizeof(*source));
    grrCopyColorSpan(&src, &dst, 0, 0, 0, 0, 65536, NULL);
    for (i = 0; i < 65536; ++i)
        CHECK(source[i] == i);
    free(source);
    free(result);
}

static void test_overlapping_conversions(void)
{
    unsigned char actual[256], expected[256];
    int s, d, offset, i;
    for (s = 0; s < 10; ++s)
    for (d = 0; d < 10; ++d)
    for (offset = -8; offset <= 8; ++offset)
    {
        grMemoryImage_s src = image_format(s), dst = image_format(d), refSrc, refDst;
        /* Identical full descriptors use memcpy, whose inputs must not overlap. */
        if (s == d && src.useDescriptor && src.descriptor.colorMask == GR_COLORMASK_FULL)
            continue;
        for (i = 0; i < (int)sizeof(actual); ++i)
            actual[i] = (unsigned char)(i * 19 + 7);
        memcpy(expected, actual, sizeof(actual));
        src.width = dst.width = 32;
        src.height = dst.height = 1;
        src.data = actual + 64;
        dst.data = actual + 64 + offset;
        refSrc = src;
        refDst = dst;
        refSrc.data = expected + 64;
        refDst.data = expected + 64 + offset;
        reference_span(&refDst, &refSrc, 11);
        grrCopyColorSpan(&dst, &src, 2, 0, 1, 0, 11, NULL);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    }
}

int main(void)
{
    test_layouts();
    test_rgb565();
    test_overlapping_conversions();
    printf("Blit: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
