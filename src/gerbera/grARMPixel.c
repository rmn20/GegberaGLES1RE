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
    PA_EQ = 0, PA_NE = 1, PA_HS = 2, PA_LO = 3, PA_HI = 8, PA_LS = 9, PA_GE = 10, PA_LT = 11, PA_AL = 14,
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
    PA_G_ROW = 1,
    PA_C_ROW = 1 + 12,
    PA_C255_ROW = 1 + 24,
    PA_W_ROW = 1 + 36,
    PA_LABEL_COUNT = 1 + 48
};

enum
{
    PA_OFF_ROW = 0,
    PA_OFF_PIXEL = 1,
    PA_OFF_SHADE = 2,
    PA_OFF_ROW_END = 3,
    PA_OFF_REJECT = 4,
    PA_OFF_REJECT_LOOP = 5,
    PA_OFF_ALPHA_REJECT = 6,
    PA_OFF_SPAN = 7,
    PA_OFF_EMPTY_ROW = 8,
    PA_OFF_FAST4 = 9,
    PA_OFF_CV = 10,
    PA_OFF_DONE = 11
};

typedef struct ARMPixelCode_s
{
    unsigned int *words;
    int count, capacity;
    int labels[PA_LABEL_COUNT];
    int branches[384], targets[384], numBranches;
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

static void pixelArmLoadB(ARMPixelCode_s *c, int dst, int base, int offset)
{
    pixelArmWord(c, (offset < 0 ? 0xE5500000u : 0xE5D00000u) |
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

static void pixelArmSMULWT(ARMPixelCode_s *c, int dst, int rm, int rs)
{
    pixelArmWord(c, 0xE12000E0u | ((unsigned int)dst << 16) |
                 ((unsigned int)rs << 8) | (unsigned int)rm);
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

static void pixelArmUXTAB16(ARMPixelCode_s *c, int dst, int src, int rhs, int bitOffset)
{
    pixelArmWord(c, 0xE6C00070u | ((unsigned int)src << 16) |
                 ((unsigned int)dst << 12) | ((unsigned int)(bitOffset / 8) << 10) |
                 (unsigned int)rhs);
}

static void pixelArmUADD16(ARMPixelCode_s *c, int dst, int lhs, int rhs)
{
    pixelArmWord(c, 0xE6500F10u | ((unsigned int)lhs << 16) |
                 ((unsigned int)dst << 12) | (unsigned int)rhs);
}

static void pixelArmSMUAD(ARMPixelCode_s *c, int dst, int lhs, int rhs)
{
    pixelArmWord(c, 0xE700F010u | ((unsigned int)dst << 16) |
                 ((unsigned int)rhs << 8) | (unsigned int)lhs);
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

static void pixelArmBranch(ARMPixelCode_s *c, int condition, int label)
{
    if (c->numBranches < 384)
    {
        c->branches[c->numBranches] = c->count;
        c->targets[c->numBranches] = label;
    }
    c->numBranches++;
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

static void pixelArmGradientKeepR14(ARMPixelCode_s *c, int gradient, int dst, int origin,
                                    int xTmpReg, int yTmpReg)
{
    pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * gradient);
    pixelArmLoad(c, xTmpReg, 0, PA_X_ADDER + 4 * gradient);
    pixelArmMLA(c, dst, xTmpReg, origin, 1);
    pixelArmLoad(c, yTmpReg, 0, PA_Y_ADDER + 4 * gradient);
    pixelArmOp(c, PA_ADD, 1, 1, yTmpReg, 0);
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

static void pixelArmClampDepth16(ARMPixelCode_s *c, int dstReg, int tmpReg,
                                 int clampMin, int clampMax)
{
    if (!clampMin && !clampMax)
    {
        pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(7, PA_LSR, 14), 0);
        return;
    }
    if (clampMin && !clampMax)
    {
        pixelArmWord(c, 0xE1C70FC7u | ((unsigned int)dstReg << 12)); /* BIC dst, r7, r7, ASR #31 */
        pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(dstReg, PA_LSR, 14), 0);
        return;
    }
    if (!clampMin)
    {
        pixelArmWord(c, 0xE3E00103u | ((unsigned int)dstReg << 12)); /* MVN dst, #0xC0000000 */
        pixelArmOp(c, PA_CMP, 0, dstReg, 7, 1);                      /* CMP dst, r7 */
        pixelArmWord(c, 0xC1A00007u | ((unsigned int)dstReg << 12)); /* MOVGT dst, r7 */
        pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(dstReg, PA_LSR, 14), 0);
        return;
    }
    (void)tmpReg;
    pixelArmWord(c, 0xE6F00757u | ((unsigned int)dstReg << 12)); /* USAT dst, #16, r7, ASR #14 */
}

static void pixelArmTextureIndex(ARMPixelCode_s *c, unsigned int layout, int packedInR12,
                                 int clampS, int clampT)
{
    if (layout)
    {
        int width = (int)((layout - 1) / PA_LAYOUT_DIM_COUNT);
        int height = (int)((layout - 1) % PA_LAYOUT_DIM_COUNT);
        if (height == 0)
        {
            pixelArmOp(c, PA_MOV, 2, 0,
                       width ? pixelArmShift(5, PA_LSR, 32 - width) : pixelArmImmediate(0), 0);
        }
        else if (width == 0)
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(6, PA_LSR, 32 - height), 0);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(6, PA_LSR, 32 - height), 0);
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, width), 0);
            pixelArmOp(c, PA_ORR, 2, 1, pixelArmShift(5, PA_LSR, 32 - width), 0);
        }
    }
    else if (packedInR12)
    {
        if (clampS)
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShiftReg(5, PA_ASR, 12), 0);
            pixelArmLoad(c, 1, 0, PA_S_MASK);
            pixelArmWord(c, 0xE1C22FC2u); /* BIC r2, r2, r2, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 2, 1, 1);
            pixelArmWord(c, 0xC1A02001u); /* MOVGT r2, r1 */
        }
        else
        {
            pixelArmOp(c, PA_MOV, 2, 0, 5u | 0x30u | (12u << 8), 0);
        }
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 8), 0);
        if (clampT)
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(6, PA_ASR, 1), 0);
            pixelArmLoad(c, 1, 0, PA_T_MASK);
            pixelArmWord(c, 0xE1C33FC3u); /* BIC r3, r3, r3, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
            pixelArmWord(c, 0xC1A03001u); /* MOVGT r3, r1 */
        }
        else
        {
            pixelArmOp(c, PA_MOV, 3, 0, 6u | 0x30u | (1u << 8), 0);
        }
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 16), 0);
        pixelArmOp(c, PA_ORR, 2, 2, 3u | 0x10u | (1u << 8), 0);
    }
    else
    {
        pixelArmLoad(c, 1, 0, PA_S_SHIFT);
        if (clampS)
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShiftReg(5, PA_ASR, 1), 0);
            pixelArmLoad(c, 1, 0, PA_S_MASK);
            pixelArmWord(c, 0xE1C22FC2u); /* BIC r2, r2, r2, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 2, 1, 1);
            pixelArmWord(c, 0xC1A02001u); /* MOVGT r2, r1 */
        }
        else
        {
            pixelArmOp(c, PA_MOV, 2, 0, 5u | 0x30u | (1u << 8), 0);
        }
        pixelArmLoad(c, 1, 0, PA_T_SHIFT);
        if (clampT)
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(6, PA_ASR, 1), 0);
            pixelArmLoad(c, 1, 0, PA_T_MASK);
            pixelArmWord(c, 0xE1C33FC3u); /* BIC r3, r3, r3, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
            pixelArmWord(c, 0xC1A03001u); /* MOVGT r3, r1 */
        }
        else
        {
            pixelArmOp(c, PA_MOV, 3, 0, 6u | 0x30u | (1u << 8), 0);
        }
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
                                      unsigned int textureLayout, int texReg, int clearByte3,
                                      int clampS, int clampT)
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
        if (clampS)
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShiftReg(5, PA_ASR, 14), 0);
            pixelArmOp(c, PA_ADD, 2, 1, pixelArmImmediate(1), 0);
            pixelArmWord(c, 0xE1C11FC1u); /* BIC r1, r1, r1, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 1, 12, 1);
            pixelArmWord(c, 0xC1A0100Cu); /* MOVGT r1, r12 */
            pixelArmWord(c, 0xE1C22FC2u); /* BIC r2, r2, r2, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 2, 12, 1);
            pixelArmWord(c, 0xC1A0200Cu); /* MOVGT r2, r12 */
        }
        else
        {
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShiftReg(5, PA_LSR, 14), 0);
            pixelArmOp(c, PA_ADD, 2, 1, pixelArmImmediate(1), 0);
            pixelArmOp(c, PA_AND, 1, 1, 12, 0);
            pixelArmOp(c, PA_AND, 2, 2, 12, 0);
        }
        pixelArmLoad(c, 14, 0, PA_T_SHIFT);
        pixelArmLoad(c, 12, 0, PA_T_MASK);
        if (clampT)
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(6, PA_ASR, 14), 0);
            pixelArmOp(c, PA_ADD, 14, 3, pixelArmImmediate(1), 0);
            pixelArmWord(c, 0xE1C33FC3u); /* BIC r3, r3, r3, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 3, 12, 1);
            pixelArmWord(c, 0xC1A0300Cu); /* MOVGT r3, r12 */
            pixelArmWord(c, 0xE1CEEFCEu); /* BIC r14, r14, r14, ASR #31 */
            pixelArmOp(c, PA_CMP, 0, 14, 12, 1);
            pixelArmWord(c, 0xC1A0E00Cu); /* MOVGT r14, r12 */
            pixelArmOp(c, PA_MOV, 12, 0, 14, 0);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShiftReg(6, PA_LSR, 14), 0);
            pixelArmOp(c, PA_ADD, 14, 3, pixelArmImmediate(1), 0);
            pixelArmOp(c, PA_AND, 3, 3, 12, 0);
            pixelArmOp(c, PA_AND, 12, 14, 12, 0);
        }
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
        if (clearByte3)
        {
            pixelArmUXTB16(c, 3, 1, 0);
            pixelArmOp(c, PA_AND, 1, 1, pixelArmImmediate(0xFF00u), 0);
            pixelArmUXTB16(c, 14, 2, 0);
            pixelArmOp(c, PA_SUB, 14, 14, 3, 0);
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSL, 8), 0);
            pixelArmMLA(c, 3, 14, 12, 3);
            pixelArmUXTB(c, 14, 2, 8);
            pixelArmOp(c, PA_SUB, 14, 14, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmMLA(c, 1, 14, 12, 1);
            pixelArmUXTB16(c, 3, 3, 8);
            if (pass == 0)
            {
                pixelArmOp(c, PA_AND, 1, 1, pixelArmImmediate(0xFF00u), 0);
                pixelArmOp(c, PA_ORR, 3, 3, 1, 0);
                pixelArmLoad(c, 1, 0, -32);
                pixelArmLoad(c, 2, 0, -36);
                pixelArmStore(c, 3, 0, -32);
            }
            else
            {
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            }
        }
        else
        {
            pixelArmOp(c, PA_RSB, 14, 12, pixelArmImmediate(256u), 0);
            pixelArmUXTB16(c, 3, 1, 0);
            pixelArmMul(c, 3, 3, 14);
            pixelArmUXTB16(c, 1, 1, 8);
            pixelArmMul(c, 1, 1, 14);
            pixelArmUXTB16(c, 14, 2, 0);
            pixelArmMLA(c, 3, 14, 12, 3);
            pixelArmUXTB16(c, 14, 2, 8);
            pixelArmMLA(c, 1, 14, 12, 1);
            pixelArmUXTB16(c, 3, 3, 8);
            pixelArmUXTB16(c, 1, 1, 8);
            if (pass == 0)
            {
                pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(1, PA_LSL, 8), 0);
                pixelArmLoad(c, 1, 0, -32);
                pixelArmLoad(c, 2, 0, -36);
                pixelArmStore(c, 3, 0, -32);
            }
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
    if (clearByte3)
    {
        pixelArmUXTB16(c, 14, 2, 0);
        pixelArmOp(c, PA_SUB, 3, 3, 14, 0);
        pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSL, 8), 0);
        pixelArmMLA(c, 3, 3, 12, 14);
        pixelArmOp(c, PA_AND, 14, 2, pixelArmImmediate(0xFF00u), 0);
        pixelArmOp(c, PA_SUB, 1, 1, pixelArmShift(14, PA_LSR, 8), 0);
        pixelArmMLA(c, 1, 1, 12, 14);
        pixelArmUXTB16(c, 3, 3, 8);
        pixelArmOp(c, PA_AND, 1, 1, pixelArmImmediate(0xFF00u), 0);
        pixelArmOp(c, PA_ORR, texReg, 3, 1, 0);
    }
    else
    {
        pixelArmOp(c, PA_RSB, 14, 12, pixelArmImmediate(256u), 0);
        pixelArmMul(c, 3, 3, 12);
        pixelArmMul(c, 1, 1, 12);
        pixelArmUXTB16(c, 12, 2, 0);
        pixelArmMLA(c, 3, 12, 14, 3);
        pixelArmUXTB16(c, 12, 2, 8);
        pixelArmMLA(c, 1, 12, 14, 1);
        pixelArmUXTB16(c, 3, 3, 8);
        pixelArmUXTB16(c, 1, 1, 8);
        pixelArmOp(c, PA_ORR, texReg, 3, pixelArmShift(1, PA_LSL, 8), 0);
    }
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
            pixelArmUXTAB16(c, 2, 2, 1, 8);
        }
        else
        {
            pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(1, PA_LSR, 8), 0);
        }
        pixelArmLoad(c, 1, 0, -32);
        pixelArmUXTAB16(c, 1, 1, 3, 8);
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
        if (dstAlpha)
        {
            pixelArmUXTB16(c, 2, 2, 8);
            pixelArmUXTAB16(c, 2, 2, 3, 8);
        }
        else
        {
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
            pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(3, PA_LSR, 8), 0);
        }
        pixelArmUXTAB16(c, 1, 1, 12, 8);
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
        pixelArmOp(c, PA_MOV, outReg, 0, pixelArmShift(dstReg, PA_LSR, 3), 0);
        pixelArmOp(c, PA_AND, outReg, outReg, pixelArmImmediate(0xFCu), 0);
        pixelArmOp(c, PA_ORR, outReg, outReg, pixelArmShift(outReg, PA_LSR, 6), 0);
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
                                     int preserveR12, int srcAlphaInR14)
{
    /* Inputs: r3 = srcR, r2 = srcG, r1 = srcB (0..255), r14 (when srcAlphaInR14) or
     * [r0, #-24] = srcA when needed, r8 = dst ptr. Output: r3 = packed pixel. */
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
        if (!srcAlphaInR14)
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
    else if (rgb565 && !dstAlpha && !dither) /* PA_BLEND_SRC_ALPHA to RGB565 without dither */
    {
        if (!srcAlphaInR14)
            pixelArmLoad(c, 14, 0, -24);
        pixelArmPKHBT(c, 1, 1, 3, 16);
        pixelArmMLA(c, 1, 14, 1, 1);
        pixelArmMLA(c, 2, 14, 2, 2);
        pixelArmUXTB16(c, 1, 1, 8);
        pixelArmStore(c, 1, 0, -32);
        pixelArmOp(c, PA_RSB, 14, 14, pixelArmImmediate(0x100u), 0);

        pixelArmEmitUnpackRGB565Dst(c, 12, 1, 1);
        pixelArmMul(c, 1, 1, 14);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
        pixelArmOp(c, PA_ADD, 2, 2, pixelArmShift(1, PA_LSR, 8), 0);

        pixelArmEmitUnpackRGB565Dst(c, 12, 3, 0);
        pixelArmEmitUnpackRGB565Dst(c, 12, 1, 2);
        pixelArmPKHBT(c, 3, 1, 3, 16);
        pixelArmMul(c, 3, 3, 14);
        pixelArmLoad(c, 1, 0, -32);
        pixelArmUXTAB16(c, 1, 1, 3, 8);

        pixelArmOp(c, PA_AND, 3, 1, pixelArmImmediate(0xF80000u), 0);
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 2), 0);
        pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(2, PA_LSL, 5), 0);
        pixelArmOp(c, PA_AND, 1, 1, pixelArmImmediate(0xF8u), 0);
        pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(1, PA_LSR, 3), 0);
    }
    else /* PA_BLEND_SRC_ALPHA */
    {
        if (!srcAlphaInR14)
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

static void pixelArmEmitTexIdxConst(ARMPixelCode_s *c, unsigned int layout,
                                    int dstReg, int tmpReg)
{
    int width = (int)((layout - 1) / PA_LAYOUT_DIM_COUNT);
    int height = (int)((layout - 1) % PA_LAYOUT_DIM_COUNT);
    if (height == 0)
    {
        pixelArmOp(c, PA_MOV, dstReg, 0,
                   width ? pixelArmShift(5, PA_LSR, 32 - width) : pixelArmImmediate(0), 0);
    }
    else if (width == 0)
    {
        pixelArmOp(c, PA_MOV, dstReg, 0, pixelArmShift(6, PA_LSR, 32 - height), 0);
    }
    else
    {
        pixelArmOp(c, PA_MOV, tmpReg, 0, pixelArmShift(6, PA_LSR, 32 - height), 0);
        pixelArmOp(c, PA_MOV, tmpReg, 0, pixelArmShift(tmpReg, PA_LSL, width), 0);
        pixelArmOp(c, PA_ORR, dstReg, tmpReg, pixelArmShift(5, PA_LSR, 32 - width), 0);
    }
}

static void pixelArmEmitEmptyRow(ARMPixelCode_s *c, int isConstRgb, int modulate,
                                 int alphaMode, int fog, int depth, int dither,
                                 int lblRowEnd)
{
    /* Advances row state without clobbering r10, r11, or r12. */
    if (depth)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_COLOR_ROW), 0);
        pixelArmWord(c, 0xE8910300u); /* LDMIA r1, {r8, r9} */
        pixelArmOp(c, PA_ADD, 2, 0, pixelArmImmediate(PA_COLOR_STRIDE), 0);
        pixelArmWord(c, 0xE892000Cu); /* LDMIA r2, {r2, r3} */
        pixelArmOp(c, PA_ADD, 8, 8, 2, 0);
        pixelArmOp(c, PA_ADD, 9, 9, 3, 0);
        pixelArmWord(c, 0xE8810300u); /* STMIA r1, {r8, r9} */
        pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_DEPTH);
        pixelArmLoad(c, 2, 0, PA_Y_ADDER + 4 * GR_GRADIENT_DEPTH);
        pixelArmOp(c, PA_ADD, 1, 1, 2, 0);
        pixelArmStore(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_DEPTH);
    }
    else
    {
        pixelArmLoad(c, 1, 0, PA_COLOR_ROW);
        pixelArmLoad(c, 2, 0, PA_COLOR_STRIDE);
        pixelArmOp(c, PA_ADD, 1, 1, 2, 0);
        pixelArmStore(c, 1, 0, PA_COLOR_ROW);
    }
    if (fog)
    {
        pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_FOG);
        pixelArmLoad(c, 2, 0, PA_Y_ADDER + 4 * GR_GRADIENT_FOG);
        pixelArmOp(c, PA_ADD, 1, 1, 2, 0);
        pixelArmStore(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_FOG);
    }
    if (!isConstRgb && modulate)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE891010Cu); /* LDMIA r1, {r2, r3, r8} */
        pixelArmOp(c, PA_ADD, 9, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89900E0u); /* LDMIA r9, {r5, r6, r7} */
        pixelArmOp(c, PA_ADD, 2, 2, 5, 0);
        pixelArmOp(c, PA_ADD, 3, 3, 6, 0);
        pixelArmOp(c, PA_ADD, 8, 8, 7, 0);
        pixelArmWord(c, 0xE881010Cu); /* STMIA r1, {r2, r3, r8} */
    }
    if (!isConstRgb && alphaMode >= 2)
    {
        pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_A);
        pixelArmLoad(c, 2, 0, PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_A);
        pixelArmOp(c, PA_ADD, 1, 1, 2, 0);
        pixelArmStore(c, 1, 0, PA_REFERENCE + 4 * GR_GRADIENT_COLOR_A);
    }
    pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_TEX0_S), 0);
    pixelArmWord(c, 0xE891000Cu); /* LDMIA r1, {r2, r3} */
    pixelArmOp(c, PA_ADD, 8, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
    pixelArmWord(c, 0xE8980060u); /* LDMIA r8, {r5, r6} */
    pixelArmOp(c, PA_ADD, 2, 2, 5, 0);
    pixelArmOp(c, PA_ADD, 3, 3, 6, 0);
    pixelArmWord(c, 0xE881000Cu); /* STMIA r1, {r2, r3} */
    if (dither)
    {
        pixelArmLoad(c, 1, 0, PA_Y);
        pixelArmOp(c, PA_ADD, 1, 1, pixelArmImmediate(1), 0);
        pixelArmStore(c, 1, 0, PA_Y);
    }
    pixelArmBranch(c, PA_AL, lblRowEnd);
}

