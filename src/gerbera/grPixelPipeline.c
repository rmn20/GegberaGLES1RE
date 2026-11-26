/* grPixelPipeline.c - the pixel-pipe dispatch (port of grPixelPipeline.h).
 *
 * The compiled program (state->pixelPipelineCode, produced by the hc
 * shader compiler) consumes the pixel-pipe record after the backend scratch
 * space at renderStack+14644. In the original 32-bit layout, the row pointers
 * are at +168..+176 and scanline state at +180..+188, followed by the gradient
 * arrays set up by grSetupTriangleGradients.
 */
#include "gerbera/grState.h"
#include "gerbera/grDefs.h"
#include "gerbera/grRGBA.h"
#include "gerbera/grARMPixel.h"
#include "gerbera/hc/hcCompiler.h"
#include "hg/hgFill.h"
#include "hg/hgInt32.h"

#include <stdint.h>
#include <string.h>

/* g_mutableParamsMask (VA 0x4785B4, defined in grSmall.c): bitmask of mutable
 * pixel-pipe stack parameters among the 78 words (params 0..77). */
extern const unsigned char g_mutableParamsMask[12];

/* stack canary words kept by the decompile's locals (grSmall.c 86) */
#define GR_STACK_CHECK_MAGIC0 (-559038737)
#define GR_STACK_CHECK_MAGIC1 (-1073628482)

/* --- grExecutePixelPipeline (grPixelPipeline.h 153; DLL 24757-24775) --------- */

#if !defined(__SYMBIAN32__) && \
    !((defined(__arm__) || defined(__i386__) || defined(_M_IX86)) && UINTPTR_MAX == 0xFFFFFFFFu)
static int grEvalCompareFunc64(int func, unsigned int lhs, unsigned int rhs)
{
    switch (func)
    {
    case GR_FUNC_NEVER:    return 0;
    case GR_FUNC_LESS:     return lhs < rhs;
    case GR_FUNC_EQUAL:    return lhs == rhs;
    case GR_FUNC_LEQUAL:   return lhs <= rhs;
    case GR_FUNC_GREATER:  return lhs > rhs;
    case GR_FUNC_NOTEQUAL: return lhs != rhs;
    case GR_FUNC_GEQUAL:   return lhs >= rhs;
    case GR_FUNC_ALWAYS:
    default:               return 1;
    }
}

static unsigned char grApplyStencilOp64(int op, unsigned char cur, unsigned char ref)
{
    switch (op)
    {
    case GR_STENCILOP_KEEP:    return cur;
    case GR_STENCILOP_REPLACE: return ref;
    case GR_STENCILOP_INCR:    return (cur < 255u) ? (unsigned char)(cur + 1u) : 255u;
    case GR_STENCILOP_DECR:    return (cur > 0u) ? (unsigned char)(cur - 1u) : 0u;
    case GR_STENCILOP_ZERO:    return 0u;
    case GR_STENCILOP_INVERT:  return (unsigned char)~cur;
    default:                   return cur;
    }
}

static unsigned int grReadTexel64(const _grPixelPipeTextureUnit *tu,
                                  int s, int t,
                                  const _grSurfaceDescriptor *desc)
{
    int idx = (t << tu->log2DimS) + s;
    unsigned int packed = 0;
    if (desc->paletteFormat == GR_PALETTEFORMAT_4BIT)
    {
        const unsigned char *bytes = (const unsigned char *)tu->pixels;
        /* Original bytecode +0xA70: shift = ((index << 2) & 4) ^ 4. */
        int palIdx = (bytes[idx >> 1] >> ((idx & 1) ? 0 : 4)) & 0xF;
        if (desc->colorBytes == 2)
            packed = ((const unsigned short *)tu->palette)[palIdx];
        else
            packed = ((const unsigned int *)tu->palette)[palIdx];
    }
    else if (desc->paletteFormat == GR_PALETTEFORMAT_8BIT)
    {
        int palIdx = ((const unsigned char *)tu->pixels)[idx];
        if (desc->colorBytes == 2)
            packed = ((const unsigned short *)tu->palette)[palIdx];
        else
            packed = ((const unsigned int *)tu->palette)[palIdx];
    }
    else
    {
        if (desc->colorBytes == 1)
            packed = ((const unsigned char *)tu->pixels)[idx];
        else if (desc->colorBytes == 2)
            packed = ((const unsigned short *)tu->pixels)[idx];
        else
            packed = ((const unsigned int *)tu->pixels)[idx];
    }
    return grUnpackColor(packed, (_grSurfaceDescriptor *)desc);
}

static unsigned int grSampleTexel64(const _grTexture *texObj,
                                    const _grPixelPipeTextureUnit *tu,
                                    int sVal, int tVal,
                                    const _grSurfaceDescriptor *desc)
{
    int raw[2] = { sVal, tVal };
    int masks[2] = { tu->sMask, tu->tMask };
    int coords[2], next[2], fraction[2];
    unsigned int texels[4], result = 0;
    int i, c;

    for (i = 0; i < 2; ++i)
    {
        int shift = tu->rShift[i];
        if (masks[i] == 0)
        {
            coords[i] = next[i] = fraction[i] = 0;
            continue;
        }
        if (texObj->wrapMode[i] == GR_WRAPMODE_CLAMP_TO_EDGE)
        {
            int base = raw[i] >> shift;
            coords[i] = hgClamp32_0_x(base, masks[i]);
            /* HC ADD wraps before the clamp, including INT_MAX at shift zero. */
            next[i] = hgClamp32_0_x((int)((unsigned int)base + 1u), masks[i]);
        }
        else if (texObj->wrapMode[i] == GR_WRAPMODE_MIRRORED_REPEAT)
        {
            int base = raw[i] >> shift;
            int neighbor = (int)((unsigned int)raw[i] + (1u << shift)) >> shift;
            coords[i] = (base ^ (base >> 31)) & masks[i];
            next[i] = (neighbor ^ (neighbor >> 31)) & masks[i];
        }
        else
        {
            coords[i] = (int)(((unsigned int)raw[i] >> shift) & (unsigned int)masks[i]);
            next[i] = (coords[i] + 1) & masks[i];
        }
        /* Original bytecode +0x307 uses eight fractional bits per axis. */
        fraction[i] = (int)((shift >= 8
                      ? (unsigned int)raw[i] >> (shift - 8)
                      : (unsigned int)raw[i] << (8 - shift)) & 255u);
    }

    texels[0] = grReadTexel64(tu, coords[0], coords[1], desc);
    if (!tu->linear)
        return texels[0];
    texels[1] = grReadTexel64(tu, next[0], coords[1], desc);
    texels[2] = grReadTexel64(tu, coords[0], next[1], desc);
    texels[3] = grReadTexel64(tu, next[0], next[1], desc);
    for (c = 0; c < 4; ++c)
    {
        int a = (int)((texels[0] >> (8 * c)) & 255u);
        int b = (int)((texels[1] >> (8 * c)) & 255u);
        int d = (int)((texels[2] >> (8 * c)) & 255u);
        int e = (int)((texels[3] >> (8 * c)) & 255u);
        /* Bytecode +0xABE interpolates horizontally, then vertically,
         * with an arithmetic shift after each signed multiply. */
        int top = a + (((b - a) * fraction[0]) >> 8);
        int bottom = d + (((e - d) * fraction[0]) >> 8);
        int value = top + (((bottom - top) * fraction[1]) >> 8);
        result |= (unsigned int)value << (8 * c);
    }
    return result;
}

