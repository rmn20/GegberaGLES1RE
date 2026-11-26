/* grState.h - the Gerbera driver context (decompile type Gerbera_s).
 *
 * Original field names and layouts are preserved in the Nokia WINSCW
 * CodeView types. Gerbera_s and _grLight below use a portable host layout;
 * offset comments describe the original ARM build unless marked WINSCW.
 * The WINSCW context includes eight-byte state guards and has offsets eight
 * bytes higher than ARM for the shared fields after stateGuardBegin.
 * Capability values come from the Nokia debug enum, also checked against
 * the ARM API mapping and state accesses.
 */
#ifndef GR_STATE_H
#define GR_STATE_H

#include "hg/hgDefs.h"
#include "gerbera/grDefs.h"
#include "gerbera/grTexture.h"
#include "gerbera/hc/hcBackend.h"
#include "hg/hgInt32.h"
#include "hg/hgFloat.h"
#include "imath/imApi.h"

HG_EXTERN_C_BEGIN

#define GR_CAPABILITY_MAX 54
#define GR_MAX_LIGHTS 8
#define GR_MAX_USER_CLIP_PLANES 6
#define GR_MAX_DISPLAY_DIMENSION 2048
#define GR_EDGE_SLOPE_CACHE_SIZE 64
#define GR_MAX_VIEWPORT_DIMENSION 2048
#define GR_CLEAR_BUFFER_SIZE 4
#if GERBERA_ARMV6_JIT
#define GR_PIPELINE_CACHE_ENTRIES 64
#define GR_PIPELINE_CACHE_BYTES 0xC000u
#elif defined(__arm__) || defined(__SYMBIAN32__)
#define GR_PIPELINE_CACHE_ENTRIES 32
#define GR_PIPELINE_CACHE_BYTES 0x6000u
#else
#define GR_PIPELINE_CACHE_ENTRIES 32
#define GR_PIPELINE_CACHE_BYTES 0x3000u
#endif
#define GR_COLORMASK_FULL 0xFu
#define GR_SIDEMASK_BOTH 3u
/* triangle side (grClipPrimitive/grQueuePrimitive: screen-area sign ^ front-
 * Face); GR_SIDEMASK picks the bits to cull (grSetCullMode) */
#define GR_SIDE_FRONT 0
#define GR_SIDE_BACK 1
/* scissor outside-bits built by grClipPrimitive's per-vertex recheck */
#define GR_SCISSORMASK_FULL 0xFu
#define GR_SCISSORMASK_LEFT 0x1u
#define GR_SCISSORMASK_RIGHT 0x2u
#define GR_SCISSORMASK_TOP 0x4u
#define GR_SCISSORMASK_BOTTOM 0x8u

/* matrix modes (grSetMatrix / grSetMatrixDirty). MODELVIEW=0 and
 * PROJECTION=1 are pinned from the EPOC grSetupCulling (objectToClip =
 * matrix[1] * matrix[0], dirty bits 0/1); texture units sit at 2,3 (dirty
 * bit i+2 per unit) and the matrix-palette matrices at 4..12 (dirty bit
 * j+4). grSetMatrix accepts 0..GR_MATRIXMODE_MAX-1; the grUtil.h dirty
 * helpers check 0..GR_MATRIXMODE_MAX inclusive (verbatim, one extra). */
enum GR_MATRIXMODE_e
{
    GR_MATRIXMODE_MODELVIEW = 0,
    GR_MATRIXMODE_PROJECTION = 1,
    GR_MATRIXMODE_TEXTURE0 = 2,
    GR_MATRIXMODE_TEXTURE1 = 3,
    GR_MATRIXMODE_PALETTE0 = 4,
    GR_MATRIXMODE_MATRIXPALETTE0 = 4,
    GR_MATRIXMODE_PALETTE_LAST = 12,
    GR_MATRIXMODE_MAX = 13
};

/* clip-plane slots in the grSmall enum space (grSetUserClipPlane planes
 * 10..15 == GR_CLIP_USER0 .. GR_CLIP_USER0+5). The ogl GL_CLIP_PLANE0..5
 * tokens map to capabilities 30..35 - a different space. */
#define GR_CLIP_USER0 10

/* primitive types (grGetPrimitiveVertices asserts TRIANGLE/LINE/POINT/
 * TEXTURE_RECT membership, grUtil.h 160). POINT = 1 is pinned twice: the
 * point-size-array selection runs when grGetPrimitiveVertices(pt) == 1 and
 * the EPOC grUpdateVertexPipeline gates it on primitiveType byte == 1;
 * the point-distance-attention update runs when primitiveType == 0, so 0
 * is a different type. The EPOC render path checks
 * (EPOC mode->type table dword_350DC: POINTS->1, LINES->2; the cull path
 * tests TRIANGLE == 3 and DrawTexOES passes TEXTURE_RECT == 4 literally).
 * Value 0 never arrives through the table - grUpdateVertexPipeline gates the
 * point-distance-attention update on a raw primitiveType == 0 comparison. */
enum GR_PRIMITIVETYPE_e
{
    GR_PRIMITIVETYPE_POINT = 1,
    GR_PRIMITIVETYPE_LINE = 2,
    GR_PRIMITIVETYPE_TRIANGLE = 3,
    GR_PRIMITIVETYPE_TEXTURE_RECT = 4
};

/* grSetupCulling result (EPOC returns 1 on every STOP path, 0 at the end). */
enum GR_COMMAND_e
{
    GR_COMMAND_RENDER = 0,
    GR_COMMAND_STOP = 1
};

/* position states stored in the top 2 bits of _grVertex.stateMask
 * (grTransformVertex / grGetFogZ): names 2/3 partially pinned - the assert
 * names GR_POSITION_OBJECT_SPACE_FIXED (358) and GR_POSITION_EYE_SPACE_
 * CARTESIAN (1911); 2 = eye space from the matrix-palette path (accfv into
 * position then projection), exact name unpinned. */
enum GR_POSITION_e
{
    GR_POSITION_OBJECT_SPACE_FLOAT = 0,
    GR_POSITION_OBJECT_SPACE_FIXED = 1,
    GR_POSITION_EYE_SPACE = 2,           /* name unpinned */
    GR_POSITION_EYE_SPACE_CARTESIAN = 3
};

/* grVertex stateMask bits (grTransformVertex / grRasterizer asserts). The
 * top 2 bits (14..15) hold the position state (grGetVertexPositionState:
 * stateMask >> 14): 0 = object space float, 1 = object space fixed,
 * 2 = eye space float, 3 = eye space cartesian. */
#define GR_VERTEXMASK_SURVIVED_2D_REJECTION 0x1u
#define GR_VERTEXMASK_SURVIVED_3D_REJECTION 0x2u
#define GR_VERTEXMASK_TRANSFORMED 0x4u
#define GR_VERTEXMASK_VIEWPORT_TRANSFORMED 0x8u
#define GR_VERTEXMASK_COLOR_COMPUTED 0x20u
#define GR_VERTEXMASK_INTERPOLATE_GRADIENTS 0x40u
#define GR_VERTEXMASK_FORCE_CULL 0x80u
#define GR_VERTEXMASK_APPROXIMATE_POSITION 0x100u

/* clip mask (vertex->clipMask, u16): bits 0..5 = guardband planes 0..5
 * (IVector4_clip), bits 6..9 = guardband w tests, bits 10..15 = user clip
 * planes 0..5. GR_CLIPMASK_CLIPPING_PLANES selects the bits grClipper acts
 * on (grClipper.inl 396). */
#define GR_CLIPMASK_CLIPPING_PLANES 0xFC3Fu

/* renderStack vertex buffer regions (records at renderStack+1332, 128B):
 * 0..63 the vertex cache (grVertexCacheGet), 64..91 the rasterizer cache,
 * 92..107 the clipper temp vertices (grVertexCacheGetTemp: index+92). */
#define GR_VERTEX_CACHE_SIZE 64
#define GR_FLOAT_TRANSFORM_MISS_LIMIT 4
#define GR_NUM_RASTERIZER_CACHE_VERTICES 28
#define GR_MAX_CLIPPER_GENERATED_VERTICES 12
#define GR_MAX_CLIPPER_VERTICES 15

/* the grFast vertex cache (cv_anon_16F8, 168 bytes; grSetupRendering sets
 * state->vertexCache = state->renderStack, i.e. it lives at renderStack+0,
 * followed by the command queue at +168). Offsets pinned from grVertexCache-
 * Get (dll.c 32127: batchLockMask = *(long long*)cache, free-slot counter =
 * cache->clipPosition[0].e as cv_anon_1712 = byte 8; hash table base =
 * cache->color... = byte 40) and grQueuePrimitive (u32 accesses: 16/20/24 =
 * previous primitive's vertex indices, 28/32/36 = their cache indices). */
typedef struct _grVertexCache
{
    HGint64 batchLockMask;         /* +0:  per-slot lock bits (hgGet/SetBit64) */
    int totalFreeSlots;            /* +8:  decremented when a slot locks */
    int allocIndex;                /* +12 */
    int prevVertexIndices[3];      /* +16: last queued primitive's vertex idxs */
    int prevCacheIndices[3];       /* +28: their cache indices */
    unsigned int cacheIndices[32]; /* +40: hash table, 4 packed bytes each */
} _grVertexCache;                  /* 168 bytes */

/* API-side primitive types (grRenderElements/grRenderArrays; fetchType =
 * 3*primitiveType + source, source 0 = u8 indices, 1 = u16, 2 = sequential).
 * TEXTURE_RECT draws bypass this family via grRenderTextureRect. */
enum GR_PRIMITIVE_e
{
    GR_PRIMITIVE_POINTS = 0,
    GR_PRIMITIVE_LINES = 1,
    GR_PRIMITIVE_LINE_LOOP = 2,
    GR_PRIMITIVE_LINE_STRIP = 3,
    GR_PRIMITIVE_TRIANGLES = 4,
    GR_PRIMITIVE_TRIANGLE_STRIP = 5,
    GR_PRIMITIVE_TRIANGLE_FAN = 6,
    GR_PRIMITIVE_TEXTURE_RECT = 7,
    GR_PRIMITIVE_MAX = 8
};