static void pixelArmEmitModulateFast4(ARMPixelCode_s *c, const _grSignature *sig,
                                      unsigned int textureLayout, int depth,
                                      int rejectCondition, int swapRB, int isConstRgb,
                                      int lblPixel, int lblFast4, int lblRowEnd)
{
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    int pal8 = tex->paletteFormat == GR_PALETTEFORMAT_8BIT;
    int smulwtG = tex->rgbaOffsets[1] == 8;
    int dSReg = depth ? 9 : 7;
    int dTReg = depth ? 10 : 9;
    int palReg = depth ? 7 : 10;
    int texDst[4] = { 12, 2, 3, 14 };
    int constTex[4] = { 12, 5, 6, 7 };
    int k;

    /* Requires count >= 4; for Gouraud, also proves 0 <= col[R,G,B] < 256<<16. */
    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_GE, lblPixel);
    if (!isConstRgb)
    {
        pixelArmWord(c, 0xE3740C02u); /* CMN r4, #512 */
        pixelArmBranch(c, PA_LT, lblPixel);
        pixelArmWord(c, 0xE910100Eu); /* LDMDB r0, {r1, r2, r3, r12}: badGrad, B, G, R */
        pixelArmOp(c, PA_ORR, 1, 1, 12, 0);
        pixelArmOp(c, PA_ORR, 1, 1, 3, 0);
        pixelArmOp(c, PA_ORR, 1, 1, 2, 0);
        pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R);
        pixelArmMLA(c, 12, 14, 4, 12);
        pixelArmOp(c, PA_ORR, 1, 1, 12, 0);
        pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_G);
        pixelArmMLA(c, 3, 14, 4, 3);
        pixelArmOp(c, PA_ORR, 1, 1, 3, 0);
        pixelArmLoad(c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);
        pixelArmMLA(c, 2, 14, 4, 2);
        pixelArmOp(c, PA_ORR, 1, 1, 2, 0);
        pixelArmOp(c, PA_TST, 0, 1, pixelArmImmediate(0xFF000000u), 1);
        pixelArmBranch(c, PA_NE, lblPixel);
    }

    c->labels[lblFast4] = c->count;
    if (depth)
    {
        pixelArmWord(c, 0xE1D910B0u); /* LDRH r1, [r9] */
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 14, 7, 10, 0);
        pixelArmWord(c, 0xE1D910B2u); /* LDRH r1, [r9, #2] */
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 2, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 14, 14, 10, 0);
        pixelArmWord(c, 0xE1D910B4u); /* LDRH r1, [r9, #4] */
        pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 12, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 14, 14, 10, 0);
        pixelArmWord(c, 0xE1D910B6u); /* LDRH r1, [r9, #6] */
        pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 14, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 7, 7, pixelArmShift(10, PA_LSL, 2), 0);
        if (sig->depthMask)
        {
            pixelArmWord(c, 0xE0C930B2u); /* STRH r3, [r9], #2 */
            pixelArmWord(c, 0xE0C920B2u); /* STRH r2, [r9], #2 */
            pixelArmWord(c, 0xE0C9C0B2u); /* STRH r12, [r9], #2 */
            pixelArmWord(c, 0xE0C9E0B2u); /* STRH r14, [r9], #2 */
        }
        else
        {
            pixelArmOp(c, PA_ADD, 9, 9, pixelArmImmediate(8), 0);
        }
    }

    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8810EE0u); /* STMIA r1, {r5, r6, r7, r9, r10, r11} */
    if (depth)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8910600u); /* LDMIA r1, {r9, r10} */
    }
    if (pal8)
        pixelArmLoad(c, palReg, 0, PA_PALETTE);

    for (k = 0; k < 4; ++k)
    {
        int dstR = texDst[k];
        pixelArmEmitTexIdxConst(c, textureLayout, dstR, 1);
        if (pal8)
            pixelArmWord(c, 0xE7DB0000u | ((unsigned int)dstR << 12) | (unsigned int)dstR);
        else
            pixelArmWord(c, 0xE79B0100u | ((unsigned int)dstR << 12) | (unsigned int)dstR);
        pixelArmOp(c, PA_ADD, 5, 5, (unsigned int)dSReg, 0);
        pixelArmOp(c, PA_ADD, 6, 6, (unsigned int)dTReg, 0);
        if (pal8)
            pixelArmWord(c, 0xE7900100u | ((unsigned int)palReg << 16) |
                         ((unsigned int)dstR << 12) | (unsigned int)dstR);
    }

    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8810060u); /* STMIA r1, {r5, r6} */

    if (isConstRgb)
    {
        pixelArmOp(c, PA_MOV, 5, 0, 2, 0);
        pixelArmOp(c, PA_MOV, 6, 0, 3, 0);
        pixelArmOp(c, PA_MOV, 7, 0, 14, 0);
        pixelArmLoad(c, 1, 0, -4);
        pixelArmUXTB(c, 9, 1, 16);
        pixelArmUXTB(c, 10, 1, 8);
        pixelArmOp(c, PA_AND, 11, 1, pixelArmImmediate(0xFFu), 0);
        for (k = 0; k < 4; ++k)
        {
            int tr = constTex[k];
            pixelArmUXTB(c, 1, tr, tex->rgbaOffsets[0]);
            pixelArmMLA(c, 1, 1, 9, 9);
            pixelArmUXTB(c, 2, tr, tex->rgbaOffsets[1]);
            pixelArmMLA(c, 2, 2, 10, 10);
            pixelArmUXTB(c, 3, tr, tex->rgbaOffsets[2]);
            pixelArmMLA(c, 3, 3, 11, 11);
            if (swapRB)
            {
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
                pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFF00u), 0);
                pixelArmPKHBT(c, 1, 3, 1, 8);
            }
            else
            {
                pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFF00u), 0);
                pixelArmPKHBT(c, 1, 1, 3, 8);
            }
            pixelArmOp(c, PA_ORR, 1, 1, 2, 0);
            pixelArmWord(c, 0xE4881004u); /* STR r1, [r8], #4 */
        }
    }
    else
    {
        int rbPair = (tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16;
        int hoistOnePair = smulwtG && rbPair;
        pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(32), 0);
        pixelArmWord(c, 0xE881400Cu); /* STMIA r1, {r2, r3, r14} */
        pixelArmOp(c, PA_ADD, 9, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE8990E00u); /* LDMIA r9, {r9, r10, r11} */
        pixelArmWord(c, 0xE91000E0u); /* LDMDB r0, {r5, r6, r7} */
        pixelArmMLA(c, 7, 9, 4, 7);
        pixelArmMLA(c, 6, 10, 4, 6);
        pixelArmMLA(c, 5, 11, 4, 5);
        if (hoistOnePair)
        {
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmImmediate(1u), 0);
            pixelArmOp(c, PA_ORR, 14, 14, pixelArmImmediate(0x10000u), 0);
        }
        for (k = 0; k < 4; ++k)
        {
            if (k > 0)
                pixelArmLoad(c, 12, 0, -32 + 4 * (k - 1));
            if (smulwtG)
            {
                pixelArmOp(c, PA_AND, 2, 12, pixelArmImmediate(0xFF00u), 0);
                pixelArmOp(c, PA_ADD, 2, 2, pixelArmImmediate(0x100u), 0);
                pixelArmSMULWT(c, 2, 2, 6);
            }
            else
            {
                pixelArmUXTB(c, 2, 12, tex->rgbaOffsets[1]);
                pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(6, PA_LSR, 16), 0);
                pixelArmMLA(c, 2, 2, 14, 14);
                pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFF00u), 0);
            }
            if (rbPair)
            {
                pixelArmUXTB16(c, 1, 12, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
                if (hoistOnePair)
                {
                    pixelArmUADD16(c, 1, 1, 14);
                }
                else
                {
                    pixelArmOp(c, PA_ADD, 1, 1, pixelArmImmediate(1u), 0);
                    pixelArmOp(c, PA_ADD, 1, 1, pixelArmImmediate(0x10000u), 0);
                }
                pixelArmWord(c, 0xE16000C0u | (3u << 16) | ((unsigned int)(swapRB ? 5 : 7) << 8) | 1u); /* SMULBT r3, r1, r5/r7 */
                pixelArmWord(c, 0xE16000E0u | (1u << 16) | ((unsigned int)(swapRB ? 7 : 5) << 8) | 1u); /* SMULTT r1, r1, r7/r5 */
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
                pixelArmPKHBT(c, 1, 3, 1, 8);
            }
            else
            {
                pixelArmUXTB(c, 1, 12, tex->rgbaOffsets[0]);
                pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(7, PA_LSR, 16), 0);
                pixelArmMLA(c, 1, 1, 14, 14);
                pixelArmUXTB(c, 3, 12, tex->rgbaOffsets[2]);
                pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(5, PA_LSR, 16), 0);
                pixelArmMLA(c, 3, 3, 14, 14);
                if (swapRB)
                {
                    pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 8), 0);
                    pixelArmPKHBT(c, 1, 3, 1, 8);
                }
                else
                {
                    pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
                    pixelArmPKHBT(c, 1, 1, 3, 8);
                }
            }
            pixelArmOp(c, PA_ORR, 1, 1, smulwtG ? pixelArmShift(2, PA_LSL, 8) : 2, 0);
            pixelArmWord(c, 0xE4881004u); /* STR r1, [r8], #4 */
            if (k < 3)
            {
                pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 10, 0);
                pixelArmOp(c, PA_ADD, 5, 5, 11, 0);
            }
        }
    }

    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8910EE0u); /* LDMIA r1, {r5, r6, r7, r9, r10, r11} */
    pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(4), 1);
    pixelArmBranch(c, PA_EQ, lblRowEnd);
    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_LT, lblFast4);
}

