/* oglContext.h - OpenGL ES 1.1 state, object managers, getter tables, and
 * internal API declarations (reconstructed from CodeView NB11 sstGlobalTypes
 * TI=0x145B..0x1652 in docs/libGLES_CM.dll).
 */
#ifndef OGL_CONTEXT_H
#define OGL_CONTEXT_H

#include "hg/hgDefs.h"
#include "hg/hgInt32.h"
#include "hg/hgMemory.h"
#include "imath/imApi.h"
#include "gerbera/grState.h"
#include "GLES/gl.h"

HG_EXTERN_C_BEGIN

/* --- OGL enums (CodeView TI=0x1461, 0x146F, 0x1480, 0x1482, 0x14C2, etc.) -- */

typedef enum OGL_ERROR_e
{
    OGL_ERROR_OK                = 0,
    OGL_ERROR_INVALID_ENUM      = 1,
    OGL_ERROR_INVALID_VALUE     = 2,
    OGL_ERROR_INVALID_OPERATION = 3,
    OGL_ERROR_STACK_OVERFLOW    = 4,
    OGL_ERROR_STACK_UNDERFLOW   = 5,
    OGL_ERROR_OUT_OF_MEMORY     = 6,
    OGL_ERROR_MAX               = 7
} OGL_ERROR;

typedef enum OGL_TEXSTATE_e
{
    OGL_TEXSTATE_EMPTY   = 0,
    OGL_TEXSTATE_VALID   = 1,
    OGL_TEXSTATE_DELETED = 2,
    OGL_TEXSTATE_MAX     = 3
} OGL_TEXSTATE;

typedef enum OGL_BUFOBJSTATE_e
{
    OGL_BUFOBJSTATE_EMPTY   = 0,
    OGL_BUFOBJSTATE_VALID   = 1,
    OGL_BUFOBJSTATE_DELETED = 2,
    OGL_BUFOBJSTATE_MAX     = 3
} OGL_BUFOBJSTATE;

typedef enum OGL_BUFOBJUSAGE_e
{
    OGL_BUFOBJUSAGE_STATIC_DRAW  = 0,
    OGL_BUFOBJUSAGE_DYNAMIC_DRAW = 1,
    OGL_BUFOBJUSAGE_MAX          = 2
} OGL_BUFOBJUSAGE;

typedef enum OGL_BUFOBJTARGET_e
{
    OGL_BUFOBJTARGET_ARRAY         = 0,
    OGL_BUFOBJTARGET_ELEMENT_ARRAY = 1,
    OGL_BUFOBJTARGET_MAX           = 2
} OGL_BUFOBJTARGET;

typedef enum OGL_INPUTTYPE_e
{
    OGL_INPUTTYPE_FLOAT = 0,
    OGL_INPUTTYPE_FIXED = 1,
    OGL_INPUTTYPE_INT   = 2,
    OGL_INPUTTYPE_MAX   = 3
} OGL_INPUTTYPE;

typedef enum OGL_OUTPUTTYPE_e
{
    OGL_OUTPUTTYPE_INT   = 0,
    OGL_OUTPUTTYPE_FLOAT = 1,
    OGL_OUTPUTTYPE_FIXED = 2,
    OGL_OUTPUTTYPE_BOOL  = 3,
    OGL_OUTPUTTYPE_MAX   = 4
} OGL_OUTPUTTYPE;

typedef enum OGL_STRINGNAME_e
{
    OGL_STRINGNAME_VENDOR     = 0,
    OGL_STRINGNAME_RENDERER   = 1,
    OGL_STRINGNAME_VERSION    = 2,
    OGL_STRINGNAME_EXTENSIONS = 3,
    OGL_STRINGNAME_MAX        = 4
} OGL_STRINGNAME;

/* --- Matrix stack (CodeView TI=0x146B, 84 bytes w/ 1 _IMatrix4x4 tail) ----- */

