/* grApiSmall.c - state setters, queries, and core context lifecycle
 * (grApiSmall.inl).
 *
 * Assert sites keep the original grApiSmall.inl file/line strings.
 */
#include <stdint.h>
#include "gerbera/grState.h"
#include "gerbera/grRGBA.h"
#include "hg/hgInt32.h"
#include "hg/hgFill.h"
#include "hg/hgMemory.h"

#define GR_API_CHECK(cond, line)                 \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, "grApiSmall.inl", (line)) != 0) \
            grDie(0);                            \
    } while (0)

/* PE .rdata @ file+0x793D4 / 0x793DC: two-word capability masks. */
const int grCapChangesRejectAllState_affectingCapabilities[2] = { 98306, 0 };
const int grCapChangesPixelPipeline_affectingCapabilities[2] = { 32766, 0 };

/* --- capabilities (grUtil.h) ---------------------------------------------- */

int grTestCapability(Gerbera_s *state, int cap)
{
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 35);
    return state->capabilities[cap];
}

void grEnableCapability(Gerbera_s *state, int cap)
{
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 41);
    state->capabilities[cap] = 1;
}

void grDisableCapability(Gerbera_s *state, int cap)
{
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 47);
    state->capabilities[cap] = 0;
}

int grIsDepthOffsetEnabled(Gerbera_s *state, int primitiveType)
{
    GR_API_CHECK(state != NULL, 148);
    return grTestCapability(state, primitiveType + 11);
}

/* --- cull ------------------------------------------------------------------ */

void grUpdateCullMode(Gerbera_s *state)
{
    unsigned char cullNegative;
    unsigned char lightingSideMask;

    GR_API_CHECK(state != NULL, 37);
    cullNegative = (state->cullMask & 1) != 0;
    if (state->frontFace == 1)
        cullNegative = (state->cullMask & 1) == 0;
    state->cullNegative = cullNegative;
    lightingSideMask = 1;
    if (grTestCapability(state, GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE) != 0)
        lightingSideMask = 3;
    state->lightingSideMask = lightingSideMask;
}

/* --- depth range / scissor / viewport -------------------------------------- */

void grSetDepthRange(Gerbera_s *state, int n, int f)
{
    int zNear, zFar;

    GR_API_CHECK(state != NULL, 81);
    zNear = IScalar_toFixedHO(IScalar_clampUnit(n), 30);
    zFar = IScalar_toFixedHO(IScalar_clampUnit(f), 30);
    state->viewportScale.normal[2] = (zFar - zNear) >> 1;
    state->viewportOffset.normal[2] = (zNear + zFar + 1) >> 1;
    state->pixelPipelineDirty = 1;
    state->zNear = n;
    state->zFar = f;
}

void grUpdateScissor(Gerbera_s *state)
{
    int x0, y0, x1, y1;
    int x0a, y0a, x1a, y1a;

    GR_API_CHECK(state != NULL, 131);
    x0 = 0;
    y0 = 0;
    x1 = state->drawSurface.width;
    y1 = state->drawSurface.height;
    if (grTestCapability(state, GR_CAPABILITY_SCISSOR_TEST) != 0)
    {
        int x0b = hgMax32(0, state->scissorRectangle.x0);
        int y0b = hgMax32(0, state->scissorRectangle.y0);
        x1 = hgMin32(x1, state->scissorRectangle.x1);
        y1 = hgMin32(y1, state->scissorRectangle.y1);
        x0 = hgMin32(x0b, x1);
        y0 = hgMin32(y0b, y1);
    }
    grSetRectangle(&state->surfaceScissor, x0, x1, y0, y1);
    x0a = hgMax32(x0, state->viewportRectangle.x0);
    x1a = hgMin32(x1, state->viewportRectangle.x1);
    y0a = hgMax32(y0, state->viewportRectangle.y0);
    y1a = hgMin32(y1, state->viewportRectangle.y1);
    if (x0a > x1a || y0a > y1a)
    {
        y1a = 0;
        y0a = 0;
        x1a = 0;
        x0a = 0;
    }
    grSetRectangle(&state->renderScissor, x0a, x1a, y0a, y1a);
    GR_API_CHECK(grIsRectangleInRange(&state->renderScissor, 0, GR_MAX_DISPLAY_DIMENSION) != 0, 176);
    state->scissorDirty = 0;
    state->rejectAllStateDirty = 1;
}

void grSetScissor(Gerbera_s *state, int x0, int x1, int y0, int y1)
{
    GR_API_CHECK(state != NULL, 195);
    grSetRectangle(&state->scissorRectangle, x0, x1, y0, y1);
    grUpdateScissor(state);
}

void grSetViewport(Gerbera_s *state, int x0, int x1, int y0, int y1)
{
    GR_API_CHECK(state != NULL, 221);
    GR_API_CHECK(y0 <= y1 && x0 <= x1, 223);
    GR_API_CHECK(x1 - x0 <= GR_MAX_VIEWPORT_DIMENSION, 224);
    GR_API_CHECK(y1 - y0 <= GR_MAX_VIEWPORT_DIMENSION, 225);
    grSetRectangle(&state->viewportRectangle, x0, x1, y0, y1);
    state->viewportOffset.normal[0] = (int)(16u * (unsigned int)x0 + 8u * (unsigned int)(x1 - x0) + 8u);
    state->viewportOffset.normal[1] = (int)(16u * (unsigned int)y0 + 8u * (unsigned int)(y1 - y0) + 8u);
    state->viewportScale.normal[0] = imLsl32OF(x1 - x0, 6);
    state->viewportScale.normal[1] = imLsl32OF(y0 - y1, 6);
    grUpdateScissor(state);
}

/* --- capabilities ----------------------------------------------------------- */

int grCapChangesRejectAllState(int cap)
{
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 285);
    return ((unsigned int)grCapChangesRejectAllState_affectingCapabilities[cap >> 5] >> (cap & 0x1F)) & 1;
}

int grCapChangesPixelPipeline(int cap)
{
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 294);
    return ((unsigned int)grCapChangesPixelPipeline_affectingCapabilities[cap >> 5] >> (cap & 0x1F)) & 1;
}

void grSetCapability(Gerbera_s *state, int cap, int status)
{
    GR_API_CHECK(state != NULL, 303);
    GR_API_CHECK(hgInRange32(cap, 0, GR_CAPABILITY_MAX - 1), 304);
    GR_API_CHECK(grIsValidBool(status) != 0, 305);
    if (status != grTestCapability(state, cap))
    {
        int i;
        if (status != 0)
            grEnableCapability(state, cap);
        else
            grDisableCapability(state, cap);
        if (cap == GR_CAPABILITY_DITHER)
            grUpdateDither(state);
        if (cap == GR_CAPABILITY_SCISSOR_TEST)
            grUpdateScissor(state);
        if (hgInRange32(cap, 41, 48) != 0)
        {
            int ndx = cap - 41;
            state->lightActiveDirtyMask |= 1 << ndx;
            GR_API_CHECK(status == ((state->lightActiveMask & (1 << ndx)) == 0), 325);
            state->lightActiveMask ^= 1 << ndx;
        }
        if (cap == GR_CAPABILITY_COLOR_MATERIAL)
            state->lightColorDirtyMask = 0xFF;
        if (cap == GR_CAPABILITY_LIGHT_MODEL_TWO_SIDE)
            grUpdateCullMode(state);
        if (grCapChangesRejectAllState(cap) != 0)
            state->rejectAllStateDirty = 1;
        if (grCapChangesPixelPipeline(cap) != 0)
            state->pixelPipelineDirty = 1;
        if (cap == GR_CAPABILITY_RESCALE_NORMAL)
        {
            for (i = 8; i >= 0; --i)
                state->normalsToEyePalDirty[i] = 1;
            state->normalsToEyeDirty = 1;
        }
    }
}

