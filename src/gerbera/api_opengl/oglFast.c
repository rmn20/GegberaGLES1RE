/* oglFast.c - fast-path draw entry points and primitive setup
 * (port of oglFast.c, docs/libGLES_CM.dll.c:11801-12300).
 */
#include <stddef.h>
#include <stdint.h>

#include "GLES/gl.h"
#include "gerbera/api_opengl/oglContext.h"
#include "hg/hgFloat.h"
#include "hg/hgInt32.h"

static const unsigned char g_primitiveTypes[8] =
{
    GR_PRIMITIVETYPE_POINT,
    GR_PRIMITIVETYPE_LINE,
    GR_PRIMITIVETYPE_LINE,
    GR_PRIMITIVETYPE_LINE,
    GR_PRIMITIVETYPE_TRIANGLE,
    GR_PRIMITIVETYPE_TRIANGLE,
    GR_PRIMITIVETYPE_TRIANGLE,
    GR_PRIMITIVETYPE_TEXTURE_RECT
};

int oglGetPrimitiveType(int primitive)
{
    OGL_ASSERT_AT("oglFast.c", 67,
                  "hgInRange32((int)primitive, 0, HG_LENGTH_OF_ARRAY(g_primitiveTypes)-1)",
                  hgInRange32(primitive, 0, 7) != 0);
    return (int)g_primitiveTypes[primitive];
}

void oglUpdateCoreMatrices(oglContext_s *context)
{
    int i;

    for (i = GR_MATRIXMODE_MODELVIEW; i < GR_MATRIXMODE_MAX; ++i)
    {
        oglMatrixStack_s *stack = oglGetMatrixStack(context, i);
        if (stack->dirty != 0)
        {
            grSetMatrix(context->gerbera, i, &stack->elements[stack->index]);
            stack->dirty = 0;
        }
    }
}

int oglSetupRendering(oglContext_s *context, int primitive, int count)
{
    Gerbera_s *gerbera;
    int minVertices;
    int mn;
    int mx;

    if (primitive != GR_PRIMITIVE_POINTS)
    {
        if (hgInRange32(primitive, 1, 3) != 0)
        {
            minVertices = 2;
        }
        else if (hgInRange32(primitive, 4, 6) != 0)
        {
            minVertices = 3;
        }
        else
        {
            if (primitive != GR_PRIMITIVE_TEXTURE_RECT)
                imAssert("0", "oglFast.c", 120);
            minVertices = 0;
        }
    }
    else
    {
        minVertices = 1;
    }

    if (count < minVertices)
        return GR_COMMAND_STOP;

    oglUpdateCoreMatrices(context);
    gerbera = context->gerbera;
    mn = context->pointSizeMin;
    mx = context->pointSizeMax;
    if (mn > mx)
    {
        grSetPointSizeMin(gerbera, mx);
        grSetPointSizeMax(gerbera, mn);
    }
    else
    {
        grSetPointSizeMin(gerbera, mn);
        grSetPointSizeMax(gerbera, mx);
    }

    if (primitive != GR_PRIMITIVE_POINTS && primitive != GR_PRIMITIVE_TEXTURE_RECT)
        grSetRectGradients(gerbera, 0);
    else
        grSetRectGradients(gerbera, 1);

    return GR_COMMAND_RENDER;
}

int oglMapPrimitive(unsigned int mode)
{
    if (mode == 7u)
        return GR_PRIMITIVE_MAX;
    return (int)mode;
}

