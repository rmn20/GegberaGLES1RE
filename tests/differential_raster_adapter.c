/* Fixed-width interfaces for comparing host structs with the 32-bit DLL. */
#include "gerbera/grState.h"
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include "gerbera/hc/hcCompiler.h"
#include "gerbera/hc/hcBackend.h"

extern const unsigned char g_mutableParamsMask[12];
extern const int g_orderedDitherKernel[4];

typedef struct SignatureInput
{
    uint32_t colorMask, useDithering, logicOp, srcBlendFactor, dstBlendFactor;
    uint32_t depthMask, depthFunc, depthOffsetFactor, depthOffsetUnits, viewportScaleZ;
    uint32_t fogColor, alphaFunc, stencilFunc, stencilFail, stencilZFail, stencilZPass;
    uint32_t stencilTestMask, stencilWriteMask, screenRectGradients;
    uint32_t bufferMask, capabilities, textureMask, packedFogColor;
} SignatureInput;

static int raster_compile_wrap(void *storage, int (*function)(void *), void *argument)
{
    if (setjmp(*(jmp_buf *)storage) != 0)
        return -1;
    return function(argument);
}

static void raster_compile_jump(void *storage, int value)
{
    longjmp(*(jmp_buf *)storage, value);
}

static int raster_compile(void *instructions, unsigned int optimizationMask,
                          void *bytes, int capacity, unsigned char *parameterMask,
                          HCBackend_s *backend)
{
    static union { uint64_t alignment; unsigned char bytes[1024 * 1024]; } arena;
    HCSetjmpConfig_s config;
    void *output = NULL;
    int length;
    config.setjmpWrap = raster_compile_wrap;
    config.longjmp = raster_compile_jump;
    config.jmpbufSize = (unsigned int)((sizeof(jmp_buf) + 15u) & ~15u);
    length = HC_compile(arena.bytes, sizeof(arena.bytes), &config, &output,
                        (HCInstr_s *)instructions, backend, optimizationMask,
                        parameterMask, (unsigned char *)g_mutableParamsMask, 78);
    if (length >= 0 && length <= capacity)
        memcpy(bytes, output, (size_t)length);
    return length;
}

int differential_raster_compile(void *instructions, unsigned int optimizationMask,
                                void *bytes, int capacity, unsigned char *parameterMask)
{
    return raster_compile(instructions, optimizationMask, bytes, capacity, parameterMask,
                          HCARMBackend_create());
}

int differential_arm_codegen(HCInstr_s *instructions, void *bytes, int capacity)
{
    static union { uint64_t alignment; unsigned char bytes[256 * 1024]; } storage;
    HCArena_s arena;
    HCBackendInfo_s info;
    HCInstr_s *fixed;
    void *output;
    int length;
    HCArena_init(&arena, storage.bytes, sizeof(storage.bytes));
    HCARMBackend_create()->preRegallocFixer(&arena, &fixed, instructions, NULL);
    info = computeBackEndInfo(&arena, fixed, 16);
    length = HCARMBackend_create()->codegen(&arena, &output, fixed, &info, 0);
    if (length >= 0 && length <= capacity)
        memcpy(bytes, output, (size_t)length);
    return length;
}

int differential_raster_compile_x86(void *instructions, unsigned int optimizationMask,
                                    void *bytes, int capacity, unsigned char *parameterMask)
{
    return raster_compile(instructions, optimizationMask, bytes, capacity, parameterMask,
                          HCX86Backend_create());
}

typedef struct SpanInput
{
    uint32_t texels[2][4], envColor[2], fogColor, textureFormat[2], envMode[2];
} SpanInput;

static void raster_pixel_marker(void *argument)
{
    (void)argument;
}