int grGetCapability(Gerbera_s *state, int cap)
{
    GR_API_CHECK(state != NULL, 371);
    return grTestCapability(state, cap);
}

void grUpdateDither(Gerbera_s *state)
{
    unsigned char useDither = 0;

    if (grTestCapability(state, GR_CAPABILITY_DITHER) != 0 &&
        (((state->drawSurface.colorMask & 1) != 0 && hgInRange32(state->drawSurface.rgbaSizes[0], 1, 7) != 0) ||
         ((state->drawSurface.colorMask & 2) != 0 && hgInRange32(state->drawSurface.rgbaSizes[1], 1, 7) != 0) ||
         ((state->drawSurface.colorMask & 4) != 0 && hgInRange32(state->drawSurface.rgbaSizes[2], 1, 7) != 0) ||
         ((state->drawSurface.colorMask & 8) != 0 && hgInRange32(state->drawSurface.rgbaSizes[3], 1, 7) != 0)))
    {
        useDither = 1;
    }
    state->useDithering = useDither;
}

/* --- clears ------------------------------------------------------------------ */

void grSetClearColor(Gerbera_s *state, unsigned int color)
{
    GR_API_CHECK(state != NULL, 426);
    state->clearColor = color;
}

void grSetClearDepth(Gerbera_s *state, int depth)
{
    GR_API_CHECK(state != NULL, 455);
    GR_API_CHECK(depth >= 0, 456);
    state->clearDepth = (int)hgMulu64h(2u * (unsigned int)depth, 0xFFFFFFFFu);
}

void grSetClearStencil(Gerbera_s *state, unsigned int s)
{
    GR_API_CHECK(state != NULL, 469);
    state->clearStencil = s;
}

/* --- grClearBuffers (grApiSmall.inl 388; DLL 00443AD0) ------------------------ */

void grClearBuffers(Gerbera_s *state, unsigned int bufferMask)
{
    _grClearInfo *c;

    if (state->clearInfoIndex == GR_CLEAR_BUFFER_SIZE)
        grFlushBufferClears(state);
    GR_API_CHECK(hgInRange32(state->clearInfoIndex, 0, GR_CLEAR_BUFFER_SIZE - 1), 395);
    c = &state->clearInfos[state->clearInfoIndex++];
    GR_API_CHECK(state != NULL && bufferMask != 0, 399);
    c->bufferMask = (unsigned char)bufferMask;
    c->scissor = state->surfaceScissor;
    c->colorMask = (unsigned char)state->colorMask;
    c->depthMask = (unsigned char)state->depthMask;
    c->stencilWriteMask = (unsigned char)state->stencilWriteMask;
    c->clearColor = state->clearColor;
    c->clearDepth = state->clearDepth;
    c->clearStencil = state->clearStencil;
    c->useDithering = (unsigned char)state->useDithering;
}

/* --- cull mode ---------------------------------------------------------------- */

void grSetCullMode(Gerbera_s *state, int frontFace, unsigned int cullMask)
{
    GR_API_CHECK(state != NULL, 483);
    GR_API_CHECK(frontFace == GR_WINDING_CW || frontFace == GR_WINDING_CCW, 484);
    GR_API_CHECK((cullMask & 0xFFFFFFFCu) == 0, 485);
    state->frontFace = frontFace;
    state->cullMask = cullMask;
    grUpdateCullMode(state);
}

/* --- fog ----------------------------------------------------------------------- */

void grSetFogMode(Gerbera_s *state, int fogMode)
{
    GR_API_CHECK(state != NULL, 542);
    GR_API_CHECK(hgInRange32(fogMode, 0, GR_FOGMODE_MAX - 1), 543);
    state->fogMode = fogMode;
}

void grSetFogDensity(Gerbera_s *state, int density)
{
    GR_API_CHECK(state != NULL, 556);
    state->fogDensity = density;
}

void grSetFogRange(Gerbera_s *state, int fogStart, int fogEnd)
{
    int diff;

    GR_API_CHECK(state != NULL, 572);
    state->fogStart = fogStart;
    state->fogEnd = fogEnd;
    diff = IScalar_sub(state->fogEnd, state->fogStart);
    GR_API_CHECK(state != NULL, 579);
    if (IScalar_testeq(diff) != 0)
    {
        state->fogOORange = IScalar_zero();
        state->fogAtZero = 0xFFFF;
    }
    else
    {
        int v;
        state->fogOORange = IScalar_rcp(diff);
        v = IScalar_mul(fogEnd, state->fogOORange);
        v = IScalar_scalarToFixed(v, 16);
        state->fogAtZero = ~(unsigned int)(unsigned short)hgClamp32_0_x(v, 0xFFFF);
    }
}

void grSetFogColor(Gerbera_s *state, unsigned int fogColor)
{
    GR_API_CHECK(state != NULL, 606);
    state->fogColor = fogColor;
    state->pixelPipelineDirty = 1;
}

/* --- gradients ------------------------------------------------------------------ */

void grSetRectGradients(Gerbera_s *state, int gradients)
{
    GR_API_CHECK(state != NULL, 622);
    GR_API_CHECK(grIsValidBool(gradients) != 0, 623);
    if (state->screenRectGradients != gradients)
    {
        state->screenRectGradients = gradients;
        state->pixelPipelineDirty = 1;
    }
}

/* --- global ambient --------------------------------------------------------------- */

void grSetGlobalAmbient(Gerbera_s *state, _XColor3 *ambient)
{
    GR_API_CHECK(state != NULL, 764);
    if (ambient->r != state->ambientColor.r ||
        ambient->g != state->ambientColor.g ||
        ambient->b != state->ambientColor.b)
    {
        state->ambientColor = *ambient;
        state->ambientLightSumDirty = 1;
    }
}

/* --- material ------------------------------------------------------------------------ */

void grSetMaterialColor(Gerbera_s *state, int component, _XColor3 *color)
{
    GR_API_CHECK(state != NULL, 658);
    GR_API_CHECK(hgInRange32(component, 0, GR_MATERIALCOLOR_MAX - 1), 659);
    if (color->r != state->material.colors[component].r ||
        color->g != state->material.colors[component].g ||
        color->b != state->material.colors[component].b)
    {
        state->material.colors[component] = *color;
        if (component == GR_MATERIALCOLOR_EMISSION)
            state->baseColorDirty = 1;
        else
        {
            state->lightColorDirtyMask = 0xFF;
            if (component == GR_MATERIALCOLOR_AMBIENT)
                state->baseColorDirty = 1;
        }
    }
}

