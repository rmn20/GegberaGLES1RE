/* grDefs.h - core definitions: surface descriptors, rectangles, stack
 * allocator, image/data format enums.
 *
 * Port of the decompiled grDefs.h (functions were inline in the original
 * header; they appear as 2-3 identical TU copies in the binary - a single
 * definition is provided in grDefs.c). Struct layouts follow the original
 * field accesses; pointer fields widen on 64-bit hosts.
 */
#ifndef GR_DEFS_H
#define GR_DEFS_H

#include "hg/hgDefs.h"
#include "gerbera/grUtil.h"

HG_EXTERN_C_BEGIN

/* --- image formats -------------------------------------------------------
 * grGetImageFormatNumComponents' bit table (0x11A319 >> 3*f) & 7 gives
 * components {1,3,4,1,2,3,4} for f = 0..6, which pins the numeric order
 * below (matching the switch cases in the decompile). */
enum GR_IMAGEFORMAT_e
{
    GR_IMAGEFORMAT_ALPHA = 0,
    GR_IMAGEFORMAT_RGB = 1,
    GR_IMAGEFORMAT_RGBA = 2,
    GR_IMAGEFORMAT_LUMINANCE = 3,
    GR_IMAGEFORMAT_LUMINANCE_ALPHA = 4,
    GR_IMAGEFORMAT_BGR = 5,
    GR_IMAGEFORMAT_BGRA = 6,
    GR_IMAGEFORMAT_6 = 6,            /* legacy alias */
    GR_IMAGEFORMAT_MAX = 7
};

/* --- data types ----------------------------------------------------------
 * grGetDataTypeSize_dataTypeSizes[12] verbatim from the binary (line 5690).
 * GR_DATATYPE_MAX = 9, as checked by grIsValidDataType.
 * Names pinned from the vertex-array fetchers (grUpdateVertexPipeline
 * switches): 0 = INT8 (normal arrays; default assert "type ==
 * GR_DATATYPE_INT8"), 1 = UINT8 (color arrays), 2 = INT16, 6 = FLOAT
 * ("color_array->type == GR_DATATYPE_FLOAT"), 7 = FIXED
 * ("sizeArray->type == GR_DATATYPE_FIXED"). */
enum GR_DATATYPE_e
{
    GR_DATATYPE_INT8 = 0,       /* size 1 */
    GR_DATATYPE_UINT8 = 1,      /* size 1 */
    GR_DATATYPE_INT16 = 2,      /* size 2 */
    GR_DATATYPE_UINT16 = 3,     /* size 2 */
    GR_DATATYPE_INT32 = 4,      /* size 4 */
    GR_DATATYPE_UINT32 = 5,     /* size 4 */
    GR_DATATYPE_4 = 4,          /* legacy alias */
    GR_DATATYPE_5 = 5,          /* legacy alias */
    GR_DATATYPE_FLOAT = 6,      /* size 4 */
    GR_DATATYPE_FIXED = 7,      /* size 4 */
    GR_DATATYPE_BOOL = 8,       /* size 1 */
    GR_DATATYPE_8 = 8,          /* legacy alias */
    GR_DATATYPE_MAX = 9,        /* pinned from CodeView @enum$3702oglFast_c */
    GR_DATATYPE_DESCRIPTOR = 9, /* size 0 (packed pixel formats) */
    GR_DATATYPE_9 = 9,          /* legacy alias */
    GR_DATATYPE_10 = 10,        /* size 0 */
    GR_DATATYPE_11 = 11         /* size 0 */
};

extern const char grGetDataTypeSize_dataTypeSizes[12];

/* --- pixel / palette format bounds (grUtilSmall.inl range asserts) ------- */
#define GR_PIXELFORMAT_MAX 6
#define GR_PALETTEFORMAT_MAX 3
#define GR_MAX_POINT_SIZE 64
#define GR_MAX_LINE_WIDTH 64

enum GR_PALETTEFORMAT_e
{
    GR_PALETTEFORMAT_NONE = 0,
    GR_PALETTEFORMAT_4BIT = 1,
    GR_PALETTEFORMAT_8BIT = 2
};

/* --- surface descriptor (decompile type cv_anon_14F9) --------------------
 * rgbaOffsets/rgbaSizes are PACKED BYTES: the decompile clears/compares/copies
 * them as whole ints (*(int*)a->rgbaOffsets = 0 in grClearDescriptor, and
 * grGetDrawBufferDescriptor moves 4-byte chunks between drawSurface and the
 * descriptor). colorMask + the 3 *Bytes form the next int; pixelFormat sits
 * at int offset 3 (drawSurface int 3 -> desc->pixelFormat), so paletteFormat
 * and padding[2] trail it. */
typedef struct _grSurfaceDescriptor
{
    unsigned char rgbaOffsets[4];
    unsigned char rgbaSizes[4];
    unsigned char colorMask;
    unsigned char colorBytes;
    unsigned char depthBytes;
    unsigned char stencilBytes;
    unsigned char pixelFormat;      /* int 3 byte 12 */
    unsigned char paletteFormat;    /* int 3 byte 13 */
    unsigned char padding[2];       /* int 3 bytes 14/15 */
} _grSurfaceDescriptor;             /* 16 bytes: ints 0..3 */

/* --- draw surface (decompile cv_anon_1686; first 4 ints ARE the descriptor
 * view, then width/height (assert strings name them), then per-buffer
 * {data, stride} records with stride at ints 7/9/11; 48 bytes on 32-bit -
 * grSetSurfaces copies 0x30. grrClear* use this struct AS the descriptor
 * (grPackColor(&surface->descriptor) over the same 16-byte view). ------- */
