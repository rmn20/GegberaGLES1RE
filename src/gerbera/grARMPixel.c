#include "gerbera/grARMPixel.h"
#include <stddef.h>
#include <string.h>

#if GERBERA_ARMV6_JIT

#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _grArmPixel_check_lin[(offsetof(_grPixelPipe, textureUnit[0].linear) == 12) ? 1 : -1];
typedef char _grArmPixel_check_sm[(offsetof(_grPixelPipe, textureUnit[0].sMask) == 16) ? 1 : -1];
typedef char _grArmPixel_check_tm[(offsetof(_grPixelPipe, textureUnit[0].tMask) == 20) ? 1 : -1];
typedef char _grArmPixel_check_fog[(offsetof(_grPixelPipe, checked.fogColor) == 144) ? 1 : -1];
typedef char _grArmPixel_check_alp[(offsetof(_grPixelPipe, checked.alphaReference) == 148) ? 1 : -1];
typedef char _grArmPixel_check_xadd[(offsetof(_grPixelPipe, xAdder) == 232) ? 1 : -1];
#endif

enum
{
    PA_AND = 0, PA_SUB = 2, PA_RSB = 3, PA_ADD = 4, PA_TST = 8, PA_CMP = 10,
    PA_ORR = 12, PA_MOV = 13, PA_BIC = 14,
    PA_LSL = 0, PA_LSR = 1, PA_ASR = 2, PA_ROR = 3,
    PA_EQ = 0, PA_NE = 1, PA_HS = 2, PA_LO = 3, PA_HI = 8, PA_LS = 9, PA_GE = 10, PA_AL = 14,
    PA_TEXTURE = 4, PA_PALETTE = 8, PA_S_MASK = 16, PA_T_MASK = 20, PA_S_SHIFT = 24, PA_T_SHIFT = 28,
    PA_WIDTH_LOG = 32, PA_DITHER = 128, PA_COLOR_STRIDE = 132, PA_DEPTH_STRIDE = 136,
    PA_FOG_COLOR = 144, PA_ALPHA_REF = 148,
    PA_COLOR_ROW = 168, PA_DEPTH_ROW = 172, PA_ROWS = 180, PA_SPANS = 184, PA_Y = 188,
    PA_REFERENCE = 192, PA_X_ADDER = 232, PA_Y_ADDER = 272,
    PA_LAYOUT_DIM_COUNT = GR_MAX_TEXTURE_DIMENSION_LOG2 + 1,
    PA_LAYOUT_COUNT = PA_LAYOUT_DIM_COUNT * PA_LAYOUT_DIM_COUNT
};

enum
{
    PA_BLEND_NONE = 0,
    PA_BLEND_SRC_ALPHA,
    PA_BLEND_ZERO_SRC_COLOR,
    PA_BLEND_DST_COLOR_ZERO,
    PA_BLEND_ONE_ONE,
    PA_BLEND_SRC_ALPHA_ONE
};

/* Byte fields in each signature texture record, after its surface descriptor. */
enum
{
    PA_TEX_ENABLED = 16, PA_TEX_MAG_LINEAR = 19, PA_TEX_MIN_LINEAR = 20,
    PA_TEX_WRAP_S = 21, PA_TEX_WRAP_T = 22, PA_TEX_RAW_COPY = 23,
    PA_TEX_RGB_SOURCE0 = 24, PA_TEX_ALPHA_SOURCE0 = 25,
    PA_TEX_RGB_OPERAND0 = 26, PA_TEX_ALPHA_OPERAND0 = 27,
    PA_TEX_RGB_SOURCE1 = 28, PA_TEX_ALPHA_SOURCE1 = 29,
    PA_TEX_RGB_OPERAND1 = 30, PA_TEX_ALPHA_OPERAND1 = 31,
    PA_TEX_RGB_FUNCTION = 36, PA_TEX_ALPHA_FUNCTION = 37,
    PA_TEX_RGB_SCALE = 38, PA_TEX_ALPHA_SCALE = 39
};

enum
{
    PA_RETURN = 0,
    PA_G_ROW, PA_G_PIXEL, PA_G_SHADE, PA_G_ROW_END, PA_G_REJECT, PA_G_REJECT_LOOP, PA_G_ALPHA_REJECT, PA_G_SPAN,
    PA_C_ROW, PA_C_PIXEL, PA_C_SHADE, PA_C_ROW_END, PA_C_REJECT, PA_C_REJECT_LOOP, PA_C_ALPHA_REJECT, PA_C_SPAN,
    PA_W_ROW, PA_W_PIXEL, PA_W_SHADE, PA_W_ROW_END, PA_W_REJECT, PA_W_REJECT_LOOP, PA_W_ALPHA_REJECT, PA_W_SPAN,
    PA_W_CHECK_256, PA_W_INIT_255, PA_W_INIT_256,
    PA_LABEL_COUNT
};

typedef struct ARMPixelCode_s
{
    unsigned int *words;
    int count, capacity;
    int labels[PA_LABEL_COUNT];
    int branches[192], targets[192], numBranches;
} ARMPixelCode_s;

static void pixelArmWord(ARMPixelCode_s *c, unsigned int word)
{
    if (c->count < c->capacity) c->words[c->count] = word;
    ++c->count;
}

static unsigned int pixelArmShift(int reg, int kind, int amount)
{
    return (unsigned int)reg | ((unsigned int)kind << 5) | ((unsigned int)(amount & 31) << 7);
}

static unsigned int pixelArmShiftReg(int reg, int kind, int shiftReg)
{
    return (unsigned int)reg | 0x10u | ((unsigned int)kind << 5) | ((unsigned int)shiftReg << 8);
}

static unsigned int pixelArmImmediate(unsigned int value)
{
    unsigned int i;
    for (i = 0; i < 16; ++i)
    {
        if (value <= 255u) return 0x02000000u | (i << 8) | value;
        value = (value << 2) | (value >> 30);
    }
    return 0;
}

static void pixelArmOp(ARMPixelCode_s *c, int op, int dst, int src,
                       unsigned int rhs, int flags)
{
    pixelArmWord(c, 0xE0000000u | ((unsigned int)op << 21) | ((unsigned int)flags << 20) |
                 ((unsigned int)src << 16) | ((unsigned int)dst << 12) | rhs);
}

static void pixelArmLoad(ARMPixelCode_s *c, int dst, int base, int offset)
{
    pixelArmWord(c, (offset < 0 ? 0xE5100000u : 0xE5900000u) |
                 ((unsigned int)base << 16) | ((unsigned int)dst << 12) |
                 (unsigned int)(offset < 0 ? -offset : offset));
}

static void pixelArmStore(ARMPixelCode_s *c, int src, int base, int offset)
{
    pixelArmWord(c, (offset < 0 ? 0xE5000000u : 0xE5800000u) |
                 ((unsigned int)base << 16) | ((unsigned int)src << 12) |
                 (unsigned int)(offset < 0 ? -offset : offset));
}

static void pixelArmMul(ARMPixelCode_s *c, int dst, int a, int b)
{
    pixelArmWord(c, 0xE0000090u | ((unsigned int)dst << 16) |
                 ((unsigned int)b << 8) | (unsigned int)a);
}

static void pixelArmMLA(ARMPixelCode_s *c, int dst, int a, int b, int add)
{
    pixelArmWord(c, 0xE0200090u | ((unsigned int)dst << 16) | ((unsigned int)add << 12) |
                 ((unsigned int)b << 8) | (unsigned int)a);
}

static void pixelArmUXTB(ARMPixelCode_s *c, int dst, int src, int bitOffset)
{
    pixelArmWord(c, 0xE6EF0070u | ((unsigned int)(bitOffset / 8) << 10) |
                 ((unsigned int)dst << 12) | (unsigned int)src);
}

static void pixelArmUXTB16(ARMPixelCode_s *c, int dst, int src, int bitOffset)
{
    pixelArmWord(c, 0xE6CF0070u | ((unsigned int)(bitOffset / 8) << 10) |
                 ((unsigned int)dst << 12) | (unsigned int)src);
}

static void pixelArmPKHBT(ARMPixelCode_s *c, int dst, int loSrc, int hiSrc, int lslAmt)
{
    pixelArmWord(c, 0xE6800010u | ((unsigned int)loSrc << 16) | ((unsigned int)dst << 12) |
                 ((unsigned int)(lslAmt & 31) << 7) | (unsigned int)hiSrc);
}

static void pixelArmUSAT8(ARMPixelCode_s *c, int dst, int src, int asrAmt)
{
    pixelArmWord(c, (asrAmt ? 0xE6E80050u : 0xE6E80010u) | ((unsigned int)dst << 12) |
                 ((unsigned int)(asrAmt & 31) << 7) | (unsigned int)src);
}

static void pixelArmREV(ARMPixelCode_s *c, int dst, int src)
{
    pixelArmWord(c, 0xE6BF0F30u | ((unsigned int)dst << 12) | (unsigned int)src);
}

static void pixelArmUQADD8(ARMPixelCode_s *c, int dst, int lhs, int rhs)
{
    pixelArmWord(c, 0xE6600F90u | ((unsigned int)lhs << 16) |
                 ((unsigned int)dst << 12) | (unsigned int)rhs);
}

static void pixelArmLoadImm32(ARMPixelCode_s *c, int dst, unsigned int value)
{
    int started = 0;
    int shift;
    if (pixelArmImmediate(value) != 0)
    {
        pixelArmOp(c, PA_MOV, dst, 0, pixelArmImmediate(value), 0);
        return;
    }
    for (shift = 0; shift < 32; shift += 8)
    {
        unsigned int part = value & (0xFFu << shift);
        if (!part) continue;
        pixelArmOp(c, started ? PA_ORR : PA_MOV, dst, started ? dst : 0,
                   pixelArmImmediate(part), 0);
        started = 1;
    }
    if (!started)
        pixelArmOp(c, PA_MOV, dst, 0, pixelArmImmediate(0), 0);
}

static void pixelArmBranch(ARMPixelCode_s *c, int condition, int label)
{
    c->branches[c->numBranches] = c->count;
    c->targets[c->numBranches++] = label;
    pixelArmWord(c, ((unsigned int)condition << 28) | 0x0A000000u);
}

static int pixelArmByteChannels(const _grSurfaceDescriptor *d, int alpha)
{
    int channel;
    unsigned int occupied = 0;
    for (channel = 0; channel < (alpha ? 4 : 3); ++channel)
    {
        unsigned int bit;
        if (d->rgbaSizes[channel] != 8 || d->rgbaOffsets[channel] > 24 ||
            (d->rgbaOffsets[channel] & 7)) return 0;
        bit = 1u << (d->rgbaOffsets[channel] >> 3);
        if (occupied & bit) return 0;
        occupied |= bit;
    }
    return d->colorBytes == 4;
}

static int pixelArmAlphaRejectCond(int alphaFunc)
{
    switch (alphaFunc)
    {
        case GR_FUNC_LESS: return PA_HS;
        case GR_FUNC_EQUAL: return PA_NE;
        case GR_FUNC_LEQUAL: return PA_HI;
        case GR_FUNC_GREATER: return PA_LS;
        case GR_FUNC_NOTEQUAL: return PA_EQ;
        case GR_FUNC_GEQUAL: return PA_LO;
        default: return -1;
    }
}

static void pixelArmGradientEx(ARMPixelCode_s *c, int gradient, int dst, int origin,
                               int scratchOffset, int tmpReg)
{
    pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * gradient);
    pixelArmLoad(c, tmpReg, 0, PA_X_ADDER + 4 * gradient);
    pixelArmMLA(c, dst, tmpReg, origin, 1);
    if (scratchOffset) pixelArmStore(c, dst, 0, scratchOffset);
    pixelArmLoad(c, 14, 0, PA_Y_ADDER + 4 * gradient);
    pixelArmOp(c, PA_ADD, 1, 1, 14, 0);
    pixelArmStore(c, 1, 0, PA_REFERENCE + 4 * gradient);
}

static void pixelArmRowPointer(ARMPixelCode_s *c, int dst, int slot, int stride, int shift)
{
    pixelArmLoad(c, dst, 0, slot);
    pixelArmLoad(c, 1, 0, stride);
    pixelArmOp(c, PA_ADD, 1, dst, 1, 0);
    pixelArmStore(c, 1, 0, slot);
    pixelArmOp(c, PA_ADD, dst, dst, pixelArmShift(3, PA_LSL, shift), 0);
}

static void pixelArmTextureIndex(ARMPixelCode_s *c, unsigned int layout, int packedInR12)
{
    if (layout)
    {
        int width = (int)((layout - 1) / PA_LAYOUT_DIM_COUNT);
        int height = (int)((layout - 1) % PA_LAYOUT_DIM_COUNT);
        pixelArmOp(c, PA_MOV, 1, 0, height ? pixelArmShift(6, PA_LSR, 32 - height) :
                   pixelArmImmediate(0), 0);
        if (width) pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, width), 0);
        pixelArmOp(c, width ? PA_ORR : PA_MOV, 2, width ? 1 : 0,
                   width ? pixelArmShift(5, PA_LSR, 32 - width) : 1, 0);
    }
    else if (packedInR12)
    {
        pixelArmOp(c, PA_MOV, 2, 0, 5u | 0x30u | (12u << 8), 0);
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 8), 0);
        pixelArmOp(c, PA_MOV, 3, 0, 6u | 0x30u | (1u << 8), 0);
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 16), 0);
        pixelArmOp(c, PA_ORR, 2, 2, 3u | 0x10u | (1u << 8), 0);
    }
    else
    {
        pixelArmLoad(c, 1, 0, PA_S_SHIFT);
        pixelArmOp(c, PA_MOV, 2, 0, 5u | 0x30u | (1u << 8), 0);
        pixelArmLoad(c, 1, 0, PA_T_SHIFT);
        pixelArmOp(c, PA_MOV, 3, 0, 6u | 0x30u | (1u << 8), 0);
        pixelArmLoad(c, 1, 0, PA_WIDTH_LOG);
        pixelArmOp(c, PA_ORR, 2, 2, 3u | 0x10u | (1u << 8), 0);
    }
}

