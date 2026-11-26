/* oglApi.c - OpenGL ES 1.1 Common Profile + OES entry points and helpers
 * (port of oglApi.inl / oglSmall.c, docs/libGLES_CM.dll.c:16669-21386).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "GLES/gl.h"
#include "gerbera/api_opengl/oglContext.h"
#include "hg/hgFloat.h"
#include "hg/hgInt32.h"
#include "hg/hgMemory.h"

/* --- Static .rdata tables (docs/libGLES_CM.dll.c:1948, 2248..2312) --------- */

static const char *const g_getStrings[4] =
{
    "Nokia",
    "1.1.0",
    "OpenGL ES-CM 1.1",
    "GL_OES_byte_coordinates GL_OES_fixed_point GL_OES_single_precision "
    "GL_OES_read_format GL_OES_query_matrix GL_OES_compressed_paletted_texture "
    "GL_OES_matrix_palette GL_OES_point_sprite GL_OES_point_size_array "
    "GL_OES_draw_texture GL_OES_matrix_get GL_OES_swap_control GL_OES_render_texture"
};

static const unsigned short acceptedCaps[44] =
{
    0x0B10u, 0x0B20u, 0x0B44u, 0x0B50u, 0x0B57u, 0x0B60u, 0x0B71u, 0x0B90u,
    0x0BA1u, 0x0BC0u, 0x0BD0u, 0x0BE2u, 0x0BF2u, 0x0C11u, 0x0DE1u, 0x3000u,
    0x3001u, 0x3002u, 0x3003u, 0x3004u, 0x3005u, 0x4000u, 0x4001u, 0x4002u,
    0x4003u, 0x4004u, 0x4005u, 0x4006u, 0x4007u, 0x8037u, 0x803Au, 0x8074u,
    0x8075u, 0x8076u, 0x8078u, 0x809Du, 0x809Eu, 0x809Fu, 0x80A0u, 0x86ADu,
    0x8840u, 0x8844u, 0x8861u, 0x8B9Cu
};

typedef struct oglArrayRestriction_s
{
    unsigned short typeMask;
    unsigned char  minSize;
    unsigned char  maxSize;
} oglArrayRestriction_s;

static const oglArrayRestriction_s restrictions[7] =
{
    { 197u, 2u, 4u }, /* POSITION:     INT8, INT16, FLOAT, FIXED (2..4) */
    { 197u, 3u, 3u }, /* NORMAL:       INT8, INT16, FLOAT, FIXED (3)    */
    { 194u, 4u, 4u }, /* COLOR:        UINT8, FLOAT, FIXED       (4)    */
    { 192u, 0u, 3u }, /* WEIGHT:       FLOAT, FIXED              (0..3) */
    {   2u, 0u, 3u }, /* MATRIX_INDEX: UINT8                     (0..3) */
    { 192u, 1u, 1u }, /* POINT_SIZE:   FLOAT, FIXED              (1)    */
    { 197u, 2u, 4u }  /* TEXCOORD0/1:  INT8, INT16, FLOAT, FIXED (2..4) */
};

/* Original EPOC tables at 0x35F28/0x35F68 use the point-size array enums. */
static const unsigned int s_arrTypes[8] =
{
    GL_VERTEX_ARRAY_TYPE, GL_NORMAL_ARRAY_TYPE, GL_COLOR_ARRAY_TYPE,
    GL_WEIGHT_ARRAY_TYPE_OES, GL_MATRIX_INDEX_ARRAY_TYPE_OES,
    GL_POINT_SIZE_ARRAY_TYPE_OES, GL_TEXTURE_COORD_ARRAY_TYPE, GL_TEXTURE_COORD_ARRAY_TYPE
};

static const unsigned int s_arrSizes[8] =
{
    GL_VERTEX_ARRAY_SIZE, 0, GL_COLOR_ARRAY_SIZE, GL_WEIGHT_ARRAY_SIZE_OES,
    GL_MATRIX_INDEX_ARRAY_SIZE_OES, 0, GL_TEXTURE_COORD_ARRAY_SIZE, GL_TEXTURE_COORD_ARRAY_SIZE
};

static const unsigned int s_arrStrides[8] =
{
    GL_VERTEX_ARRAY_STRIDE, GL_NORMAL_ARRAY_STRIDE, GL_COLOR_ARRAY_STRIDE,
    GL_WEIGHT_ARRAY_STRIDE_OES, GL_MATRIX_INDEX_ARRAY_STRIDE_OES,
    GL_POINT_SIZE_ARRAY_STRIDE_OES, GL_TEXTURE_COORD_ARRAY_STRIDE, GL_TEXTURE_COORD_ARRAY_STRIDE
};

static const unsigned int s_arrBindings[8] =
{
    GL_VERTEX_ARRAY_BUFFER_BINDING, GL_NORMAL_ARRAY_BUFFER_BINDING, GL_COLOR_ARRAY_BUFFER_BINDING,
    GL_WEIGHT_ARRAY_BUFFER_BINDING_OES, GL_MATRIX_INDEX_ARRAY_BUFFER_BINDING_OES,
    GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES,
    GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING
};

static const unsigned short s_palettedFormats[10] =
{
    0x8B90u, 0x8B91u, 0x8B92u, 0x8B93u, 0x8B94u,
    0x8B95u, 0x8B96u, 0x8B97u, 0x8B98u, 0x8B99u
};

typedef struct oglPalettedFormatInfo_s
{
    unsigned char texFormat;
    unsigned char paletteFormat;
    unsigned char pixelFormat;
    unsigned char colorBytes;
    unsigned char rSize;
    unsigned char gSize;
    unsigned char bSize;
    unsigned char aSize;
    unsigned char rOffset;
    unsigned char gOffset;
    unsigned char bOffset;
    unsigned char aOffset;
} oglPalettedFormatInfo_s;

static const oglPalettedFormatInfo_s s_formatInfos[10] =
{
    {  5u, 1u, 4u, 4u, 8u, 8u, 8u, 0u,  0u, 8u, 16u,  0u },
    {  6u, 1u, 5u, 4u, 8u, 8u, 8u, 8u,  0u, 8u, 16u, 24u },
    {  7u, 1u, 4u, 2u, 5u, 6u, 5u, 0u, 11u, 5u,  0u,  0u },
    {  8u, 1u, 5u, 2u, 4u, 4u, 4u, 4u, 12u, 8u,  4u,  0u },
    {  9u, 1u, 5u, 2u, 5u, 5u, 5u, 1u, 11u, 6u,  1u,  0u },
    { 10u, 2u, 4u, 4u, 8u, 8u, 8u, 0u,  0u, 8u, 16u,  0u },
    { 11u, 2u, 5u, 4u, 8u, 8u, 8u, 8u,  0u, 8u, 16u, 24u },
    { 12u, 2u, 4u, 2u, 5u, 6u, 5u, 0u, 11u, 5u,  0u,  0u },
    { 13u, 2u, 5u, 2u, 4u, 4u, 4u, 4u, 12u, 8u,  4u,  0u },
    { 14u, 2u, 5u, 2u, 5u, 5u, 5u, 1u, 11u, 6u,  1u,  0u }
};

/* --- Capabilities & Getters ------------------------------------------------ */

void oglSetCapability(oglContext_s *context, unsigned int target, int status)
{
    int texTarget;

    OGL_ASSERT_AT("oglApi.inl", 47, "context", context != NULL);
    OGL_ASSERT_AT("oglApi.inl", 48, "status==HG_TRUE || status==HG_FALSE",
                  status == 1 || status == 0);

    texTarget = oglMapTextureTarget(target);
    if (texTarget == GR_TEXTURETARGET_MAX)
    {
        int cap = oglMapCapability(target);
        if (cap == GR_CAPABILITY_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else
        {
            grSetCapability(context->gerbera, cap, status);
            oglSetGetterValueBool(context, 0, target, &status);
        }
    }
    else
    {
        int textureUnit = context->activeTexture;
        OGL_ASSERT_AT("oglApi.inl", 58,
                      "hgInRange32(textureUnit, 0, GR_MAX_TEXTURE_UNITS-1)",
                      hgInRange32(textureUnit, 0, 1) != 0);
        if ((unsigned char)status != context->textureEnabled[textureUnit][texTarget])
        {
            context->textureEnabled[textureUnit][texTarget] = (unsigned char)status;
            oglUpdateTextureUnit(context, textureUnit);
        }
        oglSetGetterValueBool(context, context->activeTexture, target, &status);
    }
}

GL_API void GL_APIENTRY glEnable(GLenum cap)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglSetCapability(context, cap, 1);
}

GL_API void GL_APIENTRY glDisable(GLenum cap)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglSetCapability(context, cap, 0);
}

GL_API const GLubyte *GL_APIENTRY glGetString(GLenum name)
{
    oglContext_s *context = oglGetContext();
    OGL_STRINGNAME sn;

    if (context == NULL)
        return NULL;
    sn = oglMapStringName(name);
    if (sn != OGL_STRINGNAME_MAX)
        return (const GLubyte *)g_getStrings[sn];
    oglSetError(context, OGL_ERROR_INVALID_ENUM);
    return NULL;
}

GL_API void GL_APIENTRY glGetIntegerv(GLenum pname, GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (hgInRange32((int)pname, 35213, 35215) != 0)
            oglGetInternal(context, pname - 32231u, params, OGL_OUTPUTTYPE_FLOAT);
        else
            oglGetInternal(context, pname, params, OGL_OUTPUTTYPE_INT);
    }
}