typedef struct oglMatrixStack_s
{
    unsigned char index;
    unsigned char depth;
    unsigned char dirty;
    unsigned char padding;
    _IMatrix4x4   elements[1];
} oglMatrixStack_s;

/* --- Texture manager (CodeView TI=0x147A, TI=0x147D) ----------------------- */

typedef struct oglTexContainer_s
{
    OGL_TEXSTATE  state;
    unsigned int  texIndex;
    _grTexture   *texture;
} oglTexContainer_s;

typedef struct oglTextureManager
{
    oglTexContainer_s *containers;
    int                numTextures;
    int                numDeleted;
    int                lastGenerated;
    int                capacity;
} oglTextureManager;

/* --- Buffer-object manager (CodeView TI=0x1484, 0x1487, 0x148A) ------------ */

typedef struct oglBufObj_s
{
    int             size;
    OGL_BUFOBJUSAGE usage;
    void           *buffer;
} oglBufObj_s;

typedef struct oglBufObjContainer_s
{
    OGL_BUFOBJSTATE  state;
    unsigned int     bufObjIndex;
    oglBufObj_s     *object;
} oglBufObjContainer_s;

typedef struct oglBufObjManager_s
{
    oglBufObjContainer_s *containers;
    int                   numObjects;
    int                   numDeleted;
    int                   lastGenerated;
    int                   capacity;
} oglBufObjManager_s;

/* --- Getter state block (CodeView TI=0x149A, 676 = 0x2A4 bytes) ------------
 * Pure scalar/array layout (zero pointers): byte offsets +0..+675 are
 * identical on 32-bit and 64-bit hosts, matching s_getterInfos[].offset. */