static void pixelArmPackChannel(ARMPixelCode_s *c, const _grSurfaceDescriptor *dest,
                                int channel, int srcReg, int outReg, int dither)
{
    int bits = dest->rgbaSizes[channel], shift = dest->rgbaOffsets[channel];
    if (dither)
    {
        pixelArmOp(c, PA_SUB, srcReg, srcReg, pixelArmShift(srcReg, PA_LSR, bits), 0);
        pixelArmOp(c, PA_ADD, srcReg, srcReg, pixelArmShift(10, PA_LSR, 24 + bits), 0);
    }
    if (bits < 8 && shift != 0)
        pixelArmOp(c, PA_MOV, srcReg, 0, pixelArmShift(srcReg, PA_LSR, 8 - bits), 0);
    if (channel == 0 && outReg == srcReg && shift == 0 && bits == 8)
        return;
    pixelArmOp(c, channel ? PA_ORR : PA_MOV, outReg, channel ? outReg : 0,
               pixelArmShift(srcReg, shift || bits == 8 ? PA_LSL : PA_LSR,
                             shift ? shift : 8 - bits), 0);
}

static void pixelArmPackRGB32(ARMPixelCode_s *c, const _grSurfaceDescriptor *dest,
                              int regR, int regG, int regB, int outReg, int dither)
{
    if (!dither && dest->rgbaSizes[0] == 8 && dest->rgbaSizes[1] == 8 && dest->rgbaSizes[2] == 8 &&
        dest->rgbaOffsets[1] == 8 && outReg != regG)
    {
        if (dest->rgbaOffsets[0] == 16 && dest->rgbaOffsets[2] == 0)
        {
            pixelArmOp(c, PA_ORR, outReg, regB, pixelArmShift(regR, PA_LSL, 16), 0);
            pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(regG, PA_LSL, 8), 0);
            return;
        }
        if (dest->rgbaOffsets[0] == 0 && dest->rgbaOffsets[2] == 16 && outReg != regB)
        {
            pixelArmOp(c, PA_ORR, outReg, regR, pixelArmShift(regB, PA_LSL, 16), 0);
            pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(regG, PA_LSL, 8), 0);
            return;
        }
    }
    pixelArmPackChannel(c, dest, 0, regR, outReg, dither);
    pixelArmPackChannel(c, dest, 1, regG, outReg, dither);
    pixelArmPackChannel(c, dest, 2, regB, outReg, dither);
}

static void pixelArmComponent(ARMPixelCode_s *c, const _grSurfaceDescriptor *texture,
                              const _grSurfaceDescriptor *dest, int channel,
                              int modulate, int dither, int depthStep, int texReg,
                              int outReg)
{
    if (modulate)
    {
        pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * (GR_GRADIENT_COLOR_R + channel));
        pixelArmLoad(c, 3, 0, -4 * (channel + 1));
    }
    pixelArmUXTB(c, 1, texReg, texture->rgbaOffsets[channel]);
    if (depthStep) pixelArmOp(c, PA_ADD, 7, 7, (unsigned int)depthStep, 0);
    if (modulate)
    {
        pixelArmMLA(c, 2, 2, 4, 3);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
        pixelArmMLA(c, 1, 1, 2, 2);
        pixelArmUSAT8(c, 1, 1, 8);
    }
    pixelArmPackChannel(c, dest, channel, 1, outReg, dither);
}

static void pixelArmStepSTZ(ARMPixelCode_s *c, int depth, int dither, int keepSTAdders,
                            int incFog, int preserveReg1, int preserveReg2, int clobberZReg)
{
    int tA = (preserveReg1 == 1 || preserveReg2 == 1) ? 2 : 1;
    int tB = (preserveReg1 == 3 || preserveReg2 == 3 || tA == 3) ? 2 : 3;
    if (tB == tA) tB = 14;
    if (keepSTAdders)
    {
        if (depth)
        {
            if (dither)
            {
                pixelArmLoad(c, clobberZReg, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
                pixelArmOp(c, PA_ADD, 7, 7, (unsigned int)clobberZReg, 0);
            }
            else
            {
                pixelArmOp(c, PA_ADD, 7, 7, 10, 0);
            }
        }
        pixelArmOp(c, PA_ADD, 5, 5, 12, 0);
        pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
    }
    else if (!depth && !incFog)
    {
        pixelArmOp(c, PA_ADD, 5, 5, 7, 0);
        pixelArmOp(c, PA_ADD, 6, 6, 9, 0);
    }
    else if (!depth && incFog)
    {
        pixelArmLoad(c, tA, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmLoad(c, tB, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        pixelArmOp(c, PA_ADD, 5, 5, tA, 0);
        pixelArmOp(c, PA_ADD, 6, 6, tB, 0);
    }
    else
    {
        pixelArmLoad(c, tA, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmLoad(c, tB, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        pixelArmOp(c, PA_ADD, 5, 5, tA, 0);
        if (dither)
            pixelArmLoad(c, clobberZReg, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
        pixelArmOp(c, PA_ADD, 6, 6, tB, 0);
        pixelArmOp(c, PA_ADD, 7, 7, dither ? (unsigned int)clobberZReg : 10u, 0);
    }
}

static void pixelArmEmitDitherRow(ARMPixelCode_s *c)
{
    pixelArmLoad(c, 1, 0, PA_Y);
    pixelArmOp(c, PA_AND, 2, 1, pixelArmImmediate(3), 0);
    pixelArmOp(c, PA_ADD, 1, 1, pixelArmImmediate(1), 0);
    pixelArmLoad(c, 14, 0, PA_DITHER);
    pixelArmStore(c, 1, 0, PA_Y);
    pixelArmWord(c, 0xE79EA102u); /* LDR r10, [r14, r2, LSL #2]. */
    pixelArmOp(c, PA_AND, 1, 3, pixelArmImmediate(3), 0);
    pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 3), 0);
    pixelArmOp(c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
}

static void pixelArmEmitAlphaScratch(ARMPixelCode_s *c, const _grSurfaceDescriptor *tex,
                                     int alphaMode, int texReg, int isConstRgb, int isConstWhite,
                                     int keepInR3)
{
    int dstReg = keepInR3 ? 3 : ((texReg == 1) ? 14 : 1);
    int tmpReg = keepInR3 ? 14 : 3;

    if (alphaMode == 1 || (isConstWhite && alphaMode == 3))
    {
        pixelArmUXTB(c, dstReg, texReg, tex->rgbaOffsets[3]);
    }
    else if (alphaMode == 2)
    {
        if (isConstWhite)
        {
            pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmImmediate(255u), 0);
        }
        else if (isConstRgb)
        {
            pixelArmLoad(c, 2, 0, -4);
            pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(2, PA_LSR, 24), 0);
        }
        else
        {
            pixelArmLoad(c, dstReg, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
            pixelArmLoad(c, tmpReg, 0, -16);
            pixelArmMLA(c, dstReg, dstReg, 4, tmpReg);
            pixelArmUSAT8(c, dstReg, dstReg, 16);
        }
    }
    else
    {
        pixelArmUXTB(c, dstReg, texReg, tex->rgbaOffsets[3]);
        if (isConstRgb)
        {
            pixelArmLoad(c, 2, 0, -4);
            pixelArmOp(c, PA_MOV, tmpReg, 0, pixelArmShift(2, PA_LSR, 24), 0);
            pixelArmMLA(c, dstReg, dstReg, tmpReg, tmpReg);
            pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(dstReg, PA_LSR, 8), 0);
        }
        else
        {
            pixelArmLoad(c, tmpReg, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
            pixelArmLoad(c, 2, 0, -16);
            pixelArmMLA(c, tmpReg, tmpReg, 4, 2);
            pixelArmOp(c, PA_MOV, tmpReg, 0, pixelArmShift(tmpReg, PA_LSR, 16), 0);
            pixelArmMLA(c, dstReg, dstReg, tmpReg, tmpReg);
            pixelArmUSAT8(c, dstReg, dstReg, 8);
        }
    }
    if (!keepInR3)
        pixelArmStore(c, dstReg, 0, -24);
}

static int pixelArmClassifyBlend(const _grSignature *sig)
{
    if (!sig->blendEnabled)
        return PA_BLEND_NONE;
    if (sig->srcFactor == GR_BLENDFACTOR_SRC_ALPHA &&
        sig->dstFactor == GR_BLENDFACTOR_ONE_MINUS_SRC_ALPHA)
        return PA_BLEND_SRC_ALPHA;
    if (sig->srcFactor == GR_BLENDFACTOR_ZERO &&
        sig->dstFactor == GR_BLENDFACTOR_SRC_COLOR)
        return PA_BLEND_ZERO_SRC_COLOR;
    if (sig->srcFactor == GR_BLENDFACTOR_DST_COLOR &&
        sig->dstFactor == GR_BLENDFACTOR_ZERO)
        return PA_BLEND_DST_COLOR_ZERO;
    if (sig->srcFactor == GR_BLENDFACTOR_ONE &&
        sig->dstFactor == GR_BLENDFACTOR_ONE)
        return PA_BLEND_ONE_ONE;
    if (sig->srcFactor == GR_BLENDFACTOR_SRC_ALPHA &&
        sig->dstFactor == GR_BLENDFACTOR_ONE)
        return PA_BLEND_SRC_ALPHA_ONE;
    return -1;
}

static void pixelArmEmitBilinearTexel(ARMPixelCode_s *c, const _grSurfaceDescriptor *tex,
                                      unsigned int textureLayout, int texReg, int clearByte3)
{
    /* Compute bilinearly filtered texel into texReg using r1, r2, r3, r12, r14 and [r0, #-32..-36].
     * Preserves r4, r5, r6, r7, r8, r9, r10, r11. */
    int width = textureLayout ? (int)((textureLayout - 1) / PA_LAYOUT_DIM_COUNT) : 0;
    int height = textureLayout ? (int)((textureLayout - 1) % PA_LAYOUT_DIM_COUNT) : 0;
    int pass;

    if (textureLayout)
    {
        if (width == 0)
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmImmediate(0), 0);
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmImmediate(0), 0);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(5, PA_LSR, 32 - width), 0);
            pixelArmOp(c, PA_ADD, 2, 1, pixelArmImmediate(1), 0);
            pixelArmOp(c, PA_BIC, 2, 2, pixelArmImmediate(1u << width), 0);
        }
        if (height == 0)
        {
            pixelArmStore(c, 1, 0, -32);
            pixelArmStore(c, 2, 0, -36);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(6, PA_LSR, 32 - height), 0);
            pixelArmOp(c, PA_ADD, 12, 3, pixelArmImmediate(1), 0);
            pixelArmOp(c, PA_BIC, 12, 12, pixelArmImmediate(1u << height), 0);
            if (width == 0)
            {
                pixelArmStore(c, 12, 0, -32);
                pixelArmStore(c, 12, 0, -36);
                pixelArmOp(c, PA_MOV, 1, 0, 3, 0);
                pixelArmOp(c, PA_MOV, 2, 0, 3, 0);
            }
            else
            {
                pixelArmOp(c, PA_ORR, 14, 1, pixelArmShift(12, PA_LSL, width), 0);
                pixelArmStore(c, 14, 0, -32);
                pixelArmOp(c, PA_ORR, 12, 2, pixelArmShift(12, PA_LSL, width), 0);
                pixelArmStore(c, 12, 0, -36);
                pixelArmOp(c, PA_ORR, 1, 1, pixelArmShift(3, PA_LSL, width), 0);
                pixelArmOp(c, PA_ORR, 2, 2, pixelArmShift(3, PA_LSL, width), 0);
            }
        }
    }
    else
    {
        pixelArmLoad(c, 14, 0, PA_S_SHIFT);
        pixelArmLoad(c, 12, 0, PA_S_MASK);
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShiftReg(5, PA_LSR, 14), 0);
        pixelArmOp(c, PA_ADD, 2, 1, pixelArmImmediate(1), 0);
        pixelArmOp(c, PA_AND, 1, 1, 12, 0);
        pixelArmOp(c, PA_AND, 2, 2, 12, 0);
        pixelArmLoad(c, 14, 0, PA_T_SHIFT);
        pixelArmLoad(c, 12, 0, PA_T_MASK);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(6, PA_LSR, 14), 0);
        pixelArmOp(c, PA_ADD, 14, 3, pixelArmImmediate(1), 0);
        pixelArmOp(c, PA_AND, 3, 3, 12, 0);
        pixelArmOp(c, PA_AND, 12, 14, 12, 0);
        pixelArmLoad(c, 14, 0, PA_WIDTH_LOG);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(3, PA_LSL, 14), 0);
        pixelArmOp(c, PA_MOV, 12, 0, pixelArmShiftReg(12, PA_LSL, 14), 0);
        pixelArmOp(c, PA_ORR, 14, 1, 12, 0);
        pixelArmStore(c, 14, 0, -32);
        pixelArmOp(c, PA_ORR, 12, 2, 12, 0);
        pixelArmStore(c, 12, 0, -36);
        pixelArmOp(c, PA_ORR, 1, 1, 3, 0);
        pixelArmOp(c, PA_ORR, 2, 2, 3, 0);
    }

    for (pass = 0; pass < 2; ++pass)
    {
        if (tex->paletteFormat == GR_PALETTEFORMAT_8BIT)
        {
            pixelArmWord(c, 0xE7DB1001u); /* LDRB r1, [r11, r1] */
            pixelArmWord(c, 0xE7DB2002u); /* LDRB r2, [r11, r2] */
            if (pass == 0)
            {
                if (textureLayout)
                {
                    if (width == 0)
                    {
                        pixelArmOp(c, PA_MOV, 12, 0, pixelArmImmediate(0), 0);
                    }
                    else
                    {
                        pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(5, PA_LSR, 24 - width), 0);
                        pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
                    }
                }
                else
                {
                    pixelArmLoad(c, 14, 0, PA_S_SHIFT);
                    pixelArmOp(c, PA_SUB, 14, 14, pixelArmImmediate(8), 0);
                    pixelArmOp(c, PA_MOV, 12, 0, pixelArmShiftReg(5, PA_ASR, 14), 0);
                    pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
                }
            }
            pixelArmLoad(c, 14, 0, PA_PALETTE);
            pixelArmWord(c, 0xE79E1101u); /* LDR r1, [r14, r1, LSL #2] */
            pixelArmWord(c, 0xE79E2102u); /* LDR r2, [r14, r2, LSL #2] */
        }
        else
        {
            pixelArmWord(c, 0xE79B1101u); /* LDR r1, [r11, r1, LSL #2] */
            pixelArmWord(c, 0xE79B2102u); /* LDR r2, [r11, r2, LSL #2] */
            if (pass == 0)
            {
                if (textureLayout)
                {
                    if (width == 0)
                    {
                        pixelArmOp(c, PA_MOV, 12, 0, pixelArmImmediate(0), 0);
                    }
                    else
                    {
                        pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(5, PA_LSR, 24 - width), 0);
                        pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
                    }
                }
                else
                {
                    pixelArmLoad(c, 14, 0, PA_S_SHIFT);
                    pixelArmOp(c, PA_SUB, 14, 14, pixelArmImmediate(8), 0);
                    pixelArmOp(c, PA_MOV, 12, 0, pixelArmShiftReg(5, PA_ASR, 14), 0);
                    pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
                }
            }
        }
        pixelArmOp(c, PA_RSB, 14, 12, pixelArmImmediate(256u), 0);
        pixelArmUXTB16(c, 3, 1, 0);
        pixelArmMul(c, 3, 3, 14);
        if (clearByte3)
            pixelArmUXTB(c, 1, 1, 8);
        else
            pixelArmUXTB16(c, 1, 1, 8);
        pixelArmMul(c, 1, 1, 14);
        pixelArmUXTB16(c, 14, 2, 0);
        pixelArmMLA(c, 3, 14, 12, 3);
        if (clearByte3)
            pixelArmUXTB(c, 14, 2, 8);
        else
            pixelArmUXTB16(c, 14, 2, 8);
        pixelArmMLA(c, 1, 14, 12, 1);
        pixelArmUXTB16(c, 3, 3, 8);
        if (clearByte3)
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
        else
            pixelArmUXTB16(c, 1, 1, 8);
        if (pass == 0)
        {
            pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(1, PA_LSL, 8), 0);
            pixelArmLoad(c, 1, 0, -32);
            pixelArmLoad(c, 2, 0, -36);
            pixelArmStore(c, 3, 0, -32);
        }
    }

    if (textureLayout)
    {
        if (height == 0)
        {
            pixelArmOp(c, PA_MOV, 12, 0, pixelArmImmediate(0), 0);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(6, PA_LSR, 24 - height), 0);
            pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
        }
    }
    else
    {
        pixelArmLoad(c, 14, 0, PA_T_SHIFT);
        pixelArmOp(c, PA_SUB, 14, 14, pixelArmImmediate(8), 0);
        pixelArmOp(c, PA_MOV, 12, 0, pixelArmShiftReg(6, PA_ASR, 14), 0);
        pixelArmOp(c, PA_AND, 12, 12, pixelArmImmediate(0xFFu), 0);
    }
    pixelArmLoad(c, 2, 0, -32);
    pixelArmOp(c, PA_RSB, 14, 12, pixelArmImmediate(256u), 0);
    pixelArmMul(c, 3, 3, 12);
    pixelArmMul(c, 1, 1, 12);
    pixelArmUXTB16(c, 12, 2, 0);
    pixelArmMLA(c, 3, 12, 14, 3);
    if (clearByte3)
        pixelArmUXTB(c, 12, 2, 8);
    else
        pixelArmUXTB16(c, 12, 2, 8);
    pixelArmMLA(c, 1, 12, 14, 1);
    pixelArmUXTB16(c, 3, 3, 8);
    if (clearByte3)
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
    else
        pixelArmUXTB16(c, 1, 1, 8);
    pixelArmOp(c, PA_ORR, texReg, 3, pixelArmShift(1, PA_LSL, 8), 0);
}