GL_API void GL_APIENTRY glGetFloatv(GLenum pname, GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetInternal(context, pname, params, OGL_OUTPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glGetFixedv(GLenum pname, GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetInternal(context, pname, params, OGL_OUTPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetInternal(context, pname, params, OGL_OUTPUTTYPE_BOOL);
}

GL_API GLboolean GL_APIENTRY glIsEnabled(GLenum cap)
{
    oglContext_s *context = oglGetContext();
    GLboolean b = 0xCCu;

    if (context == NULL)
        return 0;
    if (oglBinarySearch(cap, 44, acceptedCaps) >= 0)
    {
        b = 0xABu;
        glGetBooleanv(cap, &b);
        OGL_ASSERT_AT("oglApi.inl", 252, "b == GL_TRUE || b == GL_FALSE",
                      b == GL_TRUE || b == GL_FALSE);
        return b;
    }
    oglSetError(context, OGL_ERROR_INVALID_ENUM);
    return 0;
}

GL_API GLboolean GL_APIENTRY glIsTexture(GLuint texture)
{
    oglContext_s *context = oglGetContext();
    return (GLboolean)(context != NULL &&
                       oglIsTexture(context->textureManager, texture) != 0);
}

GL_API GLboolean GL_APIENTRY glIsBuffer(GLuint buffer)
{
    oglContext_s *context = oglGetContext();
    return (GLboolean)(context != NULL &&
                       oglIsBufObj(context->bufObjManager, buffer) != 0);
}

void oglGetTexParameterInternal(oglContext_s *context, unsigned int target,
                                unsigned int pname, void *params,
                                OGL_OUTPUTTYPE dataType)
{
    int texTarget = oglMapTextureTarget(target);
    _grTexture *texture;
    int outVals[4];
    int count = 1;

    OGL_ASSERT_AT("oglApi.inl", 284, "context", context != NULL);
    if (texTarget == GR_TEXTURETARGET_MAX)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (params == NULL)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }

    texture = oglGetActiveTexture(context, texTarget);
    OGL_ASSERT_AT("oglApi.inl", 292, "texture", texture != NULL);

    switch (pname)
    {
    case GL_TEXTURE_MAG_FILTER:
    {
        unsigned int fm = grGetTextureFilter(texture, GR_FILTERTYPE_MAG);
        outVals[0] = ((fm & 1u) != 0) ? GL_LINEAR : GL_NEAREST;
        break;
    }
    case GL_TEXTURE_MIN_FILTER:
    {
        unsigned int fm = grGetTextureFilter(texture, GR_FILTERTYPE_MIN);
        if ((fm & 2u) != 0)
        {
            if ((fm & 1u) != 0)
                outVals[0] = ((fm & 4u) != 0) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
            else
                outVals[0] = ((fm & 4u) != 0) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
        }
        else
        {
            outVals[0] = ((fm & 1u) != 0) ? GL_LINEAR : GL_NEAREST;
        }
        break;
    }
    case GL_TEXTURE_WRAP_S:
        outVals[0] = (int)oglUnmapWrapMode(grGetTextureWrapMode(texture, GR_TEXCOORD_S));
        break;
    case GL_TEXTURE_WRAP_T:
        outVals[0] = (int)oglUnmapWrapMode(grGetTextureWrapMode(texture, GR_TEXCOORD_T));
        break;
    case GL_GENERATE_MIPMAP:
        outVals[0] = (grGetTextureGenMipmaps(texture) != 0);
        break;
    case GL_TEXTURE_CROP_RECT_OES:
    {
        _grRect rect;
        grGetTextureCropRect(texture, &rect);
        outVals[0] = rect.x0;
        outVals[1] = rect.y0;
        outVals[2] = rect.x1 - rect.x0;
        outVals[3] = rect.y1 - rect.y0;
        count = 4;
        break;
    }
    default:
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }

    oglIntToOutput(params, outVals, count, dataType);
}

GL_API void GL_APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexParameterInternal(context, target, pname, params, OGL_OUTPUTTYPE_INT);
}

GL_API void GL_APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexParameterInternal(context, target, pname, params, OGL_OUTPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexParameterInternal(context, target, pname, params, OGL_OUTPUTTYPE_FIXED);
}

void oglGetTexEnvInternal(oglContext_s *context, unsigned int target,
                          unsigned int pname, void *params,
                          OGL_OUTPUTTYPE dataType)
{
    int iVal;

    OGL_ASSERT_AT("oglApi.inl", 376, "context", context != NULL);
    OGL_ASSERT_AT("oglApi.inl", 377,
                  "hgInRange32(dataType, 0, OGL_OUTPUTTYPE_MAX - 1)",
                  hgInRange32((int)dataType, 0, OGL_OUTPUTTYPE_MAX - 1) != 0);

    if (target != GL_TEXTURE_ENV && target != GL_POINT_SPRITE_OES)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (params == NULL)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }
    if (target == GL_POINT_SPRITE_OES && pname != GL_COORD_REPLACE_OES)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }

    switch (pname)
    {
    case GL_TEXTURE_ENV_COLOR:
    {
        float fCol[4];
        unsigned int rgba = grGetTexEnvColor(context->gerbera, context->activeTexture);
        oglRGBAToFloatv(rgba, fCol);
        oglUnitValueToOutput(params, fCol, 4, dataType);
        return;
    }
    case GL_ALPHA_SCALE:
    case GL_RGB_SCALE:
    {
        int scale = (pname == GL_RGB_SCALE)
                  ? grGetTexEnvRGBCombScale(context->gerbera, context->activeTexture)
                  : grGetTexEnvAlphaCombScale(context->gerbera, context->activeTexture);
        int sVal = IScalar_intToScalar(scale);
        oglIScalarToOutput(params, &sVal, 1, dataType);
        return;
    }
    case GL_TEXTURE_ENV_MODE:
        iVal = (int)oglUnmapTexEnvMode(grGetTexEnvMode(context->gerbera, context->activeTexture));
        oglIntToOutput(params, &iVal, 1, dataType);
        return;
    case GL_COMBINE_RGB:
        iVal = (int)oglUnmapTexEnvCombFunc(grGetTexEnvRGBCombFunc(context->gerbera, context->activeTexture));
        oglIntToOutput(params, &iVal, 1, dataType);
        return;
    case GL_COMBINE_ALPHA:
        iVal = (int)oglUnmapTexEnvCombFunc(grGetTexEnvAlphaCombFunc(context->gerbera, context->activeTexture));
        oglIntToOutput(params, &iVal, 1, dataType);
        return;
    default:
        break;
    }

    if (pname - GL_SRC0_RGB <= 2u)
    {
        iVal = (int)oglUnmapTexEnvCombSrc(
            grGetTexEnvRGBCombSrc(context->gerbera, context->activeTexture, (int)(pname - GL_SRC0_RGB)));
    }
    else if (pname - GL_SRC0_ALPHA <= 2u)
    {
        iVal = (int)oglUnmapTexEnvCombSrc(
            grGetTexEnvAlphaCombSrc(context->gerbera, context->activeTexture, (int)(pname - GL_SRC0_ALPHA)));
    }
    else if (pname - GL_OPERAND0_RGB <= 2u)
    {
        iVal = (int)oglUnmapTexEnvCombOperand(
            grGetTexEnvRGBCombOperand(context->gerbera, context->activeTexture, (int)(pname - GL_OPERAND0_RGB)));
    }
    else if (pname - GL_OPERAND0_ALPHA <= 2u)
    {
        iVal = (int)oglUnmapTexEnvCombOperand(
            grGetTexEnvAlphaCombOperand(context->gerbera, context->activeTexture, (int)(pname - GL_OPERAND0_ALPHA)));
    }
    else if (pname == GL_COORD_REPLACE_OES)
    {
        iVal = grGetTexCoordReplace(context->gerbera, context->activeTexture);
    }
    else
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }

    oglIntToOutput(params, &iVal, 1, dataType);
}

