/* eglInternal.h - internal Gerbera EGL 1.1 data structures and prototypes
 * (reconstructed from CodeView NB11 debug types TI=0x105B..0x1398 in
 * docs/libGLES_CM.dll).
 */
#ifndef GERBERA_EGL_INTERNAL_H
#define GERBERA_EGL_INTERNAL_H

#include <GLES/egl.h>
#include "gerbera/api_opengl/oglContext.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HG_ASSERT_ENABLED
#define EGL_ASSERT_AT(file, line, expr, cond)                     \
    do                                                            \
    {                                                             \
        if (!(cond) && grAssert((expr), (file), (line)) != 0)     \
            grDie(0);                                             \
    } while (0)
#else
#define EGL_ASSERT_AT(file, line, expr, cond) ((void)0)
#endif

#define EGL_NUM_COLOR_FORMATS    4
#define EGL_NUM_DEPTH_FORMATS    3
#define EGL_NUM_STENCIL_FORMATS  2
#define EGL_NUM_SURFACE_TYPES    3
#define EGL_NUM_BUFFER_FORMATS  72
#define EGL_MAX_PBUFFER_DIM   2048
#define EGL_MAX_PBUFFER_PIX   0x400000
#define EGL_POISON_PTR        ((void *)(uintptr_t)0xABABABABu)

/* TI=0x137F */
typedef enum eglSurfaceType
{
    EGL_SURFACE_WINDOW  = 0,
    EGL_SURFACE_PBUFFER = 1,
    EGL_SURFACE_PIXMAP  = 2,
    EGL_SURFACE_MAX     = 3
} eglSurfaceType;

/* TI=0x105B (12 bytes) */
typedef struct ColorFormat
{
    int            numBytes;
    unsigned char  redBits;
    unsigned char  greenBits;
    unsigned char  blueBits;
    unsigned char  alphaBits;
    unsigned char  bindToRgb;
    unsigned char  bindToRgba;
    unsigned short padding;
} ColorFormat;

/* TI=0x135C (16 bytes) */
typedef struct ConfigIndices
{
    int colorIndex;
    int depthIndex;
    int stencilIndex;
    int surfaceTypeIndex;
} ConfigIndices;

/* TI=0x1065 (96 bytes = 24 ints on both 32-bit and 64-bit) */
typedef struct eglConfig
{
    int colorBits;             /* +0  (0)  EGL_BUFFER_SIZE */
    int alphaBits;             /* +4  (1)  EGL_ALPHA_SIZE */
    int blueBits;              /* +8  (2)  EGL_BLUE_SIZE */
    int greenBits;             /* +12 (3)  EGL_GREEN_SIZE */
    int redBits;               /* +16 (4)  EGL_RED_SIZE */
    int configCaveat;          /* +20 (5)  EGL_CONFIG_CAVEAT */
    int configID;              /* +24 (6)  EGL_CONFIG_ID */
    int depthBits;             /* +28 (7)  EGL_DEPTH_SIZE */
    int level;                 /* +32 (8)  EGL_LEVEL */
    int nativeRenderable;      /* +36 (9)  EGL_NATIVE_RENDERABLE */
    int nativeVisualType;      /* +40 (10) EGL_NATIVE_VISUAL_TYPE */
    int nativeVisualID;        /* +44 (11) EGL_NATIVE_VISUAL_ID */
    int numSampleBuffers;      /* +48 (12) EGL_SAMPLE_BUFFERS */
    int numSamples;            /* +52 (13) EGL_SAMPLES */
    int stencilBits;           /* +56 (14) EGL_STENCIL_SIZE */
    int surfaceType;           /* +60 (15) EGL_SURFACE_TYPE */
    int transparentType;       /* +64 (16) EGL_TRANSPARENT_TYPE */
    int transparentRedValue;   /* +68 (17) EGL_TRANSPARENT_RED_VALUE */
    int transparentGreenValue; /* +72 (18) EGL_TRANSPARENT_GREEN_VALUE */
    int transparentBlueValue;  /* +76 (19) EGL_TRANSPARENT_BLUE_VALUE */
    int bindToTextureRgb;      /* +80 (20) EGL_BIND_TO_TEXTURE_RGB */
    int bindToTextureRgba;     /* +84 (21) EGL_BIND_TO_TEXTURE_RGBA */
    int minSwapInterval;       /* +88 (22) EGL_MIN_SWAP_INTERVAL */
    int maxSwapInterval;       /* +92 (23) EGL_MAX_SWAP_INTERVAL */
} eglConfig;

