#include "gerbera/grARMPixel.h"
#include <string.h>

#if GERBERA_ARMV6_JIT

enum
{
    PA_AND = 0, PA_SUB = 2, PA_RSB = 3, PA_ADD = 4, PA_CMP = 10,
    PA_ORR = 12, PA_MOV = 13, PA_BIC = 14,
    PA_LSL = 0, PA_LSR = 1, PA_ASR = 2, PA_ROR = 3,
    PA_EQ = 0, PA_NE = 1, PA_HS = 2, PA_HI = 8, PA_GE = 10, PA_AL = 14,
    PA_TEXTURE = 4, PA_PALETTE = 8, PA_S_SHIFT = 24, PA_T_SHIFT = 28,
    PA_WIDTH_LOG = 32, PA_DITHER = 128, PA_COLOR_STRIDE = 132, PA_DEPTH_STRIDE = 136,
    PA_COLOR_ROW = 168, PA_DEPTH_ROW = 172, PA_ROWS = 180, PA_SPANS = 184, PA_Y = 188,
    PA_REFERENCE = 192, PA_X_ADDER = 232, PA_Y_ADDER = 272,
    PA_LAYOUT_DIM_COUNT = GR_MAX_TEXTURE_DIMENSION_LOG2 + 1,
    PA_LAYOUT_COUNT = PA_LAYOUT_DIM_COUNT * PA_LAYOUT_DIM_COUNT
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
    PA_ROW, PA_PIXEL, PA_SHADE, PA_ROW_END, PA_RETURN, PA_REJECT, PA_REJECT_LOOP,
    PA_LABEL_COUNT
};