GL_API void GL_APIENTRY glGetTexEnvfv(GLenum env, GLenum pname, GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexEnvInternal(context, env, pname, params, OGL_OUTPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glGetTexEnviv(GLenum env, GLenum pname, GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexEnvInternal(context, env, pname, params, OGL_OUTPUTTYPE_INT);
}

GL_API void GL_APIENTRY glGetTexEnvxv(GLenum env, GLenum pname, GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetTexEnvInternal(context, env, pname, params, OGL_OUTPUTTYPE_FIXED);
}

void oglGetMaterialInternal(oglContext_s *context, unsigned int face,
                            unsigned int pname, void *params,
                            OGL_OUTPUTTYPE dataType)
{
    OGL_ASSERT_AT("oglApi.inl", 481, "context", context != NULL);
    OGL_ASSERT_AT("oglApi.inl", 482,
                  "hgInRange32(dataType, 0, OGL_OUTPUTTYPE_MAX - 1)",
                  hgInRange32((int)dataType, 0, OGL_OUTPUTTYPE_MAX - 1) != 0);

    if (face != GL_FRONT && face != GL_BACK)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (params == NULL)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }
    if (pname == GL_SHININESS)
    {
        int shininess = grGetMaterialShininess(context->gerbera);
        oglIScalarToOutput(params, &shininess, 1, dataType);
        return;
    }
    if (pname - GL_AMBIENT <= 2u || pname == GL_EMISSION)
    {
        int mc = oglMapMaterialColor(pname);
        float vals[4];
        int i;
        for (i = 3; i >= 0; --i)
            vals[i] = context->materialColors[mc][i];
        oglUnitValueToOutput(params, vals, 4, dataType);
    }
    else
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetMaterialInternal(context, face, pname, params, OGL_OUTPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetMaterialInternal(context, face, pname, params, OGL_OUTPUTTYPE_FIXED);
}

void oglGetLightInternal(oglContext_s *context, unsigned int light,
                         unsigned int pname, void *params,
                         OGL_OUTPUTTYPE dataType)
{
    int lIdx;
    int sVals[4];
    int count = 1;

    OGL_ASSERT_AT("oglApi.inl", 539, "context", context != NULL);
    OGL_ASSERT_AT("oglApi.inl", 540,
                  "hgInRange32(dataType, 0, OGL_OUTPUTTYPE_MAX - 1)",
                  hgInRange32((int)dataType, 0, OGL_OUTPUTTYPE_MAX - 1) != 0);

    lIdx = (int)(light - GL_LIGHT0);
    if (hgInRange32(lIdx, 0, 7) == 0)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (params == NULL)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }

    switch (pname)
    {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    {
        int mc = oglMapMaterialColor(pname);
        _XColor3 xcol;
        float fCol[4];
        grGetLightColor(context->gerbera, lIdx, mc, &xcol);
        fCol[0] = IScalar_stof(XScalar_toIScalar(xcol.r));
        fCol[1] = IScalar_stof(XScalar_toIScalar(xcol.g));
        fCol[2] = IScalar_stof(XScalar_toIScalar(xcol.b));
        fCol[3] = context->lightAlphas[lIdx][mc];
        oglUnitValueToOutput(params, fCol, 4, dataType);
        return;
    }
    case GL_POSITION:
    {
        _IFVector4 *pos = grGetLightOrgPosition(context->gerbera, lIdx);
        OGL_ASSERT_AT("oglApi.inl", 573, "pos", pos != NULL);
        sVals[0] = IFVector4_getf(pos, 0);
        sVals[1] = IFVector4_getf(pos, 1);
        sVals[2] = IFVector4_getf(pos, 2);
        sVals[3] = IFVector4_getf(pos, 3);
        count = 4;
        break;
    }
    case GL_SPOT_DIRECTION:
    {
        _IFVector3 *dir = grGetLightOrgDirection(context->gerbera, lIdx);
        OGL_ASSERT_AT("oglApi.inl", 585, "dir", dir != NULL);
        sVals[0] = IFVector3_getf(dir, 0);
        sVals[1] = IFVector3_getf(dir, 1);
        sVals[2] = IFVector3_getf(dir, 2);
        count = 3;
        break;
    }
    case GL_SPOT_EXPONENT:
        sVals[0] = grGetLightExponent(context->gerbera, lIdx);
        break;
    case GL_SPOT_CUTOFF:
        sVals[0] = grGetLightSpotCutoff(context->gerbera, lIdx);
        break;
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
        sVals[0] = grGetLightAttenuation(context->gerbera, lIdx, (int)(pname - GL_CONSTANT_ATTENUATION));
        break;
    default:
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }

    oglIScalarToOutput(params, sVals, count, dataType);
}

GL_API void GL_APIENTRY glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetLightInternal(context, light, pname, params, OGL_OUTPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glGetLightxv(GLenum light, GLenum pname, GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglGetLightInternal(context, light, pname, params, OGL_OUTPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glGetClipPlanef(GLenum pname, GLfloat eqn[4])
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (eqn != NULL)
        {
            if (hgInRange32((int)(pname - 12278u), 10, 15) != 0)
            {
                _IFVector4 *eq = grGetUserClipPlane(context->gerbera, (int)(pname - 12278u));
                int i;
                for (i = 0; i < 4; ++i)
                    eqn[i] = IScalar_stof(IFVector4_getf(eq, i));
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glGetClipPlanex(GLenum pname, GLfixed eqn[4])
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (eqn != NULL)
        {
            float eq[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glGetClipPlanef(pname, eq);
            eqn[0] = IScalar_scalarToFixed(IScalar_ftos(eq[0]), 16);
            eqn[1] = IScalar_scalarToFixed(IScalar_ftos(eq[1]), 16);
            eqn[2] = IScalar_scalarToFixed(IScalar_ftos(eq[2]), 16);
            eqn[3] = IScalar_scalarToFixed(IScalar_ftos(eq[3]), 16);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        OGL_BUFOBJTARGET bt = oglMapBufObjTarget(target);
        oglBufObj_s *bufObj;
        if (bt == OGL_BUFOBJTARGET_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        if (params == NULL)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        bufObj = oglGetActiveBufObj(context, bt);
        OGL_ASSERT_AT("oglApi.inl", 675, "bufObj", bufObj != NULL);
        switch (pname)
        {
        case GL_BUFFER_SIZE:
            *params = bufObj->size;
            break;
        case GL_BUFFER_USAGE:
            *params = (GLint)oglUnmapBufObjUsage(bufObj->usage);
            break;
        case 0x88BBu: /* GL_BUFFER_ACCESS */
            *params = 0x88B9; /* GL_WRITE_ONLY */
            break;
        default:
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            break;
        }
    }
}

GL_API void GL_APIENTRY glGetPointerv(GLenum pname, GLvoid **params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int arr;
        if (params == NULL)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        switch (pname)
        {
        case GL_VERTEX_ARRAY_POINTER:
            arr = GR_VERTEXARRAY_POSITION;
            break;
        case GL_NORMAL_ARRAY_POINTER:
            arr = GR_VERTEXARRAY_NORMAL;
            break;
        case GL_COLOR_ARRAY_POINTER:
            arr = GR_VERTEXARRAY_COLOR;
            break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            arr = context->clientActiveTexture + 6;
            break;
        case GL_WEIGHT_ARRAY_POINTER_OES:
            arr = GR_VERTEXARRAY_WEIGHT;
            break;
        case GL_MATRIX_INDEX_ARRAY_POINTER_OES:
            arr = GR_VERTEXARRAY_MATRIX_INDEX;
            break;
        case GL_POINT_SIZE_ARRAY_POINTER_OES:
            arr = GR_VERTEXARRAY_POINT_SIZE;
            break;
        default:
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        OGL_ASSERT_AT("oglApi.inl", 732,
                      "hgInRange32(arr, 0, GR_VERTEXARRAY_MAX - 1)",
                      hgInRange32(arr, 0, 7) != 0);
        *params = grGetVertexArrayPointer(context->gerbera, arr);
    }
}

GL_API GLenum GL_APIENTRY glGetError(void)
{
    oglContext_s *context = oglGetContext();
    GLenum retVal;

    if (context == NULL)
        return GL_INVALID_OPERATION;
    retVal = (GLenum)(context->error + 1279);
    OGL_ASSERT_AT("oglApi.inl", 854,
                  "hgInRange32((int)context->error, 0, OGL_ERROR_MAX-1)",
                  hgInRange32((int)context->error, 0, OGL_ERROR_MAX - 1) != 0);
    if (context->error == OGL_ERROR_OK)
        return GL_NO_ERROR;
    context->error = OGL_ERROR_OK;
    return retVal;
}

GL_API void GL_APIENTRY glHint(GLenum target, GLenum mode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int validTarget = (target == GL_PERSPECTIVE_CORRECTION_HINT ||
                           target == GL_LINE_SMOOTH_HINT ||
                           target == GL_POINT_SMOOTH_HINT ||
                           target == GL_FOG_HINT ||
                           target == GL_GENERATE_MIPMAP_HINT);
        int validMode   = (mode == GL_NICEST ||
                           mode == GL_FASTEST ||
                           mode == GL_DONT_CARE);
        if (validTarget && validMode)
        {
            unsigned int m = mode;
            if (target == GL_PERSPECTIVE_CORRECTION_HINT)
                grSetCapability(context->gerbera, GR_CAPABILITY_PERSPECTIVE_CORRECTION,
                                mode == GL_NICEST);
            oglSetGetterValueEnum(context, 0, target, &m);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

/* --- Clear Operations ------------------------------------------------------ */

GL_API void GL_APIENTRY glClear(GLbitfield mask)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if ((mask & 0xFFFFBAFFu) != 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
        else
        {
            unsigned int mapped = oglMapBufferMask(mask);
            if (mapped != 0)
                grClearBuffers(context->gerbera, mapped);
        }
    }
}

GL_API void GL_APIENTRY glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        float fArr[4];
        int i;
        grSetClearColor(context->gerbera, oglFloatsToRGBA(red, green, blue, alpha));
        fArr[0] = red;
        fArr[1] = green;
        fArr[2] = blue;
        fArr[3] = alpha;
        for (i = 3; i >= 0; --i)
            fArr[i] = oglFloatClampUnit(fArr[i]);
        oglSetGetterValueFloat(context, 0, GL_COLOR_CLEAR_VALUE, fArr);
    }
}

GL_API void GL_APIENTRY glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int xArr[4];
        float fArr[4];
        int i;
        grSetClearColor(context->gerbera, oglFixedToRGBA(red, green, blue, alpha));
        xArr[0] = red;
        xArr[1] = green;
        xArr[2] = blue;
        xArr[3] = alpha;
        for (i = 3; i >= 0; --i)
            fArr[i] = oglFixedClampUnit(xArr[i]);
        oglSetGetterValueFloat(context, 0, GL_COLOR_CLEAR_VALUE, fArr);
    }
}

GL_API void GL_APIENTRY glClearDepthf(GLclampf depth)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int sDepth = IScalar_ftos(depth);
        float f = oglFloatClampUnit(depth);
        grSetClearDepth(context->gerbera, IUnitScalar_maxZero(IScalar_toUnitScalar(sDepth)));
        oglSetGetterValueFloat(context, 0, GL_DEPTH_CLEAR_VALUE, &f);
    }
}

GL_API void GL_APIENTRY glClearDepthx(GLclampx depth)
{
    glClearDepthf(oglFixedToFloat(depth));
}

GL_API void GL_APIENTRY glClearStencil(GLint s)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        grSetClearStencil(context->gerbera, (unsigned int)s);
}

/* --- Fog ------------------------------------------------------------------- */

void oglFogInternal(oglContext_s *context, unsigned int pname,
                    const void *params, OGL_INPUTTYPE inputType)
{
    OGL_ASSERT_AT("oglApi.inl", 1080, "context", context != NULL);
    if (hgInRange32((int)pname, GL_FOG_DENSITY, GL_FOG_COLOR) != 0)
    {
        Gerbera_s *gerbera = context->gerbera;
        int sParam = oglInputToIScalarDirect(params, inputType);
        float fParam = oglInputToFloatDirect(params, inputType);

        switch (pname)
        {
        case GL_FOG_DENSITY:
            if (oglTestInput(params, 0, inputType) >= 0)
            {
                grSetFogDensity(gerbera, sParam);
                oglSetGetterValueFloat(context, 0, pname, &fParam);
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
            break;

        case GL_FOG_START:
        case GL_FOG_END:
        {
            int isEnd = (pname != GL_FOG_START);
            int range[2];
            range[isEnd] = sParam;
            range[1 - isEnd] = grGetIScalar(gerbera, isEnd ? GR_QUERY_FOG_START : GR_QUERY_FOG_END);
            grSetFogRange(gerbera, range[0], range[1]);
            oglSetGetterValueFloat(context, 0, pname, &fParam);
            break;
        }

        case GL_FOG_MODE:
        {
            unsigned int modeEnum = oglInputToEnum(params, inputType);
            int fogMode = oglMapFogMode(modeEnum);
            if (fogMode == GR_FOGMODE_MAX)
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
                return;
            }
            grSetFogMode(gerbera, fogMode);
            oglSetGetterValueEnum(context, 0, pname, &modeEnum);
            break;
        }

        default:
        {
            float clamped[4];
            int i;
            OGL_ASSERT_AT("oglApi.inl", 1131, "pname == GL_FOG_COLOR",
                          pname == GL_FOG_COLOR);
            grSetFogColor(gerbera, oglInputToRGBA(params, inputType));
            if (inputType != OGL_INPUTTYPE_FLOAT)
            {
                const int *xp = (const int *)params;
                OGL_ASSERT_AT("oglApi.inl", 1146, "inputType == OGL_INPUTTYPE_FIXED",
                              inputType == OGL_INPUTTYPE_FIXED);
                for (i = 3; i >= 0; --i)
                    clamped[i] = oglFixedClampUnit(xp[i]);
            }
            else
            {
                const float *fp = (const float *)params;
                for (i = 3; i >= 0; --i)
                    clamped[i] = oglFloatClampUnit(fp[i]);
            }
            oglSetGetterValueFloat(context, 0, pname, clamped);
            break;
        }
        }
    }
    else
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glFogf(GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarFogParam(pname) != 0)
            oglFogInternal(context, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glFogfv(GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglFogInternal(context, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glFogx(GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarFogParam(pname) != 0)
            oglFogInternal(context, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glFogxv(GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglFogInternal(context, pname, params, OGL_INPUTTYPE_FIXED);
}

/* --- Viewport, Scissor, Culling, Points, Lines, PolygonOffset -------------- */

GL_API void GL_APIENTRY glDepthRangef(GLclampf zNear, GLclampf zFar)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int sn = IScalar_clampUnit(IScalar_iftos((int)hgFloatBitPattern(zNear)));
        int sf = IScalar_clampUnit(IScalar_iftos((int)hgFloatBitPattern(zFar)));
        float fVal[2];
        grSetDepthRange(context->gerbera, sn, sf);
        fVal[0] = IScalar_stof(sn);
        fVal[1] = IScalar_stof(sf);
        oglSetGetterValueFloat(context, 0, GL_DEPTH_RANGE, fVal);
    }
}

GL_API void GL_APIENTRY glDepthRangex(GLclampx zNear, GLclampx zFar)
{
    glDepthRangef(oglFixedToFloat(zNear), oglFixedToFloat(zFar));
}

GL_API void GL_APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    eglState_s *tlsState = NULL;
    oglContext_s *context = oglGetContextAndState(&tlsState);
    if (context != NULL)
    {
        if (width >= 0 && height >= 0)
        {
            int x0, x1, y0, y1;
            int vp[4];

            if (tlsState != NULL)
                egliInvalidateWindowSurface(tlsState->currentDrawSurface);
            grLockSurfaceState(tlsState, GR_SURFACE_DRAW);
            grReleaseSurfaceState(tlsState, GR_SURFACE_DRAW);
            width = hgMin32(width, 2048);
            height = hgMin32(height, 2048);
            x0 = x;
            /* Original coordinate arithmetic wraps before the signed clamps. */
            x1 = hgMax32(x, (int)((unsigned int)width + (unsigned int)x));
            y1 = (int)((unsigned int)context->drawSurfaceHeight - (unsigned int)y);
            y0 = hgMin32((int)((unsigned int)y1 - (unsigned int)height), y1);
            grSetViewport(context->gerbera, x0, x1, y0, y1);

            vp[0] = x;
            vp[1] = y;
            vp[2] = width;
            vp[3] = height;
            oglSetGetterValueInt(context, 0, GL_VIEWPORT, vp);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (width >= 0 && height >= 0)
        {
            int x0 = hgClamp32(x, 0, context->drawSurfaceWidth);
            int x1 = hgClamp32((int)((unsigned int)width + (unsigned int)x), 0, context->drawSurfaceWidth);
            int y1 = (int)((unsigned int)context->drawSurfaceHeight - (unsigned int)y);
            int y0 = hgClamp32((int)((unsigned int)y1 - (unsigned int)height), 0, context->drawSurfaceHeight);
            int sc[4];

            y1 = hgClamp32(y1, 0, context->drawSurfaceHeight);
            x1 = hgMax32(x1, x0);
            y1 = hgMax32(y1, y0);
            grSetScissor(context->gerbera, x0, x1, y0, y1);

            sc[0] = x;
            sc[1] = y;
            sc[2] = width;
            sc[3] = height;
            oglSetGetterValueInt(context, 0, GL_SCISSOR_BOX, sc);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glFrontFace(GLenum mode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
#if GERBERA_ARM_REFERENCE
        /* ARM validates the stored byte, including aliases of CW/CCW. */
        unsigned char frontFace = (unsigned char)(mode - GL_CW);
        if (frontFace <= 1)
#else
        unsigned int frontFace = mode - GL_CW;
        if (mode == GL_CW || mode == GL_CCW)
#endif
        {
            unsigned int m = mode;
            int cullMask = grGetInteger(context->gerbera, GR_QUERY_CULL_MASK);
            grSetCullMode(context->gerbera, frontFace, (unsigned int)cullMask);
            oglSetGetterValueEnum(context, 0, GL_FRONT_FACE, &m);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glCullFace(GLenum mode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (mode == GL_FRONT || mode == GL_BACK || mode == GL_FRONT_AND_BACK)
        {
            unsigned int m = mode;
            unsigned int cullMask = 0;
            int frontFace;
            if (mode != GL_FRONT)
                cullMask = 2u;
            if (mode != GL_BACK)
                cullMask |= 1u;
            frontFace = grGetInteger(context->gerbera, GR_QUERY_FRONT_FACE);
            grSetCullMode(context->gerbera, frontFace, cullMask);
            oglSetGetterValueEnum(context, 0, GL_CULL_FACE_MODE, &m);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glPointSize(GLfloat size)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int iSize = IScalar_ftos(size);
        int fSize = IScalar_scalarToFixed(iSize, 16);
        if ((int)hgFloatBitPattern(size) > 0)
        {
            grSetPointSize(context->gerbera, hgClamp32(fSize, 0x10000, 0x400000));
            oglSetGetterValueFloat(context, 0, GL_POINT_SIZE, &size);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glPointSizex(GLfixed size)
{
    glPointSize(oglFixedToFloat(size));
}

void oglPointParameterInternal(oglContext_s *context, unsigned int pname,
                               const void *param, OGL_INPUTTYPE inputType)
{
    int sVal;
    int fixedVal;
    float fVal;

    if (param != NULL &&
        (sVal = oglInputToIScalarDirect(param, inputType),
         fVal = oglInputToFloatDirect(param, inputType),
         fixedVal = IScalar_scalarToFixed(sVal, 16),
         oglTestInput(param, 0, inputType) >= 0))
    {
        switch (pname)
        {
        case GL_POINT_SIZE_MIN:
            context->pointSizeMin = fixedVal;
            OGL_ASSERT_AT("oglApi.inl", 1451, "oglIsScalarPointParameter(pname)",
                          oglIsScalarPointParameter(pname) != 0);
            oglSetGetterValueFloat(context, 0, pname, &fVal);
            break;

        case GL_POINT_SIZE_MAX:
            context->pointSizeMax = fixedVal;
            OGL_ASSERT_AT("oglApi.inl", 1451, "oglIsScalarPointParameter(pname)",
                          oglIsScalarPointParameter(pname) != 0);
            oglSetGetterValueFloat(context, 0, pname, &fVal);
            break;

        case GL_POINT_FADE_THRESHOLD_SIZE:
            grSetPointFadeThreshold(context->gerbera, fixedVal);
            OGL_ASSERT_AT("oglApi.inl", 1451, "oglIsScalarPointParameter(pname)",
                          oglIsScalarPointParameter(pname) != 0);
            oglSetGetterValueFloat(context, 0, pname, &fVal);
            break;

        case GL_POINT_DISTANCE_ATTENUATION:
        {
            int attn[3];
            float fAttn[3];
            int i;
            OGL_ASSERT_AT("oglApi.inl", 1423,
                          "inputType == OGL_INPUTTYPE_FLOAT || inputType == OGL_INPUTTYPE_FIXED",
                          inputType == OGL_INPUTTYPE_FLOAT || inputType == OGL_INPUTTYPE_FIXED);
            for (i = 2; i >= 0; --i)
                attn[i] = oglInputToIScalarDirect((const char *)param + 4 * i, inputType);
            if (oglTestInput(param, 1, inputType) < 0 ||
                oglTestInput(param, 2, inputType) < 0)
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
                return;
            }
            grSetPointDistAttn(context->gerbera, attn);
            for (i = 2; i >= 0; --i)
                fAttn[i] = oglInputToFloatDirect((const char *)param + 4 * i, inputType);
            oglSetGetterValueFloat(context, 0, pname, fAttn);
            break;
        }

        default:
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            break;
        }
    }
    else
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
    }
}

GL_API void GL_APIENTRY glPointParameterfv(GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointParameterInternal(context, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glPointParameterxv(GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointParameterInternal(context, pname, params, OGL_INPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glPointParameterf(GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarPointParameter(pname) != 0)
            oglPointParameterInternal(context, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glPointParameterx(GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarPointParameter(pname) != 0)
            oglPointParameterInternal(context, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glLineWidth(GLfloat width)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int iWidth = IScalar_ftos(width);
        int fWidth = IScalar_scalarToFixed(iWidth, 16);
        if ((int)hgFloatBitPattern(width) > 0)
        {
            grSetLineWidth(context->gerbera, hgClamp32(fWidth, 1, 0x400000));
            oglSetGetterValueFloat(context, 0, GL_LINE_WIDTH, &width);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glLineWidthx(GLfixed width)
{
    glLineWidth(oglFixedToFloat(width));
}

GL_API void GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        grSetDepthOffset(context->gerbera, oglFloatToFixed(factor), oglFloatToFixed(units));
        oglSetGetterValueFloat(context, 0, GL_POLYGON_OFFSET_FACTOR, &factor);
        oglSetGetterValueFloat(context, 0, GL_POLYGON_OFFSET_UNITS, &units);
    }
}

GL_API void GL_APIENTRY glPolygonOffsetx(GLfixed factor, GLfixed units)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        float fFactor = IScalar_stof(IScalar_fixedToScalar(factor, 16));
        float fUnits  = IScalar_stof(IScalar_fixedToScalar(units, 16));
        grSetDepthOffset(context->gerbera, factor, units);
        oglSetGetterValueFloat(context, 0, GL_POLYGON_OFFSET_FACTOR, &fFactor);
        oglSetGetterValueFloat(context, 0, GL_POLYGON_OFFSET_UNITS, &fUnits);
    }
}

GL_API void GL_APIENTRY glSampleCoverage(GLclampf value, GLboolean invert)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        float val = oglFloatClampUnit(value);
        int inv = (invert != 0);
        oglSetGetterValueFloat(context, 0, GL_SAMPLE_COVERAGE_VALUE, &val);
        oglSetGetterValueBool(context, 0, GL_SAMPLE_COVERAGE_INVERT, &inv);
    }
}

GL_API void GL_APIENTRY glSampleCoveragex(GLclampx value, GLboolean invert)
{
    glSampleCoverage(oglFixedToFloat(value), invert);
}

/* --- Per-Fragment Tests, Blending, LogicOp, Masks -------------------------- */

GL_API void GL_APIENTRY glAlphaFunc(GLenum func, GLclampf ref)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int alphaFunc = (int)(func - GL_NEVER);
        if (hgInRange32(alphaFunc, 0, 7) != 0)
        {
            unsigned int fn = func;
            float fRef = oglFloatClampUnit(ref);
            grSetAlphaFunc(context->gerbera, alphaFunc, IScalar_clampUnit(IScalar_ftos(ref)));
            oglSetGetterValueEnum(context, 0, GL_ALPHA_TEST_FUNC, &fn);
            oglSetGetterValueFloat(context, 0, GL_ALPHA_TEST_REF, &fRef);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glAlphaFuncx(GLenum func, GLclampx ref)
{
    glAlphaFunc(func, oglFixedToFloat(ref));
}

GL_API void GL_APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int stencilFunc = (int)(func - GL_NEVER);
        if (hgInRange32(stencilFunc, 0, 7) != 0)
        {
            unsigned int fn = func;
            int m = (int)mask;
            grSetStencilFunc(context->gerbera, stencilFunc, (unsigned int)ref, mask);
            oglSetGetterValueEnum(context, 0, GL_STENCIL_FUNC, &fn);
            oglSetGetterValueInt(context, 0, GL_STENCIL_VALUE_MASK, &m);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int sf = oglMapStencilOp(fail);
        int zf = oglMapStencilOp(zfail);
        int zp = oglMapStencilOp(zpass);
        if (sf == GR_STENCILOP_MAX || zf == GR_STENCILOP_MAX || zp == GR_STENCILOP_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else
        {
            unsigned int uFail = fail, uZFail = zfail, uZPass = zpass;
            grSetStencilOp(context->gerbera, sf, zf, zp);
            oglSetGetterValueEnum(context, 0, GL_STENCIL_FAIL, &uFail);
            oglSetGetterValueEnum(context, 0, GL_STENCIL_PASS_DEPTH_FAIL, &uZFail);
            oglSetGetterValueEnum(context, 0, GL_STENCIL_PASS_DEPTH_PASS, &uZPass);
        }
    }
}

GL_API void GL_APIENTRY glDepthFunc(GLenum func)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int depthFunc = (int)(func - GL_NEVER);
        if (hgInRange32(depthFunc, 0, 7) != 0)
        {
            unsigned int fn = func;
            grSetDepthFunc(context->gerbera, depthFunc);
            oglSetGetterValueEnum(context, 0, GL_DEPTH_FUNC, &fn);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

int oglIsValidSrcFactor(int factor)
{
    OGL_ASSERT_AT("oglApi.inl", 1655,
                  "hgInRange32((int)factor, 0, GR_BLENDFACTOR_MAX-1)",
                  hgInRange32(factor, 0, GR_BLENDFACTOR_MAX - 1) != 0);
    return (-13 >> factor) & 1;
}

int oglIsValidDstFactor(int factor)
{
    OGL_ASSERT_AT("oglApi.inl", 1665,
                  "hgInRange32((int)factor, 0, GR_BLENDFACTOR_MAX-1)",
                  hgInRange32(factor, 0, GR_BLENDFACTOR_MAX - 1) != 0);
    return (-1793 >> factor) & 1;
}

GL_API void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int srcF = oglMapBlendFactor(sfactor);
        int dstF = oglMapBlendFactor(dfactor);
        if (srcF != GR_BLENDFACTOR_MAX &&
            dstF != GR_BLENDFACTOR_MAX &&
            oglIsValidSrcFactor(srcF) != 0 &&
            oglIsValidDstFactor(dstF) != 0)
        {
            unsigned int uSrc = sfactor, uDst = dfactor;
            grSetBlendFunc(context->gerbera, srcF, dstF);
            oglSetGetterValueEnum(context, 0, GL_BLEND_SRC, &uSrc);
            oglSetGetterValueEnum(context, 0, GL_BLEND_DST, &uDst);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glLogicOp(GLenum opcode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int op = oglMapLogicOp(opcode);
        if (op == GR_LOGICOP_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else
        {
            unsigned int uOp = opcode;
            grSetLogicOp(context->gerbera, op);
            oglSetGetterValueEnum(context, 0, GL_LOGIC_OP_MODE, &uOp);
        }
    }
}

GL_API void GL_APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        unsigned int mask = (red != 0)
                          | ((green != 0) ? 2u : 0u)
                          | ((blue != 0)  ? 4u : 0u)
                          | ((alpha != 0) ? 8u : 0u);
        int bArr[4];
        grSetColorMask(context->gerbera, mask);
        bArr[0] = (red != 0);
        bArr[1] = (green != 0);
        bArr[2] = (blue != 0);
        bArr[3] = (alpha != 0);
        oglSetGetterValueBool(context, 0, GL_COLOR_WRITEMASK, bArr);
    }
}

GL_API void GL_APIENTRY glDepthMask(GLboolean flag)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int m = (flag != 0);
        grSetDepthMask(context->gerbera, m);
        oglSetGetterValueBool(context, 0, GL_DEPTH_WRITEMASK, &m);
    }
}

GL_API void GL_APIENTRY glStencilMask(GLuint mask)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int m = (int)mask;
        grSetStencilMask(context->gerbera, mask);
        oglSetGetterValueInt(context, 0, GL_STENCIL_WRITEMASK, &m);
    }
}

/* --- Active Texture & Matrix Stack Operations ------------------------------ */

GL_API void GL_APIENTRY glActiveTexture(GLenum texture)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (hgInRange32((int)(texture - GL_TEXTURE0), 0, 1) != 0)
        {
            unsigned char activeTex = (unsigned char)(texture - GL_TEXTURE0);
            unsigned int uTex = texture;
            if (hgInRange32(context->matrixMode, 2, 3) != 0)
                context->matrixMode = (unsigned char)(activeTex + 2u);
            context->activeTexture = activeTex;
            oglSetGetterValueEnum(context, 0, GL_ACTIVE_TEXTURE, &uTex);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glMatrixMode(GLenum mode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int mm = oglMapMatrixMode(mode);
        if (mm == GR_MATRIXMODE_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else
        {
            unsigned int uMode = mode;
            if (mm == GR_MATRIXMODE_TEXTURE0)
                mm = context->activeTexture + 2;
            if (mm == GR_MATRIXMODE_PALETTE0)
                mm = context->activePalMatrix + 4;
            context->matrixMode = (unsigned char)mm;
            oglSetGetterValueEnum(context, 0, GL_MATRIX_MODE, &uMode);
        }
    }
}

GL_API void GL_APIENTRY glPushMatrix(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        oglMatrixStack_s *stack = oglGetActiveMatrixStack(context);
        int ndx = stack->index;
        int depth = stack->depth;
        OGL_ASSERT_AT("oglApi.inl", 1817, "hgInRange32(ndx, 0, depth-1)",
                      hgInRange32(ndx, 0, depth - 1) != 0);
        if (ndx < depth - 1)
        {
            IMatrix4x4_copy(&stack->elements[ndx + 1], &stack->elements[ndx]);
            stack->index = (unsigned char)(ndx + 1);
        }
        else
        {
            oglSetError(context, OGL_ERROR_STACK_OVERFLOW);
        }
    }
}

GL_API void GL_APIENTRY glPopMatrix(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        oglMatrixStack_s *stack = oglGetActiveMatrixStack(context);
        int ndx = stack->index;
        OGL_ASSERT_AT("oglApi.inl", 1834, "hgInRange32(ndx, 0, stack->depth-1)",
                      hgInRange32(ndx, 0, stack->depth - 1) != 0);
        if (ndx > 0)
        {
            stack->index = (unsigned char)(ndx - 1);
            stack->dirty = 1;
        }
        else
        {
            oglSetError(context, OGL_ERROR_STACK_UNDERFLOW);
        }
    }
}

GL_API void GL_APIENTRY glLoadIdentity(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        IMatrix4x4_identity(oglGetActiveMatrix(context, 1));
}

GL_API void GL_APIENTRY glLoadMatrixf(const GLfloat *m)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int bits[16];
        memcpy(bits, m, sizeof(bits));
        IMatrix4x4_setfv(oglGetActiveMatrix(context, 1), bits);
    }
}

GL_API void GL_APIENTRY glLoadMatrixx(const GLfixed *m)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        float fm[16];
        oglMatrixFixedToFloat(fm, m);
        glLoadMatrixf(fm);
    }
}

GL_API void GL_APIENTRY glMultMatrixf(const GLfloat *m)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _IMatrix4x4 *current = oglGetActiveMatrix(context, 1);
        _IMatrix4x4 rhs;
        int bits[16];
        memcpy(bits, m, sizeof(bits));
        IMatrix4x4_setfv(&rhs, bits);
        IMatrix4x4_mul(current, current, &rhs);
    }
}

GL_API void GL_APIENTRY glMultMatrixx(const GLfixed *m)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        float fm[16];
        oglMatrixFixedToFloat(fm, m);
        glMultMatrixf(fm);
    }
}