static void pixelArmEmitBlend32SWAR(ARMPixelCode_s *c, int blendMode, int dstAlpha,
                                    int alphaInR3, int preserveR12)
{
    /* Inputs: r1 = srcRB (0x00RR00BB or 0x00BB00RR matching dst R/B offsets), r2 = srcG (0..255),
     * r3 (when alphaInR3) or [r0, #-24] = srcA (0..255), r8 = dst ptr. Output: r3 = blended pixel. */
    if (!alphaInR3)
        pixelArmLoad(c, 3, 0, -24);
    if (dstAlpha)
        pixelArmOp(c, PA_ORR, 2, 2, pixelArmShift(3, PA_LSL, 16), 0);

    if (blendMode == PA_BLEND_SRC_ALPHA_ONE)
    {
        pixelArmMLA(c, 1, 3, 1, 1);
        pixelArmMLA(c, 2, 3, 2, 2);
        pixelArmUXTB16(c, 1, 1, 8);
        if (dstAlpha)
            pixelArmUXTB16(c, 2, 2, 8);
        else
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
        pixelArmOp(c, PA_ORR, 1, 1, pixelArmShift(2, PA_LSL, 8), 0);
        pixelArmWord(c, 0xE5982000u); /* LDR r2, [r8] */
        pixelArmUQADD8(c, 3, 1, 2);
        if (!dstAlpha)
            pixelArmOp(c, PA_BIC, 3, 3, pixelArmImmediate(0xFF000000u), 0);
    }
    else if (preserveR12)
    {
        pixelArmOp(c, PA_RSB, 14, 3, pixelArmImmediate(0x100u), 0);
        pixelArmMLA(c, 1, 3, 1, 1);
        pixelArmMLA(c, 2, 3, 2, 2);
        pixelArmUXTB16(c, 1, 1, 8);
        if (dstAlpha)
            pixelArmUXTB16(c, 2, 2, 8);
        else
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
        pixelArmWord(c, 0xE5983000u); /* LDR r3, [r8] */
        pixelArmStore(c, 1, 0, -32);
        if (dstAlpha)
            pixelArmUXTB16(c, 1, 3, 8);
        else
            pixelArmUXTB(c, 1, 3, 8);
        pixelArmUXTB16(c, 3, 3, 0);
        pixelArmMul(c, 1, 1, 14);
        pixelArmMul(c, 3, 3, 14);
        if (dstAlpha)
        {
            pixelArmUXTB16(c, 1, 1, 8);
            pixelArmOp(c, PA_ADD, 2, 2, 1, 0);
        }
        else
        {
            pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(1, PA_LSR, 8), 0);
        }
        pixelArmUXTB16(c, 3, 3, 8);
        pixelArmLoad(c, 1, 0, -32);
        pixelArmOp(c, PA_ADD, 1, 1, 3, 0);
        pixelArmOp(c, PA_ORR, 3, 1, pixelArmShift(2, PA_LSL, 8), 0);
    }
    else
    {
        pixelArmWord(c, 0xE598C000u); /* LDR ip, [r8] */
        pixelArmOp(c, PA_RSB, 14, 3, pixelArmImmediate(0x100u), 0);
        pixelArmMLA(c, 1, 3, 1, 1);
        pixelArmMLA(c, 2, 3, 2, 2);
        if (dstAlpha)
            pixelArmUXTB16(c, 3, 12, 8);
        else
            pixelArmUXTB(c, 3, 12, 8);
        pixelArmUXTB16(c, 12, 12, 0);
        pixelArmMul(c, 3, 3, 14);
        pixelArmMul(c, 12, 12, 14);
        pixelArmUXTB16(c, 1, 1, 8);
        pixelArmUXTB16(c, 12, 12, 8);
        if (dstAlpha)
        {
            pixelArmUXTB16(c, 2, 2, 8);
            pixelArmUXTB16(c, 3, 3, 8);
            pixelArmOp(c, PA_ADD, 2, 2, 3, 0);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
            pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(3, PA_LSR, 8), 0);
        }
        pixelArmOp(c, PA_ADD, 1, 1, 12, 0);
        pixelArmOp(c, PA_ORR, 3, 1, pixelArmShift(2, PA_LSL, 8), 0);
    }
}

static void pixelArmEmitUnpackRGB565Dst(ARMPixelCode_s *c, int dstReg, int outReg, int channel)
{
    if (channel == 0)
    {
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(dstReg, PA_LSR, 11), 0);
        pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(outReg, PA_LSL, 5), 0);
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(outReg, PA_LSR, 2), 0);
    }
    else if (channel == 1)
    {
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(dstReg, PA_LSR, 5), 0);
        pixelArmOp(c, PA_AND, outReg, outReg, pixelArmImmediate(0x3Fu), 0);
        pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(outReg, PA_LSL, 6), 0);
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(outReg, PA_LSR, 4), 0);
    }
    else
    {
        pixelArmOp(c, PA_AND, outReg, dstReg, pixelArmImmediate(0x1Fu), 0);
        pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(outReg, PA_LSL, 5), 0);
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(outReg, PA_LSR, 2), 0);
    }
}

static void pixelArmEmitBlendGeneral(ARMPixelCode_s *c, const _grSurfaceDescriptor *dst,
                                     int rgb565, int dstAlpha, int dither, int blendMode,
                                     int preserveR12)
{
    /* Inputs: r3 = srcR, r2 = srcG, r1 = srcB (0..255), [r0, #-24] = srcA when needed,
     * r8 = dst ptr. Output: r3 = packed pixel. */
    int ch;
    if (preserveR12)
        pixelArmStore(c, 12, 0, -40);
    pixelArmWord(c, rgb565 ? 0xE1D8C0B0u : 0xE598C000u); /* LDRH/LDR ip, [r8] */

    if (blendMode == PA_BLEND_ZERO_SRC_COLOR || blendMode == PA_BLEND_DST_COLOR_ZERO)
    {
        for (ch = 0; ch < 3; ++ch)
        {
            int srcReg = 3 - ch;
            int addReg = (blendMode == PA_BLEND_ZERO_SRC_COLOR) ? 14 : srcReg;
            if (rgb565)
                pixelArmEmitUnpackRGB565Dst(c, 12, 14, ch);
            else
                pixelArmUXTB(c, 14, 12, dst->rgbaOffsets[ch]);
            pixelArmMLA(c, srcReg, 14, srcReg, addReg);
            pixelArmOp(c, PA_MOV, srcReg, 0, pixelArmShift(srcReg, PA_LSR, 8), 0);
        }
        pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
        if (dstAlpha)
        {
            int addReg;
            pixelArmLoad(c, 14, 0, -24);
            pixelArmUXTB(c, 12, 12, dst->rgbaOffsets[3]);
            addReg = (blendMode == PA_BLEND_ZERO_SRC_COLOR) ? 12 : 14;
            pixelArmMLA(c, 14, 12, 14, addReg);
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 8), 0);
            pixelArmPackChannel(c, dst, 3, 14, 3, 0);
        }
    }
    else if (blendMode == PA_BLEND_ONE_ONE)
    {
        for (ch = 0; ch < 3; ++ch)
        {
            int srcReg = 3 - ch;
            if (rgb565)
                pixelArmEmitUnpackRGB565Dst(c, 12, 14, ch);
            else
                pixelArmUXTB(c, 14, 12, dst->rgbaOffsets[ch]);
            pixelArmOp(c, PA_ADD, srcReg, srcReg, 14, 0);
            pixelArmUSAT8(c, srcReg, srcReg, 0);
        }
        pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
        if (dstAlpha)
        {
            pixelArmLoad(c, 14, 0, -24);
            pixelArmUXTB(c, 12, 12, dst->rgbaOffsets[3]);
            pixelArmOp(c, PA_ADD, 14, 14, 12, 0);
            pixelArmUSAT8(c, 14, 14, 0);
            pixelArmPackChannel(c, dst, 3, 14, 3, 0);
        }
    }
    else if (blendMode == PA_BLEND_SRC_ALPHA_ONE)
    {
        pixelArmLoad(c, 14, 0, -24);
        pixelArmMLA(c, 3, 14, 3, 3);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
        pixelArmMLA(c, 2, 14, 2, 2);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
        pixelArmMLA(c, 1, 14, 1, 1);
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
        for (ch = 0; ch < 3; ++ch)
        {
            int srcReg = 3 - ch;
            if (rgb565)
                pixelArmEmitUnpackRGB565Dst(c, 12, 14, ch);
            else
                pixelArmUXTB(c, 14, 12, dst->rgbaOffsets[ch]);
            pixelArmOp(c, PA_ADD, srcReg, srcReg, 14, 0);
            pixelArmUSAT8(c, srcReg, srcReg, 0);
        }
        pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
        if (dstAlpha)
        {
            pixelArmLoad(c, 14, 0, -24);
            pixelArmMLA(c, 14, 14, 14, 14);
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 8), 0);
            pixelArmUXTB(c, 12, 12, dst->rgbaOffsets[3]);
            pixelArmOp(c, PA_ADD, 14, 14, 12, 0);
            pixelArmUSAT8(c, 14, 14, 0);
            pixelArmPackChannel(c, dst, 3, 14, 3, 0);
        }
    }
    else /* PA_BLEND_SRC_ALPHA */
    {
        pixelArmLoad(c, 14, 0, -24);
        pixelArmMLA(c, 3, 14, 3, 3);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
        pixelArmMLA(c, 2, 14, 2, 2);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
        pixelArmMLA(c, 1, 14, 1, 1);
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
        pixelArmStore(c, 1, 0, -32);
        if (dstAlpha)
        {
            pixelArmMLA(c, 1, 14, 14, 14);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmStore(c, 1, 0, -24);
        }
        pixelArmOp(c, PA_RSB, 14, 14, pixelArmImmediate(0x100u), 0);
        if (rgb565)
            pixelArmEmitUnpackRGB565Dst(c, 12, 1, 0);
        else
            pixelArmUXTB(c, 1, 12, dst->rgbaOffsets[0]);
        pixelArmMul(c, 1, 1, 14);
        pixelArmOp(c, PA_ADD, 3, 3, pixelArmShift(1, PA_LSR, 8), 0);

        if (rgb565)
            pixelArmEmitUnpackRGB565Dst(c, 12, 1, 1);
        else
            pixelArmUXTB(c, 1, 12, dst->rgbaOffsets[1]);
        pixelArmMul(c, 1, 1, 14);
        pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(1, PA_LSR, 8), 0);

        if (rgb565)
            pixelArmEmitUnpackRGB565Dst(c, 12, 1, 2);
        else
            pixelArmUXTB(c, 1, 12, dst->rgbaOffsets[2]);
        pixelArmMul(c, 1, 1, 14);
        if (dstAlpha)
        {
            pixelArmUXTB(c, 12, 12, dst->rgbaOffsets[3]);
            pixelArmMul(c, 12, 12, 14);
            pixelArmLoad(c, 14, 0, -32);
            pixelArmOp(c, PA_ADD, 1, 14, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmLoad(c, 14, 0, -24);
            pixelArmOp(c, PA_ADD, 14, 14, pixelArmShift(12, PA_LSR, 8), 0);
        }
        else
        {
            pixelArmLoad(c, 12, 0, -32);
            pixelArmOp(c, PA_ADD, 1, 12, pixelArmShift(1, PA_LSR, 8), 0);
        }
        pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
        if (dstAlpha)
            pixelArmPackChannel(c, dst, 3, 14, 3, 0);
    }

    if (preserveR12)
        pixelArmLoad(c, 12, 0, -40);
}