typedef struct _oglGetterValues
{
    unsigned char pointSmooth;                      /* +0 */
    unsigned char lineSmooth;                       /* +1 */
    unsigned char cullFace;                         /* +2 */
    unsigned char lighting;                         /* +3 */
    unsigned char lightModelTwoSide;                /* +4 */
    unsigned char colorMaterial;                    /* +5 */
    unsigned char fog;                              /* +6 */
    unsigned char depthTest;                        /* +7 */
    unsigned char depthWritemask;                   /* +8 */
    unsigned char stencilTest;                      /* +9 */
    unsigned char normalize;                        /* +10 */
    unsigned char alphaTest;                        /* +11 */
    unsigned char dither;                           /* +12 */
    unsigned char blend;                            /* +13 */
    unsigned char colorLogicOp;                     /* +14 */
    unsigned char scissorTest;                      /* +15 */
    unsigned char clipPlane0;                       /* +16 */
    unsigned char clipPlane1;                       /* +17 */
    unsigned char clipPlane2;                       /* +18 */
    unsigned char clipPlane3;                       /* +19 */
    unsigned char clipPlane4;                       /* +20 */
    unsigned char clipPlane5;                       /* +21 */
    unsigned char light0;                           /* +22 */
    unsigned char light1;                           /* +23 */
    unsigned char light2;                           /* +24 */
    unsigned char light3;                           /* +25 */
    unsigned char light4;                           /* +26 */
    unsigned char light5;                           /* +27 */
    unsigned char light6;                           /* +28 */
    unsigned char light7;                           /* +29 */
    unsigned char polygonOffsetFill;                /* +30 */
    unsigned char rescaleNormal;                    /* +31 */
    unsigned char vertexArray;                      /* +32 */
    unsigned char normalArray;                      /* +33 */
    unsigned char colorArray;                       /* +34 */
    unsigned char multisample;                      /* +35 */
    unsigned char sampleAlphaToCoverage;            /* +36 */
    unsigned char sampleAlphaToOne;                 /* +37 */
    unsigned char sampleCoverage;                   /* +38 */
    unsigned char sampleCoverageInvert;             /* +39 */
    unsigned char weightArrayOes;                   /* +40 */
    unsigned char matrixPaletteOes;                 /* +41 */
    unsigned char matrixIndexArrayOes;              /* +42 */
    unsigned char pointSpriteOes;                   /* +43 */
    unsigned char pointSizeArrayOes;                /* +44 */
    unsigned char padding0[3];                      /* +45 */
    unsigned char texture2d[2];                     /* +48 */
    unsigned char textureCoordArray[2];             /* +50 */
    unsigned char colorWritemask[4];                /* +52 */
    int           depthBits;                        /* +56 */
    int           stencilBits;                      /* +60 */
    int           stencilPassDepthFail;             /* +64 */
    int           stencilPassDepthPass;             /* +68 */
    int           matrixMode;                       /* +72 */
    int           fogMode;                          /* +76 */
    int           shadeModel;                       /* +80 */
    int           alphaTestFunc;                    /* +84 */
    int           cullFaceMode;                     /* +88 */
    int           blendDst;                         /* +92 */
    int           blendSrc;                         /* +96 */
    int           depthFunc;                        /* +100 */
    int           logicOpMode;                      /* +104 */
    int           frontFace;                        /* +108 */
    int           stencilFunc;                      /* +112 */
    int           stencilFail;                      /* +116 */
    int           perspectiveCorrectionHint;        /* +120 */
    int           pointSmoothHint;                  /* +124 */
    int           lineSmoothHint;                   /* +128 */
    int           fogHint;                          /* +132 */
    int           unpackAlignment;                  /* +136 */
    int           packAlignment;                    /* +140 */
    int           maxLights;                        /* +144 */
    int           vertexArraySize;                  /* +148 */
    int           vertexArrayType;                  /* +152 */
    int           vertexArrayStride;                /* +156 */
    int           normalArrayType;                  /* +160 */
    int           normalArrayStride;                /* +164 */
    int           colorArraySize;                   /* +168 */
    int           colorArrayType;                   /* +172 */
    int           colorArrayStride;                 /* +176 */
    int           maxClipPlanes;                    /* +180 */
    int           maxTextureSize;                   /* +184 */
    int           maxModelviewStackDepth;           /* +188 */
    int           maxProjectionStackDepth;          /* +192 */
    int           sampleBuffers;                    /* +196 */
    int           samples;                          /* +200 */
    int           maxTextureStackDepth;             /* +204 */
    int           maxElementsVertices;              /* +208 */
    int           maxElementsIndices;               /* +212 */
    int           generateMipmapHint;               /* +216 */
    int           activeTexture;                    /* +220 */
    int           clientActiveTexture;              /* +224 */
    int           maxTextureUnits;                  /* +228 */
    int           maxVertexUnitsOes;                /* +232 */
    int           weightArrayTypeOes;               /* +236 */
    int           weightArrayStrideOes;             /* +240 */
    int           weightArraySizeOes;               /* +244 */
    int           subpixelBits;                     /* +248 */
    int           redBits;                          /* +252 */
    int           maxPaletteMatricesOes;            /* +256 */
    int           greenBits;                        /* +260 */
    int           matrixIndexArraySizeOes;          /* +264 */
    int           matrixIndexArrayTypeOes;          /* +268 */
    int           matrixIndexArrayStrideOes;        /* +272 */
    int           blueBits;                         /* +276 */
    int           arrayBufferBinding;               /* +280 */
    int           elementArrayBufferBinding;        /* +284 */
    int           vertexArrayBufferBinding;         /* +288 */
    int           normalArrayBufferBinding;         /* +292 */
    int           colorArrayBufferBinding;          /* +296 */
    int           weightArrayBufferBindingOes;      /* +300 */
    int           pointSizeArrayTypeOes;            /* +304 */
    int           pointSizeArrayStrideOes;          /* +308 */
    int           implementationColorReadTypeOes;   /* +312 */
    int           implementationColorReadFormatOes; /* +316 */
    int           alphaBits;                        /* +320 */
    int           matrixIndexArrayBufferBindingOes; /* +324 */
    int           pointSizeArrayBufferBindingOes;   /* +328 */
    int           numCompressedTextureFormats;      /* +332 */
    unsigned int  stencilWritemask;                 /* +336 */
    unsigned int  stencilValueMask;                 /* +340 */
    float         pointSizeMin;                     /* +344 */
    float         pointSizeMax;                     /* +348 */
    float         pointFadeThresholdSize;           /* +352 */
    float         fogDensity;                       /* +356 */
    float         polygonOffsetUnits;               /* +360 */
    float         fogStart;                         /* +364 */
    float         fogEnd;                           /* +368 */
    float         sampleCoverageValue;              /* +372 */
    float         pointSize;                        /* +376 */
    float         polygonOffsetFactor;              /* +380 */
    float         lineWidth;                        /* +384 */
    float         alphaTestRef;                     /* +388 */
    float         depthClearValue;                  /* +392 */
    int           textureCoordArraySize[2];         /* +396 */
    int           textureCoordArrayType[2];         /* +404 */
    int           textureCoordArrayBufferBinding[2];/* +412 */
    int           textureCoordArrayStride[2];       /* +420 */
    int           maxViewportDims[2];               /* +428 */
    int           textureBinding2d[2];              /* +436 */
    float         aliasedPointSizeRange[2];         /* +444 */
    float         aliasedLineWidthRange[2];         /* +452 */
    float         smoothLineWidthRange[2];          /* +460 */
    float         smoothPointSizeRange[2];          /* +468 */
    float         depthRange[2];                    /* +476 */
    float         pointDistanceAttenuation[3];      /* +484 */
    float         currentNormal[3];                 /* +496 */
    int           viewport[4];                      /* +508 */
    int           scissorBox[4];                    /* +524 */
    float         colorClearValue[4];               /* +540 */
    float         fogColor[4];                      /* +556 */
    float         lightModelAmbient[4];             /* +572 */
    float         currentColor[4];                  /* +588 */
    float         currentTextureCoords[2][4];       /* +604 */
    int           compressedTextureFormats[10];     /* +636 */
} _oglGetterValues;