static void grApplyTexEnv64(const Gerbera_s *state, int unit,
                            unsigned int primaryRGBA, unsigned int texRGBA,
                            int curRGBA[4])
{
    const _grTextureUnitState *us = &state->textureUnit[2 * unit];
    const _grTexture *texObj = us->texture;
    int baseFormat = (texObj != NULL) ? texObj->mipmap[0].texFormat : GR_TEXTUREFORMAT_RGBA8;
    int envRGBA[4];
    int texC[4];
    int priC[4];
    int prevC[4];
    int c;

    for (c = 0; c < 4; ++c)
    {
        prevC[c] = curRGBA[c];
        priC[c]  = (int)((primaryRGBA >> (8 * c)) & 0xFFu);
        texC[c]  = (int)((texRGBA >> (8 * c)) & 0xFFu);
        envRGBA[c] = (int)((us->texEnvColor >> (8 * c)) & 0xFFu);
    }
    if (baseFormat == GR_TEXTUREFORMAT_LUMINANCE8)
    {
        texC[1] = texC[0];
        texC[2] = texC[0];
        texC[3] = 255;
    }
    else if (baseFormat == GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8)
    {
        texC[1] = texC[0];
        texC[2] = texC[0];
    }
    else if (baseFormat == GR_TEXTUREFORMAT_ALPHA8)
    {
        texC[0] = 0;
        texC[1] = 0;
        texC[2] = 0;
    }
    else if (baseFormat == GR_TEXTUREFORMAT_RGB8 ||
             baseFormat == GR_TEXTUREFORMAT_RGB565 ||
             baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB8 ||
             baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB565 ||
             baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB8 ||
             baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB565)
    {
        texC[3] = 255;
    }

    /* Bytecode +0x41E scales MODULATE by (second + 1)/256; +0x445
     * truncates INTERPOLATE with complementary weights summing to 255. */
    switch (us->texEnvMode)
    {
    case GR_TEXENVMODE_REPLACE:
        if (baseFormat == GR_TEXTUREFORMAT_ALPHA8)
        {
            curRGBA[3] = texC[3];
        }
        else if (baseFormat == GR_TEXTUREFORMAT_LUMINANCE8 ||
                 baseFormat == GR_TEXTUREFORMAT_RGB8 ||
                 baseFormat == GR_TEXTUREFORMAT_RGB565 ||
                 baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB8 ||
                 baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB565 ||
                 baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB8 ||
                 baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB565)
        {
            curRGBA[0] = texC[0];
            curRGBA[1] = texC[1];
            curRGBA[2] = texC[2];
        }
        else
        {
            for (c = 0; c < 4; ++c)
                curRGBA[c] = texC[c];
        }
        break;

    case GR_TEXENVMODE_DECAL:
        /* The original environment mapping replaces RGB for every non-RGBA format. */
        if (baseFormat == GR_TEXTUREFORMAT_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_LUMINANCE8 ||
            baseFormat == GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGB8 ||
            baseFormat == GR_TEXTUREFORMAT_RGB565 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGB565 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGB565)
        {
            curRGBA[0] = texC[0];
            curRGBA[1] = texC[1];
            curRGBA[2] = texC[2];
        }
        else
        {
            for (c = 0; c < 3; ++c)
                curRGBA[c] = (prevC[c] * (255 - texC[3]) + texC[c] * texC[3]) >> 8;
        }
        break;

    case GR_TEXENVMODE_BLEND:
        if (baseFormat != GR_TEXTUREFORMAT_ALPHA8)
        {
            for (c = 0; c < 3; ++c)
                curRGBA[c] = (prevC[c] * (255 - texC[c]) + envRGBA[c] * texC[c]) >> 8;
        }
        if (baseFormat == GR_TEXTUREFORMAT_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA4444 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA5551)
        {
            curRGBA[3] = (prevC[3] * texC[3] + prevC[3]) >> 8;
        }
        break;

    case GR_TEXENVMODE_ADD:
        if (baseFormat != GR_TEXTUREFORMAT_ALPHA8)
        {
            for (c = 0; c < 3; ++c)
                curRGBA[c] = hgClamp32_0_x(prevC[c] + texC[c], 255);
        }
        if (baseFormat == GR_TEXTUREFORMAT_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA4444 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA5551)
        {
            curRGBA[3] = (prevC[3] * texC[3] + prevC[3]) >> 8;
        }
        break;

    case GR_TEXENVMODE_COMBINE:
    {
        int argRGB[3][3];
        int argA[3];
        int k;

        for (k = 0; k < 3; ++k)
        {
            const int *srcV = prevC;
            if (us->texEnvRGBCombSrc[k] == GR_TEXENVCOMBSRC_TEXTURE)            srcV = texC;
            else if (us->texEnvRGBCombSrc[k] == GR_TEXENVCOMBSRC_CONSTANT)      srcV = envRGBA;
            else if (us->texEnvRGBCombSrc[k] == GR_TEXENVCOMBSRC_PRIMARY_COLOR) srcV = priC;

            for (c = 0; c < 3; ++c)
            {
                int v = srcV[c];
                if (us->texEnvRGBCombOperand[k] == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_COLOR)
                    v = 255 - srcV[c];
                else if (us->texEnvRGBCombOperand[k] == GR_TEXENVCOMBOPERAND_SRC_ALPHA)
                    v = srcV[3];
                else if (us->texEnvRGBCombOperand[k] == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA)
                    v = 255 - srcV[3];
                argRGB[k][c] = v;
            }

            srcV = prevC;
            if (us->texEnvAlphaCombSrc[k] == GR_TEXENVCOMBSRC_TEXTURE)            srcV = texC;
            else if (us->texEnvAlphaCombSrc[k] == GR_TEXENVCOMBSRC_CONSTANT)      srcV = envRGBA;
            else if (us->texEnvAlphaCombSrc[k] == GR_TEXENVCOMBSRC_PRIMARY_COLOR) srcV = priC;
            argA[k] = (us->texEnvAlphaCombOperand[k] == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA) ? (255 - srcV[3]) : srcV[3];
        }

        if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_DOT3_RGB ||
            us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_DOT3_RGBA)
        {
            /* Bytecode +0x47C centers both operands at 127 and shifts by six. */
            int dot = ((argRGB[0][0] - 127) * (argRGB[1][0] - 127) +
                       (argRGB[0][1] - 127) * (argRGB[1][1] - 127) +
                       (argRGB[0][2] - 127) * (argRGB[1][2] - 127)) >> 6;
            curRGBA[0] = curRGBA[1] = curRGBA[2] =
                hgClamp32_0_x(dot * (int)us->texEnvRGBCombScale, 255);
            if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_DOT3_RGBA)
            {
                curRGBA[3] = hgClamp32_0_x(dot * (int)us->texEnvAlphaCombScale, 255);
                break;
            }
        }
        else
        {
            for (c = 0; c < 3; ++c)
            {
                int r = argRGB[0][c];
                if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_MODULATE)
                    r = (argRGB[0][c] * argRGB[1][c] + argRGB[0][c]) >> 8;
                else if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_ADD)
                    r = argRGB[0][c] + argRGB[1][c];
                else if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_ADD_SIGNED)
                    r = argRGB[0][c] + argRGB[1][c] - 127;
                else if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_SUBTRACT)
                    r = argRGB[0][c] - argRGB[1][c];
                else if (us->texEnvRGBCombFunc == GR_TEXENVCOMBFUNC_INTERPOLATE)
                    r = (argRGB[0][c] * argRGB[2][c] + argRGB[1][c] * (255 - argRGB[2][c])) >> 8;
                curRGBA[c] = hgClamp32_0_x(r * (int)us->texEnvRGBCombScale, 255);
            }
        }

        {
            int ra = argA[0];
            if (us->texEnvAlphaCombFunc == GR_TEXENVCOMBFUNC_MODULATE)
                ra = (argA[0] * argA[1] + argA[0]) >> 8;
            else if (us->texEnvAlphaCombFunc == GR_TEXENVCOMBFUNC_ADD)
                ra = argA[0] + argA[1];
            else if (us->texEnvAlphaCombFunc == GR_TEXENVCOMBFUNC_ADD_SIGNED)
                ra = argA[0] + argA[1] - 127;
            else if (us->texEnvAlphaCombFunc == GR_TEXENVCOMBFUNC_SUBTRACT)
                ra = argA[0] - argA[1];
            else if (us->texEnvAlphaCombFunc == GR_TEXENVCOMBFUNC_INTERPOLATE)
                ra = (argA[0] * argA[2] + argA[1] * (255 - argA[2])) >> 8;
            curRGBA[3] = hgClamp32_0_x(ra * (int)us->texEnvAlphaCombScale, 255);
        }
        break;
    }

    case GR_TEXENVMODE_MODULATE:
    default:
        if (baseFormat != GR_TEXTUREFORMAT_ALPHA8)
        {
            for (c = 0; c < 3; ++c)
                curRGBA[c] = (prevC[c] * texC[c] + prevC[c]) >> 8;
        }
        if (baseFormat == GR_TEXTUREFORMAT_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_LUMINANCE8_ALPHA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA4444 ||
            baseFormat == GR_TEXTUREFORMAT_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE4_RGBA5551 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA8 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA4 ||
            baseFormat == GR_TEXTUREFORMAT_PALETTE8_RGBA5551)
        {
            curRGBA[3] = (prevC[3] * texC[3] + prevC[3]) >> 8;
        }
        break;
    }
}

static void grComputeBlendWeights64(int factor, const int src[4], const int dst[4], int out[4])
{
    int fA;
    switch (factor)
    {
    case GR_BLENDFACTOR_ZERO:
        out[0] = out[1] = out[2] = out[3] = 0;
        break;
    case GR_BLENDFACTOR_ONE:
        out[0] = out[1] = out[2] = out[3] = 255;
        break;
    case GR_BLENDFACTOR_SRC_COLOR:
        out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; out[3] = src[3];
        break;
    case GR_BLENDFACTOR_ONE_MINUS_SRC_COLOR:
        out[0] = 255 - src[0]; out[1] = 255 - src[1]; out[2] = 255 - src[2]; out[3] = 255 - src[3];
        break;
    case GR_BLENDFACTOR_SRC_ALPHA:
        out[0] = out[1] = out[2] = out[3] = src[3];
        break;
    case GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        out[0] = out[1] = out[2] = out[3] = 255 - src[3];
        break;
    case GR_BLENDFACTOR_DST_ALPHA:
        out[0] = out[1] = out[2] = out[3] = dst[3];
        break;
    case GR_BLENDFACTOR_ONE_MINUS_DST_ALPHA:
        out[0] = out[1] = out[2] = out[3] = 255 - dst[3];
        break;
    case GR_BLENDFACTOR_DST_COLOR:
        out[0] = dst[0]; out[1] = dst[1]; out[2] = dst[2]; out[3] = dst[3];
        break;
    case GR_BLENDFACTOR_ONE_MINUS_DST_COLOR:
        out[0] = 255 - dst[0]; out[1] = 255 - dst[1]; out[2] = 255 - dst[2]; out[3] = 255 - dst[3];
        break;
    case GR_BLENDFACTOR_SRC_ALPHA_SATURATE:
        fA = hgMin32(src[3], 255 - dst[3]);
        out[0] = out[1] = out[2] = fA;
        out[3] = 255;
        break;
    default:
        out[0] = out[1] = out[2] = out[3] = 255;
        break;
    }
}

static int grBlendComponent64(int source, int destination, int sourceWeight, int destinationWeight)
{
    /* The generated pixel program truncates each scaled contribution before
     * adding them; each eight-bit weight represents (weight + 1) / 256. */
    return hgMin32(((source * (sourceWeight + 1)) >> 8) +
                   ((destination * (destinationWeight + 1)) >> 8), 255);
}

static unsigned int grApplyLogicOp64(int op, unsigned int s, unsigned int d)
{
    switch (op)
    {
    case GR_LOGICOP_CLEAR:         return 0u;
    case GR_LOGICOP_AND:           return s & d;
    case GR_LOGICOP_AND_REVERSE:   return s & ~d;
    case GR_LOGICOP_COPY:          return s;
    case GR_LOGICOP_AND_INVERTED:  return ~s & d;
    case GR_LOGICOP_NOOP:          return d;
    case GR_LOGICOP_XOR:           return s ^ d;
    case GR_LOGICOP_OR:            return s | d;
    case GR_LOGICOP_NOR:           return ~(s | d);
    case GR_LOGICOP_EQUIV:         return ~(s ^ d);
    case GR_LOGICOP_INVERT:        return ~d;
    case GR_LOGICOP_OR_REVERSE:    return s | ~d;
    case GR_LOGICOP_COPY_INVERTED: return ~s;
    case GR_LOGICOP_OR_INVERTED:   return ~s | d;
    case GR_LOGICOP_NAND:          return ~(s & d);
    case GR_LOGICOP_SET:           return 0xFFFFFFFFu;
    default:                       return s;
    }
}

/* HC ADD/MUL operate on 32-bit words. Repeat texture coordinates deliberately
 * wrap at the signed boundary as a span advances. */
static int grAdvanceGradient64(int value, int adder, int count)
{
    return (int)((unsigned int)value + (unsigned int)adder * (unsigned int)count);
}