void differential_raster_span_sample(const _grSignature *signature, const int *gradients,
                                     void *color, void *depth, void *stencil,
                                     const SpanInput *inputs, const int *sampling)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    _grPipelineCache cache;
    _grTexture texture[2];
    int unit, operand;
    static const uint32_t texels[4] = { 0xFF204060u, 0xFF80A0C0u, 0xFF10D070u, 0xFFF03090u };
    unsigned int spans[2] = { 4u << 16, 1u | (2u << 16) };
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0, sizeof(pipe));
    memset(&cache, 0, sizeof(cache));
    memset(&texture, 0, sizeof(texture));
    memcpy(&state.drawSurface, &signature->descriptor, sizeof(signature->descriptor));
    state.colorMask = signature->colorMask;
    state.depthMask = signature->depthMask;
    state.depthFunc = signature->depthFunc;
    state.alphaFunc = signature->alphaFunc;
    state.alphaReference = 64;
    state.srcBlendFactor = signature->srcFactor;
    state.dstBlendFactor = signature->dstFactor;
    state.stencilFunc = signature->stencilFunc;
    state.stencilFail = signature->stencilFail;
    state.stencilZFail = signature->stencilZFail;
    state.stencilZPass = signature->stencilZPass;
    state.stencilTestMask = 255;
    state.stencilWriteMask = 255;
    state.stencilReference = 3;
    state.capabilities[GR_CAPABILITY_DEPTH_TEST] = signature->depthTest;
    state.capabilities[GR_CAPABILITY_ALPHA_TEST] = signature->alphaTest;
    state.capabilities[GR_CAPABILITY_BLEND] = signature->blendEnabled;
    state.capabilities[GR_CAPABILITY_STENCIL_TEST] = signature->stencilTest;
    state.capabilities[GR_CAPABILITY_SMOOTH_SHADING] = 1;
    state.activeGradientMask = 63;
    state.capabilities[GR_CAPABILITY_FOG] = signature->fogEnabled;
    state.capabilities[GR_CAPABILITY_DITHER] = signature->dither;
    pipe.ditherKernel = (void *)g_orderedDitherKernel;
    state.fogColor = inputs ? inputs->fogColor : 0;
    for (unit = 0; unit < 2; ++unit)
    {
        const unsigned char *record = signature->texture[unit];
        _grTextureUnitState *env = &state.textureUnit[2 * unit];
        _grPixelPipeTextureUnit *sample = &pipe.textureUnit[unit];
        if (!record[16])
            continue;
        texture[unit].mipmap[0].texFormat = inputs ? inputs->textureFormat[unit] : GR_TEXTUREFORMAT_RGBA8;
        texture[unit].wrapMode[0] = record[21];
        texture[unit].wrapMode[1] = record[22];
        env->texture = &texture[unit];
        env->texEnvColor = inputs ? inputs->envColor[unit] : 0;
        env->texEnvMode = inputs ? inputs->envMode[unit] : GR_TEXENVMODE_REPLACE;
        env->texEnvRGBCombFunc = record[36];
        env->texEnvAlphaCombFunc = record[37];
        env->texEnvRGBCombScale = 1u << record[38];
        env->texEnvAlphaCombScale = 1u << record[39];
        for (operand = 0; operand < 3; ++operand)
        {
            env->texEnvRGBCombSrc[operand] = record[24 + 4 * operand];
            env->texEnvAlphaCombSrc[operand] = record[25 + 4 * operand];
            env->texEnvRGBCombOperand[operand] = record[26 + 4 * operand];
            env->texEnvAlphaCombOperand[operand] = record[27 + 4 * operand];
        }
        state.useTexUnit[unit] = 1;
        state.activeGradientMask |= 0xC0 << (2 * unit);
        sample->pixels = (void *)(inputs ? inputs->texels[unit] : texels);
        sample->sMask = sample->tMask = 1;
        sample->log2DimS = 1;
        sample->linear = record[19];
        sample->rShift[0] = record[21] == GR_WRAPMODE_REPEAT ? 31 : 20;
        sample->rShift[1] = record[22] == GR_WRAPMODE_REPEAT ? 31 : 20;
        if (sampling != NULL)
        {
            const int *parameters = sampling + 6 * unit;
            sample->sMask = parameters[0];
            sample->tMask = parameters[1];
            sample->rShift[0] = parameters[2];
            sample->rShift[1] = parameters[3];
            sample->log2DimS = parameters[4];
            sample->linear = parameters[5];
        }
    }
    state.drawSurface.buffers[0].data = color;
    state.drawSurface.buffers[1].data = depth;
    state.drawSurface.buffers[2].data = stencil;
    state.drawSurface.buffers[0].stride = 16;
    state.drawSurface.buffers[1].stride = 8;
    state.drawSurface.buffers[2].stride = 4;
    pipe.colorRow = color;
    pipe.depthRow = depth;
    pipe.stencilRow = stencil;
    pipe.scanlineInfo = spans;
    pipe.numScanlines = 2;
    memcpy(pipe.reference, gradients, 40);
    memcpy(pipe.xAdder, gradients + 10, 40);
    memcpy(pipe.yAdder, gradients + 20, 40);
    state.pixelPipeline = &pipe;
    state.pixelPipelineCode = raster_pixel_marker;
    cache.arena = (unsigned char *)(uintptr_t)raster_pixel_marker;
    cache.arenaSize = 1;
    state.pipelineCache = &cache;
    grExecutePixelPipeline(&state);
}