typedef void (*oglOutputFunc_f)(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);

typedef struct _oglGetterInfo
{
    unsigned int    offset;
    unsigned int    n;
    unsigned int    stride;
    oglOutputFunc_f outputFunc;
} _oglGetterInfo;

/* --- OpenGL ES context (CodeView TI=0x149E) -------------------------------- */

typedef struct oglContext_s
{
    Gerbera_s          *gerbera;
    OGL_ERROR           error;
    struct oglContext_s *shareListPrev;
    struct oglContext_s *shareListNext;
    struct oglContext_s *shareMemPrev;
    struct oglContext_s *shareMemNext;
    oglMatrixStack_s   *matrixStack[GR_MATRIXMODE_MAX];
    oglTextureManager  *textureManager;
    oglBufObjManager_s *bufObjManager;
    _grStackAllocator   allocator;
    unsigned char       displayOpen;
    unsigned char       matrixMode;
    unsigned char       activePalMatrix;
    unsigned char       activeTexture;
    unsigned char       clientActiveTexture;
    unsigned char       pixelStorePackAlignment;
    unsigned char       pixelStoreUnpackAlignment;
    unsigned char       padding[2];
    int                 drawSurfaceWidth;
    int                 drawSurfaceHeight;
    void               *workMemory;
    void               *sharedWorkMem;
    unsigned int        activeTextures[GR_TEXTURETARGET_MAX][GR_MAX_TEXTURE_UNITS];
    unsigned char       textureEnabled[GR_MAX_TEXTURE_UNITS][GR_TEXTURETARGET_MAX];
    float               materialColors[4][4];
    float               lightAlphas[GR_MAX_LIGHTS][4];
    _oglGetterValues    getValues;
    unsigned int        activeBufObjs[OGL_BUFOBJTARGET_MAX];
    int                 pointSizeMin;
    int                 pointSizeMax;
    char                rendererStr[48];
} oglContext_s;