static void grExecutePixelPipeline64(Gerbera_s *state)
{
    _grPixelPipe *pp = state->pixelPipeline;
    _grDrawSurface *surf = &state->drawSurface;
    unsigned char *colorRow = (unsigned char *)pp->colorRow;
    unsigned char *depthRow = (unsigned char *)pp->depthRow;
    unsigned char *stencilRow = (unsigned char *)pp->stencilRow;
    int rowRef[GR_GRADIENT_MAX];
    _grSurfaceDescriptor texDesc[2];
    int unitActive[2];
    int gradMask = state->activeGradientMask;
    int colorBpp = surf->colorBytes;
    int depthBpp = surf->depthBytes;
    int stencilBpp = surf->stencilBytes;
    int alphaTest = grTestCapability(state, GR_CAPABILITY_ALPHA_TEST);
    int stencilTest = grTestCapability(state, GR_CAPABILITY_STENCIL_TEST) && (stencilBpp > 0 && stencilRow != NULL);
    int depthTest = grTestCapability(state, GR_CAPABILITY_DEPTH_TEST) && (depthBpp > 0 && depthRow != NULL);
    int fogEnabled = grTestCapability(state, GR_CAPABILITY_FOG);
    int blendEnabled = grTestCapability(state, GR_CAPABILITY_BLEND);
    int logicEnabled = grTestCapability(state, GR_CAPABILITY_COLOR_LOGIC_OP);
    int ditherEnabled = grTestCapability(state, GR_CAPABILITY_DITHER);
    int earlyDepth = depthTest && !alphaTest && !stencilTest;
    unsigned int writeMask = 0xFFFFFFFFu;
    const _grTexture *texObj0 = state->textureUnit[0].texture;
    int g, u, s;

    if (surf->colorMask != (unsigned char)(state->colorMask & surf->colorMask))
        writeMask = grGetPackedColorWriteMask((_grSurfaceDescriptor *)surf, (unsigned char)state->colorMask);

    for (g = 0; g < GR_GRADIENT_MAX; ++g)
        rowRef[g] = (gradMask & (1 << g)) ? pp->reference[g] : 0;

    for (u = 0; u < 2; ++u)
    {
        const _grTexture *texObj = state->textureUnit[2 * u].texture;
        unitActive[u] = (state->useTexUnit[u] != 0 || state->textureUnitEnabled[u] != 0) &&
                        texObj != NULL && pp->textureUnit[u].pixels != NULL;
        if (unitActive[u])
        {
            grrGetTextureFormatDescriptor(&texDesc[u], texObj->mipmap[0].texFormat);
        }
    }

    /* Fast-path for standard 32-bit XRGB8888/ARGB8888 surfaces with Z16 + RGBA8 MODULATE */
    if (!stencilTest && !logicEnabled && !ditherEnabled &&
        writeMask == 0xFFFFFFFFu && state->colorMask != 0 &&
        colorBpp == 4 && colorRow != NULL &&
        surf->rgbaSizes[0] == 8 && surf->rgbaSizes[1] == 8 && surf->rgbaSizes[2] == 8 &&
        surf->rgbaOffsets[0] == 16 && surf->rgbaOffsets[1] == 8 && surf->rgbaOffsets[2] == 0 &&
        (surf->rgbaSizes[3] == 0 || (surf->rgbaSizes[3] == 8 && surf->rgbaOffsets[3] == 24)) &&
        !unitActive[1] &&
        (!depthTest || (depthBpp == 2 && (state->depthFunc == GR_FUNC_LEQUAL || state->depthFunc == GR_FUNC_LESS))) &&
        (!alphaTest || state->alphaFunc == GR_FUNC_GREATER) &&
        (!blendEnabled || (state->srcBlendFactor == GR_BLENDFACTOR_SRC_ALPHA &&
                           state->dstBlendFactor == GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA)) &&
        (!unitActive[0] || (texObj0->mipmap[0].texFormat == GR_TEXTUREFORMAT_RGBA8 &&
                            !pp->textureUnit[0].linear &&
                            state->textureUnit[0].texEnvMode == GR_TEXENVMODE_MODULATE &&
                            texObj0->wrapMode[0] != GR_WRAPMODE_MIRRORED_REPEAT &&
                            texObj0->wrapMode[1] != GR_WRAPMODE_MIRRORED_REPEAT &&
                            pp->textureUnit[0].sMask > 0 && pp->textureUnit[0].tMask > 0)))
    {
        const int hasZ    = depthTest;
        const int zLess   = (state->depthFunc == GR_FUNC_LESS);
        const int zWrite  = (state->depthMask != 0);
        const int hasTex  = unitActive[0];
        const int hasFog  = fogEnabled;
        const int hasAlp  = alphaTest;
        const int hasBld  = blendEnabled;
        const int dstHasA = (surf->rgbaSizes[3] == 8);
        const int gFog    = (gradMask & 2) != 0;
        const int gColR   = (gradMask & 4) != 0;
        const int gColG   = (gradMask & 8) != 0;
        const int gColB   = (gradMask & 16) != 0;
        const int gColA   = (gradMask & 32) != 0;
        const int defR    = hgClamp32_0_x((state->defaultColorRGBA[0] + 128) >> 8, 255);
        const int defG    = hgClamp32_0_x((state->defaultColorRGBA[1] + 128) >> 8, 255);
        const int defB    = hgClamp32_0_x((state->defaultColorRGBA[2] + 128) >> 8, 255);
        const int defA    = hgClamp32_0_x((state->defaultColorRGBA[3] + 128) >> 8, 255);
        const int fogR    = (int)(state->fogColor & 0xFFu);
        const int fogG    = (int)((state->fogColor >> 8) & 0xFFu);
        const int fogB    = (int)((state->fogColor >> 16) & 0xFFu);
        const int fogDef  = hgClamp32_0_x((int)(state->fogAtZero + 128u) >> 8, 255);
        const int alpRef  = (int)state->alphaReference;
        const unsigned int *texPixels = hasTex ? (const unsigned int *)pp->textureUnit[0].pixels : NULL;
        const int sMask   = hasTex ? pp->textureUnit[0].sMask : 0;
        const int tMask   = hasTex ? pp->textureUnit[0].tMask : 0;
        const int rShiftS = hasTex ? pp->textureUnit[0].rShift[0] : 0;
        const int rShiftT = hasTex ? pp->textureUnit[0].rShift[1] : 0;
        const int log2S   = hasTex ? pp->textureUnit[0].log2DimS : 0;
        const int clampS  = hasTex && (texObj0->wrapMode[0] == GR_WRAPMODE_CLAMP_TO_EDGE);
        const int clampT  = hasTex && (texObj0->wrapMode[1] == GR_WRAPMODE_CLAMP_TO_EDGE);
        const int dzdx = (gradMask & 1) ? pp->xAdder[0] : 0;
        const int dfdx = gFog ? pp->xAdder[1] : 0;
        const int drdx = gColR ? pp->xAdder[2] : 0;
        const int dgdx = gColG ? pp->xAdder[3] : 0;
        const int dbdx = gColB ? pp->xAdder[4] : 0;
        const int dadx = gColA ? pp->xAdder[5] : 0;
        const int dudx = (gradMask & 64) ? pp->xAdder[6] : 0;
        const int dvdx = (gradMask & 128) ? pp->xAdder[7] : 0;

        for (s = 0; s < pp->numScanlines; ++s)
        {
            unsigned int spanWord = pp->scanlineInfo[s];
            int xStart = (int)(spanWord & 0xFFFFu);
            int xEnd   = xStart + (int)(spanWord >> 16);
            unsigned int *cRow32 = (unsigned int *)colorRow;
            unsigned short *dRow16 = (unsigned short *)depthRow;
            int zCur = grAdvanceGradient64(rowRef[0], dzdx, xStart);
            int fCur = grAdvanceGradient64(rowRef[1], dfdx, xStart);
            int rCur = grAdvanceGradient64(rowRef[2], drdx, xStart);
            int gCur = grAdvanceGradient64(rowRef[3], dgdx, xStart);
            int bCur = grAdvanceGradient64(rowRef[4], dbdx, xStart);
            int aCur = grAdvanceGradient64(rowRef[5], dadx, xStart);
            int uCur = grAdvanceGradient64(rowRef[6], dudx, xStart);
            int vCur = grAdvanceGradient64(rowRef[7], dvdx, xStart);
            int x;

            for (x = xStart; x < xEnd; ++x)
            {
                unsigned int zVal = 0;
                int pr, pg, pb, pa;

                if (hasZ && !hasAlp)
                {
                    unsigned int z30 = (unsigned int)hgClamp32_0_x(zCur, 0x3FFFFFFF);
                    unsigned int zDst = dRow16[x];
                    zVal = z30 >> 14;
                    if (zLess ? !(zVal < zDst) : !(zVal <= zDst))
                        goto fast_step;
                    if (zWrite)
                        dRow16[x] = (unsigned short)zVal;
                }

                pr = gColR ? hgClamp32_0_x(rCur >> 16, 255) : defR;
                pg = gColG ? hgClamp32_0_x(gCur >> 16, 255) : defG;
                pb = gColB ? hgClamp32_0_x(bCur >> 16, 255) : defB;
                pa = gColA ? hgClamp32_0_x(aCur >> 16, 255) : defA;

                if (hasTex)
                {
                    int sc = clampS ? hgClamp32_0_x(uCur >> rShiftS, sMask)
                                    : (int)(((unsigned int)uCur >> rShiftS) & (unsigned int)sMask);
                    int tc = clampT ? hgClamp32_0_x(vCur >> rShiftT, tMask)
                                    : (int)(((unsigned int)vCur >> rShiftT) & (unsigned int)tMask);
                    unsigned int texel = texPixels[(tc << log2S) + sc];
                    int tr = (int)((texel >> 16) & 0xFFu);
                    int tg = (int)((texel >> 8) & 0xFFu);
                    int tb = (int)(texel & 0xFFu);
                    int ta = (int)((texel >> 24) & 0xFFu);
                    pa = (pa * ta + pa) >> 8;
                    if (hasAlp && !(pa > alpRef))
                        goto fast_step;
                    pr = (pr * tr + pr) >> 8;
                    pg = (pg * tg + pg) >> 8;
                    pb = (pb * tb + pb) >> 8;
                }
                else if (hasAlp && !(pa > alpRef))
                {
                    goto fast_step;
                }

                if (hasZ && hasAlp)
                {
                    unsigned int z30 = (unsigned int)hgClamp32_0_x(zCur, 0x3FFFFFFF);
                    unsigned int zDst = dRow16[x];
                    zVal = z30 >> 14;
                    if (zLess ? !(zVal < zDst) : !(zVal <= zDst))
                        goto fast_step;
                    if (zWrite)
                        dRow16[x] = (unsigned short)zVal;
                }

                if (hasFog)
                {
                    /* Bytecode +0x539 uses complementary weights totaling 256. */
                    int fog8 = gFog ? hgClamp32_0_x(fCur >> 16, 255) : fogDef;
                    int invF = 256 - fog8;
                    pr = (invF * pr + fog8 * fogR) >> 8;
                    pg = (invF * pg + fog8 * fogG) >> 8;
                    pb = (invF * pb + fog8 * fogB) >> 8;
                }

                if (hasBld)
                {
                    unsigned int dstP = cRow32[x];
                    int dr = (int)((dstP >> 16) & 0xFFu);
                    int dg = (int)((dstP >> 8) & 0xFFu);
                    int db = (int)(dstP & 0xFFu);
                    int da = dstHasA ? (int)((dstP >> 24) & 0xFFu) : 255;
                    int invA = 255 - pa;
                    pr = grBlendComponent64(pr, dr, pa, invA);
                    pg = grBlendComponent64(pg, dg, pa, invA);
                    pb = grBlendComponent64(pb, db, pa, invA);
                    pa = grBlendComponent64(pa, da, pa, invA);
                }

                cRow32[x] = dstHasA
                          ? (((unsigned int)pa << 24) | ((unsigned int)pr << 16) | ((unsigned int)pg << 8) | (unsigned int)pb)
                          : (((unsigned int)pr << 16) | ((unsigned int)pg << 8) | (unsigned int)pb);

fast_step:
                if (hasZ) zCur = grAdvanceGradient64(zCur, dzdx, 1);
                if (hasFog && gFog) fCur = grAdvanceGradient64(fCur, dfdx, 1);
                if (gColR) rCur = grAdvanceGradient64(rCur, drdx, 1);
                if (gColG) gCur = grAdvanceGradient64(gCur, dgdx, 1);
                if (gColB) bCur = grAdvanceGradient64(bCur, dbdx, 1);
                if (gColA) aCur = grAdvanceGradient64(aCur, dadx, 1);
                if (hasTex)
                {
                    uCur = grAdvanceGradient64(uCur, dudx, 1);
                    vCur = grAdvanceGradient64(vCur, dvdx, 1);
                }
            }

            for (g = 0; g < 8; ++g)
                if (gradMask & (1 << g))
                    rowRef[g] = grAdvanceGradient64(rowRef[g], pp->yAdder[g], 1);
            colorRow += surf->buffers[0].stride;
            if (depthRow != NULL) depthRow += surf->buffers[1].stride;
        }
        return;
    }

    for (s = 0; s < pp->numScanlines; ++s)
    {
        unsigned int spanWord = pp->scanlineInfo[s];
        int xStart = (int)(spanWord & 0xFFFFu);
        int count  = (int)(spanWord >> 16);
        int cur[GR_GRADIENT_MAX];
        int yScreen = pp->yStart + s;
        int x;

        for (g = 0; g < GR_GRADIENT_MAX; ++g)
            cur[g] = (gradMask & (1 << g))
                   ? grAdvanceGradient64(rowRef[g], pp->xAdder[g], xStart) : 0;

        for (x = xStart; x < xStart + count; ++x)
        {
            int rgba[4];
            unsigned int primaryRGBA;
            int c;

            if (earlyDepth)
            {
                unsigned int z30 = (unsigned int)hgClamp32_0_x(cur[0], 0x3FFFFFFF);
                unsigned int zVal = (depthBpp == 4) ? z30 : (z30 >> (30 - 8 * depthBpp));
                unsigned int zDst = (depthBpp == 1) ? depthRow[x]
                                  : (depthBpp == 2) ? ((unsigned short *)depthRow)[x]
                                                    : ((unsigned int *)depthRow)[x];
                if (!grEvalCompareFunc64(state->depthFunc, zVal, zDst))
                    goto step_pixel;
                if (state->depthMask != 0)
                {
                    if (depthBpp == 1)      depthRow[x] = (unsigned char)zVal;
                    else if (depthBpp == 2) ((unsigned short *)depthRow)[x] = (unsigned short)zVal;
                    else                    ((unsigned int *)depthRow)[x] = zVal;
                }
            }

            for (c = 0; c < 4; ++c)
            {
                if ((gradMask & (4 << c)) != 0)
                    rgba[c] = hgClamp32_0_x(cur[2 + c] >> 16, 255);
                else
                    rgba[c] = hgClamp32_0_x((state->defaultColorRGBA[c] + 128) >> 8, 255);
            }
            primaryRGBA = grSetRGBA(rgba[0], rgba[1], rgba[2], rgba[3]);

            for (u = 0; u < 2; ++u)
            {
                if (unitActive[u])
                {
                    const _grTexture *texObj = state->textureUnit[2 * u].texture;
                    unsigned int texel = grSampleTexel64(texObj, &pp->textureUnit[u],
                                                         cur[6 + 2 * u], cur[7 + 2 * u], &texDesc[u]);
                    grApplyTexEnv64(state, u, primaryRGBA, texel, rgba);
                }
            }

            if (fogEnabled)
            {
                int fog8 = ((gradMask & 2) != 0)
                         ? hgClamp32_0_x(cur[1] >> 16, 255)
                         : hgClamp32_0_x((int)(state->fogAtZero + 128u) >> 8, 255);
                for (c = 0; c < 3; ++c)
                {
                    int fc = (int)((state->fogColor >> (8 * c)) & 0xFFu);
                    rgba[c] = ((256 - fog8) * rgba[c] + fog8 * fc) >> 8;
                }
            }

            if (alphaTest && !grEvalCompareFunc64(state->alphaFunc, (unsigned int)rgba[3],
                                                  (unsigned int)state->alphaReference))
            {
                goto step_pixel;
            }

            if (stencilTest)
            {
                unsigned char sCur = stencilRow[x];
                unsigned char sRef = (unsigned char)(state->stencilReference & 0xFFu);
                unsigned char sMask = (unsigned char)(state->stencilTestMask & 0xFFu);
                unsigned char wMask = (unsigned char)(state->stencilWriteMask & 0xFFu);
                if (!grEvalCompareFunc64(state->stencilFunc,
                                         (unsigned int)(sRef & sMask),
                                         (unsigned int)(sCur & sMask)))
                {
                    unsigned char sNew = grApplyStencilOp64(state->stencilFail, sCur, sRef);
                    stencilRow[x] = (unsigned char)((sCur & ~wMask) | (sNew & wMask));
                    goto step_pixel;
                }
            }

            if (depthTest && !earlyDepth)
            {
                unsigned int z30 = (unsigned int)hgClamp32_0_x(cur[0], 0x3FFFFFFF);
                unsigned int zVal = (depthBpp == 4) ? z30 : (z30 >> (30 - 8 * depthBpp));
                unsigned int zDst = (depthBpp == 1) ? depthRow[x]
                                  : (depthBpp == 2) ? ((unsigned short *)depthRow)[x]
                                                    : ((unsigned int *)depthRow)[x];
                if (!grEvalCompareFunc64(state->depthFunc, zVal, zDst))
                {
                    if (stencilTest)
                    {
                        unsigned char sCur = stencilRow[x];
                        unsigned char sRef = (unsigned char)(state->stencilReference & 0xFFu);
                        unsigned char wMask = (unsigned char)(state->stencilWriteMask & 0xFFu);
                        unsigned char sNew = grApplyStencilOp64(state->stencilZFail, sCur, sRef);
                        stencilRow[x] = (unsigned char)((sCur & ~wMask) | (sNew & wMask));
                    }
                    goto step_pixel;
                }
                if (state->depthMask != 0)
                {
                    if (depthBpp == 1)      depthRow[x] = (unsigned char)zVal;
                    else if (depthBpp == 2) ((unsigned short *)depthRow)[x] = (unsigned short)zVal;
                    else                    ((unsigned int *)depthRow)[x] = zVal;
                }
            }

            if (stencilTest)
            {
                unsigned char sCur = stencilRow[x];
                unsigned char sRef = (unsigned char)(state->stencilReference & 0xFFu);
                unsigned char wMask = (unsigned char)(state->stencilWriteMask & 0xFFu);
                unsigned char sNew = grApplyStencilOp64(state->stencilZPass, sCur, sRef);
                stencilRow[x] = (unsigned char)((sCur & ~wMask) | (sNew & wMask));
            }

            if (colorBpp > 0 && colorRow != NULL && state->colorMask != 0)
            {
                unsigned int dstPacked = (colorBpp == 1) ? colorRow[x]
                                       : (colorBpp == 2) ? ((unsigned short *)colorRow)[x]
                                                         : ((unsigned int *)colorRow)[x];
                unsigned int outRGBA;
                unsigned int outPacked;

                if (blendEnabled && !logicEnabled)
                {
                    unsigned int dstRGBA = grUnpackColor(dstPacked, (_grSurfaceDescriptor *)surf);
                    int dstC[4], sw[4], dw[4];
                    for (c = 0; c < 4; ++c)
                        dstC[c] = (int)((dstRGBA >> (8 * c)) & 0xFFu);
                    if (surf->rgbaSizes[3] == 0)
                        dstC[3] = 255;
                    grComputeBlendWeights64(state->srcBlendFactor, rgba, dstC, sw);
                    grComputeBlendWeights64(state->dstBlendFactor, rgba, dstC, dw);
                    for (c = 0; c < 4; ++c)
                    {
                        /* SRC_ALPHA_SATURATE's generated multiply adds its
                         * minimum factor, rather than the color component. */
                        int sourceBias = (c < 3 && state->srcBlendFactor == GR_BLENDFACTOR_SRC_ALPHA_SATURATE)
                            ? sw[c] : rgba[c];
                        int destinationBias = (c < 3 && state->dstBlendFactor == GR_BLENDFACTOR_SRC_ALPHA_SATURATE)
                            ? dw[c] : dstC[c];
                        int sourceTerm = (rgba[c] * sw[c] + sourceBias) >> 8;
                        int destinationTerm = (dstC[c] * dw[c] + destinationBias) >> 8;
                        rgba[c] = hgMin32(sourceTerm + destinationTerm, 255);
                    }
                }

                outRGBA = grSetRGBA(rgba[0], rgba[1], rgba[2], rgba[3]);
                outPacked = ditherEnabled
                          ? grDitherColor(outRGBA, (_grSurfaceDescriptor *)surf, x, yScreen)
                          : grPackColor(outRGBA, (_grSurfaceDescriptor *)surf);

                if (logicEnabled)
                    outPacked = grApplyLogicOp64(state->logicOp, outPacked, dstPacked);

                if (writeMask != 0xFFFFFFFFu)
                    outPacked = (dstPacked & ~writeMask) | (outPacked & writeMask);

                if (colorBpp == 1)      colorRow[x] = (unsigned char)outPacked;
                else if (colorBpp == 2) ((unsigned short *)colorRow)[x] = (unsigned short)outPacked;
                else                    ((unsigned int *)colorRow)[x] = outPacked;
            }

step_pixel:
            for (g = 0; g < GR_GRADIENT_MAX; ++g)
                if (gradMask & (1 << g))
                    cur[g] = grAdvanceGradient64(cur[g], pp->xAdder[g], 1);
        }

        for (g = 0; g < GR_GRADIENT_MAX; ++g)
            if (gradMask & (1 << g))
                rowRef[g] = grAdvanceGradient64(rowRef[g], pp->yAdder[g], 1);
        if (colorRow != NULL)   colorRow += surf->buffers[0].stride;
        if (depthRow != NULL)   depthRow += surf->buffers[1].stride;
        if (stencilRow != NULL) stencilRow += surf->buffers[2].stride;
    }
}
#endif /* Hosts without a native 32-bit backend. */