static void pixelArmEmitVariant(ARMPixelCode_s *c, const _grSignature *sig,
                                unsigned int textureLayout, int rgb565, int dstAlpha,
                                int isBilinear, int modulate, int alphaMode, int alphaTest,
                                int alphaRejectCond, int fog, int depth,
                                int rejectCondition, int dither, int colorShift,
                                int blendMode, int colorVariant, int lblBase,
                                int fallthroughReturn)
{
    const _grSurfaceDescriptor *dst = &sig->descriptor;
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    const unsigned char *t = sig->texture[0];
    int isConstRgb = colorVariant != 0;
    int isConstWhite = colorVariant == 2;
    int applyModulate = modulate && !isConstWhite;
    int lblRow = lblBase + 0;
    int lblPixel = lblBase + 1;
    int lblShade = lblBase + 2;
    int lblRowEnd = lblBase + 3;
    int lblReject = lblBase + 4;
    int lblRejectLoop = lblBase + 5;
    int lblAlphaReject = lblBase + 6;
    int lblSpan = lblBase + 7;
    int fastFog32 = !rgb565 && dst->rgbaOffsets[1] == 8 &&
        ((dst->rgbaOffsets[0] == 16 && dst->rgbaOffsets[2] == 0) ||
         (dst->rgbaOffsets[0] == 0 && dst->rgbaOffsets[2] == 16));
    int swapRB = fastFog32 && dst->rgbaOffsets[0] == 16;
    int isBlendSWAR32 = (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE) &&
        fastFog32 && (!dstAlpha || dst->rgbaOffsets[3] == 24);
    int isBlendOneOne32 = (blendMode == PA_BLEND_ONE_ONE) && fastFog32;
    int isBlendGeneral = (blendMode != PA_BLEND_NONE) && !isBlendSWAR32 && !isBlendOneOne32;
    int keepSTAdders = !isBilinear && (blendMode == PA_BLEND_NONE) && !applyModulate && !fog &&
        (!isConstRgb || isConstWhite) &&
        (alphaTest ? (alphaMode == 1 || (isConstWhite && alphaMode == 3)) : alphaMode < 2);
    int incFog = fog && !depth && !alphaTest && !applyModulate;
    int packShiftsR12 = (!isBilinear && textureLayout == 0 && !keepSTAdders &&
                         ((!applyModulate && fog) || (applyModulate && isConstRgb && fog)));
    int texReg = keepSTAdders ? 3 : (packShiftsR12 ? ((!applyModulate && !alphaTest) ? 1 : 14) : 12);
    int gradTmp = packShiftsR12 ? 10 : 12;
    int needAlphaScratch = (dstAlpha && (fog || (alphaTest && alphaMode >= 1 && !isConstWhite) ||
                                         blendMode == PA_BLEND_ZERO_SRC_COLOR ||
                                         blendMode == PA_BLEND_DST_COLOR_ZERO ||
                                         isBlendGeneral)) ||
        (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE) ||
        (dstAlpha && alphaTest);
    int blendAlphaInR3 = isBlendSWAR32 && !fog && !applyModulate && !alphaTest &&
        ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16);
    int preloadFogRB = fog && packShiftsR12 && fastFog32 && !sig->monochromeFog &&
        !depth && !dither && !alphaTest;
    int emitAlphaScratch = !alphaTest &&
        (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE ||
         (dstAlpha && (fog || isBlendGeneral)));
    int haveR2Const = !isConstWhite && (alphaTest || emitAlphaScratch) && (alphaMode >= 2);
    int bilinearClearByte3 = isBilinear && !dstAlpha && !alphaTest &&
        blendMode != PA_BLEND_SRC_ALPHA && blendMode != PA_BLEND_SRC_ALPHA_ONE &&
        tex->rgbaOffsets[0] <= 16 && tex->rgbaOffsets[1] <= 16 && tex->rgbaOffsets[2] <= 16;
    int channel, outputRegister;

    c->labels[lblRow] = c->count;
    if (packShiftsR12)
    {
        pixelArmLoad(c, 12, 0, PA_S_SHIFT);
        pixelArmLoad(c, 1, 0, PA_T_SHIFT);
        pixelArmLoad(c, 2, 0, PA_WIDTH_LOG);
        pixelArmOp(c, PA_ORR, 12, 12, pixelArmShift(1, PA_LSL, 8), 0);
        pixelArmOp(c, PA_ORR, 12, 12, pixelArmShift(2, PA_LSL, 16), 0);
        if (fog)
        {
            pixelArmLoad(c, 1, 0, PA_FOG_COLOR);
            pixelArmUXTB(c, 1, 1, sig->monochromeFog ? 0 : 8);
            pixelArmOp(c, PA_ORR, 12, 12, pixelArmShift(1, PA_LSL, 24), 0);
        }
    }
    c->labels[lblSpan] = c->count;
    pixelArmLoad(c, 1, 0, PA_SPANS);
    pixelArmWord(c, 0xE4912004u); /* LDR r2, [r1], #4. */
    pixelArmStore(c, 1, 0, PA_SPANS);
    pixelArmOp(c, PA_MOV, 4, 0, pixelArmShift(2, PA_LSR, 16), 0);
    pixelArmWord(c, 0xE6FF3072u); /* UXTH r3, r2. */
    pixelArmOp(c, PA_ADD, 2, 3, 4, 0);
    pixelArmRowPointer(c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
    if (depth) pixelArmRowPointer(c, 9, PA_DEPTH_ROW, PA_DEPTH_STRIDE, 1);
    if (!isConstRgb && modulate && !packShiftsR12)
    {
        /* Batch R/G/B span setup before Z/S/T while r5, r6, r7, r10, r12 are free. */
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89100E0u); /* LDMIA r1, {r5, r6, r7} */
        pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89A5400u); /* LDMIA r10, {r10, r12, r14} */
        pixelArmMLA(c, 1, 14, 2, 7);
        pixelArmMLA(c, 12, 12, 2, 6);
        pixelArmMLA(c, 14, 10, 2, 5);
        pixelArmWord(c, 0xE9005002u); /* STMDB r0, {r1, r12, r14} -> B(-12), G(-8), R(-4) */
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE8915400u); /* LDMIA r1, {r10, r12, r14} */
        pixelArmOp(c, PA_ADD, 5, 5, 10, 0);
        pixelArmOp(c, PA_ADD, 6, 6, 12, 0);
        pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE88100E0u); /* STMIA r1, {r5, r6, r7} */
        if (alphaMode >= 2)
            pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, gradTmp, 2, -16, gradTmp);
        if (depth) pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, (!dither && gradTmp != 10) ? 10 : gradTmp);
        pixelArmGradientEx(c, GR_GRADIENT_TEX0_S, 5, 3, 0, gradTmp);
        pixelArmGradientEx(c, GR_GRADIENT_TEX0_T, 6, 3, 0, gradTmp);
        if (fog) pixelArmGradientEx(c, GR_GRADIENT_FOG, gradTmp, 2, -20, gradTmp);
    }
    else
    {
        if (depth) pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, (!dither && gradTmp != 10) ? 10 : gradTmp);
        pixelArmGradientEx(c, GR_GRADIENT_TEX0_S, 5, 3, 0, gradTmp);
        pixelArmGradientEx(c, GR_GRADIENT_TEX0_T, 6, 3, 0, gradTmp);
        if (fog)
        {
            if (incFog)
            {
                pixelArmGradientEx(c, GR_GRADIENT_FOG, 7, 3, 0, gradTmp);
                pixelArmLoad(c, 9, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
            }
            else
            {
                pixelArmGradientEx(c, GR_GRADIENT_FOG, gradTmp, 2, -20, gradTmp);
            }
        }
        if (!isConstRgb)
        {
            if (modulate)
            {
                for (channel = 0; channel < 3; ++channel)
                    pixelArmGradientEx(c, GR_GRADIENT_COLOR_R + channel, gradTmp, 2,
                                       -4 * (channel + 1), gradTmp);
            }
            if (alphaMode >= 2)
                pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, gradTmp, 2, -16, gradTmp);
        }
    }
    if (dither)
        pixelArmEmitDitherRow(c);
    if (keepSTAdders)
    {
        pixelArmLoad(c, 12, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
    }
    else if (!depth && !incFog)
    {
        pixelArmLoad(c, 7, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmLoad(c, 9, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
    }
    if (depth && !dither && gradTmp == 10) pixelArmLoad(c, 10, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
    if (!depth && !dither && alphaTest) pixelArmLoad(c, 10, 0, PA_ALPHA_REF);
    if (preloadFogRB)
    {
        pixelArmLoad(c, 10, 0, PA_FOG_COLOR);
        pixelArmUXTB16(c, 10, 10, swapRB ? 16 : 0);
    }
    pixelArmOp(c, PA_RSB, 4, 4, pixelArmImmediate(0), 1);
    pixelArmBranch(c, PA_GE, lblRowEnd);

    c->labels[lblPixel] = c->count;
    if (depth)
    {
        pixelArmWord(c, 0xE0D910B2u); /* LDRH r1, [r9], #2. */
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblReject);
    }
    if (!alphaTest)
    {
        c->labels[lblShade] = c->count;
        if (depth && sig->depthMask) pixelArmWord(c, 0xE14930B2u); /* STRH r3, [r9, #-2]. */
    }
    else
    {
        c->labels[lblShade] = c->count;
    }

    if (isBilinear)
    {
        pixelArmEmitBilinearTexel(c, tex, textureLayout, texReg, bilinearClearByte3);
    }
    else
    {
        pixelArmTextureIndex(c, textureLayout, packShiftsR12);
        if (tex->paletteFormat == GR_PALETTEFORMAT_8BIT)
        {
            pixelArmWord(c, 0xE7DB2002u); /* LDRB r2, [r11, r2]. */
            pixelArmLoad(c, 1, 0, PA_PALETTE);
            if (!alphaTest && keepSTAdders && !isConstWhite)
            {
                /* Step S/T during LDRB + PA_PALETTE latency before the dependent palette LDR. */
                pixelArmOp(c, PA_ADD, 5, 5, 12, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
            }
            pixelArmWord(c, 0xE7910102u | ((unsigned int)texReg << 12));
        }
        else
        {
            pixelArmWord(c, 0xE79B0102u | ((unsigned int)texReg << 12));
        }
    }

    /* When alphaTest is off there are no more branches before the pixel loop
     * tail, so step S/T/Z immediately to hide texture-load latency. */
    if (!alphaTest)
    {
        if (keepSTAdders)
        {
            if (tex->paletteFormat == GR_PALETTEFORMAT_8BIT && !isConstWhite)
            {
                if (depth)
                {
                    if (dither) pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
                    if (!dither) pixelArmOp(c, PA_ADD, 7, 7, 10, 0);
                }
            }
            else
            {
                if (depth && dither) pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
                if (depth) pixelArmOp(c, PA_ADD, 7, 7, dither ? 2u : 10u, 0);
                pixelArmOp(c, PA_ADD, 5, 5, 12, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
            }
        }
        else
        {
            pixelArmStepSTZ(c, depth, dither, 0, incFog, packShiftsR12 ? texReg : 0, 0,
                            packShiftsR12 ? ((texReg == 1) ? 14 : 2) : 14);
        }
        if (isConstWhite)
        {
            pixelArmLoad(c, 2, 0, -4);
            pixelArmUQADD8(c, texReg, texReg, 2);
        }
    }
    else
    {
        /* Compute alpha into r1, keep texReg and r2 (colPacked if const). */
        if (isConstWhite && alphaMode == 3)
        {
            pixelArmLoad(c, 1, 0, -4);
            pixelArmUQADD8(c, texReg, texReg, 1);
            pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
        }
        else if (alphaMode == 1)
        {
            pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
        }
        else if (alphaMode == 2)
        {
            if (isConstWhite)
            {
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmImmediate(255u), 0);
            }
            else if (isConstRgb)
            {
                pixelArmLoad(c, 2, 0, -4);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(2, PA_LSR, 24), 0);
            }
            else
            {
                pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                pixelArmLoad(c, 3, 0, -16);
                pixelArmMLA(c, 2, 2, 4, 3);
                pixelArmUSAT8(c, 1, 2, 16);
            }
        }
        else /* alphaMode == 3 */
        {
            pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
            if (isConstRgb)
            {
                pixelArmLoad(c, 2, 0, -4);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(2, PA_LSR, 24), 0);
                pixelArmMLA(c, 1, 1, 3, 3);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            }
            else
            {
                pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                pixelArmLoad(c, 3, 0, -16);
                pixelArmMLA(c, 2, 2, 4, 3);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
                pixelArmMLA(c, 1, 1, 2, 2);
                pixelArmUSAT8(c, 1, 1, 8);
            }
        }
        if (!depth && !dither)
        {
            pixelArmOp(c, PA_CMP, 0, 1, 10, 1);
        }
        else
        {
            int refReg = keepSTAdders ? 2 : 3;
            pixelArmLoad(c, refReg, 0, PA_ALPHA_REF);
            pixelArmOp(c, PA_CMP, 0, 1, refReg, 1);
        }
        pixelArmBranch(c, alphaRejectCond, lblAlphaReject);
        if (needAlphaScratch) pixelArmStore(c, 1, 0, -24);
        if (depth && sig->depthMask)
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(7, PA_LSR, 14), 0);
            pixelArmWord(c, 0xE14910B2u); /* STRH r1, [r9, #-2]. */
        }
        if (keepSTAdders)
            pixelArmStepSTZ(c, depth, dither, 1, 0, 0, 0, 2);
        else
        {
            /* Keep r2 (colPacked) and texReg intact across S/T/Z stepping. */
            pixelArmStepSTZ(c, depth, dither, 0, 0, 2, packShiftsR12 ? texReg : 0,
                            packShiftsR12 ? 1 : 14);
        }
        if (isConstWhite && alphaMode != 3)
        {
            pixelArmLoad(c, 1, 0, -4);
            pixelArmUQADD8(c, texReg, texReg, 1);
        }
    }

    if (emitAlphaScratch)
        pixelArmEmitAlphaScratch(c, tex, alphaMode, texReg, isConstRgb, isConstWhite, blendAlphaInR3);

    outputRegister = 14;
    if (!fog)
    {
        if (isBlendSWAR32)
        {
            if (!applyModulate)
            {
                pixelArmUXTB(c, 2, texReg, tex->rgbaOffsets[1]);
                if ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16)
                {
                    pixelArmUXTB16(c, 1, texReg, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
                }
                else
                {
                    pixelArmUXTB(c, 3, texReg, tex->rgbaOffsets[0]);
                    pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[2]);
                    pixelArmPKHBT(c, 1, swapRB ? 14 : 3, swapRB ? 3 : 14, 16);
                }
            }
            else if (isConstRgb)
            {
                if (!haveR2Const) pixelArmLoad(c, 2, 0, -4);
                pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 3, 2, 16);
                pixelArmMLA(c, 1, 1, 3, 3);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
                pixelArmUXTB(c, 3, 2, 8);
                pixelArmMLA(c, 14, 14, 3, 3);
                pixelArmUXTB(c, 12, texReg, tex->rgbaOffsets[2]);
                pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFFu), 0);
                pixelArmMLA(c, 12, 12, 2, 2);
                if (swapRB)
                    pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(12, PA_LSR, 8), 0);
                else
                    pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                pixelArmPKHBT(c, 1, swapRB ? 12 : 1, swapRB ? 1 : 12, 8);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
            }
            else
            {
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
                pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
                pixelArmMLA(c, 3, 14, 4, 3);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
                pixelArmMLA(c, 2, 14, 4, 2);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
                pixelArmMLA(c, 1, 14, 4, 1);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[0]);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
                pixelArmMLA(c, 3, 14, 3, 3);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
                pixelArmMLA(c, 2, 14, 2, 2);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[2]);
                pixelArmMLA(c, 1, 14, 1, 1);
                pixelArmUSAT8(c, 3, 3, 8);
                pixelArmUSAT8(c, 2, 2, 8);
                pixelArmUSAT8(c, 1, 1, 8);
                pixelArmPKHBT(c, 1, swapRB ? 1 : 3, swapRB ? 3 : 1, 16);
            }
        }
        else if (isBlendGeneral)
        {
            if (!applyModulate)
            {
                pixelArmUXTB(c, 3, texReg, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 2, texReg, tex->rgbaOffsets[1]);
                pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[2]);
            }
            else if (isConstRgb)
            {
                if (!haveR2Const) pixelArmLoad(c, 2, 0, -4);
                pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 3, 2, 16);
                pixelArmMLA(c, 1, 1, 3, 3);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
                pixelArmUXTB(c, 3, 2, 8);
                pixelArmMLA(c, 14, 14, 3, 3);
                pixelArmUXTB(c, 12, texReg, tex->rgbaOffsets[2]);
                pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFFu), 0);
                pixelArmMLA(c, 12, 12, 2, 2);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(1, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 8), 0);
            }
            else
            {
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
                pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
                pixelArmMLA(c, 3, 14, 4, 3);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
                pixelArmMLA(c, 2, 14, 4, 2);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
                pixelArmMLA(c, 1, 14, 4, 1);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[0]);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
                pixelArmMLA(c, 3, 14, 3, 3);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
                pixelArmMLA(c, 2, 14, 2, 2);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[2]);
                pixelArmMLA(c, 1, 14, 1, 1);
                pixelArmUSAT8(c, 3, 3, 8);
                pixelArmUSAT8(c, 2, 2, 8);
                pixelArmUSAT8(c, 1, 1, 8);
            }
        }
        else
        {
            int rbSwap32 = !applyModulate && !rgb565 &&
                ((tex->rgbaOffsets[0] == 0 && tex->rgbaOffsets[1] == 8 && tex->rgbaOffsets[2] == 16 &&
                  dst->rgbaOffsets[0] == 16 && dst->rgbaOffsets[1] == 8 && dst->rgbaOffsets[2] == 0) ||
                 (tex->rgbaOffsets[0] == 16 && tex->rgbaOffsets[1] == 8 && tex->rgbaOffsets[2] == 0 &&
                  dst->rgbaOffsets[0] == 0 && dst->rgbaOffsets[1] == 8 && dst->rgbaOffsets[2] == 16));
            int exactAlphaCopy = !dstAlpha || (alphaMode != 2 && (alphaMode == 1 || isConstWhite) &&
                                              memcmp(tex->rgbaOffsets + 3, dst->rgbaOffsets + 3, 1u) == 0);
            int deferredZ = !alphaTest && keepSTAdders &&
                tex->paletteFormat == GR_PALETTEFORMAT_8BIT && !isConstWhite && depth && dither;
            if (!applyModulate && !rgb565 &&
                ((!dstAlpha && memcmp(tex->rgbaOffsets, dst->rgbaOffsets, 3u) == 0) ||
                 (dstAlpha && alphaMode != 2 && (alphaMode == 1 || isConstWhite) &&
                  memcmp(tex->rgbaOffsets, dst->rgbaOffsets, 4u) == 0)))
            {
                unsigned int unused = 0xFFFFFFFFu;
                if (deferredZ) pixelArmOp(c, PA_ADD, 7, 7, 2, 0);
                for (channel = 0; channel < (dstAlpha ? 4 : 3); ++channel)
                    unused &= ~(255u << dst->rgbaOffsets[channel]);
                if (unused && !(bilinearClearByte3 && unused == 0xFF000000u) &&
                    !(t[PA_TEX_RAW_COPY] && !sig->perPixelPack))
                {
                    pixelArmOp(c, PA_BIC, 2, texReg, pixelArmImmediate(unused), 0);
                    outputRegister = 2;
                }
                else
                {
                    outputRegister = texReg;
                }
            }
            else if (rbSwap32 && exactAlphaCopy && (!dstAlpha || tex->rgbaOffsets[3] == 24))
            {
                unsigned int unused = 0xFFFFFFFFu;
                if (deferredZ) pixelArmOp(c, PA_ADD, 7, 7, 2, 0);
                for (channel = 0; channel < (dstAlpha ? 4 : 3); ++channel)
                    unused &= ~(255u << dst->rgbaOffsets[channel]);
                pixelArmREV(c, 2, texReg);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_ROR, 8), 0);
                if (unused && !(bilinearClearByte3 && unused == 0xFF000000u) &&
                    !(t[PA_TEX_RAW_COPY] && !sig->perPixelPack))
                    pixelArmOp(c, PA_BIC, 2, 2, pixelArmImmediate(unused), 0);
                outputRegister = 2;
            }
            else if (!applyModulate)
            {
                outputRegister = keepSTAdders ? 2 : 14;
                if (deferredZ)
                {
                    pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[0]);
                    pixelArmOp(c, PA_ADD, 7, 7, 2, 0);
                    pixelArmPackChannel(c, dst, 0, 1, outputRegister, dither);
                    for (channel = 1; channel < 3; ++channel)
                    {
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[channel]);
                        pixelArmPackChannel(c, dst, channel, 1, outputRegister, dither);
                    }
                }
                else
                {
                    for (channel = 0; channel < 3; ++channel)
                    {
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[channel]);
                        pixelArmPackChannel(c, dst, channel, 1, outputRegister, dither);
                    }
                }
                if (dstAlpha)
                {
                    if (alphaMode == 1 || (isConstWhite && alphaMode == 3))
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
                    else if (isConstWhite && alphaMode == 2)
                        pixelArmOp(c, PA_MOV, 1, 0, pixelArmImmediate(255u), 0);
                    else if (alphaTest)
                        pixelArmLoad(c, 1, 0, -24);
                    else if (alphaMode == 2)
                    {
                        if (isConstRgb)
                        {
                            pixelArmLoad(c, 1, 0, -4);
                            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 24), 0);
                        }
                        else
                        {
                            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                            pixelArmLoad(c, 3, 0, -16);
                            pixelArmMLA(c, 1, 1, 4, 3);
                            pixelArmUSAT8(c, 1, 1, 16);
                        }
                    }
                    else
                    {
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
                        if (isConstRgb)
                        {
                            pixelArmLoad(c, 3, 0, -4);
                            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 24), 0);
                            pixelArmMLA(c, 1, 1, 3, 3);
                            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                        }
                        else
                        {
                            pixelArmLoad(c, 3, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                            pixelArmLoad(c, 2, 0, -16);
                            pixelArmMLA(c, 3, 3, 4, 2);
                            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
                            pixelArmMLA(c, 1, 1, 3, 3);
                            pixelArmUSAT8(c, 1, 1, 8);
                        }
                    }
                    pixelArmPackChannel(c, dst, 3, 1, outputRegister, 0);
                }
            }
            else if (isConstRgb)
            {
                if (!haveR2Const) pixelArmLoad(c, 2, 0, -4);
                for (channel = 0; channel < 3; ++channel)
                {
                    int rot = 16 - 8 * channel;
                    pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[channel]);
                    if (rot) pixelArmUXTB(c, 3, 2, rot);
                    else pixelArmOp(c, PA_AND, 3, 2, pixelArmImmediate(0xFFu), 0);
                    pixelArmMLA(c, 1, 1, 3, 3);
                    pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                    pixelArmPackChannel(c, dst, channel, 1, 14, dither);
                }
                if (dstAlpha)
                {
                    if (alphaMode == 1)
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
                    else if (alphaTest)
                        pixelArmLoad(c, 1, 0, -24);
                    else if (alphaMode == 2)
                        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(2, PA_LSR, 24), 0);
                    else
                    {
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
                        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(2, PA_LSR, 24), 0);
                        pixelArmMLA(c, 1, 1, 3, 3);
                        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                    }
                    pixelArmPackChannel(c, dst, 3, 1, 14, 0);
                }
                outputRegister = 14;
            }
            else if (!dstAlpha)
            {
                /* Gouraud RGB without alpha: schedule dR/dG/dB loads before LDMDB/LSR to avoid stalls. */
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
                pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
                pixelArmMLA(c, 3, 14, 4, 3);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
                pixelArmMLA(c, 2, 14, 4, 2);
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
                pixelArmMLA(c, 1, 14, 4, 1);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[0]);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
                pixelArmMLA(c, 3, 14, 3, 3);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
                pixelArmMLA(c, 2, 14, 2, 2);
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[2]);
                pixelArmMLA(c, 1, 14, 1, 1);
                pixelArmUSAT8(c, 3, 3, 8);
                pixelArmUSAT8(c, 2, 2, 8);
                pixelArmUSAT8(c, 1, 1, 8);
                pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
                outputRegister = 3;
            }
            else
            {
                for (channel = 0; channel < 4; ++channel)
                {
                    if (channel == 3 && alphaMode == 1)
                    {
                        pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[3]);
                        pixelArmPackChannel(c, dst, 3, 1, 14, 0);
                    }
                    else if (channel == 3 && alphaTest)
                    {
                        pixelArmLoad(c, 1, 0, -24);
                        pixelArmPackChannel(c, dst, 3, 1, 14, 0);
                    }
                    else if (channel == 3 && alphaMode == 2)
                    {
                        pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                        pixelArmLoad(c, 3, 0, -16);
                        pixelArmMLA(c, 1, 1, 4, 3);
                        pixelArmUSAT8(c, 1, 1, 16);
                        pixelArmPackChannel(c, dst, 3, 1, 14, 0);
                    }
                    else
                    {
                        pixelArmComponent(c, tex, dst, channel, 1, dither, 0, texReg, 14);
                    }
                }
                outputRegister = 14;
            }
        }
    }
    else /* fog == 1 */
    {
        /* Produce r1 = 0x00BB00RR (or 0x00RR00BB when swapRB) and r14 = 0x000000GG. */
        if (!applyModulate)
        {
            if (texReg != 14)
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
            if ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16)
            {
                pixelArmUXTB16(c, 1, texReg, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
            }
            else
            {
                pixelArmUXTB(c, 3, texReg, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 2, texReg, tex->rgbaOffsets[2]);
                pixelArmPKHBT(c, 1, swapRB ? 2 : 3, swapRB ? 3 : 2, 16);
            }
            if (texReg == 14)
                pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
        }
        else if (isConstRgb && packShiftsR12)
        {
            /* texReg is r14; r12 is preserved. */
            if (!haveR2Const) pixelArmLoad(c, 2, 0, -4);
            pixelArmUXTB(c, 1, 14, tex->rgbaOffsets[0]);
            pixelArmUXTB(c, 3, 2, 16);
            pixelArmMLA(c, 1, 1, 3, 3);
            pixelArmUXTB(c, 3, 14, tex->rgbaOffsets[2]);
            if (!swapRB) pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFFu), 0);
            pixelArmMLA(c, 3, 3, 2, 2);
            pixelArmLoad(c, 2, 0, -4);
            if (swapRB) pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
            pixelArmPKHBT(c, 1, swapRB ? 3 : 1, swapRB ? 1 : 3, 8);
            pixelArmUXTB(c, 14, 14, tex->rgbaOffsets[1]);
            pixelArmUXTB(c, 2, 2, 8);
            pixelArmMLA(c, 14, 14, 2, 2);
            pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 8), 0);
        }
        else if (isConstRgb)
        {
            if (!haveR2Const) pixelArmLoad(c, 2, 0, -4);
            pixelArmUXTB(c, 1, texReg, tex->rgbaOffsets[0]);
            pixelArmUXTB(c, 3, 2, 16);
            pixelArmMLA(c, 1, 1, 3, 3);
            pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
            pixelArmUXTB(c, 3, 2, 8);
            pixelArmMLA(c, 14, 14, 3, 3);
            pixelArmUXTB(c, 12, texReg, tex->rgbaOffsets[2]);
            pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFFu), 0);
            pixelArmMLA(c, 12, 12, 2, 2);
            if (!swapRB) pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 8), 0);
            if (swapRB) pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(12, PA_LSR, 8), 0);
            pixelArmPKHBT(c, 1, swapRB ? 12 : 1, swapRB ? 1 : 12, 8);
        }
        else
        {
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
            pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
            pixelArmMLA(c, 3, 14, 4, 3);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
            pixelArmMLA(c, 2, 14, 4, 2);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
            pixelArmMLA(c, 1, 14, 4, 1);
            pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[0]);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
            pixelArmMLA(c, 3, 14, 3, 3);
            pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[1]);
            pixelArmMLA(c, 2, 14, 2, 2);
            pixelArmUXTB(c, 14, texReg, tex->rgbaOffsets[2]);
            pixelArmMLA(c, 1, 14, 1, 1);
            pixelArmUSAT8(c, 3, 3, 8);
            pixelArmUSAT8(c, 14, 2, 8);
            pixelArmUSAT8(c, 1, 1, 8);
            pixelArmPKHBT(c, 1, swapRB ? 1 : 3, swapRB ? 3 : 1, 16);
        }

        /* Packed 2x16 fog blend on r1 (0x00BB00RR / 0x00RR00BB) and r14 (0x000000GG). */
        if (incFog)
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(7, PA_LSR, 16), 0);
        }
        else
        {
            if (!(applyModulate && isConstRgb && packShiftsR12))
                pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
            pixelArmLoad(c, 3, 0, -20);
            pixelArmMLA(c, 2, 2, 4, 3);
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
        }
        pixelArmOp(c, PA_RSB, 3, 2, pixelArmImmediate(0x100u), 0);
        pixelArmMul(c, 1, 1, 3);
        pixelArmMul(c, 14, 14, 3);
        if (packShiftsR12)
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(12, PA_LSR, 24), 0);
            pixelArmMLA(c, 14, 3, 2, 14);
            if (sig->monochromeFog)
            {
                if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
                pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(3, PA_LSL, 16), 0);
                pixelArmMLA(c, 1, 3, 2, 1);
            }
            else if (preloadFogRB)
            {
                if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
                pixelArmMLA(c, 1, 10, 2, 1);
            }
            else
            {
                pixelArmLoad(c, 3, 0, PA_FOG_COLOR);
                if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
                pixelArmUXTB16(c, 3, 3, swapRB ? 16 : 0);
                pixelArmMLA(c, 1, 3, 2, 1);
            }
        }
        else
        {
            pixelArmLoad(c, 3, 0, PA_FOG_COLOR);
            if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
            if (sig->monochromeFog)
            {
                pixelArmMLA(c, 14, 3, 2, 14);
                pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(3, PA_LSL, 16), 0);
                pixelArmMLA(c, 1, 3, 2, 1);
            }
            else
            {
                pixelArmUXTB(c, 12, 3, 8);
                pixelArmMLA(c, 14, 12, 2, 14);
                pixelArmUXTB16(c, 3, 3, swapRB ? 16 : 0);
                pixelArmMLA(c, 1, 3, 2, 1);
            }
        }
        if (isBlendSWAR32)
        {
            pixelArmUXTB16(c, 1, 1, 8);
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
        }
        else if (isBlendGeneral)
        {
            if (swapRB)
            {
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(1, PA_LSR, 24), 0);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
                pixelArmUXTB(c, 1, 1, 8);
            }
            else
            {
                pixelArmUXTB(c, 3, 1, 8);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 24), 0);
            }
        }
        else
        {
            if (fastFog32)
            {
                pixelArmUXTB16(c, 3, 1, 8);
                pixelArmOp(c, PA_AND, 2, 14, pixelArmImmediate(0xFF00u), 0);
                pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
            }
            else
            {
                pixelArmUXTB(c, 3, 1, 8);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 24), 0);
                pixelArmPackChannel(c, dst, 0, 3, 3, dither);
                pixelArmPackChannel(c, dst, 1, 2, 3, dither);
                pixelArmPackChannel(c, dst, 2, 1, 3, dither);
            }
            if (dstAlpha)
            {
                pixelArmLoad(c, 14, 0, -24);
                pixelArmPackChannel(c, dst, 3, 14, 3, 0);
            }
            outputRegister = 3;
        }
    }

    if (isBlendSWAR32)
    {
        pixelArmEmitBlend32SWAR(c, blendMode, dstAlpha, blendAlphaInR3, packShiftsR12);
        outputRegister = 3;
    }
    else if (isBlendOneOne32)
    {
        int dstTmp = (outputRegister == 2) ? 1 : 2;
        pixelArmWord(c, 0xE5980000u | ((unsigned int)dstTmp << 12)); /* LDR dstTmp, [r8] */
        pixelArmUQADD8(c, 3, outputRegister, dstTmp);
        if (!dstAlpha)
            pixelArmOp(c, PA_BIC, 3, 3, pixelArmImmediate(0xFF000000u), 0);
        outputRegister = 3;
    }
    else if (isBlendGeneral)
    {
        pixelArmEmitBlendGeneral(c, dst, rgb565, dstAlpha, dither, blendMode, packShiftsR12);
        outputRegister = 3;
    }

    if (rgb565) pixelArmWord(c, 0xE0C800B2u | ((unsigned int)outputRegister << 12));
    else pixelArmWord(c, 0xE4880004u | ((unsigned int)outputRegister << 12));
    if (dither) pixelArmOp(c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
    pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
    pixelArmBranch(c, PA_NE, lblPixel);

    c->labels[lblRowEnd] = c->count;
    pixelArmLoad(c, 1, 0, PA_ROWS);
    pixelArmOp(c, PA_SUB, 1, 1, pixelArmImmediate(1), 1);
    pixelArmStore(c, 1, 0, PA_ROWS);
    pixelArmBranch(c, PA_NE, lblSpan);
    if (fallthroughReturn)
    {
        c->labels[PA_RETURN] = c->count;
        pixelArmWord(c, 0xE8BD8FF0u);
    }
    else
    {
        pixelArmBranch(c, PA_AL, PA_RETURN);
    }

    if (depth)
    {
        c->labels[lblReject] = c->count;
        if (packShiftsR12)
            pixelArmStore(c, 4, 0, -28);
        else
            pixelArmOp(c, PA_MOV, 12, 0, 4, 0);
        if (dither) pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
        c->labels[lblRejectLoop] = c->count;
        pixelArmOp(c, PA_ADD, 7, 7, dither ? 14u : 10u, 0);
        pixelArmOp(c, PA_ADD, 8, 8, pixelArmImmediate(1u << colorShift), 0);
        pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(c, PA_EQ, lblRowEnd);
        pixelArmWord(c, 0xE0D910B2u);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblRejectLoop);
        if (packShiftsR12)
        {
            pixelArmLoad(c, 2, 0, -28);
            pixelArmOp(c, PA_SUB, 2, 4, 2, 0);
            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
            pixelArmMLA(c, 5, 1, 2, 5);
            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
            pixelArmMLA(c, 6, 1, 2, 6);
            if (dither)
            {
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(2, PA_LSL, 3), 0);
                pixelArmOp(c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
            }
        }
        else
        {
            pixelArmOp(c, PA_SUB, 12, 4, 12, 0);
            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
            pixelArmMLA(c, 5, 1, 12, 5);
            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
            pixelArmMLA(c, 6, 1, 12, 6);
            if (dither)
            {
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSL, 3), 0);
                pixelArmOp(c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
            }
        }
        if (keepSTAdders)
        {
            pixelArmLoad(c, 12, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        }
        pixelArmBranch(c, PA_AL, lblShade);
    }
    if (alphaTest)
    {
        c->labels[lblAlphaReject] = c->count;
        pixelArmOp(c, PA_ADD, 8, 8, pixelArmImmediate(1u << colorShift), 0);
        pixelArmStepSTZ(c, depth, dither, keepSTAdders, incFog,
                        packShiftsR12 ? texReg : 0, 0, packShiftsR12 ? 1 : 2);
        if (dither) pixelArmOp(c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
        pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(c, PA_NE, lblPixel);
        pixelArmBranch(c, PA_AL, lblRowEnd);
    }
}