void grSetMaterialAlpha(Gerbera_s *state, unsigned short alpha)
{
    GR_API_CHECK(state != NULL, 708);
    GR_API_CHECK(hgInRange32(alpha, 0, 0xFFFF), 709);
    state->material.alpha = alpha;
}

void grSetMaterialShininess(Gerbera_s *state, int shininess)
{
    GR_API_CHECK(state != NULL, 740);
    GR_API_CHECK(IScalar_testge(shininess) != 0, 742);
    state->material.shininess = IScalar_toXScalar(shininess);
}

int grGetMaterialShininess(Gerbera_s *state)
{
    GR_API_CHECK(state != NULL, 752);
    return XScalar_toIScalar(state->material.shininess);
}

/* --- lights --------------------------------------------------------------------------- */

void grSetLightPosition(Gerbera_s *state, int ndx, _IFVector4 *position)
{
    _grLight *light;

    GR_API_CHECK(state != NULL, 788);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 789);
    light = &state->lights[ndx];
    light->positional = IFVector4_tov3(light->positionXYZ, position);
    if (light->positional == 0)
        XNormal3_normalize(light->nVP, light->positionXYZ[0].m);
    IFVector4_copy(&light->orgPosition, position);
    state->lightTypeDirtyMask |= 1u << ndx;
}

_IFVector4 *grGetLightOrgPosition(Gerbera_s *state, int ndx)
{
    GR_API_CHECK(state != NULL, 815);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 816);
    return &state->lights[ndx].orgPosition;
}

void grSetLightSpotDirection(Gerbera_s *state, int ndx, int *direction, _IFVector3 *orgDirection)
{
    GR_API_CHECK(state != NULL, 826);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 827);
    XNormal3_negate(state->lights[ndx].spotDirection, direction);
    IFVector3_copy(&state->lights[ndx].orgSpotDirection, orgDirection);
    state->lightTypeDirtyMask |= 1u << ndx;
}

_IFVector3 *grGetLightOrgDirection(Gerbera_s *state, int ndx)
{
    GR_API_CHECK(state != NULL, 845);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 846);
    return &state->lights[ndx].orgSpotDirection;
}

void grSetLightColor(Gerbera_s *state, int ndx, int component, _XColor3 *color)
{
    GR_API_CHECK(state != NULL, 856);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 857);
    GR_API_CHECK(hgInRange32(component, 0, 2), 858);
    state->lights[ndx].colorsx[component] = *color;
    state->lightColorDirtyMask |= 1u << ndx;
}

void grGetLightColor(Gerbera_s *state, int ndx, int component, _XColor3 *color)
{
    GR_API_CHECK(state != NULL, 870);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 871);
    GR_API_CHECK(hgInRange32(component, 0, 2), 872);
    *color = state->lights[ndx].colorsx[component];
}

void grSetLightAttenuation(Gerbera_s *state, int ndx, int component, int value)
{
    GR_API_CHECK(state != NULL, 882);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 883);
    GR_API_CHECK(hgInRange32(component, 0, 2), 884);
    GR_API_CHECK(IScalar_testge(value) != 0, 885);
    IFVector3_setf(&state->lights[ndx].attenuation, component, value);
    state->lightTypeDirtyMask |= 1u << ndx;
}

int grGetLightAttenuation(Gerbera_s *state, int ndx, int component)
{
    GR_API_CHECK(state != NULL, 897);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 898);
    GR_API_CHECK(hgInRange32(component, 0, 2), 899);
    return IFVector3_getf(&state->lights[ndx].attenuation, component);
}

void grSetLightExponent(Gerbera_s *state, int ndx, int exponent)
{
    GR_API_CHECK(state != NULL, 914);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 915);
    state->lights[ndx].spotExponent = IScalar_toXScalar(exponent);
    state->lightTypeDirtyMask |= 1u << ndx;
}

int grGetLightExponent(Gerbera_s *state, int ndx)
{
    GR_API_CHECK(state != NULL, 927);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 928);
    return XScalar_toIScalar(state->lights[ndx].spotExponent);
}

void grSetLightSpotCutoff(Gerbera_s *state, int ndx, int cutoff)
{
    int cos, zero;

    GR_API_CHECK(state != NULL, 938);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 939);
    state->lights[ndx].spotCutoff = cutoff;
    zero = IScalar_zero();
    cos = IScalar_cos(IScalar_degreesToRadians(cutoff));
    state->lights[ndx].cosSpotCutoff = IScalar_toXScalar(IScalar_max(cos, zero));
    state->lightTypeDirtyMask |= 1u << ndx;
}

int grGetLightSpotCutoff(Gerbera_s *state, int ndx)
{
    GR_API_CHECK(state != NULL, 952);
    GR_API_CHECK(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1), 953);
    return state->lights[ndx].spotCutoff;
}

/* --- alpha / stencil / depth / blend / masks ---------------------------------------- */

void grSetAlphaFunc(Gerbera_s *state, int func, int ref)
{
    GR_API_CHECK(state != NULL, 966);
    GR_API_CHECK(hgInRange32(func, 0, GR_FUNC_MAX - 1), 967);
    GR_API_CHECK(IScalar_testge(ref) != 0 && IScalar_cmple(ref, IScalar_one()) != 0, 968);
    if (func != state->alphaFunc)
    {
        state->pixelPipelineDirty = 1;
        state->rejectAllStateDirty = 1;
    }
    state->alphaFunc = func;
    state->alphaReference = hgClamp32_0_x(IScalar_scalarToFixed(ref, 8), 255);
}

void grSetStencilFunc(Gerbera_s *state, int func, unsigned int ref, unsigned int mask)
{
    GR_API_CHECK(state != NULL, 984);
    GR_API_CHECK(hgInRange32(func, 0, GR_FUNC_MAX - 1), 985);
    if (func != state->stencilFunc || mask != state->stencilTestMask)
    {
        state->stencilFunc = func;
        state->stencilTestMask = mask;
        state->pixelPipelineDirty = 1;
        state->rejectAllStateDirty = 1;
    }
    state->stencilReference = ref;
}

void grGetDrawBufferDescriptor(Gerbera_s *state, _grSurfaceDescriptor *desc)
{
    GR_API_CHECK(state != NULL && desc != NULL, 1008);
    /* whole-int copies across the packed descriptor prefix, like the decompile */
    *(int *)desc->rgbaOffsets = *(int *)state->drawSurface.rgbaOffsets;
    *(int *)desc->rgbaSizes = *(int *)state->drawSurface.rgbaSizes;
    *(int *)&desc->colorMask = *(int *)&state->drawSurface.colorMask;
    *(int *)&desc->pixelFormat = *(int *)&state->drawSurface.pixelFormat;
}

void grSetDepthOffset(Gerbera_s *state, int factor, int units)
{
    GR_API_CHECK(state != NULL, 1014);
    if (hgSign32(state->depthOffsetFactor) != hgSign32(factor) ||
        hgSign32(state->depthOffsetUnits) != hgSign32(units))
    {
        state->pixelPipelineDirty = 1;
    }
    state->depthOffsetFactor = factor;
    state->depthOffsetUnits = units;
}