void grExecutePixelPipeline(Gerbera_s *state)
{
    grPixelPipelineCode_fn pixelPipelineCode = state->pixelPipelineCode;

    if (pixelPipelineCode == NULL &&
        grAssert("pixelPipelineCode", "grPixelPipeline.h", 153) != 0)
        grDie(0);
    if (state->pixelPipeline == NULL &&
        grAssert("state->renderStack->pixelPipelineStack", "grPixelPipeline.h", 154) != 0)
        grDie(0);
#if defined(__SYMBIAN32__) || \
    ((defined(__arm__) || defined(__i386__) || defined(_M_IX86)) && UINTPTR_MAX == 0xFFFFFFFFu)
    /* Both original backends receive the scratch area preceding the parameters:
     * ARM reserves 64 bytes and x86 reserves 44 bytes before pixelPipeline. */
    pixelPipelineCode((char *)state->renderStack + GR_RENDER_STACK_PIXEL_PIPELINE);
#else
    if (state->pipelineCache != NULL)
    {
        uintptr_t codeAddr = (uintptr_t)pixelPipelineCode;
        uintptr_t arenaStart = (uintptr_t)state->pipelineCache->arena;
        uintptr_t arenaEnd = arenaStart + (uintptr_t)state->pipelineCache->arenaSize;
        if (codeAddr >= arenaStart && codeAddr < arenaEnd)
        {
            grExecutePixelPipeline64(state);
            return;
        }
    }
    pixelPipelineCode(state->pixelPipeline);
#endif
}

/* --- grIsGradientUsed (grPixelPipeline.inl 702; DLL 36949-36959) --------------
 * Reads gradient g's bit out of the HC compiler's per-parameter mask (the
 * mask indexes parameters, gradients start at bit 48). */

int grIsGradientUsed(unsigned char *paramMask, int g)
{
    if (paramMask == NULL &&
        grAssert("paramMask", "grPixelPipeline.inl", 702) != 0)
        grDie(0);
    return (paramMask[(g + 48) >> 3] >> ((g + 48) & 7)) & 1;
}