/* --- OpenGL ES surface descriptor (CodeView TI=0x167E, 76 bytes on 32-bit) -- */

typedef struct oglSurfaceDescriptor
{
    int   width;
    int   height;
    void *colorBuffer;
    void *depthBuffer;
    void *stencilBuffer;
    int   colorBufferStride;
    int   depthBufferStride;
    int   stencilBufferStride;
    int   colorBytes;
    int   depthBytes;
    int   stencilBytes;
    int   redBits;
    int   redOffset;
    int   greenBits;
    int   greenOffset;
    int   blueBits;
    int   blueOffset;
    int   alphaBits;
    int   alphaOffset;
} oglSurfaceDescriptor;

/* --- oglError.inl ---------------------------------------------------------- */
void oglSetError(oglContext_s *context, OGL_ERROR error);

/* --- oglDescriptor.inl ----------------------------------------------------- */
void oglFillDescriptor(_grSurfaceDescriptor *desc, unsigned int format, unsigned int type);

/* --- oglMatrixStack.inl ---------------------------------------------------- */
int               oglIsMatrixStackValid(oglMatrixStack_s *stack);
oglMatrixStack_s *oglGetMatrixStack(oglContext_s *context, int matrixMode);
oglMatrixStack_s *oglGetActiveMatrixStack(oglContext_s *context);
_IMatrix4x4      *oglGetActiveMatrix(oglContext_s *context, int setDirty);
_IMatrix4x4      *oglGetMatrix(oglContext_s *context, int matrixMode);
void              oglMatrixToOutput(void *output, _IMatrix4x4 *matrix, OGL_OUTPUTTYPE dataType);
void              oglMatrixFixedToFloat(float *dst, const int *src);

/* --- oglTexManager.inl ----------------------------------------------------- */
int                oglHashTexIndex(unsigned int texIndex);
int                oglHashTexIndex2(unsigned int texIndex);
int                oglIsValidTextureManager(oglTextureManager *texManager);
void               oglTexHashInsert(oglTextureManager *texManager, unsigned int texIndex, _grTexture *texture);
void               oglTexHashRemove(oglTextureManager *texManager, unsigned int texIndex);
_grTexture        *oglGetTexture(oglTextureManager *texManager, unsigned int texIndex);
int                oglIsTexture(oglTextureManager *texManager, unsigned int texIndex);
int                oglNextPowerOfTwo(int v);
void               oglGrowTextureHash(oglTextureManager *texManager);
unsigned int       oglGenerateTextureName(oglTextureManager *texManager);
int                oglAllocateTexture(oglTextureManager *texManager, unsigned int texIndex);
void               oglDeleteTexture(oglTextureManager *texManager, unsigned int texIndex);
void               oglDestroyTextureManager(oglTextureManager *texManager);
oglTextureManager *oglCreateTextureManager(void);

/* --- oglBufObjManager.inl -------------------------------------------------- */
int                 oglIsValidBufObj(oglBufObj_s *bufObj);
int                 oglIsValidBufObjManager(oglBufObjManager_s *bufObjManager);
int                 oglHashBufObjIndex(unsigned int bufObjIndex);
int                 oglHashBufObjIndex2(unsigned int bufObjIndex);
void                oglBufObjHashInsert(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex, oglBufObj_s *object);
void                oglBufObjHashRemove(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex);
oglBufObj_s        *oglGetBufObj(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex);
int                 oglIsBufObj(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex);
void                oglGrowBufObjHash(oglBufObjManager_s *bufObjManager);
unsigned int        oglGenerateBufObjName(oglBufObjManager_s *bufObjManager);
void                oglInitBufObj(oglBufObj_s *bufObj);
void                oglFreeBufObjData(oglBufObj_s *bufObj);
int                 oglAllocateBufObj(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex);
void                oglDeleteBufObj(oglBufObjManager_s *bufObjManager, unsigned int bufObjIndex);
oglBufObjManager_s *oglCreateBufObjManager(void);
void                oglDestroyBufObjManager(oglBufObjManager_s *bufObjManager);
int                 oglSetupBufObj(oglBufObj_s *bufObj, OGL_BUFOBJUSAGE usage, int size);
oglBufObj_s        *oglGetActiveBufObj(oglContext_s *context, OGL_BUFOBJTARGET bufObjTarget);
void                oglBindBuffer(oglContext_s *context, OGL_BUFOBJTARGET bufObjTarget, unsigned int bufObjIndex);