static void pixelArmEmitConstFogFast4(ARMPixelCode_s *c, const _grSignature *sig,
                                      unsigned int textureLayout, int depth,
                                      int incFog, int rejectCondition, int swapRB,
                                      int isConstMod,
                                      int lblPixel, int lblFast4, int lblRowEnd)
{
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    int pal8 = tex->paletteFormat == GR_PALETTEFORMAT_8BIT;
    int dSReg = depth ? 9 : 7;
    int dTReg = depth ? 10 : 9;
    int palReg = depth ? 7 : 10;
    int texDst[4] = { 12, 2, 3, 14 };
    int k;

    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_GE, lblPixel);

    c->labels[lblFast4] = c->count;
    if (depth)
    {
        pixelArmWord(c, 0xE1D910B0u); /* LDRH r1, [r9] */
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 14, 7, 10, 0);
        pixelArmWord(c, 0xE1D910B2u); /* LDRH r1, [r9, #2] */
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 2, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);
        pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(2, PA_LSL, 16), 0);

        pixelArmOp(c, PA_ADD, 14, 14, 10, 0);
        pixelArmWord(c, 0xE1D910B4u); /* LDRH r1, [r9, #4] */
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 2, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 14, 14, 10, 0);
        pixelArmWord(c, 0xE1D910B6u); /* LDRH r1, [r9, #6] */
        pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 14), 0);
        pixelArmOp(c, PA_CMP, 0, 14, 1, 1);
        pixelArmBranch(c, rejectCondition, lblPixel);

        pixelArmOp(c, PA_ADD, 7, 7, pixelArmShift(10, PA_LSL, 2), 0);
        if (sig->depthMask)
        {
            pixelArmWord(c, 0xE0C930B2u); /* STRH r3, [r9], #2 */
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 16), 0);
            pixelArmWord(c, 0xE0C930B2u); /* STRH r3, [r9], #2 */
            pixelArmWord(c, 0xE0C920B2u); /* STRH r2, [r9], #2 */
            pixelArmWord(c, 0xE0C9E0B2u); /* STRH r14, [r9], #2 */
        }
        else
        {
            pixelArmOp(c, PA_ADD, 9, 9, pixelArmImmediate(8), 0);
        }
    }

    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8810EE0u); /* STMIA r1, {r5, r6, r7, r9, r10, r11} */
    pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
    if (depth)
        pixelArmWord(c, 0xE8910600u); /* LDMIA r1, {r9, r10} */
    else
        pixelArmWord(c, 0xE8910280u); /* LDMIA r1, {r7, r9} */
    if (pal8)
        pixelArmLoad(c, palReg, 0, PA_PALETTE);

    for (k = 0; k < 4; ++k)
    {
        int dstR = texDst[k];
        pixelArmEmitTexIdxConst(c, textureLayout, dstR, 1);
        if (pal8)
            pixelArmWord(c, 0xE7DB0000u | ((unsigned int)dstR << 12) | (unsigned int)dstR);
        else
            pixelArmWord(c, 0xE79B0100u | ((unsigned int)dstR << 12) | (unsigned int)dstR);
        pixelArmOp(c, PA_ADD, 5, 5, (unsigned int)dSReg, 0);
        pixelArmOp(c, PA_ADD, 6, 6, (unsigned int)dTReg, 0);
        if (pal8)
            pixelArmWord(c, 0xE7900100u | ((unsigned int)palReg << 16) |
                         ((unsigned int)dstR << 12) | (unsigned int)dstR);
    }

    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8810060u); /* STMIA r1, {r5, r6} */
    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(32), 0);
    pixelArmWord(c, 0xE881400Cu); /* STMIA r1, {r2, r3, r14} */

    if (incFog)
    {
        pixelArmLoad(c, 9, 0, -56);
        pixelArmLoad(c, 10, 0, -52);
        pixelArmOp(c, PA_ADD, 1, 9, pixelArmShift(10, PA_LSL, 2), 0);
        pixelArmStore(c, 1, 0, -56);
    }
    else
    {
        pixelArmLoad(c, 9, 0, -20);
        pixelArmLoad(c, 10, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
        pixelArmMLA(c, 9, 10, 4, 9);
    }
    if (sig->monochromeFog)
    {
        pixelArmLoad(c, 7, 0, PA_FOG_COLOR);
        pixelArmOp(c, PA_ORR, 11, 7, pixelArmShift(7, PA_LSL, 16), 0);
    }
    else
    {
        pixelArmLoadB(c, 7, 0, PA_FOG_COLOR + 1);
        pixelArmLoad(c, 11, 0, -8);
    }
    if (isConstMod)
    {
        pixelArmLoad(c, 5, 0, -4);
        pixelArmUXTB(c, 6, 5, 8);
        if (swapRB)
        {
            pixelArmUXTB16(c, 5, 5, 0);
        }
        else
        {
            pixelArmUXTB(c, 1, 5, 16);
            pixelArmOp(c, PA_AND, 2, 5, pixelArmImmediate(0xFFu), 0);
            pixelArmPKHBT(c, 5, 1, 2, 16);
        }
        pixelArmOp(c, PA_MOV, 14, 0, pixelArmImmediate(1u), 0);
        pixelArmOp(c, PA_ORR, 14, 14, pixelArmImmediate(0x10000u), 0);
    }

    for (k = 0; k < 4; ++k)
    {
        if (k > 0)
            pixelArmLoad(c, 12, 0, -32 + 4 * (k - 1));
        if (isConstMod)
        {
            pixelArmUXTB(c, 3, 12, tex->rgbaOffsets[1]);
            pixelArmMLA(c, 3, 3, 6, 6);
            if ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16)
            {
                pixelArmUXTB16(c, 1, 12, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
            }
            else
            {
                pixelArmUXTB(c, 1, 12, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 2, 12, tex->rgbaOffsets[2]);
                pixelArmPKHBT(c, 1, swapRB ? 2 : 1, swapRB ? 1 : 2, 16);
            }
            pixelArmOp(c, PA_ADD, 1, 1, 14, 0);
            pixelArmWord(c, 0xE16000E0u | (2u << 16) | (5u << 8) | 1u); /* SMULTT r2, r1, r5 */
            pixelArmWord(c, 0xE1600080u | (1u << 16) | (5u << 8) | 1u); /* SMULBB r1, r1, r5 */
            pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8), 0);
            pixelArmPKHBT(c, 1, 1, 2, 8);
            pixelArmOp(c, PA_MOV, 12, 0, pixelArmShift(3, PA_LSR, 8), 0);
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(9, PA_LSR, 16), 0);
            pixelArmOp(c, PA_RSB, 3, 2, pixelArmImmediate(0x100u), 0);
            pixelArmMul(c, 1, 1, 3);
            pixelArmMul(c, 12, 12, 3);
            pixelArmMLA(c, 12, 7, 2, 12);
            pixelArmMLA(c, 1, 11, 2, 1);
            if (k < 3)
                pixelArmOp(c, PA_ADD, 9, 9, 10, 0);
            pixelArmUXTB16(c, 3, 1, 8);
            pixelArmOp(c, PA_AND, 2, 12, pixelArmImmediate(0xFF00u), 0);
            pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
            pixelArmWord(c, 0xE4883004u); /* STR r3, [r8], #4 */
        }
        else
        {
            pixelArmUXTB(c, 14, 12, tex->rgbaOffsets[1]);
            if ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16)
            {
                pixelArmUXTB16(c, 1, 12, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
            }
            else
            {
                pixelArmUXTB(c, 3, 12, tex->rgbaOffsets[0]);
                pixelArmUXTB(c, 2, 12, tex->rgbaOffsets[2]);
                pixelArmPKHBT(c, 1, swapRB ? 2 : 3, swapRB ? 3 : 2, 16);
            }
            pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(9, PA_LSR, 16), 0);
            pixelArmOp(c, PA_RSB, 3, 2, pixelArmImmediate(0x100u), 0);
            pixelArmMul(c, 1, 1, 3);
            pixelArmMul(c, 14, 14, 3);
            pixelArmMLA(c, 14, 7, 2, 14);
            pixelArmMLA(c, 1, 11, 2, 1);
            if (k < 3)
                pixelArmOp(c, PA_ADD, 9, 9, 10, 0);
            pixelArmUXTB16(c, 3, 1, 8);
            pixelArmOp(c, PA_AND, 2, 14, pixelArmImmediate(0xFF00u), 0);
            pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
            pixelArmWord(c, 0xE4883004u); /* STR r3, [r8], #4 */
        }
    }

    if (!isConstMod)
        pixelArmOp(c, PA_MOV, 12, 0, 11, 0);
    pixelArmOp(c, PA_SUB, 1, 0, pixelArmImmediate(64), 0);
    pixelArmWord(c, 0xE8910EE0u); /* LDMIA r1, {r5, r6, r7, r9, r10, r11} */
    pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(4), 1);
    pixelArmBranch(c, PA_EQ, lblRowEnd);
    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_LT, lblFast4);
}