/* fetch walkers (grFetchPrimitives switch, values 0..20) */
enum GR_FETCHTYPE_e
{
    GR_FETCHTYPE_POINTSchar = 0,
    GR_FETCHTYPE_POINTS_SHORT = 1,
    GR_FETCHTYPE_POINTS_ARRAY = 2,
    GR_FETCHTYPE_LINESchar = 3,
    GR_FETCHTYPE_LINES_SHORT = 4,
    GR_FETCHTYPE_LINES_ARRAY = 5,
    GR_FETCHTYPE_LINELOOPchar = 6,
    GR_FETCHTYPE_LINELOOP_SHORT = 7,
    GR_FETCHTYPE_LINELOOP_ARRAY = 8,
    GR_FETCHTYPE_LINESTRIPchar = 9,
    GR_FETCHTYPE_LINESTRIP_SHORT = 10,
    GR_FETCHTYPE_LINESTRIP_ARRAY = 11,
    GR_FETCHTYPE_TRIANGLESchar = 12,
    GR_FETCHTYPE_TRIANGLES_SHORT = 13,
    GR_FETCHTYPE_TRIANGLES_ARRAY = 14,
    GR_FETCHTYPE_TRISTRIPchar = 15,
    GR_FETCHTYPE_TRISTRIP_SHORT = 16,
    GR_FETCHTYPE_TRISTRIP_ARRAY = 17,
    GR_FETCHTYPE_TRIFANchar = 18,
    GR_FETCHTYPE_TRIFAN_SHORT = 19,
    GR_FETCHTYPE_TRIFAN_ARRAY = 20,
    GR_FETCHTYPE_MAX = 21
};

/* gradient identifiers (_enum_4510grFast_c): the pixel-pipe adder/reference
 * arrays are indexed by this enum (grSetupGradient/grSetupTriangleGradients;
 * DEPTH=0/FOG=1 pinned by the mask walk `for g = GR_GRADIENT_FOG; g >=
 * GR_GRADIENT_DEPTH`, colors 2..5 by `gradient = g + 2`, texcoords 6..9 by
 * `gradient = coord + 2*texIndex + 6`) */
enum GR_GRADIENT_e
{
    GR_GRADIENT_DEPTH = 0,
    GR_GRADIENT_FOG = 1,
    GR_GRADIENT_COLOR_R = 2,
    GR_GRADIENT_COLOR_G = 3,
    GR_GRADIENT_COLOR_B = 4,
    GR_GRADIENT_COLOR_A = 5,
    GR_GRADIENT_TEX0_S = 6,
    GR_GRADIENT_TEX0_T = 7,
    GR_GRADIENT_TEX1_S = 8,
    GR_GRADIENT_TEX1_T = 9,
    GR_GRADIENT_MAX = 10
};

/* Per-texture-unit record inside the pixel pipe. Original 32-bit offsets:
 * unit = pixelPipe + 64*texIndex; fields +4 pixels/+8 palette/+12 linear/
 * +16 sMask/+20 tMask/+24 rShift[2]/+32 log2DimS. */
typedef struct _grPixelPipeTextureUnit
{
    unsigned char pad0[4];          /* +0  */
    void *pixels;                   /* +4  current mipmap pixel pointer */
    void *palette;                  /* +8  texture->palette (tex+176) */
    int linear;                     /* +12 mipmap flags bit0 (palette/
                                     *     linear filtering) */
    int sMask;                      /* +16 (1 << log2DimS) - 1 */
    int tMask;                      /* +20 (1 << log2DimT) - 1 */
    int rShift[2];                  /* +24 s/t fixed-point output shifts */
    int log2DimS;                   /* +32 current mipmap log2 width */
    unsigned char pad1[28];         /* +36..63 */
} _grPixelPipeTextureUnit;          /* 64 bytes on 32-bit */

/* Pixel-pipe parameters (cv_anon_1722) live after the backend scratch area
 * at renderStack+14644. The scanline buffer starts at renderStack+15668.
 * Offsets below describe the original 32-bit layout; pointers widen on
 * 64-bit hosts.
 *
 * The two 64-byte texture units are at +0/+64 (grSetupTextureUnit)
 * and the gradient arrays at +192/+232/+272 (grSetupTriangleGradients'
 * flat-shade path reaches them raw as pixelPipe+192/232/272). The
 * grSetupRendering head block (grApiFast.inl 288-432) pins +128..+167:
 * the dither kernel pointer, then the grCheckPixelPipeStack assert-order
 * block (strides, fog, alpha/stencil refs) at +132..+167. */
typedef struct _grPixelPipe
{
    _grPixelPipeTextureUnit textureUnit[2];   /* +0, +64 */
    void *ditherKernel;                       /* +128 g_orderedDitherKernel
                                               * (grSetupRendering) */
    struct                                    /* +132: grCheckPixelPipeStack
                                               * assert-order block */
    {
        int colorBufferStride;
        int depthBufferStride;
        int stencilBufferStride;
        unsigned int fogColor;
        int alphaReference;
        unsigned int stencilReference;
        unsigned char stencilReferenceMasked;
        unsigned int stencilTestMask;
        unsigned int stencilWriteMask;
    } checked;                                /* 36 bytes, +132..167 */
    void *colorRow;                           /* +168 drawSurface buffers[0]
                                               * .data + stride*yStart */
    void *depthRow;                           /* +172 buffers[1] */
    void *stencilRow;                         /* +176 buffers[2] */
    int numScanlines;                         /* +180 (grScanConvertTriangle) */
    unsigned int *scanlineInfo;               /* +184 renderStack+15668 */
    int yStart;                               /* +188 */
    int reference[GR_GRADIENT_MAX];           /* +192 (word 48) */
    int xAdder[GR_GRADIENT_MAX];              /* +232 (word 58) */
    int yAdder[GR_GRADIENT_MAX];              /* +272 (word 68) */
    int mutableParams[78];                    /* debug poison scratch */
    unsigned char tail[8040];                 /* reserved trailing storage */
} _grPixelPipe;

#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _grPixelPipe_check_ref_offset[(offsetof(_grPixelPipe, reference) == 192) ? 1 : -1];
typedef char _grPixelPipe_check_xadd_offset[(offsetof(_grPixelPipe, xAdder) == 232) ? 1 : -1];
typedef char _grPixelPipe_check_yadd_offset[(offsetof(_grPixelPipe, yAdder) == 272) ? 1 : -1];
#endif

/* texture-rect gradient setup info (grSetupTextureRectGradients,
 * grApiFast.inl 1687; DLL 33077-33172). The caller builds this on its
 * stack: z (the rect's depth), the screen-space rect corners fx0/fy0/
 * fx1/fy1, and per-unit texture-coordinate ranges txMin/tyMin/txMax/
 * tyMax (pointers to int arrays indexed by unit). */
typedef struct _grTRGradSetupInfo
{
    int z;              /* +0  rect depth (IScalar) */
    int fx0;            /* +4  screen x of the rect's first corner */
    int fy0;            /* +8  screen y of the rect's first corner */
    int fx1;            /* +12 screen x of the second corner */
    int fy1;            /* +16 screen y of the second corner */
    int *txMin;         /* +20 per-unit texture s minimum */
    int *tyMin;         /* +24 per-unit texture t minimum */
    int *txMax;         /* +28 per-unit texture s maximum */
    int *tyMax;         /* +32 per-unit texture t maximum */
} _grTRGradSetupInfo;

/* gradient-setup context grSetupGradient/grComputeDeltas operate on
 * (cv_anon_17A0; grSetupTriangleGradients builds one on the stack). Field
 * order = its stack layout: pPP, v0 screen coords (12 fractional bits,
 * already offset by -4096),
 * the four 16*delta*recipArea edge derivatives, the normalization shift. */
typedef struct _grGradientInfo
{
    _grPixelPipe *pPP;     /* the pixel-pipe record */
    int v0sx;              /* record0.sx - 4096 */
    int v0sy;              /* record0.sy - 4096 */
    int v01sxPerArea;      /* hgMul64h(16*(v1.sx - v0.sx), signed recip) */
    int v01syPerArea;      /* hgMul64h(16*(v1.sy - v0.sy), signed recip) */
    int v02sxPerArea;      /* hgMul64h(16*(v2.sx - v0.sx), signed recip) */
    int v02syPerArea;      /* hgMul64h(16*(v2.sy - v0.sy), signed recip) */
    int shift;             /* 31 - reciprocal shift */
} _grGradientInfo;

/* compiled-pixel-pipeline cache (grPipelineCache.inl, cv_anon_172A). The
 * cache keeps JIT-compiled pipeline programs in an arena; entries are
 * 64 bytes (grCacheInit zeroes entryCount << 6). The signature is the 40-
 * byte compressed pipeline descriptor (cv_anon_1725, grCompressSignature). */
typedef struct _grCacheSignature
{
    unsigned char bytes[40];
} _grCacheSignature;

typedef struct _grCacheEntry
{
    _grCacheSignature signature;    /* 40 bytes */
    unsigned int gradientMask;
    unsigned int timeStamp;
    int dataOffset;
    int dataSize;
    struct _grCacheEntry *prev;
    struct _grCacheEntry *next;
} _grCacheEntry;                    /* 64 bytes in the 32-bit DLL
                                       * (entryCount << 6 zeroing); larger
                                       * with hosted pointers */

typedef struct _grPipelineCache
{
    _grCacheEntry *firstUsed;
    _grCacheEntry *firstFree;
    _grCacheEntry *firstUnused;
    _grCacheEntry *entries;
    void *arena;
    int arenaSize;
    int entryCount;
    unsigned int timeStamp;
} _grPipelineCache;                 /* 32 bytes in the 32-bit DLL
                                       * (hgMemSet(cache, 0, 0x20)) */

/* pipeline signature (cv_anon_18AA = grSignature, 144 bytes): the full
 * pixel-pipeline configuration snapshot. Layout reconstructed from
 * grEvaluatePixelPipeline's named writes + grCompressSignature's byte walk;
 * byte positions are constrained by s_mainBitCounts/s_textureBitCounts
 * (a field's value must fit its byte's bit count - the compress asserts
 * verify). texture[] records retain byte indexing shared by the signature
 * evaluator and bytecode generator. */