GL_API void GL_APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _IMatrix4x4 *mat = oglGetActiveMatrix(context, 1);
        int iAngle = IScalar_ftos(angle);
        int iAxis[3];
        _IFVector3 axis;
        iAxis[0] = IScalar_ftos(x);
        iAxis[1] = IScalar_ftos(y);
        iAxis[2] = IScalar_ftos(z);
        IFVector3_setfv3(&axis, iAxis);
        IMatrix4x4_rotate(mat, mat, iAngle, &axis);
    }
}

GL_API void GL_APIENTRY glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z)
{
    glRotatef(oglFixedToFloat(angle), oglFixedToFloat(x),
              oglFixedToFloat(y), oglFixedToFloat(z));
}

GL_API void GL_APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _IMatrix4x4 *mat = oglGetActiveMatrix(context, 1);
        int tBits[3];
        tBits[0] = (int)hgFloatBitPattern(x);
        tBits[1] = (int)hgFloatBitPattern(y);
        tBits[2] = (int)hgFloatBitPattern(z);
        IMatrix4x4_translate(mat, mat, tBits);
    }
}

GL_API void GL_APIENTRY glTranslatex(GLfixed x, GLfixed y, GLfixed z)
{
    glTranslatef(oglFixedToFloat(x), oglFixedToFloat(y), oglFixedToFloat(z));
}