void differential_raster_span_inputs(const _grSignature *signature, const int *gradients,
                                     void *color, void *depth, void *stencil,
                                     const SpanInput *inputs)
{
    differential_raster_span_sample(signature, gradients, color, depth, stencil, inputs, NULL);
}

void differential_raster_span(const _grSignature *signature, const int *gradients,
                              void *color, void *depth, void *stencil)
{
    differential_raster_span_inputs(signature, gradients, color, depth, stencil, NULL);
}

void differential_raster_signature(const SignatureInput *in,
                                   const unsigned char *descriptor,
                                   const unsigned char *units, unsigned char *out)
{
    Gerbera_s state;
    _grTexture textures[2];
    _grSignature signature;
    _grCacheSignature compressed;
    unsigned char dummy = 0;
    int i;
    memset(&state, 0, sizeof(state));
    memset(textures, 0, sizeof(textures));
#define COPY_INPUT(field) state.field = in->field
    COPY_INPUT(colorMask); COPY_INPUT(useDithering); COPY_INPUT(logicOp);
    COPY_INPUT(srcBlendFactor); COPY_INPUT(dstBlendFactor); COPY_INPUT(depthMask);
    COPY_INPUT(depthFunc); COPY_INPUT(depthOffsetFactor); COPY_INPUT(depthOffsetUnits);
    COPY_INPUT(fogColor); COPY_INPUT(alphaFunc); COPY_INPUT(stencilFunc);
    COPY_INPUT(stencilFail); COPY_INPUT(stencilZFail); COPY_INPUT(stencilZPass);
    COPY_INPUT(stencilTestMask); COPY_INPUT(stencilWriteMask);
    COPY_INPUT(screenRectGradients); COPY_INPUT(packedFogColor);
#undef COPY_INPUT
    state.viewportScale.normal[2] = (int32_t)in->viewportScaleZ;
    memcpy(&state.drawSurface, descriptor, sizeof(_grSurfaceDescriptor));
    for (i = 0; i < 16; ++i)
        state.capabilities[i] = (unsigned char)((in->capabilities >> i) & 1);
    for (i = 0; i < 3; ++i)
        state.drawSurface.buffers[i].data = (in->bufferMask & (1u << i)) ? &dummy : NULL;
    for (i = 0; i < 2; ++i)
    {
        const unsigned char *unit = units + 23 * i;
        _grTextureUnitState *destination = &state.textureUnit[2 * i];
        state.useTexUnit[i] = (unsigned char)((in->textureMask >> i) & 1);
        destination->texture = &textures[i];
        textures[i].mipmap[0].texFormat = unit[0];
        textures[i].parmMask = unit[1];
        textures[i].filterMask[GR_FILTERTYPE_MIN] = unit[2];
        textures[i].filterMask[GR_FILTERTYPE_MAG] = unit[3];
        textures[i].wrapMode[0] = unit[4];
        textures[i].wrapMode[1] = unit[5];
        memcpy(&destination->texEnvMode, unit + 6, 17);
    }
    grEvaluatePixelPipeline(&state, &signature);
    grCompressSignature(&compressed, &signature);
    memcpy(out, &signature, sizeof(signature));
    memcpy(out + sizeof(signature), &compressed, sizeof(compressed));
    memcpy(out + sizeof(signature) + sizeof(compressed), &state.packedFogColor, 4);
}

void differential_raster_gradient(const int32_t *in, uint32_t *out)
{
    _grPixelPipe pipe;
    _grGradientInfo info;
    memset(&pipe, 0x5A, sizeof(pipe));
    info.pPP = &pipe;
    info.v0sx = in[1];
    info.v0sy = in[2];
    info.v01sxPerArea = in[3];
    info.v01syPerArea = in[4];
    info.v02sxPerArea = in[5];
    info.v02syPerArea = in[6];
    info.shift = in[7];
    if (in[0])
        grComputeDeltas(&info, in[8], in[9], in[10]);
    else
        grSetupGradient(&info, in[8], in[9], in[10], in[11]);
    memcpy(out, pipe.reference, 40);
    memcpy(out + 10, pipe.xAdder, 40);
    memcpy(out + 20, pipe.yAdder, 40);
}

void differential_raster_depth_offset(const int32_t *in, uint32_t *out)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0, sizeof(pipe));
    state.pixelPipeline = &pipe;
    state.depthOffsetUnits = in[0];
    state.depthOffsetFactor = in[1];
    state.drawSurface.depthBytes = (unsigned char)in[2];
    pipe.reference[0] = in[3];
    pipe.xAdder[0] = in[4];
    pipe.yAdder[0] = in[5];
    grComputeDepthOffset(&state);
    out[0] = (uint32_t)pipe.reference[0];
}