/* --- oglUtil.inl ----------------------------------------------------------- */
int            oglBinarySearch(int key, int length, const unsigned short *keys);
int            oglMapDataType(unsigned int type);
int            oglGetPrimitiveType(int primitive);
void           oglUpdateCoreMatrices(oglContext_s *context);
int            oglSetupRendering(oglContext_s *context, int primitive, int count);
int            oglMapPrimitive(unsigned int mode);
float          oglFixedToFloat(int v);
float          oglFloatClampUnit(float f);
float          oglFixedClampUnit(int x);
int            oglFloatToFixed(float v);
unsigned int   oglFloatsToRGBA(float r, float g, float b, float a);
unsigned int   oglFixedToRGBA(int r, int g, int b, int a);
void           oglInputToXColor3(_XColor3 *color, const void *params, OGL_INPUTTYPE inputType);
unsigned short oglInputAlpha(const void *params, OGL_INPUTTYPE inputType);
unsigned int   oglInputToRGBA(const void *params, OGL_INPUTTYPE inputType);
int            oglInputToIScalarDirect(const void *param, OGL_INPUTTYPE inputType);
int            oglTestInput(const void *param, int ndx, OGL_INPUTTYPE inputType);
float          oglInputToFloatDirect(const void *param, OGL_INPUTTYPE inputType);
void           oglInputToIFVector4Direct(_IFVector4 *dst, const void *params, OGL_INPUTTYPE inputType);
void           oglInputToIFVector4W0(_IFVector4 *dst, const void *params, OGL_INPUTTYPE inputType);
int            oglInputToInteger(const void *param, OGL_INPUTTYPE inputType);
unsigned int   oglInputToEnum(const void *param, OGL_INPUTTYPE inputType);
void           oglInputToFloatv(float *fArr, const void *param, OGL_INPUTTYPE inputType, int n);
void           oglRGBAToFloatv(unsigned int c, float *v);
int            oglMapFogMode(unsigned int mode);
int            oglMapMatrixMode(unsigned int mode);
OGL_STRINGNAME oglMapStringName(unsigned int name);
int            oglMapImageFormat(unsigned int format);
int            oglMapCapability(unsigned int target);
int            oglMapStencilOp(unsigned int op);
int            oglMapBlendFactor(unsigned int factor);
int            oglMapLogicOp(unsigned int op);
int            oglMapClientStateCapability(oglContext_s *context, unsigned int apiCap);
int            oglMapMaterialColor(unsigned int param);
int            oglMapTexEnvMode(unsigned int pname);
unsigned int   oglUnmapTexEnvMode(int pname);
int            oglMapTexEnvCombFunc(unsigned int pname);
unsigned int   oglUnmapTexEnvCombFunc(int pname);
int            oglMapTexEnvCombSrc(unsigned int pname);
unsigned int   oglUnmapTexEnvCombSrc(int pname);
int            oglMapTexEnvCombOperand(unsigned int pname);
unsigned int   oglUnmapTexEnvCombOperand(int pname);
int            oglMapInternalTextureFormat(unsigned int internalFormat);
int            oglIsPalettedTextureFormat(int texFormat);
int            oglMapInternalTextureFormatAndType(unsigned int internalFormat, unsigned int type);
int            oglMapTextureTarget(unsigned int target);
OGL_BUFOBJTARGET oglMapBufObjTarget(unsigned int target);
OGL_BUFOBJUSAGE  oglMapBufObjUsage(unsigned int usage);
unsigned int   oglUnmapBufObjUsage(OGL_BUFOBJUSAGE usage);
int            oglMapWrapMode(unsigned int pname);
unsigned int   oglUnmapWrapMode(int pname);
int            oglMinusOne(void);
void           oglMappingTest(void);
int            oglIsValidTextureType(unsigned int type);
int            oglIsCompatibleTextureType(unsigned int format, unsigned int type);
int            oglIsCompatibleInternalFormat(unsigned int format, int texFormat);
void           oglFillMemoryImageFormat(grMemoryImage_s *image, unsigned int format, unsigned int type);
void           oglUpdateTextureUnit(oglContext_s *context, int texUnit);
_grTexture    *oglGetActiveTexture(oglContext_s *context, int texTarget);
void           oglBindTexture(oglContext_s *context, int texUnit, int texTarget, unsigned int texIndex);
unsigned int   oglMapBufferMask(unsigned int buf);
int            oglIsScalarFogParam(unsigned int pname);
int            oglIsScalarPointParameter(unsigned int pname);
void           oglSetClientState(oglContext_s *context, unsigned int array, int state);
void           oglMaterialInternal(oglContext_s *context, unsigned int face, unsigned int pname,
                                   const void *params, OGL_INPUTTYPE inputType);