GL_API void GL_APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _IMatrix4x4 *mat = oglGetActiveMatrix(context, 1);
        int iAxis[3];
        iAxis[0] = IScalar_ftos(x);
        iAxis[1] = IScalar_ftos(y);
        iAxis[2] = IScalar_ftos(z);
        IMatrix4x4_scale(mat, mat, iAxis);
    }
}

GL_API void GL_APIENTRY glScalex(GLfixed x, GLfixed y, GLfixed z)
{
    glScalef(oglFixedToFloat(x), oglFixedToFloat(y), oglFixedToFloat(z));
}

GL_API void GL_APIENTRY glFrustumf(GLfloat left, GLfloat right, GLfloat bottom,
                                   GLfloat top, GLfloat zNear, GLfloat zFar)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int planes[6];
        planes[0] = IScalar_ftos(left);
        planes[1] = IScalar_ftos(right);
        planes[2] = IScalar_ftos(bottom);
        planes[3] = IScalar_ftos(top);
        planes[4] = IScalar_ftos(zNear);
        planes[5] = IScalar_ftos(zFar);

        if (hgFloatBitPattern(left) == hgFloatBitPattern(right) ||
            hgFloatBitPattern(bottom) == hgFloatBitPattern(top) ||
            hgFloatBitPattern(zNear) == hgFloatBitPattern(zFar) ||
            (int)hgFloatBitPattern(zNear) <= 0 ||
            (int)hgFloatBitPattern(zFar) <= 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
        else
        {
            _IMatrix4x4 *mat = oglGetActiveMatrix(context, 1);
            IMatrix4x4_frustum(mat, mat, planes);
        }
    }
}

GL_API void GL_APIENTRY glFrustumx(GLfixed left, GLfixed right, GLfixed bottom,
                                   GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glFrustumf(oglFixedToFloat(left), oglFixedToFloat(right),
               oglFixedToFloat(bottom), oglFixedToFloat(top),
               oglFixedToFloat(zNear), oglFixedToFloat(zFar));
}

GL_API void GL_APIENTRY glOrthof(GLfloat left, GLfloat right, GLfloat bottom,
                                 GLfloat top, GLfloat zNear, GLfloat zFar)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _IMatrix4x4 *mat = oglGetActiveMatrix(context, 1);
        int planes[6];
        planes[0] = IScalar_iftos((int)hgFloatBitPattern(left));
        planes[1] = IScalar_iftos((int)hgFloatBitPattern(right));
        planes[2] = IScalar_iftos((int)hgFloatBitPattern(bottom));
        planes[3] = IScalar_iftos((int)hgFloatBitPattern(top));
        planes[4] = IScalar_iftos((int)hgFloatBitPattern(zNear));
        planes[5] = IScalar_iftos((int)hgFloatBitPattern(zFar));

        if (hgFloatBitPattern(left) == hgFloatBitPattern(right) ||
            hgFloatBitPattern(bottom) == hgFloatBitPattern(top) ||
            hgFloatBitPattern(zNear) == hgFloatBitPattern(zFar))
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
        else
        {
            IMatrix4x4_ortho(mat, mat, planes);
        }
    }
}

GL_API void GL_APIENTRY glOrthox(GLfixed left, GLfixed right, GLfixed bottom,
                                 GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glOrthof(oglFixedToFloat(left), oglFixedToFloat(right),
             oglFixedToFloat(bottom), oglFixedToFloat(top),
             oglFixedToFloat(zNear), oglFixedToFloat(zFar));
}

/* --- Current Vertex Attributes & Vertex Arrays ----------------------------- */

GL_API void GL_APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        unsigned short a16 = (unsigned short)hgClamp32_0_x(
            IScalar_scalarToFixed(IScalar_ftos(alpha), 16), 0xFFFF);
        _XColor3 xcol;
        float fArr[4];
        XColor3_setf(&xcol,
                     (int)hgFloatBitPattern(red),
                     (int)hgFloatBitPattern(green),
                     (int)hgFloatBitPattern(blue));
        grSetVertexColor(context->gerbera, &xcol, a16);
        fArr[0] = red;
        fArr[1] = green;
        fArr[2] = blue;
        fArr[3] = alpha;
        oglSetGetterValueFloat(context, 0, GL_CURRENT_COLOR, fArr);
    }
}

GL_API void GL_APIENTRY glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _XColor3 xcol;
        float fArr[4];
        XColor3_setx(&xcol, red, green, blue);
        grSetVertexColor(context->gerbera, &xcol,
                         (unsigned short)hgClamp32_0_x(alpha, 0xFFFF));
        fArr[0] = IScalar_stof(IScalar_fixedToScalar(red, 16));
        fArr[1] = IScalar_stof(IScalar_fixedToScalar(green, 16));
        fArr[2] = IScalar_stof(IScalar_fixedToScalar(blue, 16));
        fArr[3] = IScalar_stof(IScalar_fixedToScalar(alpha, 16));
        oglSetGetterValueFloat(context, 0, GL_CURRENT_COLOR, fArr);
    }
}

GL_API void GL_APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _XColor3 xcol;
        unsigned char bytes[4];
        float fArr[4];
        int i;

        XColor3_setub(&xcol, red, green, blue);
        grSetVertexColor(context->gerbera, &xcol,
                         (unsigned short)((unsigned int)alpha | ((unsigned int)alpha << 8)));
        bytes[0] = red;
        bytes[1] = green;
        bytes[2] = blue;
        bytes[3] = alpha;
        for (i = 3; i >= 0; --i)
            fArr[i] = IScalar_stof(IScalar_byteToScalar(bytes[i]));
        oglSetGetterValueFloat(context, 0, GL_CURRENT_COLOR, fArr);
    }
}

GL_API void GL_APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int xnorm[3];
        float fArr[3];
        XNormal3_setf(xnorm, nx, ny, nz);
        grSetVertexNormal(context->gerbera, xnorm);
        fArr[0] = nx;
        fArr[1] = ny;
        fArr[2] = nz;
        oglSetGetterValueFloat(context, 0, GL_CURRENT_NORMAL, fArr);
    }
}

GL_API void GL_APIENTRY glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz)
{
    glNormal3f(oglFixedToFloat(nx), oglFixedToFloat(ny), oglFixedToFloat(nz));
}

GL_API void GL_APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int texUnit = (int)(target - GL_TEXTURE0);
        if (hgInRange32(texUnit, 0, 1) != 0)
        {
            int sCoords[4];
            float fCoords[4];
            sCoords[0] = IScalar_ftos(s);
            sCoords[1] = IScalar_ftos(t);
            sCoords[2] = IScalar_ftos(r);
            sCoords[3] = IScalar_ftos(q);
            grSetVertexTexCoord(context->gerbera, texUnit, sCoords);
            fCoords[0] = s;
            fCoords[1] = t;
            fCoords[2] = r;
            fCoords[3] = q;
            oglSetGetterValueFloat(context, texUnit, GL_CURRENT_TEXTURE_COORDS, fCoords);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q)
{
    glMultiTexCoord4f(target, oglFixedToFloat(s), oglFixedToFloat(t),
                      oglFixedToFloat(r), oglFixedToFloat(q));
}

GL_API void GL_APIENTRY glEnableClientState(GLenum array)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglSetClientState(context, array, 1);
}

GL_API void GL_APIENTRY glDisableClientState(GLenum array)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglSetClientState(context, array, 0);
}

GL_API void GL_APIENTRY glClientActiveTexture(GLenum texture)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (hgInRange32((int)(texture - GL_TEXTURE0), 0, 1) != 0)
        {
            unsigned int uTex = texture;
            context->clientActiveTexture = (unsigned char)(texture - GL_TEXTURE0);
            oglSetGetterValueEnum(context, 0, GL_CLIENT_ACTIVE_TEXTURE, &uTex);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

void oglPointerInternal(oglContext_s *context, int size, int type, int stride,
                        const void *pointer, int arrayType)
{
    int restrictionIndex = hgMin32(arrayType, 6);
    int dataType;
    const oglArrayRestriction_s *restr;
    int realStride = stride;

    OGL_ASSERT_AT("oglApi.inl", 2360,
                  "hgInRange32((int)arrayType, 0, GR_VERTEXARRAY_MAX-1)",
                  hgInRange32(arrayType, 0, 7) != 0);

    dataType = oglMapDataType((unsigned int)type);
    restr = &restrictions[restrictionIndex];

    if (hgInRange32(arrayType, 6, 7) != 0)
    {
        OGL_ASSERT_AT("oglApi.inl", 2368,
                      "restrictionIndex == GR_VERTEXARRAY_TEXCOORD0",
                      restrictionIndex == GR_VERTEXARRAY_TEXCOORD0);
    }
    else
    {
        OGL_ASSERT_AT("oglApi.inl", 2370,
                      "restrictionIndex == (int)arrayType",
                      restrictionIndex == arrayType);
    }

    if (hgInRange32(size, restr->minSize, restr->maxSize) == 0)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }

    if (dataType < GR_DATATYPE_MAX && (restr->typeMask & (1u << dataType)) != 0)
    {
        const void *data;
        int binding;
        int offs;

        if (stride < 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        OGL_ASSERT_AT("oglApi.inl", 2376, "dataType != GR_DATATYPE_MAX",
                      dataType != GR_DATATYPE_MAX);

        if (stride == 0)
            realStride = size * grGetDataTypeSize(dataType);

        data = pointer;
        binding = (int)context->activeBufObjs[OGL_BUFOBJTARGET_ARRAY];
        if (binding != 0)
        {
            oglBufObj_s *bufObj = oglGetActiveBufObj(context, OGL_BUFOBJTARGET_ARRAY);
            OGL_ASSERT_AT("oglApi.inl", 2395, "bufObj", bufObj != NULL);
            data = (const char *)bufObj->buffer + (uintptr_t)pointer;
        }

        grSetVertexArray(context->gerbera, arrayType, dataType, size, realStride,
                         (void *)data, (unsigned int)binding, (void *)pointer);

        offs = 0;
        if (hgInRange32(arrayType, 6, 7) != 0)
        {
            offs = context->clientActiveTexture;
            OGL_ASSERT_AT("oglApi.inl", 2472,
                          "s_arrTypes [arrayType] == GL_TEXTURE_COORD_ARRAY_TYPE",
                          s_arrTypes[arrayType] == GL_TEXTURE_COORD_ARRAY_TYPE);
            OGL_ASSERT_AT("oglApi.inl", 2473,
                          "s_arrSizes [arrayType] == GL_TEXTURE_COORD_ARRAY_SIZE",
                          s_arrSizes[arrayType] == GL_TEXTURE_COORD_ARRAY_SIZE);
            OGL_ASSERT_AT("oglApi.inl", 2474,
                          "s_arrStrides [arrayType] == GL_TEXTURE_COORD_ARRAY_STRIDE",
                          s_arrStrides[arrayType] == GL_TEXTURE_COORD_ARRAY_STRIDE);
            OGL_ASSERT_AT("oglApi.inl", 2475,
                          "s_arrBindings [arrayType] == GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING",
                          s_arrBindings[arrayType] == GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING);
        }

        oglSetGetterValueInt(context, offs, s_arrTypes[arrayType], &type);
        oglSetGetterValueInt(context, offs, s_arrStrides[arrayType], &stride);
        oglSetGetterValueInt(context, offs, s_arrBindings[arrayType], &binding);
        if (arrayType != GR_VERTEXARRAY_NORMAL && arrayType != GR_VERTEXARRAY_POINT_SIZE)
            oglSetGetterValueInt(context, offs, s_arrSizes[arrayType], &size);
    }
    else
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, size, (int)type, stride, pointer,
                           context->clientActiveTexture + 6);
}

GL_API void GL_APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, size, (int)type, stride, pointer, GR_VERTEXARRAY_COLOR);
}

GL_API void GL_APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, 3, (int)type, stride, pointer, GR_VERTEXARRAY_NORMAL);
}

GL_API void GL_APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, size, (int)type, stride, pointer, GR_VERTEXARRAY_POSITION);
}

GL_API void GL_APIENTRY glWeightPointerOES(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, size, (int)type, stride, pointer, GR_VERTEXARRAY_WEIGHT);
}

GL_API void GL_APIENTRY glMatrixIndexPointerOES(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, size, (int)type, stride, pointer, GR_VERTEXARRAY_MATRIX_INDEX);
}

GL_API void GL_APIENTRY glPointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglPointerInternal(context, 1, (int)type, stride, pointer, GR_VERTEXARRAY_POINT_SIZE);
}

/* --- Materials, Shading, Lights & LightModel ------------------------------- */