typedef struct _grSignature
{
    _grSurfaceDescriptor descriptor;    /* +0: 16 bytes (TI=0x18AA) */
    unsigned char isValid;              /* +16 */
    unsigned char colorMask;            /* +17 (4 bits) */
    unsigned char depthTest;            /* +18 */
    unsigned char alphaTest;            /* +19 */
    unsigned char alphaFunc;            /* +20 (3 bits) */
    unsigned char depthMask;            /* +21 */
    unsigned char depthFunc;            /* +22 (3 bits) */
    unsigned char depthClampMin;        /* +23 */
    unsigned char depthClampMax;        /* +24 */
    unsigned char logicOpEnabled;       /* +25 */
    unsigned char logicOp;              /* +26 (4 bits) */
    unsigned char blendEnabled;         /* +27 */
    unsigned char srcFactor;            /* +28 (4 bits) */
    unsigned char dstFactor;            /* +29 (4 bits) */
    unsigned char flatShading;          /* +30 */
    unsigned char stencilTest;          /* +31 */
    unsigned char stencilTestMaskFull;  /* +32 */
    unsigned char stencilWriteMaskFull; /* +33 */
    unsigned char stencilFunc;          /* +34 (3 bits) */
    unsigned char stencilFail;          /* +35 (3 bits) */
    unsigned char stencilZFail;         /* +36 (3 bits) */
    unsigned char stencilZPass;         /* +37 (3 bits) */
    unsigned char stencilZOpsEqual;     /* +38 */
    unsigned char stencilAllOpsEqual;   /* +39 */
    unsigned char fogEnabled;           /* +40 */
    unsigned char monochromeFog;        /* +41 */
    unsigned char writeColor;           /* +42 */
    unsigned char constantWriteColor;   /* +43 */
    unsigned char applyColorWriteMask;  /* +44 */
    unsigned char dither;               /* +45 */
    unsigned char constantDepth;        /* +46 */
    unsigned char perPixelPack;         /* +47 */
    unsigned char screenRectGradients;  /* +48 (0/1 flag) */
    unsigned char padding[7];           /* +49..55 (zero bit counts) */
    unsigned char texture[2][44];       /* +56: per-unit records (TI=0x18A7) */
} _grSignature;                         /* 144 bytes */

/* grClassifyTriangle result: Nokia CodeView TI=0x17B6 and UIQ TI=0x178A. */
enum GR_TRIANGLESTATUS_e
{
    GR_TRIANGLESTATUS_AFFINE = 0,       /* draw as-is */
    GR_TRIANGLESTATUS_NONAFFINE = 1,    /* subdivide */
    GR_TRIANGLESTATUS_DISCARD = 2
};

/* scan-conversion point (cv_anon_16FD: aliases &record->screenPosition[0])
 * and edge pair (cv_anon_1787): [0] = left edge, [1] = right edge, x in
 * 18.14 (pixel<<14), adders per scanline */
typedef struct _grRasterPoint
{
    int x;
    int y;
} _grRasterPoint;

typedef struct _grEdgePair
{
    int xStart[2];
    int xAdder[2];
} _grEdgePair;

/* compiled pixel-pipeline program (state->pixelPipelineCode, set by the
 * hc compiler; grExecutePixelPipeline calls it with the pipe record) */
typedef void (*grPixelPipelineCode_fn)(void *pipe);

/* the queued-primitive record handed to the rasterizer draws
 * (cv_anon_17A7): packed cache indices (+ flags: bit 24 = triangle side,
 * bit 28 = needs clipping), the rounded screen area, the side */
typedef struct _grClipTriangle
{
    unsigned int indices;
    int area;
    int side;
} _grClipTriangle;

/* render stack (cv_anon_1717) byte offsets used by the grFast queue:
 * +0 the vertex cache, +168 command queue (8-byte commands, u32 index
 * counter at +1192 counting DWORDS), +1196/+1264 fetch lists, +1332 vertex
 * buffer, 23860 bytes total (grApiFast.inl 381). */
#define GR_RENDER_STACK_COMMAND_QUEUE_INDEX 1192
#define GR_RENDER_STACK_PIXEL_PIPELINE 14644
#define GR_RENDER_STACK_SCANLINE_INFO 15668  /* &renderStack[1958].whichPool;
                                              * u32 left|numPixels<<16 per
                                              * scanline, up to 2048 entries
                                              * (grScanConvertTriangle); ends
                                              * exactly at the stack end */
#define GR_RENDER_STACK_SIZE 23860

/* winding (grSetCullMode) */
enum GR_WINDING_e
{
    GR_WINDING_CW = 0,
    GR_WINDING_CCW = 1
};

/* compare functions (grSetAlphaFunc/grSetStencilFunc/grSetDepthFunc) */
enum GR_FUNC_e
{
    GR_FUNC_NEVER = 0,
    GR_FUNC_LESS = 1,
    GR_FUNC_EQUAL = 2,
    GR_FUNC_LEQUAL = 3,
    GR_FUNC_GREATER = 4,
    GR_FUNC_NOTEQUAL = 5,
    GR_FUNC_GEQUAL = 6,
    GR_FUNC_ALWAYS = 7,
    GR_FUNC_MAX = 8
};

enum GR_STENCILOP_e
{
    GR_STENCILOP_KEEP = 0,
    GR_STENCILOP_REPLACE = 1,
    GR_STENCILOP_INCR = 2,
    GR_STENCILOP_DECR = 3,
    GR_STENCILOP_ZERO = 4,
    GR_STENCILOP_INVERT = 5,
    GR_STENCILOP_MAX = 6
};

enum GR_LOGICOP_e
{
    GR_LOGICOP_CLEAR = 0,
    GR_LOGICOP_AND = 1,
    GR_LOGICOP_AND_REVERSE = 2,
    GR_LOGICOP_COPY = 3,
    GR_LOGICOP_AND_INVERTED = 4,
    GR_LOGICOP_NOOP = 5,
    GR_LOGICOP_XOR = 6,
    GR_LOGICOP_OR = 7,
    GR_LOGICOP_NOR = 8,
    GR_LOGICOP_EQUIV = 9,
    GR_LOGICOP_INVERT = 10,
    GR_LOGICOP_OR_REVERSE = 11,
    GR_LOGICOP_COPY_INVERTED = 12,
    GR_LOGICOP_OR_INVERTED = 13,
    GR_LOGICOP_NAND = 14,
    GR_LOGICOP_SET = 15,
    GR_LOGICOP_MAX = 16
};

enum GR_BLENDFACTOR_e
{
    GR_BLENDFACTOR_ZERO = 0,
    GR_BLENDFACTOR_ONE = 1,
    GR_BLENDFACTOR_SRC_COLOR = 2,
    GR_BLENDFACTOR_ONE_MINUS_SRC_COLOR = 3,
    GR_BLENDFACTOR_SRC_ALPHA = 4,
    GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA = 5,
    GR_BLENDFACTOR_DST_ALPHA = 6,
    GR_BLENDFACTOR_ONE_MINUS_DST_ALPHA = 7,
    GR_BLENDFACTOR_DST_COLOR = 8,
    GR_BLENDFACTOR_ONE_MINUS_DST_COLOR = 9,
    GR_BLENDFACTOR_SRC_ALPHA_SATURATE = 10,
    GR_BLENDFACTOR_CONSTANT_COLOR = 11,
    GR_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR = 12,
    GR_BLENDFACTOR_CONSTANT_ALPHA = 13,
    GR_BLENDFACTOR_ONE_MINUS_CONSTANT_ALPHA = 14,
    GR_BLENDFACTOR_MAX = 15        /* 0..14 valid (grSetBlendFunc assert) */
};

/* fog modes (grUpdateVertexPipeline: mode 0 selects the FAST/LINEAR fog by
 * fogOORange, 1 = grApplyFogEXP, 2 = grApplyFogEXP2; grSetFogMode accepts
 * 0..GR_FOGMODE_MAX-1). GL fog modes EXP/EXP2/LINEAR map to 1/2/0. */
enum GR_FOGMODE_e
{
    GR_FOGMODE_LINEAR = 0,
    GR_FOGMODE_EXP = 1,
    GR_FOGMODE_EXP2 = 2,
    GR_FOGMODE_MAX = 3
};

/* Nokia CodeView LF_ENUMERATE field list TI=0x1596, enum TI=0x1597.
 * GR_CAPABILITY_MAX is 54 (defined above); VECTOR_SIZE is two 32-bit words.
 * These values differ from Gerbera 1.4.8's UIQ enum: that version has
 * VERTEX_ARRAY=20, LIGHT_MODEL_TWO_SIDE=29 and MAX=39.
 * Clip-plane capabilities 30..35 are distinct from GR_CLIP_USER0=10. */
enum GR_CAPABILITY_e
{
    GR_CAPABILITY_UNUSED = 0,
    GR_CAPABILITY_ALPHA_TEST = 1,
    GR_CAPABILITY_DEPTH_TEST = 2,
    GR_CAPABILITY_STENCIL_TEST = 3,
    GR_CAPABILITY_BLEND = 4,
    GR_CAPABILITY_COLOR_LOGIC_OP = 5,
    GR_CAPABILITY_DITHER = 6,
    GR_CAPABILITY_FOG = 7,
    GR_CAPABILITY_SEPARATE_SPECULAR_COLOR = 8,
    GR_CAPABILITY_SMOOTH_SHADING = 9,
    GR_CAPABILITY_TEXTURE0 = 10,
    GR_CAPABILITY_TEXTURE_LAST = 11,
    GR_CAPABILITY_DEPTH_OFFSET_POINT = 12,
    GR_CAPABILITY_DEPTH_OFFSET_LINE = 13,
    GR_CAPABILITY_DEPTH_OFFSET_FILL = 14,
    GR_CAPABILITY_SCISSOR_TEST = 15,
    GR_CAPABILITY_CULL_FACE = 16,
    GR_CAPABILITY_PERSPECTIVE_CORRECTION = 17,
    GR_CAPABILITY_LINE_SMOOTH = 18,
    GR_CAPABILITY_POINT_SMOOTH = 19,
    GR_CAPABILITY_POINT_SPRITE = 20,
    GR_CAPABILITY_VERTEX_ARRAY = 21,
    GR_CAPABILITY_NORMAL_ARRAY = 22,
    GR_CAPABILITY_COLOR_ARRAY = 23,
    GR_CAPABILITY_INDEX_ARRAY = 24,
    GR_CAPABILITY_TEXCOORD_ARRAY = 25,
    GR_CAPABILITY_TEXCOORD_ARRAY_LAST = 26,
    GR_CAPABILITY_MATRIX_INDEX_ARRAY = 27,
    GR_CAPABILITY_WEIGHT_ARRAY = 28,
    GR_CAPABILITY_POINT_SIZE_ARRAY = 29,
    GR_CAPABILITY_CLIP_USER0 = 30,
    GR_CAPABILITY_CLIP_USER_LAST = 35,
    GR_CAPABILITY_LIGHTING = 36,
    GR_CAPABILITY_NORMALIZE_NORMAL = 37,
    GR_CAPABILITY_RESCALE_NORMAL = 38,
    GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE = 39,
    GR_CAPABILITY_COLOR_MATERIAL = 40,
    GR_CAPABILITY_LIGHT0 = 41,
    GR_CAPABILITY_LAST_LIGHT = 48,
    GR_CAPABILITY_MULTISAMPLE = 49,
    GR_CAPABILITY_SAMPLE_ALPHA_TO_COVERAGE = 50,
    GR_CAPABILITY_SAMPLE_ALPHA_TO_ONE = 51,
    GR_CAPABILITY_SAMPLE_COVERAGE = 52,
    GR_CAPABILITY_MATRIX_PALETTE = 53,
    GR_CAPABILITY_VECTOR_SIZE = 2,