/* TI=0x12C0 */
typedef struct TEGLCachedConfig
{
    const eglConfig *iFilter;
    eglConfig        iConfig;
} TEGLCachedConfig;

/* TI=0x136D (4 bytes) */
typedef struct eglRefCount
{
    short       referenceCount;
    signed char destroyed;
    signed char padding;
} eglRefCount;

typedef struct eglDisplay_s eglDisplay_s;
typedef struct eglSurface_s eglSurface_s;
typedef struct eglContext_s eglContext_s;

/* TI=0x1379 */
struct eglContext_s
{
    eglRefCount   refCount;
    eglDisplay_s *display;
    eglContext_s *prev;
    eglContext_s *next;
    oglContext_s *esContext;
    int           configID;
    int           id;
    int           hasBeenBound;
};

/* TI=0x137C */
struct eglDisplay_s
{
    eglRefCount   refCount;
    eglSurface_s *surfaceList;
    eglContext_s *contextList;
    int           surfaceCount;
    int           contextCount;
};

/* TI=0x1381 */
struct eglSurface_s
{
    eglRefCount    refCount;
    eglSurface_s  *prev;
    eglSurface_s  *next;
    eglDisplay_s  *display;
    void          *colorBuffer;
    void          *depthBuffer;
    void          *stencilBuffer;
    int            configID;
    int            colorBufferStride;
    int            width;
    int            height;
    int            id;
    eglSurfaceType type;
};

/* TI=0x1383 */
typedef struct eglPbufferSurface
{
    eglSurface_s  surface;
    int           isLargestPbuffer;
    int           mipmapTexture;
    int           textureFormat;
    int           textureTarget;
    int           mipmapLevel;
    oglContext_s *boundESContext;
    unsigned int  textureName;
} eglPbufferSurface;

/* TI=0x138A */
typedef struct eglPixmapSurface
{
    eglSurface_s      surface;
    eglNativePixmap_s *nativePixmap;
} eglPixmapSurface;

/* TI=0x1391 */
typedef struct eglWindowSurface
{
    eglSurface_s       surface;
    eglNativeWindow_s *nativeWindow;
    int                ownWindow;
    eglNativePixmap_s *backBufferBitmap;
    void              *windowGc;
    int                sizeValidated;
} eglWindowSurface;

/* --- Shared config tables (eglUtil.c) -------------------------------------- */
extern const ColorFormat g_colorFormats[EGL_NUM_COLOR_FORMATS];
extern const char        g_depthFormats[EGL_NUM_DEPTH_FORMATS];
extern const char        g_stencilFormats[EGL_NUM_STENCIL_FORMATS];
extern const char        g_surfaceTypes[EGL_NUM_SURFACE_TYPES];
extern const eglConfig   s_defaultConfig;
extern const eglConfig   s_defaultFilter;