static void pixelArmEmitUntextured(ARMPixelCode_s *c, const _grSignature *sig,
                                    int rgb565, int dstAlpha, int alphaTest,
                                    int alphaRejectCond, int fog, int depth,
                                    int rejectCondition, int dither, int colorShift,
                                    int blendMode, int isConstRgb,
                                    int lblBase, int fallthroughReturn)
{
    const _grSurfaceDescriptor *dst = &sig->descriptor;
    int lblRow = lblBase + 0;
    int lblPixel = lblBase + 1;
    int lblShade = lblBase + 2;
    int lblRowEnd = lblBase + 3;
    int lblReject = lblBase + 4;
    int lblRejectLoop = lblBase + 5;
    int lblAlphaReject = lblBase + 6;
    int lblSpan = lblBase + 7;
    int fast32 = !rgb565 && dst->rgbaOffsets[1] == 8 &&
        ((dst->rgbaOffsets[0] == 16 && dst->rgbaOffsets[2] == 0) ||
         (dst->rgbaOffsets[0] == 0 && dst->rgbaOffsets[2] == 16));
    int swapRB = fast32 && dst->rgbaOffsets[0] == 16;
    int isBlendSWAR32 = (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE) &&
        fast32 && (!dstAlpha || dst->rgbaOffsets[3] == 24);
    int isBlendOneOne32 = (blendMode == PA_BLEND_ONE_ONE) && fast32;
    int isBlendGeneral = (blendMode != PA_BLEND_NONE) && !isBlendSWAR32 && !isBlendOneOne32;
    int needSrcAlpha = dstAlpha || alphaTest ||
        blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE;
    int needAlphaScratch = needSrcAlpha && (fog || alphaTest || isBlendSWAR32 || isBlendGeneral);
    int incFog = fog && !depth && !alphaTest;
    int channel;

    c->labels[lblRow] = c->count;
    if (fog)
        pixelArmLoad(c, 11, 0, PA_FOG_COLOR);
    else if (!isConstRgb)
        pixelArmLoad(c, 11, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);

    c->labels[lblSpan] = c->count;
    pixelArmLoad(c, 1, 0, PA_SPANS);
    pixelArmWord(c, 0xE4912004u); /* LDR r2, [r1], #4. */
    pixelArmStore(c, 1, 0, PA_SPANS);
    pixelArmOp(c, PA_MOV, 4, 0, pixelArmShift(2, PA_LSR, 16), 0);
    pixelArmWord(c, 0xE6FF3072u); /* UXTH r3, r2. */
    pixelArmOp(c, PA_ADD, 2, 3, 4, 0);
    pixelArmRowPointer(c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
    if (depth) pixelArmRowPointer(c, 9, PA_DEPTH_ROW, PA_DEPTH_STRIDE, 1);
    if (!isConstRgb)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89100E0u); /* LDMIA r1, {r5, r6, r7} */
        pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89A5400u); /* LDMIA r10, {r10, r12, r14} */
        pixelArmMLA(c, 1, 14, 2, 7);
        pixelArmMLA(c, 12, 12, 2, 6);
        pixelArmMLA(c, 14, 10, 2, 5);
        pixelArmWord(c, 0xE9005002u); /* STMDB r0, {r1, r12, r14} */
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE8915400u); /* LDMIA r1, {r10, r12, r14} */
        pixelArmOp(c, PA_ADD, 5, 5, 10, 0);
        pixelArmOp(c, PA_ADD, 6, 6, 12, 0);
        pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE88100E0u); /* STMIA r1, {r5, r6, r7} */
        if (needSrcAlpha)
            pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, 12, 2, -16, 12);
    }
    if (depth) pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, !dither ? 10 : 12);
    if (fog)
    {
        pixelArmGradientEx(c, GR_GRADIENT_FOG, 6, incFog ? 3 : 2, 0, 12);
        pixelArmLoad(c, 5, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
    }
    else if (!isConstRgb)
    {
        pixelArmLoad(c, 5, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
        pixelArmLoad(c, 6, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
    }
    if (dither) pixelArmEmitDitherRow(c);
    if (!depth && !dither && alphaTest) pixelArmLoad(c, 10, 0, PA_ALPHA_REF);
    pixelArmOp(c, PA_RSB, 4, 4, pixelArmImmediate(0), 1);
    pixelArmBranch(c, PA_GE, lblRowEnd);

    c->labels[lblPixel] = c->count;
    if (depth)
    {
        pixelArmWord(c, 0xE0D910B2u); /* LDRH r1, [r9], #2. */
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblReject);
    }
    c->labels[lblShade] = c->count;
    if (!alphaTest && depth && sig->depthMask)
        pixelArmWord(c, 0xE14930B2u); /* STRH r3, [r9, #-2]. */

    if (alphaTest)
    {
        if (isConstRgb)
        {
            pixelArmLoad(c, 2, 0, -4);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(2, PA_LSR, 24), 0);
        }
        else
        {
            pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
            pixelArmLoad(c, 3, 0, -16);
            pixelArmMLA(c, 2, 2, 4, 3);
            pixelArmUSAT8(c, 1, 2, 16);
        }
        if (!depth && !dither)
        {
            pixelArmOp(c, PA_CMP, 0, 1, 10, 1);
        }
        else
        {
            pixelArmLoad(c, 3, 0, PA_ALPHA_REF);
            pixelArmOp(c, PA_CMP, 0, 1, 3, 1);
        }
        pixelArmBranch(c, alphaRejectCond, lblAlphaReject);
        if (needAlphaScratch) pixelArmStore(c, 1, 0, -24);
        if (depth && sig->depthMask)
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(7, PA_LSR, 14), 0);
            pixelArmWord(c, 0xE14910B2u); /* STRH r1, [r9, #-2]. */
        }
    }
    else if (needAlphaScratch)
    {
        if (isConstRgb)
        {
            pixelArmLoad(c, 1, 0, -4);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 24), 0);
        }
        else
        {
            pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
            pixelArmLoad(c, 3, 0, -16);
            pixelArmMLA(c, 1, 1, 4, 3);
            pixelArmUSAT8(c, 1, 1, 16);
        }
        pixelArmStore(c, 1, 0, -24);
    }

    if (depth && dither)
        pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
    else if (depth)
        pixelArmOp(c, PA_ADD, 7, 7, 10, 0);

    {
        int fastFog32 = fog && fast32 && (blendMode == PA_BLEND_NONE || isBlendOneOne32 || isBlendSWAR32);
        int fogSwapRB = fastFog32 && dst->rgbaOffsets[0] == 16;

        if (isConstRgb)
        {
            pixelArmLoad(c, 12, 0, -4);
            if (depth && dither) pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
            if (fastFog32 && !sig->monochromeFog)
            {
                pixelArmUXTB16(c, 1, 12, fogSwapRB ? 0 : 16);
                pixelArmUXTB(c, 2, 12, 8);
            }
            else
            {
                pixelArmUXTB(c, 3, 12, 16);
                pixelArmUXTB(c, 2, 12, 8);
                pixelArmOp(c, PA_AND, 1, 12, pixelArmImmediate(0xFFu), 0);
            }
        }
        else if (!fog)
        {
            pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
            if (depth && dither) pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
            pixelArmMLA(c, 3, 5, 4, 3);
            pixelArmMLA(c, 2, 6, 4, 2);
            pixelArmMLA(c, 1, 11, 4, 1);
            pixelArmUSAT8(c, 3, 3, 16);
            pixelArmUSAT8(c, 2, 2, 16);
            pixelArmUSAT8(c, 1, 1, 16);
        }
        else
        {
            pixelArmWord(c, 0xE910000Eu); /* LDMDB r0, {r1, r2, r3}. */
            if (depth && dither) pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
            pixelArmLoad(c, 12, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
            pixelArmMLA(c, 3, 14, 4, 3);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
            pixelArmMLA(c, 2, 12, 4, 2);
            pixelArmUSAT8(c, 3, 3, 16);
            pixelArmMLA(c, 1, 14, 4, 1);
            pixelArmUSAT8(c, 2, 2, 16);
            pixelArmMLA(c, 12, 5, 4, 6);
            pixelArmUSAT8(c, 1, 1, 16);
        }

        if (fog)
        {
            if (incFog)
            {
                pixelArmUSAT8(c, 12, 6, 16);
                pixelArmOp(c, PA_ADD, 6, 6, 5, 0);
            }
            else
            {
                if (isConstRgb)
                    pixelArmMLA(c, 12, 5, 4, 6);
                pixelArmUSAT8(c, 12, 12, 16);
            }
            if (sig->monochromeFog)
            {
                pixelArmOp(c, PA_AND, 14, 11, pixelArmImmediate(0xFFu), 0);
                pixelArmMul(c, 14, 14, 12);
                pixelArmOp(c, PA_RSB, 12, 12, pixelArmImmediate(0x100u), 0);
                pixelArmMLA(c, 3, 3, 12, 14);
                pixelArmMLA(c, 2, 2, 12, 14);
                pixelArmMLA(c, 1, 1, 12, 14);
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                if (isBlendSWAR32)
                    pixelArmPKHBT(c, 1, swapRB ? 1 : 3, swapRB ? 3 : 1, 16);
                else if (!isBlendGeneral)
                    pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
            }
            else
            {
                if (!(isConstRgb && fastFog32))
                    pixelArmPKHBT(c, 1, fogSwapRB ? 1 : 3, fogSwapRB ? 3 : 1, 16);
                pixelArmOp(c, PA_RSB, 14, 12, pixelArmImmediate(0x100u), 0);
                pixelArmMul(c, 1, 1, 14);
                pixelArmMul(c, 2, 2, 14);
                pixelArmUXTB(c, 14, 11, 8);
                pixelArmMLA(c, 2, 14, 12, 2);
                pixelArmUXTB16(c, 14, 11, fogSwapRB ? 16 : 0);
                pixelArmMLA(c, 1, 14, 12, 1);
                if (isBlendSWAR32)
                {
                    pixelArmUXTB16(c, 1, 1, 8);
                    pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
                }
                else if (fastFog32)
                {
                    pixelArmUXTB16(c, 3, 1, 8);
                    pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFF00u), 0);
                    pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
                }
                else
                {
                    pixelArmUXTB(c, 3, 1, 8);
                    pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
                    pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 24), 0);
                    if (!isBlendGeneral)
                        pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
                }
            }
        }
        else
        {
            if (isBlendSWAR32)
                pixelArmPKHBT(c, 1, swapRB ? 1 : 3, swapRB ? 3 : 1, 16);
            else if (!isBlendGeneral)
                pixelArmPackRGB32(c, dst, 3, 2, 1, 3, dither);
        }
    }

    if (isBlendSWAR32)
    {
        pixelArmEmitBlend32SWAR(c, blendMode, dstAlpha, 0, 0);
    }
    else if (isBlendGeneral)
    {
        pixelArmEmitBlendGeneral(c, dst, rgb565, dstAlpha, dither, blendMode, 0);
    }
    else
    {
        if (dstAlpha)
        {
            if (fog || alphaTest)
            {
                pixelArmLoad(c, 14, 0, -24);
            }
            else if (isConstRgb)
            {
                pixelArmLoad(c, 14, 0, -4);
                pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 24), 0);
            }
            else
            {
                pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                pixelArmLoad(c, 2, 0, -16);
                pixelArmMLA(c, 14, 14, 4, 2);
                pixelArmUSAT8(c, 14, 14, 16);
            }
            pixelArmPackChannel(c, dst, 3, 14, 3, 0);
        }
        if (isBlendOneOne32)
        {
            pixelArmWord(c, 0xE5981000u); /* LDR r1, [r8] */
            pixelArmUQADD8(c, 3, 3, 1);
            if (!dstAlpha)
                pixelArmOp(c, PA_BIC, 3, 3, pixelArmImmediate(0xFF000000u), 0);
        }
    }

    if (rgb565) pixelArmWord(c, 0xE0C830B2u); /* STRH r3, [r8], #2 */
    else pixelArmWord(c, 0xE4883004u);        /* STR r3, [r8], #4 */
    if (dither) pixelArmOp(c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
    pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
    pixelArmBranch(c, PA_NE, lblPixel);

    c->labels[lblRowEnd] = c->count;
    pixelArmLoad(c, 1, 0, PA_ROWS);
    pixelArmOp(c, PA_SUB, 1, 1, pixelArmImmediate(1), 1);
    pixelArmStore(c, 1, 0, PA_ROWS);
    pixelArmBranch(c, PA_NE, lblSpan);
    if (fallthroughReturn)
    {
        c->labels[PA_RETURN] = c->count;
        pixelArmWord(c, 0xE8BD8FF0u);
    }
    else
    {
        pixelArmBranch(c, PA_AL, PA_RETURN);
    }

    if (depth)
    {
        c->labels[lblReject] = c->count;
        if (dither)
        {
            pixelArmOp(c, PA_MOV, 12, 0, 4, 0);
            pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
        }
        c->labels[lblRejectLoop] = c->count;
        pixelArmOp(c, PA_ADD, 7, 7, dither ? 14u : 10u, 0);
        pixelArmOp(c, PA_ADD, 8, 8, pixelArmImmediate(1u << colorShift), 0);
        pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(c, PA_EQ, lblRowEnd);
        pixelArmWord(c, 0xE0D910B2u);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblRejectLoop);
        if (dither)
        {
            pixelArmOp(c, PA_SUB, 12, 4, 12, 0);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSL, 3), 0);
            pixelArmOp(c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
        }
        pixelArmBranch(c, PA_AL, lblShade);
    }
    if (alphaTest)
    {
        c->labels[lblAlphaReject] = c->count;
        pixelArmOp(c, PA_ADD, 8, 8, pixelArmImmediate(1u << colorShift), 0);
        if (depth)
        {
            if (dither)
            {
                pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
                pixelArmOp(c, PA_ADD, 7, 7, 2, 0);
            }
            else
            {
                pixelArmOp(c, PA_ADD, 7, 7, 10, 0);
            }
        }
        if (dither) pixelArmOp(c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
        pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(c, PA_NE, lblPixel);
        pixelArmBranch(c, PA_AL, lblRowEnd);
    }
    (void)channel;
}