void differential_raster_triangle_gradients(const uint32_t *in,
                                            const void *records, uint32_t *out)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    _grClipTriangle triangle;
    _grTexture textures[2];
    unsigned char pixel = 0;
    union { uint64_t alignment; unsigned char bytes[GR_RENDER_STACK_SIZE]; } stack;
    _grVertex *vertices = (_grVertex *)(stack.bytes + 1332);
    int unit;
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0xA5, sizeof(pipe));
    memset(&stack, 0, sizeof(stack));
    memset(textures, 0, sizeof(textures));
    memcpy(vertices, records, 3 * sizeof(_grVertex));
    state.renderStack = stack.bytes;
    state.pixelPipeline = &pipe;
    state.capabilities[GR_CAPABILITY_SMOOTH_SHADING] = (unsigned char)in[1];
    state.activeGradientMask = (int)in[0];
    for (unit = 0; unit < 2; ++unit)
    {
        textures[unit].mipmap[0].pixels = &pixel;
        textures[unit].mipmap[0].log2Dim[0] = 3;
        textures[unit].mipmap[0].log2Dim[1] = 2;
        textures[unit].numMipLevels = 1;
        state.textureUnit[2 * unit].texture = &textures[unit];
    }
    triangle.indices = in[4];
    triangle.area = grRoundTriangleArea(grComputeTriangleArea(vertices, triangle.indices));
    triangle.side = (int)in[3];
    grSetupTriangleGradients(&state, &triangle, (int)in[2], in[0]);
    out[0] = (uint32_t)triangle.area;
    memcpy(out + 1, pipe.reference, 40);
    memcpy(out + 11, pipe.xAdder, 40);
    memcpy(out + 21, pipe.yAdder, 40);
}

void differential_raster_scan(const int32_t *in, uint32_t *out)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    _grRasterPoint points[3];
    union { uint64_t alignment; unsigned char bytes[GR_RENDER_STACK_SIZE]; } stack;
    int count, i;
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0, sizeof(pipe));
    memset(&stack, 0, sizeof(stack));
    grSetRectangle(&state.renderScissor, in[0], in[1], in[2], in[3]);
    state.renderStack = stack.bytes;
    state.pixelPipeline = &pipe;
    for (i = 0; i < 3; ++i)
    {
        points[i].x = in[4 + 2 * i];
        points[i].y = in[5 + 2 * i];
    }
    count = grScanConvertTriangle(&state, &points[0], &points[1], &points[2], in[10]);
    out[0] = (uint32_t)count;
    out[1] = (uint32_t)pipe.yStart;
    out[2] = (uint32_t)pipe.numScanlines;
    if (count > 0)
        memcpy(out + 3, pipe.scanlineInfo, (size_t)count * sizeof(uint32_t));
}

void differential_raster_texture(const int32_t *in, uint32_t *out)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    _grTexture texture;
    uint32_t pixels[GR_MAX_TEXTURE_MIPMAPS];
    uint32_t palette;
    int coordinates[2][2] = { { in[9], in[10] }, { in[11], in[12] } };
    int unit = in[0];
    int gradient = 6 + 2 * unit;
    int i;
    _grPixelPipeTextureUnit *result;

    memset(&state, 0, sizeof(state));
    memset(&texture, 0, sizeof(texture));
    memset(&pipe, 0x5A, sizeof(pipe));
    for (i = 0; i < GR_MAX_TEXTURE_MIPMAPS; ++i)
    {
        texture.mipmap[i].pixels = &pixels[i];
        texture.mipmap[i].log2Dim[0] = (short)(in[1] > i ? in[1] - i : 0);
        texture.mipmap[i].log2Dim[1] = (short)(in[2] > i ? in[2] - i : 0);
    }
    texture.filterMask[0] = (unsigned char)in[3];
    texture.filterMask[1] = (unsigned char)in[4];
    texture.wrapMode[0] = (unsigned char)in[5];
    texture.wrapMode[1] = (unsigned char)in[6];
    texture.parmMask = (unsigned char)in[7];
    texture.numMipLevels = (short)in[8];
    texture.palette = &palette;
    state.textureUnit[2 * unit].texture = &texture;
    state.activeGradientMask = in[19];
    state.pixelPipeline = &pipe;
    pipe.xAdder[gradient] = in[13];
    pipe.yAdder[gradient] = in[14];
    pipe.xAdder[gradient + 1] = in[15];
    pipe.yAdder[gradient + 1] = in[16];
    grSetupTextureUnit(&state, unit, coordinates, in[17], in[18]);
    result = &pipe.textureUnit[unit];
    out[0] = (uint32_t)((uint32_t *)result->pixels - pixels);
    out[1] = (uint32_t)result->linear;
    out[2] = (uint32_t)result->sMask;
    out[3] = (uint32_t)result->tMask;
    out[4] = (uint32_t)result->rShift[0];
    out[5] = (uint32_t)result->rShift[1];
    out[6] = (uint32_t)result->log2DimS;
    out[7] = result->palette == &palette;
    memcpy(out + 8, pipe.reference, 40);
    memcpy(out + 18, pipe.xAdder, 40);
    memcpy(out + 28, pipe.yAdder, 40);
}