    /* Names used by the portable implementation. */
    GR_CAPABILITY_LOGIC_OP = GR_CAPABILITY_COLOR_LOGIC_OP,
    GR_CAPABILITY_TEXTURE_2D = GR_CAPABILITY_TEXTURE0,
    GR_CAPABILITY_TEXTURE_0 = GR_CAPABILITY_TEXTURE0,
    GR_CAPABILITY_TEXTURE_1 = GR_CAPABILITY_TEXTURE_LAST,
    GR_CAPABILITY_POLYGON_OFFSET_FILL = GR_CAPABILITY_DEPTH_OFFSET_FILL,
    GR_CAPABILITY_TEXTURE_COORD_ARRAY_0 = GR_CAPABILITY_TEXCOORD_ARRAY,
    GR_CAPABILITY_TEXTURE_COORD_ARRAY_1 = GR_CAPABILITY_TEXCOORD_ARRAY_LAST,
    GR_CAPABILITY_CLIP_PLANE0 = GR_CAPABILITY_CLIP_USER0,
    GR_CAPABILITY_CLIP_PLANE1 = GR_CAPABILITY_CLIP_USER0 + 1,
    GR_CAPABILITY_CLIP_PLANE2 = GR_CAPABILITY_CLIP_USER0 + 2,
    GR_CAPABILITY_CLIP_PLANE3 = GR_CAPABILITY_CLIP_USER0 + 3,
    GR_CAPABILITY_CLIP_PLANE4 = GR_CAPABILITY_CLIP_USER0 + 4,
    GR_CAPABILITY_CLIP_PLANE5 = GR_CAPABILITY_CLIP_USER_LAST,
    GR_CAPABILITY_NORMALIZE = GR_CAPABILITY_NORMALIZE_NORMAL,
    GR_CAPABILITY_LIGHT7 = GR_CAPABILITY_LAST_LIGHT
};

/* Light record: Nokia CodeView TI=0x1748, field list TI=0x1747; original
 * size 208 bytes, ARM state base 5712. Original offsets and names:
 * positionXYZ +0, spotCutoff +16, lightMask +20, orgPosition +24,
 * orgSpotDirection +44, spotDirection +60, colorsx +72, spotExponent +108,
 * attenuation +112, cosSpotCutoff +128, premul_colors +132, halfway +168,
 * nVP +180, positional +192, la +196, ld +200, ls +204.
 * UIQ field list TI=0x172B confirms the shared light-field names.
 * spotDirection holds the negated direction (ARM grSetLightSpotDirection,
 * 0x224E8).
 * lightMask bits (grUpdateLight): 16=spotlight (spotCutoff != 180deg),
 * 32=positional with attenuation != (1,1,1), 64=directional. */
typedef struct _grLight
{
    int positional;               /* IFVector4_tov3 return: w != 0 */
    _IVector3 positionXYZ[1];     /* shared-exp position, w divided out */
    int nVP[3];                   /* normalized position (10.21), directional lights */
    int halfway[3];               /* headlight direction (directional lights only) */
    _IFVector4 orgPosition;       /* original float position */
    int lightMask;
    _XColor3 colorsx[3];          /* ambient, diffuse, specular */
    _XColor3 premul_colors[3];    /* colors * material (vertex pipeline) */
    int spotDirection[3];        /* negated spot direction (10.21) */
    _IFVector3 orgSpotDirection;  /* original float spot direction */
    int spotExponent;            /* XScalar */
    _IFVector3 attenuation;       /* constant, linear, quadratic (floats) */
    int cosSpotCutoff;            /* XScalar: max(cos(radians(cutoff)), 0) */
    int spotCutoff;               /* original cutoff (IScalar, degrees) */
    /* per-vertex scratch written by light() (EPOC +196/+200/+204): the
     * attenuation factor, the N*L dot and the specular dot^shininess
     * factor; read back by grLightVertex's color accumulation */
    int la;
    int ld;
    int ls;
} _grLight;

/* client vertex array record (grGetVertexArrayPointer/Binding/Reset +
 * the fetchers). EPOC vertexArray base = +308, stride 20; WINSCW reaches
 * the same records through a byte array. Both builds put `type` at record+5
 * and size at record+4 (EPOC grSetVertexArray, 0x22D14).
 *   stride@0 (int), size@4 (u8 component count), type@5 (u8 GR_DATATYPE),
 *   data@8, binding@12 (ogl buffer object), origPointer@16. */
typedef struct Gerbera_s Gerbera_s;

typedef struct _grVertexArray
{
    int stride;
    unsigned char size;
    unsigned char type;
    unsigned char pad[2];
    void *data;
    unsigned int binding;
    void *origPointer;
} _grVertexArray;

/* the fetch-stage vertex record (cv_anon_1712, 128 bytes; lives in the
 * renderStack vertex buffer at (char*)renderStack + 1332, one record per
 * vertex index, << 7 addressing). Layout fully pinned from the EPOC
 * grTransformVertex + the WINSCW grClipper raw accesses - see the note
 * after the struct definition. */
typedef union _grVertexColor
{                                     /* cv_anon_1703 (fetch view, 16 bytes) */
    unsigned short final[2][4];       /* u16 RGBA rows: [0] lit color,
                                       * [1] secondary/fog row */
    struct
    {                                 /* lighting-path view: XColor3 int color
                                       * + shared alpha slot (final[1][2]);
                                       * rgb[2] only exists in the vertex-cache
                                       * record (per-side colors) */
        _XColor3 rgb[1];
        unsigned short alpha;
        unsigned short pad;
    } input;
} _grVertexColor;

typedef union _grVertexPosition
{                                     /* decompile vertex->position (20B) */
    _IFVector4 objectSpaceFloat[1];   /* float view */
    int fixedView[4];                 /* int view used by the fixed-point
                                       * path: {x,y,z,w} in 16.16, x lands
                                       * in the float view's e slot */
} _grVertexPosition;

typedef struct _grVertex
{
    int index;                                        /* +0   */
    unsigned short stateMask;                         /* +4: bits 0..13 flags
                                                       * (GR_VERTEXMASK_*), 14..15
                                                       * = position state */
    unsigned short clipMask;                          /* +6  */
    _IVector4 clipPosition;                           /* +8  (20B) */
    int screenPosition[3];                            /* +28: viewport-transform
                                                       * output x,y,z (grViewport-
                                                       * TransformVertex) */
    _grVertexColor color;                             /* +40  */
    struct { int x, y; } texCoord[2];                 /* +56  */
    _grVertexPosition position;                       /* +72  */
    int normal[3];                                    /* +92: object-space normal;
                                                       * the clipper's clipFactor
                                                       * [2] aliases normal[0..1]
                                                       * (written after lighting
                                                       * consumed the normal) */
    int pointSize;                                    /* +104 */
    int weights[4];                                   /* +108 */
    unsigned int weightIndices;                       /* +124 */
} _grVertex;                                          /* 128 bytes */

/* EPOC grTransformVertex (epoc.c 22105-22280) pins the record: index@0,
 * stateMask u16@4 (& 0x3FFF | state<<14), clipMask u16@6, clipPosition@8
 * (sub_2EB08 pool+8, 20B), screenPosition@28 (IVector4_viewportTransform
 * writes 3 ints), color@40 (final[0] u16 @40..46, final[1] @48..54),
 * texCoord[2]@56/64 (sub_2E270 record+8*u+56), position@72 (fixed 4x i32
 * LABEL_39 / float sub_32BF0), weights@108 + weightIndices@124 (sub_17890),
 * and the clipper (libGLES_CM.dll.c 30967+) reads clipPosition@+8, writes
 * clip factors @+92/+96 and stateMask short@+4 - all consistent with this
 * layout. grClipper's clipFactor aliases normal[0..1] on purpose. */

/* function-pointer types for the vertex pipeline stage selectors */
typedef void (*grFetchColorFn)(Gerbera_s *state, unsigned char *vertexList);
typedef void (*grFetchNormalFn)(Gerbera_s *state, unsigned char *vertexList);
typedef void (*grFetchTexCoordFn)(Gerbera_s *state, unsigned char *vertexList, int texUnit);
typedef void (*grFetchPointSizeFn)(Gerbera_s *state, unsigned char *vertexList);
typedef void (*grApplyFogFn)(Gerbera_s *state, unsigned char *vertexList);

/* material color slots (CodeView TI=0x15A5) */
enum GR_MATERIALCOLOR_e
{
    GR_MATERIALCOLOR_AMBIENT = 0,
    GR_MATERIALCOLOR_DIFFUSE = 1,
    GR_MATERIALCOLOR_SPECULAR = 2,
    GR_MATERIALCOLOR_EMISSION = 3,
    GR_MATERIALCOLOR_MAX = 4
};

/* vertex array slots (CodeView TI=0x15FC) */
enum GR_VERTEXARRAY_e
{
    GR_VERTEXARRAY_POSITION = 0,
    GR_VERTEXARRAY_NORMAL = 1,
    GR_VERTEXARRAY_COLOR = 2,
    GR_VERTEXARRAY_WEIGHT = 3,
    GR_VERTEXARRAY_MATRIX_INDEX = 4,
    GR_VERTEXARRAY_POINT_SIZE = 5,
    GR_VERTEXARRAY_TEXCOORD0 = 6,
    GR_VERTEXARRAY_TEXCOORD_LAST = 7,
    GR_VERTEXARRAY_MAX = 8
};