static void pixelArmEmitMipReplaceFogFast4(ARMPixelCode_s *c, const _grSignature *sig,
                                           int swapRB, int lblPixel, int lblFast4,
                                           int lblRowEnd)
{
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    int k;

    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_GE, lblPixel);

    pixelArmOp(c, PA_SUB, 13, 13, pixelArmImmediate(28), 0); /* SUB sp, sp, #28 */
    pixelArmLoad(c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
    pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
    pixelArmWord(c, 0xE88D0007u); /* STMIA sp, {r0, r1, r2}: params, sAdd, tAdd */

    c->labels[lblFast4] = c->count;
    pixelArmWord(c, 0xE99D0003u); /* LDMIB sp, {r0, r1}: sAdd, tAdd */
    pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(12, PA_LSR, 16), 0); /* lr = log2S */
    for (k = 0; k < 4; ++k)
    {
        pixelArmWord(c, 0xE1A02C35u); /* LSR r2, r5, r12 */
        pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(12, PA_LSR, 8), 0);
        pixelArmWord(c, 0xE1A03336u); /* LSR r3, r6, r3 */
        pixelArmWord(c, 0xE1822E13u); /* ORR r2, r2, r3, LSL lr */
        pixelArmWord(c, 0xE79B2102u); /* LDR r2, [r11, r2, LSL #2] */
        pixelArmOp(c, PA_ADD, 5, 5, 0, 0);
        pixelArmOp(c, PA_ADD, 6, 6, 1, 0);
        pixelArmStore(c, 2, 13, 12 + 4 * k);
    }

    pixelArmOp(c, PA_MOV, 0, 0, pixelArmShift(12, PA_LSR, 24), 0); /* r0 = fogG */
    for (k = 0; k < 4; ++k)
    {
        pixelArmLoad(c, 1, 13, 12 + 4 * k);
        pixelArmUXTB(c, 14, 1, tex->rgbaOffsets[1]);
        if ((tex->rgbaOffsets[0] ^ tex->rgbaOffsets[2]) == 16)
        {
            pixelArmUXTB16(c, 1, 1, swapRB ? tex->rgbaOffsets[2] : tex->rgbaOffsets[0]);
        }
        else
        {
            pixelArmUXTB(c, 3, 1, tex->rgbaOffsets[0]);
            pixelArmUXTB(c, 2, 1, tex->rgbaOffsets[2]);
            pixelArmPKHBT(c, 1, swapRB ? 2 : 3, swapRB ? 3 : 2, 16);
        }
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(7, PA_LSR, 16), 0);
        pixelArmOp(c, PA_RSB, 3, 2, pixelArmImmediate(0x100u), 0);
        pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
        pixelArmMul(c, 1, 1, 3);
        pixelArmMul(c, 14, 14, 3);
        pixelArmMLA(c, 14, 0, 2, 14);
        pixelArmMLA(c, 1, 10, 2, 1);
        pixelArmUXTB16(c, 3, 1, 8);
        pixelArmOp(c, PA_AND, 2, 14, pixelArmImmediate(0xFF00u), 0);
        pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
        pixelArmWord(c, 0xE4883004u); /* STR r3, [r8], #4 */
    }

    pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(4), 1); /* ADDS r4, r4, #4 */
    pixelArmWord(c, 0x059D0000u); /* LDREQ r0, [sp] */
    pixelArmWord(c, 0x028DD01Cu); /* ADDEQ sp, sp, #28 */
    pixelArmBranch(c, PA_EQ, lblRowEnd);
    pixelArmWord(c, 0xE3740003u); /* CMN r4, #3 */
    pixelArmBranch(c, PA_LT, lblFast4);
    pixelArmLoad(c, 0, 13, 0);
    pixelArmOp(c, PA_ADD, 13, 13, pixelArmImmediate(28), 0);
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
    int lblRow = lblBase + PA_OFF_ROW;
    int lblPixel = lblBase + PA_OFF_PIXEL;
    int lblShade = lblBase + PA_OFF_SHADE;
    int lblRowEnd = lblBase + PA_OFF_ROW_END;
    int lblReject = lblBase + PA_OFF_REJECT;
    int lblRejectLoop = lblBase + PA_OFF_REJECT_LOOP;
    int lblAlphaReject = lblBase + PA_OFF_ALPHA_REJECT;
    int lblSpan = lblBase + PA_OFF_SPAN;
    int lblEmptyRow = lblBase + PA_OFF_EMPTY_ROW;
    int lblFast4 = lblBase + PA_OFF_FAST4;
    int lblCV = lblBase + PA_OFF_CV;
    int lblDone = lblBase + PA_OFF_DONE;
    int fastFog32 = !rgb565 && dst->rgbaOffsets[1] == 8 &&
        ((dst->rgbaOffsets[0] == 16 && dst->rgbaOffsets[2] == 0) ||
         (dst->rgbaOffsets[0] == 0 && dst->rgbaOffsets[2] == 16));
    int swapRB = fastFog32 && dst->rgbaOffsets[0] == 16;
    int isBlendSWAR32 = (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE) &&
        fastFog32 && (!dstAlpha || dst->rgbaOffsets[3] == 24);
    int isBlendOneOne32 = (blendMode == PA_BLEND_ONE_ONE) && fastFog32;
    int isBlendGeneral = (blendMode != PA_BLEND_NONE) && !isBlendSWAR32 && !isBlendOneOne32;
    int clampS = t[PA_TEX_WRAP_S] == GR_WRAPMODE_CLAMP_TO_EDGE;
    int clampT = t[PA_TEX_WRAP_T] == GR_WRAPMODE_CLAMP_TO_EDGE;
    int keepSTAdders = !isBilinear && (blendMode == PA_BLEND_NONE) && !applyModulate && !fog &&
        (!isConstRgb || isConstWhite) &&
        (alphaTest ? (alphaMode == 1 || (isConstWhite && alphaMode == 3)) : alphaMode < 2);
    int incFog = fog && !depth && !alphaTest && !applyModulate;
    int packShiftsR12 = (!isBilinear && textureLayout == 0 && !keepSTAdders &&
                         ((!applyModulate && fog) || (applyModulate && isConstRgb && fog)));
    int preloadFogRBInR12 = !isBilinear && fog && !packShiftsR12 && !keepSTAdders &&
        !applyModulate && fastFog32 && !dither;
    int preserveR12 = packShiftsR12 || preloadFogRBInR12;
    int texReg = keepSTAdders ? 3 : (preloadFogRBInR12 ? 14 : (packShiftsR12 ? ((!applyModulate && !alphaTest) ? 1 : 14) : 12));
    int gradTmp = preserveR12 ? 10 : 12;
    int preloadPaletteR10 = !isBilinear && tex->paletteFormat == GR_PALETTEFORMAT_8BIT &&
        keepSTAdders && !depth && !incFog && !dither && !alphaTest;
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
    int preloadConstFogRB = fog && !packShiftsR12 && isConstRgb && applyModulate &&
        fastFog32 && !sig->monochromeFog;
    int emitAlphaScratch = !alphaTest &&
        (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE ||
         (dstAlpha && (fog || isBlendGeneral)));
    int haveR2Const = !isConstWhite && (alphaTest || emitAlphaScratch) && (alphaMode >= 2);
    int bilinearClearByte3 = isBilinear && !dstAlpha && !alphaTest &&
        blendMode != PA_BLEND_SRC_ALPHA && blendMode != PA_BLEND_SRC_ALPHA_ONE &&
        tex->rgbaOffsets[0] <= 16 && tex->rgbaOffsets[1] <= 16 && tex->rgbaOffsets[2] <= 16;
    int needSpanEnd = (fog && !incFog) || (!isConstRgb && modulate) || (!isConstRgb && alphaMode >= 2);
    int needSpanEndAfterFog = !isConstRgb && (modulate || alphaMode >= 2);
    int batchSTWithR12 = !preserveR12;
    int batchSTKeepR10R12 = !batchSTWithR12;
    int r10HasXZ = 0;
    int emitEmptyRow = c->capacity >= 800;
    int emitConstVar = (colorVariant == 1) && !isBilinear && applyModulate && !isConstWhite &&
        !alphaMode && !fog && !alphaTest && !dstAlpha && blendMode == PA_BLEND_NONE &&
        c->capacity >= 800;
    int emitFast4 = (colorVariant == 0 || colorVariant == 1) && !isBilinear && applyModulate &&
        !isConstWhite && !dstAlpha && !fog && !alphaTest && blendMode == PA_BLEND_NONE &&
        textureLayout != 0 && fastFog32 &&
        (!depth || (!sig->depthClampMin && !sig->depthClampMax)) && c->capacity >= 800;
    int isConstModFogFast4 = (colorVariant == 1) && applyModulate;
    int emitConstFogFast4 = (isConstModFogFast4 || !applyModulate) && !isBilinear &&
        !dstAlpha && fog && fastFog32 && !dither && !alphaTest &&
        blendMode == PA_BLEND_NONE && textureLayout != 0 &&
        (!depth || (!sig->depthClampMin && !sig->depthClampMax)) && c->capacity >= 800;
    int emitMipReplaceFogFast4 = !applyModulate && !isBilinear && !dstAlpha && fog &&
        fastFog32 && !sig->monochromeFog && !depth && !dither && !alphaTest &&
        blendMode == PA_BLEND_NONE && textureLayout == 0 && !clampS && !clampT &&
        tex->paletteFormat != GR_PALETTEFORMAT_8BIT && c->capacity >= 800;
    int channel, outputRegister;

    c->labels[lblRow] = c->count;
    if (emitFast4 && !isConstRgb)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE891000Eu); /* LDMIA r1, {r1, r2, r3} */
        pixelArmOp(c, PA_ADD, 1, 1, pixelArmImmediate(0x00400000u), 0);
        pixelArmOp(c, PA_ADD, 2, 2, pixelArmImmediate(0x00400000u), 0);
        pixelArmOp(c, PA_ADD, 3, 3, pixelArmImmediate(0x00400000u), 0);
        pixelArmOp(c, PA_ORR, 1, 1, 2, 0);
        pixelArmOp(c, PA_ORR, 1, 1, 3, 0);
        pixelArmOp(c, PA_CMP, 0, 1, pixelArmImmediate(0x00800000u), 1);
        pixelArmWord(c, 0x23A014FFu); /* MOVCS r1, #0xFF000000 */
        pixelArmWord(c, 0x33A01000u); /* MOVCC r1, #0 */
        pixelArmStore(c, 1, 0, -16);
    }
    if (preloadPaletteR10)
        pixelArmLoad(c, 10, 0, PA_PALETTE);
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
    else if (preloadFogRBInR12)
    {
        pixelArmLoad(c, 12, 0, PA_FOG_COLOR);
        if (sig->monochromeFog)
            pixelArmOp(c, PA_ORR, 12, 12, pixelArmShift(12, PA_LSL, 16), 0);
        else
            pixelArmUXTB16(c, 12, 12, swapRB ? 16 : 0);
        if (emitConstFogFast4 && !sig->monochromeFog)
            pixelArmStore(c, 12, 0, -8);
    }
    c->labels[lblSpan] = c->count;
    pixelArmLoad(c, 1, 0, PA_SPANS);
    pixelArmWord(c, 0xE4912004u); /* LDR r2, [r1], #4. */
    pixelArmStore(c, 1, 0, PA_SPANS);
    pixelArmOp(c, PA_MOV, 4, 0, pixelArmShift(2, PA_LSR, 16), 0);
    if (emitEmptyRow)
    {
        pixelArmOp(c, PA_RSB, 4, 4, pixelArmImmediate(0), 1);
        pixelArmBranch(c, PA_GE, lblEmptyRow);
    }
    pixelArmWord(c, 0xE6FF3072u); /* UXTH r3, r2. */
    if (needSpanEnd)
        pixelArmOp(c, emitEmptyRow ? PA_SUB : PA_ADD, 2, 3, 4, 0);
    if (!isConstRgb && modulate && !preserveR12)
    {
        /* Batch R/G/B span setup before row pointers and Z/S/T while r5..r8, r10, r12 are free. */
        pixelArmOp(c, PA_ADD, 8, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89800E0u); /* LDMIA r8, {r5, r6, r7} */
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
        pixelArmWord(c, 0xE88800E0u); /* STMIA r8, {r5, r6, r7} */
        if (alphaMode >= 2)
            pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, gradTmp, 2, -16, gradTmp);
        if (depth)
        {
            /* PA_COLOR_ROW/PA_DEPTH_ROW and PA_COLOR_STRIDE/PA_DEPTH_STRIDE are adjacent. */
            pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_COLOR_ROW), 0);
            pixelArmWord(c, 0xE8910300u); /* LDMIA r1, {r8, r9} */
            pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_COLOR_STRIDE), 0);
            pixelArmWord(c, 0xE89A4400u); /* LDMIA r10, {r10, r14} */
            pixelArmOp(c, PA_ADD, 10, 8, 10, 0);
            pixelArmOp(c, PA_ADD, 14, 9, 14, 0);
            pixelArmWord(c, 0xE8814400u); /* STMIA r1, {r10, r14} */
            pixelArmOp(c, PA_ADD, 8, 8, pixelArmShift(3, PA_LSL, colorShift), 0);
            pixelArmOp(c, PA_ADD, 9, 9, pixelArmShift(3, PA_LSL, 1), 0);
            if (fog)
            {
                /* Batch DEPTH + FOG gradients (indices 0, 1); r10 keeps xZ. */
                pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE), 0);
                pixelArmWord(c, 0xE8910060u); /* LDMIA r1, {r5, r6} */
                pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_X_ADDER), 0);
                pixelArmWord(c, 0xE89A4400u); /* LDMIA r10, {r10, r14} */
                pixelArmMLA(c, 7, 10, 3, 5);
                pixelArmMLA(c, 14, 2, 14, 6);
                pixelArmStore(c, 14, 0, -20);
                pixelArmOp(c, PA_ADD, 14, 0, pixelArmImmediate(PA_Y_ADDER), 0);
                pixelArmWord(c, 0xE89E4004u); /* LDMIA r14, {r2, r14} */
                pixelArmOp(c, PA_ADD, 5, 5, 2, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
                pixelArmWord(c, 0xE8810060u); /* STMIA r1, {r5, r6} */
                r10HasXZ = !dither;
            }
            else
            {
                pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, (!dither && gradTmp != 10) ? 10 : gradTmp);
                r10HasXZ = !dither;
            }
        }
        else
        {
            pixelArmRowPointer(c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
            if (fog) pixelArmGradientEx(c, GR_GRADIENT_FOG, gradTmp, 2, -20, gradTmp);
        }
    }
    else
    {
        int fogHandled = 0;
        if (depth)
        {
            pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_COLOR_ROW), 0);
            pixelArmWord(c, 0xE8910300u); /* LDMIA r1, {r8, r9} */
            pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_COLOR_STRIDE), 0);
            pixelArmWord(c, 0xE89A4400u); /* LDMIA r10, {r10, r14} */
            pixelArmOp(c, PA_ADD, 10, 8, 10, 0);
            pixelArmOp(c, PA_ADD, 14, 9, 14, 0);
            pixelArmWord(c, 0xE8814400u); /* STMIA r1, {r10, r14} */
            pixelArmOp(c, PA_ADD, 8, 8, pixelArmShift(3, PA_LSL, colorShift), 0);
            pixelArmOp(c, PA_ADD, 9, 9, pixelArmShift(3, PA_LSL, 1), 0);
            if (fog && !incFog && (!needSpanEndAfterFog || !preserveR12))
            {
                /* Batch DEPTH + FOG gradients (indices 0, 1); r10 keeps xZ. */
                int yZReg = needSpanEndAfterFog ? 12 : 2;
                pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE), 0);
                pixelArmWord(c, 0xE8910060u); /* LDMIA r1, {r5, r6} */
                pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_X_ADDER), 0);
                pixelArmWord(c, 0xE89A4400u); /* LDMIA r10, {r10, r14} */
                pixelArmMLA(c, 7, 10, 3, 5);
                pixelArmMLA(c, 14, 2, 14, 6);
                pixelArmStore(c, 14, 0, -20);
                pixelArmOp(c, PA_ADD, 14, 0, pixelArmImmediate(PA_Y_ADDER), 0);
                pixelArmWord(c, needSpanEndAfterFog ? 0xE89E5000u : 0xE89E4004u);
                pixelArmOp(c, PA_ADD, 5, 5, (unsigned int)yZReg, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
                pixelArmWord(c, 0xE8810060u); /* STMIA r1, {r5, r6} */
                r10HasXZ = !dither;
                fogHandled = 1;
            }
            else
            {
                pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, (!dither && gradTmp != 10) ? 10 : gradTmp);
                r10HasXZ = !dither;
            }
        }
        else
        {
            pixelArmRowPointer(c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
        }
        if (fog && !fogHandled)
        {
            if (incFog)
            {
                pixelArmGradientEx(c, GR_GRADIENT_FOG, 7, 3, 0, gradTmp);
                pixelArmLoad(c, 9, 0, PA_X_ADDER + 4 * GR_GRADIENT_FOG);
            }
            else
            {
                pixelArmGradientEx(c, GR_GRADIENT_FOG, gradTmp, 2, -20, gradTmp);
                if (gradTmp == 10) r10HasXZ = 0;
            }
        }
        if (!isConstRgb)
        {
            if (modulate)
            {
                for (channel = 0; channel < 3; ++channel)
                    pixelArmGradientEx(c, GR_GRADIENT_COLOR_R + channel, gradTmp, 2,
                                       -4 * (channel + 1), gradTmp);
                if (gradTmp == 10) r10HasXZ = 0;
            }
            if (alphaMode >= 2)
            {
                pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, gradTmp, 2, -16, gradTmp);
                if (gradTmp == 10) r10HasXZ = 0;
            }
        }
    }
    if (dither)
    {
        pixelArmEmitDitherRow(c);
        r10HasXZ = 0;
    }
    if (batchSTWithR12)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8910060u); /* LDMIA r1, {r5, r6} */
        pixelArmOp(c, PA_ADD, 2, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8925000u); /* LDMIA r2, {r12, r14} */
        pixelArmOp(c, PA_ADD, 2, 5, 12, 0);
        pixelArmOp(c, PA_ADD, 12, 6, 14, 0);
        pixelArmWord(c, 0xE8811004u); /* STMIA r1, {r2, r12} */
        pixelArmOp(c, PA_ADD, 2, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8925000u); /* LDMIA r2, {r12, r14} */
        pixelArmMLA(c, 5, 12, 3, 5);
        pixelArmMLA(c, 6, 14, 3, 6);
        if (!keepSTAdders && !depth && !incFog)
        {
            pixelArmOp(c, PA_MOV, 7, 0, 12, 0);
            pixelArmOp(c, PA_MOV, 9, 0, 14, 0);
        }
    }
    else if (batchSTKeepR10R12)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8910060u); /* LDMIA r1, {r5, r6} */
        pixelArmOp(c, PA_ADD, 2, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8924004u); /* LDMIA r2, {r2, r14} */
        pixelArmOp(c, PA_ADD, 2, 5, 2, 0);
        pixelArmOp(c, PA_ADD, 14, 6, 14, 0);
        pixelArmWord(c, 0xE8814004u); /* STMIA r1, {r2, r14} */
        pixelArmOp(c, PA_ADD, 2, 0, pixelArmImmediate(PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S), 0);
        pixelArmWord(c, 0xE8924004u); /* LDMIA r2, {r2, r14} */
        pixelArmMLA(c, 5, 2, 3, 5);
        pixelArmMLA(c, 6, 14, 3, 6);
        if (!depth && !incFog)
        {
            pixelArmOp(c, PA_MOV, 7, 0, 2, 0);
            pixelArmOp(c, PA_MOV, 9, 0, 14, 0);
        }
    }
    if (depth && !dither && !r10HasXZ) pixelArmLoad(c, 10, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
    if (!depth && !dither && alphaTest) pixelArmLoad(c, 10, 0, PA_ALPHA_REF);
    if (preloadFogRB)
    {
        pixelArmLoad(c, 10, 0, PA_FOG_COLOR);
        pixelArmUXTB16(c, 10, 10, swapRB ? 16 : 0);
    }
    if (!emitEmptyRow)
    {
        pixelArmOp(c, PA_RSB, 4, 4, pixelArmImmediate(0), 1);
        pixelArmBranch(c, PA_GE, lblRowEnd);
    }
    if (emitFast4)
    {
        pixelArmEmitModulateFast4(c, sig, textureLayout, depth, rejectCondition, swapRB,
                                  isConstRgb, lblPixel, lblFast4, lblRowEnd);
    }
    else if (emitConstFogFast4)
    {
        pixelArmEmitConstFogFast4(c, sig, textureLayout, depth, incFog, rejectCondition,
                                   swapRB, isConstModFogFast4, lblPixel, lblFast4, lblRowEnd);
    }
    else if (emitMipReplaceFogFast4)
    {
        pixelArmEmitMipReplaceFogFast4(c, sig, swapRB, lblPixel, lblFast4, lblRowEnd);
    }

    c->labels[lblPixel] = c->count;
    if (depth)
    {
        pixelArmWord(c, 0xE0D910B2u); /* LDRH r1, [r9], #2. */
        pixelArmClampDepth16(c, 3, 2, sig->depthClampMin, sig->depthClampMax);
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
        pixelArmEmitBilinearTexel(c, tex, textureLayout, texReg, bilinearClearByte3,
                                  clampS, clampT);
    }
    else
    {
        pixelArmTextureIndex(c, textureLayout, packShiftsR12, clampS, clampT);
        if (tex->paletteFormat == GR_PALETTEFORMAT_8BIT)
        {
            pixelArmWord(c, 0xE7DB2002u); /* LDRB r2, [r11, r2]. */
            if (!preloadPaletteR10)
                pixelArmLoad(c, 1, 0, PA_PALETTE);
            if (!alphaTest && keepSTAdders && !isConstWhite)
            {
                /* Step S/T during LDRB + PA_PALETTE latency before the dependent palette LDR. */
                pixelArmOp(c, PA_ADD, 5, 5, 12, 0);
                pixelArmOp(c, PA_ADD, 6, 6, 14, 0);
            }
            pixelArmWord(c, (preloadPaletteR10 ? 0xE79A0102u : 0xE7910102u) |
                         ((unsigned int)texReg << 12));
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
            pixelArmStepSTZ(c, depth, dither, 0, incFog, preserveR12 ? texReg : 0, 0,
                            preserveR12 ? ((texReg == 1) ? 14 : 2) : 14);
        }
    }
    else
    {
        /* Compute alpha into r1, keep texReg and r2 (colPacked if const). */
        if (alphaMode == 1 || (isConstWhite && alphaMode == 3))
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
        int preserveR2 = !keepSTAdders && haveR2Const && isConstRgb;
        int refReg = preserveR2 ? 3 : 2;
        if (!depth && !dither)
        {
            pixelArmOp(c, PA_CMP, 0, 1, 10, 1);
        }
        else
        {
            pixelArmLoad(c, refReg, 0, PA_ALPHA_REF);
            pixelArmOp(c, PA_CMP, 0, 1, refReg, 1);
        }
        pixelArmBranch(c, alphaRejectCond, lblAlphaReject);
        if (needAlphaScratch) pixelArmStore(c, 1, 0, -24);
        if (depth && sig->depthMask)
        {
            if (!isBilinear && textureLayout != 0 && texReg != 3 &&
                (alphaMode == 1 || (isConstWhite && alphaMode == 3)))
            {
                pixelArmWord(c, 0xE14930B2u); /* STRH r3, [r9, #-2]. */
            }
            else
            {
                pixelArmClampDepth16(c, 1, refReg, sig->depthClampMin, sig->depthClampMax);
                pixelArmWord(c, 0xE14910B2u); /* STRH r1, [r9, #-2]. */
            }
        }
        if (keepSTAdders)
            pixelArmStepSTZ(c, depth, dither, 1, 0, 0, 0, 2);
        else
        {
            /* Keep r2 (colPacked when preserveR2) and texReg intact across S/T/Z stepping. */
            pixelArmStepSTZ(c, depth, dither, 0, 0, preserveR2 ? 2 : 0, preserveR12 ? texReg : 0,
                            preserveR12 ? 1 : 14);
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
                if (unused == 0xFF000000u && !(bilinearClearByte3 && unused == 0xFF000000u) &&
                    !(t[PA_TEX_RAW_COPY] && !sig->perPixelPack))
                {
                    pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 8), 0);
                }
                else
                {
                    pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_ROR, 8), 0);
                    if (unused && !(bilinearClearByte3 && unused == 0xFF000000u) &&
                        !(t[PA_TEX_RAW_COPY] && !sig->perPixelPack))
                        pixelArmOp(c, PA_BIC, 2, 2, pixelArmImmediate(unused), 0);
                }
                outputRegister = 2;
            }
            else if (!applyModulate)
            {
                outputRegister = keepSTAdders ? 2 : 14;
                if (!deferredZ && rgb565 && !dither && !dstAlpha && outputRegister != texReg &&
                    tex->rgbaOffsets[1] == 8 &&
                    ((tex->rgbaOffsets[0] == 0 && tex->rgbaOffsets[2] == 16) ||
                     (tex->rgbaOffsets[0] == 16 && tex->rgbaOffsets[2] == 0)))
                {
                    if (tex->rgbaOffsets[0] == 0)
                    {
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xF8u), 0);
                        pixelArmOp(c, PA_MOV, outputRegister, 0, pixelArmShift(1, PA_LSL, 8), 0);
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xFC00u), 0);
                        pixelArmOp(c, PA_ORR, outputRegister, outputRegister, pixelArmShift(1, PA_LSR, 5), 0);
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xF80000u), 0);
                        pixelArmOp(c, PA_ORR, outputRegister, outputRegister, pixelArmShift(1, PA_LSR, 19), 0);
                    }
                    else
                    {
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xF80000u), 0);
                        pixelArmOp(c, PA_MOV, outputRegister, 0, pixelArmShift(1, PA_LSR, 8), 0);
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xFC00u), 0);
                        pixelArmOp(c, PA_ORR, outputRegister, outputRegister, pixelArmShift(1, PA_LSR, 5), 0);
                        pixelArmOp(c, PA_AND, 1, texReg, pixelArmImmediate(0xF8u), 0);
                        pixelArmOp(c, PA_ORR, outputRegister, outputRegister, pixelArmShift(1, PA_LSR, 3), 0);
                    }
                }
                else if (deferredZ)
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
        else if (preloadFogRBInR12)
        {
            /* texReg is r14; r12 holds pre-swapped fogRB (low byte == fogG when monochromeFog). */
            if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
            if (sig->monochromeFog)
            {
                pixelArmMLA(c, 14, 12, 2, 14);
            }
            else
            {
                pixelArmLoadB(c, 3, 0, PA_FOG_COLOR + 1);
                pixelArmMLA(c, 14, 3, 2, 14);
            }
            pixelArmMLA(c, 1, 12, 2, 1);
        }
        else if (preloadConstFogRB)
        {
            pixelArmLoadB(c, 12, 0, PA_FOG_COLOR + 1);
            if (incFog) pixelArmOp(c, PA_ADD, 7, 7, 9, 0);
            pixelArmMLA(c, 14, 12, 2, 14);
            pixelArmLoad(c, 3, 0, -8);
            pixelArmMLA(c, 1, 3, 2, 1);
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
            pixelArmUXTB(c, 2, 14, 8);
        }
        else if (isBlendGeneral)
        {
            if (swapRB)
            {
                pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(1, PA_LSR, 24), 0);
                pixelArmUXTB(c, 2, 14, 8);
                pixelArmUXTB(c, 1, 1, 8);
            }
            else
            {
                pixelArmUXTB(c, 3, 1, 8);
                pixelArmUXTB(c, 2, 14, 8);
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
            else if (rgb565 && !dither && !dstAlpha)
            {
                pixelArmOp(c, PA_AND, 3, 1, pixelArmImmediate(0xF800u), 0);
                pixelArmOp(c, PA_AND, 2, 14, pixelArmImmediate(0xFC00u), 0);
                pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(2, PA_LSR, 5), 0);
                pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(1, PA_LSR, 27), 0);
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
        pixelArmEmitBlend32SWAR(c, blendMode, dstAlpha, blendAlphaInR3, preserveR12);
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
        pixelArmEmitBlendGeneral(c, dst, rgb565, dstAlpha, dither, blendMode, preserveR12, 0);
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
    if (emitConstVar)
    {
        pixelArmBranch(c, PA_EQ, lblDone);
        pixelArmLoad(c, 1, 0, -16);
        pixelArmOp(c, PA_CMP, 0, 1, pixelArmImmediate(0), 1);
        pixelArmBranch(c, PA_NE, lblCV);
        pixelArmBranch(c, PA_AL, lblSpan);
        c->labels[lblDone] = c->count;
    }
    else
    {
        pixelArmBranch(c, PA_NE, lblSpan);
    }
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
        if (preserveR12)
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
        pixelArmClampDepth16(c, 3, 2, sig->depthClampMin, sig->depthClampMax);
        pixelArmOp(c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(c, rejectCondition, lblRejectLoop);
        if (preserveR12)
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
                        preserveR12 ? texReg : 0, 0, preserveR12 ? 1 : 2);
        if (dither) pixelArmOp(c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
        pixelArmOp(c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(c, PA_NE, lblPixel);
        pixelArmBranch(c, PA_AL, lblRowEnd);
    }
    if (emitConstVar)
    {
        c->labels[lblCV] = c->count;
        pixelArmOp(c, PA_ADD, 8, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE898000Eu); /* LDMIA r8, {r1, r2, r3} */
        pixelArmOp(c, PA_ADD, 9, 0, pixelArmImmediate(PA_Y_ADDER + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89900E0u); /* LDMIA r9, {r5, r6, r7} */
        pixelArmOp(c, PA_ADD, 5, 1, 5, 0);
        pixelArmOp(c, PA_ADD, 6, 2, 6, 0);
        pixelArmOp(c, PA_ADD, 7, 3, 7, 0);
        pixelArmWord(c, 0xE88800E0u); /* STMIA r8, {r5, r6, r7} */
        pixelArmUSAT8(c, 1, 1, 16);
        pixelArmUSAT8(c, 2, 2, 16);
        pixelArmUSAT8(c, 3, 3, 16);
        pixelArmOp(c, PA_ORR, 1, 3, pixelArmShift(1, PA_LSL, 16), 0);
        pixelArmOp(c, PA_ORR, 1, 1, pixelArmShift(2, PA_LSL, 8), 0);
        pixelArmStore(c, 1, 0, -4);
        pixelArmBranch(c, PA_AL, lblSpan);
    }
    if (emitEmptyRow)
    {
        c->labels[lblEmptyRow] = c->count;
        pixelArmEmitEmptyRow(c, isConstRgb, applyModulate, alphaMode, fog, depth, dither, lblRowEnd);
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
    int blendAlphaInR3 = isConstRgb && !fog && !alphaTest && isBlendSWAR32;
    int blendAlphaInR14 = isConstRgb && !fog && !alphaTest && !dither && isBlendGeneral &&
        !dstAlpha && (blendMode == PA_BLEND_SRC_ALPHA || blendMode == PA_BLEND_SRC_ALPHA_ONE);
    int needAlphaScratch = needSrcAlpha &&
        (fog || alphaTest || (isBlendSWAR32 && !blendAlphaInR3) ||
         (isBlendGeneral && !blendAlphaInR14));
    int incFog = fog && !depth && !alphaTest;
    int fastFog32 = fog && fast32 && (blendMode == PA_BLEND_NONE || isBlendOneOne32 || isBlendSWAR32);
    int fogSwapRB = fastFog32 && dst->rgbaOffsets[0] == 16;
    int fastUntexConstFog = isConstRgb && fast32 && fog && !sig->monochromeFog &&
        !dither && !dstAlpha && blendMode == PA_BLEND_NONE;
    int needSpanEnd = !isConstRgb || (fog && !incFog);
    int channel;

    c->labels[lblRow] = c->count;
    if (fastUntexConstFog)
    {
        pixelArmLoad(c, 11, 0, PA_FOG_COLOR);
        pixelArmUXTB(c, 14, 11, 8);
        pixelArmUXTB16(c, 11, 11, fogSwapRB ? 16 : 0);
        pixelArmLoad(c, 1, 0, -4);
        pixelArmUXTB16(c, 12, 1, fogSwapRB ? 0 : 16);
        pixelArmUXTB(c, 1, 1, 8);
        pixelArmOp(c, PA_ORR, 14, 1, pixelArmShift(14, PA_LSL, 16), 0);
    }
    else if (fog)
        pixelArmLoad(c, 11, 0, PA_FOG_COLOR);
    else if (!isConstRgb)
        pixelArmLoad(c, 11, 0, PA_X_ADDER + 4 * GR_GRADIENT_COLOR_B);

    c->labels[lblSpan] = c->count;
    pixelArmLoad(c, 1, 0, PA_SPANS);
    pixelArmWord(c, 0xE4912004u); /* LDR r2, [r1], #4. */
    pixelArmStore(c, 1, 0, PA_SPANS);
    pixelArmOp(c, PA_MOV, 4, 0, pixelArmShift(2, PA_LSR, 16), 0);
    pixelArmWord(c, 0xE6FF3072u); /* UXTH r3, r2. */
    if (needSpanEnd)
        pixelArmOp(c, PA_ADD, 2, 3, 4, 0);
    if (!isConstRgb)
    {
        pixelArmOp(c, PA_ADD, 8, 0, pixelArmImmediate(PA_REFERENCE + 4 * GR_GRADIENT_COLOR_R), 0);
        pixelArmWord(c, 0xE89800E0u); /* LDMIA r8, {r5, r6, r7} */
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
        pixelArmWord(c, 0xE88800E0u); /* STMIA r8, {r5, r6, r7} */
        if (needSrcAlpha)
            pixelArmGradientEx(c, GR_GRADIENT_COLOR_A, 12, 2, -16, 12);
    }
    if (depth)
    {
        pixelArmOp(c, PA_ADD, 1, 0, pixelArmImmediate(PA_COLOR_ROW), 0);
        pixelArmWord(c, 0xE8910300u); /* LDMIA r1, {r8, r9} */
        pixelArmOp(c, PA_ADD, 10, 0, pixelArmImmediate(PA_COLOR_STRIDE), 0);
        if (fastUntexConstFog)
        {
            pixelArmWord(c, 0xE89A0420u); /* LDMIA r10, {r5, r10} */
            pixelArmOp(c, PA_ADD, 5, 8, 5, 0);
            pixelArmOp(c, PA_ADD, 10, 9, 10, 0);
            pixelArmWord(c, 0xE8810420u); /* STMIA r1, {r5, r10} */
        }
        else
        {
            pixelArmWord(c, 0xE89A4400u); /* LDMIA r10, {r10, r14} */
            pixelArmOp(c, PA_ADD, 10, 8, 10, 0);
            pixelArmOp(c, PA_ADD, 14, 9, 14, 0);
            pixelArmWord(c, 0xE8814400u); /* STMIA r1, {r10, r14} */
        }
        pixelArmOp(c, PA_ADD, 8, 8, pixelArmShift(3, PA_LSL, colorShift), 0);
        pixelArmOp(c, PA_ADD, 9, 9, pixelArmShift(3, PA_LSL, 1), 0);
        if (fastUntexConstFog)
            pixelArmGradientKeepR14(c, GR_GRADIENT_DEPTH, 7, 3, !dither ? 10 : 12, 6);
        else
            pixelArmGradientEx(c, GR_GRADIENT_DEPTH, 7, 3, 0, !dither ? 10 : 12);
    }
    else
    {
        pixelArmRowPointer(c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
    }
    if (fastUntexConstFog)
    {
        pixelArmGradientKeepR14(c, GR_GRADIENT_FOG, 6, incFog ? 3 : 2, 5, 3);
    }
    else if (fog)
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
        pixelArmClampDepth16(c, 3, 2, sig->depthClampMin, sig->depthClampMax);
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
            pixelArmClampDepth16(c, 1, 3, sig->depthClampMin, sig->depthClampMax);
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
        if (fastUntexConstFog)
        {
            if (depth && dither) pixelArmOp(c, PA_ADD, 7, 7, 14, 0);
            if (incFog)
            {
                pixelArmUSAT8(c, 1, 6, 16);
                pixelArmOp(c, PA_ADD, 6, 6, 5, 0);
            }
            else
            {
                pixelArmMLA(c, 1, 5, 4, 6);
                pixelArmUSAT8(c, 1, 1, 16);
            }
            pixelArmOp(c, PA_RSB, 3, 1, pixelArmImmediate(0x100u), 0);
            pixelArmPKHBT(c, 2, 3, 1, 16);
            pixelArmSMUAD(c, 2, 14, 2);
            pixelArmMul(c, 3, 12, 3);
            pixelArmMLA(c, 3, 11, 1, 3);
            pixelArmUXTB16(c, 3, 3, 8);
            pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFF00u), 0);
            pixelArmOp(c, PA_ORR, 3, 3, 2, 0);
        }
        else if (blendAlphaInR3)
        {
            pixelArmLoad(c, 3, 0, -4);
            pixelArmUXTB16(c, 1, 3, swapRB ? 0 : 16);
            pixelArmUXTB(c, 2, 3, 8);
            pixelArmOp(c, PA_MOV, 3, 0, pixelArmShift(3, PA_LSR, 24), 0);
        }
        else if (blendAlphaInR14)
        {
            pixelArmLoad(c, 14, 0, -4);
            pixelArmUXTB(c, 3, 14, 16);
            pixelArmUXTB(c, 2, 14, 8);
            pixelArmOp(c, PA_AND, 1, 14, pixelArmImmediate(0xFFu), 0);
            pixelArmOp(c, PA_MOV, 14, 0, pixelArmShift(14, PA_LSR, 24), 0);
        }
        else
        {
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
                    else if (!isBlendGeneral && rgb565 && !dither && !dstAlpha)
                    {
                        pixelArmOp(c, PA_AND, 3, 1, pixelArmImmediate(0xF800u), 0);
                        pixelArmOp(c, PA_AND, 2, 2, pixelArmImmediate(0xFC00u), 0);
                        pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(2, PA_LSR, 5), 0);
                        pixelArmOp(c, PA_ORR, 3, 3, pixelArmShift(1, PA_LSR, 27), 0);
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
    }

    if (isBlendSWAR32)
    {
        pixelArmEmitBlend32SWAR(c, blendMode, dstAlpha, blendAlphaInR3, 0);
    }
    else if (isBlendGeneral)
    {
        pixelArmEmitBlendGeneral(c, dst, rgb565, dstAlpha, dither, blendMode, 0, blendAlphaInR14);
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
        pixelArmClampDepth16(c, 3, 2, sig->depthClampMin, sig->depthClampMax);
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
    int emitConst255Variant, emitPreloadConstFog, emitConstVar;
    int blendMode;
    int i;
    unsigned int mask;

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
    if (depth && (dst->depthBytes != 2 || sig->constantDepth ||
        (!allowExtended && (sig->depthClampMin || sig->depthClampMax)) ||
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

            pixelArmEmitUntextured(&c, sig, rgb565, dstAlpha, alphaTest, alphaRejectCond,
                                   fog, depth, rejectCondition, dither, colorShift,
                                   blendMode, 1, PA_C_ROW, 0);
        }
        pixelArmEmitUntextured(&c, sig, rgb565, dstAlpha, alphaTest, alphaRejectCond,
                               fog, depth, rejectCondition, dither, colorShift,
                               blendMode, 0, PA_G_ROW, 1);
        if (c.count > c.capacity || c.numBranches > 384) return 0;
        for (i = 0; i < c.numBranches; ++i)
            c.words[c.branches[i]] |= (unsigned int)(c.labels[c.targets[i]] - c.branches[i] - 2) & 0xFFFFFFu;
        *gradientMask = mask;
        return c.count * 4;
    }

    if (t[PA_TEX_MAG_LINEAR] != t[PA_TEX_MIN_LINEAR] ||
        (!allowExtended && (t[PA_TEX_MAG_LINEAR] || t[PA_TEX_MIN_LINEAR])) ||
        ((t[PA_TEX_WRAP_S] != GR_WRAPMODE_REPEAT && t[PA_TEX_WRAP_S] != GR_WRAPMODE_CLAMP_TO_EDGE) ||
         (t[PA_TEX_WRAP_T] != GR_WRAPMODE_REPEAT && t[PA_TEX_WRAP_T] != GR_WRAPMODE_CLAMP_TO_EDGE)) ||
        ((t[PA_TEX_WRAP_S] != GR_WRAPMODE_REPEAT || t[PA_TEX_WRAP_T] != GR_WRAPMODE_REPEAT) &&
         (!allowExtended || textureLayout != 0)) ||
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
    emitConst255Variant = emitConstVariant && !isBilinear && modulate && alphaTest &&
        alphaMode == 3 && capacity >= 4096;
    emitWhiteVariant = modulate && capacity >= (isBilinear ? 3600 : 2400);
    emitPreloadConstFog = emitConstVariant && modulate && (textureLayout != 0 || isBilinear) &&
        fog && !sig->monochromeFog && !rgb565 && dst->rgbaOffsets[1] == 8 &&
        ((dst->rgbaOffsets[0] == 16 && dst->rgbaOffsets[2] == 0) ||
         (dst->rgbaOffsets[0] == 0 && dst->rgbaOffsets[2] == 16));
    emitConstVar = emitConstVariant && !isBilinear && modulate && !needColA &&
        !fog && !alphaTest && !dstAlpha && blendMode == PA_BLEND_NONE && capacity >= 3200;

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
        if (emitConstVar)
        {
            for (i = 0; i < 3; ++i)
            {
                pixelArmLoad(&c, i == 0 ? 1 : 2, 0, PA_X_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                if (i != 0) pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            }
            pixelArmOp(&c, PA_CMP, 0, 1, pixelArmImmediate(0), 1);
            pixelArmBranch(&c, PA_NE, PA_G_ROW);

            for (i = 0; i < 3; ++i)
            {
                pixelArmLoad(&c, i == 0 ? 1 : 2, 0, PA_Y_ADDER + 4 * (GR_GRADIENT_COLOR_R + i));
                if (i != 0) pixelArmOp(&c, PA_ORR, 1, 1, 2, 0);
            }
            pixelArmOp(&c, PA_CMP, 0, 1, pixelArmImmediate(0), 1);
            pixelArmWord(&c, 0x13A02001u); /* MOVNE r2, #1 */
            pixelArmWord(&c, 0x03A02000u); /* MOVEQ r2, #0 */
            pixelArmStore(&c, 2, 0, -16);
            pixelArmBranch(&c, PA_NE, PA_C_ROW + PA_OFF_CV);
        }
        else
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
        }

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
        pixelArmBranch(&c, PA_NE, PA_G_ROW);

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
                pixelArmBranch(&c, PA_EQ, PA_W_ROW);
            }
        }
        else
        {
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSR, 16), 0);
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 24), 0);
        }
        pixelArmStore(&c, 1, 0, -4);
        if (emitPreloadConstFog)
        {
            pixelArmLoad(&c, 2, 0, PA_FOG_COLOR);
            pixelArmUXTB16(&c, 2, 2, dst->rgbaOffsets[0] == 16 ? 16 : 0);
            pixelArmStore(&c, 2, 0, -8);
        }
        if (emitConst255Variant)
        {
            pixelArmOp(&c, PA_CMP, 0, 1, pixelArmImmediate(0xFF000000u), 1);
            pixelArmBranch(&c, PA_HS, PA_C255_ROW);
        }
    }

    if (emitConstVariant)
        pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                            alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                            dither, colorShift, blendMode, 1, PA_C_ROW, 0);
    if (emitConst255Variant)
        pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, 1,
                            alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                            dither, colorShift, blendMode, 1, PA_C255_ROW, 0);
    pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                        alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                        dither, colorShift, blendMode, 0, PA_G_ROW, !emitWhiteVariant);
    if (emitWhiteVariant)
        pixelArmEmitVariant(&c, sig, textureLayout, rgb565, dstAlpha, isBilinear, modulate, alphaMode,
                            alphaTest, alphaRejectCond, fog, depth, rejectCondition,
                            dither, colorShift, blendMode, 2, PA_W_ROW, 1);

    if (c.count > c.capacity || c.numBranches > 384) return 0;
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