enum { LINE_WIDTH = 64, LINE_PIXELS = LINE_WIDTH * LINE_WIDTH,
       LINE_OUTPUT_WORDS = 3 + 2 * LINE_PIXELS, VERTEX_BUFFER_OFFSET = 1332 };
static uint32_t *line_capture;

static void capture_line_span(void *argument)
{
    _grPixelPipe *pipe = (_grPixelPipe *)argument;
    int row;
    ++line_capture[0];
    for (row = 0; row < pipe->numScanlines; ++row)
    {
        unsigned int span = pipe->scanlineInfo[row];
        unsigned int x = span & 0xFFFFu;
        unsigned int end = x + (span >> 16);
        unsigned int y = (unsigned int)(pipe->yStart + row);
        if (x != end && (end > LINE_WIDTH || y >= LINE_WIDTH))
        {
            line_capture[0] |= 0x80000000u;
            return;
        }
        for (; x < end; ++x)
        {
            unsigned int index = LINE_WIDTH * y + x;
            ++line_capture[1 + index];
            line_capture[1 + LINE_PIXELS + index] = (unsigned int)pipe->reference[2]
                + (unsigned int)pipe->xAdder[2] * x + (unsigned int)pipe->yAdder[2] * row;
        }
    }
}

void differential_raster_line(const int32_t *in, uint32_t *out)
{
    Gerbera_s state;
    _grPixelPipe pipe;
    _grClipTriangle triangle;
    union { uint64_t alignment; unsigned char bytes[GR_RENDER_STACK_SIZE]; } stack;
    _grVertex *vertices;
    unsigned char dummy;
    int i;
    memset(&state, 0, sizeof(state));
    memset(&pipe, 0, sizeof(pipe));
    memset(&stack, 0, sizeof(stack));
    memset(out, 0, LINE_OUTPUT_WORDS * sizeof(uint32_t));
    state.renderStack = stack.bytes;
    vertices = (_grVertex *)(stack.bytes + VERTEX_BUFFER_OFFSET);
    state.pixelPipeline = &pipe;
    state.pixelPipelineCode = capture_line_span;
    state.lineWidth = in[4];
    state.capabilities[GR_CAPABILITY_LINE_SMOOTH] = (unsigned char)in[5];
    state.capabilities[GR_CAPABILITY_PERSPECTIVE_CORRECTION] = (unsigned char)in[6];
    state.capabilities[8] = (unsigned char)in[7];
    state.capabilities[GR_CAPABILITY_UNUSED] = 1;
    state.capabilities[GR_CAPABILITY_SMOOTH_SHADING] = 1;
    state.activeGradientMask = 4;
    state.numActiveGradients = 1;
    state.activeGradientIndex[0] = 2;
    grSetRectangle(&state.renderScissor, 0, LINE_WIDTH, 0, LINE_WIDTH);
    for (i = 0; i < 3; ++i)
        state.drawSurface.buffers[i].data = &dummy;
    for (i = 0; i < 2; ++i)
    {
        vertices[i].screenPosition[0] = in[2 * i];
        vertices[i].screenPosition[1] = in[2 * i + 1];
        vertices[i].stateMask = 0x2C;
        vertices[i].clipPosition.e = 127;
        vertices[i].clipPosition.m[3] = in[8 + i];
        vertices[i].color.final[0][0] = (unsigned short)(i ? 0xF000 : 0x1000);
    }
    triangle.indices = grSetIndices(0, 1, 1);
    triangle.area = 1;
    triangle.side = 0;
    line_capture = out;
    grDrawClippedLine(&state, &triangle, triangle.indices);
    out[LINE_OUTPUT_WORDS - 2] = state.capabilities[GR_CAPABILITY_UNUSED];
    out[LINE_OUTPUT_WORDS - 1] = state.capabilities[GR_CAPABILITY_PERSPECTIVE_CORRECTION];
    line_capture = NULL;
}