void grSetStencilOp(Gerbera_s *state, int sFail, int dFail, int dPass)
{
    GR_API_CHECK(state != NULL, 1025);
    GR_API_CHECK(hgInRange32(sFail, 0, GR_STENCILOP_MAX - 1), 1026);
    GR_API_CHECK(hgInRange32(dFail, 0, GR_STENCILOP_MAX - 1), 1027);
    GR_API_CHECK(hgInRange32(dPass, 0, GR_STENCILOP_MAX - 1), 1028);
    if (state->stencilFail != sFail || state->stencilZFail != dFail || state->stencilZPass != dPass)
    {
        state->pixelPipelineDirty = 1;
        state->stencilFail = sFail;
        state->stencilZFail = dFail;
        state->stencilZPass = dPass;
    }
}

void grSetDepthFunc(Gerbera_s *state, int func)
{
    GR_API_CHECK(state != NULL, 1042);
    GR_API_CHECK(hgInRange32(func, 0, GR_FUNC_MAX - 1), 1043);
    if (state->depthFunc != func)
    {
        state->depthFunc = func;
        state->pixelPipelineDirty = 1;
    }
}

void grSetLogicOp(Gerbera_s *state, int logicOp)
{
    GR_API_CHECK(state != NULL, 1054);
    GR_API_CHECK(hgInRange32(logicOp, 0, GR_LOGICOP_MAX - 1), 1055);
    if (state->logicOp != logicOp)
    {
        state->logicOp = logicOp;
        state->pixelPipelineDirty = 1;
    }
}

void grSetBlendFunc(Gerbera_s *state, int srcFactor, int dstFactor)
{
    GR_API_CHECK(state != NULL, 1079);
    GR_API_CHECK(hgInRange32(srcFactor, 0, GR_BLENDFACTOR_MAX - 1), 1080);
    GR_API_CHECK(hgInRange32(dstFactor, 0, GR_BLENDFACTOR_MAX - 1), 1081);
    if (state->srcBlendFactor != srcFactor || state->dstBlendFactor != dstFactor)
    {
        state->srcBlendFactor = srcFactor;
        state->dstBlendFactor = dstFactor;
        state->pixelPipelineDirty = 1;
    }
}

void grSetColorMask(Gerbera_s *state, unsigned int mask)
{
    GR_API_CHECK(state != NULL, 1093);
    GR_API_CHECK((mask & 0xFFFFFFF0u) == 0, 1094);
    if (state->colorMask != mask)
    {
        state->colorMask = mask;
        state->pixelPipelineDirty = 1;
    }
}

void grSetDepthMask(Gerbera_s *state, int mask)
{
    GR_API_CHECK(state != NULL, 1104);
    GR_API_CHECK(grIsValidBool(mask) != 0, 1105);
    if (state->depthMask != mask)
    {
        state->depthMask = mask;
        state->pixelPipelineDirty = 1;
    }
}

void grSetStencilMask(Gerbera_s *state, unsigned int mask)
{
    GR_API_CHECK(state != NULL, 1115);
    if (state->stencilWriteMask != mask)
    {
        /* The original compares the full argument, then stores one byte. */
        state->stencilWriteMask = (unsigned char)mask;
        state->pixelPipelineDirty = 1;
    }
}

/* --- transform stack (decompile 41689-41713) ------------------------------- */

void grSetMatrix(Gerbera_s *state, int mode, _IMatrix4x4 *matrix)
{
    GR_API_CHECK(state != NULL, 1743);
    GR_API_CHECK(matrix != NULL, 1744);
    GR_API_CHECK(hgInRange32(mode, 0, GR_MATRIXMODE_MAX - 1) != 0, 1745);
    IMatrix4x4_copy(&state->matrix[mode], matrix);
    grSetMatrixDirty(state, mode, 1);
}

/* --- user clip planes (decompile 39699-39745) -------------------------------
 * The decompile's storage expressions are Hex-Rays type artifacts:
 *   equation:  IFVector4_copy(&state->normalsToEyePal[17][0].originalVertex
 *              + plane, equation)  ==  EPOC state+4648+20*plane
 *              == origUserClipPlanes[plane-GR_CLIP_USER0] (base +4848)
 *   matrix:    IMatrix4x4_copy(&state->texCoordTransform[8][0]
 *              .padding[80*plane - 1], modelview)  ==  EPOC
 *              state+4168+80*plane == userClipMatrix
 *              [plane-GR_CLIP_USER0] (base +4968).                        */

void grSetUserClipPlane(Gerbera_s *state, int plane, _IFVector4 *equation, _IMatrix4x4 *modelview)
{
    GR_API_CHECK(state != NULL, 503);
    GR_API_CHECK(hgInRange32(plane, GR_CLIP_USER0, GR_CLIP_USER0 + GR_MAX_USER_CLIP_PLANES - 1) != 0, 504);
    IFVector4_copy(&state->origUserClipPlanes[plane - GR_CLIP_USER0], equation);
    IMatrix4x4_copy(&state->userClipMatrix[plane - GR_CLIP_USER0], modelview);
}

_IFVector4 *grGetUserClipPlane(Gerbera_s *state, int plane)
{
    GR_API_CHECK(state != NULL, 521);
    GR_API_CHECK(hgInRange32(plane, GR_CLIP_USER0, GR_CLIP_USER0 + GR_MAX_USER_CLIP_PLANES - 1) != 0, 522);
    return &state->origUserClipPlanes[plane - GR_CLIP_USER0];
}

/* --- vertex array record access (decompile 41632-41687) --------------------- */

void *grGetVertexArrayPointer(Gerbera_s *state, int arrayType)
{
    GR_API_CHECK(state != NULL, 1709);
    GR_API_CHECK(hgInRange32(arrayType, 0, 8 - 1) != 0, 1710);
    return state->vertexArray[arrayType].origPointer;
}

unsigned int grGetVertexArrayBinding(Gerbera_s *state, int arrayType)
{
    GR_API_CHECK(state != NULL, 1719);
    GR_API_CHECK(hgInRange32(arrayType, 0, 8 - 1) != 0, 1720);
    return state->vertexArray[arrayType].binding;
}

void grResetVertexArray(Gerbera_s *state, int arrayType)
{
    GR_API_CHECK(state != NULL, 1729);
    GR_API_CHECK(hgInRange32(arrayType, 0, 8 - 1) != 0, 1730);
    state->vertexArray[arrayType].data = NULL;
}

/* --- grGetInteger / grGetIScalar (grApiSmall.inl 2270 / 2296) -------------- */

int grGetInteger(Gerbera_s *state, int value)
{
    GR_API_CHECK(state != NULL, 2270);
    switch (value)
    {
    case GR_QUERY_FRONT_FACE:
        return state->frontFace;
    case GR_QUERY_CULL_MASK:
        return state->cullMask;
    case GR_QUERY_STENCIL_REF:
        return state->stencilReference;
    case GR_QUERY_STENCIL_CLEAR:
        return state->clearStencil;
    default:
        if (grAssert("!\"grGetInteger(): invalid query value\"", "grApiSmall.inl", 2283) != 0)
            grDie(0);
        return 0;
    }
}