/* --- signature bit-count tables (data.fragments.c 3222/3281) ------------------
 * Per-BYTE significant-bit counts of the 144-byte pipeline signature: 56
 * main bytes (descriptor + scalars) then texture[2][44]. High bits past the
 * count are padding and get squeezed out by grCompressSignature. */

static const unsigned char s_mainBitCounts[56] =
{
    5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 0, 0, 0,
    1, 4, 1, 1, 3, 1, 3, 1, 1, 1, 4, 1, 4, 4, 1, 1,
    1, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char s_textureBitCounts[44] =
{
    5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 0, 0,
    1, 1, 3, 1, 1, 2, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 3, 3, 2, 2, 1, 1, 0, 0
};

/* --- grCompressSignature (grPixelPipeline.inl 845; DLL 36962-37109) -----------
 * Packs the 144-byte signature into the 40-byte cache key, squeezing each
 * byte down to its bit count and packing MSB-first into 32-bit words (a
 * byte may spill across a word boundary). */

void grCompressSignature(_grCacheSignature *compressed,
                         _grSignature *uncompressed)
{
    const unsigned char *src = (const unsigned char *)uncompressed;
    unsigned int *dst = (unsigned int *)compressed->bytes;
    unsigned int value = 0;
    int numFreeBits = 32;
    int i;

    for (i = 0; i < 56; ++i)
    {
        unsigned int srcValue = *src++;
        int srcSize = s_mainBitCounts[i];
        int includeBits = hgMin32(srcSize, numFreeBits);
        int excludeBits = srcSize - includeBits;

        if (((int)(~0u << srcSize) & (int)srcValue) != 0 &&
            grAssert("!((0xFFFFFFFF << srcSize) & srcValue)",
                     "grPixelPipeline.inl", 845) != 0)
            grDie(0);
        if (((value << includeBits) & (srcValue >> excludeBits)) != 0 &&
            grAssert("!((value << includeBits) & (srcValue >> excludeBits))",
                     "grPixelPipeline.inl", 846) != 0)
            grDie(0);
        value = (value << includeBits) + (srcValue >> excludeBits);
        numFreeBits -= includeBits;
        if (hgInRange32(numFreeBits, 0, 32) == 0 &&
            grAssert("hgInRange32(numFreeBits, 0, 32)",
                     "grPixelPipeline.inl", 850) != 0)
            grDie(0);
        if (numFreeBits == 0)
        {
            *dst++ = value;
            value = 0;
            numFreeBits = 32;
        }
        if (excludeBits != 0)
        {
            if ((numFreeBits != 32 || value != 0) &&
                grAssert("numFreeBits == 32 && !value",
                         "grPixelPipeline.inl", 860) != 0)
                grDie(0);
            value = srcValue & ((1 << excludeBits) - 1);
            numFreeBits -= excludeBits;
        }
    }
    for (i = 0; i < 2; ++i)
    {
        if ((const unsigned char *)src !=
            (const unsigned char *)&uncompressed->texture[i] &&
            grAssert("src == (HGuint8*)&uncompressed->texture[texIndex]",
                     "grPixelPipeline.inl", 872) != 0)
            grDie(0);
        {
            int j;

            for (j = 0; j < 44; ++j)
            {
                unsigned int srcValue = *src++;
                int srcSize = s_textureBitCounts[j];
                int includeBits = hgMin32(srcSize, numFreeBits);
                int excludeBits = srcSize - includeBits;

                if (((int)(~0u << srcSize) & (int)srcValue) != 0 &&
                    grAssert("!((0xFFFFFFFF << srcSize) & srcValue)",
                             "grPixelPipeline.inl", 881) != 0)
                    grDie(0);
                if (((value << includeBits) & (srcValue >> excludeBits)) != 0 &&
                    grAssert("!((value << includeBits) & (srcValue >> excludeBits))",
                             "grPixelPipeline.inl", 882) != 0)
                    grDie(0);
                value = (value << includeBits) + (srcValue >> excludeBits);
                numFreeBits -= includeBits;
                if (hgInRange32(numFreeBits, 0, 32) == 0 &&
                    grAssert("hgInRange32(numFreeBits, 0, 32)",
                             "grPixelPipeline.inl", 886) != 0)
                    grDie(0);
                if (numFreeBits == 0)
                {
                    *dst++ = value;
                    value = 0;
                    numFreeBits = 32;
                }
                if (excludeBits != 0)
                {
                    if ((numFreeBits != 32 || value != 0) &&
                        grAssert("numFreeBits == 32 && !value",
                                 "grPixelPipeline.inl", 896) != 0)
                        grDie(0);
                    value = srcValue & ((1 << excludeBits) - 1);
                    numFreeBits -= excludeBits;
                }
            }
        }
    }
    if ((const unsigned char *)src !=
        (const unsigned char *)(uncompressed + 1) &&
        grAssert("src == (HGuint8*)uncompressed + sizeof(grSignature)",
                 "grPixelPipeline.inl", 903) != 0)
        grDie(0);
    if (numFreeBits != 32)
        *dst++ = value;
    if ((unsigned char *)dst !=
        compressed->bytes + sizeof(compressed->bytes) &&
        grAssert("dst == &compressed->data[(GR_CACHE_SIGNATURE_SIZE+3)/4]",
                 "grPixelPipeline.inl", 909) != 0)
        grDie(0);
}

/* --- grEvaluatePixelPipeline (grPixelPipeline.inl 89; DLL 36304-36946) ---------
 * Builds the 144-byte pipeline signature from the current state: the
 * draw-surface descriptor, blend/logic/alpha/depth/stencil/fog setup and
 * the per-texture-unit env decode. The decompile uses an 8-descriptor
 * scratch array (memset 0xCC) whose fields are reused as flags/pointers
 * across phases; the port keeps the value fields and replaces the spilled
 * pointers with typed locals. All raw record offsets are kept. */

void grEvaluatePixelPipeline(Gerbera_s *state, _grSignature *signature)
{
    _grSurfaceDescriptor combinedMask[8];
    _grDrawSurface *drawSurface;

    memset(combinedMask, 0xCC, sizeof(combinedMask));
    drawSurface = &state->drawSurface;
    *(int *)&combinedMask[6].colorMask =
        (unsigned char)(state->colorMask & drawSurface->colorMask);
    *(int *)combinedMask[6].rgbaOffsets = 0;
    *(int *)combinedMask[6].rgbaSizes = 0;
    *(int *)&combinedMask[5].pixelFormat = 1;
    *(int *)&combinedMask[5].colorMask =
        (combinedMask[6].colorMask & 7) != 0;
    *(int *)combinedMask[5].rgbaSizes = (combinedMask[6].colorMask & 8) != 0;
    if ((state == NULL || signature == NULL) &&
        grAssert("state && signature", "grPixelPipeline.inl", 97) != 0)
        grDie(0);
    hgFill8((unsigned char *)&signature->descriptor, 0, 144);
    signature->isValid = 1;
    signature->dither = (unsigned char)state->useDithering;
    *(int *)&combinedMask[5].pixelFormat =
        (state->useDithering == 0) & *(int *)&combinedMask[5].pixelFormat;
    if (grTestCapability(state, GR_CAPABILITY_BLEND) != 0 &&
        (combinedMask[6].colorMask != 0u ||
         combinedMask[6].colorBytes != 0u ||
         combinedMask[6].depthBytes != 0u ||
         combinedMask[6].stencilBytes != 0u) &&
        (grGetSrcBlendFactor(state) != GR_BLENDFACTOR_ONE ||
         grGetDstBlendFactor(state) != GR_BLENDFACTOR_ZERO))
    {
        *(int *)combinedMask[6].rgbaOffsets = 1;
    }
    if (grTestCapability(state, GR_CAPABILITY_COLOR_LOGIC_OP) != 0)
    {
        *(int *)combinedMask[6].rgbaOffsets = 0;
        *(int *)combinedMask[6].rgbaSizes = state->logicOp != 3;
    }
    if (*(int *)combinedMask[6].rgbaOffsets != 0 ||
        *(int *)combinedMask[6].rgbaSizes != 0)
    {
        *(int *)&combinedMask[5].pixelFormat = 0;
    }
    if (grTestCapability(state, GR_CAPABILITY_DEPTH_TEST) != 0 &&
        drawSurface->buffers[1].data != 0 &&
        grTestCapability(state, GR_CAPABILITY_DEPTH_TEST) != 0 &&
        state->depthMask != 0)
    {
        int lineOffset = grTestCapability(state,
                                          GR_CAPABILITY_DEPTH_OFFSET_LINE);
        int anyOffset = grTestCapability(state,
                                         GR_CAPABILITY_POLYGON_OFFSET_FILL) |
                        lineOffset;

        if ((grTestCapability(state,
                              GR_CAPABILITY_DEPTH_OFFSET_POINT) |
             anyOffset) != 0)
        {
            *(int *)combinedMask[4].rgbaSizes =
                hgSign32(state->depthOffsetFactor);
            *(int *)combinedMask[4].rgbaOffsets =
                hgSign32(state->depthOffsetUnits);
            signature->depthClampMin =
                (*(int *)combinedMask[4].rgbaSizes != -1)
                    ? (unsigned char)(*(int *)combinedMask[4].rgbaOffsets == -1)
                    : 1;
            signature->depthClampMax =
                (*(int *)combinedMask[4].rgbaSizes != 1)
                    ? (unsigned char)(*(int *)combinedMask[4].rgbaOffsets == -1)
                    : 1;
        }
    }
    signature->logicOpEnabled = (unsigned char)*(int *)combinedMask[6].rgbaSizes;
    if (*(int *)combinedMask[6].rgbaSizes != 0)
        signature->logicOp = (unsigned char)grGetLogicOp(state);
    else
        signature->logicOp = 0;
    signature->blendEnabled =
        (unsigned char)*(int *)combinedMask[6].rgbaOffsets;
    if (*(int *)combinedMask[6].rgbaOffsets != 0)
    {
        signature->srcFactor = (unsigned char)grGetSrcBlendFactor(state);
        signature->dstFactor = (unsigned char)grGetDstBlendFactor(state);
    }
    signature->colorMask = (unsigned char)state->colorMask;
    *(int *)signature->descriptor.rgbaOffsets =
        *(int *)drawSurface->rgbaOffsets;
    *(int *)signature->descriptor.rgbaSizes =
        *(int *)drawSurface->rgbaSizes;
    *(int *)&signature->descriptor.colorMask =
        *(int *)&drawSurface->colorMask;
    *(int *)&signature->descriptor.pixelFormat =
        *(int *)&drawSurface->pixelFormat;
    if (drawSurface->buffers[0].data != 0 &&
        (signature->colorMask & signature->descriptor.colorMask) != 0 &&
        (signature->blendEnabled == 0 || signature->srcFactor != 0 ||
         signature->dstFactor != 1))
    {
        signature->writeColor = 1;
    }
    if (grTestCapability(state, GR_CAPABILITY_FOG) != 0)
    {
        *(int *)&combinedMask[3].pixelFormat =
            state->colorMask & drawSurface->colorMask & 7;
        if (*(int *)&combinedMask[3].pixelFormat != 0)
        {
            *(unsigned int *)&combinedMask[3].colorMask = state->fogColor;
            signature->fogEnabled = 1;
            *(int *)combinedMask[3].rgbaSizes =
                *(int *)&combinedMask[3].colorMask;
            if (grMaskedColorsEqual(*(unsigned int *)&combinedMask[3].colorMask,
                                    *(unsigned int *)&combinedMask[3].pixelFormat) != 0)
            {
                int ndx = 31 - hgClz32(
                    *(unsigned int *)&combinedMask[3].pixelFormat);
                if ((hgInRange32(ndx, 0, 3) == 0) &&
                    grAssert("hgInRange32(ndx, 0, 3)",
                             "grPixelPipeline.inl", 197) != 0)
                    grDie(0);
                signature->monochromeFog = 1;
                *(int *)combinedMask[3].rgbaSizes =
                    grUnpackRGBA(*(unsigned int *)&combinedMask[3].colorMask,
                                 ndx);
            }
            state->packedFogColor = *(int *)combinedMask[3].rgbaSizes;
            *(int *)&combinedMask[5].pixelFormat = 0;
        }
    }
    if (grTestCapability(state, GR_CAPABILITY_ALPHA_TEST) != 0 &&
        grGetAlphaFunc(state) != GR_FUNC_ALWAYS)
    {
        *(int *)&combinedMask[2].pixelFormat = grGetAlphaFunc(state);
        signature->alphaTest = 1;
        signature->alphaFunc = (unsigned char)*(int *)&combinedMask[2].pixelFormat;
    }
    if (grTestCapability(state, GR_CAPABILITY_DEPTH_TEST) != 0 &&
        drawSurface->buffers[1].data != 0 &&
        (state->depthFunc != 7 || state->depthMask != 0))
    {
        signature->depthTest = 1;
        signature->depthFunc = (unsigned char)state->depthFunc;
        signature->depthMask = (unsigned char)state->depthMask;
        if (state->viewportScale.normal[2] == 0 &&
            (state->depthFunc != 7 || state->depthMask != 0))
        {
            signature->constantDepth = 1;
        }
    }
    combinedMask[2].colorMask = 0u;
    combinedMask[2].colorBytes = 0u;
    combinedMask[2].depthBytes = 0u;
    combinedMask[2].stencilBytes = 0u;
    *(int *)combinedMask[2].rgbaSizes = grGetStencilFunc(state);
    *(int *)combinedMask[2].rgbaOffsets = grGetStencilFail(state);
    *(int *)&combinedMask[1].pixelFormat = grGetStencilZFail(state);
    *(int *)&combinedMask[1].colorMask = grGetStencilZPass(state);
    if (grTestCapability(state, GR_CAPABILITY_STENCIL_TEST) != 0 &&
        drawSurface->buffers[2].data != 0)
    {
        combinedMask[2].colorMask = 1u;
        combinedMask[2].colorBytes = 0u;
        combinedMask[2].depthBytes = 0u;
        combinedMask[2].stencilBytes = 0u;
        if (*(int *)combinedMask[2].rgbaSizes == 7)
        {
            *(int *)combinedMask[2].rgbaOffsets = 0;
            if (signature->depthTest != 0)
            {
                if (signature->depthFunc == 7)
                {
                    *(int *)&combinedMask[1].pixelFormat = 0;
                }
                else if (signature->depthFunc == 0)
                {
                    combinedMask[1].colorMask = 0u;
                    combinedMask[1].colorBytes = 0u;
                    combinedMask[1].depthBytes = 0u;
                    combinedMask[1].stencilBytes = 0u;
                }
            }
            else
            {
                *(int *)&combinedMask[1].pixelFormat = 0;
            }
            if (*(int *)&combinedMask[1].pixelFormat == 0 &&
                combinedMask[1].colorMask == 0u &&
                combinedMask[1].colorBytes == 0u &&
                combinedMask[1].depthBytes == 0u &&
                combinedMask[1].stencilBytes == 0u)
            {
                combinedMask[2].colorMask = 0u;
                combinedMask[2].colorBytes = 0u;
                combinedMask[2].depthBytes = 0u;
                combinedMask[2].stencilBytes = 0u;
            }
        }
        else if (*(int *)combinedMask[2].rgbaSizes == 0)
        {
            combinedMask[1].colorMask = 0u;
            combinedMask[1].colorBytes = 0u;
            combinedMask[1].depthBytes = 0u;
            combinedMask[1].stencilBytes = 0u;
            *(int *)&combinedMask[1].pixelFormat = 0;
        }
    }
    signature->stencilTest = (unsigned char)combinedMask[2].colorMask;
    if (combinedMask[2].colorMask != 0u ||
        combinedMask[2].colorBytes != 0u ||
        combinedMask[2].depthBytes != 0u ||
        combinedMask[2].stencilBytes != 0u)
    {
        signature->stencilFunc =
            (unsigned char)*(int *)combinedMask[2].rgbaSizes;
        signature->stencilFail =
            (unsigned char)*(int *)combinedMask[2].rgbaOffsets;
        signature->stencilZFail =
            (unsigned char)*(int *)&combinedMask[1].pixelFormat;
        signature->stencilZPass =
            (unsigned char)*(int *)&combinedMask[1].colorMask;
        signature->stencilTestMaskFull =
            (unsigned char)grIsStencilMaskFull(state, state->stencilTestMask);
        signature->stencilWriteMaskFull =
            (unsigned char)grIsStencilMaskFull(state, state->stencilWriteMask);
        if (*(int *)&combinedMask[1].pixelFormat ==
            *(int *)&combinedMask[1].colorMask)
        {
            if (*(int *)combinedMask[2].rgbaOffsets ==
                *(int *)&combinedMask[1].pixelFormat)
                signature->stencilAllOpsEqual = 1;
            else
                signature->stencilZOpsEqual = 1;
        }
    }
    *(int *)combinedMask[5].rgbaOffsets = signature->flatShading;
    *(int *)&combinedMask[4].pixelFormat = 1;
    combinedMask[4].colorMask = 1u;
    combinedMask[4].colorBytes = 0u;
    combinedMask[4].depthBytes = 0u;
    combinedMask[4].stencilBytes = 0u;
    while (*(int *)&combinedMask[4].colorMask >= 0)
    {
        int unit = *(int *)&combinedMask[4].colorMask;
        _grTextureUnitState *unitRec = &state->textureUnit[2 * unit];
        _grTexture *texObj = unitRec->texture;

        if (state->useTexUnit[unit] != 0)
        {
            unsigned char *rec = signature->texture[unit];
            int pf;

            grrGetTextureFormatDescriptor(&combinedMask[7], texObj->mipmap[0].texFormat);
            *(int *)&combinedMask[0].colorMask =
                (int)(unsigned char)combinedMask[7].pixelFormat;
            rec[16] = 1;
            rec[17] = (unsigned char)((texObj->parmMask & 0x10) != 0);
            rec[19] = (unsigned char)((texObj->filterMask[GR_FILTERTYPE_MAG] & 1) != 0);
            rec[20] = (unsigned char)((texObj->filterMask[GR_FILTERTYPE_MIN] & 1) != 0);
            rec[21] = texObj->wrapMode[0];
            rec[22] = texObj->wrapMode[1];
            *(int *)&rec[0] = *(int *)combinedMask[7].rgbaOffsets;
            *(int *)&rec[4] = *(int *)combinedMask[7].rgbaSizes;
            *(int *)&rec[8] = *(int *)&combinedMask[7].colorMask;
            *(int *)&rec[12] = *(int *)&combinedMask[7].pixelFormat;
            rec[38] = 0;
            rec[39] = 0;
            rec[26] = 0;
            rec[30] = 0;
            rec[34] = 0;
            rec[27] = 2;
            rec[31] = 2;
            rec[35] = 2;
            rec[28] = 0;
            rec[32] = 0;
            rec[29] = 0;
            rec[33] = 0;
            rec[36] = 0xFF;
            rec[37] = 0xFF;
            rec[24] = 0xFF;
            rec[25] = 0xFF;
            pf = *(int *)&combinedMask[0].colorMask;
            switch (unitRec->texEnvMode)
            {
                case 0:
                    if (pf != 0)
                    {
                        if (pf == 1 || pf == 4)
                        {
                            rec[36] = 0;
                            rec[37] = 1;
                        }
                        else
                        {
                            if ((pf != 2 && pf != 3 && pf != 5) &&
                                grAssert(
                                    "pixelFormat == GR_PIXELFORMAT_LUMINANCE_ALPHA || pixelFormat == GR_PIXELFORMAT_INTENSITY || pixelFormat == GR_PIXELFORMAT_RGBA",
                                    "grPixelPipeline.inl", 408) != 0)
                                grDie(0);
                            rec[36] = 0;
                            rec[37] = 0;
                        }
                    }
                    else
                    {
                        rec[36] = 1;
                        rec[37] = 0;
                    }
                    rec[24] = 3;
                    rec[28] = 0;
                    rec[25] = 3;
                    rec[29] = 0;
                    break;
                case 1:
                    if (pf == 5)
                    {
                        rec[24] = 0;
                        rec[28] = 3;
                        rec[32] = 0;
                        rec[34] = 2;
                        rec[36] = 4;
                    }
                    else
                    {
                        rec[24] = 0;
                        rec[36] = 1;
                    }
                    rec[25] = 3;
                    rec[37] = 1;
                    break;
                case 2:
                    if (pf != 0)
                    {
                        if (pf == 3)
                        {
                            rec[24] = 1;
                            rec[28] = 3;
                            rec[32] = 0;
                            rec[36] = 4;
                            rec[25] = 1;
                            rec[29] = 3;
                            rec[33] = 0;
                            rec[37] = 4;
                        }
                        else if (pf == 1 || pf == 4)
                        {
                            rec[24] = 1;
                            rec[28] = 3;
                            rec[32] = 0;
                            rec[36] = 4;
                            rec[25] = 3;
                            rec[37] = 1;
                        }
                        else
                        {
                            if ((pf != 2 && pf != 5) &&
                                grAssert(
                                    "pixelFormat == GR_PIXELFORMAT_LUMINANCE_ALPHA || pixelFormat == GR_PIXELFORMAT_RGBA",
                                    "grPixelPipeline.inl", 463) != 0)
                                grDie(0);
                            rec[24] = 1;
                            rec[28] = 3;
                            rec[32] = 0;
                            rec[36] = 4;
                            rec[25] = 3;
                            rec[29] = 0;
                            rec[37] = 0;
                        }
                    }
                    else
                    {
                        rec[24] = 3;
                        rec[36] = 1;
                        rec[25] = 3;
                        rec[29] = 0;
                        rec[37] = 0;
                    }
                    break;
                case 3:
                    if (pf != 0)
                    {
                        if (pf == 1 || pf == 4)
                        {
                            rec[24] = 0;
                            rec[25] = 3;
                        }
                        else
                        {
                            if ((pf != 2 && pf != 3 && pf != 5) &&
                                grAssert(
                                    "pixelFormat == GR_PIXELFORMAT_LUMINANCE_ALPHA || pixelFormat == GR_PIXELFORMAT_INTENSITY || pixelFormat == GR_PIXELFORMAT_RGBA",
                                    "grPixelPipeline.inl", 383) != 0)
                                grDie(0);
                            rec[24] = 0;
                            rec[25] = 0;
                        }
                    }
                    else
                    {
                        rec[24] = 3;
                        rec[25] = 0;
                    }
                    rec[36] = 1;
                    rec[37] = 1;
                    break;
                case 4:
                    if (pf != 0)
                    {
                        if (pf == 3)
                        {
                            rec[24] = 3;
                            rec[28] = 0;
                            rec[36] = 2;
                            rec[25] = 3;
                            rec[29] = 0;
                            rec[37] = 2;
                        }
                        else if (pf == 1 || pf == 4)
                        {
                            rec[24] = 3;
                            rec[28] = 0;
                            rec[36] = 2;
                            rec[25] = 3;
                            rec[37] = 1;
                        }
                        else
                        {
                            if ((pf != 2 && pf != 5) &&
                                grAssert(
                                    "pixelFormat == GR_PIXELFORMAT_LUMINANCE_ALPHA || pixelFormat == GR_PIXELFORMAT_RGBA",
                                    "grPixelPipeline.inl", 515) != 0)
                                grDie(0);
                            rec[24] = 3;
                            rec[28] = 0;
                            rec[36] = 2;
                            rec[25] = 3;
                            rec[29] = 0;
                            rec[37] = 0;
                        }
                    }
                    else
                    {
                        rec[24] = 3;
                        rec[36] = 1;
                        rec[25] = 3;
                        rec[29] = 0;
                        rec[37] = 0;
                    }
                    break;
                case 5:
                    rec[36] = unitRec->texEnvRGBCombFunc;
                    rec[37] = unitRec->texEnvAlphaCombFunc;
                    rec[38] = (unsigned char)(31 - hgClz32(
                        (unsigned int)unitRec->texEnvRGBCombScale));
                    rec[39] = (unsigned char)(31 - hgClz32(
                        (unsigned int)unitRec->texEnvAlphaCombScale));
                    rec[24] = unitRec->texEnvRGBCombSrc[0];
                    rec[26] = unitRec->texEnvRGBCombOperand[0];
                    if (rec[36] != 1)
                    {
                        rec[28] = unitRec->texEnvRGBCombSrc[1];
                        rec[30] = unitRec->texEnvRGBCombOperand[1];
                        if (unitRec->texEnvRGBCombFunc == 4)
                        {
                            rec[32] = unitRec->texEnvRGBCombSrc[2];
                            rec[34] = unitRec->texEnvRGBCombOperand[2];
                        }
                    }
                    rec[25] = unitRec->texEnvAlphaCombSrc[0];
                    rec[27] = unitRec->texEnvAlphaCombOperand[0];
                    if (rec[37] != 1)
                    {
                        rec[29] = unitRec->texEnvAlphaCombSrc[1];
                        rec[31] = unitRec->texEnvAlphaCombOperand[1];
                        if (rec[37] == 4)
                        {
                            rec[33] = unitRec->texEnvAlphaCombSrc[2];
                            rec[35] = unitRec->texEnvAlphaCombOperand[2];
                        }
                    }
                    break;
                default:
                    if (grAssert("HG_FALSE",
                                 "grPixelPipeline.inl", 605) != 0)
                        grDie(0);
                    break;
            }
            if ((hgInRange32(rec[24], 0, 3) == 0) &&
                grAssert("hgInRange32(tex->rgbSource0, 0, GR_TEXENVCOMBSRC_MAX-1)",
                         "grPixelPipeline.inl", 609) != 0)
                grDie(0);
            if ((hgInRange32(rec[25], 0, 3) == 0) &&
                grAssert("hgInRange32(tex->alphaSource0, 0, GR_TEXENVCOMBSRC_MAX-1)",
                         "grPixelPipeline.inl", 610) != 0)
                grDie(0);
            if ((hgInRange32(rec[36], 0, 7) == 0) &&
                grAssert("hgInRange32(tex->rgbFunction, 0, GR_TEXENVCOMBFUNC_MAX-1)",
                         "grPixelPipeline.inl", 611) != 0)
                grDie(0);
            if ((hgInRange32(rec[37], 0, 7) == 0) &&
                grAssert("hgInRange32(tex->alphaFunction, 0, GR_TEXENVCOMBFUNC_MAX-1)",
                         "grPixelPipeline.inl", 612) != 0)
                grDie(0);
            rec[40] = 1;
            rec[41] = 1;
            if (rec[36] == 1 && rec[24] == 0 && rec[26] == 0)
            {
                combinedMask[5].colorMask = 0u;
                combinedMask[5].colorBytes = 0u;
                combinedMask[5].depthBytes = 0u;
                combinedMask[5].stencilBytes = 0u;
            }
            if (rec[37] == 1 && rec[25] == 0 && rec[27] == 2)
            {
                *(int *)combinedMask[5].rgbaSizes = 0;
            }
            if (*(int *)&combinedMask[5].pixelFormat != 0 &&
                rec[36] == 1 && rec[24] == 0 && rec[26] == 0 &&
                ((combinedMask[6].colorMask & 8) == 0 ||
                 (rec[37] == 1 && rec[25] == 0 && rec[27] == 2)) &&
                rec[19] == 0 && rec[20] == 0 &&
                (combinedMask[7].pixelFormat == 4 ||
                 combinedMask[7].pixelFormat == 5) &&
                grMaskedDescriptorsEqual(
                    *(unsigned int *)&combinedMask[6].colorMask,
                    (_grSurfaceDescriptor *)&state->drawSurface,
                    &combinedMask[7]) != 0)
            {
                if (!(combinedMask[5].colorMask == 0u &&
                      combinedMask[5].colorBytes == 0u &&
                      combinedMask[5].depthBytes == 0u &&
                      combinedMask[5].stencilBytes == 0u &&
                      *(int *)combinedMask[5].rgbaSizes == 0) &&
                    grAssert("!vertexRGBUsed && !vertexAlphaUsed",
                             "grPixelPipeline.inl", 646) != 0)
                    grDie(0);
                rec[23] = 1;
                *(int *)&combinedMask[4].pixelFormat = 0;
            }
            *(int *)&combinedMask[5].pixelFormat = 0;
            *(int *)combinedMask[5].rgbaOffsets = 0;
        }
        --*(int *)&combinedMask[4].colorMask;
    }
    if (grTestCapability(state, GR_CAPABILITY_SMOOTH_SHADING) == 0 ||
        (signature->alphaTest == 0 &&
         combinedMask[5].colorMask == 0u &&
         combinedMask[5].colorBytes == 0u &&
         combinedMask[5].depthBytes == 0u &&
         combinedMask[5].stencilBytes == 0u &&
         *(int *)combinedMask[5].rgbaSizes == 0))
    {
        signature->flatShading = 1;
    }
    {
        int v28 = 0;
        int v29 = 0;

        if (signature->blendEnabled == 0)
            v29 = signature->fogEnabled == 0;
        if (v29 && signature->dither == 0)
            v28 = 1;
        *(int *)combinedMask[5].rgbaOffsets &= v28;
        *(int *)&combinedMask[4].pixelFormat =
            (*(int *)combinedMask[5].rgbaOffsets == 0) &
            *(int *)&combinedMask[4].pixelFormat;
        signature->constantWriteColor =
            (unsigned char)*(int *)combinedMask[5].rgbaOffsets;
        signature->perPixelPack =
            (unsigned char)*(int *)&combinedMask[4].pixelFormat;
    }
    *(int *)combinedMask[0].rgbaSizes = signature->descriptor.colorMask;
    *(int *)combinedMask[0].rgbaOffsets =
        (unsigned char)(combinedMask[0].rgbaSizes[0] & signature->colorMask);
    {
        int v30 = 0;

        if (*(int *)combinedMask[0].rgbaOffsets != 0)
            v30 = *(int *)combinedMask[0].rgbaOffsets !=
                  *(int *)combinedMask[0].rgbaSizes;
        signature->applyColorWriteMask = (unsigned char)v30;
    }
    signature->screenRectGradients =
        (unsigned char)grGetRectGradients(state);
}

#if GERBERA_ARMV6_JIT
static int grSpanPackedRGB(const unsigned char *sizes)
{
    return (sizes[0] == 5 && sizes[1] == 6 && sizes[2] == 5 && sizes[3] == 0) ||
        (sizes[0] == 8 && sizes[1] == 8 && sizes[2] == 8 && (sizes[3] == 0 || sizes[3] == 8));
}

#endif

static unsigned int grPixelPipelineOptimizationMask(const _grSignature *sig)
{
#if GERBERA_ARMV6_JIT
    const unsigned char *tex = sig->texture[0];
    unsigned int mask = HC_OPT_ALL;
    int modulate = !sig->flatShading && tex[24] == GR_TEXENVCOMBSRC_PREVIOUS &&
        tex[28] == GR_TEXENVCOMBSRC_TEXTURE && tex[30] == GR_TEXENVCOMBOPERAND_SRC_COLOR &&
        tex[36] == GR_TEXENVCOMBFUNC_MODULATE;
    if (sig->writeColor && !sig->screenRectGradients && modulate &&
        !sig->blendEnabled && !sig->logicOpEnabled && !sig->applyColorWriteMask &&
        !sig->alphaTest && !sig->stencilTest &&
        (!sig->depthTest || sig->descriptor.depthBytes == 2) &&
        tex[16] && tex[17] && !sig->texture[1][16] && !tex[19] && !tex[20] &&
        tex[21] == GR_WRAPMODE_REPEAT && tex[22] == GR_WRAPMODE_REPEAT &&
        tex[26] == GR_TEXENVCOMBOPERAND_SRC_COLOR && !tex[38] && !tex[39])
    {
        if ((sig->depthTest && sig->descriptor.colorBytes == 2) ||
            (!sig->dither && (sig->fogEnabled || sig->descriptor.rgbaSizes[3] != 0) &&
             grSpanPackedRGB(sig->descriptor.rgbaSizes) && grSpanPackedRGB(tex + 4)))
            mask |= HC_OPT_SPAN_GRADIENTS;
        if (sig->depthTest && sig->descriptor.colorBytes == 2 &&
            !sig->fogEnabled && !sig->constantDepth &&
            !sig->depthClampMin && !sig->depthClampMax &&
            (sig->depthFunc == GR_FUNC_LESS || sig->depthFunc == GR_FUNC_LEQUAL))
            mask |= HC_OPT_SPAN_GRADIENTS | HC_OPT_DEPTH_REJECT_RUNS;
    }
    return mask;
#else
    return HC_OPT_ALL;
#endif
}

static void grEvaluateEffectivePixelPipeline(Gerbera_s *state, _grSignature *signature)
{
    if (state->constWhiteModulate != 0)
    {
        int savedMode = state->textureUnit[0].texEnvMode;
        state->textureUnit[0].texEnvMode = GR_TEXENVMODE_REPLACE;
        grEvaluatePixelPipeline(state, signature);
        state->textureUnit[0].texEnvMode = savedMode;
    }
    else
    {
        grEvaluatePixelPipeline(state, signature);
    }
}

/* The compressed descriptor occupies 313 bits. Its seven high padding bits
 * distinguish fixed base-level texture dimensions from the generic program. */
enum
{
    GR_TEXTURE_LAYOUT_DIM_COUNT = 11,
    GR_TEXTURE_LAYOUT_KEY_BITS = 7,
    GR_TEXTURE_LAYOUT_KEY_SHIFT = 32 - GR_TEXTURE_LAYOUT_KEY_BITS,
    GR_TEXTURE_LAYOUT_KEY_MASK = (1 << GR_TEXTURE_LAYOUT_KEY_BITS) - 1
};

unsigned int grGetPixelTextureLayout(const Gerbera_s *state)
{
#if GERBERA_ARMV6_JIT
    const _grTexture *texture;
    int width, height;
    if (state == NULL) return 0;
    texture = state->textureUnit[0].texture;
    if (!state->useTexUnit[0] || state->useTexUnit[1] || texture == NULL ||
        (texture->filterMask[GR_FILTERTYPE_MAG] & GR_FILTERMASK_FILTER) ||
        (texture->filterMask[GR_FILTERTYPE_MIN] & GR_FILTERMASK_FILTER) ||
        (texture->wrapMode[0] != GR_WRAPMODE_REPEAT && texture->wrapMode[0] != GR_WRAPMODE_MIRRORED_REPEAT) ||
        (texture->wrapMode[1] != GR_WRAPMODE_REPEAT && texture->wrapMode[1] != GR_WRAPMODE_MIRRORED_REPEAT) ||
        (texture->filterMask[GR_FILTERTYPE_MIN] & GR_FILTERMASK_USE_MIPMAPS))
        return 0;
    width = texture->mipmap[0].log2Dim[0];
    height = texture->mipmap[0].log2Dim[1];
    if ((unsigned int)width >= GR_TEXTURE_LAYOUT_DIM_COUNT ||
        (unsigned int)height >= GR_TEXTURE_LAYOUT_DIM_COUNT)
        return 0;
    return (1u + GR_TEXTURE_LAYOUT_DIM_COUNT * (unsigned int)width + (unsigned int)height) |
        ((texture->wrapMode[0] == GR_WRAPMODE_MIRRORED_REPEAT) << GR_TEXTURE_LAYOUT_KEY_BITS) |
        ((texture->wrapMode[1] == GR_WRAPMODE_MIRRORED_REPEAT) << (GR_TEXTURE_LAYOUT_KEY_BITS + 1));
#else
    (void)state;
    return 0;
#endif
}

static unsigned int grCompressEffectiveSignature(Gerbera_s *state,
                                                  _grCacheSignature *compressed,
                                                  _grSignature *signature)
{
    unsigned int layout = grGetPixelTextureLayout(state);
    unsigned int last;
    grCompressSignature(compressed, signature);
    memcpy(&last, compressed->bytes + 36, sizeof(last));
    last |= (layout & GR_TEXTURE_LAYOUT_KEY_MASK) << GR_TEXTURE_LAYOUT_KEY_SHIFT;
    memcpy(compressed->bytes + 36, &last, sizeof(last));
    return layout;
}

static void grSpecializeTextureLayout(HCInstr_s *program, unsigned int layout,
                                      const _grSignature *signature)
{
    /* Byte offsets in the 32-bit HC parameter block, independent of host ABI. */
    enum { TEX_S_MASK = 16, TEX_T_MASK = 20, TEX_S_SHIFT = 24,
           TEX_T_SHIFT = 28, TEX_WIDTH_LOG2 = 32 };
    layout &= GR_TEXTURE_LAYOUT_KEY_MASK;
    if (layout != 0)
    {
        int width = (int)((layout - 1) / GR_TEXTURE_LAYOUT_DIM_COUNT);
        int height = (int)((layout - 1) % GR_TEXTURE_LAYOUT_DIM_COUNT);
        int sShift = signature->texture[0][21] == GR_WRAPMODE_REPEAT ? 32 : 31;
        int tShift = signature->texture[0][22] == GR_WRAPMODE_REPEAT ? 32 : 31;
        HC_setConstantParameter(program, TEX_S_MASK, (1 << width) - 1);
        HC_setConstantParameter(program, TEX_T_MASK, (1 << height) - 1);
        HC_setConstantParameter(program, TEX_S_SHIFT, width ? sShift - width : 0);
        HC_setConstantParameter(program, TEX_T_SHIFT, height ? tShift - height : 0);
        HC_setConstantParameter(program, TEX_WIDTH_LOG2, width);
    }
}

/* --- grEnsureValidPixelPipeline (grPixelPipeline.inl 1080; DLL 37112-37279) ----
 * Evaluates + compresses the signature, looks the program up in the
 * pipeline cache and, on a hit, publishes the entry's gradient mask. On a
 * miss it emits supported ARM scanlines directly or runs the HC program
 * generator and compiler, then inserts the result. An allocation or compilation failure
 * leaves compilationFailed set and returns without publishing a program. */

void grEnsureValidPixelPipeline(Gerbera_s *state)
{
    _grSignature signature;
    _grCacheSignature compressed;
    unsigned char paramMask[16];
    void *entry;
    unsigned int gradientMask = 0;
    unsigned int textureLayout;
    unsigned int magic0 = GR_STACK_CHECK_MAGIC0;
    unsigned int magic1 = GR_STACK_CHECK_MAGIC1;

    grEvaluateEffectivePixelPipeline(state, &signature);
    textureLayout = grCompressEffectiveSignature(state, &compressed, &signature);
    entry = grCacheFind(state->pipelineCache, &compressed, &gradientMask);
    if (entry == NULL)
    {
        HCBackend_s *backend = (HCBackend_s *)grGetBackend();
        void *code = NULL;
        int lengthInBytes;
        int g;
        int direct = 0;

        state->compilationFailed = 1;
        if (state->scratchMemory == NULL)
        {
            if (state->scratchMemorySize != 0 &&
                grAssert("!state->scratchMemorySize",
                         "grPixelPipeline.inl", 1099) != 0)
                grDie(0);
            grScratchGrowMemory(state);
            if (state->scratchMemory == NULL)
            {
                if ((magic0 != (unsigned int)GR_STACK_CHECK_MAGIC0 ||
                     magic1 != (unsigned int)GR_STACK_CHECK_MAGIC1) &&
                    grAssert(
                        "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                        "grPixelPipeline.inl", 1105) != 0)
                    grDie(0);
                return;
            }
        }

        if (backend == HCARMBackend_create())
        {
            lengthInBytes = grCompileARMPixelPipeline(state->scratchMemory, state->scratchMemorySize,
                                                      &signature, textureLayout, &gradientMask);
            if (lengthInBytes > 0)
            {
                code = state->scratchMemory;
                state->compilationFailed = 0;
                direct = 1;
            }
        }

        while (!direct)
        {
            char *scratch = (char *)state->scratchMemory;
            int scratchSize = state->scratchMemorySize;
            HCInstr_s *hcTmpCode = (HCInstr_s *)scratch;
            int instrCount = grExecuteProgram(hcTmpCode, &signature,
                                              scratchSize >> 3);

            scratch += 8 * instrCount;
            scratchSize -= 8 * instrCount;
            if (!(scratch != NULL && scratchSize > 0 && instrCount > 0 && hcTmpCode != NULL) &&
                grAssert("scratch && scratchSize > 0 && instrCount > 0 && hcTmpCode",
                         "grPixelPipeline.inl", 1124) != 0)
                grDie(0);
            if (((uintptr_t)scratch & 3u) != 0 &&
                grAssert("!((HGuint32)scratch & (GR_MEMORY_ALIGNMENT-1))",
                         "grPixelPipeline.inl", 1125) != 0)
                grDie(0);

            grSpecializeTextureLayout(hcTmpCode, textureLayout, &signature);
            lengthInBytes = HC_compile(
                scratch,
                scratchSize,
                (HCSetjmpConfig_s *)state->setjmpConfig,
                &code,
                hcTmpCode,
                backend,
                grPixelPipelineOptimizationMask(&signature),
                paramMask,
                (unsigned char *)g_mutableParamsMask,
                78);
            if (lengthInBytes != -1)
            {
                state->compilationFailed = 0;
                break;
            }
            if (grScratchGrowMemory(state) == 0)
                break;
        }

        if (state->compilationFailed != 0)
        {
            if ((magic0 != (unsigned int)GR_STACK_CHECK_MAGIC0 ||
                 magic1 != (unsigned int)GR_STACK_CHECK_MAGIC1) &&
                grAssert(
                    "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                    "grPixelPipeline.inl", 1149) != 0)
                grDie(0);
            return;
        }
        if (lengthInBytes <= 0 &&
            grAssert("lengthInBytes > 0", "grPixelPipeline.inl", 1153) != 0)
            grDie(0);

        if (!direct)
        {
            gradientMask = 0;
            for (g = 0; g < 10; ++g)
                gradientMask += (unsigned int)(grIsGradientUsed(paramMask, g) << g);
        }

        entry = grCacheInsert(state->pipelineCache, &compressed,
                              gradientMask, code, lengthInBytes);
    }
    {
        int index = 0;
        int g;

        for (g = 0; g < 10; ++g)
        {
            if (((1 << g) & (int)gradientMask) != 0)
                state->activeGradientIndex[index++] = g;
        }
        state->numActiveGradients = index;
        state->activeGradientMask = (int)gradientMask;
        state->pixelPipelineCode = (grPixelPipelineCode_fn)entry;
        state->pixelTextureLayout = textureLayout;
    }
    if ((magic0 != (unsigned int)GR_STACK_CHECK_MAGIC0 ||
         magic1 != (unsigned int)GR_STACK_CHECK_MAGIC1) &&
        grAssert(
            "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
            "grPixelPipeline.inl", 1196) != 0)
        grDie(0);
}

/* --- grCheckPixelPipeConsistency (grPixelPipeline.inl 1213; DLL 37282-37335) ---
 * Debug check: re-evaluates the signature and confirms the cache still
 * resolves to the active program (and its gradient mask). The decompile
 * keeps the stack canary pair past the locals. */

void grCheckPixelPipeConsistency(Gerbera_s *state)
{
#if HG_ASSERT_ENABLED
    unsigned int magic0 = GR_STACK_CHECK_MAGIC0;
    unsigned int magic1 = GR_STACK_CHECK_MAGIC1;

    (void)magic0;
    (void)magic1;
    if (state->pixelPipelineDirty == 0)
    {
        _grSignature signature;
        _grCacheSignature compressed;
        unsigned int gradientMask = 0;
        void *code;

        grEvaluateEffectivePixelPipeline(state, &signature);
        grCompressEffectiveSignature(state, &compressed, &signature);
        code = grCacheFind(state->pipelineCache, &compressed, &gradientMask);
        if ((void *)code != (void *)state->pixelPipelineCode &&
            grAssert("pixelPipelineCode == state->pixelPipelineCode",
                     "grPixelPipeline.inl", 1235) != 0)
            grDie(0);
        if (!((void *)code == NULL ||
              gradientMask == (unsigned int)state->activeGradientMask) &&
            grAssert("pixelPipelineCode ? (gradientMask == state->activeGradientMask) : HG_TRUE",
                     "grPixelPipeline.inl", 1236) != 0)
            grDie(0);
    }
    if ((magic0 != (unsigned int)GR_STACK_CHECK_MAGIC0 ||
         magic1 != (unsigned int)GR_STACK_CHECK_MAGIC1) &&
        grAssert(
            "__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
            "grPixelPipeline.inl", 1239) != 0)
        grDie(0);
#else
    (void)state;
#endif
}