void           oglLightInternal(oglContext_s *context, unsigned int lname, unsigned int pname,
                                const void *params, OGL_INPUTTYPE inputType);
int            oglIsScalarLightParameter(unsigned int pname);
int            oglIsScalarLightModelParameter(unsigned int pname);
int            oglIsScalarTexEnvParam(unsigned int pname);
int            oglIsScalarTexParam(unsigned int param);

/* --- oglContext.inl -------------------------------------------------------- */
oglContext_s     *oglGetContext(void);
struct eglState_s;
oglContext_s     *oglGetContextAndState(struct eglState_s **outState);
oglMatrixStack_s *oglInitMatrixStack(oglContext_s *context, int depth);
int               oglInitContext(oglContext_s *context, unsigned char *sharedWorkMem, int sharedWorkMemSize);
int               oglIsValidContext(oglContext_s *context);
oglContext_s     *oglCreateContext(oglContext_s *shareList, oglContext_s *shareMemContext);
void              oglDestroyContext(oglContext_s *context);
int               oglIsValidSurfaceDescriptor(const oglSurfaceDescriptor *desc);
void              oglConvertSurfaceDescriptor(_grDrawSurface *dst, const oglSurfaceDescriptor *src);
void              oglSetBuffers(oglContext_s *context, const oglSurfaceDescriptor *drawDesc,
                                const oglSurfaceDescriptor *readDesc);
int               oglSetSurfaceTexture(oglContext_s *context, void *pixels, int width, int height,
                                       unsigned int format, int mipLevel, int createMipmaps);
void              oglReleaseSurfaceTexture(oglContext_s *context, unsigned int textureName, int mipLevel);
int               oglIsSurfaceTextureReleased(oglContext_s *context, unsigned int textureName);
void              oglCheckValidity(oglContext_s *context);

/* EGL thread-local state (TI=0x1398 in CodeView NB11; implemented in eglSymbian.c) */
struct eglContext_s;
struct eglSurface_s;
struct eglDisplay_s;

typedef struct eglState_s
{
    oglContext_s        *currentESContext;
    struct eglContext_s *currentContext;
    struct eglSurface_s *currentReadSurface;
    struct eglSurface_s *currentDrawSurface;
    struct eglDisplay_s *currentDisplay;
    struct eglDisplay_s *windowDisplay;
    int                  errorCode;
    int                  displayMode;
    void                *heap;
    int                  ownEnvironment;
} eglState_s;