int grGetIScalar(Gerbera_s *state, int value)
{
    GR_API_CHECK(state != NULL, 2296);
    switch (value)
    {
    case GR_QUERY_FOG_START:
        return state->fogStart;
    case GR_QUERY_FOG_END:
        return state->fogEnd;
    default:
        if (grAssert("!\"grGetIScalar(): invalid query value\"", "grApiSmall.inl", 2305) != 0)
            grDie(0);
        return IScalar_zero();
    }
}

/* --- grSetActiveTexture / grSetTexEnv* / grSetTexCoordReplace -------------- */

void grSetActiveTexture(Gerbera_s *state, int ndx, _grTexture *texture)
{
    GR_API_CHECK(state != NULL, 1288);
    if (!(texture != NULL && grIsValidTexture(texture) != 0) &&
        grAssert("texture && grIsValidTexture(texture)", "grApiSmall.inl", 1289) != 0)
        grDie(0);
    if (hgInRange32(ndx, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(ndx, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1290) != 0)
        grDie(0);
    if (state->textureUnit[2 * ndx].texture != texture)
    {
        state->textureUnit[2 * ndx].texture = texture;
        state->pixelPipelineDirty = 1;
    }
}

void grSetTexEnvColor(Gerbera_s *state, int texUnit, unsigned int color)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1391) != 0)
        grDie(0);
    if (hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1392) != 0)
        grDie(0);
    state->textureUnit[2 * texUnit].texEnvColor = color;
}

void grSetTexEnvMode(Gerbera_s *state, int texIndex, int texEnvMode)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1398) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1399) != 0)
        grDie(0);
    if (hgInRange32(texEnvMode, 0, GR_TEXENVMODE_MAX - 1) == 0 &&
        grAssert("hgInRange32(texEnvMode, 0, GR_TEXENVMODE_MAX-1)", "grApiSmall.inl", 1400) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvMode != (unsigned char)texEnvMode)
    {
        state->textureUnit[2 * texIndex].texEnvMode = (unsigned char)texEnvMode;
        state->pixelPipelineDirty = 1;
    }
}

void grSetTexEnvRGBCombFunc(Gerbera_s *state, int texIndex, int func)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1414) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1415) != 0)
        grDie(0);
    if (hgInRange32(func, 0, GR_TEXENVCOMBFUNC_MAX - 1) == 0 &&
        grAssert("hgInRange32(func, 0, GR_TEXENVCOMBFUNC_MAX - 1)", "grApiSmall.inl", 1416) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvRGBCombFunc != (unsigned char)func)
    {
        state->textureUnit[2 * texIndex].texEnvRGBCombFunc = (unsigned char)func;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvRGBCombFunc(Gerbera_s *state, int texIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1429) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1430) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvRGBCombFunc;
}

void grSetTexEnvAlphaCombFunc(Gerbera_s *state, int texIndex, int func)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1440) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1441) != 0)
        grDie(0);
    if (hgInRange32(func, 0, GR_TEXENVCOMBFUNC_MAX - 1) == 0 &&
        grAssert("hgInRange32(func, 0, GR_TEXENVCOMBFUNC_MAX - 1)", "grApiSmall.inl", 1442) != 0)
        grDie(0);
    if (!(func != GR_TEXENVCOMBFUNC_DOT3_RGB && func != GR_TEXENVCOMBFUNC_DOT3_RGBA) &&
        grAssert("func != GR_TEXENVCOMBFUNC_DOT3_RGB && func != GR_TEXENVCOMBFUNC_DOT3_RGBA",
                 "grApiSmall.inl", 1444) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvAlphaCombFunc != (unsigned char)func)
    {
        state->textureUnit[2 * texIndex].texEnvAlphaCombFunc = (unsigned char)func;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvAlphaCombFunc(Gerbera_s *state, int texIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1457) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1458) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvAlphaCombFunc;
}

void grSetTexEnvRGBCombSrc(Gerbera_s *state, int texIndex, int srcIndex, int src)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1467) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1468) != 0)
        grDie(0);
    if (hgInRange32(srcIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(srcIndex, 0, 2)", "grApiSmall.inl", 1469) != 0)
        grDie(0);
    if (hgInRange32(src, 0, GR_TEXENVCOMBSRC_MAX - 1) == 0 &&
        grAssert("hgInRange32(src, 0, GR_TEXENVCOMBSRC_MAX - 1)", "grApiSmall.inl", 1470) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvRGBCombSrc[srcIndex] != (unsigned char)src)
    {
        state->textureUnit[2 * texIndex].texEnvRGBCombSrc[srcIndex] = (unsigned char)src;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvRGBCombSrc(Gerbera_s *state, int texIndex, int srcIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1483) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1484) != 0)
        grDie(0);
    if (hgInRange32(srcIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(srcIndex, 0, 2)", "grApiSmall.inl", 1485) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvRGBCombSrc[srcIndex];
}

void grSetTexEnvAlphaCombSrc(Gerbera_s *state, int texIndex, int srcIndex, int src)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1494) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1495) != 0)
        grDie(0);
    if (hgInRange32(srcIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(srcIndex, 0, 2)", "grApiSmall.inl", 1496) != 0)
        grDie(0);
    if (hgInRange32(src, 0, GR_TEXENVCOMBSRC_MAX - 1) == 0 &&
        grAssert("hgInRange32(src, 0, GR_TEXENVCOMBSRC_MAX - 1)", "grApiSmall.inl", 1497) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvAlphaCombSrc[srcIndex] != (unsigned char)src)
    {
        state->textureUnit[2 * texIndex].texEnvAlphaCombSrc[srcIndex] = (unsigned char)src;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvAlphaCombSrc(Gerbera_s *state, int texIndex, int srcIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1510) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1511) != 0)
        grDie(0);
    if (hgInRange32(srcIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(srcIndex, 0, 2)", "grApiSmall.inl", 1512) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvAlphaCombSrc[srcIndex];
}

void grSetTexEnvRGBCombOperand(Gerbera_s *state, int texIndex, int opIndex, int op)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1521) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1522) != 0)
        grDie(0);
    if (hgInRange32(opIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(opIndex, 0, 2)", "grApiSmall.inl", 1523) != 0)
        grDie(0);
    if (hgInRange32(op, 0, GR_TEXENVCOMBOPERAND_MAX - 1) == 0 &&
        grAssert("hgInRange32(op, 0, GR_TEXENVCOMBOPERAND_MAX - 1)", "grApiSmall.inl", 1524) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvRGBCombOperand[opIndex] != (unsigned char)op)
    {
        state->textureUnit[2 * texIndex].texEnvRGBCombOperand[opIndex] = (unsigned char)op;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvRGBCombOperand(Gerbera_s *state, int texIndex, int opIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1537) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1538) != 0)
        grDie(0);
    if (hgInRange32(opIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(opIndex, 0, 2)", "grApiSmall.inl", 1539) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvRGBCombOperand[opIndex];
}

void grSetTexEnvAlphaCombOperand(Gerbera_s *state, int texIndex, int opIndex, int op)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1548) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1549) != 0)
        grDie(0);
    if (hgInRange32(opIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(opIndex, 0, 2)", "grApiSmall.inl", 1550) != 0)
        grDie(0);
    if (!(op == GR_TEXENVCOMBOPERAND_SRC_ALPHA || op == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA) &&
        grAssert("op == GR_TEXENVCOMBOPERAND_SRC_ALPHA || op == GR_TEXENVCOMBOPERAND_ONE_MINUS_SRC_ALPHA",
                 "grApiSmall.inl", 1552) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvAlphaCombOperand[opIndex] != (unsigned char)op)
    {
        state->textureUnit[2 * texIndex].texEnvAlphaCombOperand[opIndex] = (unsigned char)op;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvAlphaCombOperand(Gerbera_s *state, int texIndex, int opIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1565) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1566) != 0)
        grDie(0);
    if (hgInRange32(opIndex, 0, 2) == 0 &&
        grAssert("hgInRange32(opIndex, 0, 2)", "grApiSmall.inl", 1567) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvAlphaCombOperand[opIndex];
}

void grSetTexEnvRGBCombScale(Gerbera_s *state, int texIndex, int scale)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1576) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1577) != 0)
        grDie(0);
    if (!(scale == 1 || scale == 2 || scale == 4) &&
        grAssert("scale == 1 || scale == 2 || scale == 4", "grApiSmall.inl", 1578) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvRGBCombScale != (unsigned char)scale)
    {
        state->textureUnit[2 * texIndex].texEnvRGBCombScale = (unsigned char)scale;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvRGBCombScale(Gerbera_s *state, int texIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1591) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1592) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvRGBCombScale;
}