/* material state (decompile cv_anon_174D, reconstructed from byte offsets:
 * colors[4] _XColor3 at ints 0..11, XScalar shininess at int 12, alpha
 * unsigned short at byte 52) */
typedef struct _grMaterial
{
    _XColor3 colors[4];
    int shininess;                /* XScalar */
    unsigned short alpha;
} _grMaterial;

/* scissor / viewport rectangles; the pipeline consumes these as scale/offset
 * triplets (the decompile's union member normal[3]) */
typedef struct _grViewportParams
{
    int normal[3];
} _grViewportParams;

/* texture-coordinate transform record (_helpers_core.c fragment
 * ITCTransform_copy). Size 132 bytes is pinned by the EPOC texCoordTransform
 * stride (state+4064+132*unit; the WINSCW [11*unit] indexing = 11 records of
 * 12 bytes per decompiler element): lsh/rsh are bytes and im is int.
 *   e[ci]   = matrix column exponent words (packed 4 bytes, as stored)
 *   lsh/rsh = clamp(eByte-119, 0..31) / clamp(119-eByte, 0..31)
 *   im[ria] = imMulLsl64h(m[3][ria], 0x10000, lsh[3][ria]) >> rsh[3][ria]
 *   cl      = classification: 0/1/2/3 per element (0 zero, 1 general,
 *             2 +0x20000000, 3 -0x20000000 with eByte==119+8) */
typedef struct _ITCTransform
{
    int e[4];
    int m[4][4];
    unsigned char lsh[4][4];
    unsigned char rsh[4][4];
    int im[4];
    int cl;
} _ITCTransform;

/* Texture unit record: Nokia CodeView TI=0x1732, field list TI=0x1731;
 * UIQ TI=0x171B confirms texture, texEnvColor and texEnvMode.
 * The original has two 32-byte records (array TI=0x1733 is 64 bytes),
 * at ARM state+468 and +500. The portable context retains the decompile's
 * textureUnit[2*unit] indexing; its odd records are unused. Field offsets
 * below describe the original 32-bit record. */
typedef struct _grTextureUnitState
{
    _grTexture *texture;                    /* +0: bound texture object */
    unsigned int texEnvColor;               /* +4 */
    unsigned char texEnvMode;               /* +8 (internal 0..5) */
    unsigned char texEnvRGBCombFunc;         /* +9 */
    unsigned char texEnvAlphaCombFunc;       /* +10 */
    unsigned char texEnvRGBCombSrc[3];       /* +11..13 */
    unsigned char texEnvAlphaCombSrc[3];     /* +14..16 */
    unsigned char texEnvRGBCombOperand[3];   /* +17..19 */
    unsigned char texEnvAlphaCombOperand[3]; /* +20..22 */
    unsigned char texEnvRGBCombScale;        /* +23 multiplier (1, 2 or 4) */
    unsigned char texEnvAlphaCombScale;      /* +24 multiplier (1, 2 or 4) */
    unsigned char padding[3];               /* +25..27 */
    int texCoordReplace;                    /* +28 */
} _grTextureUnitState;

/* --- blit image / memory image structs (CodeView TI=0x15D1..0x1659) -------- */
enum GR_BLITIMAGE_e
{
    GR_BLITIMAGE_MEMORY_IMAGE = 0,
    GR_BLITIMAGE_TEXTURE = 1,
    GR_BLITIMAGE_READ_SURFACE = 2
};

typedef struct grMemoryImage_s
{
    int width;
    int height;
    int stride;
    int bytesPerPixel;
    void *data;
    int useDescriptor;
    _grSurfaceDescriptor descriptor;
    int dataType;
    int imageFormat;
} grMemoryImage_s;

typedef struct grTextureImage_s
{
    _grTexture *texture;
    int mipLevel;
} grTextureImage_s;

typedef struct grBlitImage_s
{
    int type;
    grTextureImage_s texture;
    grMemoryImage_s memoryImage;
} grBlitImage_s;

typedef struct grBlitInfo_s
{
    _grRect srcRect;
    _grRect dstRect;
    grBlitImage_s *srcImage;
    grBlitImage_s *dstImage;
} grBlitInfo_s;

struct Gerbera_s
{
    /* --- capabilities --------------------------------------------------- */
    unsigned char capabilities[GR_CAPABILITY_MAX];

    /* --- rectangles ------------------------------------------------------ */
    _grRect scissorRectangle;
    _grRect viewportRectangle;
    _grRect surfaceScissor;
    _grRect renderScissor;
    int scissorDirty;
    int rejectAllState;                 /* cached "reject all" verdict */
    int rejectAllStateDirty;
    int pixelPipelineDirty;
    int vertexPipelineDirty;

    /* --- viewport / depth range ------------------------------------------ */
    _grViewportParams viewportScale;
    _grViewportParams viewportOffset;
    int zNear;
    int zFar;

    /* --- transform stack (EPOC offsets; the arena is contiguous there:
     * matrix[13] +624, the _IVertexTransform records +1664..+4063, the
     * _ITCTransform/_XNormalTransform records +4064..+4727, the user clip
     * planes +4728..+4967 and their matrices +4968..+5447) --------------- */
    int matrixDirtyMask;                                       /* +112  */
    _IMatrix4x4 matrix[GR_MATRIXMODE_MAX];                     /* +624  */
    _IVertexTransform modelviewTransform;                      /* +1664 */
    _IVertexTransform objectToClipTransform;                   /* +1864 */
    _IVertexTransform projectionTransform;                     /* +2064 */
    _IVertexTransform matrixPalTransform[9];                   /* +2264 */
    _ITCTransform texCoordTransform[2];                        /* +4064 */
    _XNormalTransform normalsToEyeTransform;                   /* +4328 */
    _XNormalTransform normalsToEyePal[9];                      /* +4368 */
    _IFVector4 userClipPlanes[GR_MAX_USER_CLIP_PLANES];        /* +4728 */
    _IFVector4 origUserClipPlanes[GR_MAX_USER_CLIP_PLANES];    /* +4848 */
    _IMatrix4x4 userClipMatrix[GR_MAX_USER_CLIP_PLANES];       /* +4968 */
    int enabledUserClipPlaneIndices;                           /* +7620 */

    /* --- draw / read surfaces -------------------------------------------- */
    _grDrawSurface drawSurface;
    _grDrawSurface readSurface;
    unsigned char displayOpen;
    int useDithering;

    _grTextureUnitState textureUnit[2 * GR_MAX_TEXTURE_UNITS];
    unsigned char useTexUnit[GR_MAX_TEXTURE_UNITS]; /* per-unit enable */

    /* --- vertex pipeline (EPOC: vertexArray +308, 8 records; renderStack
     * pool pointer +116, vertex fetch buffer at pool+1332, 128-byte vertex
     * records; pointDistAttn +576, computePointDistAttn +592, transformVE
     * +47, stage pointers +7624..7652) -------------------------------------- */
    void *renderStack;                                /* +116 */
    unsigned char renderStackAllocated;
    unsigned char compilationFailed;                  /* hc compiler verdict;
                                                       * grRenderElements/Arrays
                                                       * no-op when set */
    void *vertexCache;                                /* = renderStack while
                                                       * allocated (grSetup-
                                                       * Rendering) */
    void *pixelPipeline;                              /* pixel-pipe record in
                                                       * the render stack
                                                       * (grSetupRendering) */
    unsigned int packedFogColor;                      /* fog color packed for
                                                       * the pixel pipe */
    int activeGradientMask;                           /* per-attribute gradient
                                                       * enable bits (1 depth,
                                                       * 2 fog, 0x3C colors,
                                                       * 0x40/0x80 tex0 s/t,
                                                       * 0x100/0x200 tex1) */
    int numActiveGradients;                           /* entries in active-
                                                       * GradientIndex (grDraw-
                                                       * ClippedTriangle pre-
                                                       * advances them) */
    int activeGradientIndex[GR_GRADIENT_MAX];
    grPixelPipelineCode_fn pixelPipelineCode;         /* compiled program
                                                       * (hc compiler) */
    _grPipelineCache *pipelineCache;                  /* original embedded
                                                       * cache at +188 */
    _grPipelineCache pipelineCacheStorage;
    void *pipelineCacheExecutable;
    _grStackAllocator allocator;
    _grStackAllocator scratchAllocator;               /* render-stack
                                                       * allocator (grSetup-
                                                       * Rendering allocates
                                                       * the 23860-byte render
                                                       * stack from it) */
    void *scratchMemory;                              /* +600 */
    int scratchMemorySize;                            /* +604 */
    void *setjmpConfig;                               /* original embedded HC
                                                       * setjmp config at +608 */
    struct
    {
        int (*setjmpWrap)(void *jmpbuf, int (*func)(void *), void *args);
        void (*longjmp)(void *jmpbuf, int value);
        unsigned int jmpbufSize;
    } setjmpConfigStorage;
    unsigned char textureUnitEnabled[2];              /* +48/49: per-unit texcoord
                                                       * stage active (set by the
                                                       * ogl texture-enable path) */
    _grVertexArray vertexArray[8];                    /* +308 */
    _IFVector3 pointDistAttn;                         /* +576 */
    unsigned char computePointDistAttn;               /* +592 */
    int lineWidth;                                    /* +140 (16.16) */
    int pointSize;                                    /* +136 (16.16) */
    int pointSizeMin;                                 /* +564 (16.16) */
    int pointSizeMax;                                 /* +568 (16.16) */
    int pointFadeThreshold;                           /* +572 (IScalar;
                                                       * WINSCW +580) */
    unsigned char transformVE;                        /* +47  */
    unsigned char primitiveType;                      /* +22  */
    int defaultNormal[3];                             /* XNormal3 ints */
    _XColor3 defaultColor;                            /* int current color */
    unsigned short defaultColorRGBA[4];               /* u16 current color */
    union
    {                                                 /* rows 0,1 = current
                                                       * s,t,r,q floats (grSet
                                                       * TexCoord defaults);
                                                       * row 2 = transformed
                                                       * outputs {x0,y0,x1,y1}
                                                       * stored as 10.21 ints
                                                       * (the decompile reaches
                                                       * both views through one
                                                       * array) */
        HGfloat f[3][4];
        int x[3][4];
    } defaultTexCoord;
    grFetchColorFn fetchVertexColor;                  /* +7628 */
    grFetchNormalFn fetchVertexNormal;                /* +7632 */
    grFetchTexCoordFn fetchTexCoord[2];               /* +7636 */
    grFetchPointSizeFn fetchPointSize;                /* +7624 */
    grApplyFogFn applyFog;                            /* +7644 */