GL_API void GL_APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (pname == GL_SHININESS)
            oglMaterialInternal(context, face, GL_SHININESS, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglMaterialInternal(context, face, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glMaterialx(GLenum face, GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (pname == GL_SHININESS)
            oglMaterialInternal(context, face, GL_SHININESS, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glMaterialxv(GLenum face, GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglMaterialInternal(context, face, pname, params, OGL_INPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glShadeModel(GLenum mode)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        unsigned int uMode = mode;
        if (mode == GL_FLAT)
        {
            grSetCapability(context->gerbera, GR_CAPABILITY_SMOOTH_SHADING, 0);
        }
        else if (mode == GL_SMOOTH)
        {
            grSetCapability(context->gerbera, GR_CAPABILITY_SMOOTH_SHADING, 1);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        oglSetGetterValueEnum(context, 0, GL_SHADE_MODEL, &uMode);
    }
}

GL_API void GL_APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarLightParameter(pname) != 0)
            oglLightInternal(context, light, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglLightInternal(context, light, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glLightx(GLenum light, GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarLightParameter(pname) != 0)
            oglLightInternal(context, light, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glLightxv(GLenum light, GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglLightInternal(context, light, pname, params, OGL_INPUTTYPE_FIXED);
}

void oglLightModelInternal(oglContext_s *context, unsigned int pname,
                           const void *params, OGL_INPUTTYPE inputType)
{
    Gerbera_s *gerbera = context->gerbera;
    int twoSide = (*(const int *)params != 0);

    switch (pname)
    {
    case GL_LIGHT_MODEL_TWO_SIDE:
        grSetCapability(gerbera, GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE, twoSide);
        oglSetGetterValueBool(context, 0, pname, &twoSide);
        break;

    case GL_LIGHT_MODEL_AMBIENT:
    {
        _XColor3 xcol;
        float fCol[4];
        oglInputToXColor3(&xcol, params, inputType);
        grSetGlobalAmbient(gerbera, &xcol);
        if (inputType != OGL_INPUTTYPE_FLOAT)
        {
            const int *xp = (const int *)params;
            OGL_ASSERT_AT("oglApi.inl", 2873, "inputType == OGL_INPUTTYPE_FIXED",
                          inputType == OGL_INPUTTYPE_FIXED);
            fCol[0] = IScalar_stof(IScalar_fixedToScalar(xp[0], 16));
            fCol[1] = IScalar_stof(IScalar_fixedToScalar(xp[1], 16));
            fCol[2] = IScalar_stof(IScalar_fixedToScalar(xp[2], 16));
            fCol[3] = IScalar_stof(IScalar_fixedToScalar(xp[3], 16));
        }
        else
        {
            const float *fp = (const float *)params;
            fCol[0] = fp[0];
            fCol[1] = fp[1];
            fCol[2] = fp[2];
            fCol[3] = fp[3];
        }
        oglSetGetterValueFloat(context, 0, GL_LIGHT_MODEL_AMBIENT, fCol);
        break;
    }

    default:
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        break;
    }
}

GL_API void GL_APIENTRY glLightModelf(GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarLightModelParameter(pname) != 0)
            oglLightModelInternal(context, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glLightModelfv(GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglLightModelInternal(context, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glLightModelx(GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarLightModelParameter(pname) != 0)
            oglLightModelInternal(context, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glLightModelxv(GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglLightModelInternal(context, pname, params, OGL_INPUTTYPE_FIXED);
}

/* --- Texture Environment & Parameters -------------------------------------- */

void oglTexEnvInternal(oglContext_s *context, unsigned int target,
                       unsigned int pname, const void *params,
                       OGL_INPUTTYPE inputType)
{
    OGL_ASSERT_AT("oglApi.inl", 2959, "context", context != NULL);
    if (params == NULL)
    {
        oglSetError(context, OGL_ERROR_INVALID_VALUE);
        return;
    }
    if (target != GL_TEXTURE_ENV && target != GL_POINT_SPRITE_OES)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (target != GL_TEXTURE_ENV)
    {
        if (pname == GL_COORD_REPLACE_OES)
        {
            unsigned int replace = oglInputToEnum(params, inputType);
            grSetTexCoordReplace(context->gerbera, context->activeTexture, replace != 0);
            return;
        }
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }

    switch (pname)
    {
    case GL_ALPHA_SCALE:
    case GL_RGB_SCALE:
    {
        int fixedScale;
        if (inputType == OGL_INPUTTYPE_INT)
            fixedScale = *(const int *)params << 16;
        else
            fixedScale = IScalar_scalarToFixed(oglInputToIScalarDirect(params, inputType), 16);

        if (fixedScale != 0x10000 && fixedScale != 0x20000 && fixedScale != 0x40000)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        fixedScale >>= 16;
        if (pname == GL_RGB_SCALE)
            grSetTexEnvRGBCombScale(context->gerbera, context->activeTexture, fixedScale);
        else
            grSetTexEnvAlphaCombScale(context->gerbera, context->activeTexture, fixedScale);
        return;
    }
    case GL_TEXTURE_ENV_MODE:
    {
        int m = oglMapTexEnvMode(oglInputToEnum(params, inputType));
        if (m != GR_TEXENVMODE_MAX)
        {
            grSetTexEnvMode(context->gerbera, context->activeTexture, m);
            return;
        }
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    case GL_TEXTURE_ENV_COLOR:
        grSetTexEnvColor(context->gerbera, context->activeTexture,
                         oglInputToRGBA(params, inputType));
        return;
    case GL_COMBINE_RGB:
    {
        int cf = oglMapTexEnvCombFunc(oglInputToEnum(params, inputType));
        if (cf != GR_TEXENVCOMBFUNC_MAX)
        {
            grSetTexEnvRGBCombFunc(context->gerbera, context->activeTexture, cf);
            return;
        }
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    case GL_COMBINE_ALPHA:
    {
        int cf = oglMapTexEnvCombFunc(oglInputToEnum(params, inputType));
        if (cf != GR_TEXENVCOMBFUNC_MAX &&
            cf != GR_TEXENVCOMBFUNC_DOT3_RGB &&
            cf != GR_TEXENVCOMBFUNC_DOT3_RGBA)
        {
            grSetTexEnvAlphaCombFunc(context->gerbera, context->activeTexture, cf);
            return;
        }
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    default:
        break;
    }

    if (pname - GL_SRC0_RGB <= 2u)
    {
        int cs = oglMapTexEnvCombSrc(oglInputToEnum(params, inputType));
        if (cs != GR_TEXENVCOMBSRC_MAX)
        {
            grSetTexEnvRGBCombSrc(context->gerbera, context->activeTexture,
                                  (int)(pname - GL_SRC0_RGB), cs);
            return;
        }
    }
    else if (pname - GL_SRC0_ALPHA <= 2u)
    {
        int cs = oglMapTexEnvCombSrc(oglInputToEnum(params, inputType));
        if (cs != GR_TEXENVCOMBSRC_MAX)
        {
            grSetTexEnvAlphaCombSrc(context->gerbera, context->activeTexture,
                                    (int)(pname - GL_SRC0_ALPHA), cs);
            return;
        }
    }
    else if (pname - GL_OPERAND0_RGB <= 2u)
    {
        int co = oglMapTexEnvCombOperand(oglInputToEnum(params, inputType));
        if (co != GR_TEXENVCOMBOPERAND_MAX)
        {
            grSetTexEnvRGBCombOperand(context->gerbera, context->activeTexture,
                                      (int)(pname - GL_OPERAND0_RGB), co);
            return;
        }
    }
    else if (pname - GL_OPERAND0_ALPHA <= 2u)
    {
        int co = oglMapTexEnvCombOperand(oglInputToEnum(params, inputType));
        if (co != GR_TEXENVCOMBOPERAND_MAX && co > GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_COLOR)
        {
            grSetTexEnvAlphaCombOperand(context->gerbera, context->activeTexture,
                                        (int)(pname - GL_OPERAND0_ALPHA), co);
            return;
        }
    }

    oglSetError(context, OGL_ERROR_INVALID_ENUM);
}

GL_API void GL_APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexEnvParam(pname) != 0)
            oglTexEnvInternal(context, target, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexEnvInternal(context, target, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glTexEnvx(GLenum target, GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexEnvParam(pname) != 0)
            oglTexEnvInternal(context, target, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexEnvInternal(context, target, pname, params, OGL_INPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexEnvParam(pname) != 0)
            oglTexEnvInternal(context, target, pname, &param, OGL_INPUTTYPE_INT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexEnvInternal(context, target, pname, params, OGL_INPUTTYPE_INT);
}

void oglTexParameterInternal(oglContext_s *context, unsigned int target,
                             unsigned int pname, const void *params,
                             OGL_INPUTTYPE inputType)
{
    int texTarget = oglMapTextureTarget(target);
    _grTexture *texture;
    unsigned int val;

    OGL_ASSERT_AT("oglApi.inl", 3181, "context", context != NULL);
    if (texTarget == GR_TEXTURETARGET_MAX)
    {
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
        return;
    }
    if (params == NULL)
        return;

    texture = oglGetActiveTexture(context, texTarget);
    val = oglInputToEnum(params, inputType);

    if (pname - GL_TEXTURE_MAG_FILTER <= 1u)
    {
        int filterType = 0;
        unsigned int filterMask;

        if (pname == GL_TEXTURE_MAG_FILTER)
        {
            if (val != GL_NEAREST && val != GL_LINEAR)
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
                return;
            }
            filterType = 1;
        }
        switch (val)
        {
        case GL_NEAREST:                filterMask = 0u; break;
        case GL_LINEAR:                 filterMask = 1u; break;
        case GL_NEAREST_MIPMAP_NEAREST: filterMask = 2u; break;
        case GL_LINEAR_MIPMAP_NEAREST:  filterMask = 3u; break;
        case GL_NEAREST_MIPMAP_LINEAR:  filterMask = 6u; break;
        case GL_LINEAR_MIPMAP_LINEAR:   filterMask = 7u; break;
        default:
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        grSetTextureFilter(texture, filterType, filterMask);
    }
    else
    {
        switch (pname)
        {
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        {
            int wrap = oglMapWrapMode(val);
            int axis = (int)(pname - GL_TEXTURE_WRAP_S);
            if (wrap == GR_WRAPMODE_MAX)
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
                return;
            }
            OGL_ASSERT_AT("oglApi.inl", 3227, "hgInRange32(axis, 0, 1)",
                          hgInRange32(axis, 0, 1) != 0);
            grSetTextureWrapMode(texture, axis, wrap);
            break;
        }
        case GL_GENERATE_MIPMAP:
            if (val >= 2u)
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
                return;
            }
            grSetTextureGenMipmaps(texture, (int)val);
            break;

        case GL_TEXTURE_CROP_RECT_OES:
        {
            int crop[4];
            _grRect rect;
            int i;
            OGL_ASSERT_AT("oglApi.inl", 3252,
                          "inputType == OGL_INPUTTYPE_FLOAT || inputType == OGL_INPUTTYPE_FIXED || inputType == OGL_INPUTTYPE_INT",
                          inputType == OGL_INPUTTYPE_FLOAT ||
                          inputType == OGL_INPUTTYPE_FIXED ||
                          inputType == OGL_INPUTTYPE_INT);
            for (i = 3; i >= 0; --i)
                crop[i] = oglInputToInteger((const char *)params + 4 * i, inputType);
            grSetRectangle(&rect, crop[0], crop[2] + crop[0], crop[1], crop[3] + crop[1]);
            grSetTextureCropRect(texture, &rect);
            break;
        }
        default:
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
    }
}

GL_API void GL_APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexParam(pname) != 0)
            oglTexParameterInternal(context, target, pname, &param, OGL_INPUTTYPE_FLOAT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexParameterx(GLenum target, GLenum pname, GLfixed param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexParam(pname) != 0)
            oglTexParameterInternal(context, target, pname, &param, OGL_INPUTTYPE_FIXED);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (oglIsScalarTexParam(pname) != 0)
            oglTexParameterInternal(context, target, pname, &param, OGL_INPUTTYPE_INT);
        else
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
    }
}

GL_API void GL_APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexParameterInternal(context, target, pname, params, OGL_INPUTTYPE_FLOAT);
}

GL_API void GL_APIENTRY glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexParameterInternal(context, target, pname, params, OGL_INPUTTYPE_FIXED);
}

GL_API void GL_APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        oglTexParameterInternal(context, target, pname, params, OGL_INPUTTYPE_INT);
}

/* --- Texture Upload, Copy & Management ------------------------------------- */

GL_API void GL_APIENTRY glTexSubImage2D(GLenum target, GLint level,
                                        GLint xoffset, GLint yoffset,
                                        GLsizei width, GLsizei height,
                                        GLenum format, GLenum type,
                                        const GLvoid *pixels)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        _grTexture *texture;
        int log2Dim[2];

        if (target != GL_TEXTURE_2D || oglIsValidTextureType(type) == 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        if (hgInRange32(level, 0, 10) == 0 || width < 0 || height < 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
        if (grTextureMipLevelInitialized(texture, level) == 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_OPERATION);
            return;
        }
        if (grGetTextureMipLevel(texture, level, log2Dim) != NULL)
        {
            int mipW = 1 << log2Dim[0];
            int mipH = 1 << log2Dim[1];
            int mipFmt;
            int unpackAlignment;
            grBlitImage_s srcImg;
            grBlitImage_s dstImg;
            grBlitInfo_s blitInfo;

            if (xoffset < 0 || width + xoffset > mipW ||
                yoffset < 0 || height + yoffset > mipH)
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
                return;
            }
            mipFmt = grGetTextureMipLevelFormat(texture, level);
            if (oglIsCompatibleTextureType(format, type) == 0 ||
                oglIsCompatibleInternalFormat(format, mipFmt) == 0 ||
                oglIsPalettedTextureFormat(mipFmt) != 0)
            {
                oglSetError(context, OGL_ERROR_INVALID_OPERATION);
                return;
            }

            memset(&srcImg, 0, sizeof(srcImg));
            memset(&dstImg, 0, sizeof(dstImg));
            srcImg.type = GR_BLITIMAGE_MEMORY_IMAGE;
            dstImg.type = GR_BLITIMAGE_TEXTURE;
            dstImg.texture.texture = texture;
            dstImg.texture.mipLevel = level;

            srcImg.memoryImage.width = width;
            srcImg.memoryImage.height = height;
            srcImg.memoryImage.data = (void *)pixels;
            oglFillMemoryImageFormat(&srcImg.memoryImage, format, type);

            unpackAlignment = context->pixelStoreUnpackAlignment;
            OGL_ASSERT_AT("oglApi.inl", 3409,
                          "hgInRange32(unpackAlignment, 1, 8) && hgIsPowerOfTwo32(unpackAlignment)",
                          hgInRange32(unpackAlignment, 1, 8) != 0 &&
                          hgIsPowerOfTwo32((unsigned int)unpackAlignment) != 0);
            srcImg.memoryImage.stride = (srcImg.memoryImage.bytesPerPixel * width + unpackAlignment - 1)
                                      & ~(unpackAlignment - 1);

            blitInfo.srcImage = &srcImg;
            blitInfo.dstImage = &dstImg;
            grSetRectangle(&blitInfo.srcRect, 0, width, 0, height);
            grSetRectangle(&blitInfo.dstRect, xoffset, width + xoffset, yoffset, height + yoffset);
            grrBlit(context->gerbera, &blitInfo);

            if (level == 0 &&
                grGetTextureGenMipmaps(texture) != 0 &&
                grrCalcTextureMipmaps(context->gerbera, texture) == 0)
            {
                oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
            }
        }
    }
}

GL_API void GL_APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLenum format, GLenum type, const GLvoid *pixels)
{
    int texFormat = oglMapInternalTextureFormatAndType((unsigned int)internalformat, type);
    oglContext_s *context = oglGetContext();

    if (context != NULL)
    {
        if (target == GL_TEXTURE_2D && oglIsValidTextureType(type) != 0)
        {
            if (border == 0 &&
                texFormat != GR_TEXTUREFORMAT_MAX &&
                hgInRange32(level, 0, 10) != 0 &&
                hgInRange32(width, 0, 1024 >> level) != 0 &&
                hgInRange32(height, 0, 1024 >> level) != 0 &&
                hgIsPowerOfTwo32((unsigned int)width) != 0 &&
                hgIsPowerOfTwo32((unsigned int)height) != 0)
            {
                if (oglIsCompatibleTextureType(format, type) != 0 &&
                    oglIsCompatibleInternalFormat(format, texFormat) != 0)
                {
                    _grTexture *texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
                    if (grrSetupTextureLevel(texture, level, width, height, texFormat) != 0)
                    {
                        if (pixels != NULL)
                            glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, format, type, pixels);
                    }
                    else
                    {
                        oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
                    }
                }
                else
                {
                    oglSetError(context, OGL_ERROR_INVALID_OPERATION);
                }
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset,
                                        GLsizeiptr size, const GLvoid *data)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        OGL_BUFOBJTARGET bt = oglMapBufObjTarget(target);
        oglBufObj_s *bufObj;
        if (bt == OGL_BUFOBJTARGET_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else if (offset >= 0 && size >= 0 &&
                 offset <= (GLintptr)(bufObj = oglGetActiveBufObj(context, bt))->size &&
                 size <= (GLsizeiptr)((GLintptr)bufObj->size - offset))
        {
            if (context->activeBufObjs[bt] != 0)
            {
                if (data != NULL)
                    hgMemCopy((char *)bufObj->buffer + offset, data, (unsigned int)size);
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_OPERATION);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size,
                                     const GLvoid *data, GLenum usage)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        OGL_BUFOBJTARGET bt = oglMapBufObjTarget(target);
        OGL_BUFOBJUSAGE bu = oglMapBufObjUsage(usage);

        if (bt == OGL_BUFOBJTARGET_MAX || bu == OGL_BUFOBJUSAGE_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else if (size >= 0)
        {
            if (context->activeBufObjs[bt] != 0)
            {
                oglBufObj_s *bufObj = oglGetActiveBufObj(context, bt);
                OGL_ASSERT_AT("oglApi.inl", 3564, "bufObj", bufObj != NULL);
                if (oglSetupBufObj(bufObj, bu, (int)size) != 0)
                {
                    oglContext_s *shared = context;
                    unsigned int activeName = context->activeBufObjs[bt];

                    /* Vertex arrays retain the object binding independently of
                     * its current target. Replace cached addresses in every
                     * sharing context before any of them draws again. */
                    while (shared->shareListPrev != NULL)
                        shared = shared->shareListPrev;
                    for (; shared != NULL; shared = shared->shareListNext)
                    {
                        int i;
                        for (i = 0; i < GR_VERTEXARRAY_MAX; ++i)
                        {
                            _grVertexArray *arr = &shared->gerbera->vertexArray[i];
                            if (arr->binding == activeName)
                                arr->data = bufObj->buffer != NULL
                                          ? (char *)bufObj->buffer + (uintptr_t)arr->origPointer
                                          : NULL;
                        }
                    }
                    glBufferSubData(target, 0, size, data);
                    if (data == NULL && bt == OGL_BUFOBJTARGET_ELEMENT_ARRAY)
                        hgMemSet(bufObj->buffer, 0, (unsigned int)size);
                }
                else
                {
                    oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
                }
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_OPERATION);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

int oglGetPalettedFormatDescriptor(_grSurfaceDescriptor *desc, unsigned int internalFormat)
{
    int ndx;
    const oglPalettedFormatInfo_s *info;
    unsigned char colorMask;

    OGL_ASSERT_AT("oglApi.inl", 3650, "desc", desc != NULL);
    ndx = oglBinarySearch(internalFormat, 10, s_palettedFormats);
    if (ndx == -1)
        return GR_TEXTUREFORMAT_MAX;

    info = &s_formatInfos[ndx];
    colorMask = (unsigned char)((info->rSize != 0) |
                                ((info->gSize != 0) ? 2u : 0u) |
                                ((info->bSize != 0) ? 4u : 0u) |
                                ((info->aSize != 0) ? 8u : 0u));
    grClearDescriptor(desc);
    grSetDescriptorSizes(desc, info->rSize, info->gSize, info->bSize, info->aSize);
    grSetDescriptorOffsets(desc, info->rOffset, info->gOffset, info->bOffset, info->aOffset);
    desc->colorBytes = info->colorBytes;
    desc->paletteFormat = info->paletteFormat;
    desc->pixelFormat = info->pixelFormat;
    desc->colorMask = colorMask;
    return (int)info->texFormat;
}

GL_API void GL_APIENTRY glCompressedTexImage2D(GLenum target, GLint level,
                                               GLenum internalformat,
                                               GLsizei width, GLsizei height,
                                               GLint border, GLsizei imageSize,
                                               const GLvoid *data)
{
    unsigned int magic0 = 0xDEADBEEFu;
    unsigned int magic1 = 0xC001BABEu;
    oglContext_s *context = oglGetContext();
    (void)imageSize;

    if (context != NULL)
    {
        _grSurfaceDescriptor desc;
        int texFormat = oglGetPalettedFormatDescriptor(&desc, internalformat);
        int numLevels = hgAbs32(level) + 1;

        if (target == GL_TEXTURE_2D && texFormat != GR_TEXTUREFORMAT_MAX)
        {
            if (border == 0 &&
                level <= 0 &&
                numLevels <= 11 &&
                hgIsPowerOfTwo32((unsigned int)width) != 0 &&
                hgIsPowerOfTwo32((unsigned int)height) != 0 &&
                hgInRange32(width, 0, 1024) != 0 &&
                hgInRange32(height, 0, 1024) != 0 &&
                data != NULL)
            {
                _grTexture *texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
                if (grrSetPalettedTexture(texture, &desc, texFormat,
                                          width, height, numLevels, (void *)data) != 0)
                {
                    OGL_ASSERT_AT("oglApi.inl", 3722,
                                  "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                                  magic0 == 0xDEADBEEFu && magic1 == 0xC001BABEu);
                }
                else
                {
                    oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
                }
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level,
                                                  GLint xoffset, GLint yoffset,
                                                  GLsizei width, GLsizei height,
                                                  GLenum format, GLsizei imageSize,
                                                  const GLvoid *data)
{
    oglContext_s *context = oglGetContext();
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)width;
    (void)height;
    (void)format;
    (void)imageSize;
    (void)data;
    if (context != NULL)
        oglSetError(context, OGL_ERROR_INVALID_ENUM);
}

GL_API void GL_APIENTRY glCopyTexSubImage2D(GLenum target, GLint level,
                                            GLint xoffset, GLint yoffset,
                                            GLint x, GLint y,
                                            GLsizei width, GLsizei height)
{
    unsigned int magic0 = 0xDEADBEEFu;
    unsigned int magic1 = 0xC001BABEu;
    oglContext_s *context = oglGetContext();

    if (context != NULL)
    {
        _grTexture *texture;
        int log2Dim[2];

        if (target != GL_TEXTURE_2D)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        if (hgInRange32(level, 0, 10) == 0 ||
            hgInRange32(width, 1, 1024 >> level) == 0 ||
            hgInRange32(height, 1, 1024 >> level) == 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
        if (grTextureMipLevelInitialized(texture, level) == 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_OPERATION);
            return;
        }
        if (grGetTextureMipLevel(texture, level, log2Dim) != NULL)
        {
            int mipFmt = grGetTextureMipLevelFormat(texture, level);
            int mipW, mipH;
            grBlitImage_s srcImg;
            grBlitImage_s dstImg;
            grBlitInfo_s blitInfo;

            if (oglIsPalettedTextureFormat(mipFmt) != 0)
            {
                oglSetError(context, OGL_ERROR_INVALID_OPERATION);
                return;
            }
            mipW = 1 << log2Dim[0];
            mipH = 1 << log2Dim[1];
            if (xoffset < 0 || width + xoffset > mipW ||
                yoffset < 0 || height + yoffset > mipH)
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
                return;
            }

            memset(&srcImg, 0, sizeof(srcImg));
            memset(&dstImg, 0, sizeof(dstImg));
            srcImg.type = GR_BLITIMAGE_READ_SURFACE;
            dstImg.type = GR_BLITIMAGE_TEXTURE;
            dstImg.texture.texture = texture;
            dstImg.texture.mipLevel = level;

            blitInfo.srcImage = &srcImg;
            blitInfo.dstImage = &dstImg;
            grSetRectangle(&blitInfo.srcRect, x, width + x, y, height + y);
            grSetRectangle(&blitInfo.dstRect, xoffset, width + xoffset, yoffset, height + yoffset);
            grrBlit(context->gerbera, &blitInfo);

            OGL_ASSERT_AT("oglApi.inl", 3823,
                          "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                          magic0 == 0xDEADBEEFu && magic1 == 0xC001BABEu);
        }
    }
}

GL_API void GL_APIENTRY glCopyTexImage2D(GLenum target, GLint level,
                                         GLenum internalformat,
                                         GLint x, GLint y,
                                         GLsizei width, GLsizei height,
                                         GLint border)
{
    unsigned int magic0 = 0xDEADBEEFu;
    unsigned int magic1 = 0xC001BABEu;
    int texFormat = oglMapInternalTextureFormat(internalformat);
    oglContext_s *context = oglGetContext();

    if (context != NULL)
    {
        if (target == GL_TEXTURE_2D && texFormat != GR_TEXTUREFORMAT_MAX)
        {
            if (border == 0 &&
                hgInRange32(level, 0, 10) != 0 &&
                hgInRange32(width, 0, 1024 >> level) != 0 &&
                hgInRange32(height, 0, 1024 >> level) != 0 &&
                hgIsPowerOfTwo32((unsigned int)width) != 0 &&
                hgIsPowerOfTwo32((unsigned int)height) != 0)
            {
                _grSurfaceDescriptor drawDesc;
                grGetDrawBufferDescriptor(context->gerbera, &drawDesc);
                if (drawDesc.rgbaSizes[3] != 0 ||
                    (texFormat != GR_TEXTUREFORMAT_ALPHA8 &&
                     texFormat != GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8 &&
                     texFormat != GR_TEXTUREFORMAT_RGBA8))
                {
                    _grTexture *texture = oglGetActiveTexture(context, GR_TEXTURETARGET_2D);
                    grrSetupTextureLevel(texture, level, width, height, texFormat);
                    glCopyTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, x, y, width, height);
                    OGL_ASSERT_AT("oglApi.inl", 3881,
                                  "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                                  magic0 == 0xDEADBEEFu && magic1 == 0xC001BABEu);
                }
                else
                {
                    oglSetError(context, OGL_ERROR_INVALID_OPERATION);
                }
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

void oglUpdateShareListTextures(oglContext_s *context)
{
    int texUnit;

    OGL_ASSERT_AT("oglApi.inl", 3896, "context", context != NULL);
    while (context->shareListPrev != NULL)
        context = context->shareListPrev;
    while (context != NULL)
    {
        for (texUnit = 1; texUnit >= 0; --texUnit)
            oglUpdateTextureUnit(context, texUnit);
        context = context->shareListNext;
    }
}

void oglShareListRemoveTexture(oglContext_s *context, unsigned int texIndex)
{
    int texUnit;
    int texTarget;

    OGL_ASSERT_AT("oglApi.inl", 3920, "context", context != NULL);
    while (context->shareListPrev != NULL)
        context = context->shareListPrev;
    while (context != NULL)
    {
        for (texUnit = 1; texUnit >= 0; --texUnit)
        {
            int changed = 0;
            for (texTarget = GR_TEXTURETARGET_2D; texTarget < GR_TEXTURETARGET_MAX; ++texTarget)
            {
                if (texIndex == context->activeTextures[texTarget][texUnit])
                {
                    oglBindTexture(context, texUnit, texTarget, 0);
                    changed = 1;
                }
            }
            if (changed != 0)
                oglUpdateTextureUnit(context, texUnit);
        }
        context = context->shareListNext;
    }
}

GL_API void GL_APIENTRY glGenTextures(GLsizei n, GLuint *textures)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (n >= 0)
        {
            if (textures != NULL)
            {
                unsigned char success = 1;
                int i;
                oglContext_s *shareContext;

                for (i = 0; i < n; ++i)
                {
                    oglTextureManager *texMgr = context->textureManager;
                    unsigned int texIndex = oglGenerateTextureName(texMgr);
                    success &= (unsigned char)oglAllocateTexture(texMgr, texIndex);
                    if (success == 0)
                    {
                        n = i;
                        break;
                    }
                    textures[i] = texIndex;
                }
                if (success == 0)
                {
                    for (i = 0; i < n; ++i)
                    {
                        oglDeleteTexture(context->textureManager, textures[i]);
                        textures[i] = 0;
                    }
                }
                oglUpdateShareListTextures(context);
                for (shareContext = context;
                     shareContext->shareListPrev != NULL;
                     shareContext = shareContext->shareListPrev)
                {
                }
                while (shareContext != NULL)
                {
                    int texUnit;
                    for (texUnit = 0; texUnit < 2; ++texUnit)
                        oglUpdateTextureUnit(context, texUnit);
                    shareContext = shareContext->shareListNext;
                }
                if (success == 0)
                    oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (n >= 0)
        {
            if (textures != NULL)
            {
                int i;
                for (i = 0; i < n; ++i)
                {
                    unsigned int texIndex = textures[i];
                    if (texIndex != 0 && oglIsTexture(context->textureManager, texIndex) != 0)
                    {
                        int texUnit, texTarget;
                        oglShareListRemoveTexture(context, texIndex);
                        for (texUnit = 0; texUnit < 2; ++texUnit)
                        {
                            int changed = 0;
                            for (texTarget = GR_TEXTURETARGET_2D; texTarget < GR_TEXTURETARGET_MAX; ++texTarget)
                            {
                                if (texIndex == context->activeTextures[texTarget][texUnit])
                                {
                                    oglBindTexture(context, texUnit, texTarget, 0);
                                    changed = 1;
                                }
                            }
                            if (changed != 0)
                                oglUpdateTextureUnit(context, texUnit);
                        }
                        oglDeleteTexture(context->textureManager, texIndex);
                    }
                }
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glBindTexture(GLenum target, GLuint texture)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        oglTextureManager *texMgr = context->textureManager;
        int texTarget = oglMapTextureTarget(target);
        if (texTarget == GR_TEXTURETARGET_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else if (oglIsTexture(texMgr, texture) != 0 ||
                 oglAllocateTexture(texMgr, texture) != 0)
        {
            oglBindTexture(context, context->activeTexture, texTarget, texture);
        }
        else
        {
            oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
        }
    }
}

/* --- Buffer Object Management ---------------------------------------------- */

void oglShareListRemoveBufObj(oglContext_s *context, unsigned int bufObjIndex)
{
    OGL_BUFOBJTARGET bt;

    OGL_ASSERT_AT("oglApi.inl", 4104, "context", context != NULL);
    while (context->shareListPrev != NULL)
        context = context->shareListPrev;
    while (context != NULL)
    {
        int arrayType;
        for (bt = OGL_BUFOBJTARGET_ARRAY; bt < OGL_BUFOBJTARGET_MAX; bt = (OGL_BUFOBJTARGET)(bt + 1))
        {
            if (bufObjIndex == context->activeBufObjs[bt])
                oglBindBuffer(context, bt, 0);
        }
        for (arrayType = 0; arrayType < GR_VERTEXARRAY_MAX; ++arrayType)
        {
            _grVertexArray *array = &context->gerbera->vertexArray[arrayType];
            if (array->binding == bufObjIndex)
            {
                grResetVertexArray(context->gerbera, arrayType);
                /* Detach the private storage reference without changing the
                 * original driver's saved pointer and binding query values. */
                array->binding = 0;
            }
        }
        context = context->shareListNext;
    }
}

GL_API void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (n >= 0)
        {
            if (buffers != NULL)
            {
                unsigned char success = 1;
                int i;
                for (i = 0; i < n; ++i)
                {
                    oglBufObjManager_s *mgr = context->bufObjManager;
                    unsigned int idx = oglGenerateBufObjName(mgr);
                    success &= (unsigned char)oglAllocateBufObj(mgr, idx);
                    if (success == 0)
                    {
                        n = i;
                        break;
                    }
                    buffers[i] = idx;
                }
                if (success == 0)
                {
                    for (i = 0; i < n; ++i)
                    {
                        oglDeleteBufObj(context->bufObjManager, buffers[i]);
                        buffers[i] = 0;
                    }
                    oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
                }
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (n >= 0)
        {
            if (buffers != NULL)
            {
                int i;
                for (i = 0; i < n; ++i)
                {
                    unsigned int idx = buffers[i];
                    if (idx != 0 && oglIsBufObj(context->bufObjManager, idx) != 0)
                    {
                        oglShareListRemoveBufObj(context, idx);
                        oglDeleteBufObj(context->bufObjManager, idx);
                    }
                }
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        oglBufObjManager_s *mgr = context->bufObjManager;
        OGL_BUFOBJTARGET bt = oglMapBufObjTarget(target);
        if (bt == OGL_BUFOBJTARGET_MAX)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
        else if (oglIsBufObj(mgr, buffer) != 0 ||
                 oglAllocateBufObj(mgr, buffer) != 0)
        {
            oglBindBuffer(context, bt, buffer);
        }
        else
        {
            oglSetError(context, OGL_ERROR_OUT_OF_MEMORY);
        }
    }
}

/* --- Clip Planes, Flush/Finish, PixelStore, ReadPixels & OES --------------- */

GL_API void GL_APIENTRY glClipPlanef(GLenum plane, const GLfloat *equation)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (hgInRange32((int)(plane - 12278u), 10, 15) != 0)
        {
            if (equation != NULL)
            {
                _IFVector4 planeVec;
                int eqBits[4];
                _IMatrix4x4 *mv;
                memcpy(eqBits, equation, sizeof(eqBits));
                IFVector4_setfv(&planeVec, eqBits, 4);
                mv = oglGetMatrix(context, GR_MATRIXMODE_MODELVIEW);
                grSetUserClipPlane(context->gerbera, (int)(plane - 12278u), &planeVec, mv);
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glClipPlanex(GLenum plane, const GLfixed *equation)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (equation != NULL)
        {
            float eq[4];
            eq[0] = oglFixedToFloat(equation[0]);
            eq[1] = oglFixedToFloat(equation[1]);
            eq[2] = oglFixedToFloat(equation[2]);
            eq[3] = oglFixedToFloat(equation[3]);
            glClipPlanef(plane, eq);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glFlush(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        grFlushBufferClears(context->gerbera);
}

GL_API void GL_APIENTRY glFinish(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
        grFlushBufferClears(context->gerbera);
}

GL_API void GL_APIENTRY glPixelStorei(GLenum pname, GLint param)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (pname == GL_PACK_ALIGNMENT || pname == GL_UNPACK_ALIGNMENT)
        {
            if (hgInRange32(param, 1, 8) != 0 && hgIsPowerOfTwo32((unsigned int)param) != 0)
            {
                if (pname == GL_PACK_ALIGNMENT)
                    context->pixelStorePackAlignment = (unsigned char)param;
                else
                    context->pixelStoreUnpackAlignment = (unsigned char)param;
                oglSetGetterValueInt(context, 0, pname, &param);
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_VALUE);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
        }
    }
}

GL_API void GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                     GLenum format, GLenum type, GLvoid *pixels)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (pixels != NULL && width >= 0 && height >= 0)
        {
            if (format == GL_RGBA && type == GL_UNSIGNED_BYTE)
            {
                int packAlignment = context->pixelStorePackAlignment;
                grBlitImage_s srcImg;
                grBlitImage_s dstImg;
                grBlitInfo_s blitInfo;

                OGL_ASSERT_AT("oglApi.inl", 4381,
                              "hgInRange32(packAlignment, 1, 8) && hgIsPowerOfTwo32(packAlignment)",
                              hgInRange32(packAlignment, 1, 8) != 0 &&
                              hgIsPowerOfTwo32((unsigned int)packAlignment) != 0);

                memset(&srcImg, 0, sizeof(srcImg));
                memset(&dstImg, 0, sizeof(dstImg));
                srcImg.type = GR_BLITIMAGE_READ_SURFACE;
                dstImg.type = GR_BLITIMAGE_MEMORY_IMAGE;

                dstImg.memoryImage.width = width;
                dstImg.memoryImage.height = height;
                dstImg.memoryImage.data = pixels;
                dstImg.memoryImage.bytesPerPixel = 4;
                dstImg.memoryImage.useDescriptor = 0;
                dstImg.memoryImage.dataType = GR_DATATYPE_UINT8;
                dstImg.memoryImage.imageFormat = GR_IMAGEFORMAT_RGBA;
                dstImg.memoryImage.stride = (4 * width + packAlignment - 1) & ~(packAlignment - 1);

                blitInfo.srcImage = &srcImg;
                blitInfo.dstImage = &dstImg;
                grSetRectangle(&blitInfo.srcRect, x, width + x, y, height + y);
                grSetRectangle(&blitInfo.dstRect, 0, width, 0, height);
                grrBlit(context->gerbera, &blitInfo);
            }
            else
            {
                oglSetError(context, OGL_ERROR_INVALID_ENUM);
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API GLbitfield GL_APIENTRY glQueryMatrixxOES(GLfixed mantissa[16], GLint exponent[16])
{
    oglContext_s *context = oglGetContext();
    _IMatrix4x4 *matrix;
    int i;

    if (context == NULL)
        return 0;
    matrix = oglGetActiveMatrix(context, 0);
    for (i = 0; i < 16; ++i)
    {
        int s = IMatrix4x4_getScalar(matrix, i >> 2, i & 3);
        mantissa[i] = IScalar_getOESMantissa(s);
        exponent[i] = IScalar_getOESExponent(s);
    }
    return 0;
}

GL_API void GL_APIENTRY glCurrentPaletteMatrixOES(GLuint matrixpaletteindex)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (hgInRange32((int)matrixpaletteindex, 0, 8) != 0)
        {
            context->activePalMatrix = (unsigned char)matrixpaletteindex;
            if (hgInRange32(context->matrixMode, 4, 12) != 0)
                context->matrixMode = (unsigned char)(matrixpaletteindex + 4u);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glLoadPaletteFromModelViewMatrixOES(void)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        int palMode = context->activePalMatrix + 4;
        _IMatrix4x4 *mvMatrix = oglGetMatrix(context, GR_MATRIXMODE_MODELVIEW);
        _IMatrix4x4 *palMatrix = oglGetMatrix(context, palMode);
        IMatrix4x4_copy(palMatrix, mvMatrix);
        context->matrixStack[palMode]->dirty = 1;
    }
}