GL_API void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    eglState_s *tlsState = NULL;
    oglContext_s *context = oglGetContextAndState(&tlsState);

    if (context != NULL)
    {
        Gerbera_s *gerbera = context->gerbera;
        int primitive = oglMapPrimitive(mode);

        if (hgInRange32(primitive, 0, 7) != 0)
        {
            if (count >= 0)
            {
                if (oglSetupRendering(context, primitive, count) != GR_COMMAND_STOP)
                {
                    int primType = oglGetPrimitiveType(primitive);
                    if (grSetupCulling(gerbera, primType) != GR_COMMAND_STOP)
                    {
                        gerbera->surfaceTlsState = tlsState;
                        grSetupRendering(gerbera);
                        grRenderArrays(gerbera, primitive, count, first);
                        grCleanupRendering(gerbera);
                        gerbera->surfaceTlsState = NULL;
                    }
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

GL_API void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    eglState_s *tlsState = NULL;
    oglContext_s *context = oglGetContextAndState(&tlsState);

    if (context != NULL)
    {
        Gerbera_s *gerbera = context->gerbera;
        int primitive = oglMapPrimitive(mode);

        if (hgInRange32(primitive, 0, 7) == 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }
        if (count < 0)
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
            return;
        }
        if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT)
        {
            oglSetError(context, OGL_ERROR_INVALID_ENUM);
            return;
        }

        if (oglSetupRendering(context, primitive, count) != GR_COMMAND_STOP)
        {
            int primType = oglGetPrimitiveType(primitive);
            if (grSetupCulling(gerbera, primType) != GR_COMMAND_STOP)
            {
                void *ind = (void *)indices;
                if (context->activeBufObjs[OGL_BUFOBJTARGET_ELEMENT_ARRAY] != 0)
                {
                    oglBufObj_s *bufObj = oglGetActiveBufObj(context, OGL_BUFOBJTARGET_ELEMENT_ARRAY);
                    OGL_ASSERT_AT("oglFast.c", 270, "bufObj", bufObj != NULL);
                    OGL_ASSERT_AT("oglFast.c", 273, "bufObj->size >= 0", bufObj->size >= 0);
                    if ((uintptr_t)indices >= (uintptr_t)bufObj->size)
                        return;
                    ind = (char *)bufObj->buffer + (uintptr_t)indices;
                }
                gerbera->surfaceTlsState = tlsState;
                grSetupRendering(gerbera);
                grRenderElements(gerbera, primitive, count, oglMapDataType(type), ind);
                grCleanupRendering(gerbera);
                gerbera->surfaceTlsState = NULL;
            }
        }
    }
}

GL_API void GL_APIENTRY glDrawTexfOES(GLfloat xs, GLfloat ys, GLfloat zs, GLfloat ws, GLfloat hs)
{
    unsigned int magic0 = 0xDEADBEEFu;
    unsigned int magic1 = 0xC001D00Eu;
    eglState_s *tlsState = NULL;
    oglContext_s *context = oglGetContextAndState(&tlsState);

    if (context != NULL)
    {
        Gerbera_s *gerbera = context->gerbera;
        int sx = IScalar_ftos(xs);
        int sy = IScalar_ftos(ys);
        int sz = IScalar_clampUnit(IScalar_ftos(zs));
        int sw = IScalar_ftos(ws);
        int sh = IScalar_ftos(hs);

        if ((int)hgFloatBitPattern(ws) > 0 && (int)hgFloatBitPattern(hs) > 0)
        {
            if (IScalar_testeq(sw) == 0 && IScalar_testeq(sh) == 0)
            {
                if (oglSetupRendering(context, GR_PRIMITIVE_TEXTURE_RECT, 0) == GR_COMMAND_STOP ||
                    grSetupCulling(gerbera, oglGetPrimitiveType(GR_PRIMITIVE_TEXTURE_RECT)) == GR_COMMAND_STOP)
                {
                    OGL_ASSERT_AT("oglFast.c", 323,
                                  "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                                  magic0 == 0xDEADBEEFu && magic1 == 0xC001D00Eu);
                }
                else
                {
                    gerbera->surfaceTlsState = tlsState;
                    grSetupRendering(gerbera);
                    grRenderTextureRect(gerbera, sx, sy, sw, sh, sz);
                    grCleanupRendering(gerbera);
                    gerbera->surfaceTlsState = NULL;
                    OGL_ASSERT_AT("oglFast.c", 332,
                                  "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                                  magic0 == 0xDEADBEEFu && magic1 == 0xC001D00Eu);
                }
            }
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glDrawTexxOES(GLfixed xs, GLfixed ys, GLfixed zs, GLfixed ws, GLfixed hs)
{
    float fxs = IScalar_stof(IScalar_fixedToScalar(xs, 16));
    float fys = IScalar_stof(IScalar_fixedToScalar(ys, 16));
    float fzs = IScalar_stof(IScalar_fixedToScalar(zs, 16));
    float fws = IScalar_stof(IScalar_fixedToScalar(ws, 16));
    float fhs = IScalar_stof(IScalar_fixedToScalar(hs, 16));
    glDrawTexfOES(fxs, fys, fzs, fws, fhs);
}

GL_API void GL_APIENTRY glDrawTexiOES(GLint xs, GLint ys, GLint zs, GLint ws, GLint hs)
{
    float fxs = IScalar_stof(IScalar_intToScalar(xs));
    float fys = IScalar_stof(IScalar_intToScalar(ys));
    float fzs = IScalar_stof(IScalar_intToScalar(zs));
    float fws = IScalar_stof(IScalar_intToScalar(ws));
    float fhs = IScalar_stof(IScalar_intToScalar(hs));
    glDrawTexfOES(fxs, fys, fzs, fws, fhs);
}

GL_API void GL_APIENTRY glDrawTexsOES(GLshort xs, GLshort ys, GLshort zs, GLshort ws, GLshort hs)
{
    float fxs = IScalar_stof(IScalar_intToScalar(xs));
    float fys = IScalar_stof(IScalar_intToScalar(ys));
    float fzs = IScalar_stof(IScalar_intToScalar(zs));
    float fws = IScalar_stof(IScalar_intToScalar(ws));
    float fhs = IScalar_stof(IScalar_intToScalar(hs));
    glDrawTexfOES(fxs, fys, fzs, fws, fhs);
}

GL_API void GL_APIENTRY glDrawTexfvOES(const GLfloat *coords)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (coords != NULL)
            glDrawTexfOES(coords[0], coords[1], coords[2], coords[3], coords[4]);
        else
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
    }
}

GL_API void GL_APIENTRY glDrawTexxvOES(const GLfixed *coords)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (coords != NULL)
        {
            float fxs = IScalar_stof(IScalar_fixedToScalar(coords[0], 16));
            float fys = IScalar_stof(IScalar_fixedToScalar(coords[1], 16));
            float fzs = IScalar_stof(IScalar_fixedToScalar(coords[2], 16));
            float fws = IScalar_stof(IScalar_fixedToScalar(coords[3], 16));
            float fhs = IScalar_stof(IScalar_fixedToScalar(coords[4], 16));
            glDrawTexfOES(fxs, fys, fzs, fws, fhs);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glDrawTexivOES(const GLint *coords)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (coords != NULL)
        {
            float fxs = IScalar_stof(IScalar_intToScalar(coords[0]));
            float fys = IScalar_stof(IScalar_intToScalar(coords[1]));
            float fzs = IScalar_stof(IScalar_intToScalar(coords[2]));
            float fws = IScalar_stof(IScalar_intToScalar(coords[3]));
            float fhs = IScalar_stof(IScalar_intToScalar(coords[4]));
            glDrawTexfOES(fxs, fys, fzs, fws, fhs);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}

GL_API void GL_APIENTRY glDrawTexsvOES(const GLshort *coords)
{
    oglContext_s *context = oglGetContext();
    if (context != NULL)
    {
        if (coords != NULL)
        {
            float fxs = IScalar_stof(IScalar_intToScalar(coords[0]));
            float fys = IScalar_stof(IScalar_intToScalar(coords[1]));
            float fzs = IScalar_stof(IScalar_intToScalar(coords[2]));
            float fws = IScalar_stof(IScalar_intToScalar(coords[3]));
            float fhs = IScalar_stof(IScalar_intToScalar(coords[4]));
            glDrawTexfOES(fxs, fys, fzs, fws, fhs);
        }
        else
        {
            oglSetError(context, OGL_ERROR_INVALID_VALUE);
        }
    }
}