    /* --- clears ----------------------------------------------------------- */
    unsigned int clearColor;
    int clearDepth;
    unsigned int clearStencil;
    int clearInfoIndex;             /* +7472: queued grClearBuffers records */
    _grClearInfo clearInfos[GR_CLEAR_BUFFER_SIZE];  /* +7476, 36B each */

    /* --- per-pixel fixed-function state ----------------------------------- */
    unsigned int colorMask;
    int depthMask;
    unsigned int stencilWriteMask;
    int alphaFunc;
    int alphaReference;
    int stencilFunc;
    unsigned int stencilReference;
    unsigned int stencilTestMask;
    int stencilFail;
    int stencilZFail;
    int stencilZPass;
    int depthFunc;
    int logicOp;
    int srcBlendFactor;
    int dstBlendFactor;
    int depthOffsetFactor;
    int depthOffsetUnits;

    /* --- fog --------------------------------------------------------------- */
    int fogMode;
    int fogDensity;
    int fogStart;
    int fogEnd;
    int fogOORange;
    unsigned int fogAtZero;
    unsigned int fogColor;

    /* --- gradients ---------------------------------------------------------- */
    int screenRectGradients;

    /* --- lighting bookkeeping ----------------------------------------------- */
    int lightActiveDirtyMask;
    unsigned int lightActiveMask;
    unsigned int lightColorDirtyMask;
    unsigned int lightTypeDirtyMask;
    unsigned char lightDirectionalMask;   /* EPOC byte field at +29 */
    unsigned char normalsToEyePalDirty[9];
    int normalsToEyeDirty;
    _grLight lights[GR_MAX_LIGHTS];
    _grLight *l_directional[GR_MAX_LIGHTS + 1];  /* enabled lights with
                                                  * lightMask bit 64, NUL-ended */
    _grLight *l_positional[GR_MAX_LIGHTS + 1];   /* the rest, NUL-ended */

    /* --- culling -------------------------------------------------------------- */
    int frontFace;
    unsigned int cullMask;
    unsigned char cullNegative;
    unsigned char lightingSideMask;
    unsigned char constWhiteModulate;

    /* --- global ambient -------------------------------------------------------- */
    _XColor3 ambientColor;
    _XColor3 ambientLightSum;       /* ambientColor + sum of directional lights
                                     * (EPOC +5588); maintained by
                                     * grUpdateAmbientLightSum */
    unsigned char ambientLightSumMask; /* directional mask it was built with */
    int ambientLightSumDirty;

    /* --- material ---------------------------------------------------------------- */
    _grMaterial material;
    _XColor3 baseColor;             /* ambientLightSum*material.ambient+emission
                                     * (grUpdateLights) */
    int baseColorDirty;
    void *surfaceTlsState;         /* eglState_s borrowed for the active draw call */
    struct
    {
        unsigned int divisor;     /* zero marks an unused entry */
        unsigned int reciprocal;
    } edgeSlopeCache[GR_EDGE_SLOPE_CACHE_SIZE];
    _IFVertexTransformPlan floatTransformPlan[2];
    unsigned int floatTransformRecent;
    unsigned int floatTransformMisses;
    _ICommonVertexTransform commonTransform;
    unsigned int pixelTextureLayout; /* dimensions and wrap modes of specialized code */
    void (*transformVertexFast)(struct Gerbera_s *state, _grVertex *vertex);
};

void (*grSelectFastVertexTransform(Gerbera_s *state))(Gerbera_s *state, _grVertex *vertex);

/* --- grApiSmall.inl: transform stack / user clip planes -------------------- */
void grSetMatrix(Gerbera_s *state, int mode, _IMatrix4x4 *matrix);

/* --- grUtil.h: matrix dirty helpers ---------------------------------------- */
int  grIsMatrixDirty(Gerbera_s *state, int mode);
void grSetMatrixDirty(Gerbera_s *state, int mode, int status);

/* --- grApiSmall.inl: user clip planes -------------------------------------- */
void grSetUserClipPlane(Gerbera_s *state, int plane, _IFVector4 *equation, _IMatrix4x4 *modelview);
_IFVector4 *grGetUserClipPlane(Gerbera_s *state, int plane);

/* --- _helpers_core.c fragment: texture-coordinate transforms ---------------- */
void ITCTransform_copy(_ITCTransform *pD, _IMatrix4x4 *pM);
void grTexCoordDivByQ(int *output, int *coords);
void ITCTransform_mulxv2(int *pO, _ITCTransform *pT, int *st);
void ITCTransform_mulxv4(int *pO, _ITCTransform *pT, int *strq);
void ITCTransform_mulxv(int *pD, _ITCTransform *pT, int *strq, int n);
void ITCTransform_mulfv2(int *pO, _ITCTransform *pT, int *st);
void ITCTransform_mulfv4(int *pO, _ITCTransform *pT, int *strq);
void ITCTransform_mulfv(int *pO, _ITCTransform *pT, int *strq, int n);

/* --- grVertexPipeline.inl: light update ------------------------------------- */
unsigned int grUpdateLight(Gerbera_s *state, int ndx);
int  grUpdateLights(Gerbera_s *state);
void grUpdateAmbientLightSum(Gerbera_s *state);