typedef struct ARMPixelCode_s
{
    unsigned int *words;
    int count, capacity;
    int labels[PA_LABEL_COUNT];
    int branches[16], targets[16], numBranches;
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

static void pixelArmMLA(ARMPixelCode_s *c, int dst, int a, int b, int add)
{
    pixelArmWord(c, 0xE0200090u | ((unsigned int)dst << 16) | ((unsigned int)add << 12) |
                  ((unsigned int)b << 8) | (unsigned int)a);
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

static void pixelArmGradient(ARMPixelCode_s *c, int gradient, int dst, int origin,
                              int scratchOffset)
{
    pixelArmLoad(c, 1, 0, PA_REFERENCE + 4 * gradient);
    pixelArmLoad(c, 12, 0, PA_X_ADDER + 4 * gradient);
    pixelArmMLA(c, dst, 12, origin, 1);
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

static void pixelArmTextureIndex(ARMPixelCode_s *c, unsigned int layout)
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

static void pixelArmComponent(ARMPixelCode_s *c, const _grSurfaceDescriptor *texture,
                              const _grSurfaceDescriptor *dest, int channel,
                              int modulate, int dither, int depthStep)
{
    int bits = dest->rgbaSizes[channel], shift = dest->rgbaOffsets[channel];
    int packed = modulate ? 12 : 3, output = modulate ? 14 : 2;
    if (modulate)
    {
        pixelArmLoad(c, 2, 0, PA_X_ADDER + 4 * (GR_GRADIENT_COLOR_R + channel));
        pixelArmLoad(c, 3, 0, -4 * (channel + 1));
    }
    pixelArmWord(c, 0xE6EF1070u | ((unsigned int)(texture->rgbaOffsets[channel] / 8) << 10) |
                  (unsigned int)packed);
    if (depthStep) pixelArmOp(c, PA_ADD, 7, 7, (unsigned int)depthStep, 0);
    if (modulate)
    {
        pixelArmMLA(c, 2, 2, 4, 3);
        pixelArmOp(c, PA_MOV, 2, 0, pixelArmShift(2, PA_LSR, 16), 0);
        pixelArmMLA(c, 1, 1, 2, 2);
        pixelArmWord(c, 0xE6E81451u); /* USAT r1, #8, r1, ASR #8. */
    }
    if (dither)
    {
        pixelArmOp(c, PA_SUB, 1, 1, pixelArmShift(1, PA_LSR, bits), 0);
        pixelArmOp(c, PA_ADD, 1, 1, pixelArmShift(10, PA_LSR, 24 + bits), 0);
    }
    if (bits < 8 && shift != 0)
        pixelArmOp(c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSR, 8 - bits), 0);
    pixelArmOp(c, channel ? PA_ORR : PA_MOV, output, channel ? output : 0,
                pixelArmShift(1, shift || bits == 8 ? PA_LSL : PA_LSR,
                              shift ? shift : 8 - bits), 0);
}

int grCompileARMPixelPipeline(void *output, int capacity, const _grSignature *sig,
                              unsigned int textureLayout, unsigned int *gradientMask)
{
    const _grSurfaceDescriptor *dst = &sig->descriptor;
    const _grSurfaceDescriptor *tex = (const _grSurfaceDescriptor *)sig->texture[0];
    const unsigned char *t = sig->texture[0];
    ARMPixelCode_s c;
    int rgb565, alpha, modulate, depth, rejectCondition, dither, colorShift;
    int channel, i, outputRegister;
    unsigned int mask;

    rgb565 = dst->colorBytes == 2 && dst->rgbaSizes[0] == 5 && dst->rgbaSizes[1] == 6 &&
        dst->rgbaSizes[2] == 5 && dst->rgbaSizes[3] == 0 && dst->rgbaOffsets[0] == 11 &&
        dst->rgbaOffsets[1] == 5 && dst->rgbaOffsets[2] == 0;
    alpha = dst->rgbaSizes[3] != 0;
    if (textureLayout > PA_LAYOUT_COUNT || !sig->isValid || !sig->writeColor ||
        sig->constantWriteColor || sig->screenRectGradients ||
        sig->blendEnabled || sig->logicOpEnabled || sig->applyColorWriteMask ||
        sig->alphaTest || sig->stencilTest || sig->fogEnabled ||
        (sig->colorMask & dst->colorMask) != dst->colorMask ||
        (!rgb565 && !pixelArmByteChannels(dst, alpha)) ||
        !t[PA_TEX_ENABLED] || sig->texture[1][PA_TEX_ENABLED] || t[PA_TEX_MAG_LINEAR] || t[PA_TEX_MIN_LINEAR] ||
        t[PA_TEX_WRAP_S] != GR_WRAPMODE_REPEAT || t[PA_TEX_WRAP_T] != GR_WRAPMODE_REPEAT ||
        !pixelArmByteChannels(tex, alpha) ||
        (tex->paletteFormat != GR_PALETTEFORMAT_NONE && tex->paletteFormat != GR_PALETTEFORMAT_8BIT) ||
        t[PA_TEX_RGB_OPERAND0] != GR_TEXENVCOMBOPERAND_SRC_COLOR || t[PA_TEX_RGB_SCALE] || t[PA_TEX_ALPHA_SCALE])
        return 0;
    modulate = t[PA_TEX_RGB_FUNCTION] == GR_TEXENVCOMBFUNC_MODULATE &&
        t[PA_TEX_RGB_SOURCE0] == GR_TEXENVCOMBSRC_PREVIOUS && t[PA_TEX_RGB_SOURCE1] == GR_TEXENVCOMBSRC_TEXTURE &&
        t[PA_TEX_RGB_OPERAND1] == GR_TEXENVCOMBOPERAND_SRC_COLOR && !sig->flatShading;
    if (!modulate && (t[PA_TEX_RGB_FUNCTION] != GR_TEXENVCOMBFUNC_REPLACE || t[PA_TEX_RGB_SOURCE0] != GR_TEXENVCOMBSRC_TEXTURE))
        return 0;
    if (alpha && (t[PA_TEX_ALPHA_OPERAND0] != GR_TEXENVCOMBOPERAND_SRC_ALPHA ||
        t[PA_TEX_ALPHA_FUNCTION] != (modulate ? GR_TEXENVCOMBFUNC_MODULATE : GR_TEXENVCOMBFUNC_REPLACE) ||
        t[PA_TEX_ALPHA_SOURCE0] != (modulate ? GR_TEXENVCOMBSRC_PREVIOUS : GR_TEXENVCOMBSRC_TEXTURE) ||
        (modulate && (t[PA_TEX_ALPHA_SOURCE1] != GR_TEXENVCOMBSRC_TEXTURE || t[PA_TEX_ALPHA_OPERAND1] != GR_TEXENVCOMBOPERAND_SRC_ALPHA))))
        return 0;
    depth = sig->depthTest != 0;
    if (depth && (dst->depthBytes != 2 || sig->constantDepth || sig->depthClampMin || sig->depthClampMax ||
        (sig->depthFunc != GR_FUNC_LESS && sig->depthFunc != GR_FUNC_LEQUAL))) return 0;
    dither = rgb565 && sig->dither;
    colorShift = rgb565 ? 1 : 2;
    rejectCondition = sig->depthFunc == GR_FUNC_LESS ? PA_HS : PA_HI;
    mask = (1u << GR_GRADIENT_TEX0_S) | (1u << GR_GRADIENT_TEX0_T);
    if (depth) mask |= 1u << GR_GRADIENT_DEPTH;
    if (modulate) mask |= (alpha ? 15u : 7u) << GR_GRADIENT_COLOR_R;
    memset(&c, 0, sizeof(c));
    c.words = (unsigned int *)output;
    c.capacity = capacity / 4;
    pixelArmWord(&c, 0xE92D4FF0u); /* Preserve r4-r11 and the return address. */
    pixelArmOp(&c, PA_ADD, 0, 0, pixelArmImmediate(64), 0);
    pixelArmLoad(&c, 2, 0, PA_ROWS);
    pixelArmOp(&c, PA_CMP, 0, 2, pixelArmImmediate(0), 1);
    pixelArmBranch(&c, PA_EQ, PA_RETURN);
    pixelArmLoad(&c, 11, 0, PA_TEXTURE);

    /* r4 counts toward zero; r5/r6/r7 hold S/T/Z, r8/r9 the next output
     * addresses, r10 the dither phase or Z step, and r11 the texture base. */
    c.labels[PA_ROW] = c.count;
    pixelArmLoad(&c, 1, 0, PA_SPANS);
    pixelArmWord(&c, 0xE4912004u); /* LDR r2, [r1], #4. */
    pixelArmStore(&c, 1, 0, PA_SPANS);
    pixelArmOp(&c, PA_MOV, 4, 0, pixelArmShift(2, PA_LSR, 16), 0);
    pixelArmWord(&c, 0xE6FF3072u); /* UXTH r3, r2. */
    pixelArmOp(&c, PA_ADD, 2, 3, 4, 0);
    pixelArmRowPointer(&c, 8, PA_COLOR_ROW, PA_COLOR_STRIDE, colorShift);
    if (depth) pixelArmRowPointer(&c, 9, PA_DEPTH_ROW, PA_DEPTH_STRIDE, 1);
    if (depth) pixelArmGradient(&c, GR_GRADIENT_DEPTH, 7, 3, 0);
    pixelArmGradient(&c, GR_GRADIENT_TEX0_S, 5, 3, 0);
    pixelArmGradient(&c, GR_GRADIENT_TEX0_T, 6, 3, 0);
    if (modulate)
        for (channel = 0; channel < (alpha ? 4 : 3); ++channel)
            pixelArmGradient(&c, GR_GRADIENT_COLOR_R + channel, 12, 2, -4 * (channel + 1));
    if (dither)
    {
        pixelArmLoad(&c, 1, 0, PA_Y);
        pixelArmOp(&c, PA_AND, 12, 1, pixelArmImmediate(3), 0);
        pixelArmOp(&c, PA_ADD, 1, 1, pixelArmImmediate(1), 0);
        pixelArmStore(&c, 1, 0, PA_Y);
        pixelArmLoad(&c, 1, 0, PA_DITHER);
        pixelArmWord(&c, 0xE791A10Cu); /* LDR r10, [r1, r12, LSL #2]. */
        pixelArmOp(&c, PA_AND, 1, 3, pixelArmImmediate(3), 0);
        pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(1, PA_LSL, 3), 0);
        pixelArmOp(&c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
    }
    if (!modulate)
    {
        pixelArmLoad(&c, 12, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmLoad(&c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
    }
    if (depth && !dither) pixelArmLoad(&c, 10, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
    pixelArmOp(&c, PA_RSB, 4, 4, pixelArmImmediate(0), 1);
    pixelArmBranch(&c, PA_GE, PA_ROW_END);
    c.labels[PA_PIXEL] = c.count;
    if (depth)
    {
        pixelArmWord(&c, 0xE0D910B2u); /* LDRH r1, [r9], #2. */
        pixelArmOp(&c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(&c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(&c, rejectCondition, PA_REJECT);
    }
    c.labels[PA_SHADE] = c.count;
    if (depth && sig->depthMask) pixelArmWord(&c, 0xE14930B2u); /* STRH r3, [r9, #-2]. */
    pixelArmTextureIndex(&c, textureLayout);
    if (tex->paletteFormat == GR_PALETTEFORMAT_8BIT)
    {
        pixelArmWord(&c, 0xE7DB2002u); /* LDRB r2, [r11, r2]. */
        pixelArmLoad(&c, 1, 0, PA_PALETTE);
        pixelArmWord(&c, 0xE7910102u | ((unsigned int)(modulate ? 12 : 3) << 12));
    }
    else pixelArmWord(&c, 0xE79B0102u | ((unsigned int)(modulate ? 12 : 3) << 12));
    if (depth && dither) pixelArmLoad(&c, modulate ? 14 : 2, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
    outputRegister = modulate ? 14 : 2;
    if (!modulate && !rgb565 &&
        memcmp(tex->rgbaOffsets, dst->rgbaOffsets, alpha ? 4u : 3u) == 0)
    {
        unsigned int unused = 0xFFFFFFFFu;
        for (channel = 0; channel < (alpha ? 4 : 3); ++channel)
            unused &= ~(255u << dst->rgbaOffsets[channel]);
        if (depth) pixelArmOp(&c, PA_ADD, 7, 7, dither ? 2u : 10u, 0);
        if (unused && !(t[PA_TEX_RAW_COPY] && !sig->perPixelPack))
            pixelArmOp(&c, PA_BIC, 2, 3, pixelArmImmediate(unused), 0);
        else
            outputRegister = 3;
    }
    else
        for (channel = 0; channel < (alpha ? 4 : 3); ++channel)
            pixelArmComponent(&c, tex, dst, channel, modulate, dither,
                              depth && channel == 0 ? (dither ? (modulate ? 14 : 2) : 10) : 0);
    if (rgb565) pixelArmWord(&c, 0xE0C800B2u | ((unsigned int)outputRegister << 12));
    else pixelArmWord(&c, 0xE4880004u | ((unsigned int)outputRegister << 12));
    if (modulate)
    {
        pixelArmLoad(&c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmOp(&c, PA_ADD, 5, 5, 1, 0);
        pixelArmLoad(&c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        pixelArmOp(&c, PA_ADD, 6, 6, 1, 0);
    }
    else
    {
        pixelArmOp(&c, PA_ADD, 5, 5, 12, 0);
        pixelArmOp(&c, PA_ADD, 6, 6, 14, 0);
    }
    if (dither) pixelArmOp(&c, PA_MOV, 10, 0, pixelArmShift(10, PA_ROR, 8), 0);
    pixelArmOp(&c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
    pixelArmBranch(&c, PA_NE, PA_PIXEL);
    c.labels[PA_ROW_END] = c.count;
    pixelArmLoad(&c, 1, 0, PA_ROWS);
    pixelArmOp(&c, PA_SUB, 1, 1, pixelArmImmediate(1), 1);
    pixelArmStore(&c, 1, 0, PA_ROWS);
    pixelArmBranch(&c, PA_NE, PA_ROW);
    c.labels[PA_RETURN] = c.count;
    pixelArmWord(&c, 0xE8BD8FF0u);
    if (depth)
    {
        c.labels[PA_REJECT] = c.count;
        pixelArmOp(&c, PA_MOV, 12, 0, 4, 0);
        if (dither) pixelArmLoad(&c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_DEPTH);
        c.labels[PA_REJECT_LOOP] = c.count;
        pixelArmOp(&c, PA_ADD, 7, 7, dither ? 14u : 10u, 0);
        pixelArmOp(&c, PA_ADD, 8, 8, pixelArmImmediate(1u << colorShift), 0);
        pixelArmOp(&c, PA_ADD, 4, 4, pixelArmImmediate(1), 1);
        pixelArmBranch(&c, PA_EQ, PA_ROW_END);
        pixelArmWord(&c, 0xE0D910B2u);
        pixelArmOp(&c, PA_MOV, 3, 0, pixelArmShift(7, PA_LSR, 14), 0);
        pixelArmOp(&c, PA_CMP, 0, 3, 1, 1);
        pixelArmBranch(&c, rejectCondition, PA_REJECT_LOOP);
        pixelArmOp(&c, PA_SUB, 12, 4, 12, 0);
        pixelArmLoad(&c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
        pixelArmMLA(&c, 5, 1, 12, 5);
        pixelArmLoad(&c, 1, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        pixelArmMLA(&c, 6, 1, 12, 6);
        if (dither)
        {
            pixelArmOp(&c, PA_MOV, 1, 0, pixelArmShift(12, PA_LSL, 3), 0);
            pixelArmOp(&c, PA_MOV, 10, 0, 10u | 0x70u | (1u << 8), 0);
        }
        if (!modulate)
        {
            pixelArmLoad(&c, 12, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_S);
            pixelArmLoad(&c, 14, 0, PA_X_ADDER + 4 * GR_GRADIENT_TEX0_T);
        }
        pixelArmBranch(&c, PA_AL, PA_SHADE);
    }
    if (c.count > c.capacity) return 0;
    for (i = 0; i < c.numBranches; ++i)
        c.words[c.branches[i]] |= (unsigned int)(c.labels[c.targets[i]] - c.branches[i] - 2) & 0xFFFFFFu;
    *gradientMask = mask;
    return c.count * 4;
}

#else

int grCompileARMPixelPipeline(void *output, int capacity, const _grSignature *signature,
                              unsigned int textureLayout, unsigned int *gradientMask)
{
    (void)output; (void)capacity; (void)signature; (void)textureLayout; (void)gradientMask;
    return 0;
}

#endif