typedef struct _grSurfaceBuffer
{
    void *data;
    int stride;
} _grSurfaceBuffer;

enum GR_BUFFERTYPE_e
{
    GR_BUFFERTYPE_COLOR = 0,
    GR_BUFFERTYPE_DEPTH = 1,
    GR_BUFFERTYPE_STENCIL = 2
};

typedef struct _grDrawSurface
{
    unsigned char rgbaOffsets[4];
    unsigned char rgbaSizes[4];
    unsigned char colorMask;
    unsigned char colorBytes;
    unsigned char depthBytes;
    unsigned char stencilBytes;
    unsigned char pixelFormat;
    unsigned char paletteFormat;
    unsigned char padding[2];
    int width;
    int height;
    _grSurfaceBuffer buffers[3];
} _grDrawSurface;

/* --- rectangle (decompile type cv_anon_1475) ----------------------------- */
typedef struct _grRect
{
    int x0, x1, y0, y1;
} _grRect;

/* --- clear-record queue (grClearBuffers grApiSmall.inl 388 queue side,
 * grFlushBufferClears grApiFast.inl 26 flush side). EPOC: the queue index
 * sits at state+7472 and the records at state+7476, 36 bytes (9 dwords)
 * each - the WINSCW decompile reaches them as clearInfos[9*i]. Field order
 * from the EPOC stores: the scissor snapshot, the clear values, then the
 * byte pack bufferMask/colorMask/depthMask/stencilWriteMask/useDithering. */
#define GR_CLEAR_BUFFER_SIZE 4

typedef struct _grClearInfo
{
    _grRect scissor;                /* +0:  surfaceScissor snapshot x0,x1,y0,y1 */
    unsigned int clearColor;        /* +16 */
    int clearDepth;                 /* +20 */
    unsigned int clearStencil;      /* +24 */
    unsigned char bufferMask;       /* +28: 1=color 2=depth 4=stencil */
    unsigned char colorMask;        /* +29 */
    unsigned char depthMask;        /* +30 */
    unsigned char stencilWriteMask; /* +31 */
    unsigned char useDithering;     /* +32 */
} _grClearInfo;

/* --- integer 3-vector (decompile type cv_anon_16FD; accessed via &v->x) --- */
typedef struct _grVector3i
{
    int x, y, z;
} _grVector3i;

/* --- stack allocator (decompile type cv_anon_148D) ----------------------- */
typedef struct _grStackAllocator
{
    void *data;
    int size;
    int allocated;
} _grStackAllocator;

/* --- descriptor helpers (grDefs.h) -------------------------------------- */
void grClearDescriptor(_grSurfaceDescriptor *a);
void grSetDescriptorSizes(_grSurfaceDescriptor *desc, int rSize, int gSize, int bSize, int aSize);
void grSetDescriptorOffsets(_grSurfaceDescriptor *desc, int rOffset, int gOffset, int bOffset, int aOffset);
int  grDescriptorsEqual(_grSurfaceDescriptor *a, _grSurfaceDescriptor *b);
int  grGetDescriptorSize(_grSurfaceDescriptor *desc, int channel);
int  grGetDescriptorOffset(_grSurfaceDescriptor *desc, int channel);

/* --- rectangles (grDefs.h) ---------------------------------------------- */
void grSetRectangle(_grRect *rect, int x0, int x1, int y0, int y1);
int  grIsRectangleEmpty(_grRect *rect);
int  grIsRectangleInRange(_grRect *rect, int mn, int mx);

/* --- data types / image formats (grDefs.h) ------------------------------ */
int grGetDataTypeSize(int dataType);
int grGetImageFormatNumComponents(int imageFormat);

/* --- stack allocator (grDefs.h) ----------------------------------------- */
int   grIsStackValid(_grStackAllocator *allocator);
int   grStackGetFreeSpace(_grStackAllocator *allocator);
void  grStackInit(_grStackAllocator *allocator, void *data, int size);
void *grStackGetPtr(_grStackAllocator *allocator, int offset);
void *grStackAllocate(_grStackAllocator *allocator, int size);
void  grStackFree(_grStackAllocator *allocator, void *ptr);

/* --- descriptor <-> color utilities (grUtilSmall.inl) -------------------- */
int          grIsValidDescriptor(_grSurfaceDescriptor *desc);
unsigned int grPackColor(unsigned int color, _grSurfaceDescriptor *desc);
int          grMaskedDescriptorsEqual(unsigned int mask, _grSurfaceDescriptor *a, _grSurfaceDescriptor *b);
unsigned int grGetPackedColorWriteMask(_grSurfaceDescriptor *desc, unsigned int colorMask);
unsigned int grUnpackColor(unsigned int color, _grSurfaceDescriptor *desc);
unsigned int grDitherColor(unsigned int color, _grSurfaceDescriptor *desc, int x, int y);

/* --- alignment / bool validation (grSmall.c) ----------------------------- */
int grIsAligned(const void *ptr, int alignment);
int grIsValidBool(int v);

HG_EXTERN_C_END

/* core asserts follow the decompile pattern:
 *   if (!cond && grAssert("cond", file, line) != 0) grDie(0);
 * i.e. grAssert prints and returns 1, grDie is fatal. */
#define GR_DEFS_CHECK(cond, file, line)          \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, (file), (line)) != 0) \
            grDie(0);                            \
    } while (0)

#endif /* GR_DEFS_H */