/* --- eglUtil.inl ----------------------------------------------------------- */
void eglInitRefCount(eglRefCount *refCount);
void eglAddRef(eglRefCount *refCount);
int  eglDestroyRef(eglRefCount *refCount);
int  eglReleaseRef(eglRefCount *refCount);
void eglDisplayAddSurface(eglDisplay_s *display, eglSurface_s *surface);
void eglDisplayRemoveSurface(eglDisplay_s *display, eglSurface_s *surface);
void eglDisplayRemoveContext(eglDisplay_s *display, eglContext_s *context);
void eglFreeSurface(eglSurface_s *surface);
void eglGetConfigIndices(ConfigIndices *indices, int configID);
void eglFillConfig(eglConfig *config, int configID);
int  eglCompareConfigs(const eglConfig *config1, const eglConfig *config2, const eglConfig *filter);
int  eglFilterConfig(const eglConfig *config, const eglConfig *filter);
int  eglUpdateSurfaceBuffers(eglSurface_s *surface);
void eglSetSurfaceDescriptor(oglSurfaceDescriptor *desc, const eglSurface_s *surface, const eglConfig *config);
int  eglUpdateSurfaces(oglContext_s *esContext, eglSurface_s *drawSurface, eglSurface_s *readSurface);
void eglFreeContext(eglContext_s *context);
void eglBlit(void *dstBuffer, int dstStride, const void *srcBuffer, int srcStride,
             int width, int height, int srcBytesPerPixel, int dstBytesPerPixel);

/* --- eglBase.inl ----------------------------------------------------------- */
int           eglIsValidState(eglState_s *state);
eglDisplay_s *eglMapDisplay(EGLDisplay dpy);
eglSurface_s *eglMapSurface(eglDisplay_s *display, EGLSurface id);
eglContext_s *eglMapContext(eglDisplay_s *display, EGLContext id);
int           eglDisplayOwnsSurface(eglDisplay_s *display, eglSurface_s *surface);
int           eglDisplayOwnsContext(eglDisplay_s *display, eglContext_s *context);
void          eglDisplayAddContext(eglDisplay_s *display, eglContext_s *context);
void          eglSurfaceReleaseRef(eglSurface_s *surface);
void          eglContextReleaseRef(eglContext_s *context);
int           eglGetConfigAttributeOffset(int attribute);
int           eglIsValidConfigID(int configID);
int           eglIsEmptyList(const EGLint *attribList);
int           eglIsValidDevice(void);
int           eglCheckSurface(eglSurface_s *surface);

/* --- eglSymbian.inl (native and hosted platform layers) -------------------- */
int                egliNextPowerOfTwo(int v);
int                egliGetTotalConfigs(void);
eglNativePixmap_s *eglGetActiveBitmap(int op);
int                egliCheckSurfaceSize(eglSurface_s *surface);
int                egliUpdateSurfaceRenderTarget(eglSurface_s *surface);
void               egliSetGlobal(void *data);
void              *egliGetGlobal(void);
eglState_s        *egliInitState(void);
void               egliDestroyState(void);
int                egliInitWindowSurface(eglWindowSurface *winSurface);
int                egliConfigPixmapMatch(int configID, eglNativePixmap_s *pixmap);
int                egliInitPixmapSurface(eglPixmapSurface *pixSurface);
int                egliIsValidNativeWindow(eglNativeWindow_s *window);
int                egliIsValidNativePixmap(eglNativePixmap_s *pixmap);
void               egliGetNativeWindowSize(eglWindowSurface *winSurface, int *width, int *height);
void               egliGetNativePixmapSize(eglNativePixmap_s *pixmap, int *width, int *height);
void               egliSwapWindowSurface(eglWindowSurface *winSurface);
int                egliBlitToPixmap(eglSurface_s *surface, eglNativePixmap_s *target);
void               egliWindowSurfaceDestroy(eglWindowSurface *winSurface);
void               egliPixmapSurfaceDestroy(eglPixmapSurface *pixSurface);
int                egliSurfaceResized(eglSurface_s *surface);
void               egliInvalidateWindowSurface(eglSurface_s *surface);
void               egliFixConfig(eglConfig *config);
void               egliFixPixelOffsets(oglSurfaceDescriptor *desc, const eglSurface_s *surface);
int                egliCompareConfigs(const TEGLCachedConfig *config1, const TEGLCachedConfig *config2);
void               egliSortConfigs(EGLConfig *configs, int configCount, const eglConfig *filter);
void               egliSetDefaultDisplayMode(int displayMode);

#ifdef __cplusplus
}
#endif

#endif /* GERBERA_EGL_INTERNAL_H */