void grSetTexEnvAlphaCombScale(Gerbera_s *state, int texIndex, int scale)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1601) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1602) != 0)
        grDie(0);
    if (!(scale == 1 || scale == 2 || scale == 4) &&
        grAssert("scale == 1 || scale == 2 || scale == 4", "grApiSmall.inl", 1603) != 0)
        grDie(0);
    if (state->textureUnit[2 * texIndex].texEnvAlphaCombScale != (unsigned char)scale)
    {
        state->textureUnit[2 * texIndex].texEnvAlphaCombScale = (unsigned char)scale;
        state->pixelPipelineDirty = 1;
    }
}

int grGetTexEnvAlphaCombScale(Gerbera_s *state, int texIndex)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1616) != 0)
        grDie(0);
    if (hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texIndex, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1617) != 0)
        grDie(0);
    return state->textureUnit[2 * texIndex].texEnvAlphaCombScale;
}

int grGetTexEnvMode(Gerbera_s *state, int texUnit)
{
    if (hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1626) != 0)
        grDie(0);
    return state->textureUnit[2 * texUnit].texEnvMode;
}

unsigned int grGetTexEnvColor(Gerbera_s *state, int texUnit)
{
    if (hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1632) != 0)
        grDie(0);
    return state->textureUnit[2 * texUnit].texEnvColor;
}

void grSetTexCoordReplace(Gerbera_s *state, int texUnit, int replace)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1638) != 0)
        grDie(0);
    if (hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1639) != 0)
        grDie(0);
    if (!(replace == 1 || replace == 0) &&
        grAssert("replace == HG_TRUE || replace == HG_FALSE", "grApiSmall.inl", 1640) != 0)
        grDie(0);
    state->textureUnit[2 * texUnit].texCoordReplace = replace;
}

int grGetTexCoordReplace(Gerbera_s *state, int texUnit)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1647) != 0)
        grDie(0);
    if (hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(texUnit, 0, GR_MAX_TEXTURE_UNITS - 1)", "grApiSmall.inl", 1648) != 0)
        grDie(0);
    return state->textureUnit[2 * texUnit].texCoordReplace;
}

/* --- grSetVertexArray / point & line state / default vertex attribs -------- */

void grSetVertexArray(Gerbera_s *state, int arrayType, int dataType, int numComponents,
                      int stride, void *pointer, unsigned int binding, void *origPointer)
{
    _grVertexArray *array;

    if (state == NULL && grAssert("state", "grApiSmall.inl", 1664) != 0)
        grDie(0);
    if (hgInRange32(arrayType, 0, GR_VERTEXARRAY_MAX - 1) == 0 &&
        grAssert("hgInRange32(arrayType, 0, GR_VERTEXARRAY_MAX-1)", "grApiSmall.inl", 1665) != 0)
        grDie(0);
    if (hgInRange32(dataType, 0, 8) == 0 &&
        grAssert("hgInRange32(dataType, 0, GR_DATATYPE_MAX-1)", "grApiSmall.inl", 1666) != 0)
        grDie(0);
    if (hgInRange32(numComponents, 1, 4) == 0 &&
        grAssert("hgInRange32(numComponents, 1, 4)", "grApiSmall.inl", 1667) != 0)
        grDie(0);
    switch (arrayType)
    {
    case GR_VERTEXARRAY_POSITION:
    case GR_VERTEXARRAY_COLOR:
    case GR_VERTEXARRAY_WEIGHT:
    case GR_VERTEXARRAY_MATRIX_INDEX:
        break;
    case GR_VERTEXARRAY_NORMAL:
        if (numComponents != 3 && grAssert("numComponents == 3", "grApiSmall.inl", 1684) != 0)
            grDie(0);
        break;
    case GR_VERTEXARRAY_POINT_SIZE:
        if (numComponents != 1 && grAssert("numComponents == 1", "grApiSmall.inl", 1679) != 0)
            grDie(0);
        break;
    default:
        if (hgInRange32(arrayType, GR_VERTEXARRAY_TEXCOORD0,
                        GR_VERTEXARRAY_TEXCOORD0 + GR_MAX_TEXTURE_UNITS - 1) == 0 &&
            grAssert("hgInRange32(arrayType, GR_VERTEXARRAY_TEXCOORD0, GR_VERTEXARRAY_TEXCOORD0+GR_MAX_TEXTURE_UNITS-1)",
                     "grApiSmall.inl", 1688) != 0)
            grDie(0);
        break;
    }
    array = &state->vertexArray[arrayType];
    array->type = (unsigned char)dataType;
    array->size = (unsigned char)numComponents;
    array->stride = stride;
    array->data = pointer;
    array->binding = binding;
    array->origPointer = origPointer;
}

void grSetPointSize(Gerbera_s *state, int size)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1757) != 0)
        grDie(0);
    if (hgInRange32(size, 0, GR_MAX_POINT_SIZE << 16) == 0 &&
        grAssert("hgInRange32(size, 0, GR_MAX_POINT_SIZE<<16)", "grApiSmall.inl", 1758) != 0)
        grDie(0);
    state->pointSize = size;
}

void grSetLineWidth(Gerbera_s *state, int width)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1764) != 0)
        grDie(0);
    if (hgInRange32(width, 0, GR_MAX_LINE_WIDTH << 16) == 0 &&
        grAssert("hgInRange32(width, 0, GR_MAX_LINE_WIDTH<<16)", "grApiSmall.inl", 1765) != 0)
        grDie(0);
    state->lineWidth = width;
}

void grSetPointSizeMin(Gerbera_s *state, int min)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1771) != 0)
        grDie(0);
    state->pointSizeMin = min;
}

void grSetPointSizeMax(Gerbera_s *state, int max)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1778) != 0)
        grDie(0);
    state->pointSizeMax = max;
}