/* --- grVertexPipeline.inl: fetch / transform / fog stages -------------------- */
void grFetchVertexColorUINT8(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFLOAT(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFIXED(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorDEFAULT(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFastUINT8(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFastFLOAT(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFastFIXED(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexColorFastDEFAULT(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexNormalINT8(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexNormalINT16(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexNormalFIXED(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexNormalFLOAT(Gerbera_s *state, unsigned char *vertexList);
void grFetchVertexNormalDEFAULT(Gerbera_s *state, unsigned char *vertexList);
void grTransformTexCoordINT8x2(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordINT8x4(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordINT16x2(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordINT16x4(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordFIXEDx4(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordDEFAULT(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grTransformTexCoordFLOATx4(Gerbera_s *state, unsigned char *vertexList, int texUnit);
void grFetchPointSizeFLOAT(Gerbera_s *state, unsigned char *vertexList);
void grFetchPointSizeFIXED(Gerbera_s *state, unsigned char *vertexList);
void grApplyFogFAST(Gerbera_s *state, unsigned char *vertexList);
void grApplyFogLINEAR(Gerbera_s *state, unsigned char *vertexList);
void grApplyFogEXP(Gerbera_s *state, unsigned char *vertexList);
void grApplyFogEXP2(Gerbera_s *state, unsigned char *vertexList);
int  grGetFogZ(Gerbera_s *state, _grVertex *vertex);
int  grGetVertexPositionState(_grVertex *v);
void grSetVertexPositionState(_grVertex *v, int state);
void grUpdateVertexPipeline(Gerbera_s *state);
unsigned int grFetchWeightAndIndex(Gerbera_s *state, int *weights, int vertexIndex);
void grTransformVertex(Gerbera_s *state, _grVertex *vertex);
void grTransformVertexPreloaded(Gerbera_s *state, _grVertex *vertex, int weightsPreloaded);
unsigned int grComputeVertexClipCodes(Gerbera_s *state, _grVertex *vertex);
int  grIsOutsideUserClipPlane(_IFVector4 *plane, _IVector4 *clipPos);

/* --- clipper (grClipper.inl, implemented in grClipper.c) -------------------
 * _grClipVertex is the clip polygon entry (cv_anon_17F5): barycentric clip
 * factors a/b (IUnitScalar, a+b <= ONE, the third weight = ONE-a-b), the
 * source vertex record (NULL for intersections) and the fan edge flag
 * (the source vertex's cache index). The clip factors alias the vertex
 * record's normal[0..1] while the polygon lives (decompile +92/+96). */
typedef struct _grClipVertex
{
    int clipFactor[2];
    _grVertex *originalVertex;
    unsigned char edgeFlag;
} _grClipVertex;

int  grIsInsideClipPlane(int distance);
int  grIsOutsideClipPlane(int distance);
int  grIsOnPlane(int distance);
int  grSubpixelTrunc(int i);
int  grGetFrontFace(Gerbera_s *state);
void grClipFactorSet(_grClipVertex *v, int ndx, int val);
void grClipFactorInit(_grClipVertex *v, int ndx);
void grLerpClipFactor(_grClipVertex *dst, _grClipVertex *s0, _grClipVertex *s1, int factor);
int  grVertexCacheGetTemp(Gerbera_s *state, int index);
int  grClipInterpolate(int d0, int d1);
int  grComputeDistanceToGuardbandPlane(int plane, _IVector4 *position, int shift);
int  grComputeDistanceToUserClipPlane(Gerbera_s *state, int plane, _IVector4 *position, int planeSign);
int  grClipConvexPolygon(_grClipVertex *clipped, int numVertices, _grClipVertex *inPoly,
                         int *distances, int swapDirection);
int  grClipper(Gerbera_s *state, unsigned char *outputFan, unsigned int indices);

/* --- grFast vertex cache (grApiFast.inl, implemented in grApiFast.c) ------- */
void grSetupRendering(Gerbera_s *state);
void grCleanupRendering(Gerbera_s *state);
void grSetupTextureRectGradients(Gerbera_s *state, _grTRGradSetupInfo *info);
void grRenderTextureRect(Gerbera_s *state, int x0, int y0, int w, int h, int z);
/* hc module (grInterpreter.inl) */
int grExecuteProgram(HCInstr_s *result, _grSignature *signature, int resultLength);
void grrBeginRendering(Gerbera_s *state);
void grrEndRendering(Gerbera_s *state);
void grrUpdateState(Gerbera_s *state);
void grVertexCacheInvalidate(Gerbera_s *state);
int  grVertexCacheGet(Gerbera_s *state, int vtxIndex);
void grViewportTransformVertex(Gerbera_s *state, _grVertex *vertex);
HGint64 grComputeTriangleArea(_grVertex *vertexCache, unsigned int indices);
int  grRoundTriangleArea(HGint64 area);
void grQueuePrimitive(Gerbera_s *state, int i0, int i1, int i2, int numVertices);
void grQueuePrimitive1(Gerbera_s *state, int i0);
void grQueuePrimitive2(Gerbera_s *state, int i0, int i1);
void grQueuePrimitive3(Gerbera_s *state, int i0, int i1, int i2);
void grFlushPrimitiveBatch(Gerbera_s *state);
void grClipPrimitive(Gerbera_s *state, unsigned int origIndices, int numVertices);
/* rasterizer / vertex-gradient entry points reached by grFlushPrimitiveBatch
 * and grClipPrimitive */
void grUpdateVertexPipeline(Gerbera_s *state);
void grInterpolateVertexGradients(Gerbera_s *state, _grVertex *vertex,
                                  unsigned int origIndices, int side);
void grrDrawClippedPoint(Gerbera_s *state, _grClipTriangle *tri, unsigned int origIndices);
void grrDrawClippedLine(Gerbera_s *state, _grClipTriangle *tri, unsigned int origIndices);
void grrDrawClippedTriangle(Gerbera_s *state, _grClipTriangle *tri, unsigned int origIndices);
void grDrawClippedLine(Gerbera_s *state, _grClipTriangle *tri,
                       unsigned int origIndices);
void grDrawClippedPoint(Gerbera_s *state, _grClipTriangle *tri,
                        unsigned int origIndices);

/* --- grTessellator.inl: perspective subdivision ------------------------------- */
int grFracDiv(int a, int b);
int grLerp(int a, int b, int factor);
int grClassifyTriangle(Gerbera_s *state, _grVertex *vertexCache,
                       unsigned int indices);
int grSplitEdge(_grVertex *dst, _grVertex *v0, _grVertex *v1, int forceSplit);
void grTriangleTessellator(Gerbera_s *state, _grClipTriangle *tri,
                           unsigned int origIndices);

/* --- grPipelineCache.inl: compiled-program cache ------------------------------ */
int grCacheMatchSignatures(_grCacheSignature *a, _grCacheSignature *b);
_grCacheEntry *grCacheGetOldestUsed(_grPipelineCache *cache);
void grCacheChangeList(_grCacheEntry *entry, _grCacheEntry **newList,
                       _grCacheEntry **oldList);
void grCacheMergeFreeBlock(_grPipelineCache *cache, _grCacheEntry *entry);
void grCacheRemove(_grPipelineCache *cache, _grCacheEntry *entry);
void grCacheDefragment(_grPipelineCache *cache);
void grCacheInit(_grPipelineCache *cache, void *arena,
                 _grCacheEntry *entries, int arenaSize, int entryCount);
void grCacheSetArena(_grPipelineCache *cache, void *arena, int arenaSize);
void *grCacheFind(_grPipelineCache *cache, _grCacheSignature *signature,
                  unsigned int *gradientMask);
void *grCacheInsert(_grPipelineCache *cache, _grCacheSignature *signature,
                    unsigned int gradientMask, void *data, int dataSize);
int grCacheGetFreeMemory(_grPipelineCache *cache);
void grCacheSelfCheck(_grPipelineCache *cache);
_grCacheEntry *grCacheFindFreeEntry(_grPipelineCache *cache, int dataSize);

/* --- grPixelPipeline.inl: signature machinery --------------------------------- */
int grIsGradientUsed(unsigned char *paramMask, int g);
void grCompressSignature(_grCacheSignature *compressed,
                         _grSignature *uncompressed);
void grEvaluatePixelPipeline(Gerbera_s *state, _grSignature *signature);
void grEnsureValidPixelPipeline(Gerbera_s *state);
unsigned int grGetPixelTextureLayout(const Gerbera_s *state);
void grCheckPixelPipeConsistency(Gerbera_s *state);
void grCheckPixelPipeStack(Gerbera_s *state);
void grFetchPrimitives(Gerbera_s *state, int fetchType, int numVertices,
                       int vertexCount, void *srcIndices, int baseIndex);
void grRenderElements(Gerbera_s *state, int primitiveType, int vertexCount,
                      int dataType, void *indices);
void grRenderArrays(Gerbera_s *state, int primitiveType, int vertexCount, int baseIndex);

/* --- grRasterizer.inl: gradient setup ---------------------------------------- */
void grSetupGradient(_grGradientInfo *info, int gradient, int g01, int g02,
                     int reference);
void grComputeDeltas(_grGradientInfo *info, int gradient, int g01, int g02);
int grSetupTriangleGradientsVisible(Gerbera_s *state, _grClipTriangle *tri,
                                    int flatShadeIndex, unsigned int gradientMask,
                                    int rejectDepth);
void grSetupTriangleGradients(Gerbera_s *state, _grClipTriangle *tri,
                              int flatShadeIndex, unsigned int gradientMask);
void grSetupTextureUnit(Gerbera_s *state, int texIndex, int (*pairs)[2],
                        int v0sx, int v0sy);

/* --- grSmall.c: gradient helpers --------------------------------------------- */
int grComputeGradientReference(int v0sx, int v0sy, int xAdd, int yAdd);
void grComputeDepthOffset(Gerbera_s *state);

/* --- grUtil.h: polygon offset ------------------------------------------------ */
int grIsDepthOffsetEnabled(Gerbera_s *state, int primitiveType);

/* --- grScanConvert.inl: scan conversion -------------------------------------- */
int grDivsu(int a, int b);
int grDivideEdgeSlope(Gerbera_s *state, int numerator, int denominator);
void grScanConvertWithXScissoring(_grEdgePair *edgePairs,
                                  unsigned int *scanlineInfo,
                                  _grRect *scissor, int topHeight,
                                  int btmHeight);
void grScanConvertWithoutXScissoring(_grEdgePair *edgePairs,
                                     unsigned int *scanlineInfo,
                                     int topHeight, int btmHeight);
int grScanConvertTriangle(Gerbera_s *state, _grRasterPoint *p0,
                          _grRasterPoint *p1, _grRasterPoint *p2,
                          int emptyBits);

/* --- grSmall.c / grPixelPipeline.h: triangle fill ---------------------------- */
void grDrawClippedTriangle(Gerbera_s *state, _grClipTriangle *tri,
                           unsigned int origIndices, int emptyBits);
int grDepthSpansOccluded(const _grPixelPipe *pipe, int strictLess);
void grExecutePixelPipeline(Gerbera_s *state);
void grICacheFlush(void *start, unsigned int numBytes);
int grIsStencilMaskFull(Gerbera_s *state, unsigned int stencilMask);
int grMaskedDescriptorsEqual(unsigned int mask, _grSurfaceDescriptor *a,
                             _grSurfaceDescriptor *b);

void light(_grLight **lights, int shininess, _IVector3 *pos, int *eyeNormal, int nSides, int bDirectional);
void grLightVertex(Gerbera_s *state, _grVertex *v);
void grApplyLighting(Gerbera_s *state, unsigned char *vertexList);

/* --- grUtil.h: primitive helper --------------------------------------------- */
int  grGetPrimitiveVertices(int primitiveType);

/* --- grApiSmall.inl: vertex array record access ------------------------------ */
void *grGetVertexArrayPointer(Gerbera_s *state, int arrayType);
unsigned int grGetVertexArrayBinding(Gerbera_s *state, int arrayType);
void grResetVertexArray(Gerbera_s *state, int arrayType);

/* --- grSmall.c prefix getters (grSmall.c 34729-34780) ---------------------- */
int grGetAlphaFunc(Gerbera_s *state);
int grGetSrcBlendFactor(Gerbera_s *state);
int grGetDstBlendFactor(Gerbera_s *state);
int grGetStencilFunc(Gerbera_s *state);
int grGetStencilFail(Gerbera_s *state);
int grGetStencilZFail(Gerbera_s *state);
int grGetStencilZPass(Gerbera_s *state);
int grGetLogicOp(Gerbera_s *state);
int grGetRectGradients(Gerbera_s *state);

/* --- capabilities (grUtil.h) --------------------------------------------- */
int  grTestCapability(Gerbera_s *state, int cap);
void grEnableCapability(Gerbera_s *state, int cap);
void grDisableCapability(Gerbera_s *state, int cap);
int  grIsDepthOffsetEnabled(Gerbera_s *state, int primitiveType);

/* --- grApiSmall.inl: cull / scissor / viewport / caps --------------------- */
void grUpdateCullMode(Gerbera_s *state);
void grSetDepthRange(Gerbera_s *state, int n, int f);
void grUpdateScissor(Gerbera_s *state);
void grSetScissor(Gerbera_s *state, int x0, int x1, int y0, int y1);
void grSetViewport(Gerbera_s *state, int x0, int x1, int y0, int y1);
int  grCapChangesRejectAllState(int cap);
int  grCapChangesPixelPipeline(int cap);
void grSetCapability(Gerbera_s *state, int cap, int status);
int  grGetCapability(Gerbera_s *state, int cap);
void grUpdateDither(Gerbera_s *state);

/* --- grApiSmall.inl: clears ----------------------------------------------- */
void grSetClearColor(Gerbera_s *state, unsigned int color);
void grSetClearDepth(Gerbera_s *state, int depth);
void grSetClearStencil(Gerbera_s *state, unsigned int s);

/* --- grApiSmall.inl: per-pixel state -------------------------------------- */
void grSetCullMode(Gerbera_s *state, int frontFace, unsigned int cullMask);
void grSetFogMode(Gerbera_s *state, int fogMode);
void grSetFogDensity(Gerbera_s *state, int density);
void grSetFogRange(Gerbera_s *state, int fogStart, int fogEnd);
void grSetFogColor(Gerbera_s *state, unsigned int fogColor);
void grSetRectGradients(Gerbera_s *state, int gradients);
void grSetMaterialColor(Gerbera_s *state, int component, _XColor3 *color);
void grSetMaterialAlpha(Gerbera_s *state, unsigned short alpha);
void grSetMaterialShininess(Gerbera_s *state, int shininess);
int  grGetMaterialShininess(Gerbera_s *state);
void grSetLightPosition(Gerbera_s *state, int ndx, _IFVector4 *position);
_IFVector4 *grGetLightOrgPosition(Gerbera_s *state, int ndx);
void grSetLightSpotDirection(Gerbera_s *state, int ndx, int *direction, _IFVector3 *orgDirection);
_IFVector3 *grGetLightOrgDirection(Gerbera_s *state, int ndx);
void grSetLightColor(Gerbera_s *state, int ndx, int component, _XColor3 *color);
void grGetLightColor(Gerbera_s *state, int ndx, int component, _XColor3 *color);
void grSetLightAttenuation(Gerbera_s *state, int ndx, int component, int value);
int  grGetLightAttenuation(Gerbera_s *state, int ndx, int component);
void grSetLightExponent(Gerbera_s *state, int ndx, int exponent);
int  grGetLightExponent(Gerbera_s *state, int ndx);
void grSetLightSpotCutoff(Gerbera_s *state, int ndx, int cutoff);
int  grGetLightSpotCutoff(Gerbera_s *state, int ndx);
unsigned int grTestLightColor(_grLight *light, int component);
unsigned int grPrecalcLightColor(_grLight *light, int component, _XColor3 *materialColor);
void grSetGlobalAmbient(Gerbera_s *state, _XColor3 *ambient);
void grSetAlphaFunc(Gerbera_s *state, int func, int ref);
void grSetStencilFunc(Gerbera_s *state, int func, unsigned int ref, unsigned int mask);
void grGetDrawBufferDescriptor(Gerbera_s *state, _grSurfaceDescriptor *desc);
void grSetDepthOffset(Gerbera_s *state, int factor, int units);
void grSetStencilOp(Gerbera_s *state, int sFail, int dFail, int dPass);
void grSetDepthFunc(Gerbera_s *state, int func);
void grSetLogicOp(Gerbera_s *state, int logicOp);
void grSetBlendFunc(Gerbera_s *state, int srcFactor, int dstFactor);
void grSetColorMask(Gerbera_s *state, unsigned int mask);
void grSetDepthMask(Gerbera_s *state, int mask);
void grSetStencilMask(Gerbera_s *state, unsigned int mask);

/* --- grQuery (TI=0x1558) --------------------------------------------------- */
enum GR_QUERY_e
{
    GR_QUERY_FRONT_FACE    = 0,
    GR_QUERY_CULL_MASK     = 1,
    GR_QUERY_STENCIL_REF   = 2,
    GR_QUERY_STENCIL_CLEAR = 3,
    GR_QUERY_FOG_START     = 4,
    GR_QUERY_FOG_END       = 5,
    GR_QUERY_MAX           = 6
};

int  grGetInteger(Gerbera_s *state, int value);
int  grGetIScalar(Gerbera_s *state, int value);

/* --- grSmall.c / grApiSmall.inl: lifecycle, surfaces, texenv, vertex/point --- */
int        grGetMinWorkMemorySize(void);
int        grGetMinSharedMemorySize(void);
Gerbera_s *grCreateGerbera(int workMemorySize, unsigned char *workMemory,
                           unsigned char *sharedWorkMem, int sharedWorkMemSize);
void       grDestroyGerbera(Gerbera_s *state);
int        grIsValidSurfaceInfo(_grDrawSurface *info);
void       grSetSurfaces(Gerbera_s *state, _grDrawSurface *drawInfo, _grDrawSurface *readInfo);

void         grSetActiveTexture(Gerbera_s *state, int ndx, _grTexture *texture);
void         grSetTexEnvColor(Gerbera_s *state, int texUnit, unsigned int color);
unsigned int grGetTexEnvColor(Gerbera_s *state, int texUnit);
void         grSetTexEnvMode(Gerbera_s *state, int texIndex, int texEnvMode);
int          grGetTexEnvMode(Gerbera_s *state, int texUnit);
void         grSetTexEnvRGBCombFunc(Gerbera_s *state, int texIndex, int func);
int          grGetTexEnvRGBCombFunc(Gerbera_s *state, int texIndex);
void         grSetTexEnvAlphaCombFunc(Gerbera_s *state, int texIndex, int func);
int          grGetTexEnvAlphaCombFunc(Gerbera_s *state, int texIndex);
void         grSetTexEnvRGBCombSrc(Gerbera_s *state, int texIndex, int srcIndex, int src);
int          grGetTexEnvRGBCombSrc(Gerbera_s *state, int texIndex, int srcIndex);
void         grSetTexEnvAlphaCombSrc(Gerbera_s *state, int texIndex, int srcIndex, int src);
int          grGetTexEnvAlphaCombSrc(Gerbera_s *state, int texIndex, int srcIndex);
void         grSetTexEnvRGBCombOperand(Gerbera_s *state, int texIndex, int opIndex, int op);
int          grGetTexEnvRGBCombOperand(Gerbera_s *state, int texIndex, int opIndex);
void         grSetTexEnvAlphaCombOperand(Gerbera_s *state, int texIndex, int opIndex, int op);
int          grGetTexEnvAlphaCombOperand(Gerbera_s *state, int texIndex, int opIndex);
void         grSetTexEnvRGBCombScale(Gerbera_s *state, int texIndex, int scale);
int          grGetTexEnvRGBCombScale(Gerbera_s *state, int texIndex);
void         grSetTexEnvAlphaCombScale(Gerbera_s *state, int texIndex, int scale);
int          grGetTexEnvAlphaCombScale(Gerbera_s *state, int texIndex);
void         grSetTexCoordReplace(Gerbera_s *state, int texUnit, int replace);
int          grGetTexCoordReplace(Gerbera_s *state, int texUnit);

void grSetVertexArray(Gerbera_s *state, int arrayType, int dataType, int numComponents,
                      int stride, void *pointer, unsigned int binding, void *origPointer);
void grSetPointSize(Gerbera_s *state, int size);
void grSetLineWidth(Gerbera_s *state, int width);
void grSetPointSizeMin(Gerbera_s *state, int min);
void grSetPointSizeMax(Gerbera_s *state, int max);
void grSetPointFadeThreshold(Gerbera_s *state, int size);
void grSetPointDistAttn(Gerbera_s *state, int *attn);
void grSetVertexColor(Gerbera_s *state, _XColor3 *color, unsigned short alpha);
void grSetVertexNormal(Gerbera_s *state, int *normal);
void grSetVertexTexCoord(Gerbera_s *state, int ndx, int *texCoord);

/* --- grrBlit.inl: color read/write, box-filter mipmap & surface blits ------- */
unsigned int grReadDataTypeColor(void *src, int dataType, int imageFormat);
void         grWriteDataTypeColor(void *dst, int dataType, int imageFormat, unsigned int color);
void         grrConvertBlitImage(Gerbera_s *state, grBlitImage_s *image);
void        *grrComputeImageAddress(grMemoryImage_s *image, int x, int y);
void         grrCopyColorSpan(grMemoryImage_s *dstImage, grMemoryImage_s *srcImage,
                              int xDst, int yDst, int xSrc, int ySrc, int length, void *workMemory);
void         grrBoxFilterLoop32(unsigned int *src, unsigned int *dst, int srcWidth, int srcHeight,
                                int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                                unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep);
void         grrBoxFilterLoop16(unsigned short *src, unsigned short *dst, int srcWidth, int srcHeight,
                                int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                                unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep);
void         grrBoxFilterLoop8(unsigned char *src, unsigned char *dst, int srcWidth, int srcHeight,
                               int dstWidth, int dstHeight, unsigned int *mask, unsigned int *rounding,
                               unsigned int *preShift, unsigned int *postShift, unsigned int xStep, unsigned int yStep);
void         grrDownsample(_grSurfaceDescriptor *desc, int srcWidth, int srcHeight,
                           int dstWidth, int dstHeight, void *src, void *dst);
void         grrBlitMipmap(Gerbera_s *state, grBlitInfo_s *blitInfo);
void         grrBlit(Gerbera_s *state, grBlitInfo_s *blitInfo);

/* --- grClearBuffers (grApiSmall.inl 388): queue a clear record ------------- */
void grClearBuffers(Gerbera_s *state, unsigned int bufferMask);

/* --- egl allocator & executable hooks (eglSymbian.inl / eglBase.inl) -------- */
typedef struct grExecutable_s grExecutable_s;
void           *eglAlloc(unsigned int size);
void            eglFree(void *ptr);
grExecutable_s *eglAllocExecutable(unsigned int size);
void            eglFreeExecutable(grExecutable_s *executable);
void           *eglGetExecutableData(grExecutable_s *executable);
int             eglGetExecutableSize(grExecutable_s *executable);
int             eglGetJmpbufSize(void);
int             eglHCWrapSetjmp(void *jmpbuf, int (*func)(void *), void *args);
void            eglHCWrapLongjmp(void *jmpbuf, int val);

/* --- grr layer: clear queue and EGL surface lock/release ------------------ */
enum GR_SURFACEOP_e
{
    GR_SURFACE_DRAW = 0,
    GR_SURFACE_READ = 1
};

int  grLockSurface(int op);         /* returns 1 when the heap was locked */
int  grReleaseSurface(int op);
int  grLockSurfaceState(void *tlsState, int op);
int  grReleaseSurfaceState(void *tlsState, int op);
/* hc-module entry points */
void *grGetBackend(void);
int  HC_extraStackSpace(void *backend);
int  grScratchGrowMemory(Gerbera_s *state);

int  grSetupCulling(Gerbera_s *state, int primitiveType);
void grrClearColorBuffer(_grDrawSurface *surface, _grClearInfo *info);
void grrClearDitheredColorBuffer(_grDrawSurface *surface, _grClearInfo *info, unsigned int fillMask);
void grrClearDepthBuffer(_grDrawSurface *surface, _grClearInfo *info);
void grrClearStencilBuffer(_grDrawSurface *surface, _grClearInfo *info);
void grrClearBuffers(_grDrawSurface *surface, _grClearInfo *info);
void grFlushBufferClears(Gerbera_s *state);

#if defined(_WIN32) && defined(GERBERA_DLL_EXPORTS)
#define GR_PUBLIC_API __declspec(dllexport)
#else
#define GR_PUBLIC_API
#endif
GR_PUBLIC_API int  grGetStatistic(int stat);
GR_PUBLIC_API void grResetStatistics(void);

HG_EXTERN_C_END

#endif /* GR_STATE_H */