eglState_s *egliGetState(void);
void        eglSetError(int error);
void        egliSetCurrentContext(oglContext_s *context);
void        egliInvalidateWindowSurface(struct eglSurface_s *surface);

/* --- oglGetter.inl --------------------------------------------------------- */
void                  oglIntToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
void                  oglUIntToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
void                  oglBoolToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
void                  oglIScalarToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
void                  oglFloatToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
void                  oglUnitValueToOutput(void *output, const void *input, int n, OGL_OUTPUTTYPE dataType);
const _oglGetterInfo *oglFindGetterInfo(unsigned int token);
void                  oglSetDefaultGetterValues(_oglGetterValues *val);
void                  oglSetGetterValueBool(oglContext_s *context, int offs, unsigned int pname, const int *val);
void                  oglSetGetterValueInt(oglContext_s *context, int offs, unsigned int pname, const int *val);
void                  oglSetGetterValueEnum(oglContext_s *context, int offs, unsigned int pname, const unsigned int *val);
void                  oglSetGetterValueFloat(oglContext_s *context, int offs, unsigned int pname, const float *val);
void                  oglGetInternal(oglContext_s *context, unsigned int pname, void *params, OGL_OUTPUTTYPE dataType);

/* --- oglApi.inl ------------------------------------------------------------ */
void oglSetCapability(oglContext_s *context, unsigned int target, int status);
void oglGetTexParameterInternal(oglContext_s *context, unsigned int target, unsigned int pname,
                                void *params, OGL_OUTPUTTYPE dataType);
void oglGetTexEnvInternal(oglContext_s *context, unsigned int target, unsigned int pname,
                          void *params, OGL_OUTPUTTYPE dataType);
void oglGetMaterialInternal(oglContext_s *context, unsigned int face, unsigned int pname,
                            void *params, OGL_OUTPUTTYPE dataType);
void oglGetLightInternal(oglContext_s *context, unsigned int light, unsigned int pname,
                         void *params, OGL_OUTPUTTYPE dataType);
void oglFogInternal(oglContext_s *context, unsigned int pname, const void *params, OGL_INPUTTYPE inputType);
void oglPointParameterInternal(oglContext_s *context, unsigned int pname, const void *param, OGL_INPUTTYPE inputType);
int  oglIsValidSrcFactor(int factor);
int  oglIsValidDstFactor(int factor);
void oglPointerInternal(oglContext_s *context, int size, int type, int stride,
                        const void *pointer, int arrayType);
void oglLightModelInternal(oglContext_s *context, unsigned int pname, const void *params, OGL_INPUTTYPE inputType);
void oglTexEnvInternal(oglContext_s *context, unsigned int target, unsigned int pname,
                       const void *params, OGL_INPUTTYPE inputType);
void oglTexParameterInternal(oglContext_s *context, unsigned int target, unsigned int pname,
                             const void *params, OGL_INPUTTYPE inputType);
int  oglGetPalettedFormatDescriptor(_grSurfaceDescriptor *desc, unsigned int internalFormat);
void oglUpdateShareListTextures(oglContext_s *context);
void oglShareListRemoveTexture(oglContext_s *context, unsigned int texIndex);
void oglShareListRemoveBufObj(oglContext_s *context, unsigned int bufObjIndex);

HG_EXTERN_C_END

#if HG_ASSERT_ENABLED
#define OGL_ASSERT_AT(file, line, expr_str, cond)                 \
    do                                                            \
    {                                                             \
        if (!(cond) && grAssert((expr_str), (file), (line)) != 0) \
            grDie(0);                                             \
    } while (0)
#else
#define OGL_ASSERT_AT(file, line, expr_str, cond) ((void)0)
#endif

#endif /* OGL_CONTEXT_H */