void grSetPointFadeThreshold(Gerbera_s *state, int size)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1785) != 0)
        grDie(0);
    state->pointFadeThreshold = size;
}

void grSetPointDistAttn(Gerbera_s *state, int *attn)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1792) != 0)
        grDie(0);
    if (attn == NULL && grAssert("attn", "grApiSmall.inl", 1793) != 0)
        grDie(0);
    if (IScalar_testge(attn[0]) == 0 && grAssert("IScalar_testge(attn[0])", "grApiSmall.inl", 1794) != 0)
        grDie(0);
    if (IScalar_testge(attn[1]) == 0 && grAssert("IScalar_testge(attn[1])", "grApiSmall.inl", 1795) != 0)
        grDie(0);
    if (IScalar_testge(attn[2]) == 0 && grAssert("IScalar_testge(attn[2])", "grApiSmall.inl", 1796) != 0)
        grDie(0);
    IFVector3_setfv3(&state->pointDistAttn, attn);
}

void grSetVertexColor(Gerbera_s *state, _XColor3 *color, unsigned short alpha)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1813) != 0)
        grDie(0);
    state->defaultColor = *color;
    if (grTestCapability(state, GR_CAPABILITY_COLOR_MATERIAL) != 0)
    {
        grSetMaterialColor(state, GR_MATERIALCOLOR_AMBIENT, color);
        grSetMaterialColor(state, GR_MATERIALCOLOR_DIFFUSE, color);
    }
    XColor3_tousv(state->defaultColorRGBA, &state->defaultColor);
    state->defaultColorRGBA[3] = alpha;
}

void grSetVertexNormal(Gerbera_s *state, int *normal)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1829) != 0)
        grDie(0);
    XNormal3_copy(state->defaultNormal, normal);
}

void grSetVertexTexCoord(Gerbera_s *state, int ndx, int *texCoord)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 1835) != 0)
        grDie(0);
    if (texCoord == NULL && grAssert("texCoord", "grApiSmall.inl", 1836) != 0)
        grDie(0);
    if (hgInRange32(ndx, 0, GR_MAX_TEXTURE_UNITS - 1) == 0 &&
        grAssert("hgInRange32(ndx, 0, GR_MAX_TEXTURE_UNITS-1)", "grApiSmall.inl", 1837) != 0)
        grDie(0);
    state->defaultTexCoord.x[ndx][0] = texCoord[0];
    state->defaultTexCoord.x[ndx][1] = texCoord[1];
    state->defaultTexCoord.x[ndx][2] = texCoord[2];
    state->defaultTexCoord.x[ndx][3] = texCoord[3];
}

/* --- grScratchGrowMemory / grCreateGerbera / grDestroyGerbera / surfaces --- */

static const int scratchSizes[7] =
{
    30720, 40960, 51200, 81920, 102400, 153600, 204800
};

int grScratchGrowMemory(Gerbera_s *state)
{
    int currSize;
    void *scratch;
    int i;
    int nextSize;
    void *nextScratch;

    if (state == NULL && grAssert("state", "grApiSmall.inl", 1858) != 0)
        grDie(0);
    currSize = state->scratchMemorySize;
    scratch = state->scratchMemory;
    if (currSize != 0)
    {
        if (scratch == NULL && grAssert("scratch", "grApiSmall.inl", 1887) != 0)
            grDie(0);
        if ((currSize & 1023) != 0 && grAssert("!(currSize & 1023)", "grApiSmall.inl", 1888) != 0)
            grDie(0);
        for (i = 0; currSize != scratchSizes[i]; ++i)
        {
            if (i >= 6 && grAssert("i < nSizes - 1", "grApiSmall.inl", 1900) != 0)
                grDie(0);
        }
        if (i == 6)
            return 0;
        if (grStackGetFreeSpace(&state->scratchAllocator) != currSize &&
            grAssert("grStackGetFreeSpace(&state->scratchAllocator) == currSize",
                     "grApiSmall.inl", 1908) != 0)
            grDie(0);
        if (grStackGetPtr(&state->scratchAllocator, 0) != scratch &&
            grAssert("grStackGetPtr(&state->scratchAllocator, 0) == scratch",
                     "grApiSmall.inl", 1909) != 0)
            grDie(0);
        eglFree(scratch);
    }
    else
    {
        if (scratch != NULL && grAssert("!scratch", "grApiSmall.inl", 1919) != 0)
            grDie(0);
        i = -1;
    }
    nextSize = scratchSizes[i + 1];
    nextScratch = eglAlloc((unsigned int)nextSize);
    if (nextScratch != NULL)
        grStackInit(&state->scratchAllocator, nextScratch, nextSize);
    else
        nextSize = 0;
    state->scratchMemory = nextScratch;
    state->scratchMemorySize = nextSize;
#if HG_ASSERT_ENABLED
    if (nextScratch != NULL)
    {
        int j;
        for (j = 0; j < nextSize; ++j)
            ((unsigned char *)nextScratch)[j] = 0xABu;
    }
#endif
    return nextScratch != NULL;
}