int grCompileARMPixelPipelineEx(void *output, int capacity, const _grSignature *sig,
                                unsigned int textureLayout, unsigned int *gradientMask,
                                int allowExtended)
{
    const _grSurfaceDescriptor *dst = &sig->descriptor;
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    const unsigned char *t = sig->texture[0];
    ARMPixelCode_s c;
    int rgb565, dstAlpha, isBilinear, modulate, alphaMode, alphaTest, alphaRejectCond, fog;
    int depth, rejectCondition, dither, colorShift, needColA, emitConstVariant, emitWhiteVariant;
    int blendMode;
    int i;
    unsigned int mask;
    unsigned int whiteMask;

    rgb565 = dst->colorBytes == 2 && dst->rgbaSizes[0] == 5 && dst->rgbaSizes[1] == 6 &&
        dst->rgbaSizes[2] == 5 && dst->rgbaSizes[3] == 0 && dst->rgbaOffsets[0] == 11 &&
        dst->rgbaOffsets[1] == 5 && dst->rgbaOffsets[2] == 0;
    dstAlpha = dst->rgbaSizes[3] != 0;
    if (textureLayout > PA_LAYOUT_COUNT || !sig->isValid || !sig->writeColor ||
        sig->constantWriteColor || sig->screenRectGradients ||
        sig->logicOpEnabled || sig->applyColorWriteMask ||
        sig->stencilTest ||
        (!allowExtended && (sig->blendEnabled || sig->alphaTest || sig->fogEnabled || sig->flatShading)) ||
        (sig->colorMask & dst->colorMask) != dst->colorMask ||
        (!rgb565 && !pixelArmByteChannels(dst, dstAlpha)) ||
        sig->texture[1][PA_TEX_ENABLED])
        return 0;

    alphaTest = (allowExtended && sig->alphaTest) ? 1 : 0;
    alphaRejectCond = alphaTest ? pixelArmAlphaRejectCond(sig->alphaFunc) : -1;
    if (alphaTest && alphaRejectCond < 0)
        return 0;
    fog = (allowExtended && sig->fogEnabled) ? 1 : 0;
    depth = sig->depthTest != 0;
    if (depth && (dst->depthBytes != 2 || sig->constantDepth || sig->depthClampMin || sig->depthClampMax ||
        (sig->depthFunc != GR_FUNC_LESS && sig->depthFunc != GR_FUNC_LEQUAL)))
        return 0;

    blendMode = pixelArmClassifyBlend(sig);
    if (blendMode < 0 || (!allowExtended && blendMode != PA_BLEND_NONE))
        return 0;

    dither = rgb565 && sig->dither;
    colorShift = rgb565 ? 1 : 2;
    rejectCondition = sig->depthFunc == GR_FUNC_LESS ? PA_HS : PA_HI;

    if (!t[PA_TEX_ENABLED])
    {
        if (!allowExtended) return 0;
        if (blendMode == PA_BLEND_ZERO_SRC_COLOR && alphaTest)
            return 0;
        needColA = dstAlpha || alphaTest ||
            blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE;
        emitConstVariant = capacity >= 1600;

        mask = 7u << GR_GRADIENT_COLOR_R;
        if (depth) mask |= 1u << GR_GRADIENT_DEPTH;
        if (fog) mask |= 1u << GR_GRADIENT_FOG;
        if (needColA) mask |= 1u << GR_GRADIENT_COLOR_A;

        memset(&c, 0, sizeof(c));
        c.words = (unsigned int *)output;
        c.capacity = capacity / 4;
        pixelArmWord(&c, 0xE92D4FF0u); /* Preserve r4-r11 and the return address. */
        pixelArmOp(&c, PA_ADD, 0, 0, pixelArmImmediate(64), 0);
        pixelArmLoad(&c, 2, 0, PA_ROWS);
        pixelArmOp(&c, PA_CMP, 0, 2, pixelArmImmediate(0), 1);
        pixelArmBranch(&c, PA_EQ, PA_RETURN);

        if (emitConstVariant)
        {
            for (i = 0; i < 3; ++i)
            {
                pixelArmLoad(&c, i == 0 ? 1 : 2, 0, PA_X_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                if (i != 0) pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
                pixelArmLoad(&c, 2, 0, PA_Y_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            }
            if (needColA)
            {
                pixelArmLoad(&c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
                pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
                pixelArmLoad(&c, 2, 0, PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_A);
                pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            }
            pixelArmOp(&c, PA_CMP, 0, 1, pixelArmImmediate(0), 1);
            pixelArmBranch(&c, PA_NE, PA_G_ROW);

            pixelArmLoad(&c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R);
            pixelArmLoad(&c, 2, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_G);
            pixelArmLoad(&c, 3, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_B);
            pixelArmOp(&c, PA_ORR, 14, 1, 2, 0);
            pixelArmOp(&c, PA_ORR, 14, 14, 3, 0);
            if (needColA)
            {
                pixelArmLoad(&c, 12, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_A);
                pixelArmOp(&c, PA_ORR, 14, 14, 12, 0);
            }
            pixelArmOp(&c, PA_TST, 0, 14, pixelArmImmediate(0xFF000000u), 1);
            pixelArmBranch(&c, PA_NE, PA_G_ROW);

            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 16), 0);
            pixelArmOp(&c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(2, PA_LSL, 8), 0);
            pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(3, PA_LSR, 16), 0);
            if (needColA)
            {
                pixelArmOp(&c, PA_MOV, 12, 0, pixelArmShift(12, PA_LSR, 16), 0);
                pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(12, PA_LSL, 24), 0);
            }
            pixelArmStore(&c, 1, 0, -4);
            pixelArmBranch(&c, PA_AL, PA_C_ROW);

            pixelArmEmitUntextured(&c, sig, rgb565, dstAlpha, alphaTest, alphaRejectCond,
                                   fog, depth, rejectCondition, dither, colorShift,
                                   blendMode, 1, PA_C_ROW, 0);
        }
        pixelArmEmitUntextured(&c, sig, rgb565, dstAlpha, alphaTest, alphaRejectCond,
                               fog, depth, rejectCondition, dither, colorShift,
                               blendMode, 0, PA_G_ROW, 1);
        if (c.count > c.capacity) return 0;
        for (i = 0; i < c.numBranches; ++i)
            c.words[c.branches[i]] |= (unsigned int)(c.labels[c.targets[i]] - c.branches[i] - 2) & 0xFFFFFFu;
        *gradientMask = mask;
        return c.count * 4;
    }

    if (t[PA_TEX_MAG_LINEAR] != t[PA_TEX_MIN_LINEAR] ||
        (!allowExtended && (t[PA_TEX_MAG_LINEAR] || t[PA_TEX_MIN_LINEAR])) ||
        t[PA_TEX_WRAP_S] != GR_WRAPMODE_REPEAT || t[PA_TEX_WRAP_T] != GR_WRAPMODE_REPEAT ||
        !pixelArmByteChannels(tex, 0) ||
        (tex->paletteFormat != GR_PALETTEFORMAT_NONE && tex->paletteFormat != GR_PALETTEFORMAT_8BIT) ||
        t[PA_TEX_RGB_OPERAND0] != GR_TEXENVCOMBOPERAND_SRC_COLOR || t[PA_TEX_RGB_SCALE] || t[PA_TEX_ALPHA_SCALE])
        return 0;

    isBilinear = (allowExtended && t[PA_TEX_MAG_LINEAR]) ? 1 : 0;

    modulate = t[PA_TEX_RGB_FUNCTION] == GR_TEXENVCOMBFUNC_MODULATE &&
        t[PA_TEX_RGB_SOURCE0] == GR_TEXENVCOMBSRC_PREVIOUS &&
        t[PA_TEX_RGB_SOURCE1] == GR_TEXENVCOMBSRC_TEXTURE &&
        t[PA_TEX_RGB_OPERAND1] == GR_TEXENVCOMBOPERAND_SRC_COLOR &&
        (!sig->flatShading || allowExtended);
    if (!modulate && (t[PA_TEX_RGB_FUNCTION] != GR_TEXENVCOMBFUNC_REPLACE ||
        t[PA_TEX_RGB_SOURCE0] != GR_TEXENVCOMBSRC_TEXTURE))
        return 0;

    alphaMode = 0;
    if (dstAlpha || alphaTest ||
        blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE)
    {
        if (t[PA_TEX_ALPHA_FUNCTION] == GR_TEXENVCOMBFUNC_REPLACE &&
            t[PA_TEX_ALPHA_OPERAND0] == GR_TEXENVCOMBOPERAND_SRC_ALPHA)
        {
            if (t[PA_TEX_ALPHA_SOURCE0] == GR_TEXENVCOMBSRC_TEXTURE)
            {
                if (!pixelArmByteChannels(tex, 1)) return 0;
                alphaMode = 1;
            }
            else if (t[PA_TEX_ALPHA_SOURCE0] == GR_TEXENVCOMBSRC_PREVIOUS)
            {
                if (!allowExtended) return 0;
                alphaMode = 2;
            }
            else
                return 0;
        }
        else if (t[PA_TEX_ALPHA_FUNCTION] == GR_TEXENVCOMBFUNC_MODULATE &&
                 t[PA_TEX_ALPHA_SOURCE0] == GR_TEXENVCOMBSRC_PREVIOUS &&
                 t[PA_TEX_ALPHA_OPERAND0] == GR_TEXENVCOMBOPERAND_SRC_ALPHA &&
                 t[PA_TEX_ALPHA_SOURCE1] == GR_TEXENVCOMBSRC_TEXTURE &&
                 t[PA_TEX_ALPHA_OPERAND1] == GR_TEXENVCOMBOPERAND_SRC_ALPHA)
        {
            if (!pixelArmByteChannels(tex, 1)) return 0;
            alphaMode = 3;
        }
        else
            return 0;
    }

    needColA = alphaMode >= 2;
    emitConstVariant = (modulate || needColA) && capacity >= (isBilinear ? 2400 : 1600);
    emitWhiteVariant = modulate && capacity >= (isBilinear ? 3600 : 2400);

    whiteMask = (1u << tex->rgbaOffsets[0]) | (1u << tex->rgbaOffsets[1]) | (1u << tex->rgbaOffsets[2]);
    if (alphaMode == 3)
        whiteMask |= (1u << tex->rgbaOffsets[3]);

    mask = (1u << GR_GRADIENT_TEX0_S) | (1u << GR_GRADIENT_TEX0_T);
    if (depth) mask |= 1u << GR_GRADIENT_DEPTH;
    if (fog) mask |= 1u << GR_GRADIENT_FOG;
    if (modulate) mask |= 7u << GR_GRADIENT_COLOR_R;
    if (needColA) mask |= 1u << GR_GRADIENT_COLOR_A;

    memset(&c, 0, sizeof(c));
    c.words = (unsigned int *)output;
    c.capacity = capacity / 4;
    pixelArmWord(&c, 0xE92D4FF0u); /* Preserve r4-r11 and the return address. */
    pixelArmOp(&c, PA_ADD, 0, 0, pixelArmImmediate(64), 0);
    pixelArmLoad(&c, 2, 0, PA_ROWS);
    pixelArmOp(&c, PA_CMP, 0, 2, pixelArmImmediate(0), 1);
    pixelArmBranch(&c, PA_EQ, PA_RETURN);
    pixelArmLoad(&c, 11, 0, PA_TEXTURE);

    if (emitConstVariant)
    {
        int first = 1;
        if (modulate)
        {
            for (i = 0; i < 3; ++i)
            {
                pixelArmLoad(&c, first ? 1 : 2, 0, PA_X_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                if (!first) pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
                first = 0;
                pixelArmLoad(&c, 2, 0, PA_Y_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            }
        }
        if (needColA)
        {
            pixelArmLoad(&c, first ? 1 : 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_A);
            if (!first) pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            pixelArmLoad(&c, 2, 0, PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_A);
            pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
        }
        pixelArmOp(&c, PA_CMP, 0, 1, pixelArmImmediate(0), 1);
        pixelArmBranch(&c, PA_NE, PA_G_ROW);

        if (modulate)
        {
            pixelArmLoad(&c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R);
            pixelArmLoad(&c, 2, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_G);
            pixelArmLoad(&c, 3, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_B);
            pixelArmOp(&c, PA_ORR, 14, 1, 2, 0);
            pixelArmOp(&c, PA_ORR, 14, 14, 3, 0);
            if (needColA)
            {
                pixelArmLoad(&c, 12, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_A);
                pixelArmOp(&c, PA_ORR, 14, 14, 12, 0);
            }
        }
        else
        {
            pixelArmLoad(&c, 12, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_A);
            pixelArmOp(&c, PA_MOV, 14, 0, 12, 0);
        }
        pixelArmOp(&c, PA_TST, 0, 14, pixelArmImmediate(0xFF000000u), 1);
        pixelArmBranch(&c, PA_NE, emitWhiteVariant ? PA_W_CHECK_256 : PA_G_ROW);

        if (modulate)
        {
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 16), 0);
            pixelArmOp(&c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(2, PA_LSL, 8), 0);
            pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(3, PA_LSR, 16), 0);
            if (needColA)
            {
                pixelArmOp(&c, PA_MOV, 12, 0, pixelArmShift(12, PA_LSR, 16), 0);
                pixelArmOp(&c, PA_ORR, 1, 1, pixelArmShift(12, PA_LSL, 24), 0);
            }
            if (emitWhiteVariant)
            {
                if (needColA)
                {
                    pixelArmWord(&c, 0xE3710001u); /* CMN r1, #1 (r1 == 0xFFFFFFFF). */
                }
                else
                {
                    pixelArmOp(&c, PA_ORR, 2, 1, pixelArmImmediate(0xFF000000u), 0);
                    pixelArmWord(&c, 0xE3720001u); /* CMN r2, #1 (r2 == 0xFFFFFFFF). */
                }
                pixelArmBranch(&c, PA_EQ, PA_W_INIT_255);
            }
        }
        else
        {
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 24), 0);
        }
        pixelArmStore(&c, 1, 0, -4);
        if (!emitWhiteVariant)
            pixelArmBranch(&c, PA_AL, PA_C_ROW);
    }

    if (emitConstVariant)
        pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                            alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                            dither, colorShift, blendMode, 1, PA_C_ROW, 0);
    pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                        alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                        dither, colorShift, blendMode, 0, PA_G_ROW, !emitWhiteVariant);
    if (emitWhiteVariant)
    {
        c.labels[PA_W_CHECK_256] = c.count;
        pixelArmOp(&c, PA_AND, 4, 1, 2, 0);
        pixelArmOp(&c, PA_AND, 4, 4, 3, 0);
        if (needColA)
            pixelArmOp(&c, PA_AND, 4, 4, 12, 0);
        pixelArmOp(&c, PA_MOV, 5, 0, pixelArmShift(14, PA_LSR, 16), 0);
        pixelArmOp(&c, PA_SUB, 5, 5, pixelArmImmediate(256u), 0);
        pixelArmOp(&c, PA_MOV, 4, 0, pixelArmShift(4, PA_LSR, 16), 0);
        pixelArmOp(&c, PA_SUB, 4, 4, pixelArmImmediate(256u), 0);
        pixelArmOp(&c, PA_ORR, 4, 4, 5, 1);
        pixelArmBranch(&c, PA_NE, PA_G_ROW);

        c.labels[PA_W_INIT_256] = c.count;
        pixelArmLoadImm32(&c, 1, whiteMask);
        pixelArmStore(&c, 1, 0, -4);
        pixelArmBranch(&c, PA_AL, PA_W_ROW);

        c.labels[PA_W_INIT_255] = c.count;
        pixelArmOp(&c, PA_MOV, 1, 0, pixelArmImmediate(0), 0);
        pixelArmStore(&c, 1, 0, -4);

        pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                            alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                            dither, colorShift, blendMode, 2, PA_W_ROW, 1);
    }

    if (c.count > c.capacity) return 0;
    for (i = 0; i < c.numBranches; ++i)
        c.words[c.branches[i]] |= (unsigned int)(c.labels[c.targets[i]] - c.branches[i] - 2) & 0xFFFFFFu;
    *gradientMask = mask;
    return c.count * 4;
}

int grCompileARMPixelPipeline(void *output, int capacity, const _grSignature *sig,
                              unsigned int textureLayout, unsigned int *gradientMask)
{
    return grCompileARMPixelPipelineEx(output, capacity, sig, textureLayout, gradientMask, 0);
}

#else

int grCompileARMPixelPipelineEx(void *output, int capacity, const _grSignature *signature,
                                unsigned int textureLayout, unsigned int *gradientMask,
                                int allowExtended)
{
    (void)output; (void)capacity; (void)signature; (void)textureLayout;
    (void)gradientMask; (void)allowExtended;
    return 0;
}

int grCompileARMPixelPipeline(void *output, int capacity, const _grSignature *signature,
                              unsigned int textureLayout, unsigned int *gradientMask)
{
    (void)output; (void)capacity; (void)signature; (void)textureLayout; (void)gradientMask;
    return 0;
}

#endif