Gerbera_s *grCreateGerbera(int workMemorySize, unsigned char *workMemory,
                           unsigned char *sharedWorkMem, int sharedWorkMemSize)
{
    unsigned int magic0 = 0xDEADBEEFu;
    unsigned int magic1 = 0xC001D00Eu;
    int stateBytes = ((int)sizeof(Gerbera_s) + 7) & ~7;
    int entryBytes = GR_PIPELINE_CACHE_ENTRIES * (((int)sizeof(_grCacheEntry) + 7) & ~7);
    Gerbera_s *state;
    _grCacheEntry *entries;
    grExecutable_s *exec;
    int i, c;

    (void)sharedWorkMem;
    (void)sharedWorkMemSize;
    (void)i;

    if (workMemorySize < 0 && grAssert("workMemorySize >= 0", "grApiSmall.inl", 1978) != 0)
        grDie(0);
    if (workMemory == NULL && grAssert("workMemory", "grApiSmall.inl", 1979) != 0)
        grDie(0);
    if (((uintptr_t)workMemory & 3u) != 0 &&
        grAssert("!((HGuint32)workMemory & 3)", "grApiSmall.inl", 1980) != 0)
        grDie(0);

#if HG_ASSERT_ENABLED
    for (i = 0; i < workMemorySize; ++i)
        workMemory[i] = 0xABu;
#endif

    state = (Gerbera_s *)workMemory;
    hgFill8(workMemory, 0, stateBytes);
    grStackInit(&state->allocator, workMemory, workMemorySize);
    grStackAllocate(&state->allocator, stateBytes);
    entries = (_grCacheEntry *)grStackAllocate(&state->allocator, entryBytes);

    exec = eglAllocExecutable(GR_PIPELINE_CACHE_BYTES);
    state->pipelineCacheExecutable = exec;
    if (exec == NULL)
        return NULL;

    state->pipelineCache = &state->pipelineCacheStorage;
    grCacheInit(state->pipelineCache, eglGetExecutableData(exec), entries,
                eglGetExecutableSize(exec), GR_PIPELINE_CACHE_ENTRIES);

    state->setjmpConfigStorage.longjmp = eglHCWrapLongjmp;
    state->setjmpConfigStorage.setjmpWrap = eglHCWrapSetjmp;
    state->setjmpConfigStorage.jmpbufSize = (unsigned int)eglGetJmpbufSize();
    state->setjmpConfig = &state->setjmpConfigStorage;

    if (!(state->scratchMemory == NULL && state->scratchMemorySize == 0) &&
        grAssert("!state->scratchMemory && !state->scratchMemorySize", "grApiSmall.inl", 2030) != 0)
        grDie(0);

    if (grScratchGrowMemory(state) == 0)
    {
        eglFreeExecutable((grExecutable_s *)state->pipelineCacheExecutable);
        return NULL;
    }

    state->rejectAllStateDirty = 1;
    state->pixelPipelineDirty = 1;
    state->vertexPipelineDirty = 1;
    state->lineWidth = 0x10000;
    state->pointSize = 0x10000;
    state->pointFadeThreshold = 0;
    IFVector3_init100(&state->pointDistAttn);
    state->computePointDistAttn = 0;
    state->colorMask = 0xFu;
    state->stencilTestMask = 0xFFu;
    state->stencilWriteMask = 0xFFu;
    state->alphaFunc = GR_FUNC_ALWAYS;
    state->depthFunc = GR_FUNC_LESS;
    state->depthMask = 1;
    state->cullMask = 2u;
    state->frontFace = GR_WINDING_CCW;
    state->srcBlendFactor = GR_BLENDFACTOR_ONE;
    state->dstBlendFactor = GR_BLENDFACTOR_ZERO;
    state->logicOp = GR_LOGICOP_COPY;
    state->clearDepth = 0x3FFFFFFF;
    state->pixelPipelineDirty = 1;
    state->vertexArray[GR_VERTEXARRAY_NORMAL].size = 3;
    state->defaultColor.r = 0x200000;
    state->defaultColor.g = 0x200000;
    state->defaultColor.b = 0x200000;
    state->defaultColorRGBA[0] = 0xFFFFu;
    state->defaultColorRGBA[1] = 0xFFFFu;
    state->defaultColorRGBA[2] = 0xFFFFu;
    state->defaultColorRGBA[3] = 0xFFFFu;
    XNormal3_init001p(state->defaultNormal);

    for (i = GR_MAX_TEXTURE_UNITS - 1; i >= 0; --i)
    {
        _grTextureUnitState *tu = &state->textureUnit[2 * i];
        tu->texEnvMode = GR_TEXENVMODE_MODULATE;
        tu->texCoordReplace = 0;
        tu->texEnvRGBCombScale = 1;
        tu->texEnvAlphaCombScale = 1;
        state->defaultTexCoord.x[i][0] = IScalar_zero();
        state->defaultTexCoord.x[i][1] = IScalar_zero();
        state->defaultTexCoord.x[i][2] = IScalar_zero();
        state->defaultTexCoord.x[i][3] = IScalar_one();
    }

    state->fogMode = GR_FOGMODE_EXP;
    state->fogStart = IScalar_zero();
    state->fogEnd = IScalar_one();
    state->fogDensity = IScalar_one();
    state->fogOORange = IScalar_one();
    state->fogAtZero = 0;

    state->lightTypeDirtyMask = 0xFFu;
    state->lightDirectionalMask = 0;
    state->lightColorDirtyMask = 0xFFu;
    state->lightActiveMask = 0;
    state->lightActiveDirtyMask = 0xFFu;
    state->ambientLightSumDirty = 1;
    state->ambientLightSumMask = 0;
    state->baseColorDirty = 1;

    for (c = 0; c < 4; ++c)
        XColor3_zero(&state->material.colors[c]);
    state->material.shininess = 0;

    for (i = 0; i < GR_MAX_LIGHTS; ++i)
    {
        _grLight *light = &state->lights[i];
        for (c = 0; c <= 2; ++c)
            XColor3_zero(&light->colorsx[c]);
        IVector3_init001p(light->positionXYZ);
        light->positional = 0;
        XNormal3_init001n(light->spotDirection);
        IFVector3_init100(&light->attenuation);
        light->spotExponent = 0;
        light->spotCutoff = IScalar_immediate(180.0f);
    }

    grEnableCapability(state, GR_CAPABILITY_SMOOTH_SHADING);
    grSetViewport(state, 0, 0, 0, 0);
    grSetScissor(state, 0, 0, 0, 0);
    grSetDepthRange(state, IScalar_zero(), IScalar_one());
    grUpdateCullMode(state);
    grrSelfTest();

    if (!(magic0 == 0xDEADBEEFu && magic1 == 0xC001D00Eu) &&
        grAssert("__grMemoryCheck[0]==GR_STACK_CHECK_MAGIC0 && __grMemoryCheck[1]==GR_STACK_CHECK_MAGIC1",
                 "grApiSmall.inl", 2195) != 0)
        grDie(0);

    return state;
}

void grDestroyGerbera(Gerbera_s *state)
{
    unsigned char *allocData;
    int allocSize;

    if (state == NULL && grAssert("state", "grApiSmall.inl", 2203) != 0)
        grDie(0);
    eglFreeExecutable((grExecutable_s *)state->pipelineCacheExecutable);
    if (state->scratchMemory != NULL)
    {
        if (state->scratchMemorySize == 0 &&
            grAssert("state->scratchMemorySize", "grApiSmall.inl", 2209) != 0)
            grDie(0);
        eglFree(state->scratchMemory);
    }
    allocData = (unsigned char *)state->allocator.data;
    allocSize = state->allocator.size;
    hgFill8(allocData, 0xBEu, allocSize);
}

int grIsValidSurfaceInfo(_grDrawSurface *info)
{
    if (info == NULL && grAssert("info", "grApiSmall.inl", 2233) != 0)
        grDie(0);
    if (grIsValidDescriptor((_grSurfaceDescriptor *)info) == 0 &&
        grAssert("grIsValidDescriptor(&info->descriptor)", "grApiSmall.inl", 2236) != 0)
        grDie(0);
    return 1;
}

void grSetSurfaces(Gerbera_s *state, _grDrawSurface *drawInfo, _grDrawSurface *readInfo)
{
    if (state == NULL && grAssert("state", "grApiSmall.inl", 2243) != 0)
        grDie(0);
    if (grIsValidSurfaceInfo(drawInfo) == 0 &&
        grAssert("grIsValidSurfaceInfo(drawInfo)", "grApiSmall.inl", 2244) != 0)
        grDie(0);
    if (grIsValidSurfaceInfo(readInfo) == 0 &&
        grAssert("grIsValidSurfaceInfo(readInfo)", "grApiSmall.inl", 2245) != 0)
        grDie(0);
    hgMemCopy(&state->drawSurface, drawInfo, (unsigned int)sizeof(_grDrawSurface));
    hgMemCopy(&state->readSurface, readInfo, (unsigned int)sizeof(_grDrawSurface));
    grUpdateDither(state);
    grUpdateScissor(state);
    state->pixelPipelineDirty = 1;
}

int grGetStatistic(int stat)
{
    (void)stat;
    return 0;
}

void grResetStatistics(void)
{
}
